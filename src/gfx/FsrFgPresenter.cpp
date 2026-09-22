#include "veyra/gfx/FsrFgPresenter.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <atomic>
#include <filesystem>
#include <string>

#ifdef VEYRA_HAS_FSR
#include <ffx_api_loader.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_FSR
namespace {

void fsrMessage(uint32_t type, const wchar_t* message) {
    if (message == nullptr) return;
    const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) {
        WideCharToMultiByte(CP_UTF8, 0, message, -1, text.data(), length, nullptr, nullptr);
    }
    if (type == FFX_API_MESSAGE_TYPE_ERROR) log::error("fsr-sdk", text);
    else log::warn("fsr-sdk", text);
}

FfxApiSurfaceFormat surfaceFormatFromDxgi(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R16G16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32_FLOAT: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

FfxApiResource makeResource(ID3D12Resource* resource, uint32_t state) {
    FfxApiResource api{};
    if (resource == nullptr) return api;
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    api.resource = resource;
    api.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    api.description.format = surfaceFormatFromDxgi(desc.Format);
    api.description.width = uint32_t(desc.Width);
    api.description.height = desc.Height;
    api.description.depth = desc.DepthOrArraySize;
    api.description.mipCount = desc.MipLevels;
    api.description.flags = 0;
    api.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0) {
        api.description.usage = FfxApiResourceUsage(api.description.usage | FFX_API_RESOURCE_USAGE_UAV);
    }
    api.state = state;
    return api;
}

D3D12_RESOURCE_STATES dx12State(uint32_t ffxState) {
    switch (ffxState) {
    case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_COMPUTE_READ: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_COPY_SRC: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_API_RESOURCE_STATE_COPY_DEST: return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_API_RESOURCE_STATE_PRESENT: return D3D12_RESOURCE_STATE_PRESENT;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

} // namespace
#endif

struct FsrFgPresenter::Impl {
    // Written from the provider's presentation thread.
    std::atomic<uint64_t> realFrames{0};
    std::atomic<uint64_t> generatedFrames{0};
    std::atomic<uint64_t> callbackFailures{0};
    // Render thread.
    uint64_t frameId = 0;
    uint32_t requestedGenerated = 0;
    uint32_t deliveredGenerated = 0;
    bool failed = false;
    bool softDisabled = false;
    bool available = false;
    std::string providerVersion;

    // A provider failure must not take playback down with it: generation is
    // switched off, the frame keeps presenting through the proxy swapchain and
    // the engine is told once so it can surface a warning.
    void disableGeneration(const char* reason) {
        if (!softDisabled) {
            softDisabled = true;
            failed = true;
            log::warn("fsr-fg", std::format("frame generation disabled after {}; plain presentation continues", reason));
        }
    }
#ifdef VEYRA_HAS_FSR
    HMODULE loader = nullptr;
    ffxFunctions functions{};
    ffxContext swapchainContext = nullptr;
    ffxContext fgContext = nullptr;
    IDXGISwapChain4* swapchain = nullptr;  // borrowed; owned by the sink and the context
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t backBufferFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
#endif
};

#ifdef VEYRA_HAS_FSR
namespace {

// Called by the provider for every frame that reaches the swapchain, real or
// interpolated, on its presentation thread. Veyra has no UI surface to
// composite, so this performs the same copy the provider's default callback
// would and only keeps counters. No logging or allocation happens here.
ffxReturnCode_t presentCallback(ffxCallbackDescFrameGenerationPresent* params, void* userCtx) {
    auto* impl = static_cast<FsrFgPresenter::Impl*>(userCtx);
    if (params == nullptr || impl == nullptr) return FFX_API_RETURN_ERROR_PARAMETER;
    if (params->isGeneratedFrame) {
        impl->generatedFrames.fetch_add(1, std::memory_order_relaxed);
    } else {
        impl->realFrames.fetch_add(1, std::memory_order_relaxed);
    }
    if (params->currentUI.resource != nullptr || params->commandList == nullptr ||
        params->outputSwapChainBuffer.resource == nullptr || params->currentBackBuffer.resource == nullptr) {
        impl->callbackFailures.fetch_add(1, std::memory_order_relaxed);
        return FFX_API_RETURN_OK;
    }
    auto* list = static_cast<ID3D12GraphicsCommandList*>(params->commandList);
    auto* destination = static_cast<ID3D12Resource*>(params->outputSwapChainBuffer.resource);
    auto* source = static_cast<ID3D12Resource*>(params->currentBackBuffer.resource);
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = source;
    barriers[0].Transition.StateBefore = dx12State(params->currentBackBuffer.state);
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = destination;
    barriers[1].Transition.StateBefore = dx12State(params->outputSwapChainBuffer.state);
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(2, barriers);
    list->CopyResource(destination, source);
    for (auto& barrier : barriers) {
        const D3D12_RESOURCE_STATES before = barrier.Transition.StateBefore;
        barrier.Transition.StateBefore = barrier.Transition.StateAfter;
        barrier.Transition.StateAfter = before;
    }
    list->ResourceBarrier(2, barriers);
    return FFX_API_RETURN_OK;
}

const char* returnName(ffxReturnCode_t code) {
    switch (code) {
    case FFX_API_RETURN_OK: return "OK";
    case FFX_API_RETURN_ERROR: return "ERROR";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "ERROR_UNKNOWN_DESCTYPE";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "ERROR_RUNTIME_ERROR";
    case FFX_API_RETURN_NO_PROVIDER: return "NO_PROVIDER";
    case FFX_API_RETURN_ERROR_MEMORY: return "ERROR_MEMORY";
    case FFX_API_RETURN_ERROR_PARAMETER: return "ERROR_PARAMETER";
    case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE: return "PROVIDER_NO_SUPPORT_NEW_DESCTYPE";
    default: return "OTHER";
    }
}

} // namespace
#endif

FsrFgPresenter::FsrFgPresenter() : p_(std::make_unique<Impl>()) {}

void FsrFgPresenter::shutdown() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    // The provider requires generation to be switched off before the context
    // goes away: that call flushes interpolation and UI composition GPU work,
    // after which the context can release the proxy swapchain (and with it the
    // real DXGI swapchain, which is what lets another swapchain be created for
    // the same HWND later).
    if (p.swapchainContext != nullptr) {
        // Drain the provider's own presentation queue first: its pacing state
        // still holds the last submitted frame id, and destroying the
        // contexts with a present in flight crashed inside the provider
        // (WER 0xC0000005 in amd_fidelityfx_framegeneration_dx12.dll).
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
        (void)p.functions.Dispatch(&p.swapchainContext, &wait.header);
        log::info("fsr-fg", "shutdown: presentation queue drained");
        veyra::Logger::instance().flush();
    }
    if (p.fgContext != nullptr && p.swapchain != nullptr) {
        ffxConfigureDescFrameGeneration disable{};
        disable.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        disable.swapChain = p.swapchain;
        disable.frameGenerationEnabled = false;
        disable.presentCallback = presentCallback;
        disable.presentCallbackUserContext = &p;
        disable.generationRect = {0, 0, int32_t(p.width), int32_t(p.height)};
        disable.frameID = p.frameId;
        const auto result = p.functions.Configure(&p.fgContext, &disable.header);
        log::info("fsr-fg", std::format("shutdown disable result={} frameId={}", returnName(result), p.frameId));
        veyra::Logger::instance().flush();
    }
    // The context owns one reference to the proxy swapchain and releases it
    // here; the sink drops its own reference before this object is destroyed.
    if (p.fgContext != nullptr) {
        const auto result = p.functions.DestroyContext(&p.fgContext, nullptr);
        log::info("fsr-fg", std::format("framegen context destroyed result={}", returnName(result)));
        p.fgContext = nullptr;
        veyra::Logger::instance().flush();
    }
    if (p.swapchainContext != nullptr) {
        const auto result = p.functions.DestroyContext(&p.swapchainContext, nullptr);
        log::info("fsr-fg", std::format("swapchain context destroyed result={}", returnName(result)));
        p.swapchainContext = nullptr;
        veyra::Logger::instance().flush();
    }
    p.swapchain = nullptr;
    p.available = false;
#endif
}

FsrFgPresenter::~FsrFgPresenter() {
#ifdef VEYRA_HAS_FSR
    shutdown();
    // The loader is intentionally never unloaded. Unloading it right after
    // DestroyContext crashed with an access violation inside
    // amd_fidelityfx_framegeneration_dx12.dll (Application Error 0xC0000005 at
    // 0x10b9a9): the provider keeps internal presentation threads and state
    // that do not survive module unload. Keeping the module resident for the
    // process lifetime is the safe contract, and a later session can create a
    // fresh context from it.
#endif
}

bool FsrFgPresenter::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory2* factory,
                                HWND window, const DXGI_SWAP_CHAIN_DESC1& desc, uint32_t renderWidth,
                                uint32_t renderHeight, IDXGISwapChain4** swapchain, uint32_t fgMultiplier) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    (void)fgMultiplier;
    const auto root = runtime::localDataDirectory() / "amd" / "fidelityfx";
    const auto loaderPath = std::filesystem::absolute(root / "amd_fidelityfx_loader_dx12.dll");
    const auto providerPath = std::filesystem::absolute(root / "amd_fidelityfx_framegeneration_dx12.dll");

    p.requestedGenerated = 1;  // FSR 3.1.x providers deliver one generated frame per present

    // The loader resolves provider DLLs by base name through the normal search
    // order, so pre-loading the provider by absolute path and pointing the
    // legacy DLL directory at the same folder keeps the discovery deterministic
    // without leaking the directory into the rest of the process.
    wchar_t previousDirectory[MAX_PATH]{};
    const DWORD previousLength = GetDllDirectoryW(MAX_PATH, previousDirectory);
    SetDllDirectoryW(root.c_str());
    struct DirectoryGuard {
        const wchar_t* previous = nullptr;
        DWORD previousLength = 0;
        wchar_t buffer[MAX_PATH]{};
        ~DirectoryGuard() { SetDllDirectoryW(previousLength > 0 ? buffer : nullptr); }
    } directoryGuard;
    directoryGuard.previous = previousDirectory;
    directoryGuard.previousLength = previousLength;
    if (previousLength > 0 && previousLength < MAX_PATH) {
        wcsncpy_s(directoryGuard.buffer, previousDirectory, previousLength);
    }

    p.loader = LoadLibraryExW(loaderPath.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (p.loader == nullptr) {
        log::error("fsr-fg", std::format("loader load failed path={} win32={}", loaderPath.string(), GetLastError()));
        return false;
    }
    if (std::filesystem::exists(providerPath)) {
        // Signed AMD provider; the loader is expected to pick this module up.
        if (LoadLibraryExW(providerPath.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) == nullptr) {
            log::warn("fsr-fg", std::format("provider preload failed path={} win32={}", providerPath.string(), GetLastError()));
        }
    }
    ffxLoadFunctions(&p.functions, p.loader);
    if (p.functions.CreateContext == nullptr || p.functions.DestroyContext == nullptr ||
        p.functions.Configure == nullptr || p.functions.Query == nullptr || p.functions.Dispatch == nullptr) {
        log::error("fsr-fg", "loader is missing the FidelityFX API entry points");
        return false;
    }

    // Report which provider version serves this adapter before creating it, so
    // the log carries the same evidence as the standalone probe.
    {
        uint64_t count = 0;
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versions.device = device;
        versions.outputCount = &count;
        uint64_t ids[8]{};
        const char* names[8]{};
        uint64_t capacity = 8;
        versions.versionIds = ids;
        versions.versionNames = names;
        versions.outputCount = &capacity;
        const auto queryResult = p.functions.Query(nullptr, &versions.header);
        if (queryResult == FFX_API_RETURN_OK && capacity > 0) {
            p.providerVersion = names[0] != nullptr ? names[0] : "unknown";
        }
        log::info("fsr-fg", std::format("providers query={} count={} framegen={} swapchainVersion={}", returnName(queryResult),
                                        capacity, p.providerVersion.empty() ? "none" : p.providerVersion,
                                        FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION));
    }

    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 swapchainVersion{};
    swapchainVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
    swapchainVersion.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
    IDXGISwapChain4* proxy = nullptr;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 swapchainDesc{};
    swapchainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    swapchainDesc.header.pNext = &swapchainVersion.header;
    swapchainDesc.swapchain = &proxy;
    swapchainDesc.hwnd = window;
    swapchainDesc.desc = &const_cast<DXGI_SWAP_CHAIN_DESC1&>(desc);
    swapchainDesc.fullscreenDesc = nullptr;
    swapchainDesc.dxgiFactory = factory;
    swapchainDesc.gameQueue = queue;
    const auto swapchainResult = p.functions.CreateContext(&p.swapchainContext, &swapchainDesc.header, nullptr);
    if (swapchainResult != FFX_API_RETURN_OK || p.swapchainContext == nullptr || proxy == nullptr) {
        log::error("fsr-fg", std::format("swapchain context failed result={} (0x{:X})", returnName(swapchainResult),
                                         unsigned(swapchainResult)));
        p.failed = true;
        return false;
    }
    p.swapchain = proxy;
    p.width = desc.Width;
    p.height = desc.Height;
    p.backBufferFormat = surfaceFormatFromDxgi(desc.Format);

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device;
    ffxCreateContextDescFrameGenerationVersion fgVersion{};
    fgVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    fgVersion.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGeneration fgDesc{};
    fgDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    fgDesc.header.pNext = &backendDesc.header;
    backendDesc.header.pNext = &fgVersion.header;
    // Debug checking makes the provider report the exact reason a dispatch is
    // rejected instead of a bare runtime error code.
    fgDesc.flags = 0;
    fgDesc.displaySize = {p.width, p.height};
    // The provider validates the per-frame renderSize against maxRenderSize; the
    // working extent can be larger than the window buffer (contain-scaled
    // preview), so the ceiling covers both.
    fgDesc.maxRenderSize = {std::max(p.width, renderWidth), std::max(p.height, renderHeight)};
    fgDesc.backBufferFormat = p.backBufferFormat;
    auto fgResult = p.functions.CreateContext(&p.fgContext, &fgDesc.header, nullptr);
    if (fgResult != FFX_API_RETURN_OK || p.fgContext == nullptr) {
        log::warn("fsr-fg", std::format("frame generation context with version desc failed result={}; retrying without it",
                                        returnName(fgResult)));
        backendDesc.header.pNext = nullptr;
        fgResult = p.functions.CreateContext(&p.fgContext, &fgDesc.header, nullptr);
    }
    if (fgResult != FFX_API_RETURN_OK || p.fgContext == nullptr) {
        log::error("fsr-fg", std::format("frame generation context failed result={} (0x{:X})", returnName(fgResult),
                                         unsigned(fgResult)));
        p.failed = true;
        return false;
    }
    p.available = true;
    p.deliveredGenerated = p.requestedGenerated;
    {
        ffxConfigureDescGlobalDebug1 global{};
        global.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        global.fpMessage = fsrMessage;
        global.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        p.functions.Configure(nullptr, &global.header);
        p.functions.Configure(&p.fgContext, &global.header);
        if (p.swapchainContext != nullptr) p.functions.Configure(&p.swapchainContext, &global.header);
    }
    *swapchain = proxy;
    log::info("fsr-fg", std::format("provider={} swapchain={}x{} maxRender={}x{} format={} multiplierRequested={}X (delivered multiplier is reported per frame)",
                                    p.providerVersion.empty() ? "unknown" : p.providerVersion, p.width, p.height,
                                    fgDesc.maxRenderSize.width, fgDesc.maxRenderSize.height,
                                    int(desc.Format), p.requestedGenerated + 1));
    return true;
#else
    (void)device; (void)queue; (void)factory; (void)window; (void)desc; (void)swapchain; (void)fgMultiplier;
    (void)renderWidth; (void)renderHeight;
    log::error("fsr-fg", "AMD FidelityFX SDK headers were unavailable at build time");
    return false;
#endif
}

bool FsrFgPresenter::tag(ID3D12GraphicsCommandList* list, ID3D12Resource* backBuffer, ID3D12Resource* motion,
                         ID3D12Resource* depth, RECT region, bool enabled, bool reset, float elapsedMs) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available) return false;
    if (region.right <= region.left || region.bottom <= region.top) return false;

    ffxConfigureDescFrameGeneration configure{};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.swapChain = p.swapchain;
    configure.presentCallback = presentCallback;
    configure.presentCallbackUserContext = &p;
    configure.frameGenerationEnabled = enabled;
    configure.allowAsyncWorkloads = false;
    configure.flags = 0;
    configure.generationRect = {region.left, region.top, region.right - region.left, region.bottom - region.top};
    configure.frameID = p.frameId;

    if (!enabled) {
        const auto result = p.functions.Configure(&p.fgContext, &configure.header);
        if (result != FFX_API_RETURN_OK) {
            log::warn("fsr-fg", std::format("configure(disabled) result={} frameId={}", returnName(result), p.frameId));
            p.disableGeneration("configure(disabled) rejection");
        }
        return true;
    }
    if (list == nullptr || backBuffer == nullptr || motion == nullptr || depth == nullptr) return false;
    if (p.softDisabled) return true;

    // The provider runs its own presentation/pacing threads; flushing their
    // outstanding presentations before recording the next frame is what the
    // swapchain contract expects from an application that is not otherwise
    // synchronized to the display (the player submits as fast as it decodes).
    {
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
        const auto waitResult = p.functions.Dispatch(&p.swapchainContext, &wait.header);
        if (waitResult != FFX_API_RETURN_OK) {
            log::warn("fsr-fg", std::format("wait-for-presents result={} frameId={}", returnName(waitResult), p.frameId));
        }
    }

    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = p.frameId;
    prepare.flags = 0;
    prepare.commandList = list;
    prepare.renderSize = {uint32_t(motion->GetDesc().Width), motion->GetDesc().Height};
    prepare.jitterOffset = {0.0f, 0.0f};
    // Guidance motion is pixel space in working-extent units, so the scale that
    // converts it to UV inside the provider is 1:1. FSR reprojects with
    // "previous = current + motion", which already matches Veyra's
    // current->previous guidance (DLSSG needs the opposite sign).
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeDelta = elapsedMs > 0.0f ? elapsedMs : 16.6f;
    prepare.reset = reset;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.cameraFovAngleVertical = 1.0f;
    prepare.viewSpaceToMetersFactor = 0.0f;
    // The graph leaves both guidance textures in the common state after each
    // frame, so the declared state must be the truthful one: a mismatched
    // barrier would corrupt the queue instead of failing loudly.
    prepare.depth = makeResource(depth, FFX_API_RESOURCE_STATE_COMMON);
    prepare.motionVectors = makeResource(motion, FFX_API_RESOURCE_STATE_COMMON);
    const auto prepareResult = p.functions.Dispatch(&p.fgContext, &prepare.header);
    if (prepareResult != FFX_API_RETURN_OK) {
        log::warn("fsr-fg", std::format("prepare dispatch result={} frameId={}", returnName(prepareResult), p.frameId));
        p.disableGeneration("prepare dispatch rejection");
        return true;
    }

    ffxDispatchDescFrameGeneration dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    dispatch.commandList = nullptr;
    dispatch.presentColor = makeResource(backBuffer, FFX_API_RESOURCE_STATE_COMMON);
    dispatch.numGeneratedFrames = p.requestedGenerated;
    dispatch.reset = reset;
    dispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    dispatch.minMaxLuminance[0] = 0.0f;
    dispatch.minMaxLuminance[1] = 1000.0f;
    dispatch.generationRect = configure.generationRect;
    dispatch.frameID = p.frameId;

    const auto configureResult = p.functions.Configure(&p.fgContext, &configure.header);
    if (configureResult != FFX_API_RETURN_OK) {
        log::warn("fsr-fg", std::format("configure result={} frameId={}", returnName(configureResult), p.frameId));
        p.disableGeneration("configure rejection");
        return true;
    }
    // The swapchain only hands out an interpolation command list while its own
    // generation flag is on, so the queries must follow the configure.
    ffxQueryDescFrameGenerationSwapChainInterpolationCommandListDX12 queryList{};
    queryList.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12;
    queryList.pOutCommandList = &dispatch.commandList;
    auto queryResult = p.functions.Query(&p.swapchainContext, &queryList.header);
    if (queryResult != FFX_API_RETURN_OK || dispatch.commandList == nullptr) {
        log::warn("fsr-fg", std::format("interpolation command list query result={} list={}", returnName(queryResult),
                                        dispatch.commandList));
        p.disableGeneration("interpolation command list query rejection");
        return true;
    }
    ffxQueryDescFrameGenerationSwapChainInterpolationTextureDX12 queryTexture{};
    queryTexture.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12;
    queryTexture.pOutTexture = &dispatch.outputs[0];
    queryResult = p.functions.Query(&p.swapchainContext, &queryTexture.header);
    if (queryResult != FFX_API_RETURN_OK) {
        log::warn("fsr-fg", std::format("interpolation texture query result={}", returnName(queryResult)));
        p.disableGeneration("interpolation texture query rejection");
        return true;
    }

    if (p.frameId <= 2 || log::verboseFrameLogs()) {
        const auto backBufferDesc = backBuffer->GetDesc();
        const auto motionDesc = motion->GetDesc();
        const auto depthDesc = depth->GetDesc();
        const auto outputDesc = dispatch.outputs[0].resource != nullptr
            ? static_cast<ID3D12Resource*>(dispatch.outputs[0].resource)->GetDesc()
            : D3D12_RESOURCE_DESC{};
        log::info("fsr-fg", std::format(
            "trace frameId={} reset={} render={}x{} rect={}+{} {}x{} numGenerated={} present={}x{} fmt={} motion={}x{} fmt={} depth={}x{} fmt={} output={}x{} fmt={}",
            p.frameId, reset, prepare.renderSize.width, prepare.renderSize.height, configure.generationRect.left,
            configure.generationRect.top, configure.generationRect.width, configure.generationRect.height,
            dispatch.numGeneratedFrames, uint32_t(backBufferDesc.Width), backBufferDesc.Height,
            int(backBufferDesc.Format), uint32_t(motionDesc.Width), motionDesc.Height, int(motionDesc.Format),
            uint32_t(depthDesc.Width), depthDesc.Height, int(depthDesc.Format), uint32_t(outputDesc.Width),
            outputDesc.Height, int(outputDesc.Format)));
    }
    const auto dispatchResult = p.functions.Dispatch(&p.fgContext, &dispatch.header);
    if (dispatchResult != FFX_API_RETURN_OK) {
        log::warn("fsr-fg", std::format("frame generation dispatch result={} frameId={}", returnName(dispatchResult), p.frameId));
        p.disableGeneration("frame generation dispatch rejection");
        return true;
    }
    if (log::verboseFrameLogs()) {
        log::info("fsr-fg", std::format("frameId={} region={}x{}+{}+{} generatedPerFrame={}", p.frameId,
                                        configure.generationRect.width, configure.generationRect.height,
                                        configure.generationRect.left, configure.generationRect.top,
                                        p.requestedGenerated));
    }
    return true;
#else
    (void)list; (void)backBuffer; (void)motion; (void)depth; (void)region; (void)enabled; (void)reset; (void)elapsedMs;
    return false;
#endif
}

void FsrFgPresenter::afterPresent() {
    // Frame ids must advance by exactly one per presented frame; a gap makes the
    // provider reset its interpolation history.
    auto& p = *p_;
    ++p.frameId;
    // The provider reports on its own thread; a periodic sample is the only
    // honest way to show whether interpolation reached the display path.
    if (log::verboseFrameLogs() || p.frameId % 120 == 0) {
        log::info("fsr-fg", std::format("frameId={} real={} generated={} presented={} callbackFailures={}",
                                        p.frameId, p.realFrames.load(), p.generatedFrames.load(),
                                        p.realFrames.load() + p.generatedFrames.load(), p.callbackFailures.load()));
    }
}

uint64_t FsrFgPresenter::generatedCount() const { return p_->generatedFrames.load(); }
uint64_t FsrFgPresenter::presentedCount() const { return p_->realFrames.load() + p_->generatedFrames.load(); }
uint32_t FsrFgPresenter::maxGeneratedFrames() const { return p_->available ? p_->deliveredGenerated : 0; }
bool FsrFgPresenter::available() const { return p_->available; }
bool FsrFgPresenter::failed() const { return p_->failed; }
const char* FsrFgPresenter::providerVersion() const { return p_->providerVersion.c_str(); }

IDXGISwapChain4* FsrFgPresenter::swapchainHandle() const {
#ifdef VEYRA_HAS_FSR
    return p_->swapchain;
#else
    return nullptr;
#endif
}

void FsrFgPresenter::disableGeneration() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available || p.softDisabled || p.fgContext == nullptr || p.swapchain == nullptr) return;
    ffxConfigureDescFrameGeneration configure{};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.swapChain = p.swapchain;
    configure.frameGenerationEnabled = false;
    configure.presentCallback = presentCallback;
    configure.presentCallbackUserContext = &p;
    configure.generationRect = {0, 0, int32_t(p.width), int32_t(p.height)};
    configure.frameID = p.frameId;
    const auto result = p.functions.Configure(&p.fgContext, &configure.header);
    if (result != FFX_API_RETURN_OK) {
        log::warn("fsr-fg", std::format("disableGeneration result={} frameId={}", returnName(result), p.frameId));
        return;
    }
    log::info("fsr-fg", "generation disabled; the AMD proxy keeps presenting real frames");
#endif
}

} // namespace veyra::gfx
