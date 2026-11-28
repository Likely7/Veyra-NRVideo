#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace veyra::engine {
// Observed readiness of earlier pairs bounds the phase of the next pair.
// Values are host-clock 100 ns; source PTS differences only supply spacing.
class LivePairLatency {
    struct Sample { int64_t time, delay; };
    static constexpr size_t kMaxSamples=64;
    std::deque<Sample> samples_;
    int64_t selected_=0;
public:
    // Beyond the 95th percentile of readiness; LiveArrivalSmoother::kLead100ns
    // takes part of it back on callback-anchored capture.
    static constexpr int64_t kMargin100ns=5000;
    void reset(){samples_.clear();selected_=0;}
    void observe(int64_t now,int64_t requiredDelay){
        if(requiredDelay<=0||requiredDelay>20000000)return;
        while(!samples_.empty()&&(samples_.size()>=kMaxSamples||samples_.front().time<now-10000000))samples_.pop_front();
        samples_.push_back({now,requiredDelay});
    }
    // Apply the floor from the actual pair's deadline geometry before
    // deciding if the same pair fits. Keep the original allowance cap.
    int64_t enforceAdmissionFloor(int64_t need,int64_t limit,std::optional<int64_t> transientPhase={}){
        const auto phase=std::min(std::max(transientPhase.value_or(selected_),need),limit);
        if(!transientPhase)selected_=phase;
        return phase;
    }
    int64_t select(int64_t now,int64_t legacyDelay,int64_t interval,unsigned multiplier,int64_t samplingAllowance=0,
                   bool slewCapturePhase=false,bool rememberPhase=true){
        if(interval<=0||multiplier<2){if(rememberPhase)selected_=0;return legacyDelay;}
        std::array<int64_t,kMaxSamples> current;
        size_t count=0;
        for(const auto& sample:samples_)if(sample.time>=now-10000000)current[count++]=sample.delay;
        const auto step = slewCapturePhase ? std::max<int64_t>(2500, interval / multiplier / 32) :
            std::min<int64_t>(5000, interval / multiplier / 8);
        // A static map can change the pair interval in one step. Allow an
        // existing phase to descend without advancing a deadline by a slot.
        const auto limit = slewCapturePhase ? std::max(legacyDelay + samplingAllowance, selected_ - step) :
            legacyDelay + samplingAllowance;
        // A gap in readiness observations is expected when pairs are skipped.
        // Retain the last phase until enough new samples arrive.
        auto phase=std::min(selected_?selected_:legacyDelay,limit);
        if(count>=8){
            const auto index=(count*95+99)/100-1;
            std::nth_element(current.begin(),current.begin()+index,current.begin()+count);
            // 0.5 ms covers wake-up jitter with MMCSS. The allowance lets
            // readiness move past the legacy delay for deduplicated capture
            // and when interpolation needs extra time.
            const auto target=std::min(limit,current[index]+kMargin100ns);
            phase=std::clamp(selected_?selected_-step:legacyDelay,target,limit);
        }
        if(rememberPhase)selected_=phase;
        return phase;
    }
};
// Extra time on a capture-card FG pair's deadlines, on top of the learned phase,
// so that 99 of 100 generated frames are ready by their deadline, never more than
// 1 ms (user, 2026-11-13: automatic, up to 1 ms). The phase sits at the 95th
// percentile of readiness once the callback anchor's lead has taken its margin
// back, so one generated frame in twenty was still in the GPU at its deadline,
// 0.1-0.4 ms late on T911 4K60 NR + DLSS/VFG 2X, and those set the 1% low. This
// learns how much later the late ones are: it rises quickly (0.1 ms a quarter
// second) when frames miss and gives latency back more slowly (0.04 ms) when they
// do not. The first 2 s after a start learn nothing: feature warmup reached 0.9 ms
// there and took 6 s to come back down (T911 run, 2026-11-13). A sample is the slack the frame would have had without the extra; the
// caller passes only frames whose readiness it observed by the deadline, or that
// it saw unfinished after it, never ones it simply looked at late.
class LiveDeadlineMargin {
    struct Sample { int64_t time, slack; };
    std::deque<Sample> samples_;
    int64_t extra_=kStart100ns,nextUpdate_=0,start_=0;
public:
    static constexpr int64_t kStart100ns=2500,kMax100ns=10000,kGuard100ns=500,kRise100ns=1000,kFall100ns=400;
    static constexpr int64_t kUpdate100ns=2500000,kFloor100ns=-20000,kAge100ns=40000000,kWarmup100ns=20000000;
    static constexpr size_t kSamples=240,kMinSamples=100;
    void reset(){samples_.clear();extra_=kStart100ns;nextUpdate_=0;start_=0;}
    int64_t extra()const{return extra_;}
    // deadline already includes `extra`, the margin this pair was given.
    void observe(int64_t now,int64_t deadline,int64_t ready,int64_t extra){
        if(ready<=0||deadline<=0)return;
        if(!start_)start_=now;
        if(now<start_+kWarmup100ns)return;
        // One stall cannot outweigh the rest: no frame counts as more than 2 ms late.
        const auto slack=std::max(kFloor100ns,deadline-extra-ready);
        while(!samples_.empty()&&(samples_.size()>=kSamples||samples_.front().time<now-kAge100ns))samples_.pop_front();
        samples_.push_back({now,slack});
        if(samples_.size()<kMinSamples||now<nextUpdate_)return;
        nextUpdate_=now+kUpdate100ns;
        std::vector<int64_t> values;values.reserve(samples_.size());
        for(const auto& sample:samples_)values.push_back(sample.slack);
        const auto index=values.size()/100;
        std::nth_element(values.begin(),values.begin()+std::ptrdiff_t(index),values.end());
        const auto target=std::clamp(kGuard100ns-values[index],int64_t(0),kMax100ns);
        extra_=target>extra_?std::min(target,extra_+kRise100ns):std::max(target,extra_-kFall100ns);
    }
};
// A capture pair whose FG the budget rejects still shows its real frame at the
// FG phase, so FG can resume without a jump. After a second of nothing but
// rejections that phase only costs latency: VFG High 3X at 4K on an RTX 5070
// (2026-11-13, 30 fps game) had 98% of its pairs rejected and showed 30 fps
// 38-46 ms after the callback, ~2 ms without FG. A held pair shows its real frame
// as soon as it is ready. A pair admitted while held (a budget probe) stays held:
// its generated frames are then past their deadlines and expire unshown, while
// the budget still measures its cost. FG resumes on its phase only after more
// pairs in a row are admitted than one budget probe runs (FgCostSettling: 2
// warmup groups plus up to 4 measured ones); with 3 every probe ended the hold
// on that 3X run, a second on the FG phase every 3-4 s and 1% low 26.8.
// Entering and leaving are one uneven interval each.
class LiveFgRejectionHold {
    int64_t rejectedSince_=0;
    unsigned admittedRun_=0;
    bool held_=false;
public:
    static constexpr int64_t kHold100ns=10000000;
    static constexpr unsigned kResumePairs=8;
    void reset(){*this=LiveFgRejectionHold{};}
    bool held()const{return held_;}
    // One call per capture pair after its FG decision; true: show this pair's
    // real frame without the FG phase.
    bool observe(int64_t now,bool admitted){
        if(admitted){
            rejectedSince_=0;
            if(held_&&++admittedRun_<kResumePairs)return true;
            held_=false;admittedRun_=0;
            return false;
        }
        admittedRun_=0;
        if(!rejectedSince_)rejectedSince_=now;
        if(now-rejectedSince_>=kHold100ns)held_=true;
        return held_;
    }
};
}
