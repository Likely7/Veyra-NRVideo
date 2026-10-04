#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#ifdef VEYRA_HAS_NGX_CORE_CACHE
#include "veyra/ngx/NgxCoreCache.h"
#endif
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>

// Fixed source frames through the real product graph, 20 rebuilds per group.
// No actual Present here: create/destroy CPU-with-GPU-wait latency is measured
// separately from pixel readbacks. Real UI/Present validation is a later test.
namespace {
double nowMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc!=4)return 2; // media, output, nr|sr|layers|sizes
    const std::wstring group=argv[3];
    if(group!=L"nr"&&group!=L"sr"&&group!=L"layers"&&group!=L"sizes")return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;
    if(!media.open(open)||media.info().width!=1920||media.info().height!=1080)return 2;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(ctx.device()->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return 2;
    auto usage=[&](){DXGI_QUERY_VIDEO_MEMORY_INFO info{};return SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))?info.CurrentUsage:UINT64_MAX;};
#ifdef VEYRA_HAS_NGX_CORE_CACHE
    ngx::NgxCoreCache coreCache;
    pipeline::EnhanceGraph graph(ctx,ring,&coreCache);
#else
    pipeline::EnhanceGraph graph(ctx,ring);
#endif
    std::ofstream csv(out/L"rebuild.csv");csv<<"cycle,nr,sr,layers,policy,destroyMs,createMs,totalMs,vramBefore,vramLive,vramAfter,nrEvaluations,srEvaluations,frameCount\n";
    bool pass=true;
    for(unsigned i=0;i<20&&pass;++i){
        const bool nr=group!=L"nr"||i%2==0,sr=group==L"sr"&&i%2==0;
        const unsigned layers=group==L"layers"?1+i%3:1;
        const pipeline::NrSizePolicy sizes[]={pipeline::NrSizePolicy::Native,pipeline::NrSizePolicy::P720,pipeline::NrSizePolicy::P480};
        const auto policy=group==L"sizes"?sizes[i%3]:pipeline::NrSizePolicy::Realtime;
        engine::EffectChain chain;chain.nodeCount=layers;
        for(unsigned layer=0;layer<layers;++layer){auto& node=chain.nodes[layer];node.type=engine::EffectType::NrEnhance;
            node.enabled=nr;node.nr.temporal=false;node.nr.sizePolicy=policy;}
        engine::EnhancementSettings settings;engine::fromChain(chain,settings);settings.sr=sr;settings.srTarget=pipeline::SrTarget::Uhd4K;
        settings.videoSrQuality=0;settings.nrTemporal=false;settings.multiplier=1;
        engine::StageRequest request;request.width=1920;request.height=1080;request.nr=nr;request.sr=sr;
        pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);
        desc.enableNvofStandalone=nr;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        const auto before=usage();const auto start=nowMs();graph.shutdown();const auto destroyed=nowMs();
        pass=graph.initialize(desc)&&graph.createViews()&&ring.waitIdle();const auto created=nowMs();
        const auto live=usage();unsigned frames=0;
        const auto priorMetrics=graph.metrics();
        if(pass&&!media.seek({0,1000}))pass=false;
        sink::RgbaImage image;
        for(unsigned frame=0;frame<3&&pass;++frame){pipeline::FramePacket packet;const AVFrame* input=nullptr;
            pipeline::EnhanceGraph::FrameOutputs output;
            pass=media.read(packet,&input)==source::SourceReadStatus::Frame&&
                graph.process(input,packet.pts.toDouble()*1000,frame==0,output,packet.sequence,&packet.colorInfo)&&ring.waitIdle();
            if(pass&&frame==2)pass=sink::readRgba8(ctx,ring,graph.videoFrameResource(output.videoSlot),image);
            ++frames;
        }
        const auto metrics=graph.metrics();
        const auto nrDelta=metrics.nrEvaluateCount-priorMetrics.nrEvaluateCount,srDelta=metrics.srEvaluateCount-priorMetrics.srEvaluateCount;
        pass=pass&&frames==3&&nrDelta==uint64_t(nr?layers*3:0)&&srDelta==uint64_t(sr?3:0);
        if(pass){std::ofstream f(out/(L"frame-"+std::to_wstring(i)+L".rgba"),std::ios::binary);
            f.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));
            std::cout<<"FRAME cycle="<<i<<" size="<<image.width<<'x'<<image.height<<" bytes="<<image.pixels.size()<<std::endl;}
        // A per-cycle release census complements peak/live usage and a later
        // all-features-off context close. Creation stays measured separately.
        const auto releaseStart=nowMs();graph.shutdown();const auto releaseEnd=nowMs();const auto released=usage();
        csv<<i<<','<<nr<<','<<sr<<','<<layers<<','<<unsigned(policy)<<','<<releaseEnd-releaseStart<<','<<created-destroyed<<','<<(created-destroyed)+(releaseEnd-releaseStart)<<','<<before<<','<<live<<','<<released<<','<<nrDelta<<','<<srDelta<<','<<frames<<'\n';csv.flush();
        std::cout<<"CYCLE i="<<i<<" createMs="<<created-destroyed<<" destroyMs="<<releaseEnd-releaseStart<<" pass="<<pass<<std::endl;
    }
    ring.drainQueue();graph.shutdown();media.close();
#ifdef VEYRA_HAS_NGX_CORE_CACHE
    if(!coreCache.close("test-final-close"))pass=false;
#endif
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&size)))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();
    std::cout<<"RESULT pass="<<(pass&&errors==0&&SUCCEEDED(removed))<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass&&errors==0&&SUCCEEDED(removed)?0:1;
}
