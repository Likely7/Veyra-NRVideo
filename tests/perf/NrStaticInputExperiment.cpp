#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <bcrypt.h>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <algorithm>
#include <chrono>

// Counterexample/measurement for plan 1a: the actual graph receives the same
// decoded natural image 300 times with genuine increasing PTS/sequence.
// No candidate shader or invented motion/model substitutes are used here.
namespace {
std::string sha256(const std::vector<uint8_t>& pixels){
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    std::array<unsigned char,32> digest{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0&&
        BCryptHashData(hash,const_cast<PUCHAR>(pixels.data()),ULONG(pixels.size()),0)>=0&&
        BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(!ok)return {};const char digits[]="0123456789abcdef";std::string text;
    for(auto b:digest){text+=digits[b>>4];text+=digits[b&15];}return text;
}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc!=4)return 2;
    const std::wstring mode=argv[3];
    if(mode!=L"nr"&&mode!=L"nr-temporal"&&mode!=L"srnr"&&mode!=L"sr"&&mode!=L"off"&&mode!=L"nr-reset"&&mode!=L"srnr-reset")return 2;
    const bool nr=mode!=L"off"&&mode!=L"sr",sr=mode==L"srnr"||mode==L"sr"||mode==L"srnr-reset";
    const bool resetEveryFrame=mode==L"nr-reset"||mode==L"srnr-reset";
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!media.open(open))return 2;
    pipeline::FramePacket packet;const AVFrame* input=nullptr;
    if(media.read(packet,&input)!=source::SourceReadStatus::Frame)return 2;
    // NR temporal control is the real product implementation; model/runtime
    // state may advance even when that optional control is disabled.
    engine::EffectChain chain;chain.nodeCount=1;chain.nodes[0].type=engine::EffectType::NrEnhance;
    chain.nodes[0].enabled=nr;chain.nodes[0].nr.temporal=mode==L"nr-temporal";
    chain.nodes[0].nr.sizePolicy=pipeline::NrSizePolicy::Realtime;
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);
    settings.sr=sr;settings.videoSrQuality=0;settings.srTarget=pipeline::SrTarget::Uhd4K;
    settings.multiplier=1;
    engine::StageRequest request;request.width=media.info().width;request.height=media.info().height;request.nr=nr;request.sr=sr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
    desc.enableNvofStandalone=nr||sr;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    pipeline::EnhanceGraph graph(ctx,ring); // deliberately no core cache
    if(!graph.initialize(desc)||!graph.createViews())return 2;
    std::ofstream csv(out/L"frames.csv");csv<<"frame,ptsMs,sha256,equalFirst,equalPrevious,differentBytesToFirst,maeToFirst,processCpuWithWaitMs\n";
    std::vector<uint8_t> first,previous;unsigned differing=0;bool pass=true;
    const unsigned snapshots[]={0,1,2,4,8,16,32,64,127,255,299};
    for(unsigned i=0;i<300&&pass;++i){
        pipeline::EnhanceGraph::FrameOutputs output;const auto start=std::chrono::steady_clock::now();
        pass=graph.process(input,(i+1)*1000.0/60,i==0||resetEveryFrame,output,i+1,&packet.colorInfo)&&ring.waitIdle();
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        sink::RgbaImage image;if(pass)pass=sink::readRgba8(ctx,ring,graph.videoFrameResource(output.videoSlot),image);
        if(!pass)break;
        if(i==0)first=image.pixels;
        const bool equalFirst=first==image.pixels,equalPrevious=i==0||previous==image.pixels;
        uint64_t differentBytes=0,absolute=0;
        for(size_t b=0;b<first.size();++b){differentBytes+=first[b]!=image.pixels[b];absolute+=unsigned(std::abs(int(first[b])-int(image.pixels[b])));}
        differing+=!equalFirst;
        const auto hash=sha256(image.pixels);if(hash.empty()){pass=false;break;}
        csv<<i<<','<<(i+1)*1000.0/60<<','<<hash<<','<<equalFirst<<','<<equalPrevious<<','<<differentBytes<<','<<double(absolute)/first.size()<<','<<ms<<'\n';csv.flush();
        if(std::find(std::begin(snapshots),std::end(snapshots),i)!=std::end(snapshots)){
            std::ofstream f(out/(L"frame-"+std::to_wstring(i)+L".rgba"),std::ios::binary);
            f.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));
        }
        previous=std::move(image.pixels);
        if(i%60==0)std::cout<<"FRAME "<<i<<" equalFirst="<<equalFirst<<" differentBytes="<<differentBytes<<" size="<<image.width<<'x'<<image.height<<std::endl;
    }
    const auto metrics=graph.metrics();
    pass=pass&&metrics.nrEvaluateCount==(nr?300u:0u)&&metrics.srEvaluateCount==(sr?300u:0u);
    ring.drainQueue();graph.shutdown();media.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&size)))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();
    std::cout<<"RESULT safetyPass="<<(pass&&errors==0&&SUCCEEDED(removed))<<" differentOutputFrames="<<differing
             <<" nrEvaluations="<<metrics.nrEvaluateCount<<" srEvaluations="<<metrics.srEvaluateCount
             <<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    // Exit success means the measurement is valid, not that reuse is valid.
    return pass&&errors==0&&SUCCEEDED(removed)?0:1;
}
