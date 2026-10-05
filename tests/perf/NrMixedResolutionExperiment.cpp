#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/FileIdentity.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>

// Diagnostic readback only: exercise the existing per-layer product contract.
// It is deliberately separate from the timed, ordinary QML player runs.
int wmain(int argc,wchar_t** argv) {
    using namespace veyra;
    if(argc!=4)return 2;
    const std::wstring group=argv[3];
    if(group!=L"none"&&group!=L"2-native"&&group!=L"2-mixed"&&group!=L"3-native"&&group!=L"3-mixed")return 2;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com))return 2;
    struct ComLifetime {~ComLifetime(){CoUninitialize();}} comLifetime;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> debugLayer;
    if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer))))return 2;
    debugLayer->EnableDebugLayer();
    const bool nr=group!=L"none",mixed=group.find(L"mixed")!=std::wstring::npos;
    const unsigned count=nr?(group.front()==L'3'?3:2):0;
    engine::EffectChain chain;chain.nodeCount=count;
    for(unsigned i=0;i<count;++i) {
        auto& node=chain.nodes[i];node.type=engine::EffectType::NrEnhance;node.enabled=true;
        node.nr.temporal=false;node.nr.sizePolicy=pipeline::NrSizePolicy::Realtime;
        if(mixed&&i+1<count)node.nr.sizePolicy=count==3&&i==0?pipeline::NrSizePolicy::P480:pipeline::NrSizePolicy::P720;
    }
    engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=false;settings.multiplier=1;
    engine::StageRequest request;request.width=1920;request.height=1080;request.nr=nr;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
    desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    if(!desc.fixedExecutionPlanError.empty()||(nr&&desc.nrLayersExtent.size()!=count))return 2;
    std::cout<<"NR_MIXED_PLAN layers="<<count<<" work="<<desc.workWidth<<'x'<<desc.workHeight;
    if(nr)for(const auto& extent:desc.nrLayersExtent)std::cout<<" nr="<<extent.width<<'x'<<extent.height;
    std::cout<<std::endl;
    engine::PreviewGpuSession session;
    if(!session.initialize()||!session.graph->initialize(desc)||!session.graph->createViews())return 2;
    source::MediaFileSource source;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!source.open(open))return 2;
    std::ofstream frames(out/L"frames.csv");frames<<"frame,epoch,sequence,pts100ns,width,height,sha256\n";
    bool pass=true;unsigned completed=0;
    for(unsigned i=0;i<40&&pass;++i) {
        if(i==20)pass=source.seek({10,1});
        pipeline::FramePacket packet;const AVFrame* input=nullptr;pipeline::EnhanceGraph::FrameOutputs output;
        pass=pass&&source.read(packet,&input)==source::SourceReadStatus::Frame&&
            session.graph->process(input,packet.pts.toDouble()*1000,i==0||i==20,output,packet.sequence,&packet.colorInfo)&&session.ring.waitIdle();
        sink::RgbaImage image;
        if(pass)pass=sink::readRgba8(session.context,session.ring,session.graph->videoFrameResource(output.videoSlot),image)&&
            image.width==1920&&image.height==1080;
        if(pass) {
            frames<<i<<','<<source.epoch()<<','<<packet.sequence<<','<<int64_t(packet.pts.toDouble()*10000000)<<','<<image.width<<','<<image.height<<','
                <<sha256Hex(image.pixels.data(),image.pixels.size())<<'\n';frames.flush();
            if(i==0||i==1||i==19||i==20||i==21||i==39)pass=sink::saveImage((out/(L"frame-"+std::to_wstring(i)+L".png")).wstring(),image);
            if(pass)++completed;
        }
    }
    source.close();session.ring.drainQueue();session.graph->shutdown();pass=session.core.close("mixed-resolution-test")&&pass;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(session.context.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i) {
        SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if(FAILED(debug->GetMessage(i,message,&n)))return 2;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<"DEBUG_ERROR "<<message->pDescription<<std::endl;}
    }
    const auto removed=session.context.device()->GetDeviceRemovedReason();
    pass=pass&&completed==40&&errors==0&&SUCCEEDED(removed);debug.Reset();session.shutdown();
    std::cout<<"NR_MIXED_RESULT pass="<<pass<<" frames="<<completed<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass?0:1;
}
