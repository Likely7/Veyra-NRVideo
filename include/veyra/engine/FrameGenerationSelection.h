// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/EnhancementSettings.h"

namespace veyra::engine {
struct FrameGenerationAvailability {
    bool dlss = false, xess = false, fsr = false, fsr4 = false, vfg = false;
    constexpr bool supports(FrameGenerationBackend backend) const {
        switch (backend) {
        case FrameGenerationBackend::Dlss: return dlss;
        case FrameGenerationBackend::XeSS: return xess;
        case FrameGenerationBackend::Fsr: return fsr;
        case FrameGenerationBackend::Fsr4: return fsr4;
        case FrameGenerationBackend::Vfg: return vfg;
        }
        return false;
    }
};
constexpr FrameGenerationBackend defaultFrameGenerationBackend(uint32_t vendor) {
    return vendor == 0x1002 || vendor == 0x8086
        ? FrameGenerationBackend::Fsr : FrameGenerationBackend::Dlss;
}
// Keep usable manual choices. An unavailable saved choice prefers the
// cross-vendor FSR provider, then XeSS; never auto-select an experimental one.
constexpr FrameGenerationBackend restoredFrameGenerationBackend(
    FrameGenerationBackend saved, FrameGenerationAvailability available) {
    if (available.supports(saved)) return saved;
    if (available.fsr) return FrameGenerationBackend::Fsr;
    if (available.xess) return FrameGenerationBackend::XeSS;
    if (available.dlss) return FrameGenerationBackend::Dlss;
    return saved; // No provider: the caller disables the effect with its reason.
}
}
