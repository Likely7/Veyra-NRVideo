#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>

// Bounded competition fixture. All GPU load is the actual product graph;
// counters describe completed work, never display refresh or a game FPS.
int wmain(int argc,wchar_t** argv){
    using namespace veyra;using Clock=std::chrono::steady_clock;
    if(argc!=4)return 2;const unsigned seconds=std::stoul(argv[3]);
    if(seconds<5||seconds>120)return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!media.open(open))return 2;pipeline::FramePacket packet;const AVFrame* input=nullptr;
    if(media.read(packet,&input)!=source::SourceReadStatus::Frame)return 2;
    engine::EffectChain chain;chain.nodeCount=3;
    for(unsigned i=0;i<3;++i){chain.nodes[i].type=engine::EffectType::NrEnhance;chain.nodes[i].enabled=true;
        chain.nodes[i].nr.temporal=false;chain.nodes[i].nr.sizePolicy=pipeline::NrSizePolicy::Native;}
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.multiplier=1;
    engine::StageRequest request;request.width=media.info().width;request.height=media.info().height;request.nr=true;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
    desc.enableNvofStandalone=true;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    pipeline::EnhanceGraph graph(ctx,ring);if(!graph.initialize(desc)||!graph.createViews())return 2;
    bool pass=true;unsigned frames=0;double totalMs=0;
    for(unsigned i=0;i<30&&pass;++i){pipeline::EnhanceGraph::FrameOutputs output;
        pass=graph.process(input,i*1000.0/60,i==0,output,i+1,&packet.colorInfo)&&ring.waitIdle();}
    if(!pass)return 2;
    std::ofstream csv(out/L"completed.csv");csv<<"elapsedSeconds,frames,completedFps,nrEvaluations,cpuProcessWithWaitMs,wallUtc\n";csv<<std::setprecision(12);
    std::cout<<"LOAD_READY"<<std::endl;
    const auto start=Clock::now();auto last=start;unsigned previous=0;
    while(pass&&std::chrono::duration<double>(Clock::now()-start).count()<seconds&&!std::filesystem::exists(out/L"stop")){
        pipeline::EnhanceGraph::FrameOutputs output;const auto begin=Clock::now();
        pass=graph.process(input,(frames+30)*1000.0/60,false,output,frames+31,&packet.colorInfo)&&ring.waitIdle();
        if(!pass)break;++frames;totalMs+=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        const auto now=Clock::now();const double dt=std::chrono::duration<double>(now-last).count();
        if(dt>=1){csv<<std::chrono::duration<double>(now-start).count()<<','<<frames<<','<<(frames-previous)/dt<<','<<graph.metrics().nrEvaluateCount<<','<<totalMs<<','<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<'\n';csv.flush();last=now;previous=frames;}
    }
    const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();const auto metrics=graph.metrics();
    pass=pass&&metrics.nrEvaluateCount==uint64_t(frames+30)*3;
    ring.drainQueue();graph.shutdown();media.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&size)))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();pass=pass&&errors==0&&SUCCEEDED(removed);
    std::cout<<"LOAD_RESULT pass="<<pass<<" frames="<<frames<<" seconds="<<elapsed<<" completedFps="<<frames/elapsed
        <<" nrEvaluations="<<metrics.nrEvaluateCount<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
