#pragma once
// Monitoring overlays that inject into this process and hook presentation.
//
// Field crashes 2026-10-01: with RivaTuner Statistics Server (MSI Afterburner's on-screen
// display, RTSSHooks64.dll) injected, every session that turned XeSS frame generation on
// crashed inside d3d11.dll a second or two later: RTSS draws its OSD with D3D11On12 into
// the XeSS proxy swapchain. GamePP (游加加) held swapchain buffers so every resize failed
// and the driver crashed on a window change. The same tools are fine with the ordinary
// DXGI swapchain and with DLSS frame generation.
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <iterator>
#include <string>

namespace veyra::gfx {

struct PresentationHook {
    const wchar_t* module;
    const char* product;
};

inline constexpr PresentationHook kPresentationHooks[] = {
    {L"RTSSHooks64.dll", "RivaTuner Statistics Server (MSI Afterburner)"},
    {L"GPP64.dll", "GamePP"},
    {L"shade64.dll", "GamePP"},
    {L"GameTracker64.dll", "GamePP"},
};

// The first injected overlay found, or nullptr.
inline const PresentationHook* injectedPresentationHook() {
    for (const auto& hook : kPresentationHooks)
        if (GetModuleHandleW(hook.module)) return &hook;
    return nullptr;
}

// The overlay that keeps XeSS frame generation off, or nullptr. Since the interface draws
// with Direct3D 12 (main.cpp) the crash may be gone; VEYRA_ALLOW_XESS_WITH_OVERLAYS=1 lets
// a tester with RivaTuner or GamePP installed try XeSS anyway without a new build.
inline const PresentationHook* xessBlockingHook() {
    if (GetEnvironmentVariableW(L"VEYRA_ALLOW_XESS_WITH_OVERLAYS", nullptr, 0) > 0) return nullptr;
    return injectedPresentationHook();
}

// Every loaded module that is neither part of Windows nor shipped next to the executable,
// as "name; name; …" for the log. Overlays, recorders and monitoring tools show up here;
// a field log with a mystery slowdown can then say which of them was in the process.
inline std::string thirdPartyModules() {
    HMODULE modules[1024]; DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return "unavailable";
    wchar_t windowsDir[MAX_PATH]{}, exeDir[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    if (auto* slash = wcsrchr(exeDir, L'\\')) slash[1] = 0;
    const auto startsWith = [](const wchar_t* path, const wchar_t* prefix) {
        const size_t n = wcslen(prefix); return n && _wcsnicmp(path, prefix, n) == 0;
    };
    std::string out;
    const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), DWORD(std::size(modules)));
    for (DWORD i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(modules[i], path, MAX_PATH)) continue;
        // Driver components live under Windows\System32\DriverStore; keep those, they carry
        // the driver's own overlay and filter modules.
        if ((startsWith(path, windowsDir) && !wcsstr(path, L"\\DriverStore\\")) || startsWith(path, exeDir)) continue;
        char utf8[MAX_PATH * 3]{};
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, int(sizeof(utf8)), nullptr, nullptr);
        if (!out.empty()) out += "; ";
        out += utf8;
    }
    return out.empty() ? "none" : out;
}

} // namespace veyra::gfx
