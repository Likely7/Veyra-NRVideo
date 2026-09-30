#include "veyra/gfx/FsrFgPresenter.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef VEYRA_HAS_FSR
#include <ffx_api_loader.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_FSR
namespace {

HMODULE loadResidentModule(const std::filesystem::path& path) {
    // One retained reference per absolute module path, even when graph rebuilds
    // or independent export workers create many short-lived contexts.
    static std::mutex mutex;
    static std::map<std::wstring, HMODULE> modules;
    std::lock_guard lock(mutex);
    const auto key = path.wstring();
    if (const auto found = modules.find(key); found != modules.end()) return found->second;
    const auto module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module) modules.emplace(key, module);
    else log::error("fsr-fg", std::format("runtime load failed path={} win32={}", path.string(), GetLastError()));
    return module;
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

struct FsrFgPresenter::Impl {
    uint64_t frameId = 0;
    uint32_t width = 0, height = 0, backBufferFormat = 0;
    bool failed = false, available = false;
    std::string providerVersion;
#ifdef VEYRA_HAS_FSR
    HMODULE loader = nullptr;
    ffxFunctions functions{};
    ffxContext fgContext = nullptr;
#endif
};

FsrFgPresenter::FsrFgPresenter() : p_(std::make_unique<Impl>()) {}

bool FsrFgPresenter::initializeIndependent(ID3D12Device* device, uint32_t width, uint32_t height,
                                         DXGI_FORMAT format, bool requireMl) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!device || !width || !height || p.fgContext) return false;
    const auto root = std::filesystem::absolute(runtime::localDataDirectory() / "amd" / "fidelityfx");
    const auto loaderPath = root / "amd_fidelityfx_loader_dx12.dll";
    const auto providerPath = root / "amd_fidelityfx_framegeneration_dx12.dll";
    // Absolute preloading makes provider discovery deterministic. Modules stay
    // resident, as in the existing integration; contexts are independently freed.
    p.loader = loadResidentModule(loaderPath);
    const auto provider = loadResidentModule(providerPath);
    if (!p.loader || !provider) {
        log::error("fsr-fg", std::format("independent runtime load failed root={} win32={}", root.string(), GetLastError()));
        p.failed = true; return false;
    }
    ffxLoadFunctions(&p.functions, p.loader);
    if (!p.functions.CreateContext || !p.functions.DestroyContext || !p.functions.Query ||
        !p.functions.Configure || !p.functions.Dispatch) {
        log::error("fsr-fg", "required loader API missing"); p.failed = true; return false;
    }

    uint64_t count = 0;
    ffxQueryDescGetVersions versions{};
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    versions.device = device; versions.outputCount = &count;
    auto result = p.functions.Query(nullptr, &versions.header);
    if (result != FFX_API_RETURN_OK || count == 0 || count > 64) {
        log::error("fsr-fg", std::format("provider inventory result={} count={}", returnName(result), count));
        p.failed = true; return false;
    }
    std::vector<uint64_t> ids(size_t(count), 0);
    std::vector<const char*> names(size_t(count), nullptr);
    versions.versionIds = ids.data(); versions.versionNames = names.data();
    result = p.functions.Query(nullptr, &versions.header);
    if (result != FFX_API_RETURN_OK || count > ids.size()) {
        log::error("fsr-fg", std::format("provider inventory read result={} count={} capacity={}", returnName(result), count, ids.size()));
        p.failed = true; return false;
    }
    uint64_t selected = 0;
    const char* family = requireMl ? "4.0." : "3.1.";
    for (size_t i = 0; i < size_t(count); ++i) {
        const std::string_view name = names[i] ? names[i] : "unknown";
        log::info("fsr-fg", std::format("inventory id=0x{:X} name={} request={}", ids[i], name, family));
        if (!selected && name.find(family) != std::string_view::npos) selected = ids[i];
    }
    if (!selected) {
        log::error("fsr-fg", std::format("requested FSR {} provider unavailable on this adapter; no version fallback", family));
        p.failed = true; return false;
    }
    ffxOverrideVersion overrideVersion{};
    overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    overrideVersion.versionId = selected;
    ffxCreateContextDescFrameGenerationVersion apiVersion{};
    apiVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    apiVersion.version = FFX_FRAMEGENERATION_VERSION;
    apiVersion.header.pNext = &overrideVersion.header;
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = device; backend.header.pNext = &apiVersion.header;
    ffxCreateContextDescFrameGeneration desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    desc.header.pNext = &backend.header;
    desc.flags = FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS |
        ((format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R10G10B10A2_UNORM)
            ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0);
    desc.displaySize = desc.maxRenderSize = {width, height};
    desc.backBufferFormat = surfaceFormatFromDxgi(format);
    result = p.functions.CreateContext(&p.fgContext, &desc.header, nullptr);
    if (result != FFX_API_RETURN_OK || !p.fgContext) {
        log::error("fsr-fg", std::format("independent CreateContext id=0x{:X} result={} code=0x{:X}", selected, returnName(result), unsigned(result)));
        p.failed = true; return false;
    }
    ffxQueryGetProviderVersion actual{};
    actual.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    result = p.functions.Query(&p.fgContext, &actual.header);
    p.providerVersion = actual.versionName ? actual.versionName : "unknown";
    if (result != FFX_API_RETURN_OK || actual.versionId != selected ||
        p.providerVersion.find(family) == std::string::npos) {
        log::error("fsr-fg", std::format("actual provider rejected query={} requested=0x{:X} actual=0x{:X} name={}", returnName(result), selected, actual.versionId, p.providerVersion));
        p.failed = true; return false;
    }
    p.width = width; p.height = height; p.backBufferFormat = desc.backBufferFormat;
    p.available = true;
    log::info("fsr-fg", std::format("independent context provider={} id=0x{:X} extent={}x{} format={} swapchainContexts=0 generatedPerPair=1 guidance=estimated-video virtualCamera=1",
        p.providerVersion, actual.versionId, width, height, int(format)));
    return true;
#else
    (void)device; (void)width; (void)height; (void)format; (void)requireMl;
    log::error("fsr-fg", "FSR SDK headers unavailable"); return false;
#endif
}

bool FsrFgPresenter::generate(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                            ID3D12Resource* motion, ID3D12Resource* depth, ID3D12Resource* output,
                            bool reset, float elapsedMs) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available || p.failed || !list || !color || !motion || !depth || !output ||
        color == output) return false;
    const auto c = color->GetDesc(), m = motion->GetDesc(), d = depth->GetDesc(), o = output->GetDesc();
    if (c.Width != p.width || c.Height != p.height || o.Width != c.Width || o.Height != c.Height ||
        c.Format != o.Format || uint32_t(surfaceFormatFromDxgi(c.Format)) != p.backBufferFormat ||
        m.Width != c.Width || m.Height != c.Height || m.Format != DXGI_FORMAT_R16G16_FLOAT ||
        d.Width != c.Width || d.Height != c.Height || d.Format != DXGI_FORMAT_R32_FLOAT ||
        !(o.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) return false;
    const auto check = [&](ffxReturnCode_t result, const char* stage) {
        if (result == FFX_API_RETURN_OK) return true;
        log::error("fsr-fg", std::format("independent {} result={} code=0x{:X} frameId={}", stage, returnName(result), unsigned(result), p.frameId));
        p.failed = true; return false;
    };
    ffxConfigureDescFrameGeneration configure{};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    configure.frameGenerationEnabled = true; configure.frameID = p.frameId;
    configure.generationRect = {0, 0, int32_t(p.width), int32_t(p.height)};
    if (!check(p.functions.Configure(&p.fgContext, &configure.header), "Configure")) return false;
    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.commandList = list; prepare.frameID = p.frameId;
    prepare.renderSize = {p.width, p.height}; prepare.motionVectorScale = {1, 1};
    prepare.frameTimeDelta = std::isfinite(elapsedMs) && elapsedMs > 0 ? elapsedMs : 16.6667f;
    prepare.reset = reset; prepare.cameraNear = 0.1f; prepare.cameraFar = 1000.0f;
    prepare.cameraFovAngleVertical = 1.0f; prepare.viewSpaceToMetersFactor = 1.0f;
    // Video has estimated depth/motion, no engine camera. Use a stable virtual
    // right-handed basis, not zero-length vectors, and label this limitation.
    prepare.cameraUp[1] = prepare.cameraRight[0] = prepare.cameraForward[2] = 1.0f;
    prepare.depth = makeResource(depth, FFX_API_RESOURCE_STATE_COMMON);
    prepare.motionVectors = makeResource(motion, FFX_API_RESOURCE_STATE_COMMON);
    if (!check(p.functions.Dispatch(&p.fgContext, &prepare.header), "PrepareV2")) return false;
    ffxDispatchDescFrameGeneration dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    dispatch.commandList = list; dispatch.frameID = p.frameId; dispatch.reset = reset;
    dispatch.presentColor = makeResource(color, FFX_API_RESOURCE_STATE_COMMON);
    dispatch.outputs[0] = makeResource(output, FFX_API_RESOURCE_STATE_COMMON);
    dispatch.numGeneratedFrames = 1; dispatch.generationRect = configure.generationRect;
    dispatch.backbufferTransferFunction = c.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
        ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB : c.Format == DXGI_FORMAT_R10G10B10A2_UNORM
        ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    dispatch.minMaxLuminance[0] = 0; dispatch.minMaxLuminance[1] = 1000;
    if (!check(p.functions.Dispatch(&p.fgContext, &dispatch.header), "Generate")) return false;
    if (p.frameId < 3 || log::verboseFrameLogs())
        log::info("fsr-fg", std::format("independent dispatch frameId={} reset={} elapsedMs={} outputs=1 transfer={} extent={}x{}",
            p.frameId, reset, prepare.frameTimeDelta, dispatch.backbufferTransferFunction, p.width, p.height));
    ++p.frameId;
    return true;
#else
    (void)list; (void)color; (void)motion; (void)depth; (void)output; (void)reset; (void)elapsedMs;
    return false;
#endif
}

void FsrFgPresenter::shutdown() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    // The graph owner drains its queue and all texture consumers first.
    // No swapchain context or provider present thread exists on this path.
    if (p.fgContext) {
        const auto result = p.functions.DestroyContext(&p.fgContext, nullptr);
        log::info("fsr-fg", std::format("independent context destroyed result={}", returnName(result)));
        p.fgContext = nullptr;
    }
    p.available = false;
#endif
}
FsrFgPresenter::~FsrFgPresenter() { shutdown(); }
uint32_t FsrFgPresenter::maxGeneratedFrames() const { return p_->available ? 1u : 0u; }
bool FsrFgPresenter::available() const { return p_->available; }
bool FsrFgPresenter::failed() const { return p_->failed; }
const char* FsrFgPresenter::providerVersion() const { return p_->providerVersion.c_str(); }
} // namespace veyra::gfx
