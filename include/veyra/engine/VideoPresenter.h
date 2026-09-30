#pragma once
#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/engine/PreviewView.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/ReflexSession.h"
#include "veyra/engine/PresentationSettings.h"
#include <chrono>
#include <array>
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }
namespace veyra::sink { struct RgbaImage; }
namespace veyra::engine {
class VideoPresenter {
public:
    ~VideoPresenter(){close();}
    // mediaClockPaced: the owner already waits on a media clock before each
    // source (file playback). Retained for diagnostics; XeLL low latency must
    // stay enabled or the provider refuses generation (result -15).
    bool open(gfx::D3D12DeviceContext&, HWND, pipeline::EnhanceGraph&, bool captureCompatible=false, bool mediaClockPaced=false);
    bool present(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,unsigned slot,bool generated,bool referencesValid=true,int comparison=0,bool baseReference=false,float split=.5f,pipeline::FrameIdentity identity={},PreviewView view={},int64_t sourcePts100ns=-1);
    void close();
    // Present-sink providers (XeSS) block inside Present while pacing
    // their generated frames. A helper-thread Present was tried on
    // 2026-09-22 (X3): the provider then estimated its period from the
    // longer hand-off intervals and the source rate fell 51 -> 24/s at 44%
    // GPU. The synchronous call stays; see FG_INDEPENDENT_REPAIR_EXECUTION.
    bool beginSourceInput();
    bool beginSourceProcessing();
    void sourceProcessed(pipeline::FrameIdentity);
    PresentationSettings configurePresentation(gfx::D3D12DeviceContext&,PresentationSettings,bool fg,std::wstring& status);
    bool presentationReady(){return sink_.presentationReady();}
    uint64_t beginReflex(){return reflex_.begin();}
    void reflexFrame(uint64_t id){reflexFrame_=id;}
    // True while XeSS owns Present: the low-latency queue and the output
    // cap cannot reach provider-generated frames, so the UI greys them out.
    bool providerOwnedPresentation()const{return providerOwnedPresentation_;}

    bool reflexActive()const{return reflex_.active();}
    bool reflexDisablePending()const{return reflex_.disablePending();}
    bool pacingActive()const{return sink_.pacingActive();}
    uint64_t generation()const{return generation_;}
    // Explicit integration-test capture only; never called by playback/export.
    bool readPresentedFrameForTest(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,sink::RgbaImage&);
    // Test-only buffer reference for lossless HDR readback. Drains the private
    // queue; caller drains the shared queue and releases before resize/close.
    Microsoft::WRL::ComPtr<ID3D12Resource> presentedResourceForTest();
    uint64_t submittedCount() const {return sink_.presentCount();}
    uint64_t consumerFenceValue(uint64_t sharedValue)const{return presentationQueue_?presentationRing_.lastSignaledValue():sharedValue;}
    double cpuWaitMilliseconds(const gfx::CommandSlotRing& shared)const{return presentationQueue_?presentationRing_.cpuWaitMilliseconds():shared.cpuWaitMilliseconds();}
    uint64_t xessGeneratedCount() const {return sink_.xess()?sink_.xess()->generatedCount():0;}
    uint64_t xessPresentedCount() const {return sink_.xess()?sink_.xess()->presentedCount():0;}
    bool xessActive() const {return sink_.xess()!=nullptr;}
    bool xessFailed() const {return xessFailed_;}
    // Sustained under-rate may suppress SDK-owned XeSS-FG generation over a
    // stable interval (xefgSwapChainSetEnabled); re-enabling goes through the
    // per-frame history reset, never a per-frame toggle.
    void setXessGenerationSuppressed(bool v){xessGenerationSuppressed_=v;}
    bool xessGenerationSuppressed() const {return xessGenerationSuppressed_;}
    diagnostics::GpuSample blitTiming(ID3D12Fence* f,uint64_t revision=0,uint64_t epoch=0){gpuTimer_.collect(presentationFence_?presentationFence_.Get():f);if((revision&&gpuTimer_.last().identity.settingsRevision!=revision)||(epoch&&gpuTimer_.last().identity.epoch!=epoch)){diagnostics::GpuSample pending;pending.state=diagnostics::SampleState::Pending;return pending;}return gpuTimer_.last().gpu[size_t(diagnostics::GpuStage::Blit)];}
    // Application-side frame-generation timing for present-sink backends
    // (XeSS): the copies, barriers and provider prepare work recorded on
    // our list. collect() is idempotent, so this is safe alongside blitTiming.
    diagnostics::GpuSample fgTiming(ID3D12Fence* f,uint64_t revision=0,uint64_t epoch=0){gpuTimer_.collect(f);if((revision&&gpuTimer_.last().identity.settingsRevision!=revision)||(epoch&&gpuTimer_.last().identity.epoch!=epoch)){diagnostics::GpuSample pending;pending.state=diagnostics::SampleState::Pending;return pending;}return gpuTimer_.last().gpu[size_t(diagnostics::GpuStage::FgBatch)];}
std::vector<diagnostics::GpuFrameTiming> takeGpuTimings(ID3D12Fence* fence){gpuTimer_.collect(presentationFence_?presentationFence_.Get():fence);return gpuTimer_.takeCompleted();}
void recordGpuTimings(){gpuTimer_.recordCompleted();}
private:
    gfx::ReflexSession reflex_;
    uint64_t reflexFrame_=0,generation_=0;
    bool providerOwnedPresentation_=false;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> presentationQueue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> presentationFence_;
    HANDLE presentationEvent_=nullptr;
    gfx::CommandSlotRing presentationRing_;
    diagnostics::GpuTimer gpuTimer_;
    gfx::PresentSink sink_;
    pipeline::GraphicsPass pass_;
    pipeline::ComputePass presentMotionPass_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> presentMotion_;
    PreviewView previousXessView_{};
    unsigned previousXessWidth_=0,previousXessHeight_=0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvs_;
    unsigned inc_=0;
    unsigned lastBuffer_=0;bool hasPresented_=false;
    HWND window_=nullptr;
    std::chrono::steady_clock::time_point lastResize_{};
    unsigned viewWidth_=0,viewHeight_=0;
    HMONITOR bufferMonitor_=nullptr;
    unsigned monitorWidth_=0,monitorHeight_=0;
    std::chrono::steady_clock::time_point nextCostLog_{};
    std::chrono::steady_clock::time_point lastXessFrame_{};
    pipeline::FrameIdentity lastXessIdentity_{};
    int64_t lastXessPts100ns_=-1;
    bool xessWasEnabled_=false;
    bool xessFailed_=false;
    bool xessGenerationSuppressed_=false;
    uint32_t xessInputId_=0;
    struct XessWork {pipeline::FrameIdentity identity{};uint32_t id=0;};
    std::array<XessWork,4> xessWork_{};
    size_t xessWorkPosition_=0;
    void refresh(ID3D12Device*);
};
}
