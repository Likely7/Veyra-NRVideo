#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <objbase.h>

int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=4)return 2;const std::wstring group=argv[3];
    const bool sr=group==L"2-sr"||group==L"3-sr"||group==L"single-sr";
    const bool single=group==L"single"||group==L"single-sr",triple=group==L"3"||group==L"3-sr";
    const bool mixed=group==L"mixed",temporal=group==L"temporal",protectedGroup=group==L"protected",edits=group==L"edits";
    if(group!=L"2"&&!sr&&!single&&!triple&&!mixed&&!temporal&&!protectedGroup&&!edits)return 2;
    const unsigned layers=single?1:triple?3:2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);log::info("low-chain-test",std::format("COM init hr=0x{:X}",unsigned(com)));if(FAILED(com))return 2;
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    Microsoft::WRL::ComPtr<ID3D12Debug> layer;if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer))))return 2;layer->EnableDebugLayer();
    engine::EffectChain chain;chain.nodeCount=layers;
    for(unsigned i=0;i<layers;++i){chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].enabled=true;
        chain.nodes[i].nr.temporal=temporal;chain.nodes[i].nr.sizePolicy=sr?pipeline::NrSizePolicy::Realtime:
            mixed&&i==0?pipeline::NrSizePolicy::P480:pipeline::NrSizePolicy::P720;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=sr;settings.videoSrQuality=0;
    settings.srTarget=pipeline::SrTarget::Uhd4K;settings.multiplier=1;
    if(protectedGroup){settings.protection.enabled=true;settings.protection.featherPixels=4;settings.protection.regions[0]={.2f,.2f,.8f,.8f,false};}
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=true;request.sr=sr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;if(!session.initialize()||!session.graph->initialize(desc)||!session.graph->createViews())return 2;
    const bool requested=GetEnvironmentVariableW(L"VEYRA_TEST_NR_LOW_CHAIN",nullptr,0)>0,active=session.graph->diagnosticLowResolutionNrChain();
    bool pass=active==(requested&&!single&&!mixed&&!temporal)&&session.graph->nrLayerCount()==layers;
    for(unsigned i=0;i<layers&&pass;++i){const auto shape=session.graph->diagnosticNrLayerOutput(i)->GetDesc();const auto expected=active?desc.nrLayersExtent[i]:desc.nrFullExtent(i);pass=shape.Width==expected.width&&shape.Height==expected.height;}
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!media.open(open))return 2;
    std::ofstream frames(out/L"frames.csv");frames<<"frame,pts100ns,width,height,sha256,nrDelta\n";unsigned completed=0;
    for(unsigned i=0;i<40&&pass;++i){
        if(edits&&(i==10||i==20||i==30)){
            const unsigned edited=i==20?1:0;auto& r=settings.nrLayers[edited].residual;r.total=i==30?1.f:.4f;r.color=i==30?1.f:.3f;r.darken=i==30?1.f:1.3f;
            pass=session.graph->applySettings(settings)&&session.graph->diagnosticNrLayerSettings(edited).residual==r;
        }
        pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;const auto before=session.graph->metrics().nrEvaluateCount;
        pass=pass&&media.read(packet,&input)==source::SourceReadStatus::Frame&&session.graph->process(input,packet.pts.toDouble()*1000,i==0,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
        sink::RgbaImage image;if(pass)pass=sink::readRgba8(session.context,session.ring,session.graph->videoFrameResource(output.videoSlot),image);
        const auto delta=session.graph->metrics().nrEvaluateCount-before;pass=pass&&delta==layers;
        if(pass){frames<<i<<','<<int64_t(packet.pts.toDouble()*10000000)<<','<<image.width<<','<<image.height<<','<<sha256Hex(image.pixels.data(),image.pixels.size())<<','<<delta<<'\n';frames.flush();
            pass=sink::saveImage((out/(L"frame-"+std::to_wstring(i)+L".png")).wstring(),image);if(pass)++completed;}
    }
    media.close();session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("low-chain-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<m->pDescription<<std::endl;}}
    const auto removed=session.context.device()->GetDeviceRemovedReason();pass=pass&&completed==40&&errors==0&&SUCCEEDED(removed);debug.Reset();session.shutdown();
    std::cout<<"LOW_CHAIN_RESULT pass="<<pass<<" frames="<<completed<<" layers="<<layers<<" requested="<<requested<<" active="<<active<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;return pass?0:1;
}
