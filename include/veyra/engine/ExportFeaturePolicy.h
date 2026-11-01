#pragma once
#include "veyra/engine/EffectChain.h"

namespace veyra::engine {
// Adapted from the user-supplied 2_EXPORT_NR_PRESET_SIZE_2026-10-25 patch;
// manual percentages/invalid-value preservation are local compatibility work.
// Video export runs NR at the same preset extent as the preview and composes
// its residual into the original working-resolution image, so an 8K export
// does not feed the NR model a native 8K frame. Auto timing is preview-only:
// an export snapshot fixes it at 1440p. The AMD model's admission limit
// belongs to its INPUT: presets above 1080p stay bounded at 1080p.
constexpr pipeline::NrSizePolicy exportNrSizePolicy(NrRuntime runtime,pipeline::NrSizePolicy preset) {
    // Invalid settings must reach validation unchanged. Manual percentages
    // retain their encoded value; the extent calculation applies AMD's input
    // budget BEFORE multiplying, just as the preview does.
    if(!pipeline::validNrSizePolicy(preset)||pipeline::manualNrSizePolicy(preset))return preset;
    if(preset==pipeline::NrSizePolicy::Auto)preset=pipeline::NrSizePolicy::P1440;
    if(currentNrRuntime(runtime)==NrRuntime::LmxxfAmd){
        const auto limit=pipeline::nrHeightLimit(preset);
        if(limit==0||limit>1080)return pipeline::NrSizePolicy::Realtime;
    }
    return preset;
}
inline pipeline::Extent exportNrProcessingExtent(pipeline::Extent input,NrRuntime runtime,pipeline::NrSizePolicy preset){
    return pipeline::ResolutionPlan::make(input,false,exportNrSizePolicy(runtime,preset),false,0,
        pipeline::SrTarget::Uhd4K,false,currentNrRuntime(runtime)==NrRuntime::LmxxfAmd?1080u:0u).nr;
}
inline EnhancementSettings freezeExportSettings(EnhancementSettings settings) {
    // Preserve invalid counts for the caller's validation; normalization must
    // not turn a corrupt settings transaction into an apparently valid one.
    if(settings.nrLayerCount>settings.nrLayers.size()||settings.additionalColorCount>settings.additionalColors.size()||
       settings.additionalAntiAliasCount>settings.additionalAntiAlias.size()||
       settings.additionalBeautyCount>settings.additionalBeauty.size())return settings;
    settings.nrPolicy=exportNrSizePolicy(settings.nrRuntime,settings.nrPolicy);
    for(uint32_t i=0;i<settings.nrLayerCount&&i<settings.nrLayers.size();++i)
        settings.nrLayers[i].sizePolicy=exportNrSizePolicy(settings.nrLayers[i].runtime,settings.nrLayers[i].sizePolicy);
    // The beauty mask view is a diagnostic overlay of the preview, never part of a file.
    settings.beauty.showMask=false;
    for(auto& beauty:settings.additionalBeauty)beauty.showMask=false;
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
