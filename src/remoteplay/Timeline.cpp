// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Timeline.h"
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <bit>
namespace veyra::remoteplay {
Sequence16Extender::Result Sequence16Extender::observe(std::uint16_t n) noexcept {
    if(!initialized_){initialized_=true;previous_=n;extended_=n;return {extended_,true,true,0};}
    const auto delta=static_cast<std::uint16_t>(n-previous_);
    if(delta==0 || delta>=0x8000u)return {extended_,false,false,0};
    if(extended_>std::numeric_limits<std::uint64_t>::max()-delta)return {};
    extended_+=delta;previous_=n;return {extended_,true,false,static_cast<std::uint32_t>(delta-1)};
}
namespace {
std::optional<std::int64_t> rescale(std::uint64_t n,std::uint32_t den) noexcept {
    if(den==0)return {};
    constexpr auto max=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto whole=n/den,rem=n%den;
    if(whole>max/10000000u)return {};
    const auto a=whole*10000000u,b=rem*10000000u/den;
    if(a>max-b) return {};
    return static_cast<std::int64_t>(a+b);
}
}
void RemotePlayClock::reset(HostTime origin,std::uint32_t fps) {
    if(origin<0 || (fps!=30&&fps!=60))throw std::invalid_argument("Invalid local clock origin/rate");
    *this={};origin_=origin;fps_=fps;
}
std::optional<EstimatedStamp> RemotePlayClock::video(std::uint64_t index,HostTime arrival) {
    if(!fps_ || arrival<origin_)return {};
    if(anchored_ && (index<=lastIndex_ || arrival<lastArrival_))return {};
    constexpr HostTime stableWindow = 10000000; // 1 s for ordinary oscillator drift.
    constexpr HostTime slowWindow = 2000000;    // 200 ms to observe slower source cadence.
    const auto step = 10000000 / static_cast<HostTime>(fps_);
    const auto fineSlew = step / 200;
    const auto samples = fps_ / 5;
    bool gap=false;
    if(!anchored_){videoAnchor_=arrival-origin_;firstIndex_=index;anchored_=true;}
    else if(arrival-lastArrival_>slowWindow || index-lastIndex_>fps_/2u){
        videoAnchor_=arrival-origin_;firstIndex_=index;gap=true;
        phaseWindow_.clear();phase_=0;recentSlowIntervals_=0;
    } else {
        const auto excess = arrival - lastArrival_ - step;
        // A dropped index or a large one-frame delay is not evidence of a
        // slower console cadence. Look for modest extra time across frames.
        const bool slower = index == lastIndex_ + 1 && excess > fineSlew && excess <= step / 2;
        recentSlowIntervals_ = ((recentSlowIntervals_ << 1) | uint32_t(slower)) & ((1u << samples) - 1u);
    }
    const auto delta=rescale(index-firstIndex_,fps_);
    if(!delta || videoAnchor_>std::numeric_limits<std::int64_t>::max()-*delta)return {};
    const auto nominal=videoAnchor_+*delta;
    const auto error=(arrival-origin_)-nominal;
    while(!phaseWindow_.empty()&&phaseWindow_.front().arrival<=arrival-stableWindow)phaseWindow_.pop_front();
    while(!phaseWindow_.empty()&&phaseWindow_.back().error>=error)phaseWindow_.pop_back();
    phaseWindow_.push_back({arrival,error});
    auto correction = std::clamp(phaseWindow_.front().error - phase_, -fineSlew, fineSlew);
    if (error - phase_ > step / 4 && std::popcount(recentSlowIntervals_) >= int(samples / 2)) {
        // The deque is ordered by arrival and increasing error. Its first
        // point in the shorter window is that window's lower envelope.
        const auto recent = std::upper_bound(phaseWindow_.begin(), phaseWindow_.end(), arrival - slowWindow,
            [](HostTime cutoff, const PhaseSample& sample) { return cutoff < sample.arrival; });
        const auto lag = recent->error - phase_;
        if (lag > step / 4) correction = std::min(lag / 2, step / 2);
    }
    phase_ += correction;
    if(phase_>0&&nominal>std::numeric_limits<std::int64_t>::max()-phase_)return {};
    auto pts=nominal+phase_;
    if(index!=firstIndex_&&!gap)pts=std::max(pts,lastPts_+1);
    lastArrival_=arrival;lastIndex_=index;lastPts_=pts;
    return EstimatedStamp{pts,step,gap};
}
std::optional<std::int64_t> RemotePlayClock::audioPts(std::uint64_t first,std::uint32_t rate,HostTime start) const noexcept {
    if(!fps_ || start<origin_ || rate<8000 || rate>192000)return {};
    const auto delta=rescale(first,rate);const auto base=start-origin_;
    if(!delta || base>std::numeric_limits<std::int64_t>::max()-*delta) return {};
    return base+*delta;
}
} // namespace veyra::remoteplay
