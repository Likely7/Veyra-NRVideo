#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/AutoNrController.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <chrono>
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    using namespace veyra;if(argc!=5)return 2;
    const bool pooled=std::wstring(argv[3])==L"pool";if(!pooled&&std::wstring(argv[3])!=L"reference")return 2;
    const unsigned count=_wtoi(argv[4]);if(count<1||count>2)return 2;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
    struct ComLifetime{~ComLifetime(){CoUninitialize();}} comLifetime;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> debugLayer;if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer))))return 2;debugLayer->EnableDebugLayer();
    engine::EffectChain chain;chain.nodeCount=count;
    for(unsigned i=0;i<count;++i){auto& node=chain.nodes[i];node.type=engine::EffectType::NrEnhance;node.enabled=true;node.nr.temporal=false;node.nr.sizePolicy=pipeline::NrSizePolicy::Realtime;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.multiplier=1;
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=true;
    pipeline::EnhanceGraphDesc initial;engine::describeStages(request,settings,initial);initial.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;if(!session.initialize())return 2;
    auto makeDesc=[&](unsigned level){auto desc=initial;const auto percent=engine::AutoNrController::percent(level);
        const pipeline::Extent size{(1920*percent/100)&~1u,(1080*percent/100)&~1u};desc.nrLayersExtent[0]=size;
        for(unsigned i=0;i<desc.fixedExecutionPlan->stepCount;++i){auto& step=desc.fixedExecutionPlan->steps[i];if(step.type==engine::EffectType::NrEnhance&&step.resourceIndex==0)step.processing=size;}
        return desc;};
    if(pooled){initial.nrAutoPoolLayer=0;if(!session.graph->initialize(initial)||!session.graph->createViews()||!session.graph->autoNrPoolReady())return 2;}
    source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!source.open(open))return 2;
    constexpr unsigned levels[6]={0,1,2,3,4,0};
    std::ofstream frames(out/L"frames.csv");frames<<"frame,level,pts100ns,width,height,historyReset,sha256\n";
    bool pass=true;unsigned completed=0;
    for(unsigned segment=0;segment<6&&pass;++segment){const auto level=levels[segment];
        if(pooled)pass=session.graph->selectAutoNrLevel(level);
        else{session.graph->shutdown();pass=session.graph->initialize(makeDesc(level))&&session.graph->createViews();}
        for(unsigned j=0;j<15&&pass;++j){const unsigned frame=segment*15+j;
            pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
            // For the pool, switches must request reset themselves. Only the
            // first frame has an explicit caller reset.
            pass=source.read(packet,&input)==source::SourceReadStatus::Frame&&
                session.graph->process(input,packet.pts.toDouble()*1000,pooled?frame==0:j==0,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
            if(pass&&j==0)pass=output.historyReset;
            sink::RgbaImage image;if(pass)pass=sink::readRgba8(session.context,session.ring,session.graph->videoFrameResource(output.videoSlot),image);
            if(pass){frames<<frame<<','<<level<<','<<int64_t(packet.pts.toDouble()*10000000)<<','<<image.width<<','<<image.height<<','<<output.historyReset<<','
                <<sha256Hex(image.pixels.data(),image.pixels.size())<<'\n';frames.flush();
                if(j==0||j==14)pass=sink::saveImage((out/(L"frame-"+std::to_wstring(frame)+L".png")).wstring(),image);
                if(pass)++completed;}
        }
    }
    source.close();session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("nr-auto-pool-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,m,&n)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<m->pDescription<<std::endl;}}
    const auto removed=session.context.device()->GetDeviceRemovedReason();pass=pass&&completed==90&&errors==0&&SUCCEEDED(removed);debug.Reset();session.shutdown();
    std::cout<<"NR_AUTO_POOL_RESULT pass="<<pass<<" frames="<<completed<<" layers="<<count<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
