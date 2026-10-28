#pragma once
#include "veyra/engine/EnhancementSettings.h"
namespace veyra::engine {
// Atomic settings edit: never submit a transient 8K NR/FG graph while the user
// is selecting an 8K SR target before selecting its smaller output.
inline bool boundSrStageOutput(EnhancementSettings& s,pipeline::Extent source){
    if(!s.sr||!srStageBackend(s.videoSrQuality)||(!s.nr&&s.multiplier<=1))return false;
    if(!source.valid())source={3840,2160};
    const auto p=pipeline::srStageExtents(source,s.srTarget,s.srPrescale,s.srOutput);
    if(!pipeline::unsafeSrStageOutput(source,p.input,p.evaluated,p.output,true))return false;
    s.srOutput=pipeline::SrOutput::Uhd4K;
    return true;
}
}
