#include "veyra/gfx/FsrSrBackend.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

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

const char* versionModeName(FsrSrVersionMode mode) {
    switch (mode) {
    case FsrSrVersionMode::Fsr31: return "FSR 3.1";
    case FsrSrVersionMode::Fsr4: return "FSR 4.x";
    default: return "auto";
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
    FsrSrVersionMode versionMode = FsrSrVersionMode::Auto;
    uint32_t maxRenderWidth = 0, maxRenderHeight = 0;
    uint32_t maxUpscaleWidth = 0, maxUpscaleHeight = 0;
#endif
};

FsrSrBackend::FsrSrBackend() : p_(std::make_unique<Impl>()) {}

FsrSrBackend::~FsrSrBackend() { release(); }

bool FsrSrBackend::initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                              uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr,
                              FsrSrVersionMode versionMode) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.available) {
        if (p.versionMode != versionMode)
            log::warn("fsr-sr", std::format("already initialized as {}; keeping the existing context instead of {}",
                                            versionModeName(p.versionMode), versionModeName(versionMode)));
        return true;
    }
    // A previous attempt may have left the loader or provider loaded; release it
    // instead of overwriting the handles.
    if (p.loader != nullptr || p.provider != nullptr || p.context != nullptr) release();
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
    // Provider inventory: every installed provider reports its version name here
    // (ffx_api.h:97). The name pointers are only valid during the call, so they
    // are copied out immediately; the ranking then decides the try order.
    struct Candidate { uint64_t id = 0; std::string name; };
    std::vector<Candidate> candidates;
    {
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = device;
        uint64_t count = 0;
        versions.outputCount = &count;
        auto result = p.functions.Query(nullptr, &versions.header);
        if (result != FFX_API_RETURN_OK || count == 0 || count > 64) {
            if (versionMode != FsrSrVersionMode::Auto) {
                log::error("fsr-sr", std::format("provider inventory failed result={} count={} requested={}",
                                                 returnName(result), count, versionModeName(versionMode)));
                return false;
            }
            log::warn("fsr-sr", std::format("provider inventory failed result={} count={}; creating without a version override",
                                            returnName(result), count));
        } else {
            std::vector<uint64_t> ids(size_t(count), 0);
            std::vector<const char*> rawNames(size_t(count), nullptr);
            // outputCount is in/out: capacity in, returned count out.
            versions.outputCount = &count;
            versions.versionIds = ids.data();
            versions.versionNames = rawNames.data();
            result = p.functions.Query(nullptr, &versions.header);
            if (result != FFX_API_RETURN_OK || count > ids.size()) {
                if (versionMode != FsrSrVersionMode::Auto) {
                    log::error("fsr-sr", std::format("provider inventory read failed result={} count={} capacity={} requested={}",
                                                     returnName(result), count, ids.size(), versionModeName(versionMode)));
                    return false;
                }
                log::warn("fsr-sr", std::format("provider inventory read failed result={} count={} capacity={}; creating without a version override",
                                                returnName(result), count, ids.size()));
            } else {
                std::vector<std::string> names;
                names.reserve(size_t(count));
                for (size_t i = 0; i < size_t(count); ++i) names.emplace_back(rawNames[i] ? rawNames[i] : "");
                const auto order = rankedFfxProviderIndices(names, versionMode);
                for (size_t i = 0; i < size_t(count); ++i)
                    log::info("fsr-sr", std::format("inventory id=0x{:X} name={} requested={}", ids[i],
                                                    names[i].empty() ? "unknown" : names[i], versionModeName(versionMode)));
                if (order.empty() && versionMode != FsrSrVersionMode::Auto) {
                    log::error("fsr-sr", std::format("requested FSR {} provider unavailable in inventory; no version fallback",
                                                     versionModeName(versionMode)));
                    return false;
                }
                for (const auto index : order) candidates.push_back({ids[index], names[index]});
            }
        }
    }

    ffxCreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device;
    ffxCreateContextDescUpscaleVersion versionDesc{};
    versionDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    versionDesc.version = FFX_UPSCALER_VERSION;
    ffxCreateContextDescUpscale upscaleDesc{};
    upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.flags = hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u;
    upscaleDesc.maxRenderSize = {maxRenderWidth, maxRenderHeight};
    upscaleDesc.maxUpscaleSize = {maxUpscaleWidth, maxUpscaleHeight};
    upscaleDesc.fpMessage = fsrMessage;
    // The provider reports the version it actually created, so the request is
    // verified against the context and not just against the inventory.
    std::string reported;
    const auto tryCreate = [&](std::optional<uint64_t> overrideId, std::string_view expectedName) -> bool {
        const bool overrideUsed = overrideId.has_value();
        const auto overrideNote = [&] { return overrideUsed ? std::format(" override=0x{:X}", *overrideId) : std::string(); };
        ffxOverrideVersion overrideVersion{};
        overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        overrideVersion.versionId = overrideId.value_or(0);
        upscaleDesc.header.pNext = &backendDesc.header;
        backendDesc.header.pNext = &versionDesc.header;
        versionDesc.header.pNext = overrideUsed ? &overrideVersion.header : nullptr;
        auto result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
        if (result != FFX_API_RETURN_OK || p.context == nullptr) {
            log::warn("fsr-sr", std::format("context with version desc failed result={}{}; retrying without it",
                                            returnName(result), overrideNote()));
            if (p.context != nullptr) {
                const auto cleanup = p.functions.DestroyContext(&p.context, nullptr);
                p.context = nullptr;
                if (cleanup != FFX_API_RETURN_OK) { log::error("fsr-sr", "cannot release partial context"); return false; }
                p.context = nullptr;
            }
            // The version override is the selection and stays in the chain; only
            // the API-version descriptor (a build-compatibility note) is dropped.
            versionDesc.header.pNext = nullptr;
            backendDesc.header.pNext = overrideUsed ? &overrideVersion.header : nullptr;
            result = p.functions.CreateContext(&p.context, &upscaleDesc.header, nullptr);
        }
        if (result != FFX_API_RETURN_OK || p.context == nullptr) {
            log::warn("fsr-sr", std::format("context creation failed result={} (0x{:X}){}", returnName(result),
                                            unsigned(result), overrideNote()));
            // A non-OK create can still hand back a partial context; release it
            // before dropping the handle, otherwise the provider state leaks.
            if (p.context != nullptr) {
                if (p.functions.DestroyContext(&p.context, nullptr) != FFX_API_RETURN_OK)
                    log::error("fsr-sr", "cannot release partial context");
                p.context = nullptr;
            }
            return false;
        }
        ffxQueryGetProviderVersion providerVersion{};
        providerVersion.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        providerVersion.versionId = 0;
        providerVersion.versionName = nullptr;
        const bool queried = p.functions.Query(&p.context, &providerVersion.header) == FFX_API_RETURN_OK &&
                             providerVersion.versionName != nullptr && *providerVersion.versionName != '\0';
        reported = queried ? std::string(providerVersion.versionName) : std::string();
        const auto destroy = [&](const char* why) {
            log::warn("fsr-sr", why);
            if (p.functions.DestroyContext(&p.context, nullptr) != FFX_API_RETURN_OK)
                log::error("fsr-sr", "cannot release rejected context");
            p.context = nullptr;
            reported.clear();
            return false;
        };
        const auto reportedVersion = parseFfxSemver(reported);
        if (reportedVersion.valid) {
            if (matchesFsrSrVersion(reported, versionMode)) return true;
            // Auto accepts any named provider; an explicit request does not.
            if (versionMode != FsrSrVersionMode::Auto)
                return destroy("provider readback does not match the requested version; rejecting the context");
        }
        // No usable version metadata: the inventory already proved this target, so
        // a metadata gap is not an SR failure.
        if (!overrideUsed || versionMode == FsrSrVersionMode::Auto)
            log::warn("fsr-sr", std::format("provider version query {}; using {} from the inventory",
                                            queried ? "returned no version" : "failed",
                                            expectedName.empty() ? "an unknown name" : expectedName));
        else
            log::warn("fsr-sr", std::format("provider version query unavailable; keeping the inventory target {}", expectedName));
        reported = std::string(expectedName);
        return true;
    };
    bool contextReady = false;
    for (const auto& candidate : candidates) {
        if (tryCreate(candidate.id, candidate.name)) { contextReady = true; break; }
    }
    // An explicit request never falls back; Auto may still take whatever the
    // loader installs by default.
    if (!contextReady && versionMode == FsrSrVersionMode::Auto) contextReady = tryCreate(std::nullopt, {});
    if (!contextReady) {
        p.context = nullptr;
        p.providerVersion.clear();
        p.available = false;
        log::error("fsr-sr", std::format("AMD FSR upscaling unavailable requested={}", versionModeName(versionMode)));
        return false;
    }
    p.providerVersion = reported;
    p.maxRenderWidth = maxRenderWidth;
    p.maxRenderHeight = maxRenderHeight;
    p.maxUpscaleWidth = maxUpscaleWidth;
    p.maxUpscaleHeight = maxUpscaleHeight;
    p.available = true;
    p.versionMode = versionMode;
    log::info("fsr-sr", std::format("context ready requested={} provider={} maxRender={}x{} maxUpscale={}x{} hdr={}",
                                    versionModeName(versionMode), p.providerVersion.empty() ? "unknown" : p.providerVersion,
                                    maxRenderWidth, maxRenderHeight, maxUpscaleWidth, maxUpscaleHeight, hdr));
    return true;
#else
    (void)device; (void)maxRenderWidth; (void)maxRenderHeight; (void)maxUpscaleWidth; (void)maxUpscaleHeight; (void)hdr;
    (void)versionMode;
    log::error("fsr-sr", "AMD FidelityFX SDK headers were unavailable at build time");
    return false;
#endif
}

bool FsrSrBackend::evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                            ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                            uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs,
                            float sharpness) {
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
    // The provider's own RCAS pass: 0 = none, 1 = maximum (ffx_upscale.h). The
    // dispatch value is a live uniform, so the whole range needs no rebuild.
    const float clampedSharpness = (std::isfinite(sharpness) && sharpness > 0.0f) ? std::min(sharpness, 1.0f) : 0.0f;
    dispatch.enableSharpening = clampedSharpness > 0.0f;
    dispatch.sharpness = clampedSharpness;
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
    (void)upscaleWidth; (void)upscaleHeight; (void)reset; (void)frameTimeMs; (void)sharpness;
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
