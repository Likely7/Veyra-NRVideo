#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <chrono>
#include <fstream>
#include <iostream>

// First-use equality: prepared features have never been evaluated. Readbacks
// belong to this fixture, after measured GPU completion, never playback.
int wmain(int argc,wchar_t** argv){
    using namespace veyra;using Clock=std::chrono::steady_clock;
    if(argc!=5)return 2;const bool enabled=std::wstring_view(argv[3])==L"on";
    if(!enabled&&std::wstring_view(argv[3])!=L"off")return 2;
    const std::wstring group=argv[4];if(group!=L"nr"&&group!=L"sr"&&group!=L"srnr")return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> debugLayer;
    if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer))))return 2;
    debugLayer->EnableDebugLayer();
    engine::EffectChain chain;chain.nodeCount=1;chain.nodes[0].type=engine::EffectType::NrEnhance;
    chain.nodes[0].enabled=group!=L"sr";chain.nodes[0].nr.temporal=false;chain.nodes[0].nr.sizePolicy=pipeline::NrSizePolicy::Realtime;
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.multiplier=1;
    settings.sr=group!=L"nr";settings.videoSrQuality=0;settings.srTarget=pipeline::SrTarget::Uhd4K;
    engine::PreviewGpuSession session;bool pass=true,ready=false;
    const auto prewarmStart=Clock::now();
    if(enabled){ready=session.prepare(settings,1920,1080);pass=ready;}
    const auto prewarmMs=std::chrono::duration<double,std::milli>(Clock::now()-prewarmStart).count();
    pass=pass&&session.graph->metrics().nrEvaluateCount==0&&session.graph->metrics().srEvaluateCount==0;
    source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!source.open(open)||source.info().width!=1920||source.info().height!=1080)return 2;
    settings.revision=31; // A source open always receives a new revision.
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=settings.nr;request.sr=settings.sr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    const auto openStart=Clock::now();pass=pass&&session.initialize();
    const bool adopted=pass&&session.adopt(settings,desc);
    if(!adopted)pass=pass&&session.graph->initialize(desc)&&session.graph->createViews();
    pass=pass&&adopted==enabled;
    const auto createMs=std::chrono::duration<double,std::milli>(Clock::now()-openStart).count();
    std::ofstream csv(out/L"frames.csv");csv<<"frame,width,height,nrEvaluations,srEvaluations\n";
    double firstGpuCompleteMs=-1;
    for(unsigned frame=0;frame<3&&pass;++frame){
        pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
        pass=source.read(packet,&input)==source::SourceReadStatus::Frame&&
            session.graph->process(input,packet.pts.toDouble()*1000,frame==0,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
        if(frame==0)firstGpuCompleteMs=std::chrono::duration<double,std::milli>(Clock::now()-openStart).count();
        sink::RgbaImage image;if(pass)pass=sink::readRgba8(session.context,session.ring,session.graph->videoFrameResource(output.videoSlot),image);
        if(pass){std::ofstream file(out/(L"frame-"+std::to_wstring(frame)+L".rgba"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));
            csv<<frame<<','<<image.width<<','<<image.height<<','<<session.graph->metrics().nrEvaluateCount<<','<<session.graph->metrics().srEvaluateCount<<'\n';csv.flush();}
    }
    pass=pass&&session.graph->metrics().nrEvaluateCount==unsigned(settings.nr?3:0)&&session.graph->metrics().srEvaluateCount==unsigned(settings.sr?3:0);
    source.close();session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("native-prewarm-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);
        std::vector<unsigned char> bytes(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,message,&n)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}}
    const auto removed=session.context.device()->GetDeviceRemovedReason();debug.Reset();session.shutdown();
    pass=pass&&errors==0&&SUCCEEDED(removed);
    std::cout<<"PREWARM_RESULT pass="<<pass<<" enabled="<<enabled<<" ready="<<ready<<" adopted="<<adopted
        <<" prewarmMs="<<prewarmMs<<" createMs="<<createMs<<" firstGpuCompleteMs="<<firstGpuCompleteMs
        <<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
