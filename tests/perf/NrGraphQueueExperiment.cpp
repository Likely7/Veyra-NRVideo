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
    using namespace veyra;if(argc!=4)return 2;const std::wstring group=argv[3];
    if(group!=L"nr"&&group!=L"srnr"&&group!=L"2x"&&group!=L"3x")return 2;
    const bool sr=group!=L"nr",fg=group==L"2x"||group==L"3x";
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> debugLayer;if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer))))return 2;debugLayer->EnableDebugLayer();
    engine::EffectChain chain;chain.nodeCount=fg?2:1;
    for(unsigned i=0;i<chain.nodeCount;++i){chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].enabled=true;
        chain.nodes[i].nr.temporal=false;chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Realtime;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=sr;settings.videoSrQuality=0;
    settings.srTarget=pipeline::SrTarget::Uhd4K;settings.multiplier=fg?(group==L"3x"?3:2):1;
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=true;request.sr=sr;request.fg=fg;request.fgMultiplier=settings.multiplier;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;if(!session.initialize()||!session.graph->initialize(desc)||!session.graph->createViews())return 2;
    source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!source.open(open))return 2;
    std::ofstream frames(out/L"frames.csv");frames<<"frame,subframe,pts100ns,width,height,sha256\n";bool pass=true;unsigned completed=0,generated=0;
    for(unsigned i=0;i<60&&pass;++i){
        pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
        pass=source.read(packet,&input)==source::SourceReadStatus::Frame&&
            session.graph->process(input,packet.pts.toDouble()*1000,i==0,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
        if(pass&&fg){for(unsigned spin=0;spin<1000&&!session.graph->resolveGeneration(output);++spin)Sleep(1);pass=output.gpuComplete();}
        auto record=[&](ID3D12Resource* texture,unsigned subframe,int64_t pts){sink::RgbaImage image;
            const bool ok=sink::readRgba8(session.context,session.ring,texture,image);if(ok){frames<<i<<','<<subframe<<','<<pts<<','<<image.width<<','<<image.height<<','<<sha256Hex(image.pixels.data(),image.pixels.size())<<'\n';frames.flush();
                if(i<3||i==59){std::ofstream raw(out/(L"frame-"+std::to_wstring(i)+L"-"+std::to_wstring(subframe)+L".rgba"),std::ios::binary);
                    raw.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));}}return ok;};
        if(pass)pass=record(session.graph->videoFrameResource(output.videoSlot),0,int64_t(packet.pts.toDouble()*10000000));
        for(unsigned f=0;f<output.batch.count&&pass;++f){const auto& item=output.batch.frames[f];if(item.kind!=pipeline::FrameKind::Generated)continue;
            pass=item.validity==pipeline::GenerationValidity::Valid&&item.lease&&record(item.lease->texture.Get(),item.subframe,item.pts100ns);if(pass)++generated;}
        if(pass)++completed;
    }
    source.close();session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("graph-queue-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<m->pDescription<<std::endl;}}
    const auto removed=session.context.device()->GetDeviceRemovedReason();const auto type=session.ring.queue()->GetDesc().Type;
    pass=pass&&completed==60&&(!fg||generated>40)&&errors==0&&SUCCEEDED(removed);debug.Reset();session.shutdown();
    std::cout<<"GRAPH_QUEUE_RESULT pass="<<pass<<" frames="<<completed<<" generated="<<generated<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<" queueType="<<unsigned(type)<<std::endl;
    return pass?0:1;
}
