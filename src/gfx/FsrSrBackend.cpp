#include "veyra/gfx/FsrSrBackend.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <filesystem>
#include <string>

#ifdef VEYRA_HAS_FSR
#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_FSR
namespace {

FfxApiSurfaceFormat surfaceFormatFromDxgi(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R11G11B10_FLOAT: return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
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

void fsrMessage(uint32_t type, const wchar_t* message) {
    if (message == nullptr) return;
    const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, message, -1, text.data(), length, nullptr, nullptr);
    if (type == FFX_API_MESSAGE_TYPE_ERROR) log::error("fsr-sr", text);
    else log::warn("fsr-sr", text);
}

// The loader resolves providers by base name through the normal search order,
// so the provider DLL is pre-loaded by absolute path and the legacy DLL
// directory is pointed at the same folder for the duration of the calls.
struct DirectoryGuard {
    wchar_t previous[MAX_PATH]{};
    DWORD previousLength = 0;
    DirectoryGuard(const std::filesystem::path& directory) {
        previousLength = GetDllDirectoryW(MAX_PATH, previous);
        SetDllDirectoryW(directory.c_str());
    }
    ~DirectoryGuard() { SetDllDirectoryW(previousLength > 0 ? previous : nullptr); }
};

} // namespace
#endif

struct FsrSrBackend::Impl {
    bool available = false;
    uint64_t evaluateCount = 0;
    uint64_t failureCount = 0;
    std::string providerVersion;
#ifdef VEYRA_HAS_FSR
    HMODULE loader = nullptr;
    HMODULE provider = nullptr;
    ffxFunctions functions{};
    ffxContext context = nullptr;
    uint32_t maxRenderWidth = 0, maxRenderHeight = 0;
    uint32_t maxUpscaleWidth = 0, maxUpscaleHeight = 0;
#endif
};

FsrSrBackend::FsrSrBackend() : p_(std::make_unique<Impl>()) {}

FsrSrBackend::~FsrSrBackend() { release(); }

bool FsrSrBackend::initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                              uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.available) return true;
    const auto root = runtime::localDataDirectory() / "amd" / "fidelityfx";
    const auto loaderPath = std::filesystem::absolute(root / "amd_fidelityfx_loader_dx12.dll");
    const auto providerPath = std::filesystem::absolute(root / "amd_fidelityfx_upscaler_dx12.dll");
    if (!std::filesystem::exists(loaderPath)) {
        log::warn("fsr-sr", std::format("loader missing at {}", loaderPath.string()));
        return false;
    }
    DirectoryGuard guard(root);
    p.loader = LoadLibraryExW(loaderPath.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (p.loader == nullptr) {
        log::error("fsr-sr", std::format("loader load failed win32={}", GetLastError()));
        return false;
    }
    if (std::filesystem::exists(providerPath)) {
        p.provider=LoadLibraryExW(providerPath.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!p.provider)log::warn("fsr-sr", std::format("provider preload failed win32={}", GetLastError()));
    }
    ffxLoadFunctions(&p.functions, p.loader);
    if (p.functions.CreateContext == nullptr || p.functions.DestroyContext == nullptr ||
        p.functions.Dispatch == nullptr || p.functions.Query == nullptr) {
        log::error("fsr-sr", "loader is missing the FidelityFX API entry points");
        return false;
    }
    {
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = device;
        uint64_t count = 0;
        versions.outputCount = &count;
        const auto countResult = p.functions.Query(nullptr, &versions.header);
        uint64_t ids[8]{};
        const char* names[8]{};
        uint64_t capacity = 8;
        versions.outputCount = &capacity;
        versions.versionIds = ids;
        versions.versionNames = names;
        const auto listResult = countResult == FFX_API_RETURN_OK
            ? p.functions.Query(nullptr, &versions.header) : countResult;
        if (listResult == FFX_API_RETURN_OK && capacity > 0 && names[0] != nullptr) {
            p.providerVersion = names[0];
        }
        log::info("fsr-sr", std::format("providers count={} selected={}", capacity,
                                        p.providerVersion.empty() ? "none" : p.providerVersion));
    }

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device;
    ffxCreateContextDescUpscaleVersion versionDesc{};
    versionDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    versionDesc.version = FFX_UPSCALER_VERSION;
    ffxCreateContextDescUpscale upscaleDesc{};
    upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.header.pNext = &backendDesc.header;
    backendDesc.header.pNext = &versionDesc.header;
    upscaleDesc.flags = hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u;
    upscaleDesc.maxRenderSize = {maxRenderWidth, maxRenderHeight};
    upscaleDesc.maxUpscaleSize = {maxUpscaleWidth, maxUpscaleHeight};
    upscaleDesc.fpMessage = fsrMessage;
    auto result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        log::warn("fsr-sr", std::format("context with version desc failed result={}; retrying without it",
                                        returnName(result)));
        if(p.context){
            const auto cleanup=p.functions.DestroyContext(&p.context,nullptr);
            if(cleanup!=FFX_API_RETURN_OK){log::error("fsr-sr","cannot release partial context");return false;}
            p.context=nullptr;
        }
        backendDesc.header.pNext = nullptr;
        result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
    }
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        log::error("fsr-sr", std::format("context creation failed result={} (0x{:X})", returnName(result),
                                         unsigned(result)));
        return false;
    }
    p.maxRenderWidth = maxRenderWidth;
    p.maxRenderHeight = maxRenderHeight;
    p.maxUpscaleWidth = maxUpscaleWidth;
    p.maxUpscaleHeight = maxUpscaleHeight;
    p.available = true;
    log::info("fsr-sr", std::format("provider={} maxRender={}x{} maxUpscale={}x{} hdr={}",
                                    p.providerVersion.empty() ? "unknown" : p.providerVersion,
                                    maxRenderWidth, maxRenderHeight, maxUpscaleWidth, maxUpscaleHeight, hdr));
    return true;
#else
    (void)device; (void)maxRenderWidth; (void)maxRenderHeight; (void)maxUpscaleWidth; (void)maxUpscaleHeight; (void)hdr;
    log::error("fsr-sr", "AMD FidelityFX SDK headers were unavailable at build time");
    return false;
#endif
}

bool FsrSrBackend::evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                            ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                            uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available || !list) return false;
    if (renderWidth > p.maxRenderWidth || renderHeight > p.maxRenderHeight ||
        upscaleWidth > p.maxUpscaleWidth || upscaleHeight > p.maxUpscaleHeight) {
        log::error("fsr-sr", std::format("extent {}x{} -> {}x{} exceeds the created ceiling", renderWidth,
                                         renderHeight, upscaleWidth, upscaleHeight));
        ++p.failureCount;
        return false;
    }
    const auto validTexture=[](const char* role,ID3D12Resource* resource,uint32_t w,uint32_t h,bool requiresUav){
        if(!resource){
            log::error("fsr-sr",std::format("dispatch rejected resource={} missing required={}x{}",role,w,h));
            return false;
        }
        const auto desc=resource->GetDesc();
        const bool uav=(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)!=0;
        const bool valid=w&&h&&desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc.SampleDesc.Count==1
            &&desc.Width>=w&&desc.Height>=h&&(!requiresUav||uav)
            &&surfaceFormatFromDxgi(desc.Format)!=FFX_API_SURFACE_FORMAT_UNKNOWN;
        if(!valid)log::error("fsr-sr",std::format("dispatch rejected resource={} format={} extent={}x{} required={}x{} dimension={} samples={} uav={}",
            role,unsigned(desc.Format),desc.Width,desc.Height,w,h,unsigned(desc.Dimension),desc.SampleDesc.Count,uav));
        return valid;
    };
    bool valid=validTexture("color",color,renderWidth,renderHeight,false);
    valid&=validTexture("motion",motion,renderWidth,renderHeight,false);
    valid&=validTexture("depth",depth,renderWidth,renderHeight,false);
    valid&=validTexture("output",output,upscaleWidth,upscaleHeight,true);
    if(output==color)log::error("fsr-sr","dispatch rejected because output aliases color input");
    if(!valid||output==color){++p.failureCount;return false;}
    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = list;
    // Declared states are the states the caller actually set: a mismatch here
    // is what produced runtime errors in the frame-generation path.
    dispatch.color = makeResource(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.motionVectors = makeResource(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.depth = makeResource(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.exposure = {};
    dispatch.reactive = {};
    dispatch.transparencyAndComposition = {};
    dispatch.output = makeResource(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.jitterOffset = {0.0f, 0.0f};
    // Guidance motion is render-extent pixel space, so the factor that converts
    // it to UV inside the provider is 1:1 (same rule as the frame generator).
    dispatch.motionVectorScale = {1.0f, 1.0f};
    dispatch.renderSize = {renderWidth, renderHeight};
    dispatch.upscaleSize = {upscaleWidth, upscaleHeight};
    dispatch.enableSharpening = false;
    dispatch.sharpness = 0.0f;
    dispatch.frameTimeDelta = frameTimeMs > 0.0f ? frameTimeMs : 16.6f;
    dispatch.preExposure = 1.0f;
    dispatch.reset = reset;
    dispatch.cameraNear = 0.1f;
    dispatch.cameraFar = 1000.0f;
    dispatch.cameraFovAngleVertical = 1.0f;
    dispatch.viewSpaceToMetersFactor = 1.0f;
    dispatch.flags = 0;
    const auto result = p.functions.Dispatch(&p.context, &dispatch.header);
    if (result != FFX_API_RETURN_OK) {
        log::warn("fsr-sr", std::format("dispatch result={} render={}x{} upscale={}x{} reset={}", returnName(result),
                                        renderWidth, renderHeight, upscaleWidth, upscaleHeight, reset));
        ++p.failureCount;
        return false;
    }
    ++p.evaluateCount;
    return true;
#else
    (void)list; (void)color; (void)motion; (void)depth; (void)output; (void)renderWidth; (void)renderHeight;
    (void)upscaleWidth; (void)upscaleHeight; (void)reset; (void)frameTimeMs;
    return false;
#endif
}

void FsrSrBackend::release() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.context != nullptr) {
        const auto result = p.functions.DestroyContext(&p.context, nullptr);
        log::info("fsr-sr", std::format("context destroyed result={} evaluations={} failures={}", returnName(result),
                                        p.evaluateCount, p.failureCount));
        p.context = nullptr;
    }
    if (p.loader != nullptr) {
        FreeLibrary(p.loader);
        p.loader = nullptr;
    }
    if (p.provider != nullptr) {
        FreeLibrary(p.provider);
        p.provider = nullptr;
    }
    p.available = false;
#endif
}

bool FsrSrBackend::created() const { return p_->available; }
uint64_t FsrSrBackend::evaluateCount() const { return p_->evaluateCount; }
uint64_t FsrSrBackend::failureCount() const { return p_->failureCount; }
const char* FsrSrBackend::providerVersion() const { return p_->providerVersion.c_str(); }

} // namespace veyra::gfx
