#pragma once
#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <atomic>
#include <future>

namespace veyra::engine {
// The temporary DXGI surface leaves the normal video HWND available while a
// different provider or output format creates its final swapchain.
class CapturePreviewWindow {
public:
    CapturePreviewWindow()=default;
    ~CapturePreviewWindow();
    CapturePreviewWindow(const CapturePreviewWindow&)=delete;
    CapturePreviewWindow& operator=(const CapturePreviewWindow&)=delete;
    HWND open(HWND parent);
    void hide();
    void dispatchMessages() const;
private:
    HWND window_=nullptr;
};

// The engine still owns the capture, presenter and render ring. While only
// the source graph is live, this worker is the only user of the NGX core.
class CaptureGraphPreparation {
public:
    struct Result {
        ~Result();
        bool ready=false;
        bool retiringPreview=false;
        bool vfgMediumFault=false;
        int maxGeneratedFrames=0;
        FailedBackend failure=FailedBackend::Infrastructure;
        uint64_t bytes=0;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        HANDLE event=nullptr;
        gfx::CommandSlotRing ring;
        std::unique_ptr<pipeline::EnhanceGraph> graph;
        std::unique_ptr<VideoPresenter::PreparedOpening> opening;
        std::unique_ptr<VideoPresenter> presenter;
    };

    bool start(gfx::D3D12DeviceContext&,const gfx::CommandSlotRing&,
               ngx::NgxCoreCache&,pipeline::EnhanceGraphDesc,const std::atomic<bool>& stop,
               HWND finalWindow,bool captureCompatible);
    bool pending() const {return future_.valid();}
    std::unique_ptr<Result> takeReady();
    void discard(std::unique_ptr<Result>);
    void finish(const CapturePreviewWindow&);

private:
    std::future<std::unique_ptr<Result>> future_;
};
}
