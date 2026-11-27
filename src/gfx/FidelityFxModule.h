#pragma once

#include "veyra/Log.h"

#include <windows.h>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>

namespace veyra::gfx {

inline HMODULE loadResidentFidelityFxModule(const std::filesystem::path& path, const char* logTag) {
    // SR and FG share the loader. It can retain provider code addresses after a
    // context is destroyed, so keep one module reference until process exit.
    static std::mutex mutex;
    static std::map<std::filesystem::path, HMODULE> modules;
    std::lock_guard lock(mutex);
    if (const auto found = modules.find(path); found != modules.end()) return found->second;
    const auto module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module) {
        modules.emplace(path, module);
    } else {
        const auto error = GetLastError();
        log::warn(logTag, std::format("runtime load failed path={} win32={}", path.string(), error));
    }
    return module;
}

} // namespace veyra::gfx
