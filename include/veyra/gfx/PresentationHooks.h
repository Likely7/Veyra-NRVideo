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

} // namespace veyra::gfx
