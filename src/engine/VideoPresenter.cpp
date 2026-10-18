#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/DoviCurveAdapter.h" // custom: Dolby Vision level 1 -> curve
#include "veyra/engine/PresentationGeometry.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <cstdlib> // std::getenv (custom: Dolby Vision follow switch)
namespace veyra::engine {
bool VideoPresenter::beginSourceInput(){
    auto* xess=sink_.xess();if(!xess)return true;
    // Waiting/filtered inputs keep the same preparation ID. Do not sleep again
    // until one accepted source has been submitted to the enhancement graph.
    if(!xessInputId_)xessInputId_=xess->beginInput();
    return xessInputId_!=0;
}
bool VideoPresenter::beginSourceProcessing(){
    auto* xess=sink_.xess();return !xess||(beginSourceInput()&&xess->beginProcessing(xessInputId_));
}
void VideoPresenter::sourceProcessed(pipeline::FrameIdentity identity){
    if(!xessInputId_)return;
    xessWork_[xessWorkPosition_]={identity,xessInputId_};
    xessWorkPosition_=(xessWorkPosition_+1)%xessWork_.size();xessInputId_=0;
}
bool VideoPresenter::open(gfx::D3D12DeviceContext& ctx,HWND window,pipeline::EnhanceGraph& graph,bool captureCompatible,bool mediaClockPaced) {
    const bool reuse=sink_.swapChain()&&!sink_.xess()&&!graph.xessEnabled()&&window_==window&&
        presentationDevice_==ctx.device()&&presentationFormat_==graph.outputFormat()&&
        captureCompatible_==captureCompatible&&presentationQueue_;
    if(reuse){if(!prepareGraphChange())return false;}
    else close();
    RECT rc{};
    if(!IsWindow(window)||!GetClientRect(window,&rc)){
        veyra::log::warn("present","presentation creation cancelled: target window is no longer available");
        return false;
    }
    viewWidth_=viewHeight_=0;bufferMonitor_=nullptr;monitorWidth_=monitorHeight_=0;refreshMonitor_=nullptr;refreshHz_=0;refreshQuery_={};vrr_={};refreshQueried_=false;vrrSync_=false;
    latestDovi_={}; // A new chain never inherits the previous source's RPU.
    ++generation_;
    Status st=Status::Ok;
    // Keep one presentation queue for the native chain's lifetime. A later
    // FG/compute switch can reuse it instead of replacing the HWND's chain.
    if(!reuse&&!graph.xessEnabled()){
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        wchar_t requestedPriority[16]{};
        if(GetEnvironmentVariableW(L"VEYRA_TEST_PRESENT_QUEUE_PRIORITY",requestedPriority,16)&&std::wstring_view(requestedPriority)==L"high"){
            D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY support{desc.Type,D3D12_COMMAND_QUEUE_PRIORITY_HIGH,FALSE};
            const HRESULT queried=ctx.device()->CheckFeatureSupport(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY,&support,sizeof(support));
            if(SUCCEEDED(queried)&&support.PriorityForTypeIsSupported)desc.Priority=D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
            veyra::log::info("present-priority",std::format("requested=HIGH supported={} queryHr=0x{:X} selected={}",
                support.PriorityForTypeIsSupported,unsigned(queried),desc.Priority));
        }
        HRESULT hr=ctx.device()->CreateCommandQueue(&desc,IID_PPV_ARGS(&presentationQueue_));
        if(SUCCEEDED(hr))veyra::log::info("present-priority",std::format("actualType={} actualPriority={}",
            unsigned(presentationQueue_->GetDesc().Type),presentationQueue_->GetDesc().Priority));
        if(SUCCEEDED(hr))hr=ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&presentationFence_));
        if(FAILED(hr)){veyra::log::error("present",std::format("application frame-generation presentation queue/fence hr=0x{:X}",unsigned(hr)));close();return false;}
        presentationEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!presentationEvent_||!presentationRing_.initialize(ctx.device(),presentationQueue_.Get(),presentationFence_.Get(),presentationEvent_,3,st)){close();return false;}
        veyra::log::info("present","native presentation uses persistent independent queue/fence, 3 command slots; shared textures use producer/consumer GPU fences");
    }
    auto* queue=presentationQueue_?presentationQueue_.Get():ctx.directQueue();
    gpuTimer_.initialize(ctx.device(),queue);window_=window;
    presentationDevice_=ctx.device();presentationFormat_=graph.outputFormat();captureCompatible_=captureCompatible;
    gfx::PresentSink::Desc d;d.targetWindow=window;d.width=std::max(1L,rc.right);d.height=std::max(1L,rc.bottom);d.vsync=false;d.tearing=false;
    d.waitable=!GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_SWAPCHAIN",nullptr,0);
    d.hdr=graph.hdrOutput();d.hdr10=graph.hdr10Output();d.xess=graph.xessEnabled();d.captureCompatible=captureCompatible;d.fgMultiplier=graph.fgMultiplier();
    // Candidate X2 (2026-09-22) tried XeLL pass-through for media-clock-paced
    // sources; the audited provider then rejects generation with -15
    // (LATENCY_REDUCTION_UNSUPPORTED) and Intel's guide states frame
    // generation is disabled unless XeLL is enabled. Keep low latency on.
    // VEYRA_TEST_XESS_LOW_LATENCY=0 remains a diagnostic-only toggle.
    (void)mediaClockPaced;
    wchar_t lowLatency[4]{};
    d.xessLowLatencySleep=!(GetEnvironmentVariableW(L"VEYRA_TEST_XESS_LOW_LATENCY",lowLatency,4)&&lowLatency[0]==L'0');lastXessFrame_={};lastXessIdentity_={};xessWasEnabled_=false;
    if(d.xess){
        bufferMonitor_=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(bufferMonitor_,&monitor)){
            monitorWidth_=monitor.rcMonitor.right-monitor.rcMonitor.left;monitorHeight_=monitor.rcMonitor.bottom-monitor.rcMonitor.top;
            d.width=std::max(d.width,monitorWidth_);d.height=std::max(d.height,monitorHeight_);
        }
        veyra::log::info("present",std::format("XeSS retained buffers {}x{} client={}x{}; window scaling avoids provider rebuild",d.width,d.height,rc.right,rc.bottom));
    }
    if(!reuse&&!sink_.initialize(ctx.device(),queue,d,st))return false;
    if(reuse)veyra::log::info("present-reuse",std::format("retained native swapchain generation={} format={} fg={} output={}x{}; rebinding drained graph descriptors",generation_,unsigned(graph.outputFormat()),graph.fgEnabled(),graph.workWidth(),graph.workHeight()));
    pass_={};rtvs_.Reset();compositeTarget_.Reset();
    std::vector<uint8_t> vs,ps;
    // The output pool contains both parities of every generated subframe.
    // Comparison references follow it; derive offsets to cover VFG through 8X.
    if(!pipeline::loadShaderBytes("PresentBlit_vs.dxil",vs)||!pipeline::loadShaderBytes("PresentBlit_ps.dxil",ps)||!pass_.create(ctx.device(),vs,ps,pipeline::kOutputPoolSlots+4,graph.outputFormat(),16))return false;
    if(sink_.xess()){
        std::vector<uint8_t> cs;
        if(!pipeline::loadShaderBytes("PresentMotion.dxil",cs)||!presentMotionPass_.create(ctx.device(),cs,4,1,1,16))return false;
        const auto heap=presentMotionPass_.heap->GetCPUDescriptorHandleForHeapStart();
        for(unsigned i=0;i<2;++i){
            presentMotion_[i]=pipeline::makeTexture(ctx.device(),graph.workWidth(),graph.workHeight(),DXGI_FORMAT_R16G16_FLOAT,true);
            if(!presentMotion_[i])return false;
            presentMotion_[i]->SetName(L"Veyra XeSS display motion");
            pipeline::makeSrv(ctx.device(),graph.presentMotion(i),DXGI_FORMAT_R16G16_FLOAT,{heap.ptr+size_t(i*2)*presentMotionPass_.increment});
            pipeline::makeUav(ctx.device(),presentMotion_[i].Get(),DXGI_FORMAT_R16G16_FLOAT,{heap.ptr+size_t(i*2+1)*presentMotionPass_.increment});
        }
    }
    // Field 2026-10-09: HDR application FG flashes on an RTX5090 while the
    // pixel-copy diagnostic does not. This is a compatibility candidate, not
    // proof of a driver defect. Compose separately and copy the actual final
    // image to DXGI; never retain the diagnostic's CPU readback or add sleeps.
    copyEligible_=ctx.adapter().isNvidia&&graph.hdr10Output()&&graph.fgEnabled()&&bool(presentationQueue_);
    // Opening a source never opts the user in or allocates the copy surface.
    // configurePresentation applies the saved global choice on the GPU owner.
    copyBeforePresent_=false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=4;
    if(FAILED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs_))))return false;
    inc_=ctx.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);if(!refresh(ctx.device()))return false;
    veyra::log::info("present-copy",std::format("enabled={} format={} extent={}x{} (GPU-only final composition; no pixel readback)",copyBeforePresent_,unsigned(graph.outputFormat()),sink_.bufferWidth(),sink_.bufferHeight()));
    for(unsigned i=0;i<pipeline::kOutputPoolSlots;++i)pipeline::makeSrv(ctx.device(),i<2?graph.videoFrameResource(i):graph.generatedFrameResource(i-2),graph.outputFormat(),{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*pass_.increment});
    for(unsigned i=0;i<4;++i)pipeline::makeSrv(ctx.device(),i<2?graph.sourceReference(i):graph.baseReference(i-2),DXGI_FORMAT_R16G16B16A16_FLOAT,{pass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i+pipeline::kOutputPoolSlots)*pass_.increment});
    return true;
}
bool VideoPresenter::prepareGraphChange(){
    if(sink_.xess()){close();return true;}
    if(presentationRing_.initialized()&&!presentationRing_.drainQueue())return false;
    hasPresented_=false;retainedFrame_.reset();gpuTimer_.close();
    reflex_.close();reflexFrame_=0;
    return true;
}
bool VideoPresenter::refresh(ID3D12Device* device){
    // ResizeBuffers keeps the swapchain object and may retain its metadata.
    // Request a resend without forgetting an active hint that must be cleared
    // if the user disables metadata immediately after a resize.
    Microsoft::WRL::ComPtr<ID3D12Resource> bb;
    for(unsigned i=0;i<3;++i){
        bb.Reset();const HRESULT hr=sink_.swapChain()->GetBuffer(i,IID_PPV_ARGS(&bb));
        if(FAILED(hr)){veyra::log::error("present-copy",std::format("GetBuffer index={} hr=0x{:X}",i,unsigned(hr)));return false;}
        device->CreateRenderTargetView(bb.Get(),nullptr,{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(i)*inc_});
    }
    if(copyBeforePresent_){
        auto desc=bb->GetDesc();
        if(!compositeTarget_||compositeTarget_->GetDesc().Width!=desc.Width||compositeTarget_->GetDesc().Height!=desc.Height||compositeTarget_->GetDesc().Format!=desc.Format){
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;clear.Color[3]=1;
            Microsoft::WRL::ComPtr<ID3D12Resource> target;
            const HRESULT hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,&clear,IID_PPV_ARGS(&target));
            if(FAILED(hr)){
                veyra::log::warn("present-copy",std::format("composition allocation {}x{} format={} hr=0x{:X}; using direct presentation",desc.Width,desc.Height,unsigned(desc.Format),unsigned(hr)));
                compositeTarget_.Reset();copyBeforePresent_=false;
            }else{
                target->SetName(L"Veyra HDR final composition");compositeTarget_=std::move(target);
                const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
                veyra::log::info("present-copy",std::format("composition allocation {}x{} format={} bytes={} surfaces=1",desc.Width,desc.Height,unsigned(desc.Format),allocation.SizeInBytes));
            }
        }
        if(compositeTarget_)device->CreateRenderTargetView(compositeTarget_.Get(),nullptr,{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(3)*inc_});
    }
    return true;
}
bool VideoPresenter::present(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& sharedRing,pipeline::EnhanceGraph& graph,unsigned slot,bool generated,bool referencesValid,int comparison,bool baseReference,float split,pipeline::FrameIdentity identity,PreviewView view,int64_t sourcePts100ns,bool retainUnchanged) {
    auto& ring=presentationQueue_?presentationRing_:sharedRing;
    auto* fence=presentationFence_?presentationFence_.Get():ctx.fence();
    const auto presentStart=std::chrono::steady_clock::now();
    const auto slotWaitStart=ring.cpuWaitMilliseconds();
    xessFailed_=false;
    RECT rc{};GetClientRect(window_,&rc);if(rc.right<1||rc.bottom<1)return true;
    const auto now=std::chrono::steady_clock::now();
    bool resized=false;
    const auto deferUntil=uint64_t(uintptr_t(GetPropW(window_,L"Veyra.ResizeDeferUntil")));
    if(viewWidth_!=unsigned(rc.right)||viewHeight_!=unsigned(rc.bottom)){
        viewWidth_=rc.right;viewHeight_=rc.bottom;xessWasEnabled_=false;
    }
    unsigned targetWidth=rc.right,targetHeight=rc.bottom;
    if(sink_.xess()){
        const auto monitor=MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST);
        if(monitor!=bufferMonitor_){
            bufferMonitor_=monitor;MONITORINFO info{sizeof(info)};
            if(GetMonitorInfoW(monitor,&info)){monitorWidth_=info.rcMonitor.right-info.rcMonitor.left;monitorHeight_=info.rcMonitor.bottom-info.rcMonitor.top;}
        }
        // Never shrink the provider's working set on ordinary window changes.
        // Grow only when the client or a new display needs more output pixels.
        targetWidth=std::max({targetWidth,sink_.width(),monitorWidth_});
        targetHeight=std::max({targetHeight,sink_.height(),monitorHeight_});
    }
    // The UI keeps pumping messages during the native move loop. Retain and
    // stretch current buffers there instead of repeatedly draining the GPU
    // and rebuilding provider resources at monitor/DPI boundaries.
    const bool windowChanging=GetPropW(window_,L"Veyra.InteractiveMove")||GetPropW(window_,L"Veyra.DpiTransition");
    view=filmPixelAlignedView(window_,rc.right,rc.bottom,graph.workWidth(),graph.workHeight(),view);
    auto hdrBaseline=graph.hdrCurve();
    const auto& metadataBaseline=graph.hdrMetadata();
    const bool needsDisplayPeak=hdrBaseline.enabled||(metadataBaseline.enabled&&
        (metadataBaseline.masteringPeakNits||metadataBaseline.maxCllNits||metadataBaseline.maxFallNits));
    {
        // Custom: one-shot record of what the display reports and whether it was
        // trusted, so the curve's effective target is never a guess.
        static unsigned peakLogged = 0;
        if (peakLogged < 2 && graph.hdrOutput()) {
            ++peakLogged;
            veyra::log::info("hdr-peak", std::format(
                "report valid={} plausible={} peak={} black={:.3f} needsPeak={} curveDisplayPeakNits={}",
                hdrPeakInfo_.valid ? 1 : 0, hdrPeakInfo_.plausible ? 1 : 0,
                hdrPeakInfo_.peakNits, hdrPeakInfo_.minNits,
                needsDisplayPeak ? 1 : 0, hdrBaseline.displayPeakNits));
        }
    }
    if(graph.hdrOutput()&&needsDisplayPeak&&hdrBaseline.displayPeakNits==0){
        const auto monitor=MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST);
        if(hdrPeakMonitor_!=monitor||now-hdrPeakQuery_>=std::chrono::seconds(2)){
            const auto previous=hdrPeakInfo_;
            hdrPeakInfo_=gfx::PresentSink::queryDisplayPeak(window_);hdrPeakMonitor_=monitor;hdrPeakQuery_=now;
            if(previous.valid!=hdrPeakInfo_.valid||previous.peakNits!=hdrPeakInfo_.peakNits)retainedFrame_.reset();
        }
        // Only a plausible report may scale the curve; a ceiling value would push the
        // highlight start past anything the content contains.
        if(hdrPeakInfo_.valid&&hdrPeakInfo_.plausible)hdrBaseline.displayPeakNits=std::clamp(hdrPeakInfo_.peakNits,100u,10000u);

    }
    HdrCurveSettings hdrCurve;HdrMetadataSettings hdrMetadata;
    resolveHdrTuning(hdrBaseline,graph.hdrMetadata(),hdrCurve,hdrMetadata);
    // Custom: follow Dolby Vision level 1. Only the highlight start (and the
    // peak cap under the user's ceiling) moves; shadow lift, mid-gray and
    // roll-off strength stay exactly what the user set, so the picture is never
    // silently re-shaded. hdrCurve itself stays the static curve for the
    // display metadata and the coverage probe, and nothing is applied at all
    // unless the switch is on: the delivered image is unchanged otherwise.
    // VEYRA_DOVI_FOLLOW stays as a test override while the switch is checked.
    const bool followDovi=latestDovi_.valid&&(graph.doviFollow()||std::getenv("VEYRA_DOVI_FOLLOW")!=nullptr);
    // Average brightness, smoothed over ~0.5 s so the knee does not breathe with
    // every frame. A large jump (scene change) resets the window instead of
    // dragging the old value along.
    double smoothedAvgPq=-1.0;
    if(followDovi&&latestDovi_.valid){
        const double avgPq=latestDovi_.avgPq;
        if(avgPq>0.0){
            constexpr int kWindow=12;
            if(avgSumCount_>0&&(avgPq>0.0&&(avgSum_/double(avgSumCount_))>0.0)){
                const double prevAvg=avgSum_/double(avgSumCount_);
                if(prevAvg>0.0&&(avgPq>prevAvg*3.0||avgPq<prevAvg/3.0)){avgSum_=0.0;avgSumCount_=0;}
            }
            avgSum_+=avgPq;++avgSumCount_;
            if(avgSumCount_>kWindow){avgSum_*=0.5;avgSumCount_/=2;}
            smoothedAvgPq=avgSum_/double(avgSumCount_);
        }
    }
    const HdrCurveSettings effective=followDovi?engine::doviCurveFromLevel1(latestDovi_,hdrCurve,graph.doviFollowAverage(),smoothedAvgPq):hdrCurve;
    // The trace is its own gate: a user who turns the switch on must not get a
    // line per frame pair for the length of a film. It also records the switch
    // itself, so "RPU present but the setting did not arrive" is diagnosable.
    const bool traceDovi=latestDovi_.valid&&std::getenv("VEYRA_DOVI_TRACE")!=nullptr;
    const RetainedFrame retained{graph.presentationReadyFenceObject(slot,generated),graph.presentationReadyFence(slot,generated),
        slot,unsigned(rc.right),unsigned(rc.bottom),generated,referencesValid,baseReference,IsWindowVisible(window_)!=FALSE,
        comparison,split,view,identity,MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),graph.hdrOutput()?effective:HdrCurveSettings{}};
    // Pausing keeps the last flip-model buffer visible. Re-submit only when
    // its producer fence, view, comparison, visibility, monitor or size changes.
    // Deferred resize must keep polling until the actual buffers match.
    if(retainUnchanged&&retained.producer&&retained.fence&&retainedFrame_&&*retainedFrame_==retained&&
        !windowChanging&&targetWidth==sink_.width()&&targetHeight==sink_.height()&&
        SUCCEEDED(ctx.device()->GetDeviceRemovedReason())&&
        !GetEnvironmentVariableW(L"VEYRA_TEST_DISABLE_PAUSED_PRESENT_REUSE",nullptr,0))return true;
    retainedFrame_.reset(); // A failed present never seeds an idle cache.
    if(!windowChanging&&(targetWidth!=sink_.width()||targetHeight!=sink_.height())&&GetTickCount64()>=deferUntil&&now-lastResize_>=std::chrono::milliseconds(100)&&sink_.resizeDue()) {
        resized=true;
        if(!ring.drainQueue())return false;sink_.resize(targetWidth,targetHeight);
        // A capture hook may temporarily retain a DXGI buffer. Keep the old
        // valid buffers and let DXGI scale them until a later resize succeeds.
        if(!sink_.currentBackBuffer()||FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
        if(!refresh(ctx.device()))return false;lastResize_=now;xessWasEnabled_=false;
    }
    const auto resizeEnd=std::chrono::steady_clock::now();
    if(auto* xess=sink_.xess()){
        uint32_t preparedId=0;
        for(auto& work:xessWork_)if(work.id&&work.identity==identity){preparedId=work.id;work.id=0;break;}
        if(!xess->beginFrame(preparedId,identity)){xessFailed_=true;return false;}
    }
    const auto beginEnd=std::chrono::steady_clock::now();
    if(presentationQueue_){
        auto* producerFence=graph.presentationReadyFenceObject(slot,generated);
        if(!producerFence){veyra::log::error("present","missing producer fence");return false;}
        const HRESULT hr=presentationQueue_->Wait(producerFence,graph.presentationReadyFence(slot,generated));
        if(FAILED(hr)){veyra::log::error("present",std::format("producer handoff wait hr=0x{:X}",unsigned(hr)));return false;}
    }
    Status st=Status::Ok;uint32_t commandSlot=0;auto* list=ring.acquireNext(commandSlot,st);if(!list)return false;
    gpuTimer_.frame(identity.sourceFrameId?identity:pipeline::FrameIdentity{0,0,submittedCount()+1},fence);gpuTimer_.mark(list,diagnostics::GpuStage::Blit);
    uint32_t backBufferIndex=0;
    auto* source=generated?graph.generatedFrameResource(slot):graph.videoFrameResource(slot);auto* bb=sink_.currentBackBuffer(&backBufferIndex);if(!bb)return false;
    auto* renderTarget=copyBeforePresent_?compositeTarget_.Get():bb;
    if(!source||!renderTarget)return false;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    barriers[0].Transition.pResource=source;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource=renderTarget;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->ResourceBarrier(2,barriers);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs_->GetCPUDescriptorHandleForHeapStart().ptr+size_t(copyBeforePresent_?3:backBufferIndex)*inc_};
    const float black[4]={0,0,0,1};list->ClearRenderTargetView(rtv,black,0,nullptr);list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    // DXGI stretches the retained buffer while the native workspace unfolds.
    // Compute contain in CURRENT client coordinates, then map back to buffer
    // coordinates so that the onscreen image keeps its aspect throughout.
    const auto [renderW,renderH]=view.renderedSize(float(rc.right),float(rc.bottom),float(graph.workWidth()),float(graph.workHeight()));
    D3D12_VIEWPORT viewport{0,0,float(sink_.bufferWidth()),float(sink_.bufferHeight()),0,1};
    D3D12_RECT rect{0,0,LONG(sink_.bufferWidth()),LONG(sink_.bufferHeight())};list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
    ID3D12DescriptorHeap* heaps[]={pass_.heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(pass_.rootSig.Get());list->SetPipelineState(pass_.pso.Get());
    const float rotationFlags=float(view.quarterTurns()<<6);
    const float samplingFlags=(graph.highQualityPresentation()?2.0f:0.0f)+rotationFlags;
    // PresentBlit uses a contain mapping. Cancel its fit so these dimensions
    // express the exact shared display transform, including stretch/SAR/fill.
    const float inverseFit=1.0f/std::min(float(rc.right)/renderW,float(rc.bottom)/renderH);
    // Custom: static HDR curve. The present blit applies it on absolute nits just
    // before the PQ encode, so it never touches the exported or the graded frame.
    // Main HDR10/FG surfaces already contain PQ BT.2020. Decode only when
    // tuning, then encode back; disabled tuning keeps the original exact blit.
    // Display tuning also applies after RTX Video HDR converted an SDR source.
    const float curveFlag=(hdrCurve.enabled&&graph.hdrOutput())?16.0f:0.0f;
    const float mainFlags=samplingFlags+curveFlag+(curveFlag&&graph.hdr10Output()?36.0f:0.0f);
    float dims[16]={renderW,renderH,mainFlags,inverseFit,float(rc.right),float(rc.bottom),view.centerX,view.centerY};
    // The blit sees the per-frame curve when Dolby Vision follow is on; every
    // other frame (and every non-DV source) passes the user's settings through.
    dims[8]=float(effective.shadowLiftEv100)/100.0f;
    dims[9]=float(effective.shadowRangeNits);
    dims[10]=float(effective.midGrayNits);
    dims[11]=float(effective.highlightStartNits);
    {
        // The roll-off has to close at a real ceiling. Following the frame means
        // closing at the frame's own peak, so the knee is not a slope stretched
        // across the whole panel range (a 4000 nit ceiling with a 286 nit knee
        // would barely compress anything the frame actually contains). The hard
        // ceiling itself (dims[13]) stays the user's: it also anchors the mid-gray
        // gamma, so moving it would reshape the whole tone scale.
        float peak=effective.peakCapNits?float(effective.peakCapNits):1000.0f;
        const float start=float(effective.highlightStartNits);
        if(followDovi){
            const float framePeak=float(engine::pqToNits(latestDovi_.maxPq));
            peak=std::min(peak,std::max(framePeak,start+1.0f));
        }
        dims[12]=std::max(1.0f,peak-start)*float(effective.rollOffPercent)/100.0f;
        dims[13]=float(effective.peakCapNits);
    }
    dims[14]=0.0f;dims[15]=0.0f;
    if(traceDovi){
        // Gated trace: proves the per-frame curve reached the blit constants.
        // Throttled like the source-side dovi-probe so a long file stays small.
        static int doviFollowCount=0;
        if(doviFollowCount<3||doviFollowCount%200==0){
            veyra::log::info("dovi-follow",std::format("n={} follow={} applied={} profile={} maxPq={:.4f} peakNits={:.1f} highlightStartNits={} (user={}) peakCapNits={} (user={})",
                doviFollowCount,followDovi?1:0,curveFlag!=0.0f?1:0,static_cast<int>(latestDovi_.profile),latestDovi_.maxPq,
                engine::pqToNits(latestDovi_.maxPq),effective.highlightStartNits,hdrCurve.highlightStartNits,
                effective.peakCapNits,hdrCurve.peakCapNits));
        }
        // The counter advances outside the branch, so the periodic sample fires.
        ++doviFollowCount;
    }
    list->SetGraphicsRoot32BitConstants(0,16,dims,0);
    list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(slot+(generated?2:0))*pass_.increment});
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
    bool probeIssued=false;
    const bool observe=probeObserved_&&curveFlag!=0.0f;
    const bool sameScope=probeCurve_==hdrCurve&&probeIdentity_.epoch==identity.epoch&&probeIdentity_.settingsRevision==identity.settingsRevision;
    if(!observe||!sameScope)liveCoverage_={};
    if(probePending_&&pipeline::fenceComplete(fence,probeFenceValue_))publishLiveCoverage(hdrCurve,identity);
    if(!observe)liveCoverage_={};
    const auto probeNow=std::chrono::steady_clock::now();
    if(observe&&!probePending_&&probeNow>=probeNext_&&!probeFailed_&&ensureCoverageProbe(ctx.device())){
        probeNext_=probeNow+std::chrono::milliseconds(250);
        // One descriptor per outstanding probe; never rewrite it while in flight.
        pipeline::makeSrv(ctx.device(),source,graph.outputFormat(),probePass_.heap->GetCPUDescriptorHandleForHeapStart());
        pipeline::StateTracker state;state.set(source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        state.transition(list,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        state.transition(list,probeStats_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto gpu=probePass_.heap->GetGPUDescriptorHandleForHeapStart();
        const auto cpu=probeCpuViews_->GetCPUDescriptorHandleForHeapStart();
        probePass_.bind(list,dims,gpu.ptr,gpu.ptr+probePass_.increment);
        const UINT zero[4]{};list->ClearUnorderedAccessViewUint({gpu.ptr+probePass_.increment},cpu,probeStats_.Get(),zero,0,nullptr);
        state.uavBarrier(list,probeStats_.Get());list->Dispatch(8,5,1);
        state.transition(list,probeStats_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(probeReadback_.Get(),0,probeStats_.Get(),0,kProbeStatsBytes);
        state.transition(list,probeStats_.Get(),D3D12_RESOURCE_STATE_COMMON);
        state.transition(list,source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // Restore the present state for comparisons, subtitles and provider work.
        list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(pass_.rootSig.Get());list->SetPipelineState(pass_.pso.Get());
        list->SetGraphicsRoot32BitConstants(0,16,dims,0);
        list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(slot+(generated?2:0))*pass_.increment});
        probeCurve_=hdrCurve;probeIdentity_=identity;probeIssued=true;
    }
    if(comparison&&!generated&&referencesValid){
        auto* reference=baseReference?graph.baseReference(slot):graph.sourceReference(slot);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=reference;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
        dims[2]=samplingFlags+curveFlag+(graph.hdr10Output()?4:graph.hdrOutput()?0:1)+(graph.videoHdrActive()?8:0);list->SetGraphicsRoot32BitConstants(0,16,dims,0);list->SetGraphicsRootDescriptorTable(1,{pass_.heap->GetGPUDescriptorHandleForHeapStart().ptr+size_t(pipeline::kOutputPoolSlots+slot+(baseReference?2:0))*pass_.increment});
        if(comparison==2)rect.right=LONG(std::clamp((rc.right*.5f+(std::clamp(split,0.0f,1.0f)-view.centerX)*renderW)*sink_.bufferWidth()/rc.right,0.0f,float(sink_.bufferWidth())));list->RSSetScissorRects(1,&rect);list->DrawInstanced(3,1,0,0);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        if(veyra::log::verboseFrameLogs())veyra::log::info("comparison",std::format("real-frame source={} epoch={} revision={} reference={} mode={} split={} (same leased frame)",identity.sourceFrameId,identity.epoch,identity.settingsRevision,baseReference?"base":"input",comparison,split));
    }
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    if(copyBeforePresent_)barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(2,barriers);
    if(copyBeforePresent_){
        D3D12_RESOURCE_BARRIER copy{};copy.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copy.Transition.pResource=bb;copy.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        copy.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;copy.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&copy);list->CopyResource(bb,renderTarget);
        copy.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;copy.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        barriers[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1,&barriers[1]);list->ResourceBarrier(1,&copy);
    }
    // Interpolation region for the XeSS present-sink FG backend.
    // It must follow the PresentBlit view mapping (contain * zoom, view
    // center) rather than assuming the default centered view: the previous
    // exact `view == PreviewView{}` guard disabled generation forever after
    // any wheel zoom or drag, even when the user zoomed back out. The rect is
    // clipped to the window and aligned to even coordinates for the providers.
    const RECT fgRect=presentationRegion(graph.workWidth(),graph.workHeight(),rc.right,rc.bottom,sink_.bufferWidth(),sink_.bufferHeight(),view);
    const bool regionVisible=fgRect.right>fgRect.left&&fgRect.bottom>fgRect.top;
    if(auto* xess=sink_.xess()){
        if(GetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_FAILURE",nullptr,0)>0){
            veyra::log::warn("backend-test","Injected XeSS tagging failure with recorded commands");xessFailed_=true;return false;
        }
        // A history break is not a user disable: the graph supplies initialized
        // zero motion in this case. Keep the current color in XeSS history so
        // the next continuous frame can interpolate without another warmup.
        const bool enabled=regionVisible&&!generated&&!comparison&&graph.presentMotion(slot)&&graph.presentDepth()&&identity.sourceFrameId!=lastXessIdentity_.sourceFrameId&&!xessGenerationSuppressed_;
        const bool reset=!graph.presentMotionValid(slot)||!xessWasEnabled_||identity.epoch!=lastXessIdentity_.epoch||identity.settingsRevision!=lastXessIdentity_.settingsRevision||graph.motionPreviousSource(slot)!=lastXessIdentity_.sourceFrameId||view.quarterTurns()!=previousXessView_.quarterTurns();
        // frameRenderTime is the provider's only independent period estimate on
        // non-Intel GPUs. Feeding present-to-present time back would include the
        // provider's own wait and lengthen the next burst (measured 2026-09-21).
        // Zero makes it infer the period from observed presents, which under
        // load fed the owner-thread Present block back into the burst spacing
        // (8.5 ms in-burst at 4X against a 16.67 ms source, review 2026-09-22).
        // Supply the real consecutive source PTS delta instead: only with valid
        // history, an identity that advanced by exactly one, increasing PTS and
        // a plausible range. Resets, skips and repeats keep zero.
        // VEYRA_DISABLE_XESS_SOURCE_TIMING restores the old zero for A/B runs.
        float elapsed=0.0f;
        static const bool sourceTiming=GetEnvironmentVariableW(L"VEYRA_DISABLE_XESS_SOURCE_TIMING",nullptr,0)==0;
        // Identity may advance by more than one across a bounded preview
        // skip (history kept); the interval the provider must bridge is then
        // the real PTS delta of the two presented sources, not zero.
        if(sourceTiming&&!reset&&lastXessPts100ns_>=0&&sourcePts100ns>lastXessPts100ns_&&
           identity.sourceFrameId>lastXessIdentity_.sourceFrameId){
            const auto delta=double(sourcePts100ns-lastXessPts100ns_)/10000;
            if(delta>=0.125&&delta<500)elapsed=float(delta);
        }
        auto* motion=graph.presentMotion(slot);
        auto* depth=graph.presentDepth();
        if(enabled){
            // Stable full-buffer interpolation avoids provider letterbox
            // reallocations. Guidance must follow the same displayed domain.
            auto* mapped=presentMotion_[slot].Get();
            pipeline::StateTracker states;
            states.transition(list,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            states.transition(list,mapped,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const auto previous=reset?view:previousXessView_;
            const auto [prevRenderW,prevRenderH]=previous.renderedSize(float(reset?rc.right:previousXessWidth_),float(reset?rc.bottom:previousXessHeight_),float(graph.workWidth()),float(graph.workHeight()));
            const float mapping[16]={float(graph.workWidth()),float(graph.workHeight()),float(rc.right),float(rc.bottom),
                renderW,view.centerX,view.centerY,reset?1.0f:0.0f,
                renderH,previous.centerX,previous.centerY,float(view.quarterTurns()),
                float(reset?rc.right:previousXessWidth_),float(reset?rc.bottom:previousXessHeight_),prevRenderW,prevRenderH};
            const auto heap=presentMotionPass_.heap->GetGPUDescriptorHandleForHeapStart().ptr;
            presentMotionPass_.bind(list,mapping,heap+size_t(slot*2)*presentMotionPass_.increment,heap+size_t(slot*2+1)*presentMotionPass_.increment);
            list->Dispatch((graph.workWidth()+7)/8,(graph.workHeight()+7)/8,1);
            states.transition(list,mapped,D3D12_RESOURCE_STATE_COMMON);
            states.transition(list,motion,D3D12_RESOURCE_STATE_COMMON);
            // No graph work or resource reuse may occur between tagging and
            // Present. XeSS transitions from/to COMMON on this same queue.
            // This timer covers application commands, not provider execution.
            gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch);
            const RECT fullBuffer{0,0,LONG(sink_.bufferWidth()),LONG(sink_.bufferHeight())};
            if(!xess->tag(list,bb,mapped,depth,fullBuffer,true,reset,elapsed)){xessFailed_=true;return false;}
            gpuTimer_.mark(list,diagnostics::GpuStage::FgBatch,true);
        }else if(!xess->tag(list,bb,motion,depth,fgRect,false,reset,elapsed)){xessFailed_=true;return false;}
        // A present that carried no NEW source frame is a repeat, not a
        // discontinuity: the provider's history is still valid, there is just
        // nothing new to interpolate from. Clearing the flag on a repeat made
        // the very next frame set reset=1, so under load the provider re-warmed
        // at the repeat rate - which is the XeSS flicker users reported
        // (2026-09-22). Only a real gap invalidates the history.
        const bool xessRepeatOnly=identity.sourceFrameId==lastXessIdentity_.sourceFrameId;
        lastXessFrame_=now;xessWasEnabled_=enabled||(xessWasEnabled_&&xessRepeatOnly);
        if(!xessRepeatOnly)lastXessIdentity_=identity;else lastXessIdentity_.epoch=identity.epoch,lastXessIdentity_.settingsRevision=identity.settingsRevision;
        if(enabled)lastXessPts100ns_=sourcePts100ns;
        previousXessView_=view;previousXessWidth_=rc.right;previousXessHeight_=rc.bottom;
    }

    gpuTimer_.mark(list,diagnostics::GpuStage::Blit,true);gpuTimer_.resolve(list);
    if(!ring.submitAndSignal(commandSlot))return false;gpuTimer_.submitted(ring.lastSignaledValue());
    if(probeIssued){probePending_=true;probeFenceValue_=ring.lastSignaledValue();}
    // Custom: collect the previous probe as soon as the GPU reports it complete.
    // GetCompletedValue is a plain read, so presentation never waits here.
    if(probePending_&&probeReadback_&&pipeline::fenceComplete(fence,probeFenceValue_))
        publishLiveCoverage(hdrCurve,identity);lastBuffer_=backBufferIndex;hasPresented_=true;
    if(presentationQueue_)graph.presentationSubmitted(slot,fence,ring.lastSignaledValue());
    const auto dxgiStart=std::chrono::steady_clock::now();
    reflex_.mark(reflexFrame_,3);reflex_.mark(reflexFrame_,4);
    const bool presented=sink_.present(st);
    reflex_.mark(reflexFrame_,5);reflexFrame_=0;
    const auto presentEnd=std::chrono::steady_clock::now();
    const auto ms=[](auto d){return std::chrono::duration<double,std::milli>(d).count();};
    const bool slow=ms(presentEnd-presentStart)>=80.0;
    if(slow||presentEnd>=nextCostLog_){
        nextCostLog_=presentEnd+std::chrono::seconds(1);
        // Display-side sample (F4, review 2026-09-22): which submissions the
        // display actually scanned out. This is still not a photon measurement.
        static const bool displayStats=GetEnvironmentVariableW(L"VEYRA_TEST_NO_DISPLAY_STATS",nullptr,0)==0;
        const auto stats=displayStats?sink_.sampleFrameStatistics():gfx::PresentSink::FrameStatisticsDelta{};
        if(stats.supported)veyra::log::info("display-stats",std::format("refreshHz={:.3f} presentCalls={} dxgiPresentCount={} refreshes={} presentRefreshDelta={} presentsAhead={} backend={} (DXGI GetFrameStatistics delta over ~1s. These counters CANNOT tell how many frames the panel actually showed: with vsync off and tearing allowed, dxgiPresentCount tracks completed Present calls, and both refresh counters just advance with the monitor. presentsAhead = submitted presents after the last shown one at the sample: the display queue under VSync; with Automatic Present(0,0) it includes frames a newer one replaces; -1 unavailable. Measuring scanout needs PresentMon. XeSS generated presents are provider-internal.)",sink_.displayRefreshHz(),stats.presents,stats.displayed,stats.refreshes,stats.displayedRefresh,stats.ahead,sink_.xess()?"XeSS":"DXGI"));
        else if(FAILED(stats.result))veyra::log::info("display-stats",std::format("unavailable hr=0x{:X} backend={}",unsigned(stats.result),sink_.xess()?"XeSS":"DXGI"));
        const auto scanout=displayStats?sink_.takeDisplayLatency():gfx::PresentSink::DisplayLatencySummary{};
        if(scanout.samples||scanout.compositionMode>=0)veyra::log::info("display-latency",std::format("presentToScanoutP50Ms={:.3f} presentToScanoutP95Ms={:.3f} presentToScanoutMaxMs={:.3f} samples={} compositionMode={} refreshHz={:.3f} (DXGI frame statistics: Present call to the vblank that showed it, VSync and Automatic DXGI presents; Automatic dates only frames named as shown; compositionMode 0 composed 1 overlay 2 none 3 failure -1 unknown; not a photon measurement)",scanout.p50Ms,scanout.p95Ms,scanout.maxMs,scanout.samples,scanout.compositionMode,sink_.displayRefreshHz()));
        const auto& timing=sink_.lastPresentTiming();
        veyra::log::info("present-cost",std::format("generated={} totalMs={:.3f} recordSubmitMs={:.3f} slotWaitMs={:.3f} dxgiMs={:.3f} slow={} source={} epoch={} backend={} resized={} resizeMs={:.3f} beginMs={:.3f} commandsMs={:.3f} beforeMs={:.3f} presentCallMs={:.3f} bufferMs={:.3f} afterMs={:.3f} hr=0x{:X}",generated,
            std::chrono::duration<double,std::milli>(presentEnd-presentStart).count(),
            std::chrono::duration<double,std::milli>(dxgiStart-presentStart).count(),ring.cpuWaitMilliseconds()-slotWaitStart,
            ms(presentEnd-dxgiStart),slow,identity.sourceFrameId,identity.epoch,sink_.xess()?"XeSS":"DXGI",resized,
            ms(resizeEnd-presentStart),ms(beginEnd-resizeEnd),ms(dxgiStart-beginEnd),timing.beforeMs,timing.callMs,timing.bufferMs,timing.afterMs,unsigned(timing.result)));
    }
    xessFailed_=sink_.xessFailed();if(presented)retainedFrame_=retained;return presented;
}
bool VideoPresenter::readPresentedFrameForTest(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,sink::RgbaImage& image){
    if(presentationQueue_&&!presentationRing_.drainQueue())return false;
    if(!hasPresented_||!sink_.swapChain()||!ring.drainQueue())return false;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    return SUCCEEDED(sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer)))&&sink::readRgba8(ctx,ring,buffer.Get(),image);
}
bool VideoPresenter::ensureCoverageProbe(ID3D12Device* device){
    if(probeStats_)return true;
    std::vector<uint8_t> cs;
    if(!pipeline::loadShaderBytes("HdrCurveCoverage.dxil",cs)||!probePass_.create(device,cs,2,1,1,16)){probeFailed_=true;return false;}
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=kProbeStatsBytes;
    desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&probeStats_));
    if(SUCCEEDED(hr)){heap.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&probeReadback_));}
    if(FAILED(hr)){log::warn("present-probe",std::format("statistics unavailable hr=0x{:X}",unsigned(hr)));probeStats_.Reset();probeReadback_.Reset();probeFailed_=true;return false;}
    D3D12_DESCRIPTOR_HEAP_DESC views{};views.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;views.NumDescriptors=1;
    hr=device->CreateDescriptorHeap(&views,IID_PPV_ARGS(&probeCpuViews_));
    if(FAILED(hr)){log::warn("present-probe",std::format("statistics CPU descriptor failed hr=0x{:X}",unsigned(hr)));probeStats_.Reset();probeReadback_.Reset();probeFailed_=true;return false;}
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_UINT;uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=5;
    device->CreateUnorderedAccessView(probeStats_.Get(),nullptr,&uav,{probePass_.heap->GetCPUDescriptorHandleForHeapStart().ptr+probePass_.increment});
    device->CreateUnorderedAccessView(probeStats_.Get(),nullptr,&uav,probeCpuViews_->GetCPUDescriptorHandleForHeapStart());
    log::info("present-probe","GPU statistics 64x36; readback=20 bytes, interval>=250ms, pixelReadbacks=0, additionalSubmits=0");
    return true;
}
void VideoPresenter::publishLiveCoverage(const HdrCurveSettings& curve,pipeline::FrameIdentity identity){
    probePending_=false;
    if(!probeObserved_||!probeReadback_||probeCurve_!=curve||probeIdentity_.epoch!=identity.epoch||probeIdentity_.settingsRevision!=identity.settingsRevision){liveCoverage_={};return;}
    void* mapped=nullptr;const D3D12_RANGE read{0,kProbeStatsBytes};
    const HRESULT hr=probeReadback_->Map(0,&read,&mapped);
    if(FAILED(hr)||!mapped){log::warn("present-probe",std::format("statistics Map failed hr=0x{:X}",unsigned(hr)));liveCoverage_={};return;}
    uint32_t counts[5]{};std::memcpy(counts,mapped,sizeof(counts));const D3D12_RANGE written{};probeReadback_->Unmap(0,&written);
    liveCoverage_={counts[0],counts[1],counts[2],counts[3],counts[0]==kProbeWidth*kProbeHeight};
    // A real black frame has samples with zero events and is valid.
}

void VideoPresenter::close(){
    hdrPeakMonitor_=nullptr;hdrPeakInfo_={};hdrPeakQuery_={};
    refreshMonitor_=nullptr;refreshHz_=0;refreshQuery_={};vrr_={};refreshQueried_=false;vrrSync_=false;
    retainedFrame_.reset();
    xessInputId_=0;xessWork_={};xessWorkPosition_=0;
    reflex_.close();reflexFrame_=0;
    if(presentationRing_.initialized())presentationRing_.drainQueue();
    hasPresented_=false;gpuTimer_.close();compositeTarget_.Reset();copyBeforePresent_=copyEligible_=false;rtvs_.Reset();
    probeStats_.Reset();probeReadback_.Reset();probeCpuViews_.Reset();probePass_={};probePending_=false;probeFailed_=false;probeNext_={};probeCurve_={};probeIdentity_={};liveCoverage_={};pass_={};sink_.shutdown();
    presentMotionPass_={};presentMotion_={};previousXessWidth_=previousXessHeight_=0;
    presentationRing_.shutdown();presentationFence_.Reset();presentationQueue_.Reset();
    if(presentationEvent_){CloseHandle(presentationEvent_);presentationEvent_=nullptr;}
    presentationDevice_=nullptr;presentationFormat_=DXGI_FORMAT_UNKNOWN;window_=nullptr;
}
double VideoPresenter::displayRefreshHz(){
    if(!window_)return 0;
    const auto now=std::chrono::steady_clock::now();
    const auto monitor=MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST);
    if(monitor!=refreshMonitor_||now-refreshQuery_>=std::chrono::seconds(2)){
        const double hz=gfx::PresentSink::displayRefreshFps(window_);
        if(refreshMonitor_&&std::abs(hz-refreshHz_)>0.5)veyra::log::info("display-refresh",std::format("monitor refresh={:.3f} Hz (was {:.3f}; window moved or display mode changed)",hz,refreshHz_));
        refreshHz_=hz;refreshMonitor_=monitor;refreshQuery_=now;sink_.setDisplayRefreshHz(hz);
        const auto vrr=gfx::queryVrr(window_);
        if(!(vrr==vrr_)||!refreshQueried_)veyra::log::info("display-vrr",std::format("level={} known={} monitorSupportsVrr={} currentlyCapable={} trueGsync={} capsKnown={} possible={} requested={} active={} displayInVrrMode={} indicator={} simulated={} status={} refreshHz={:.3f} (NVIDIA G-SYNC / G-SYNC Compatible state of the window's monitor; enabled by the user in the NVIDIA Control Panel, never changed by Veyra)",gfx::vrrLevel(vrr),vrr.known,vrr.supportsVrr,vrr.currentlyCapable,vrr.trueGsync,vrr.capsKnown,vrr.possible,vrr.requested,vrr.enabled,vrr.inVrrMode,vrr.indicator,vrr.simulated,vrr.status,hz));
        vrr_=vrr;refreshQueried_=true;
    }
    return refreshHz_;
}
PresentationSettings VideoPresenter::configurePresentation(gfx::D3D12DeviceContext& ctx,PresentationSettings requested,bool fg,std::wstring& status){
    bool wanted=requested.hdrFgCompatibility&&copyEligible_;
    wchar_t copyOverride[4]{};
    if(GetEnvironmentVariableW(L"VEYRA_TEST_HDR_PRESENT_COPY",copyOverride,4)&&copyOverride[1]==L'\0'){
        if(copyOverride[0]==L'0')wanted=false;
        // Only bounded regression tests may force the copy path on SDR.
        else if(copyOverride[0]==L'1'&&!sink_.xess())wanted=true;
    }
    if(wanted!=copyBeforePresent_){
        // Switching is infrequent and runs on the presentation owner thread.
        // Drain once before changing the target, never in the per-frame path.
        if(presentationRing_.initialized()&&!presentationRing_.drainQueue()){
            veyra::log::warn("present-copy","compatibility switch deferred: presentation queue drain failed");
        }else{
            copyBeforePresent_=wanted;
            if(wanted&&!refresh(ctx.device())){
                copyBeforePresent_=false;
                veyra::log::warn("present-copy","compatibility target setup failed; retaining direct presentation");
            }
            if(!copyBeforePresent_)compositeTarget_.Reset();
            veyra::log::info("present-copy",std::format("requested={} eligible={} enabled={} (user preference; switch drains once)",requested.hdrFgCompatibility,copyEligible_,copyBeforePresent_));
        }
    }
    const bool reflexDisabled=reflex_.disable();reflexFrame_=0;
    const bool followDisplay=requested.outputRate==OutputRateMode::FollowDisplay;
    if(followDisplay){
        const double hz=gfx::PresentSink::displayRefreshFps(sink_.hwnd());
        if(hz>=1.0&&std::isfinite(hz)){requested.outputRate=OutputRateMode::Custom;requested.customFps=hz;}
        else requested.outputRate=OutputRateMode::Off;
    }
    auto effective=requested;
    effective.hdrFgCompatibility=copyBeforePresent_;
    std::wstring capNotice;
    if((xessActive())&&requested.outputRate!=OutputRateMode::Off){
        // Provider owns generated Present calls. Capping our real-frame input
        // would change the source cadence, not cap its final output.
        effective.outputRate=OutputRateMode::Off;
        capNotice=L" · 此补帧提供方暂不支持独立输出限帧；已保留设置";
    }
    // 可变刷新: when the applied monitor mode can request VRR, Automatic presents like
    // explicit Vsync - the G-SYNC/FreeSync + V-SYNC combination. Inside the
    // VRR range the panel starts a refresh when the frame arrives, so the
    // VSync wait is about zero and nothing tears; above the range the
    // one-deep VSync queue below holds. Without VRR (switch off, another
    // monitor, driver setting off) Automatic stays Present(0,0).
    displayRefreshHz();
    vrrSync_=vrrSyncWanted(requested,vrr_);
    if(vrrSync_){requested.display=DisplaySync::Vsync;effective.display=DisplaySync::Vsync;}
    const bool vsync=presentationVsync(requested),tearing=presentationTearing(requested);
    providerOwnedPresentation_=xessActive();
    const std::wstring sync=vrrSync_?(gfx::vrrLevel(vrr_)>=4?L"显示同步：自动 + 可变刷新 · 屏幕跟着画面刷新，不撕裂":L"显示同步：自动 + 可变刷新 · 已请求，等待驱动确认"):requested.display==DisplaySync::Automatic?L"显示同步：自动 · 不撕裂，也不等待垂直同步（延迟最低）":vsync?L"显示同步：垂直同步":L"显示同步：允许撕裂";
    if(!reflexDisabled){effective.enabled=false;sink_.configurePacing(false,vsync,tearing);status=L"Reflex 驱动状态撤销失败；低延迟队列已停用 · "+sync;return effective;}
    if(xessActive()){
        // The provider owns Present for its generated frames, so neither the
        // latency waiter nor an output cap can reach them. Only the DXGI VSync
        // bit still applies (XeSS accepts it on the proxy chain).
        effective.enabled=false;
        providerOwnedPresentation_=true;
        sink_.configurePacing(false,vsync,tearing);
        status=L"XeSS 提供方自行调度：低延迟队列与输出上限不生效 · "+sync;
        return effective;
    }
    providerOwnedPresentation_=false;
    // Only explicit VSync can queue presents behind the display, so only VSync
    // keeps a one-deep display queue, and the engine really waits for it: it
    // acquires the latency object before reading the next capture frame
    // (without frame generation) and before every present, so no frame sits
    // in DXGI's queue. Before latency-min B3 VSync set latency 1 without
    // anyone waiting, which does not limit the queue (7 frames in the DXGI
    // probe; DLSS 6X on 320 Hz 12.2/18.4 ms present->scanout, field: "twice
    // 1.4.4's latency" on a 60 Hz panel, 2026-10-01). Automatic replaces an
    // unshown frame and Tearing never waits: acquiring the latency object
    // there would throttle presents to the refresh, the wait they avoid.
    const bool paced=vsync&&sink_.configurePacing(true,vsync,tearing);
    const bool restored=paced||sink_.configurePacing(false,vsync,tearing);
    const std::wstring queue=paced?(vrrSync_?L"可变刷新：超过屏幕最高刷新时只排 1 帧 · ":L"垂直同步：屏幕队列 1 帧，读帧前等待刷新 · "):vsync?L"垂直同步：屏幕队列控制不可用 · ":restored?L"":L"显示队列恢复失败 · ";
    std::wstring cap;
    if(followDisplay)cap=std::format(L"输出上限：跟随显示器 {:.0f} Hz",requested.customFps);
    else if(requested.outputRate==OutputRateMode::Custom)cap=std::format(L"输出上限：{:.3f} FPS",requested.customFps);
    else cap=L"输出上限：关闭";
    if(!requested.enabled){status=queue+sync;if(effective.outputRate==OutputRateMode::Custom)status+=L" · "+cap;status+=capNotice;return effective;}
    // Low-latency queue: one source frame in flight (the next input is read
    // after the previous one was presented), plus Reflex when frame generation
    // is off (generated outputs would need separately validated markers).
    std::wstring latency=L"低延迟队列：只保留一帧在处理";
    if(fg)latency+=L"（补帧运行，Reflex 不启用）";
    else if(reflex_.enable(ctx.device()))latency+=L" + NVIDIA Reflex（实验）";
    else latency+=L"（Reflex 初始化失败）";
    status=latency+L" · "+cap+L" · "+queue+sync;
    return effective;
}
Microsoft::WRL::ComPtr<ID3D12Resource> VideoPresenter::presentedResourceForTest() {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    if(presentationQueue_&&!presentationRing_.drainQueue())return buffer;
    if(hasPresented_&&sink_.swapChain())sink_.swapChain()->GetBuffer(lastBuffer_,IID_PPV_ARGS(&buffer));
    return buffer;
}

// Optional HDR10 metadata hint. Windows/monitors may ignore it; the curve itself
// is applied to pixels independently. Cache only accepted swapchain state.
}
