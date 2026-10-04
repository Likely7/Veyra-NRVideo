#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EngineController.h"
#include "veyra/gfx/GpuSchedulingPriority.h"
#include "veyra/RuntimePaths.h"
#include <chrono>

namespace veyra::engine {
PreviewGpuSession::~PreviewGpuSession(){shutdown();}
bool PreviewGpuSession::initialize(unsigned slots){
    if(context.initialized()&&ring.initialized())return true;
    Status status;gfx::DeviceContextDesc desc;desc.commandSlotCount=6;
    if(!context.initialize(desc,status))return false;
    if(GetEnvironmentVariableW(L"VEYRA_TEST_GRAPH_COMPUTE",nullptr,0)){
        D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        const auto hr=context.device()->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&executionQueue));
        log::info("graph-queue",std::format("requested=COMPUTE createHr=0x{:X}",unsigned(hr)));
        if(FAILED(hr))return false;executionQueue->SetName(L"Veyra experimental compute producer");
    }
    if(!ring.initialize(context.device(),executionQueue?executionQueue.Get():context.directQueue(),context.fence(),context.fenceEvent(),slots,status))return false;
    gfx::applyRequestedGpuPriority();return true;
}
bool PreviewGpuSession::requestAllowed(const EnhancementSettings& settings){
    return settings.validate().empty()&&(settings.nr||settings.sr)&&settings.multiplier==1&&
        settings.nrRuntime==NrRuntime::Original&&settings.activeNrLayerCount()<=1&&
        !settings.videoHdr.enabled&&!settings.color.enabled&&settings.additionalColorCount==0&&
        (settings.videoSrQuality==0||settings.videoSrQuality==4);
}
bool PreviewGpuSession::prepare(EnhancementSettings settings,unsigned width,unsigned height){
    if(!requestAllowed(settings)||width<64||height<64||width>3840||height>2160)return false;
    const auto start=std::chrono::steady_clock::now();settings=PlayerOptions::from(settings).snapshot();
    if(!initialize()||!context.adapter().isNvidia||ngx::FgCompatibilitySession::requested(context.adapter().vendorId,context.adapter().deviceId))return false;
    StageRequest request;request.width=width;request.height=height;request.nr=settings.nr;request.sr=settings.sr;
    pipeline::EnhanceGraphDesc desc;describeStages(request,settings,desc);desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    uint64_t budget=0,before=0;const bool measured=context.videoMemoryInfo(budget,before);
    if(!graph->initialize(desc)||!graph->createViews()||!ring.waitIdle())return false;
    if((settings.nr&&!graph->nrEnabled())||(settings.sr&&settings.videoSrQuality==0&&!graph->srEnabled()))return false;
    uint64_t after=0;if(!measured||!context.videoMemoryInfo(budget,after)||after<=before)return false;
    graphBytes=after-before;
    if(!RecentGraphCache::fits(budget,after,graphBytes)){log::info("prewarm","event=discard reason=memory-budget");return false;}
    prepared=RecentGraphCache::key(settings,{},desc);
    log::info("prewarm",std::format("event=ready source={}x{} bytes={} usage={} budget={} buildMs={:.3f} evaluated=0",
        width,height,graphBytes,after,budget,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));
    return true;
}
bool PreviewGpuSession::adopt(const EnhancementSettings& settings,const pipeline::EnhanceGraphDesc& desc){
    if(!prepared)return false;
    const auto requested=RecentGraphCache::key(settings,{},desc);
    uint64_t budget=0,usage=0;
    const bool compatible=*prepared==requested&&context.videoMemoryInfo(budget,usage)&&
        RecentGraphCache::fits(budget,usage,graphBytes)&&SUCCEEDED(context.device()->GetDeviceRemovedReason());
    prepared.reset();
    if(compatible&&graph->applySettings(settings)){
        graph->invalidatePausedResidualCache();log::info("prewarm",std::format("event=adopt bytes={} full-first-frame-reset=true",graphBytes));return true;
    }
    log::info("prewarm","event=miss reason=config-dimensions-color-or-budget");
    graph->shutdown();graph=std::make_unique<pipeline::EnhanceGraph>(context,ring,&core);graphBytes=0;return false;
}
void PreviewGpuSession::shutdown(){
    if(ring.initialized())ring.drainQueue();
    if(graph){graph->shutdown();graph.reset();}recent.evict("device-close");
    core.close("device-close");ring.shutdown();executionQueue.Reset();context.shutdown();prepared.reset();graphBytes=0;
}
}
