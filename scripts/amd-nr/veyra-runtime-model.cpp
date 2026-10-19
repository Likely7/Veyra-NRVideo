// SPDX-License-Identifier: GPL-3.0-only
// Veyra adapter; the linked inference implementation is lmxxf 0.44 (MIT).
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include "native_model_mode.h"
#include "small_model_assets.h"
// Reports the very selector used by the compiled network, rather than a label
// inferred from a filename. The host rejects a swapped/mismatched model module.
extern "C" __declspec(dllexport) uint32_t VeyraLmxxfModelMode() {
    return dlss5_mode::Requested();
}
// The host may use a different CRT. Inspect the same environment copy read by
// the network after LoadFlagsFileOnce, rather than the host's stale getenv().
extern "C" __declspec(dllexport) uint32_t VeyraLmxxfControlsCompatible() {
    for (const auto* key : {"DLSS5_CODEC_SRGB", "DLSS5_VIT_ADAPTIVE",
            "DLSS5_TEMPORAL_MODE", "DLSS5_FAST_HISTORY", "DLSS5_TEMPORAL_HISTORY_EXPERIMENT"}) {
        const auto* value=std::getenv(key);
        if(value&&*value&&std::string_view(value)!="0")return 0;
    }
    return 1;
}
// Use the network's own structural preflight. Without this check upstream can
// silently fall back to the original network when a Small weight is missing.
extern "C" __declspec(dllexport) uint32_t VeyraLmxxfModelAssetsReady(const wchar_t* assets) {
    if (!assets) return 0;
    try {
        return dlss5_mode::Requested() != 0 || hip_reference::SmallAssetsAvailable(
            (std::filesystem::path(assets) / L"small").u8string());
    } catch (...) { return 0; }
}
