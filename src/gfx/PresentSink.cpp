#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/PresentationHooks.h"

#include <windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <format>
#include <chrono>
#include <vector>
#include <mutex>

#include "veyra/Log.h"
#include "veyra/diagnostics/CpuStallTrace.h"

namespace veyra::gfx {

namespace {

const wchar_t* kWindowClassName = L"VeyraPresentSink";


} // namespace

bool PresentSink::hdrDisplayActive(HWND window){
    return queryHdrDisplayActive(window).value_or(false);
}

std::optional<bool> PresentSink::queryHdrDisplayActive(HWND window,HMONITOR* queriedMonitor){
    const auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
    if(queriedMonitor)*queriedMonitor=monitor;
    if(!monitor)return std::nullopt;
    static ComPtr<IDXGIFactory1> cachedFactory;static std::mutex factoryMutex;
    ComPtr<IDXGIFactory1> factory;
    {
        std::lock_guard lock(factoryMutex);
        if(cachedFactory&&!cachedFactory->IsCurrent())cachedFactory.Reset();
        if(!cachedFactory){
            const auto hr=CreateDXGIFactory1(IID_PPV_ARGS(&cachedFactory));
            if(FAILED(hr)){log::warn("display-color",std::format("CreateDXGIFactory1 query failed hr=0x{:X}",unsigned(hr)));cachedFactory.Reset();return std::nullopt;}
        }
        factory=cachedFactory;
    }
    HRESULT hr=S_OK;
    for(UINT a=0;;++a){
        ComPtr<IDXGIAdapter1> adapter;hr=factory->EnumAdapters1(a,&adapter);
        if(hr==DXGI_ERROR_NOT_FOUND)break;
        if(FAILED(hr)){log::warn("display-color",std::format("EnumAdapters1 query failed hr=0x{:X}",unsigned(hr)));return std::nullopt;}
        for(UINT i=0;;++i){
            ComPtr<IDXGIOutput> output;hr=adapter->EnumOutputs(i,&output);
            if(hr==DXGI_ERROR_NOT_FOUND)break;
            if(FAILED(hr)){log::warn("display-color",std::format("EnumOutputs query failed hr=0x{:X}",unsigned(hr)));return std::nullopt;}
            DXGI_OUTPUT_DESC basic{};hr=output->GetDesc(&basic);
            if(FAILED(hr)){log::warn("display-color",std::format("GetDesc query failed hr=0x{:X}",unsigned(hr)));continue;}
            if(basic.Monitor!=monitor)continue;
            ComPtr<IDXGIOutput6> advanced;hr=output.As(&advanced);
            // An older output interface cannot expose HDR. Other failures
            // leave the previously established output contract untouched.
            if(hr==E_NOINTERFACE)return false;
            if(FAILED(hr)){log::warn("display-color",std::format("IDXGIOutput6 query failed hr=0x{:X}",unsigned(hr)));return std::nullopt;}
            DXGI_OUTPUT_DESC1 desc{};hr=advanced->GetDesc1(&desc);
            if(FAILED(hr)){log::warn("display-color",std::format("GetDesc1 query failed hr=0x{:X}",unsigned(hr)));return std::nullopt;}
            if(desc.Monitor!=monitor)return std::nullopt;
            return desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }
    return std::nullopt;
}

PresentSink::DisplayPeakInfo PresentSink::queryDisplayPeak(HWND window){
    DisplayPeakInfo info{};
    // Without a window (the UI asking before playback starts) answer about the
    // primary monitor, which is what a fresh install would be showing on.
    HMONITOR monitor=nullptr;
    if(window){
        MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);
        monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        if(!monitor||!GetMonitorInfoW(monitor,&mi))return info;
    }else{
        monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    }
    if(!monitor)return info;
    // Called on demand (the user asking for the system value), so a fresh factory is
    // fine here - no need to join the cached one the 2 s HDR poll uses.
    ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||!factory)return info;
    for(UINT a=0;;++a){
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        if(!adapter)break;
        for(UINT i=0;;++i){
            ComPtr<IDXGIOutput> output;
            if(adapter->EnumOutputs(i,&output)==DXGI_ERROR_NOT_FOUND)break;
            if(!output)break;
            DXGI_OUTPUT_DESC basic{};if(FAILED(output->GetDesc(&basic)))continue;
            if(basic.Monitor!=monitor)continue;
            ComPtr<IDXGIOutput6> advanced;if(FAILED(output.As(&advanced)))return info;
            DXGI_OUTPUT_DESC1 desc{};if(FAILED(advanced->GetDesc1(&desc)))return info;
            if(!std::isfinite(desc.MaxLuminance)||desc.MaxLuminance<100.0f||desc.MaxLuminance>10000.0f)return info;
            info.peakNits=unsigned(std::lround(desc.MaxLuminance));
            info.fullFrameNits=std::isfinite(desc.MaxFullFrameLuminance)&&desc.MaxFullFrameLuminance>0
                ?unsigned(std::lround(std::min(desc.MaxFullFrameLuminance,10000.0f))):0;
            info.minNits=std::isfinite(desc.MinLuminance)&&desc.MinLuminance>=0?desc.MinLuminance:0;
            info.valid=info.peakNits>0;
            return info;
        }
    }
    return info;
}

double PresentSink::displayRefreshFps(HWND window){
    MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);
    const auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
    if(!monitor||!GetMonitorInfoW(monitor,&mi))return 0;
    UINT32 pathCount=0,modeCount=0;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)==ERROR_SUCCESS){
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr)==ERROR_SUCCESS){
            for(UINT32 i=0;i<pathCount;++i){const auto& path=paths[i];
                DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
                if(DisplayConfigGetDeviceInfo(&name.header)==ERROR_SUCCESS&&wcscmp(name.viewGdiDeviceName,mi.szDevice)==0&&path.targetInfo.refreshRate.Denominator&&path.targetInfo.refreshRate.Numerator)
                    return double(path.targetInfo.refreshRate.Numerator)/path.targetInfo.refreshRate.Denominator;
            }
        }
    }
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(!EnumDisplaySettingsExW(mi.szDevice,ENUM_CURRENT_SETTINGS,&mode,0)||mode.dmDisplayFrequency==0)return 0;
    // Older/remote drivers may expose only the integer fallback.
    return double(mode.dmDisplayFrequency);
}

PresentSink::~PresentSink()
{
    shutdown();
}

LRESULT CALLBACK PresentSink::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // Buffer recreation is driven by the engine loop (resize()).
        return DefWindowProcW(hwnd, msg, wp, lp);
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool PresentSink::initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                             const Desc& desc, Status& status)
{
    device_ = device;
    // These are borrowed until shutdown. Bind the current queue before any
    // fallible creation: a failed reopen must never signal the previous queue.
    queue_ = queue;
    shutdownCalled_ = false;
    desc_ = desc;
    width_ = desc.width;
    height_ = desc.height;
    if (!device_ || !queue_ || (desc.targetWindow && !IsWindow(desc.targetWindow))) {
        log::warn("present", "sink initialization refused invalid device, queue or target window");
        status = Status::WindowFailure;
        return false;
    }

    // Register a window class once per process.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &PresentSink::wndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClassRegistered_ = RegisterClassExW(&wc) != 0;
    if (!windowClassRegistered_) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log::error("present", std::format("RegisterClassExW failed err={}", err));
            status = Status::WindowFailure;
            return false;
        }
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    AdjustWindowRect(&rect, style, FALSE);
    hwnd_ = desc.targetWindow ? desc.targetWindow : CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, kWindowClassName, desc_.title.c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        log::error("present", std::format("CreateWindowExW failed err={}", GetLastError()));
        status = Status::WindowFailure;
        return false;
    }
    if (!desc.targetWindow) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    // Factory with tearing awareness.
    UINT factoryFlags = 0;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)))) {
        log::error("present", "CreateDXGIFactory2 failed");
        status = Status::WindowFailure;
        return false;
    }
    factory_ = factory;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing)))) {
            tearingSupported_ = allowTearing != FALSE;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = desc.hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:desc.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    // Opt-in capture experiment; actual capture support depends on the
    // external capture API. Neither mode changes the pixel format.
    scd.SwapEffect = desc.captureCompatible ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if(desc.waitable&&!desc.xess)scd.Flags|=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    // XeSS is the only provider that owns a swapchain. FSR is now a graph
    // effect producing application-owned textures, so it never retains an HWND.
    if(desc.xess){
        // The XeSS initializer writes through the pointer: release any
        // existing reference first so the previous swapchain (native or a
        // proxy that was kept) is actually released instead of leaked.
        if(xess_){
            // Re-initializing the same backend (a settings change): the old
            // wrapper owns the window's swapchain slot until it is destroyed.
            for(auto& b:backBuffers_)b.Reset();
            swapChain_.Reset();swapChain4_.Reset();
            xess_.reset();
        }
        for(auto& b:backBuffers_)b.Reset();
        swapChain_.Reset();swapChain4_.Reset();
        xess_=std::make_unique<XessPresenter>();
        if(!xess_->initialize(device,queue,factory_.Get(),hwnd_,scd,swapChain_.GetAddressOf(),desc.fgMultiplier,desc.xessLowLatencySleep)){
            // XeSS is an optional experimental presenter. A missing or
            // incompatible local runtime must not prevent basic playback.
            log::warn("present", "XeSS FG initialization failed; falling back to native presentation");
            swapChain_.Reset();swapChain4_.Reset();
            xess_.reset();
        }
    }
    if (!swapChain_) {
    const auto createNative=[&](){
        HRESULT result=E_FAIL;
        // E_ACCESSDENIED: the window still has the previous swapchain. That is normally gone
        // within a frame or two, but an overlay that hooks presentation (GamePP in the field
        // log 2026-10-01) can hold it longer; wait up to ~1.3 s before failing the rebuild.
        constexpr unsigned attempts=8;
        for(unsigned attempt=0;attempt<attempts;++attempt){
            swapChain1.Reset();
            result=factory_->CreateSwapChainForHwnd(queue,hwnd_,&scd,nullptr,nullptr,&swapChain1);
            if(result!=E_ACCESSDENIED||!IsWindow(hwnd_)||attempt+1==attempts)break;
            log::warn("present",std::format("swapchain transient access denied attempt={}; bounded owner-thread retry",attempt+1));
            Sleep(std::min(50u*(attempt+1),300u));
        }
        return result;
    };
    HRESULT created=createNative();
    if(FAILED(created)&&created!=E_ACCESSDENIED&&(scd.Flags&DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)){
        log::warn("pacing",std::format("waitable swapchain unavailable hr=0x{:X}; retrying baseline",unsigned(created)));
        scd.Flags&=~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        swapChain1.Reset();
        created=createNative();
    }
    if (FAILED(created)) {
        log::error("present",std::format("CreateSwapChainForHwnd failed hr=0x{:X}",unsigned(created)));
        status = Status::WindowFailure;
        return false;
    }
    if (FAILED(swapChain1.As(&swapChain_))) {
        log::error("present", "QI IDXGISwapChain3 failed");
        status = Status::WindowFailure;
        return false;
    }
    }
    const auto space=desc.hdr10?DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:desc.hdr?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT support=0;const auto check=swapChain_->CheckColorSpaceSupport(space,&support);
    if(FAILED(check)||!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)||FAILED(swapChain_->SetColorSpace1(space))){log::error("present","Requested output color space is unavailable");status=Status::WindowFailure;return false;}
    log::info("present",desc.hdr10?"output=HDR10 RGB10 PQ/BT2020":desc.hdr?"output=scRGB FP16 (1=80 nits)":"output=SDR RGB G22");
    // Block ALT+ENTER; the engine owns mode changes.
    (void)factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    if (!refetchBackBuffers()) {
        status = Status::WindowFailure;
        return false;
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;

    DXGI_SWAP_CHAIN_DESC1 actual{};
    const HRESULT descResult=swapChain_->GetDesc1(&actual);
    const bool flip=actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL||actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if(FAILED(descResult)||!flip||actual.Format!=scd.Format||actual.BufferCount!=3||
       actual.Width!=scd.Width||actual.Height!=scd.Height||actual.SampleDesc.Count!=1||actual.SampleDesc.Quality!=0){
        log::error("present",std::format("swapchain contract rejected hr=0x{:X} requestedMode={} actualMode={} format={} buffers={} extent={}x{} samples={}/{}",unsigned(descResult),unsigned(scd.SwapEffect),unsigned(actual.SwapEffect),unsigned(actual.Format),actual.BufferCount,actual.Width,actual.Height,actual.SampleDesc.Count,actual.SampleDesc.Quality));
        status=Status::WindowFailure;return false;
    }
    if(actual.SwapEffect!=scd.SwapEffect)log::warn("present",std::format("swapchain mode negotiated requested={} actual={} (compatible flip model)",unsigned(scd.SwapEffect),unsigned(actual.SwapEffect)));
    swapChainFlags_=actual.Flags;
    if(actual.Flags&DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT){
        latencyHandle_=swapChain_->GetFrameLatencyWaitableObject();
        if(!configurePacing(false,desc.vsync,desc.tearing))log::warn("pacing","baseline latency configuration failed; playback remains available");
    }
    tearingSupported_=tearingSupported_&&(actual.Flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)!=0;
    displayRefreshHz_=displayRefreshFps(hwnd_);
    frameStatisticsBaseValid_=false;
    log::info("display-refresh",std::format("monitor refresh={:.3f} Hz at swapchain creation (0 = unknown); submissions above this rate cannot all be scanned out",displayRefreshHz_));
    log::info("present", std::format("present-sink: window {}x{} swapEffect={} buffers=3 vsync={} tearing={} captureCompatible={} (capture not verified)",
        width_, height_, actual.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL?"flip-sequential":"flip-discard", desc_.vsync ? 1 : 0, (desc_.tearing && tearingSupported_) ? 1 : 0, desc.captureCompatible));
    return true;
}

bool PresentSink::refetchBackBuffers()
{
    for (auto& b : backBuffers_) b.Reset();
    for (UINT i = 0; i < 3; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])))) {
            log::error("present", std::format("GetBuffer({}) failed", i));
            return false;
        }
    }
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    return true;
}

IDXGISwapChain4* PresentSink::swapChain4(){
    if(!swapChain_){swapChain4_.Reset();return nullptr;}
    if(!swapChain4_&&FAILED(swapChain_.As(&swapChain4_)))swapChain4_.Reset();
    return swapChain4_.Get();
}

void PresentSink::recreateSwapChain(UINT flags)
{
    (void)flags;
    // Not used anymore: ResizeBuffers failure is a hard error surfaced to
    // the engine (P0.6). Kept as a no-op stub for link compatibility.
}

bool PresentSink::processMessages(bool& windowClosed)
{
    windowClosed = false;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            windowClosed = true;
            closed_ = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

ID3D12Resource* PresentSink::currentBackBuffer(uint32_t* acquiredIndex)
{
    if(!swapChain_)return nullptr;
    // Provider proxy swapchains own their buffer rotation. Acquire once for
    // both the resource barriers and the matching render-target descriptor.
    backBufferIndex_=swapChain_->GetCurrentBackBufferIndex();
    if(backBufferIndex_>=3)return nullptr;
    if(acquiredIndex)*acquiredIndex=backBufferIndex_;
    return backBuffers_[backBufferIndex_].Get();
}

bool PresentSink::present(Status& status)
{
    using Clock=std::chrono::steady_clock;
    auto begin=Clock::now();
    const auto split=[&]{const auto end=Clock::now();const double ms=std::chrono::duration<double,std::milli>(end-begin).count();begin=end;return ms;};
    presentTiming_={};
    capacityAcquired_=false;
    xessFailed_=false;
    const UINT syncInterval = desc_.vsync ? 1 : 0;
    const UINT flags = (desc_.tearing && !desc_.vsync && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ++attemptedPresentCount_;
    const bool beforeOk=!xess_||xess_->beforePresent();
    presentTiming_.beforeMs=split();
    if(!beforeOk){xessFailed_=true;status=Status::WindowFailure;return false;}
    const HRESULT hr = swapChain_->Present(syncInterval, flags);
    presentTiming_.callMs=split();presentTiming_.result=hr;
    if(attemptedPresentCount_<=3||attemptedPresentCount_%120==0)
        log::info("present-contract",std::format("backend={} sync={} flags=0x{:X} hr=0x{:X}",xess_?"XeSS":"DXGI",syncInterval,flags,unsigned(hr)));
    if (SUCCEEDED(hr)) {
        ++presentCount_;
        backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
        presentTiming_.bufferMs=split();
        const bool afterOk=!xess_||xess_->afterPresent();
        presentTiming_.afterMs=split();
        if(!afterOk){xessFailed_=true;status=Status::WindowFailure;return false;}
        return true;
    }
    ++failedPresentCount_;
    const HRESULT removed = device_ != nullptr ? device_->GetDeviceRemovedReason() : S_OK;
    log::error("present", std::format(
        "Present FAILED hr=0x{:X} sync={} flags=0x{:X} attempted={} failed={} removedReason=0x{:X}",
        static_cast<unsigned>(hr), syncInterval, flags,
        static_cast<unsigned long long>(attemptedPresentCount_),
        static_cast<unsigned long long>(failedPresentCount_),
        static_cast<unsigned>(removed)));
    // Device-lost class failures: dump DRED evidence (breadcrumbs + page
    // fault) before reporting the failure to the engine.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
        removed != S_OK) {
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (device_ != nullptr && SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dred)))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs))) {
                UINT count = 0;
                for (const D3D12_AUTO_BREADCRUMB_NODE* n = crumbs.pHeadAutoBreadcrumbNode;
                     n != nullptr && count < 8; n = n->pNext, ++count) {
                    log::error("present", std::format(
                        "DRED breadcrumb #{}: lastOp={} of {} on cmdList={} cmdQueue={}",
                        count, n->pLastBreadcrumbValue ? static_cast<int>(*n->pLastBreadcrumbValue) : -1,
                        n->BreadcrumbCount, n->pCommandList != nullptr ? "ptr" : "null",
                        n->pCommandQueue != nullptr ? "ptr" : "null"));
                }
                if (crumbs.pHeadAutoBreadcrumbNode == nullptr) {
                    log::error("present", "DRED breadcrumbs: none (CPU-side removal)");
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault))) {
                log::error("present", std::format("DRED pageFaultVA=0x{:X}",
                    static_cast<unsigned long long>(fault.PageFaultVA)));
            }
        }
        status = Status::DeviceFailure;
        return false;
    }
    status = Status::WindowFailure;
    return false;
}

PresentSink::FrameStatisticsDelta PresentSink::sampleFrameStatistics(){
    FrameStatisticsDelta delta;
    if(!swapChain_)return delta;
    DXGI_FRAME_STATISTICS stats{};
    delta.result=swapChain_->GetFrameStatistics(&stats);
    // DXGI_ERROR_FRAME_STATISTICS_DISJOINT: mode change or first frames; a
    // proxy swapchain may also refuse the query. Restart the baseline.
    if(FAILED(delta.result)){frameStatisticsBaseValid_=false;return delta;}
    if(frameStatisticsBaseValid_){
        delta.supported=true;
        delta.presents=presentCount_-frameStatisticsBasePresents_;
        // PresentCount is the one that counts frames the display actually
        // showed. PresentRefreshCount is a vblank index: its delta is just the
        // refresh count, so using it as "displayed" made every configuration
        // look like it displayed exactly refreshHz frames and made
        // refreshes-displayed structurally zero (bug found 2026-09-22).
        delta.displayed=uint64_t(stats.PresentCount-frameStatisticsBaseDisplayed_);
        delta.displayedRefresh=uint64_t(stats.PresentRefreshCount-frameStatisticsBasePresentRefresh_);
        delta.refreshes=uint64_t(stats.SyncRefreshCount-frameStatisticsBaseSyncRefresh_);
    }
    frameStatisticsBaseValid_=true;
    frameStatisticsBasePresents_=presentCount_;
    frameStatisticsBaseDisplayed_=stats.PresentCount;
    frameStatisticsBasePresentRefresh_=stats.PresentRefreshCount;
    frameStatisticsBaseSyncRefresh_=stats.SyncRefreshCount;
    return delta;
}

bool PresentSink::configurePacing(bool enabled,bool vsync,bool tearing){
    // XeSS owns pacing, but its proxy accepts DXGI VSync independently.
    // Do not install another latency waiter on the provider swap chain.
    if(xess_){pacing_=false;capacityAcquired_=false;desc_.vsync=xess_&&vsync;desc_.tearing=tearing;log::info("pacing",std::format("provider={} applicationWait=0 vsync={} tearing={}","XeSS",desc_.vsync,desc_.tearing));return !enabled;}
    const HRESULT hr=latencyHandle_?swapChain_->SetMaximumFrameLatency(enabled?1:3):enabled?E_NOTIMPL:S_OK;
    log::info("pacing",std::format("DXGI enabled={} maximumLatency={} vsync={} tearing={} hr=0x{:X}",enabled,enabled?1:3,vsync,tearing,unsigned(hr)));
    pacing_=enabled&&SUCCEEDED(hr);desc_.vsync=vsync;desc_.tearing=tearing;capacityAcquired_=false;
    return SUCCEEDED(hr);
}
bool PresentSink::presentationReady(){
    if(!pacing_||!latencyHandle_||capacityAcquired_)return true;
    const auto result=WaitForSingleObject(latencyHandle_,0);
    if(result==WAIT_OBJECT_0)capacityAcquired_=true;
    if(result==WAIT_FAILED){log::warn("pacing",std::format("DXGI capacity wait failed error={}",GetLastError()));configurePacing(false,desc_.vsync,desc_.tearing);return true;}
    return capacityAcquired_;
}

bool PresentSink::waitForQueueIdle()
{
    if (queue_ == nullptr || device_ == nullptr) return true;
    ComPtr<ID3D12Fence> idleFence;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence)))) return false;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (ev == nullptr) return false;
    bool completed=false;
    const HRESULT signal = queue_->Signal(idleFence.Get(), 1);
    const HRESULT wait = SUCCEEDED(signal) ? idleFence->SetEventOnCompletion(1, ev) : signal;
    if (SUCCEEDED(wait)) {
        const DWORD result = WaitForSingleObject(ev, 5000);
        completed=result==WAIT_OBJECT_0;
        if (result != WAIT_OBJECT_0) log::warn("present", std::format("queue idle wait result={}", result));
    } else {
        log::warn("present", std::format("queue idle setup failed hr=0x{:X}", static_cast<unsigned>(wait)));
    }
    CloseHandle(ev);
    return completed;
}

bool PresentSink::resizeDue() const
{
    // After a failure, wait 0.2, 0.4 ... up to 3.2 s before the next attempt; the old
    // buffers keep presenting (stretched) meanwhile.
    return resizeFailures_ == 0 ||
        GetTickCount64() - lastResizeFailureMs_ >= (100ull << std::min<uint32_t>(resizeFailures_, 5));
}

void PresentSink::resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;
    if (!resizeDue()) return;
    diagnostics::CpuStallTrace trace("resize-stall");
    const UINT flags = swapChainFlags_;
    // DXGI spec compliance before ResizeBuffers: wait for outstanding GPU
    // work on the presenting queue, then release ALL back-buffer references.
    if (!waitForQueueIdle()) return;
    trace.mark("queueIdle");
    for (auto& b : backBuffers_) b.Reset();
    trace.mark("releaseBuffers");
    HRESULT hr = swapChain_->ResizeBuffers(3, width, height,
        desc_.hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:desc_.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM, flags);
    trace.mark("resizeBuffers");
    if (FAILED(hr)) {
        ++resizeFailures_;
        lastResizeFailureMs_ = GetTickCount64();
        // DXGI_ERROR_INVALID_CALL with our own references released means another module
        // still holds a back buffer: an overlay or capture hook in this process.
        if (resizeFailures_ <= 3 || (resizeFailures_ & (resizeFailures_ - 1)) == 0)
            log::error("present", std::format("ResizeBuffers FAILED hr=0x{:X} (queue idle, buffers released; failures in a row={}; "
                "an injected overlay/capture hook can hold the buffers)", static_cast<unsigned>(hr), resizeFailures_));
        refetchBackBuffers();
        return;
    }
    if (resizeFailures_ > 0) log::info("present", std::format("ResizeBuffers recovered after {} failures", resizeFailures_));
    resizeFailures_ = 0;
    if (!refetchBackBuffers()) return;
    trace.mark("refetchBuffers");
    width_ = width;
    height_ = height;
    scBufferWidth_ = width_;
    scBufferHeight_ = height_;
    bufferExtentW_ = width_;
    bufferExtentH_ = height_;
    pendingResize_ = true;
    frameStatisticsBaseValid_=false;
    log::info("present", std::format("present-sink: resized to {}x{}", width_, height_));
}

void PresentSink::shutdown()
{
    if(latencyHandle_){CloseHandle(latencyHandle_);latencyHandle_=nullptr;}
    pacing_=false;capacityAcquired_=false;
    // Dependency order (s10, proven by the 0x87D matrix): backbuffers ->
    // swapchain -> window -> factory. The window must OUTLIVE the swapchain:
    // with the D3D12 debug layer active, releasing a flip swapchain whose
    // target HWND is already destroyed raises exception 0x87D in
    // KERNELBASE (WER event 1000, observed t10-L1 2026-09-04). The swapchain
    // must also be released BEFORE the D3D12 queue it presents on.
    if (shutdownCalled_) return; // second call (destructor) has nothing left
    shutdownCalled_ = true;
    // XeSS owns a proxy swapchain and may have copied tagged inputs on the
    // presenting queue. Its shutdown contract requires that work to finish.
    waitForQueueIdle();
    log::info("present", "sink-shutdown: sub-step backbuffers-release");
    for (auto& b : backBuffers_) b.Reset();
    log::info("present", "sink-shutdown: sub-step swapchain-release");
    if (swapChain_ != nullptr) {
        swapChain_->AddRef();
        const ULONG rc = swapChain_->Release();
        log::info("present", std::format("sink-shutdown: swapchain pre-release refcount={} (2 = only our ComPtr + probe)", rc));
    }
    swapChain_.Reset();swapChain4_.Reset();
    xess_.reset();
    log::info("present", "sink-shutdown: sub-step window-destroy-last");
    if (hwnd_ != nullptr && !desc_.targetWindow) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
        }
    }
    log::info("present", "sink-shutdown: sub-step factory-release");
    factory_.Reset();

    hwnd_ = nullptr; // borrowed HWNDs are cleared too, never destroyed here
    queue_ = nullptr; // owner may release its queue immediately after shutdown
    device_ = nullptr;
    log::info("present", "sink-shutdown: complete");
}

} // namespace veyra::gfx
