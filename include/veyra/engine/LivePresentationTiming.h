#pragma once
#include <algorithm>
#include <cmath>
#include "veyra/pipeline/FramePacket.h"

namespace veyra::engine {
inline int64_t liveSourceInterval100ns(pipeline::Rational duration,double nominalFps){
    if(!duration.isUnknown()&&duration.den>0&&duration.num>0)return int64_t(std::clamp(static_cast<long double>(duration.num)*10000000.L/duration.den,10000.L,1000000.L)+.5L);
    if(std::isfinite(nominalFps)&&nominalFps>=10&&nominalFps<=1000)return int64_t(std::llround(10000000.0/nominalFps));
    return 200000; // documented last-resort 50 Hz estimate, never a measured duration
}
// Compositor frames have jittery PTS intervals even at a fixed capture limit.
// Use that limit for the pair's presentation phase, not for its actual A/B PTS.
// Otherwise alternating 40/20 ms packets move the next pair before the last B.
inline int64_t livePhaseInterval100ns(pipeline::Rational duration,double nominalFps,bool compositor){
    return liveSourceInterval100ns(compositor?pipeline::Rational{}:duration,nominalFps);
}
// Variable-refresh capture often retains the negotiated maximum-rate sample
// duration. The actual A/B source span controls interpolation, not that cap.
// Never use a stale/reset predecessor to establish the new phase.
inline int64_t capturePairInterval100ns(pipeline::Rational duration,double nominalFps,
                                      double previousPtsMs,double ptsMs,bool reset){
    const double span=ptsMs-previousPtsMs;
    if(!reset&&std::isfinite(span)&&span>=1&&span<=100)return int64_t(std::llround(span*10000));
    return liveSourceInterval100ns(duration,nominalFps);
}
// Live capture never waits on an absolute source PTS. Once B is available,
// display generated(A,B), then B up to half an input interval later. Anchor
// each pair to current host time, not to the first (possibly stale) sample.
inline double livePairHoldMs(bool hasGenerated,double realPtsMs,double generatedPtsMs) {
    if(!hasGenerated||!std::isfinite(realPtsMs)||!std::isfinite(generatedPtsMs))return 0;
    return std::clamp(realPtsMs-generatedPtsMs,0.0,1000.0/30.0);
}
}
