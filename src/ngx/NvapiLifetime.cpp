#include "veyra/ngx/NvapiLifetime.h"
#include "veyra/Log.h"
#include <format>
#include <string>

namespace veyra::ngx {
HMODULE processNvapi()
{
    // One load/pin per process, including concurrent callers and graph rebuilds.
    // Intentionally no module-owning destructor: driver detach under the loader
    // lock must not run on the render thread after NR has been torn down (#67).
    static const HMODULE module = []() -> HMODULE {
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            log::error("nvapi-lifetime", std::format("system directory lookup failed error={} length={}", GetLastError(), length));
            return nullptr;
        }
        const std::wstring path = std::wstring(directory) + L"\\nvapi64.dll";
        HMODULE loaded = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loaded) {
            log::error("nvapi-lifetime", std::format("system NVAPI load failed error={}", GetLastError()));
            return nullptr;
        }
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(loaded), &pinned)) {
            // Fail NR initialization; retain this one reference rather than risk
            // triggering the very driver detach this policy is meant to avoid.
            log::error("nvapi-lifetime", std::format("system NVAPI pin failed error={}; NR disabled", GetLastError()));
            return nullptr;
        }
        log::info("nvapi-lifetime", "system NVAPI pinned until process exit; no per-graph unload");
        return pinned;
    }();
    return module;
}
}
