// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace veyra::ui {
// Hardware gates are independent of packaging and runtime initialization. A
// qualifying adapter still needs the component files and a successful backend.
enum class HardwareEffect { Nvidia, NvidiaNr50, NvidiaNrSf, Vfg, AmdNr, Fsr4, CrossVendor };
struct EffectGpu {
    uint32_t vendor = 0;
    bool rtx = false, blackwell = false, ada = false, rx9000 = false;
    uint32_t deviceId = 0;
};
constexpr bool hardwareSupports(HardwareEffect effect, EffectGpu gpu) {
    switch (effect) {
    case HardwareEffect::Nvidia: return gpu.vendor == 0x10DE && gpu.rtx;
    case HardwareEffect::NvidiaNr50: return gpu.vendor == 0x10DE && gpu.blackwell;
    case HardwareEffect::NvidiaNrSf: return gpu.vendor == 0x10DE && gpu.rtx;
    case HardwareEffect::Vfg: return gpu.vendor == 0x10DE && (gpu.ada || gpu.blackwell);
    case HardwareEffect::AmdNr: case HardwareEffect::Fsr4: return gpu.vendor == 0x1002 && gpu.rx9000;
    case HardwareEffect::CrossVendor: return gpu.vendor == 0x10DE || gpu.vendor == 0x1002 || gpu.vendor == 0x8086;
    }
    return false;
}
}
