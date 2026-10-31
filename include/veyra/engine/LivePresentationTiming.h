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
// How late a live generated frame is still worth showing: 10 ms, the bound the
// FG budget admits against. Near the refresh a dropped frame leaves a refresh
// repeating the last picture, so it stays useful until the next frame is due
// (60 Hz 2X: 16.2 ms). Never later: the real frame must not wait behind it.
inline constexpr int64_t kLiveGeneratedGrace100ns=100000,kLiveNextFrameMargin100ns=5000;
inline int64_t liveGeneratedGrace100ns(bool outputNearRefresh,int64_t outputInterval100ns){
    return outputNearRefresh?std::max(kLiveGeneratedGrace100ns,outputInterval100ns-kLiveNextFrameMargin100ns):kLiveGeneratedGrace100ns;
}
// One present of display capacity (explicit VSync): when a generated frame
// stops waiting for it. Above the refresh the queue is always full, so waiting
// only moves it into the scheduler and holds the real frame behind (DLSS 6X
// replay on 320 Hz: callback->present P95 18.4 -> 34.2 ms): half the interval,
// at most 2 ms. When the output fits, capacity comes back within a refresh and
// no backlog can build; giving up after 2 ms there dropped 3-4% of the generated
// frames of a 60 Hz 2X stream in bursts every 16.5 s (field log 2026-10-31), so
// it waits until the next frame is due.
inline bool liveGeneratedCapacityExpired(bool outputFitsRefresh,double waitedMs,double outputIntervalMs,int64_t now,int64_t nextDue){
    if(waitedMs<=std::min(2.0,outputIntervalMs/2))return false;
    return !outputFitsRefresh||now>=nextDue-kLiveNextFrameMargin100ns;
}
}
