// AMD FSR upscaling probe (diagnostic only, never shipped in the player).
//
// Answers two questions with evidence instead of assumption:
//   1. which FSR upscaler provider this adapter advertises (3.1.x vs the 4.x
//      ML family) and
//   2. whether an upscale actually runs here: a procedural checkerboard plus
//      gradient is uploaded at render resolution, dispatched through FSR to a
//      larger output, read back and validated (non-black, real contrast, and
//      the pattern still lands where it should after upscaling).
//
// Usage: veyra_fsr_upscale_probe [path-to-amd_fidelityfx_loader_dx12.dll]
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kRenderWidth = 1280;
constexpr UINT kRenderHeight = 720;
constexpr UINT kUpscaleWidth = 2560;
constexpr UINT kUpscaleHeight = 1440;

const char* resultName(ffxReturnCode_t code) {
    switch (code) {
    case FFX_API_RETURN_OK: return "OK";
    case FFX_API_RETURN_ERROR: return "ERROR";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "ERROR_UNKNOWN_DESCTYPE";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "ERROR_RUNTIME_ERROR";
    case FFX_API_RETURN_NO_PROVIDER: return "NO_PROVIDER";
    case FFX_API_RETURN_ERROR_PARAMETER: return "ERROR_PARAMETER";
    default: return "OTHER";
    }
}

FfxApiResource makeResource(ID3D12Resource* resource, uint32_t state) {
    FfxApiResource api{};
    if (resource == nullptr) return api;
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    api.resource = resource;
    api.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    api.description.format = desc.Format == DXGI_FORMAT_R16G16_FLOAT ? FFX_API_SURFACE_FORMAT_R16G16_FLOAT
        : desc.Format == DXGI_FORMAT_R32_FLOAT ? FFX_API_SURFACE_FORMAT_R32_FLOAT
        : FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
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

ComPtr<ID3D12Resource> makeTexture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format) {
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
        return {};
    }
    return resource;
}

ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    const D3D12_RESOURCE_STATES initial = type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST
                                                                          : D3D12_RESOURCE_STATE_GENERIC_READ;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               initial, nullptr, IID_PPV_ARGS(&resource)))) {
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

void reportVersions(const ffxFunctions& functions, uint64_t createDescType, const char* label, void* device) {
    uint64_t count = 0;
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = createDescType;
    query.device = device;
    query.outputCount = &count;
    const auto countResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-up] %s versionsQuery=%s count=%llu\n", label, resultName(countResult),
                static_cast<unsigned long long>(count));
    if (countResult != FFX_API_RETURN_OK || count == 0 || count > 32) return;
    std::vector<uint64_t> ids(count, 0);
    std::vector<const char*> names(count, nullptr);
    uint64_t capacity = count;
    query.outputCount = &capacity;
    query.versionIds = ids.data();
    query.versionNames = names.data();
    const auto listResult = functions.Query(nullptr, &query.header);
    std::printf("[fsr-up] %s versionsList=%s returned=%llu\n", label, resultName(listResult),
                static_cast<unsigned long long>(capacity));
    for (uint64_t i = 0; i < capacity && i < ids.size(); ++i) {
        std::printf("[fsr-up]   %s[%llu] id=%llu name=%s\n", label, static_cast<unsigned long long>(i),
                    static_cast<unsigned long long>(ids[i]), names[i] ? names[i] : "(null)");
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const auto loaderPath = std::filesystem::absolute(argc > 1 ? argv[1] : VEYRA_FFX_LOADER);
    SetCurrentDirectoryW(loaderPath.parent_path().wstring().c_str());
    HMODULE loader = LoadLibraryW(loaderPath.filename().wstring().c_str());
    if (loader == nullptr) {
        std::printf("[fsr-up] loader load failed win32=%lu\n", GetLastError());
        return 2;
    }
    ffxFunctions functions{};
    ffxLoadFunctions(&functions, loader);
    if (functions.CreateContext == nullptr || functions.DestroyContext == nullptr ||
        functions.Dispatch == nullptr || functions.Query == nullptr) {
        std::printf("[fsr-up] loader is missing the FidelityFX API entry points\n");
        return 3;
    }

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
        std::printf("[fsr-up] adapter selection failed\n");
        return 4;
    }
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    std::wprintf(L"[fsr-up] adapter=%s vendor=0x%04X\n", adapterDesc.Description, adapterDesc.VendorId);

    ComPtr<ID3D12Device> device;
    bool diag = false;
    for (int i = 1; i < argc; ++i) if (_wcsicmp(argv[i], L"--diag") == 0) diag = true;
    if (diag) {
        // Debug layer + GPU-based validation, the same pair the product's
        // quality probe uses, so probe results can be compared directly.
        ComPtr<ID3D12Debug> debug;
        ComPtr<ID3D12Debug1> gbv;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))) && SUCCEEDED(debug.As(&gbv))) {
            debug->EnableDebugLayer();
            gbv->SetEnableGPUBasedValidation(TRUE);
            std::printf("[fsr-up] debug layer + GPU-based validation enabled\n");
        }
    }
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::printf("[fsr-up] D3D12CreateDevice failed\n");
        return 5;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
        std::printf("[fsr-up] CreateCommandQueue failed\n");
        return 6;
    }
    reportVersions(functions, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "upscale", device.Get());

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device.Get();
    ffxCreateContextDescUpscaleVersion versionDesc{};
    versionDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    versionDesc.version = FFX_UPSCALER_VERSION;

    ffxCreateContextDescUpscale upscaleDesc{};
    upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.header.pNext = &backendDesc.header;
    backendDesc.header.pNext = &versionDesc.header;
    upscaleDesc.flags = 0;
    upscaleDesc.maxRenderSize = {kRenderWidth, kRenderHeight};
    upscaleDesc.maxUpscaleSize = {kUpscaleWidth, kUpscaleHeight};
    ffxContext context = nullptr;
    auto createResult = functions.CreateContext(&context, &upscaleDesc.header, nullptr);
    std::printf("[fsr-up] CreateContext(upscale, version=%u) result=%s (0x%llX) context=%p\n", versionDesc.version,
                resultName(createResult), static_cast<unsigned long long>(createResult), static_cast<void*>(context));
    if (createResult != FFX_API_RETURN_OK || context == nullptr) {
        backendDesc.header.pNext = nullptr;
        createResult = functions.CreateContext(&context, &upscaleDesc.header, nullptr);
        std::printf("[fsr-up] CreateContext(upscale, no version desc) result=%s context=%p\n",
                    resultName(createResult), static_cast<void*>(context));
    }
    if (createResult != FFX_API_RETURN_OK || context == nullptr) {
        std::printf("[fsr-up] no upscaler context; stopping\n");
        return 7;
    }
    if (functions.Configure != nullptr) {
        ffxConfigureDescGlobalDebug1 global{};
        global.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        global.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        functions.Configure(&context, &global.header);
    }

    ComPtr<ID3D12Resource> color = makeTexture(device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
    ComPtr<ID3D12Resource> output = makeTexture(device.Get(), kUpscaleWidth, kUpscaleHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
    ComPtr<ID3D12Resource> motion = makeTexture(device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT);
    ComPtr<ID3D12Resource> depth = makeTexture(device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R32_FLOAT);
    if (!color || !output || !motion || !depth) {
        std::printf("[fsr-up] texture creation failed\n");
        functions.DestroyContext(&context, nullptr);
        return 8;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readFootprint{};
    UINT colorRows = 0, readRows = 0;
    UINT64 colorRowSize = 0, colorTotal = 0, readRowSize = 0, readTotal = 0;
    const D3D12_RESOURCE_DESC colorDesc = color->GetDesc();
    const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
    device->GetCopyableFootprints(&colorDesc, 0, 1, 0, &colorFootprint, &colorRows, &colorRowSize, &colorTotal);
    device->GetCopyableFootprints(&outputDesc, 0, 1, 0, &readFootprint, &readRows, &readRowSize, &readTotal);
    ComPtr<ID3D12Resource> upload = makeBuffer(device.Get(), colorTotal, D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> readback = makeBuffer(device.Get(), readTotal, D3D12_HEAP_TYPE_READBACK);
    if (!upload || !readback) {
        std::printf("[fsr-up] staging buffers failed\n");
        functions.DestroyContext(&context, nullptr);
        return 9;
    }
    // Procedural input: 64-pixel checkerboard modulated by a horizontal
    // gradient, so both structure and contrast survive a correct upscale.
    {
        void* mapped = nullptr;
        if (FAILED(upload->Map(0, nullptr, &mapped))) {
            std::printf("[fsr-up] upload map failed\n");
            functions.DestroyContext(&context, nullptr);
            return 10;
        }
        auto* base = static_cast<uint8_t*>(mapped) + colorFootprint.Offset;
        for (UINT y = 0; y < kRenderHeight; ++y) {
            uint8_t* row = base + size_t(y) * colorFootprint.Footprint.RowPitch;
            for (UINT x = 0; x < kRenderWidth; ++x) {
                const bool light = ((x / 64) + (y / 64)) % 2 == 0;
                const uint8_t ramp = uint8_t(x * 255 / (kRenderWidth - 1));
                row[size_t(x) * 4 + 0] = light ? ramp : uint8_t(255 - ramp);
                row[size_t(x) * 4 + 1] = light ? 255 : 0;
                row[size_t(x) * 4 + 2] = light ? 0 : 255;
                row[size_t(x) * 4 + 3] = 255;
            }
        }
        upload->Unmap(0, nullptr);
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        std::printf("[fsr-up] command objects failed\n");
        functions.DestroyContext(&context, nullptr);
        return 11;
    }
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Upload the pattern and clear the guidance textures to a static scene.
    transition(list.Get(), color.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = color.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = colorFootprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    transition(list.Get(), color.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Guidance is a static scene: both textures are zero-initialised committed
    // resources, so only the read state has to be prepared.
    for (auto* guidance : {motion.Get(), depth.Get()}) {
        transition(list.Get(), guidance, D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = list.Get();
    dispatch.color = makeResource(color.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatch.depth = makeResource(depth.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatch.motionVectors = makeResource(motion.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    dispatch.output = makeResource(output.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.jitterOffset = {0.0f, 0.0f};
    dispatch.motionVectorScale = {1.0f, 1.0f};
    dispatch.renderSize = {kRenderWidth, kRenderHeight};
    dispatch.upscaleSize = {kUpscaleWidth, kUpscaleHeight};
    dispatch.enableSharpening = true;
    dispatch.sharpness = 0.5f;
    dispatch.frameTimeDelta = 16.6f;
    dispatch.preExposure = 1.0f;
    dispatch.reset = true;
    dispatch.cameraNear = 0.1f;
    dispatch.cameraFar = 1000.0f;
    dispatch.cameraFovAngleVertical = 1.0f;
    dispatch.viewSpaceToMetersFactor = 0.0f;
    dispatch.flags = 0;
    const auto dispatchResult = functions.Dispatch(&context, &dispatch.header);
    std::printf("[fsr-up] Dispatch(upscale %ux%u -> %ux%u) result=%s (0x%llX)\n", kRenderWidth, kRenderHeight,
                kUpscaleWidth, kUpscaleHeight, resultName(dispatchResult),
                static_cast<unsigned long long>(dispatchResult));

    transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION outDst{};
    outDst.pResource = readback.Get();
    outDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    outDst.PlacedFootprint = readFootprint;
    D3D12_TEXTURE_COPY_LOCATION outSrc{};
    outSrc.pResource = output.Get();
    outSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    outSrc.SubresourceIndex = 0;
    list->CopyTextureRegion(&outDst, 0, 0, 0, &outSrc, nullptr);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, fenceEvent);
    WaitForSingleObject(fenceEvent, 10000);

    const auto* pixels = static_cast<const uint8_t*>(nullptr);
    void* mapped = nullptr;
    double mean = 0.0;
    int distinct = 0;
    uint8_t minLuma = 255, maxLuma = 0;
    bool structureOk = false;
    if (SUCCEEDED(readback->Map(0, nullptr, &mapped))) {
        pixels = static_cast<const uint8_t*>(mapped) + readFootprint.Offset;
        std::vector<uint8_t> histogram(256, 0);
        for (UINT y = 0; y < kUpscaleHeight; ++y) {
            const uint8_t* row = pixels + size_t(y) * readFootprint.Footprint.RowPitch;
            for (UINT x = 0; x < kUpscaleWidth; ++x) {
                const uint8_t luma = row[size_t(x) * 4 + 1];
                mean += luma;
                histogram[luma] = 1;
                minLuma = std::min(minLuma, luma);
                maxLuma = std::max(maxLuma, luma);
            }
        }
        mean /= double(kUpscaleWidth) * double(kUpscaleHeight);
        distinct = int(std::count(histogram.begin(), histogram.end(), uint8_t(1)));
        // The 64-pixel checkerboard of the input must survive as alternating
        // bright/dark blocks in the upscaled image: sample block centres (so
        // edge blending is not what is measured) and compare the phase means.
        const auto sample = [&](UINT x, UINT y) {
            const uint8_t* row = pixels + size_t(y) * readFootprint.Footprint.RowPitch;
            return int(row[size_t(x) * 4 + 1]);
        };
        double lightSum = 0.0, darkSum = 0.0;
        int lightCount = 0, darkCount = 0;
        for (UINT ry = 32; ry < kRenderHeight; ry += 64) {
            for (UINT rx = 32; rx < kRenderWidth; rx += 64) {
                const bool light = ((rx / 64) + (ry / 64)) % 2 == 0;
                const int green = sample(rx * (kUpscaleWidth / kRenderWidth), ry * (kUpscaleHeight / kRenderHeight));
                if (light) { lightSum += green; ++lightCount; } else { darkSum += green; ++darkCount; }
            }
        }
        structureOk = lightCount > 0 && darkCount > 0 &&
                      (lightSum / lightCount - darkSum / darkCount) > 100.0;
        readback->Unmap(0, nullptr);
    }
    std::printf("[fsr-up] readback mean=%.2f min=%u max=%u distinctLevels=%d checkerStructure=%s\n", mean, minLuma,
                maxLuma, distinct, structureOk ? "ok" : "unexpected");
    const auto destroyResult = functions.DestroyContext(&context, nullptr);
    std::printf("[fsr-up] DestroyContext=%s\n", resultName(destroyResult));
    if (fenceEvent) CloseHandle(fenceEvent);
    FreeLibrary(loader);

    const bool ok = dispatchResult == FFX_API_RETURN_OK && mean > 1.0 && distinct > 4 && structureOk;
    std::printf("[fsr-up] VERDICT %s\n", ok ? "FSR upscaling produced a real image on this adapter"
                                             : "no proof of a correct upscaled image");
    return ok ? 0 : 1;
}
