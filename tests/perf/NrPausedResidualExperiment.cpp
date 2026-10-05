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
#include <fstream>
#include <iostream>
#include <array>
#include <chrono>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
}
namespace {
std::string sha(const std::vector<uint8_t>& pixels){BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> digest{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0&&BCryptHashData(hash,const_cast<PUCHAR>(pixels.data()),ULONG(pixels.size()),0)>=0&&BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);if(!ok)return {};std::string s;const char* hex="0123456789abcdef";for(auto c:digest){s+=hex[c>>4];s+=hex[c&15];}return s;}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=4)return 2;const std::wstring mode=argv[3];const bool sr=mode==L"srnr",temporal=mode==L"temporal",multiple=mode==L"layers";
    if(mode!=L"nr"&&!sr&&!temporal&&!multiple)return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
    if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!media.open(open))return 2;
    pipeline::FramePacket packet;const AVFrame* frame=nullptr;if(media.read(packet,&frame)!=source::SourceReadStatus::Frame)return 2;
    engine::EffectChain chain;chain.nodeCount=multiple?2:1;
    for(unsigned i=0;i<chain.nodeCount;++i){chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].enabled=true;chain.nodes[i].nr.temporal=temporal;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=sr;settings.srTarget=pipeline::SrTarget::Uhd4K;settings.videoSrQuality=0;settings.multiplier=1;
    engine::StageRequest request;request.width=frame->width;request.height=frame->height;request.nr=true;request.sr=sr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.enableNvofStandalone=true;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    pipeline::EnhanceGraph graph(ctx,ring);if(!graph.initialize(desc)||!graph.createViews())return 2;
    std::ofstream csv(out/L"edits.csv");csv<<"edit,modelChanged,total,processCpuWithWaitMs,nrDelta,srDelta,sha256\n";bool pass=true;
    for(unsigned i=0;i<300&&pass;++i){
        settings.nrLayers[0].residual.total=settings.residual.total=float(i%10)*.2f;
        // A model edit in the middle must invalidate the resident NR result.
        if(i==150)settings.nrLayers[0].model.intensity=settings.model.intensity=.7f;
        pass=graph.applySettings(settings);if(!pass)break;const auto before=graph.metrics();pipeline::EnhanceGraph::FrameOutputs output;
        const auto started=std::chrono::steady_clock::now();
#ifdef VEYRA_HAS_PAUSED_NR_RESIDUAL_REUSE
        pass=graph.process(frame,0,true,output,1,&packet.colorInfo,nullptr,false,{},0,true)&&ring.waitIdle();
#else
        pass=graph.process(frame,0,true,output,1,&packet.colorInfo,nullptr,false)&&ring.waitIdle();
#endif
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();const auto after=graph.metrics();sink::RgbaImage image;
        if(pass)pass=sink::readRgba8(ctx,ring,graph.videoFrameResource(output.videoSlot),image);if(!pass)break;
        const auto hash=sha(image.pixels);if(hash.empty()){pass=false;break;}
        csv<<i<<','<<(i==150)<<','<<settings.residual.total<<','<<ms<<','<<after.nrEvaluateCount-before.nrEvaluateCount<<','<<after.srEvaluateCount-before.srEvaluateCount<<','<<hash<<'\n';csv.flush();
        if(i==0||i==1||i==10||i==149||i==150||i==151||i==299){std::ofstream f(out/(L"frame-"+std::to_wstring(i)+L".rgba"),std::ios::binary);f.write(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());}
        output={};if(i%60==0)std::cout<<"EDIT "<<i<<" ms="<<ms<<" nrDelta="<<after.nrEvaluateCount-before.nrEvaluateCount<<std::endl;
    }
    const auto metrics=graph.metrics();ring.drainQueue();graph.shutdown();media.close();Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());if(FAILED(debug->GetMessage(i,m,&size)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();std::cout<<"RESULT safetyPass="<<(pass&&errors==0&&SUCCEEDED(removed))<<" nrEvaluations="<<metrics.nrEvaluateCount<<" srEvaluations="<<metrics.srEvaluateCount<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass&&errors==0&&SUCCEEDED(removed)?0:1;
}
