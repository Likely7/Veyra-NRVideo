#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace veyra::engine {
// Observed readiness of earlier pairs bounds the phase of the next pair.
// Values are host-clock 100 ns; source PTS differences only supply spacing.
class LivePairLatency {
    struct Sample { int64_t time, delay; };
    std::deque<Sample> samples_;
    int64_t selected_=0;
public:
    void reset(){samples_.clear();selected_=0;}
    void observe(int64_t now,int64_t requiredDelay){
        if(requiredDelay<=0||requiredDelay>20000000)return;
        while(!samples_.empty()&&(samples_.size()>=64||samples_.front().time<now-10000000))samples_.pop_front();
        samples_.push_back({now,requiredDelay});
    }
    int64_t select(int64_t now,int64_t legacyDelay,int64_t interval,unsigned multiplier,int64_t samplingAllowance=0){
        std::vector<int64_t> current;
        for(const auto& sample:samples_)if(sample.time>=now-10000000)current.push_back(sample.delay);
        if(current.size()<8||interval<=0||multiplier<2){selected_=legacyDelay;return selected_;}
        const auto index=(current.size()*95+99)/100-1;
        std::nth_element(current.begin(),current.begin()+index,current.end());
        // 0.5 ms covers wake-up jitter with MMCSS. The allowance is only used
        // by deduplicated capture, where a later sample can move readiness
        // past a deadline on the continuous content clock.
        const auto limit=legacyDelay+samplingAllowance;
        const auto target=std::min(limit,current[index]+5000);
        const auto step=std::min<int64_t>(5000,interval/multiplier/8);
        selected_=std::clamp(selected_?selected_-step:legacyDelay,target,limit);
        return selected_;
    }
};
}
