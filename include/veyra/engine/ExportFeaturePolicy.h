#pragma once
#include "veyra/engine/EffectChain.h"

namespace veyra::engine {
// The AMD model's admission limit belongs to its INPUT, not the source or
// encoder output. Offline AMD NR uses the largest supported internal extent
// and composes its residual into the original working-resolution image.
constexpr pipeline::NrSizePolicy exportNrSizePolicy(NrRuntime runtime) {
    return currentNrRuntime(runtime)==NrRuntime::LmxxfAmd
        ? pipeline::NrSizePolicy::Realtime : pipeline::NrSizePolicy::Native;
}
inline EnhancementSettings freezeExportSettings(EnhancementSettings settings) {
    // Preserve invalid counts for the caller's validation; normalization must
    // not turn a corrupt settings transaction into an apparently valid one.
    if(settings.nrLayerCount>settings.nrLayers.size()||settings.additionalColorCount>settings.additionalColors.size())return settings;
    settings.nrPolicy=exportNrSizePolicy(settings.nrRuntime);
    for(uint32_t i=0;i<settings.nrLayerCount&&i<settings.nrLayers.size();++i)
        settings.nrLayers[i].sizePolicy=exportNrSizePolicy(settings.nrLayers[i].runtime);
    const auto chain=toChain(settings);fromChain(chain,settings);
    return settings;
}
// XeSS owns Present calls and exposes no encoder input texture in this
// integration. Every other backend writes application-owned graph textures.
constexpr bool exportFrameGenerationSupported(FrameGenerationBackend backend,bool nvidia) {
    if(fsrFrameGeneration(backend))return true;
    return nvidia&&(backend==FrameGenerationBackend::Dlss||backend==FrameGenerationBackend::Vfg);
}
}
