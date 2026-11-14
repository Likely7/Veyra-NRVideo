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
// File replay has no arrival clock and keeps its PTS anchor; FG keeps the A/B
// pair phase; streams re-anchor every frame. Physical and screen capture without
// FG present when ready: one PTS anchor per epoch turned the first sample's late
// read into a hold on every later frame (60 fps window capture: 3-52 ms per run).
inline bool livePacesSourcePts(bool fg,bool physicalCapture,bool screenCapture){return fg||!(physicalCapture||screenCapture);}
// Live capture never waits on an absolute source PTS. Once B is available,
// display generated(A,B), then B up to half an input interval later. Anchor
// each pair to current host time, not to the first (possibly stale) sample.
inline double livePairHoldMs(bool hasGenerated,double realPtsMs,double generatedPtsMs) {
    if(!hasGenerated||!std::isfinite(realPtsMs)||!std::isfinite(generatedPtsMs))return 0;
    return std::clamp(realPtsMs-generatedPtsMs,0.0,1000.0/30.0);
}
// Presentation anchor of a live FG pair. A capture callback jitters where the
// picture does not: field log 2026-10-31 (4K30 on USB) has callbacks with sd
// 1.9-2.6 ms against device PTS steady to the microsecond, 800 ppm off the host
// clock. Anchored to its own callback each pair carries that jitter to the panel,
// where a real frame and the next generated frame then share a refresh and one
// of them is replaced: at 59.94 fps on 60.005 Hz 3.5% of all frames, nearly all
// of them real. The anchor is PTS plus a tracked offset to the callback, second
// order so that a clock difference in either direction leaves no lag: its mean
// stays on the mean callback, so it moves single frames and never adds delay.
// Capture-to-panel latency then keeps its mean and loses tail. The latency this
// application reports starts at the callback and would read 0.6 ms higher at P95
// only because its start point jitters; kLead100ns keeps it where it was, for
// 0.5% more late generated frames (reports/claude-fg-sidefx-20261101/sim_final).
// On capture cards the lead cancels the 0.5 ms readiness margin of LivePairLatency
// (it learns readiness against the callback); LiveDeadlineMargin adds back what the
// generated frames actually need there.
// One callback beyond min(interval/3, 8 ms) does not move the line; a second on
// the same side moves it there at once. The FG budget, generated-frame expiry
// stays tied to the callback. Deduplicated capture also learns GPU readiness on the content clock.
class LiveArrivalSmoother {
    double offset_=0,drift_=0,jitter_=0,outside_=0;
    unsigned samples_=0;
    uint64_t snaps_=0;
public:
    static constexpr double kGain=.1,kDriftGain=kGain*kGain/4;
    static constexpr int64_t kLead100ns=5000;
    void reset(){offset_=drift_=outside_=0;samples_=0;}
    int64_t anchor(int64_t source,int64_t callback,int64_t interval){
        const double measured=double(callback-source),error=measured-offset_;
        const bool outside=samples_&&std::abs(error)>double(std::min<int64_t>(interval/3,80000));
        if(outside&&error*outside_<=0)outside_=error;
        else if(outside||!samples_){
            snaps_+=samples_>0;offset_=measured;outside_=0;samples_=1;
        }else{
            outside_=0;
            // A running mean until it has as many samples as the gain averages.
            samples_=std::min(samples_+1,10u);
            if(samples_<10)offset_+=error/samples_;
            else{offset_+=kGain*error+drift_;drift_+=kDriftGain*error;}
            jitter_+=.02*(std::abs(error)-jitter_);
        }
        return source+std::llround(offset_)-kLead100ns;
    }
    // Reuse the capture clock offset for content timestamps. Center the
    // one-slot sampling uncertainty so a late sample can still reach its first deadline.
    int64_t contentAnchor(int64_t contentPts,int64_t samplePts,int64_t callback,int64_t transportInterval){
        return anchor(samplePts,callback,transportInterval)+(contentPts-samplePts)+transportInterval/2;
    }
    // Diagnostics: mean distance of a callback from the line, the clock difference, jumps.
    double jitterMs()const{return jitter_/10000;}
    double driftPpm(int64_t interval)const{return interval>0?drift_/double(interval)*1e6:0;}
    uint64_t snaps()const{return snaps_;}
};
// How late a live generated frame is still worth showing: 10 ms, the bound the
// FG budget admits against, from the later of its smoothed due time and the one
// its own callback gives. A late callback must not expire more frames than it did.
inline constexpr int64_t kLiveGeneratedGrace100ns=100000;
inline int64_t liveGeneratedGrace100ns(int64_t callback,int64_t anchor){return kLiveGeneratedGrace100ns+std::max<int64_t>(0,callback-anchor);}
// Live VFG 2X: a generated frame is ready base + model time after the callback,
// its real frame is due an interval + base after it, and the panel shows one of
// them per refresh. With model time above half the interval they are closer than
// a refresh at some phases of the 16.5 s beat, the real frame replaces the generated
// one and the picture falls to 30 fps for seconds (60 Hz, model 23 ms: 28-31% of all
// frames on a real panel, 2026-11-01). Waiting for the next refresh would cost the
// real frame up to a refresh. Instead such a pair runs the next faster model.
inline bool liveFramesShareRefresh(int64_t generated,int64_t real,int64_t lastVblank,double period,double latch){
    if(period<=0)return false;
    const auto refresh=[&](int64_t present){return std::ceil((double(present-lastVblank)+latch)/period);};
    return refresh(generated)>=refresh(real);
}
// Explicit VSync: how long a generated frame waits for display capacity with a
// smoothed anchor, half its interval being the other limit. The flip it takes can
// cost the real frame a whole refresh, so the wait falls with the interval. 60 Hz
// 2X model: 2 ms keeps 0.6% more frames but holds real frames 0.7 ms longer on
// average than 1 ms. 320 Hz A/B: 1 ms loses 3-6% of the presents for nothing at
// P99, and 2 ms shows no growth against the callback anchor.
// Near 60 Hz the frames this limit loses come from callback jitter, and that differs
// between cards by two orders: sd 0.01-0.06 ms on MCS T911 and KUHAIMI G50, 1.87 ms
// on KUHAIMI 4K60 Version at 4K30 (field logs, 2026-11). So there the wait follows
// the jitter the smoother measures, 1 ms + 2.5 sd (its mean distance is 0.8 sd), to
// at most 3 ms. Real 60 Hz replay with injected callback jitter reduced missing
// generated-frame submissions in the short A/B runs. Submission counts and a
// fitted refresh grid do not establish physical display loss or panel latency;
// see docs/VSYNC_JITTER_WAIT_2026-11-02.md for evidence and measurement limits.
inline constexpr double kLiveJitterWaitMinIntervalMs=12.5,kLiveJitterWaitMaxMs=3;
inline double liveSmoothedCapacityWaitMs(double outputIntervalMs,double callbackJitterMs=0){
    const double base=std::clamp(2-(outputIntervalMs-5)/12,1.0,2.0);
    return outputIntervalMs<kLiveJitterWaitMinIntervalMs?base:std::clamp(1+3.125*callbackJitterMs,base,kLiveJitterWaitMaxMs);
}
}
