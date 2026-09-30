#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace veyra::engine {
// Retained only so saved settings keep their field layout. Nothing reads it
// any more: the three pacing "modes" measured identical on throughput,
// per-second spread and media-clock deviation (2026-09-22, b13/pacing-ab),
// because file playback already presents within 0.8 ms of media time.
enum class PacingMode : unsigned { LowQueue, Even, Reflex };
enum class DisplaySync : unsigned { Tearing, Vsync, Automatic };
enum class OutputRateMode : unsigned { Off, FollowDisplay, Custom };
struct PresentationSettings {
    // Low-latency queue: DXGI maximum frame latency 1 instead of 3, plus
    // Reflex when frame generation is off. Reduces submit-to-scanout queueing,
    // which cannot be measured on this machine - do not claim a figure for it.
    bool enabled=false;
    PacingMode mode=PacingMode::LowQueue;  // deprecated, see above
    DisplaySync display=DisplaySync::Automatic;
    OutputRateMode outputRate=OutputRateMode::Off;
    double customFps=60.0;
    bool fullscreen=false; // transient UI state, never persisted as a preference
    bool operator==(const PresentationSettings&) const = default;
    bool valid()const{return unsigned(mode)<=2&&unsigned(display)<=2&&unsigned(outputRate)<=2&&std::isfinite(customFps)&&customFps>=1.0&&customFps<=1000.0;}
};
// Automatic never waits for vertical sync and never requests tearing: a
// flip-model Present(0, 0) replaces the queued frame at the next refresh.
// Tearing is only requested when the user explicitly selects it, because a
// borderless fullscreen window is promoted to independent flip, where
// DXGI_PRESENT_ALLOW_TEARING really tears (windowed output is composed).
constexpr bool presentationVsync(const PresentationSettings& s) {
    return s.display==DisplaySync::Vsync;
}
constexpr bool presentationTearing(const PresentationSettings& s) {
    return s.display==DisplaySync::Tearing;
}
// Media time remains the authority. Spacing adds at most one interval from
// the current decision; stale generated frames are discarded by the caller.
class PresentationCadence {
    int64_t last_=0,rateAnchor_=0;
public:
    void reset(){last_=0;rateAnchor_=0;}
    int64_t due(int64_t mediaDeadline,int64_t outputInterval,unsigned catchUpPercent=10)const{
        if(!last_)return mediaDeadline;
        // Bounded catch-up absorbs wakeup/Present jitter without moving the
        // media grid forward every frame. File MFG allows 20% recovery;
        // other callers retain 10%. No caller can remove the spacing bound.
        const auto recovery=(outputInterval/100)*std::min(catchUpPercent,20u)
            +(outputInterval%100)*std::min(catchUpPercent,20u)/100;
        return std::max(mediaDeadline,last_+outputInterval-recovery);
    }
    void submitted(int64_t now){last_=now;}
    // The output cap runs on an absolute grid, not on "last actual present +
    // interval": chaining off the actual time let jitter accumulate, so a
    // 100 FPS cap measured 97.4 presents/s on a 100 Hz panel and a few
    // refreshes per second showed a repeat (2026-09-22, b17/cap-ab).
    int64_t rateDue(int64_t now,int64_t interval)const{return !rateAnchor_||interval<=0?now:rateAnchor_+interval;}
    void rateSubmitted(int64_t now,int64_t interval){
        // Re-anchor after a gap (seek, stall, reset); otherwise advance by whole
        // intervals so the long-run average stays exactly at the cap.
        if(interval<=0){rateAnchor_=0;return;}
        rateAnchor_=(!rateAnchor_||now-rateAnchor_>2*interval||now<rateAnchor_)?now:rateAnchor_+interval;
    }
    bool rateSkipsCandidate(int64_t now,int64_t mediaDeadline,int64_t candidateInterval,int64_t capInterval)const{
        // Discard only while the cap is still ahead of the next opportunity.
        // An expired media deadline must never prevent a ready frame recovering.
        return rateDue(now,capInterval)>=std::max(now,mediaDeadline)+candidateInterval;
    }
};
}
