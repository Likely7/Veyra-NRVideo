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
enum class NvidiaLowLatency : unsigned { Off=0, On=1 };
// Legacy value 2 selected On + Boost; keep the opt-in, but never enable Boost.
constexpr NvidiaLowLatency savedNvidiaLowLatency(int value){return value==1||value==2?NvidiaLowLatency::On:NvidiaLowLatency::Off;}
struct PresentationSettings {
    // Low-latency queue: DXGI maximum frame latency 1 instead of 3.
    // Native NVIDIA Reflex is selected separately below. Reduces queueing,
    // which cannot be measured on this machine - do not claim a figure for it.
    bool enabled=false;
    PacingMode mode=PacingMode::LowQueue;  // deprecated, see above
    DisplaySync display=DisplaySync::Automatic;
    OutputRateMode outputRate=OutputRateMode::Off;
    double customFps=60.0;
    bool fullscreen=false; // transient UI state, never persisted as a preference
    // Global opt-in compatibility choice; never part of enhancement presets.
    bool hdrFgCompatibility=false;
    // 可变刷新 switch: where the monitor can run VRR, Automatic presents like
    // explicit Vsync (G-SYNC/FreeSync + V-SYNC, see presentationVsync below).
    bool variableRefresh=false;
    NvidiaLowLatency nvidiaLowLatency=NvidiaLowLatency::Off;
    bool operator==(const PresentationSettings&) const = default;
    bool valid()const{return unsigned(mode)<=2&&unsigned(display)<=2&&unsigned(outputRate)<=2&&unsigned(nvidiaLowLatency)<=1&&std::isfinite(customFps)&&customFps>=1.0&&customFps<=1000.0;}
};
// Who owns the low queue in the interface: on an NVIDIA adapter the two
// NVIDIA states (开 implies it, 关 clears it); on another adapter the
// plain low-queue switch, and the NVIDIA choice is only kept for the next
// NVIDIA session. Returns whether `enabled` had to change (review 2026-10-29:
// a kept NVIDIA choice used to force the queue on an AMD compute GPU for good).
constexpr bool syncNvidiaLowLatencyQueue(PresentationSettings& p,bool nvidiaAdapter){
    const bool queue=p.nvidiaLowLatency!=NvidiaLowLatency::Off;
    if(!nvidiaAdapter||p.enabled==queue)return false;
    p.enabled=queue;return true;
}
// Automatic never waits for vertical sync and never requests tearing: a
// flip-model Present(0, 0) replaces a frame that has not been shown yet at the
// next refresh, so the newest finished frame is the one shown and nothing
// queues behind the display (games running uncapped in a borderless window).
// Measured 2026-10-13 (claude-latency-20261013, 320 Hz, producer faster than
// the display): the frame on screen was 1.0 refresh old with Present(0,0) and
// 6.9 refreshes old with Present(1,0), whose queue the unwaited latency object
// does not limit. 2.0.6 (ef2069f) had made Automatic identical to Vsync; the
// 4K NR+XeSS capture measured 112 ms on a 60 Hz panel with it.
// Explicit Vsync makes every frame wait for a refresh; only explicit Tearing
// passes DXGI_PRESENT_ALLOW_TEARING. With the 可变刷新 switch on and VRR
// possible on the monitor, the presenter reports Automatic as Vsync in its
// effective settings: inside the VRR range the panel starts a refresh when
// the frame arrives, so the wait is about zero and nothing tears; above the
// range the one-deep VSync queue holds (Microsoft documents VRR through the
// tearing flag, which only explicit Tearing may use).
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
