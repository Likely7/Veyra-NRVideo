#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=3)return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> layer;if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer))))return 2;layer->EnableDebugLayer();
    engine::EffectChain chain;chain.nodeCount=2;
    for(unsigned i=0;i<2;++i){chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].enabled=true;chain.nodes[i].nr.temporal=false;chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Realtime;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=true;settings.videoSrQuality=0;settings.srTarget=pipeline::SrTarget::Uhd4K;settings.multiplier=2;
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=true;request.sr=true;request.fg=true;request.fgMultiplier=2;
    pipeline::EnhanceGraphDesc base;engine::describeStages(request,settings,base);base.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;if(!session.initialize())return 2;
    const auto adapter=session.context.adapter();bool pass=engine::PreviewGpuSession::computeAllowed(adapter,base);unsigned exclusions=0;
    auto excluded=[&](pipeline::EnhanceGraphDesc desc){++exclusions;pass=!engine::PreviewGpuSession::computeAllowed(adapter,desc)&&pass;};
    auto incompatible=base;incompatible.nrRuntime=engine::NrRuntime::LmxxfAmd;excluded(incompatible);
    for(auto backend:{engine::FrameGenerationBackend::Vfg,engine::FrameGenerationBackend::XeSS,engine::FrameGenerationBackend::Fsr}){incompatible=base;incompatible.frameGenerationBackend=backend;excluded(incompatible);}
    incompatible=base;incompatible.hdrInput=true;excluded(incompatible);incompatible=base;incompatible.color.enabled=true;excluded(incompatible);
    incompatible=base;incompatible.nrLayersTemporal[0]=true;excluded(incompatible);incompatible=base;incompatible.runtimeNodeOrder=true;excluded(incompatible);
    incompatible=base;incompatible.opticalFlowBackend=engine::OpticalFlowBackend::AmdFidelityFx;excluded(incompatible);
    auto other=adapter;other.deviceId=0x2684;pass=!engine::PreviewGpuSession::computeAllowed(other,base)&&pass;++exclusions;
    std::ofstream frames(out/L"frames.csv");frames<<"phase,frame,subframe,pts100ns,width,height,sha256\n";unsigned real=0,generated=0,migrations=0;uint64_t lastFence=0;
    for(unsigned phase=0;phase<4&&pass;++phase){
        auto desc=base;if(phase%2){desc.enableNr=desc.enableSr=desc.enableFg=false;desc.noFeatures=true;desc.fixedExecutionPlan.reset();}
        pass=session.ring.drainQueue()&&session.ring.discardRecording();session.graph->shutdown();
        session.graph=std::make_unique<pipeline::EnhanceGraph>(session.context,session.ring,&session.core);
        const auto previousType=session.ring.queue()->GetDesc().Type;
        pass=pass&&session.configureQueue(desc)&&session.graph->initialize(desc)&&session.graph->createViews();
        const bool expectedCompute=phase%2==0&&!GetEnvironmentVariableW(L"VEYRA_TEST_GRAPH_DIRECT",nullptr,0)&&!GetEnvironmentVariableW(L"VEYRA_TEST_GRAPH_CREATE_FAIL",nullptr,0);
        pass=pass&&((session.ring.queue()->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_COMPUTE)==expectedCompute);
        if(previousType!=session.ring.queue()->GetDesc().Type)++migrations;
        source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;pass=pass&&source.open(open);
        for(unsigned i=0;i<24&&pass;++i){
            pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
            pass=source.read(packet,&input)==source::SourceReadStatus::Frame&&session.graph->process(input,packet.pts.toDouble()*1000,i==0,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
            if(pass&&desc.enableFg){for(unsigned spin=0;spin<1000&&!session.graph->resolveGeneration(output);++spin)Sleep(1);pass=output.gpuComplete();}
            auto record=[&](ID3D12Resource* texture,unsigned subframe,int64_t pts){sink::RgbaImage image;const bool ok=sink::readRgba8(session.context,session.ring,texture,image);
                if(ok){frames<<phase<<','<<i<<','<<subframe<<','<<pts<<','<<image.width<<','<<image.height<<','<<sha256Hex(image.pixels.data(),image.pixels.size())<<'\n';frames.flush();}return ok;};
            if(pass){pass=record(session.graph->videoFrameResource(output.videoSlot),0,int64_t(packet.pts.toDouble()*10000000));++real;}
            for(unsigned f=0;f<output.batch.count&&pass;++f){const auto& item=output.batch.frames[f];if(item.kind!=pipeline::FrameKind::Generated)continue;pass=item.validity==pipeline::GenerationValidity::Valid&&item.lease&&record(item.lease->texture.Get(),item.subframe,item.pts100ns);if(pass)++generated;}
            pass=pass&&session.ring.lastSignaledValue()>lastFence;lastFence=session.ring.lastSignaledValue();
        }
        source.close();
    }
    session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("migration-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T bytes=0;debug->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> data(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&bytes)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<m->pDescription<<std::endl;}}
    const HRESULT removed=session.context.device()->GetDeviceRemovedReason();pass=pass&&real==96&&generated==46&&errors==0&&SUCCEEDED(removed);
    debug.Reset();session.shutdown();std::cout<<"QUEUE_MIGRATION_RESULT pass="<<pass<<" real="<<real<<" generated="<<generated<<" migrations="<<migrations<<" exclusions="<<exclusions<<" fence="<<lastFence<<" debugErrors="<<errors<<" removed="<<unsigned(removed)<<std::endl;return pass?0:1;
}
