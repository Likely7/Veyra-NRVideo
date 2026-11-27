// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/EnhancementSettings.h"

namespace veyra::engine {
struct FrameGenerationAvailability {
    bool dlss = false, xess = false, fsr = false, fsr4 = false, vfg = false, anr = false, amdFrc = false, rife = false;
    constexpr bool supports(FrameGenerationBackend backend) const {
        switch (backend) {
        case FrameGenerationBackend::Dlss: return dlss;
        case FrameGenerationBackend::XeSS: return xess;
        case FrameGenerationBackend::Fsr: return fsr;
        case FrameGenerationBackend::Fsr4: return fsr4;
        case FrameGenerationBackend::Vfg: return vfg;
        case FrameGenerationBackend::Anr: return anr;
        case FrameGenerationBackend::AmdFrc: return amdFrc;
        case FrameGenerationBackend::Rife: return rife;
        }
        return false;
    }
};
// A fresh profile on an AMD RX 9000 starts on FSR 4 ML, AMD's ML frame
// generation; when the driver's provider does not offer it, the engine switches
// to FSR 3.1 at 2X and says so. Other AMD and Intel GPUs start on FSR 3.1.
constexpr FrameGenerationBackend defaultFrameGenerationBackend(uint32_t vendor, bool rx9000 = false) {
    // Intel Arc gets native XeSS multi-frame generation (driver-paced) by
    // default; AMD RX9000 prefers FSR4 ML, other AMD FSR; NVIDIA DLSS.
    return vendor == 0x1002 && rx9000 ? FrameGenerationBackend::Fsr4
        : vendor == 0x8086 ? FrameGenerationBackend::XeSS
        : vendor == 0x1002 ? FrameGenerationBackend::Fsr : FrameGenerationBackend::Dlss;
}
// Keep usable manual choices. An unavailable saved choice prefers FSR 4 ML where
// it is offered (RX 9000), then the cross-vendor FSR 3.1, then XeSS; never an
// experimental provider.
constexpr FrameGenerationBackend restoredFrameGenerationBackend(
    FrameGenerationBackend saved, FrameGenerationAvailability available) {
    if (available.supports(saved)) return saved;
    if (available.fsr4) return FrameGenerationBackend::Fsr4;
    if (available.fsr) return FrameGenerationBackend::Fsr;
    if (available.xess) return FrameGenerationBackend::XeSS;
    if (available.dlss) return FrameGenerationBackend::Dlss;
    return saved; // No provider: the caller disables the effect with its reason.
}
}
