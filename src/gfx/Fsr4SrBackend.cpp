#include "veyra/gfx/Fsr4SrBackend.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <mutex>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

#ifdef VEYRA_HAS_FSR
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_FSR
namespace {

// ---- driver provider ABI (mirrors OptiScaler fsr4/FSR4DriverProvider.h) ----
struct ProviderInterface {
    uint64_t versionId;
    const char* versionName;
    bool (*canProvide)(uint64_t descType);
    ffxReturnCode_t (*createContext)(ffxContext*, ffxCreateContextDescHeader*, const ffxAllocationCallbacks*);
    ffxReturnCode_t (*destroyContext)(ffxContext*, const ffxAllocationCallbacks*);
    ffxReturnCode_t (*configure)(ffxContext*, ffxConfigureDescHeader*);
    ffxReturnCode_t (*query)(ffxContext*, ffxQueryDescHeader*);
    ffxReturnCode_t (*dispatch)(ffxContext*, const ffxDispatchDescHeader*);
};
struct ExternalProviderData {
    uint32_t structVersion;
    uint64_t descType;
    ProviderInterface provider;
    bool (*isSupported)(const void* backend);
    void* reserved;
};
static_assert(offsetof(ExternalProviderData, isSupported) == 0x50);
static_assert(sizeof(ExternalProviderData) == 0x60);
typedef HRESULT(STDMETHODCALLTYPE* PFN_UpdateFfxApiProvider)(void*, uint32_t);

// ---- RTTI vtable finder (mirrors OptiScaler scanner/rtti.cpp) ----
struct TypeDescriptor { void* vtable; void* spare; char name[1]; };
struct CompleteObjectLocator {
    uint32_t signature, offset, constructorDisplacementOffset;
    uint32_t typeDescriptor, classHierarchyDescriptor, self;
};
enum class SectionKind { Code, ReadOnlyData, WritableData };

std::vector<std::span<const uint8_t>> getSections(HMODULE module, SectionKind kind) {
    std::vector<std::span<const uint8_t>> sections;
    auto base = reinterpret_cast<const uint8_t*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        const bool code = section->Characteristics & IMAGE_SCN_MEM_EXECUTE;
        const bool writable = section->Characteristics & IMAGE_SCN_MEM_WRITE;
        const auto sectionKind = code ? SectionKind::Code : (writable ? SectionKind::WritableData : SectionKind::ReadOnlyData);
        if (sectionKind == kind)
            sections.emplace_back(base + section->VirtualAddress, section->Misc.VirtualSize);
    }
    return sections;
}

void** findVtable(HMODULE module, const char* className) {
    const TypeDescriptor* typeDescriptor = nullptr;
    char mangled[256];
    for (auto prefix : {".?AV", ".?AU"}) {
        std::snprintf(mangled, sizeof(mangled), "%s%s@@", prefix, className);
        const size_t needleLen = std::strlen(mangled) + 1;
        for (auto section : getSections(module, SectionKind::WritableData)) {
            std::string_view data(reinterpret_cast<const char*>(section.data()), section.size());
            if (auto position = data.find(std::string_view(mangled, needleLen)); position != std::string_view::npos) {
                auto name = data.data() + position;
                typeDescriptor = reinterpret_cast<const TypeDescriptor*>(name - offsetof(TypeDescriptor, name));
                break;
            }
        }
        if (typeDescriptor) break;
    }
    if (!typeDescriptor) return nullptr;

    auto base = reinterpret_cast<const uint8_t*>(module);
    auto typeDescriptorRva = static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(typeDescriptor) - base);
    const CompleteObjectLocator* locator = nullptr;
    auto roSections = getSections(module, SectionKind::ReadOnlyData);
    for (auto section : roSections) {
        for (size_t offset = 0; offset + sizeof(CompleteObjectLocator) <= section.size(); offset += alignof(uint32_t)) {
            auto candidate = reinterpret_cast<const CompleteObjectLocator*>(section.data() + offset);
            auto locatorRva = static_cast<uint32_t>(section.data() + offset - base);
            if (candidate->signature == 1 && candidate->offset == 0 &&
                candidate->typeDescriptor == typeDescriptorRva && candidate->self == locatorRva) {
                locator = candidate;
                break;
            }
        }
        if (locator) break;
    }
    if (!locator) return nullptr;

    for (auto section : roSections) {
        auto entries = reinterpret_cast<const uintptr_t*>(section.data());
        auto count = section.size() / sizeof(uintptr_t);
        for (size_t i = 0; i + 1 < count; i++) {
            if (entries[i] == reinterpret_cast<uintptr_t>(locator))
                return reinterpret_cast<void**>(const_cast<uintptr_t*>(&entries[i + 1]));
        }
    }
    return nullptr;
}

bool patchVtableEntry(void** entry, void* value) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &oldProtection)) return false;
    *entry = value;
    VirtualProtect(entry, sizeof(*entry), oldProtection, &oldProtection);
    return true;
}

// IsSupported(void* device) is vtable index 2 in FFX SDK 2.0+ providers.
// The hook forces the INT8 Lite model and rejects the others, so the driver
// cannot silently fall back to FSR3 or pick an FP8 model this GPU lacks.
//
// The driver DLL, its SDK preloads and the vtable patches are process-wide
// state shared by every backend instance (dual-SR creates two). A mutex plus
// a reference count install the hooks exactly once and restore/unload only
// after the last context is destroyed; a second instance never saves the
// hook itself as the "original".
constexpr size_t kIsSupportedIndex = 2;
typedef bool (*PFN_IsSupported)(void* provider, void* device);
struct PatchedProvider { void** vtable; bool isLite; PFN_IsSupported original; };

struct SharedRuntime {
    std::mutex mutex;
    int refs = 0;
    HMODULE driver = nullptr;
    HMODULE sdkLoader = nullptr;
    HMODULE sdkUpscaler = nullptr;
    ProviderInterface provider{};
    std::string providerVersion;
    std::vector<PatchedProvider> patched;
};
SharedRuntime g_shared;

bool STDMETHODCALLTYPE hkIsSupported(void* provider, void* /*device*/) {
    auto vtable = *static_cast<void***>(provider);
    std::scoped_lock lock(g_shared.mutex);
    for (auto& patched : g_shared.patched) {
        if (patched.vtable != vtable) continue;
        return patched.isLite;
    }
    return false;
}

// Caller holds g_shared.mutex.
void hookModelsLocked(HMODULE module) {
    struct Entry { const char* className; bool isLite; };
    const Entry entries[] = {
        {"ffxProvider_FSR4", false},
        {"ffxProvider_FSR4_Int8", false},
        {"ffxProvider_FSR4_Int8_Navi2", false},
        {"ffxProvider_FSR4Lite_Int8", true},
    };
    for (auto& entry : entries) {
        auto vtable = findVtable(module, entry.className);
        if (!vtable) continue;
        PatchedProvider patched{vtable, entry.isLite,
                                reinterpret_cast<PFN_IsSupported>(vtable[kIsSupportedIndex])};
        if (patchVtableEntry(&vtable[kIsSupportedIndex], reinterpret_cast<void*>(&hkIsSupported))) {
            g_shared.patched.push_back(patched);
            log::info("fsr4-sr", std::format("model gate hooked: {}", entry.className));
        }
    }
}

// Caller holds g_shared.mutex. Restores hooks and unloads every module the
// shared runtime owns; used by every init failure path and the last release.
void teardownSharedLocked() {
    for (auto& patched : g_shared.patched) {
        if (patched.original) patchVtableEntry(&patched.vtable[kIsSupportedIndex],
                                               reinterpret_cast<void*>(patched.original));
    }
    g_shared.patched.clear();
    g_shared.provider = {};
    g_shared.providerVersion.clear();
    if (g_shared.driver) { FreeLibrary(g_shared.driver); g_shared.driver = nullptr; }
    if (g_shared.sdkUpscaler) { FreeLibrary(g_shared.sdkUpscaler); g_shared.sdkUpscaler = nullptr; }
    if (g_shared.sdkLoader) { FreeLibrary(g_shared.sdkLoader); g_shared.sdkLoader = nullptr; }
}

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
    default: return "OTHER";
    }
}

void fsr4Message(uint32_t type, const wchar_t* message) {
    if (message == nullptr) return;
    const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length > 0 ? length - 1 : 0), '\0');
    if (length > 1) WideCharToMultiByte(CP_UTF8, 0, message, -1, text.data(), length, nullptr, nullptr);
    if (type == FFX_API_MESSAGE_TYPE_ERROR) log::error("fsr4-sr", text);
    else log::warn("fsr4-sr", text);
}

std::filesystem::path runtimeRoot() {
    return runtime::localDataDirectory() / "amd" / "fidelityfx";
}

} // namespace
#endif

struct Fsr4SrBackend::Impl {
    bool available = false;
    uint64_t evaluateCount = 0;
    uint64_t failureCount = 0;
#ifdef VEYRA_HAS_FSR
    // Per-instance state only; DLLs/hooks/provider live in g_shared.
    ffxContext context = nullptr;
    uint32_t maxRenderWidth = 0, maxRenderHeight = 0;
    uint32_t maxUpscaleWidth = 0, maxUpscaleHeight = 0;
#endif
};

Fsr4SrBackend::Fsr4SrBackend() : p_(std::make_unique<Impl>()) {}
Fsr4SrBackend::~Fsr4SrBackend() { release(); }

bool Fsr4SrBackend::runtimeAvailable() {
#ifdef VEYRA_HAS_FSR
    const auto root = runtimeRoot();
    return std::filesystem::exists(root / "amdxcffx64.dll") &&
           std::filesystem::exists(root / "amd_fidelityfx_loader_dx12.dll") &&
           std::filesystem::exists(root / "amd_fidelityfx_upscaler_dx12.dll");
#else
    return false;
#endif
}

bool Fsr4SrBackend::initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                               uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (p.available) return true;
    if (!device) return false;
    // Hard Intel gate: this backend exists for the FSR4-on-Arc path only.
    // The UI hides it elsewhere, but the backend must refuse on its own so
    // no other vendor's build can ever enter the hook path.
    {
        // D3D12 device -> IDXGIDevice QI is unreliable on some drivers (Arc
        // B390 field log 2026-11-26: the whole chain failed and the gate saw
        // vendor 0). Resolve the adapter by the device's own LUID instead.
        const LUID luid = device->GetAdapterLuid();
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc{};
        UINT vendor = 0;
        const HRESULT factoryHr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        HRESULT enumHr = E_FAIL, descHr = E_FAIL;
        if (SUCCEEDED(factoryHr)) {
            enumHr = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
            if (SUCCEEDED(enumHr)) {
                descHr = adapter->GetDesc1(&desc);
                if (SUCCEEDED(descHr)) vendor = desc.VendorId;
            }
        }
        if (vendor != 0x8086) {
            log::warn("fsr4-sr", std::format("non-Intel adapter vendor=0x{:04X}; refusing (factoryHr=0x{:X} enumHr=0x{:X} descHr=0x{:X})",
                                             vendor, unsigned(factoryHr), unsigned(enumHr), unsigned(descHr)));
            return false;
        }
    }
    // Provider init runs under the mutex; the third-party createContext /
    // configure calls run WITHOUT it (their IsSupported callback re-locks
    // g_shared.mutex). A reserved ref keeps another thread from tearing the
    // runtime down while this context is being created.
    {
    std::scoped_lock lock(g_shared.mutex);
    if (g_shared.refs == 0) {
        const auto root = runtimeRoot();
        const auto driverPath = std::filesystem::absolute(root / "amdxcffx64.dll");
        if (!std::filesystem::exists(driverPath)) {
            log::warn("fsr4-sr", std::format("driver DLL missing at {}", driverPath.string()));
            return false;
        }
        // The driver validates the FFX SDK it finds in the process; preload the
        // signed SDK loader + upscaler beside it before the provider query.
        for (auto name : {L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_upscaler_dx12.dll"}) {
            const auto path = std::filesystem::absolute(root / name);
            if (!std::filesystem::exists(path)) continue;
            auto module = LoadLibraryExW(path.c_str(), nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) {
                log::warn("fsr4-sr", std::format("SDK preload failed win32={}", GetLastError()));
                continue;
            }
            if (g_shared.sdkLoader == nullptr) g_shared.sdkLoader = module;
            else g_shared.sdkUpscaler = module;
        }
        g_shared.driver = LoadLibraryExW(driverPath.c_str(), nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!g_shared.driver) {
            log::error("fsr4-sr", std::format("driver load failed win32={}", GetLastError()));
            teardownSharedLocked();
            return false;
        }
        auto updateProvider = reinterpret_cast<PFN_UpdateFfxApiProvider>(
            GetProcAddress(g_shared.driver, "UpdateFfxApiProvider"));
        if (!updateProvider) {
            log::error("fsr4-sr", "UpdateFfxApiProvider export missing");
            teardownSharedLocked();
            return false;
        }
        hookModelsLocked(g_shared.driver);
        for (uint32_t structVersion : {2u, 1u}) {
            ExternalProviderData data{};
            data.structVersion = structVersion;
            data.descType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            const uint32_t size = structVersion >= 3 ? sizeof(data)
                                                     : uint32_t(offsetof(ExternalProviderData, isSupported));
            const HRESULT hr = updateProvider(&data, size);
            if (SUCCEEDED(hr) && data.provider.createContext) {
                g_shared.provider = data.provider;
                g_shared.providerVersion = data.provider.versionName ? data.provider.versionName : "unknown";
                log::info("fsr4-sr", std::format("provider={} structVersion={}", g_shared.providerVersion,
                                                 structVersion));
                break;
            }
            log::warn("fsr4-sr", std::format("provider query v{} hr=0x{:X}", structVersion, unsigned(hr)));
        }
        if (!g_shared.provider.createContext) {
            log::error("fsr4-sr", "no upscaler provider from the driver DLL");
            teardownSharedLocked();
            return false;
        }
    }
    ++g_shared.refs;
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
    upscaleDesc.fpMessage = fsr4Message;
    auto result = g_shared.provider.createContext(&p.context, &upscaleDesc.header, nullptr);
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        backendDesc.header.pNext = nullptr;
        result = g_shared.provider.createContext(&p.context, &upscaleDesc.header, nullptr);
    }
    if (result != FFX_API_RETURN_OK || p.context == nullptr) {
        log::error("fsr4-sr", std::format("context creation failed result={}", returnName(result)));
        // Roll the reservation back through the shared teardown path.
        p.available = true;
        release();
        return false;
    }
    if (g_shared.provider.configure) {
        ffxConfigureDescGlobalDebug1 global{};
        global.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        global.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        global.fpMessage = fsr4Message;
        g_shared.provider.configure(&p.context, &global.header);
    }
    // Ref already reserved above; just publish the instance state.
    p.maxRenderWidth = maxRenderWidth;
    p.maxRenderHeight = maxRenderHeight;
    p.maxUpscaleWidth = maxUpscaleWidth;
    p.maxUpscaleHeight = maxUpscaleHeight;
    p.available = true;
    log::info("fsr4-sr", std::format("ready maxRender={}x{} maxUpscale={}x{} hdr={} refs={}",
                                     maxRenderWidth, maxRenderHeight, maxUpscaleWidth, maxUpscaleHeight, hdr,
                                     g_shared.refs));
    return true;
#else
    (void)device; (void)maxRenderWidth; (void)maxRenderHeight;
    (void)maxUpscaleWidth; (void)maxUpscaleHeight; (void)hdr;
    log::error("fsr4-sr", "AMD FidelityFX SDK headers were unavailable at build time");
    return false;
#endif
}

bool Fsr4SrBackend::evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                             ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth,
                             uint32_t renderHeight, uint32_t upscaleWidth, uint32_t upscaleHeight,
                             bool reset, float frameTimeMs) {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available || !list) return false;
    if (renderWidth > p.maxRenderWidth || renderHeight > p.maxRenderHeight ||
        upscaleWidth > p.maxUpscaleWidth || upscaleHeight > p.maxUpscaleHeight) {
        log::error("fsr4-sr", std::format("extent {}x{} -> {}x{} exceeds the created ceiling", renderWidth,
                                          renderHeight, upscaleWidth, upscaleHeight));
        ++p.failureCount;
        return false;
    }
    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = list;
    // Declared states match the states the caller actually set (same rule as
    // FsrSrBackend): NON_PIXEL_SHADER_RESOURCE is COMPUTE_READ, not
    // PIXEL_COMPUTE_READ; a mismatch produced runtime errors in the FG path.
    dispatch.color = makeResource(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.depth = makeResource(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.motionVectors = makeResource(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.output = makeResource(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.jitterOffset = {0.0f, 0.0f};
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
    const auto result = g_shared.provider.dispatch(&p.context, &dispatch.header);
    if (result != FFX_API_RETURN_OK) {
        log::error("fsr4-sr", std::format("dispatch failed result={}", returnName(result)));
        ++p.failureCount;
        return false;
    }
    ++p.evaluateCount;
    // First dispatch and every 300th: proves the ML path actually ran and at
    // which extents, so a silent plain-scale fallback is visible in the log.
    if (p.evaluateCount == 1 || p.evaluateCount % 300 == 0) {
        log::info("fsr4-sr", std::format("evaluate#{} render={}x{} upscale={}x{} reset={} deltaMs={:.2f}",
                                         p.evaluateCount, renderWidth, renderHeight, upscaleWidth, upscaleHeight,
                                         reset, dispatch.frameTimeDelta));
    }
    return true;
#else
    (void)list; (void)color; (void)motion; (void)depth; (void)output;
    (void)renderWidth; (void)renderHeight; (void)upscaleWidth; (void)upscaleHeight;
    (void)reset; (void)frameTimeMs;
    return false;
#endif
}

void Fsr4SrBackend::release() {
#ifdef VEYRA_HAS_FSR
    auto& p = *p_;
    if (!p.available && p.context == nullptr) return;
    std::scoped_lock lock(g_shared.mutex);
    if (p.context && g_shared.provider.destroyContext) {
        g_shared.provider.destroyContext(&p.context, nullptr);
        p.context = nullptr;
    }
    if (p.available && g_shared.refs > 0) --g_shared.refs;
    p.available = false;
    // Restore the vtable patches and unload only after the last context is
    // gone; another live instance keeps the hooks and modules alive.
    if (g_shared.refs == 0) teardownSharedLocked();
#endif
}

bool Fsr4SrBackend::created() const { return p_->available; }
uint64_t Fsr4SrBackend::evaluateCount() const { return p_->evaluateCount; }
uint64_t Fsr4SrBackend::failureCount() const { return p_->failureCount; }
const char* Fsr4SrBackend::providerVersion() const {
#ifdef VEYRA_HAS_FSR
    return g_shared.providerVersion.c_str();
#else
    return "";
#endif
}

} // namespace veyra::gfx
