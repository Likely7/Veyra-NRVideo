#include "veyra/gfx/XessSrBackend.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <filesystem>
#include <string>

#ifdef VEYRA_HAS_XESS
#include <xess/xess.h>
#include <xess/xess_d3d12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_XESS
namespace {
// Pick the XeSS quality preset whose dynamic input-resolution range contains the
// actual source->output ratio. XeSS-SR presets map to fixed scale factors but
// accept a range (xessGetOptimalInputResolution); choose the closest preset.
xess_quality_settings_t qualityForRatio(float ratio) {
    if (ratio <= 1.05f) return XESS_QUALITY_SETTING_AA;
    if (ratio <= 1.35f) return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
    if (ratio <= 1.55f) return XESS_QUALITY_SETTING_ULTRA_QUALITY;
    if (ratio <= 1.75f) return XESS_QUALITY_SETTING_QUALITY;
    if (ratio <= 2.05f) return XESS_QUALITY_SETTING_BALANCED;
    if (ratio <= 2.35f) return XESS_QUALITY_SETTING_PERFORMANCE;
    return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
}
} // namespace
#endif

struct XessSrBackend::Impl {
    bool available = false;
    uint64_t evaluateCount = 0;
    uint64_t failureCount = 0;
    std::string providerVersion;
#ifdef VEYRA_HAS_XESS
    HMODULE dll = nullptr;
    xess_context_handle_t context = nullptr;
    uint32_t maxRenderWidth = 0, maxRenderHeight = 0;
    uint32_t maxUpscaleWidth = 0, maxUpscaleHeight = 0;
    bool hdr = false;
    decltype(&xessD3D12CreateContext) createContext = nullptr;
    decltype(&xessD3D12Init) init = nullptr;
    decltype(&xessD3D12Execute) execute = nullptr;
    decltype(&xessDestroyContext) destroy = nullptr;
    decltype(&xessGetVersion) getVersion = nullptr;
    decltype(&xessGetOptimalInputResolution) optimalInput = nullptr;
#endif
};
XessSrBackend::XessSrBackend() : p_(std::make_unique<Impl>()) {}
XessSrBackend::~XessSrBackend() { release(); }

bool XessSrBackend::initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                               uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr) {
#ifdef VEYRA_HAS_XESS
    auto& p = *p_;
    if (p.available) return true;
    const auto root = runtime::localDataDirectory() / "intel" / "experimental";
    const auto path = std::filesystem::absolute(root / "libxess.dll");
    if (!std::filesystem::exists(path)) {
        log::warn("xess-sr", std::format("libxess.dll missing at {}", path.string()));
        return false;
    }
    p.dll = LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!p.dll) { log::error("xess-sr", std::format("LoadLibrary failed win32={}", GetLastError())); return false; }
#define LOADX(member, name) p.member = reinterpret_cast<decltype(&name)>(GetProcAddress(p.dll, #name)); if (!p.member) { log::error("xess-sr", "missing export " #name); release(); return false; }
    LOADX(createContext, xessD3D12CreateContext);
    LOADX(init, xessD3D12Init);
    LOADX(execute, xessD3D12Execute);
    LOADX(destroy, xessDestroyContext);
    LOADX(getVersion, xessGetVersion);
    LOADX(optimalInput, xessGetOptimalInputResolution);
#undef LOADX
    xess_version_t ver{};
    if (p.getVersion(&ver) == XESS_RESULT_SUCCESS)
        p.providerVersion = std::format("{}.{}.{}", ver.major, ver.minor, ver.patch);
    if (p.createContext(device, &p.context) != XESS_RESULT_SUCCESS) { log::error("xess-sr", "CreateContext failed"); release(); return false; }
    p.maxRenderWidth = maxRenderWidth; p.maxRenderHeight = maxRenderHeight;
    p.maxUpscaleWidth = maxUpscaleWidth; p.maxUpscaleHeight = maxUpscaleHeight; p.hdr = hdr;
    xess_d3d12_init_params_t init{};
    init.outputResolution = { maxUpscaleWidth, maxUpscaleHeight };
    init.qualitySetting = qualityForRatio(float(maxUpscaleWidth) / float(maxRenderWidth ? maxRenderWidth : 1));
    // Veyra's SR guidance motion (flowTex_) is at render/input extent, matching
    // XeSS's default low-res-MV contract; depth (constant far) is supplied and
    // used for dilation. Do NOT set HIGH_RES_MV (that expects output-extent MV).
    init.initFlags = hdr ? XESS_INIT_FLAG_NONE : XESS_INIT_FLAG_LDR_INPUT_COLOR;
    const auto r = p.init(p.context, &init);
    if (r != XESS_RESULT_SUCCESS) { log::error("xess-sr", std::format("Init failed result={}", int(r))); release(); return false; }
    p.available = true;
    log::info("xess-sr", std::format("selected render={}x{} output={}x{} provider={} hdr={}", maxRenderWidth, maxRenderHeight, maxUpscaleWidth, maxUpscaleHeight, p.providerVersion, hdr));
    return true;
#else
    (void)device; (void)maxRenderWidth; (void)maxRenderHeight; (void)maxUpscaleWidth; (void)maxUpscaleHeight; (void)hdr;
    log::error("xess-sr", "Intel XeSS SDK unavailable at build time");
    return false;
#endif
}

bool XessSrBackend::evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                             ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                             uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs) {
#ifdef VEYRA_HAS_XESS
    auto& p = *p_;
    if (!p.available || !list || !color || !motion || !depth || !output) { ++p.failureCount; return false; }
    (void)frameTimeMs; (void)upscaleWidth; (void)upscaleHeight;
    xess_d3d12_execute_params_t ex{};
    ex.pColorTexture = color;
    ex.pVelocityTexture = motion;
    ex.pDepthTexture = depth;
    ex.pOutputTexture = output;
    ex.jitterOffsetX = 0.0f; ex.jitterOffsetY = 0.0f;   // video has no subpixel jitter
    ex.exposureScale = 1.0f;
    ex.resetHistory = reset ? 1u : 0u;
    ex.inputWidth = renderWidth; ex.inputHeight = renderHeight;
    const auto r = p.execute(p.context, list, &ex);
    if (r != XESS_RESULT_SUCCESS) { ++p.failureCount; log::error("xess-sr", std::format("Execute failed result={}", int(r))); return false; }
    ++p.evaluateCount;
    return true;
#else
    (void)list; (void)color; (void)motion; (void)depth; (void)output; (void)renderWidth; (void)renderHeight;
    (void)upscaleWidth; (void)upscaleHeight; (void)reset; (void)frameTimeMs;
    return false;
#endif
}

void XessSrBackend::release() {
#ifdef VEYRA_HAS_XESS
    auto& p = *p_;
    if (p.context && p.destroy) p.destroy(p.context);
    p.context = nullptr;
    if (p.dll) FreeLibrary(p.dll);
    p.dll = nullptr;
    p.available = false;
#endif
}

bool XessSrBackend::created() const { return p_->available; }
uint64_t XessSrBackend::evaluateCount() const { return p_->evaluateCount; }
uint64_t XessSrBackend::failureCount() const { return p_->failureCount; }
const char* XessSrBackend::providerVersion() const { return p_->providerVersion.c_str(); }

} // namespace veyra::gfx