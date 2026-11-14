#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>
namespace veyra::engine {
// Completion events, not source arrivals or generated-frame multipliers.
// Call under the owning controller's mutex. All times use monotonic 100 ns.
class FrameRateWindow {
public:
    void reset(int64_t now){start_=now;times_.clear();}
    void complete(int64_t now){
        times_.push_back(now);
        while(times_.size()>8192||(!times_.empty()&&times_.front()<=now-window))times_.pop_front();
    }
    double rate(int64_t now)const{
        const auto elapsed=(std::min)(window,(std::max<int64_t>)(0,now-start_));
        if(elapsed<1000000)return 0;
        const auto first=std::upper_bound(times_.begin(),times_.end(),now-window);
        return double(times_.end()-first)*10000000.0/double(elapsed);
    }
private:
    static constexpr int64_t window=10000000;
    int64_t start_=0;
    std::deque<int64_t> times_;
};
// The "1% low" a frame-time overlay (RTSS, PresentMon) reports for an
// application: the 99th-percentile interval between consecutive presents over
// the last ten seconds, as a frame rate. Like those tools it spaces Present
// calls by their start (FrameCompletionRates::presented paceAt), the same events
// as the submit rate, not panel scanout. A gap over half
// a second is a pause or a seek rather than a slow frame: it is not counted and
// the next present starts a new run.
class FramePaceWindow {
public:
    void reset(){last_=0;intervals_.clear();}
    void complete(int64_t now){
        if(last_>0&&now>last_&&now-last_<=kGap)intervals_.push_back({now,now-last_});
        last_=now;
        while(intervals_.size()>16384||(!intervals_.empty()&&intervals_.front().first<=now-kWindow))intervals_.pop_front();
    }
    std::optional<double> lowFps(int64_t now)const{
        std::vector<int64_t> recent;recent.reserve(intervals_.size());
        for(const auto& entry:intervals_)if(entry.first>now-kWindow)recent.push_back(entry.second);
        if(recent.size()<kMinIntervals)return {};
        const size_t at=size_t(std::ceil(0.99*double(recent.size())))-1;
        std::nth_element(recent.begin(),recent.begin()+std::ptrdiff_t(at),recent.end());
        if(recent[at]<=0)return {};
        return 10000000.0/double(recent[at]);
    }
    static constexpr int64_t kWindow=100000000,kGap=5000000;
    static constexpr size_t kMinIntervals=100;
private:
    int64_t last_=0;
    std::deque<std::pair<int64_t,int64_t>> intervals_; // present time, interval since the previous one
};
}
