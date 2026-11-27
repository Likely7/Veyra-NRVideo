// AMD FSR frame-generation probe (diagnostic only, never shipped in the player).
//
// Two stages of evidence:
//   1. provider inventory: loads the vendored AMD FidelityFX loader DLL and
//      enumerates the frame-generation / swapchain provider versions that this
//      machine actually advertises;
//   2. full frame-generation run: creates the swapchain proxy context and the
//      frame-generation context, renders a moving back buffer, records
//      prepare + configure + dispatch every frame and presents through the
//      AMD proxy swapchain. A present callback counts real vs generated
//      frames, which is the only ground truth that interpolation actually
//      reached the display path.
//
// Usage: veyra_fsr_probe [path-to-amd_fidelityfx_loader_dx12.dll] [generated-frames-per-real-frame] [upscale] [pipelined] [motion]
//
// The optional "upscale" mode reproduces the player's geometry: the render
// resolution (guidance) is larger than the swapchain and the interpolation
// rectangle is the letterboxed fit inside it.
//
// The optional "motion" mode moves a white square by a known number of pixels
// per frame, then reads the interpolation target back and checks that the
// generated frame really sits at the temporal midpoint between the two real
// frames (real interpolation) instead of repeating or extrapolating.
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <ffx_api_loader.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kProbeWidth = 1280;
constexpr UINT kProbeHeight = 720;
constexpr UINT kProbeFrames = 90;
// Motion mode: white square geometry, in back buffer pixels.
constexpr UINT kMotionStartX = 100;
constexpr UINT kMotionStep = 40;
constexpr UINT kMotionTop = 320;
constexpr UINT kMotionSize = 80;

const char* resultName(ffxReturnCode_t code) {
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

LRESULT CALLBACK probeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Resource description conversion, mirroring the FidelityFX DX12 backend.
FfxApiSurfaceFormat surfaceFormatFromDxgi(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32_FLOAT: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R32_UINT: return FFX_API_SURFACE_FORMAT_R32_UINT;
    default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

FfxApiResourceDescription describeResource(ID3D12Resource* resource) {
    FfxApiResourceDescription description{};
    if (resource == nullptr) return description;
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    description.format = surfaceFormatFromDxgi(desc.Format);
    description.width = uint32_t(desc.Width);
    description.height = desc.Height;
    description.depth = desc.DepthOrArraySize;
    description.mipCount = desc.MipLevels;
    description.flags = 0;
    description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0) {
        description.usage = FfxApiResourceUsage(description.usage | FFX_API_RESOURCE_USAGE_UAV);
    }
    return description;
}

FfxApiResource makeResource(ID3D12Resource* resource, uint32_t state) {
    FfxApiResource api{};
    api.resource = resource;
    api.description = describeResource(resource);
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

struct PresentCounters {
    std::atomic<uint64_t> real{0};
    std::atomic<uint64_t> generated{0};
    std::atomic<uint64_t> callbackFailures{0};
    // Cadence sampling from the provider's presentation thread: intervals are
    // stored in microseconds so the counters stay lock-free.
    std::atomic<int64_t> lastPresentQpc{0};
    std::atomic<int64_t> intervalSumUs{0};
    std::atomic<uint64_t> intervalCount{0};
    std::atomic<uint64_t> fastIntervals{0};
    std::atomic<int64_t> maxIntervalUs{0};
};

// The swapchain context calls this for every frame it presents (real and
// interpolated). With no UI resource registered the SDK's own default callback
// would only copy the finished frame into the real swapchain buffer, so the
// same copy is performed here and the frame is counted.
ffxReturnCode_t presentCallback(ffxCallbackDescFrameGenerationPresent* params, void* userCtx) {
    auto* counters = static_cast<PresentCounters*>(userCtx);
    if (params == nullptr || counters == nullptr) return FFX_API_RETURN_ERROR_PARAMETER;
    if (params->isGeneratedFrame) {
        counters->generated.fetch_add(1, std::memory_order_relaxed);
    } else {
        counters->real.fetch_add(1, std::memory_order_relaxed);
    }
    {
        LARGE_INTEGER now{};
        LARGE_INTEGER frequency{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        const int64_t previous = counters->lastPresentQpc.exchange(now.QuadPart);
        if (previous != 0 && frequency.QuadPart > 0) {
            const int64_t micros = (now.QuadPart - previous) * 1000000 / frequency.QuadPart;
            counters->intervalSumUs.fetch_add(micros, std::memory_order_relaxed);
            counters->intervalCount.fetch_add(1, std::memory_order_relaxed);
            if (micros < 5000) counters->fastIntervals.fetch_add(1, std::memory_order_relaxed);
            int64_t observed = counters->maxIntervalUs.load(std::memory_order_relaxed);
            while (micros > observed &&
                   !counters->maxIntervalUs.compare_exchange_weak(observed, micros, std::memory_order_relaxed)) {
            }
        }
    }
    if (params->currentUI.resource != nullptr || params->commandList == nullptr ||
        params->outputSwapChainBuffer.resource == nullptr || params->currentBackBuffer.resource == nullptr) {
        counters->callbackFailures.fetch_add(1, std::memory_order_relaxed);
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
        const auto before = barrier.Transition.StateBefore;
        barrier.Transition.StateBefore = barrier.Transition.StateAfter;
        barrier.Transition.StateAfter = before;
    }
    list->ResourceBarrier(2, barriers);
    return FFX_API_RETURN_OK;
}

void reportVersions(const ffxFunctions& functions, uint64_t createDescType, const char* label, void* device) {
    uint64_t count = 0;
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = createDescType;
    query.device = device;
    query.outputCount = &count;
    query.versionIds = nullptr;
    query.versionNames = nullptr;
    const auto countResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-probe] %s versionsQuery=%s count=%llu\n", label, resultName(countResult),
                static_cast<unsigned long long>(count));
    if (countResult != FFX_API_RETURN_OK || count == 0 || count > 32) return;
    std::vector<uint64_t> ids(count, 0);
    std::vector<const char*> names(count, nullptr);
    uint64_t capacity = count;
    query.outputCount = &capacity;
    query.versionIds = ids.data();
    query.versionNames = names.data();
    const auto listResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-probe] %s versionsList=%s returned=%llu\n", label, resultName(listResult),
                static_cast<unsigned long long>(capacity));
    for (uint64_t i = 0; i < capacity && i < ids.size(); ++i) {
        std::printf("[fsr-probe]   %s[%llu] id=%llu name=%s\n", label, static_cast<unsigned long long>(i),
                    static_cast<unsigned long long>(ids[i]), names[i] ? names[i] : "(null)");
    }
}

ComPtr<ID3D12Resource> makeTexture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
                                   D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
        return {};
    }
    return resource;
}

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // evidence survives a probe crash
    const auto loaderPath = std::filesystem::absolute(argc > 1 ? argv[1] : VEYRA_FFX_LOADER);
    UINT generatedPerFrame = 1;
    if (argc > 2) {
        const int requested = _wtoi(argv[2]);
        if (requested >= 1 && requested <= 4) generatedPerFrame = UINT(requested);
    }
    bool upscaleMode = argc > 3 && _wcsicmp(argv[3], L"upscale") == 0;
    const bool pipelined = argc > 4 && _wcsicmp(argv[4], L"pipelined") == 0;
    const bool motionMode = argc > 5 && _wcsicmp(argv[5], L"motion") == 0;
    const UINT frames = motionMode ? 24u : kProbeFrames;
    const UINT lastRealLeft = kMotionStartX + kMotionStep * (frames - 1);
    const UINT renderWidth = upscaleMode ? 1920u : kProbeWidth;
    const UINT renderHeight = upscaleMode ? 1080u : kProbeHeight;
    const FfxApiRect2D generationRect = upscaleMode
        ? FfxApiRect2D{7, 0, 1266, 712}
        : FfxApiRect2D{0, 0, int32_t(kProbeWidth), int32_t(kProbeHeight)};
    std::printf("[fsr-probe] loader=%s\n", loaderPath.string().c_str());
    std::printf("[fsr-probe] requested generated frames per real frame=%u (multiplier %uX)\n", generatedPerFrame,
                generatedPerFrame + 1);
    std::printf("[fsr-probe] display=%ux%u render=%ux%u generationRect=%d+%d %dx%d\n", kProbeWidth, kProbeHeight,
                renderWidth, renderHeight, generationRect.left, generationRect.top, generationRect.width,
                generationRect.height);

    // The loader only discovers providers when the directory holding the AMD
    // binaries is on the search path; changing the process directory is the
    // cheapest way to prove that for the probe.
    SetCurrentDirectoryW(loaderPath.parent_path().wstring().c_str());
    HMODULE loader = LoadLibraryW(loaderPath.filename().wstring().c_str());
    if (loader == nullptr) {
        std::printf("[fsr-probe] loader load failed win32=%lu\n", GetLastError());
        return 2;
    }

    ffxFunctions functions{};
    ffxLoadFunctions(&functions, loader);
    if (functions.CreateContext == nullptr || functions.Query == nullptr || functions.DestroyContext == nullptr ||
        functions.Configure == nullptr || functions.Dispatch == nullptr) {
        std::printf("[fsr-probe] loader is missing the FSR API entry points\n");
        return 3;
    }
    std::printf("[fsr-probe] FSR API entry points resolved\n");

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        std::printf("[fsr-probe] CreateDXGIFactory2 failed\n");
        return 4;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
        std::printf("[fsr-probe] no high-performance adapter\n");
        return 5;
    }
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    std::wprintf(L"[fsr-probe] adapter=%s vendor=0x%04X\n", adapterDesc.Description, adapterDesc.VendorId);

    // Local-memory accounting: this probe exists to show what the provider
    // keeps resident after its contexts are destroyed.
    ComPtr<IDXGIAdapter3> adapter3;
    adapter.As(&adapter3);
    const auto localUsageMiB = [&adapter3]() -> double {
        if (adapter3 == nullptr) return -1.0;
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return -1.0;
        return double(info.CurrentUsage) / (1024.0 * 1024.0);
    };
    std::printf("[fsr-probe] localUsageMiB before-create=%.1f\n", localUsageMiB());

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::printf("[fsr-probe] D3D12CreateDevice failed\n");
        return 6;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
        std::printf("[fsr-probe] CreateCommandQueue failed\n");
        return 7;
    }

    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12, "framegen-swapchain-forhwnd", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, "framegen", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12, "framegen-swapchain-version", device.Get());
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION, "framegen-version", device.Get());

    WNDCLASSW wc{};
    wc.lpfnWndProc = probeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VeyraFsrProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"FSR probe", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, LONG(kProbeWidth), LONG(kProbeHeight),
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        std::printf("[fsr-probe] CreateWindow failed\n");
        return 8;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = kProbeWidth;
    scd.Height = kProbeHeight;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Scaling = DXGI_SCALING_STRETCH;

    // Stage 1: swapchain proxy context.
    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 swapchainVersion{};
    swapchainVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
    swapchainVersion.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;

    // The context hands out a borrowed pointer: it owns one reference of its
    // own and releases it in ffxDestroyContext, so the application must take
    // its own reference instead of adopting the pointer.
    IDXGISwapChain4* proxySwapchain = nullptr;
    ComPtr<IDXGISwapChain4> swapchain;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 swapchainDesc{};
    swapchainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    swapchainDesc.header.pNext = &swapchainVersion.header;
    swapchainDesc.swapchain = &proxySwapchain;
    swapchainDesc.hwnd = hwnd;
    swapchainDesc.desc = &scd;
    swapchainDesc.fullscreenDesc = nullptr;
    swapchainDesc.dxgiFactory = factory.Get();
    swapchainDesc.gameQueue = queue.Get();

    ffxContext swapchainContext = nullptr;
    const auto swapchainResult = functions.CreateContext(&swapchainContext, &swapchainDesc.header, nullptr);
    std::printf("[fsr-probe] CreateContext(swapchain for hwnd, version=%u) result=%s (0x%llX) context=%p swapchain=%p\n",
                swapchainVersion.version, resultName(swapchainResult),
                static_cast<unsigned long long>(swapchainResult), static_cast<void*>(swapchainContext),
                static_cast<void*>(proxySwapchain));
    if (swapchainResult != FFX_API_RETURN_OK || swapchainContext == nullptr || proxySwapchain == nullptr) {
        if (swapchainContext != nullptr) functions.DestroyContext(&swapchainContext, nullptr);
        DestroyWindow(hwnd);
        return 9;
    }
    swapchain.Attach(proxySwapchain);
    proxySwapchain->AddRef();  // application-owned reference (Attach adopted the borrowed one)
    DXGI_SWAP_CHAIN_DESC1 actual{};
    std::printf("[fsr-probe] proxy swapchain GetDesc1=%s format=%d buffers=%u\n",
                SUCCEEDED(swapchain->GetDesc1(&actual)) ? "OK" : "FAILED", int(actual.Format), actual.BufferCount);
    std::printf("[fsr-probe] localUsageMiB after-swapchain-context=%.1f\n", localUsageMiB());

    // Stage 2: frame-generation context. The version desc asks for the newest
    // API surface the header knows; when a provider rejects that the probe
    // retries without it so the failure mode is recorded instead of guessed.
    ComPtr<ID3D12Resource> motionTexture = makeTexture(device.Get(), renderWidth, renderHeight,
                                                       DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> depthTexture = makeTexture(device.Get(), renderWidth, renderHeight,
                                                      DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!motionTexture || !depthTexture) {
        std::printf("[fsr-probe] guidance texture creation failed\n");
        functions.DestroyContext(&swapchainContext, nullptr);
        DestroyWindow(hwnd);
        return 10;
    }

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device.Get();
    ffxCreateContextDescFrameGenerationVersion fgVersion{};
    fgVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    fgVersion.version = FFX_FRAMEGENERATION_VERSION;

    ffxCreateContextDescFrameGeneration fgDesc{};
    fgDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    fgDesc.flags = 0;
    fgDesc.displaySize = {kProbeWidth, kProbeHeight};
    fgDesc.maxRenderSize = {std::max(kProbeWidth, renderWidth), std::max(kProbeHeight, renderHeight)};
    fgDesc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

    ffxContext fgContext = nullptr;
    fgDesc.header.pNext = &backendDesc.header;
    fgVersion.header.pNext = nullptr;
    backendDesc.header.pNext = &fgVersion.header;
    auto fgResult = functions.CreateContext(&fgContext, &fgDesc.header, nullptr);
    std::printf("[fsr-probe] CreateContext(framegen, headerVersion=%u) result=%s (0x%llX) context=%p\n",
                fgVersion.version, resultName(fgResult), static_cast<unsigned long long>(fgResult),
                static_cast<void*>(fgContext));
    if (fgResult != FFX_API_RETURN_OK || fgContext == nullptr) {
        fgDesc.header.pNext = &backendDesc.header;
        backendDesc.header.pNext = nullptr;
        fgResult = functions.CreateContext(&fgContext, &fgDesc.header, nullptr);
        std::printf("[fsr-probe] CreateContext(framegeneration, no version desc) result=%s (0x%llX) context=%p\n",
                    resultName(fgResult), static_cast<unsigned long long>(fgResult), static_cast<void*>(fgContext));
    }
    if (fgResult != FFX_API_RETURN_OK || fgContext == nullptr) {
        std::printf("[fsr-probe] frame-generation context unavailable; stopping\n");
        functions.DestroyContext(&swapchainContext, nullptr);
        DestroyWindow(hwnd);
        return 11;
    }
    std::printf("[fsr-probe] localUsageMiB after-framegen-context=%.1f\n", localUsageMiB());

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, uavHeap;
    HANDLE fenceEvent = nullptr;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        std::printf("[fsr-probe] command objects failed\n");
        functions.DestroyContext(&fgContext, nullptr);
        functions.DestroyContext(&swapchainContext, nullptr);
        DestroyWindow(hwnd);
        return 12;
    }
    list->Close();
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 4;
    D3D12_DESCRIPTOR_HEAP_DESC uavDesc{};
    uavDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavDesc.NumDescriptors = 2;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap))) ||
        FAILED(device->CreateDescriptorHeap(&uavDesc, IID_PPV_ARGS(&uavHeap)))) {
        std::printf("[fsr-probe] descriptor heaps failed\n");
        functions.DestroyContext(&fgContext, nullptr);
        functions.DestroyContext(&swapchainContext, nullptr);
        DestroyWindow(hwnd);
        return 13;
    }
    const UINT rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const UINT uavIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = 0; i < scd.BufferCount; ++i) {
        ComPtr<ID3D12Resource> buffer;
        if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)))) {
            device->CreateRenderTargetView(buffer.Get(), nullptr,
                {rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + size_t(i) * rtvIncrement});
        }
    }
    device->CreateUnorderedAccessView(motionTexture.Get(), nullptr, nullptr,
        {uavHeap->GetCPUDescriptorHandleForHeapStart().ptr});
    device->CreateUnorderedAccessView(depthTexture.Get(), nullptr, nullptr,
        {uavHeap->GetCPUDescriptorHandleForHeapStart().ptr + uavIncrement});
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    PresentCounters counters{};
    uint64_t prepareFailures = 0, configureFailures = 0, dispatchFailures = 0, queryFailures = 0, presentFailures = 0;
    uint64_t preparedFrames = 0;
    ID3D12Resource* lastInterpolationTarget = nullptr;
    ComPtr<ID3D12Resource> lastRealBackBuffer;

    for (UINT frame = 0; frame < frames; ++frame) {
        if (FAILED(allocator->Reset())) break;
        list->Reset(allocator.Get(), nullptr);

        ComPtr<ID3D12Resource> backBuffer;
        const UINT backBufferIndex = swapchain->GetCurrentBackBufferIndex();
        if (FAILED(swapchain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&backBuffer)))) break;

        D3D12_RESOURCE_BARRIER toTarget{};
        toTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toTarget.Transition.pResource = backBuffer.Get();
        toTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &toTarget);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
            rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + size_t(backBufferIndex) * rtvIncrement};
        if (motionMode) {
            // Static black background plus one white square that advances a
            // fixed number of pixels per frame.
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            list->ClearRenderTargetView(rtv, black, 0, nullptr);
            const LONG left = LONG(kMotionStartX + kMotionStep * frame);
            const D3D12_RECT square{left, LONG(kMotionTop), left + LONG(kMotionSize),
                                    LONG(kMotionTop + kMotionSize)};
            list->ClearRenderTargetView(rtv, white, 1, &square);
        } else {
            const float shade = float(frame % 60) / 60.0f;
            const float clear[4] = {shade, 0.25f, 1.0f - shade, 1.0f};
            list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        }
        std::swap(toTarget.Transition.StateBefore, toTarget.Transition.StateAfter);
        list->ResourceBarrier(1, &toTarget);

        // Guidance: constant horizontal pixel motion, mid depth. Real content
        // would give richer interpolation, but the counters only need the
        // workload to be valid.
        D3D12_RESOURCE_BARRIER guidance[2]{};
        for (auto& barrier : guidance) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        guidance[0].Transition.pResource = motionTexture.Get();
        guidance[1].Transition.pResource = depthTexture.Get();
        list->ResourceBarrier(2, guidance);
        // Constant motion matching the square: FSR reprojects "previous =
        // current + mv", so +x is where the content was one frame ago.
        const float motion[4] = {motionMode ? float(kMotionStep) : 4.0f, 0.0f, 0.0f, 0.0f};
        const float depth = 0.5f;
        list->ClearUnorderedAccessViewFloat(uavHeap->GetGPUDescriptorHandleForHeapStart(),
            {uavHeap->GetCPUDescriptorHandleForHeapStart().ptr}, motionTexture.Get(), motion, 0, nullptr);
        list->ClearUnorderedAccessViewFloat(
            {uavHeap->GetGPUDescriptorHandleForHeapStart().ptr + uavIncrement},
            {uavHeap->GetCPUDescriptorHandleForHeapStart().ptr + uavIncrement},
            depthTexture.Get(), &depth, 0, nullptr);
        for (auto& barrier : guidance) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        guidance[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        guidance[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(2, guidance);

        ffxDispatchDescFrameGenerationPrepareV2 prepare{};
        prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prepare.frameID = frame;
        prepare.flags = 0;
        prepare.commandList = list.Get();
        prepare.renderSize = {renderWidth, renderHeight};
        prepare.jitterOffset = {0.0f, 0.0f};
        prepare.motionVectorScale = {1.0f, 1.0f};  // guidance is already pixel space
        prepare.frameTimeDelta = 16.6f;
        prepare.reset = frame == 0;
        prepare.cameraNear = 0.1f;
        prepare.cameraFar = 1000.0f;
        prepare.cameraFovAngleVertical = 1.0f;
        prepare.viewSpaceToMetersFactor = 0.0f;
        prepare.depth = makeResource(depthTexture.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        prepare.motionVectors = makeResource(motionTexture.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        const auto prepareResult = functions.Dispatch(&fgContext, &prepare.header);
        if (prepareResult != FFX_API_RETURN_OK) ++prepareFailures;
        else ++preparedFrames;

        ffxConfigureDescFrameGeneration configure{};
        configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        configure.swapChain = swapchain.Get();
        configure.presentCallback = presentCallback;
        configure.presentCallbackUserContext = &counters;
        configure.frameGenerationEnabled = true;
        configure.allowAsyncWorkloads = false;
        configure.flags = 0;
        configure.generationRect = generationRect;
        configure.frameID = frame;
        const auto configureResult = functions.Configure(&fgContext, &configure.header);
        if (configureResult != FFX_API_RETURN_OK) ++configureFailures;

        ffxDispatchDescFrameGeneration dispatch{};
        dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
        dispatch.commandList = nullptr;
        dispatch.presentColor = makeResource(backBuffer.Get(), FFX_API_RESOURCE_STATE_COMMON);
        dispatch.numGeneratedFrames = generatedPerFrame;
        dispatch.reset = frame == 0;
        dispatch.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        dispatch.minMaxLuminance[0] = 0.0f;
        dispatch.minMaxLuminance[1] = 1000.0f;
        dispatch.generationRect = generationRect;
        dispatch.frameID = frame;

        ffxQueryDescFrameGenerationSwapChainInterpolationCommandListDX12 queryList{};
        queryList.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12;
        queryList.pOutCommandList = &dispatch.commandList;
        auto queryResult = functions.Query(&swapchainContext, &queryList.header);
        if (queryResult != FFX_API_RETURN_OK) ++queryFailures;
        ffxQueryDescFrameGenerationSwapChainInterpolationTextureDX12 queryTexture{};
        queryTexture.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12;
        queryTexture.pOutTexture = &dispatch.outputs[0];
        queryResult = functions.Query(&swapchainContext, &queryTexture.header);
        if (queryResult != FFX_API_RETURN_OK) ++queryFailures;

    const auto dispatchResult = functions.Dispatch(&fgContext, &dispatch.header);
    if (dispatchResult != FFX_API_RETURN_OK) ++dispatchFailures;
    lastInterpolationTarget = static_cast<ID3D12Resource*>(dispatch.outputs[0].resource);
    lastRealBackBuffer = backBuffer;
        if (frame == 0) {
            std::printf("[fsr-probe] frame0 prepare=%s configure=%s dispatch=%s interpCommandList=%p interpTexture=%p\n",
                        resultName(prepareResult), resultName(configureResult), resultName(dispatchResult),
                        dispatch.commandList, dispatch.outputs[0].resource);
        }

        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        const HRESULT presentResult = pipelined ? swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING)
                                                : swapchain->Present(1, 0);
        if (FAILED(presentResult)) ++presentFailures;
        // Pipelined mode mirrors the player: no per-frame GPU idle wait, so the
        // provider's pacing threads stay busy while the next frame is recorded.
        const UINT64 signal = frame + 1;
        queue->Signal(fence.Get(), signal);
        fence->SetEventOnCompletion(signal, fenceEvent);
        WaitForSingleObject(fenceEvent, pipelined ? 0 : 5000);
    }

    std::printf("[fsr-probe] frames=%u requestedGenerated=%u prepared=%llu real=%llu generated=%llu\n", frames,
                generatedPerFrame,
                static_cast<unsigned long long>(preparedFrames),
                static_cast<unsigned long long>(counters.real.load()),
                static_cast<unsigned long long>(counters.generated.load()));

    // Motion analysis: where does the square sit in the interpolation target?
    bool motionProven = false;
    if (motionMode && lastInterpolationTarget != nullptr && lastRealBackBuffer != nullptr) {
        // Let the provider finish its presentation work before reading its
        // interpolation target back.
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
        functions.Dispatch(&swapchainContext, &wait.header);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, 10000);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowSize = 0, total = 0;
        const D3D12_RESOURCE_DESC targetDesc = lastInterpolationTarget->GetDesc();
        device->GetCopyableFootprints(&targetDesc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        ComPtr<ID3D12Resource> motionReadback;
        D3D12_HEAP_PROPERTIES readHeap{};
        readHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC readDesc{};
        readDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readDesc.Width = total;
        readDesc.Height = 1;
        readDesc.DepthOrArraySize = 1;
        readDesc.MipLevels = 1;
        readDesc.Format = DXGI_FORMAT_UNKNOWN;
        readDesc.SampleDesc.Count = 1;
        readDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bool readbackReady = SUCCEEDED(device->CreateCommittedResource(
            &readHeap, D3D12_HEAP_FLAG_NONE, &readDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&motionReadback)));
        if (readbackReady) {
            // Copy the interpolation target and the last real back buffer into
            // the readback buffer, one at a time.
            // The interpolation target holds the frame generated between the
            // last two real frames, so the square should sit half a step behind
            // the last rendered position.
            int interpolatedLeft = -1, interpolatedRight = -1, interpolatedWidth = 0;
            double meanLuma = 0.0;
            int minLuma = 255, maxLuma = 0;
            uint64_t sampledPixels = 0;
            for (int pass = 0; pass < 1 && readbackReady; ++pass) {
                ID3D12Resource* source = lastInterpolationTarget;
                if (FAILED(allocator->Reset())) break;
                list->Reset(allocator.Get(), nullptr);
                transition(list.Get(), source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = motionReadback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = footprint;
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = source;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                transition(list.Get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                list->Close();
                ID3D12CommandList* submit[] = {list.Get()};
                queue->ExecuteCommandLists(1, submit);
                queue->Signal(fence.Get(), 2 + pass);
                fence->SetEventOnCompletion(2 + pass, fenceEvent);
                WaitForSingleObject(fenceEvent, 10000);
                void* mapped = nullptr;
                if (FAILED(motionReadback->Map(0, nullptr, &mapped))) { readbackReady = false; break; }
                const auto* pixels = static_cast<const uint8_t*>(mapped) + footprint.Offset;
                const UINT row = kMotionTop + kMotionSize / 2;
                const uint8_t* rowPixels = pixels + size_t(row) * footprint.Footprint.RowPitch;
                for (UINT y = 0; y < uint32_t(footprint.Footprint.Height); y += 8) {
                    const uint8_t* scanRow = pixels + size_t(y) * footprint.Footprint.RowPitch;
                    for (UINT x = 0; x < uint32_t(footprint.Footprint.Width); x += 8) {
                        const int green = scanRow[size_t(x) * 4 + 1];
                        meanLuma += green;
                        minLuma = std::min(minLuma, green);
                        maxLuma = std::max(maxLuma, green);
                        ++sampledPixels;
                    }
                }
                int left = -1, right = -1;
                for (UINT x = 0; x < kProbeWidth; ++x) {
                    const bool bright = rowPixels[size_t(x) * 4 + 1] > 128;
                    if (bright && left < 0) left = int(x);
                    if (bright) right = int(x);
                }
                motionReadback->Unmap(0, nullptr);
                interpolatedLeft = left;
                if (right >= 0) interpolatedWidth = right - left + 1;
                interpolatedRight = right;
            }
            if (sampledPixels > 0) meanLuma /= double(sampledPixels);
            // Interpolated edges are softer than the cleared square, so compare
            // square CENTRES: that is scale independent and is what "the frame
            // sits at the temporal midpoint" actually means.
            const double interpolatedCenter = interpolatedLeft >= 0 && interpolatedRight > interpolatedLeft
                ? (interpolatedLeft + interpolatedRight) / 2.0 : -1.0;
            const double realCenter = double(lastRealLeft) + double(kMotionSize) / 2.0;
            const double previousCenter = realCenter - double(kMotionStep);
            const double expected = (realCenter + previousCenter) / 2.0;
            const bool midpoint = interpolatedCenter >= 0.0 && std::abs(interpolatedCenter - expected) <= 4.0;
            const bool notDuplicate = interpolatedCenter >= 0.0 &&
                                      std::abs(interpolatedCenter - realCenter) > double(kMotionStep) / 4.0;
            const bool notOlderFrame = interpolatedCenter >= 0.0 &&
                                       std::abs(interpolatedCenter - previousCenter) > double(kMotionStep) / 4.0;
            motionProven = midpoint && notDuplicate && notOlderFrame;
            std::printf("[fsr-probe] motion: realCenter=%.1f previousCenter=%.1f expectedMidpoint=%.1f "
                        "interpolatedCenter=%.1f (left=%d width=%d) meanLuma=%.2f min=%d max=%d "
                        "midpoint=%d notDuplicate=%d notOlderFrame=%d\n",
                        realCenter, previousCenter, expected, interpolatedCenter, interpolatedLeft,
                        interpolatedWidth, meanLuma, minLuma, maxLuma, midpoint ? 1 : 0, notDuplicate ? 1 : 0,
                        notOlderFrame ? 1 : 0);
        }
    }
    std::printf("[fsr-probe] failures prepare=%llu configure=%llu dispatch=%llu query=%llu present=%llu callback=%llu\n",
                static_cast<unsigned long long>(prepareFailures),
                static_cast<unsigned long long>(configureFailures),
                static_cast<unsigned long long>(dispatchFailures),
                static_cast<unsigned long long>(queryFailures),
                static_cast<unsigned long long>(presentFailures),
                static_cast<unsigned long long>(counters.callbackFailures.load()));
    if (counters.intervalCount.load() > 0) {
        const double meanMs = double(counters.intervalSumUs.load()) / double(counters.intervalCount.load()) / 1000.0;
        std::printf("[fsr-probe] cadence: intervals=%llu mean=%.2fms max=%.2fms fast(<5ms)=%llu (provider submission pacing, not measured scanout)\n",
                    static_cast<unsigned long long>(counters.intervalCount.load()), meanMs,
                    double(counters.maxIntervalUs.load()) / 1000.0,
                    static_cast<unsigned long long>(counters.fastIntervals.load()));
    }

    // Ground truth: the swapchain context reports how many frames it actually
    // presented, so require the requested multiplier to be reached.
    const uint64_t expectedGenerated = uint64_t(frames - 1) * generatedPerFrame;
    const bool generatedFrames = counters.generated.load() >= expectedGenerated;
    const bool clean = prepareFailures == 0 && configureFailures == 0 && dispatchFailures == 0 &&
                       queryFailures == 0 && presentFailures == 0 && counters.callbackFailures.load() == 0;
    if (fenceEvent) CloseHandle(fenceEvent);
    // Swapchain buffers belong to the proxy: drop those references before the
    // context goes away or the release walks into freed memory.
    lastRealBackBuffer.Reset();
    list.Reset();
    allocator.Reset();
    // The proxy swapchain is owned by its context: drop our reference before
    // destroying the context, otherwise the release calls into freed memory.
    swapchain.Reset();
    const auto destroyFg = functions.DestroyContext(&fgContext, nullptr);
    const auto destroySwapchain = functions.DestroyContext(&swapchainContext, nullptr);
    std::printf("[fsr-probe] DestroyContext framegen=%s swapchain=%s\n", resultName(destroyFg), resultName(destroySwapchain));
    std::printf("[fsr-probe] localUsageMiB after-destroy=%.1f (this is what stays resident)\n", localUsageMiB());

    // Settings changes in the player destroy the AMD swapchain and create a new
    // one for the same HWND; DXGI allows a single swapchain per HWND, so this
    // proves the teardown really releases the previous one.
    {
        IDXGISwapChain4* second = nullptr;
        ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 again{};
        again.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
        again.header.pNext = &swapchainVersion.header;
        again.swapchain = &second;
        again.hwnd = hwnd;
        again.desc = &scd;
        again.dxgiFactory = factory.Get();
        again.gameQueue = queue.Get();
        ffxContext secondContext = nullptr;
        const auto recreate = functions.CreateContext(&secondContext, &again.header, nullptr);
        std::printf("[fsr-probe] re-create after teardown result=%s swapchain=%p\n", resultName(recreate),
                    static_cast<void*>(second));
        std::printf("[fsr-probe] localUsageMiB after-recreate-attempt=%.1f\n", localUsageMiB());
        if (secondContext != nullptr) functions.DestroyContext(&secondContext, nullptr);
    }
    DestroyWindow(hwnd);
    FreeLibrary(loader);

    if (motionMode && !motionProven) {
        std::printf("[fsr-probe] VERDICT generated frames reached the display path but the motion check did not prove interpolation\n");
        return 1;
    }
    if (clean && generatedFrames) {
        std::printf("[fsr-probe] VERDICT generated frames reached the present callback on this adapter\n");
        return 0;
    }
    std::printf("[fsr-probe] VERDICT no proof of generated output (clean=%d generated=%d)\n",
                clean ? 1 : 0, generatedFrames ? 1 : 0);
    return 1;
}
