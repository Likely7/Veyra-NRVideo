#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace veyra::engine {
// Revision-scoped costs survive temporal-history resets. Old GPU pressure
// expires by wall time, rather than requiring successful FG to replace it.
class FgRecoveryBudget {
    struct Sample {int64_t time;double ms;};
    std::deque<Sample> base_,fg_,warmup_;
    bool limited_=false;
    unsigned recoveryPairs_=0;
    bool recordAdmission(bool fits){
        if(!fits){limited_=true;recoveryPairs_=0;return false;}
        if(limited_&&++recoveryPairs_>=2){limited_=false;recoveryPairs_=0;}
        return true;
    }
    static void add(std::deque<Sample>& values,int64_t now,double ms){
        if(!std::isfinite(ms)||ms<0)return;
        while(!values.empty()&&(values.size()>=64||values.front().time<now-10000000))values.pop_front();
        values.push_back({now,ms});
    }
    static std::optional<double> p95(const std::deque<Sample>& values,int64_t now,int64_t age){
        std::vector<double> current;
        for(const auto& sample:values)if(sample.time>=now-age)current.push_back(sample.ms);
        if(current.empty())return {};
        const auto index=(current.size()*95+99)/100-1;
        std::nth_element(current.begin(),current.begin()+index,current.end());return current[index];
    }
public:
    void reset(){base_.clear();fg_.clear();warmup_.clear();limited_=false;recoveryPairs_=0;}
    void fgCost(double ms,int64_t now){add(fg_,now,ms);}
    // fgCostComparable=false marks a group whose FG stage did less work than a
    // full group (a reduced 2X group): its FG time must not lower the
    // full-group FG estimate. Its BASE cost is still recorded - the base work
    // is per source frame and does not depend on the multiplier. Excluding the
    // base cost too starved the model: with ~40% of groups reduced, base_ and
    // fg_ only ever saw the expensive full groups, the estimate stayed high,
    // more groups were rejected, and more reductions followed (measured
    // 2026-09-22: full admissions halved, 550 -> 348, net -10% presented).
    void complete(std::optional<double> measuredMs,bool evaluated,bool warmup,int64_t now,std::optional<double> measuredFg={},bool fgCostComparable=true){
        // Missing GPU timestamps are unknown, not CPU polling delay. Let old
        // samples expire; admission still checks each batch's real deadline.
        if(warmup){if(evaluated&&measuredMs)add(warmup_,now,*measuredMs);return;}
        if(evaluated&&measuredFg&&fgCostComparable)fgCost(*measuredFg,now);
        if(!measuredMs)return;
        const double ms=*measuredMs;
        const auto extra=evaluated?(measuredFg?measuredFg:p95(fg_,now,20000000)):std::optional<double>(0);
        // If a generated batch lacks a GPU timing, retain its whole measured
        // completion as conservative base cost; never assume free FG.
        add(base_,now,std::max(0.0,ms-extra.value_or(0)));
    }
    std::optional<double> predicted(int64_t now,bool warmingHistory=false)const{
        // Use a measured whole warmup, never divide MFG cost by its multiplier.
        // Until measured, retain the conservative steady-work estimate.
        if(warmingHistory)if(auto cost=p95(warmup_,now,10000000))return cost;
        const auto base=baseCost(now);
        if(!base)return {};
        return *base+p95(fg_,now,20000000).value_or(0);
    }
    std::optional<double> baseCost(int64_t now)const{return p95(base_,now,10000000);}
    // A/B interpolation needs B first; enhancement of B is additional work.
    // Schedule its measured base cost before the subframe cadence, bounded to
    // one source interval. Sample before submitting this pair, never at ready.
    int64_t processingAllowance(int64_t now,int64_t interval)const{
        return int64_t(std::min(double(std::max<int64_t>(0,interval)),baseCost(now).value_or(0)*10000));
    }
    // Capture pairs (LivePairLatency): how far the learned phase may go past
    // interval + base cost. A generated frame also waits for its own
    // interpolation: VFG High 2X at 4K is 16 ms on an RTX 5070, more than half a
    // 30 fps interval, and 60->30 pairs capped at the base cost showed 36-55% of
    // their generated frames late (2026-11-13). Only while base + FG fit in one
    // source interval: a group the GPU cannot sustain must not earn later
    // deadlines, and with them more admissions.
    int64_t phaseAllowance(int64_t now,int64_t interval)const{
        const auto base=baseCost(now);const auto fg=p95(fg_,now,20000000);
        if(!base||!fg||interval<=0||(*base+*fg)*10000>double(interval))return 0;
        return int64_t(*fg*10000);
    }
    bool canAdmit(int64_t now,int64_t deadline,double elapsed,double present,bool warmingHistory=false,double queuedMs=0)const{
        const auto cost=predicted(now,warmingHistory);
        // FG has not been submitted at admission. CPU time can at most cover
        // the base work; it must never erase the cost of future FG calls.
        const double queued=std::isfinite(queuedMs)?std::max(0.0,queuedMs):0.0;
        const double progress=std::clamp(elapsed,0.0,queued+baseCost(now).value_or(0));
        const bool fits=double(deadline-now)/10000+10>=std::max(0.0,queued+cost.value_or(0)-progress)+std::max(0.0,present);
        return fits;
    }
    bool admit(int64_t now,int64_t deadline,double elapsed,double present,bool warmingHistory=false,double queuedMs=0){
        return recordAdmission(canAdmit(now,deadline,elapsed,present,warmingHistory,queuedMs));
    }
    // strictFirstOutput=false reproduces the 1.4.0 rule: only the whole group
    // against its last deadline. true additionally requires the first output to
    // reach its own deadline; see EnhancementSettings::fgStrictAdmission.
    bool admitFile(int64_t now,int64_t firstDeadline,int64_t lastDeadline,int64_t outputInterval,
                   double elapsed,double present,std::optional<double> firstFgMs,bool warmingHistory,double queuedMs,
                   bool strictFirstOutput=true){
        const auto whole=predicted(now,warmingHistory),base=baseCost(now);
        if(warmingHistory||!whole||!base||!firstFgMs||!std::isfinite(*firstFgMs)||*firstFgMs<0)
            return admit(now,lastDeadline,elapsed,present,warmingHistory,queuedMs);
        const double queued=std::isfinite(queuedMs)?std::max(0.0,queuedMs):0.0;
        const double progress=std::clamp(elapsed,0.0,queued+*base);
        const double blit=std::max(0.0,present),grace=double(std::max<int64_t>(0,outputInterval))/10000;
        const auto fits=[&](int64_t deadline,double cost){
            return double(deadline-now)/10000+grace>=std::max(0.0,queued+cost-progress)+blit;
        };
        // The first output must be reachable too: the last deadline alone can
        // admit a whole MFG group whose early outputs are already doomed.
        return recordAdmission((!strictFirstOutput||fits(firstDeadline,*base+*firstFgMs))&&fits(lastDeadline,*whole));
    }
    // Reduced-group affordability: base work plus ONE measured first
    // interpolation must reach the pair midpoint. Does not touch recovery
    // state; the caller decides between Reduced and Seed/Skip.
    bool canAdmitReduced(int64_t now,int64_t midpointDeadline,int64_t outputInterval,double elapsed,double present,std::optional<double> firstFgMs,double queuedMs)const{
        const auto base=baseCost(now);
        if(!base||!firstFgMs||!std::isfinite(*firstFgMs)||*firstFgMs<0)return false;
        const double queued=std::isfinite(queuedMs)?std::max(0.0,queuedMs):0.0;
        const double progress=std::clamp(elapsed,0.0,queued+*base);
        const double grace=double(std::max<int64_t>(0,outputInterval))/10000;
        return double(midpointDeadline-now)/10000+grace>=std::max(0.0,queued+*base+*firstFgMs-progress)+std::max(0.0,present);
    }
    bool recovering()const{return limited_&&recoveryPairs_>0;}
};
// Live VFG keeps its A/B history across skipped pairs, so the evaluation that
// ends a long skip is no DLSS-style warmup, and it is not a steady sample either.
// Field log 2026-10-31 (RTX 5060 Ti, 4K30 VFG High): 23.1-23.6 ms running
// continuously, 28.5 ms windowed and 39.6 ms fullscreen after 2 s or more of
// skipping, while the budget allowed about 25.9 ms. Each lone probe measured over
// budget and rejected the next pair for 2 s: 30 fps for up to 76 s, until a
// settings change. After a second without an evaluation the next two are warmup
// samples, so they raise neither the FG nor the base estimate. One skipped pair
// changes nothing. A mode that really is too slow is rejected by the group after
// them, so a probe costs three groups where it cost one: failed probes retry
// after 2, 4, then every 8 s, where the budget alone would retry every 2 s.
class FgCostSettling {
    int64_t lastEvaluated_=0,holdUntil_=0;
    unsigned left_=0,recorded_=0,failures_=0;
    bool probing_=false;
public:
    static constexpr int64_t kIdle100ns=10000000,kRetry100ns=20000000;
    static constexpr unsigned kEvaluations=2,kProbeGroups=4,kMaxDoublings=2;
    void reset(){*this=FgCostSettling{};}
    // Call for each completed group in order; true marks a warmup sample.
    bool observe(bool skipped,bool evaluated,int64_t now){
        if(skipped){
            if(probing_){probing_=false;left_=0;holdUntil_=now+(kRetry100ns<<failures_);failures_=std::min(failures_+1,kMaxDoublings);}
            return false;
        }
        if(!evaluated)return false;
        const bool idle=lastEvaluated_&&now-lastEvaluated_>=kIdle100ns;
        lastEvaluated_=now;
        if(idle){left_=kEvaluations;recorded_=0;probing_=true;}
        if(left_){--left_;return true;}
        if(probing_&&++recorded_>=kProbeGroups){probing_=false;failures_=0;}
        return false;
    }
    bool holding(int64_t now)const{return now<holdUntil_;}
    int64_t holdUntil()const{return holdUntil_;}
};
}
