#include "CaptureGraphPreparation.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"
#include <chrono>
#include <format>

namespace veyra::engine {
namespace {
struct ComApartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~ComApartment(){if(SUCCEEDED(result))CoUninitialize();}
};

LRESULT CALLBACK previewWndProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wParam,lParam);
}
}

CapturePreviewWindow::~CapturePreviewWindow(){if(window_)DestroyWindow(window_);}

HWND CapturePreviewWindow::open(HWND parent){
    if(window_)return window_;
    RECT rect{};
    if(!GetClientRect(parent,&rect))return nullptr;
    const auto instance=GetModuleHandleW(nullptr);
    constexpr auto name=L"VeyraCapturePreviewWindow";
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=name;cls.lpfnWndProc=previewWndProc;
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOPARENTNOTIFY,name,L"",WS_VISIBLE|WS_CHILD|WS_DISABLED,
                            0,0,rect.right,rect.bottom,parent,nullptr,instance,nullptr);
    if(window_)SetWindowPos(window_,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    return window_;
}

void CapturePreviewWindow::hide(){if(window_)ShowWindow(window_,SW_HIDE);}

void CapturePreviewWindow::dispatchMessages() const{
    if(!window_)return;
    MSG message{};
    while(PeekMessageW(&message,window_,0,0,PM_REMOVE))DispatchMessageW(&message);
}

CaptureGraphPreparation::Result::~Result(){
    presenter.reset();
    opening.reset();
    if(retiringPreview&&graph)graph->shutdownCapturePreview(ring);
    graph.reset();
    ring.shutdown();
    if(event)CloseHandle(event);
}

bool CaptureGraphPreparation::start(gfx::D3D12DeviceContext& ctx,const gfx::CommandSlotRing& render,
                                    ngx::NgxCoreCache& core,pipeline::EnhanceGraphDesc desc,
                                    const std::atomic<bool>& stop,HWND finalWindow,bool captureCompatible){
    if(pending())return false;
    try{
        // Keep the exact queue used by the live graph for provider binding.
        // Each ring has its own allocators and is the only signaler of its fence.
        auto* queue=render.queue();
        const auto slots=render.slotCount();
        future_=std::async(std::launch::async,[&ctx,&core,&stop,queue,slots,finalWindow,captureCompatible,desc=std::move(desc)]{
            using Clock=std::chrono::steady_clock;
            ComApartment com;
            const auto start=Clock::now();
            auto r=std::make_unique<Result>();
            log::info("capture-startup",std::format("event=load-start revision={}",desc.settingsRevision));
            try{
                uint64_t budget=0,before=0,after=0;
                const auto measured=ctx.videoMemoryInfo(budget,before);
                Status status=Status::Ok;
                const auto hr=ctx.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&r->fence));
                if(SUCCEEDED(hr))r->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
                if(r->event&&r->ring.initialize(ctx.device(),queue,r->fence.Get(),r->event,slots,status)){
                    r->graph=std::make_unique<pipeline::EnhanceGraph>(ctx,r->ring,&core);
                    if(stop||!r->graph->initialize(desc))r->failure=r->graph->failedBackend();
                    else if(r->graph->fsrSrRequested()&&!r->graph->fsrSrEnabled())r->failure=FailedBackend::Sr;
                    else if(r->graph->fsrEnabled()&&!r->graph->fsrActive())r->failure=FailedBackend::Fg;
                    else if(!stop&&r->graph->createViews()){
                        const auto warmup=Clock::now();
                        const bool warmed=r->graph->warmCapture()&&r->ring.waitIdle();
                        r->failure=warmed?FailedBackend::None:r->graph->failedBackend();
                        log::info("capture-startup",std::format("event=warm-finish revision={} ready={} ms={:.3f}",
                            desc.settingsRevision,warmed,std::chrono::duration<double,std::milli>(Clock::now()-warmup).count()));
                        if(warmed&&!stop){
                            r->opening=std::make_unique<VideoPresenter::PreparedOpening>();
                            r->ready=VideoPresenter::prepareOpening(ctx,*r->graph,*r->opening);
                            if(r->opening->xess)r->maxGeneratedFrames=int(r->opening->xess->maxInterpolatedFrames());
                            if(r->ready&&finalWindow&&!stop){
                                log::info("capture-startup","event=prepare-final-surface");
                                r->presenter=std::make_unique<VideoPresenter>();
                                r->ready=r->presenter->open(ctx,finalWindow,*r->graph,captureCompatible,false,std::move(r->opening))&&
                                    (!r->graph->xessEnabled()||r->presenter->xessActive());
                            }
                            if(!r->ready)r->failure=r->graph->xessEnabled()?FailedBackend::Fg:FailedBackend::Infrastructure;
                        }
                    }
                    r->vfgMediumFault=r->graph->vfgAdaMediumMemoryFault();
                    if(!r->graph->xessEnabled())r->maxGeneratedFrames=r->graph->fgMultiFrameCountMax();
                    if(r->graph->fsrActive()||r->graph->amfFrcActive())r->maxGeneratedFrames=1;
                    if(r->ready&&measured&&ctx.videoMemoryInfo(budget,after)&&after>before)r->bytes=after-before;
                }else log::warn("capture-startup",std::format("preparation ring failed hr=0x{:X} status={}",unsigned(hr),unsigned(status)));
            }catch(const std::exception& e){log::error("capture-startup",std::format("preparation failed: {}",e.what()));}
            if(!r->ready){r->presenter.reset();r->opening.reset();r->graph.reset();}
            log::info("capture-startup",std::format("event=load-finish revision={} ready={} failure={} ms={:.3f}",
                desc.settingsRevision,r->ready,unsigned(r->failure),std::chrono::duration<double,std::milli>(Clock::now()-start).count()));
            return r;
        });
        return true;
    }catch(const std::exception& e){
        log::error("capture-startup",std::format("could not start preparation: {}",e.what()));
        return false;
    }
}

std::unique_ptr<CaptureGraphPreparation::Result> CaptureGraphPreparation::takeReady(){
    if(!pending()||future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
    try{return future_.get();}
    catch(const std::exception& e){
        log::error("capture-startup",std::format("preparation worker failed: {}",e.what()));
        return std::make_unique<Result>();
    }
}

void CaptureGraphPreparation::discard(std::unique_ptr<Result> r){
    // Release feature handles and provisional textures off the render thread.
    try{
        future_=std::async(std::launch::async,[r=std::move(r)]() mutable -> std::unique_ptr<Result>{
            ComApartment com;
            r.reset();
            return {};
        });
    }catch(const std::exception& e){
        log::warn("capture-startup",std::format("could not start cleanup worker: {}",e.what()));
    }
}

void CaptureGraphPreparation::finish(const CapturePreviewWindow& window){
    while(pending()&&future_.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout)
        window.dispatchMessages();
    future_={};
}
}
