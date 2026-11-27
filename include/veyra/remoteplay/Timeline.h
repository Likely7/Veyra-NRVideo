// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Types.h"
#include <deque>
namespace veyra::remoteplay {
class Sequence16Extender {
public:
    struct Result {std::uint64_t value=0;bool accepted=false,first=false;std::uint32_t gap=0;};
    Result observe(std::uint16_t) noexcept;
    void reset() noexcept {*this={};}
private:
    bool initialized_=false;std::uint16_t previous_=0;std::uint64_t extended_=0;
};
struct EstimatedStamp {
    std::int64_t pts100ns=0,duration100ns=0;
    bool discontinuity=false;
    TimestampProvenance provenance=TimestampProvenance::LocalEstimated;
};
// A shared local origin for audio and video, NOT independent zero-at-first-frame clocks.
// Source index gives cadence, host arrival provides an anchor. Reset after network
// discontinuity; consumer must advance its epoch and reset temporal GPU history.
class RemotePlayClock {
public:
    void reset(HostTime commonOrigin,std::uint32_t fps);
    std::optional<EstimatedStamp> video(std::uint64_t index,HostTime arrival);
    std::optional<std::int64_t> audioPts(std::uint64_t firstSample,std::uint32_t rate,HostTime audioStart) const noexcept;
private:
    HostTime origin_=0,videoAnchor_=0,lastArrival_=0;
    // Bounded lower-envelope PLL: nominal profile FPS is not a source clock
    // (a 60 profile can deliver ~59.94). Filter network jitter, slew only the
    // locally estimated timestamps, and never accumulate nominal-rate error.
    struct PhaseSample {HostTime arrival;std::int64_t error;};
    std::deque<PhaseSample> phaseWindow_;
    std::int64_t phase_=0,lastPts_=0;
    std::uint64_t firstIndex_=0,lastIndex_=0;
    std::uint32_t fps_=0, recentSlowIntervals_=0;
    bool anchored_=false;
};
} // namespace veyra::remoteplay
