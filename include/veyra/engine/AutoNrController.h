#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>

namespace veyra::engine {
// Transport-frame budget, completed enhancement P95. No content-rate guesses.
// History resets discard observations, while a one-minute switch cap persists
// until the source/settings session itself changes.
class AutoNrController {
public:
    void reset(int64_t now100ns,unsigned initialLevel=0) {level_=initialLevel<5?initialLevel:0;switches_.clear();lowSince_.reset();next_=now100ns+5000000;lastSwitch_=now100ns-30000000;}
    std::optional<unsigned> observe(int64_t now100ns,std::optional<double> p95Ms,uint64_t samples,double budgetMs) {
        if(now100ns<next_)return {};
        next_=now100ns+5000000;
        while(!switches_.empty()&&now100ns-switches_.front()>=600000000)switches_.pop_front();
        if(!p95Ms||!std::isfinite(*p95Ms)||*p95Ms<=0||samples<8||!std::isfinite(budgetMs)||budgetMs<=0){lowSince_.reset();return {};}
        const bool over=*p95Ms>budgetMs,low=*p95Ms<budgetMs*.7;
        if(low){if(!lowSince_)lowSince_=now100ns;}else lowSince_.reset();
        if(switches_.size()>=4||now100ns-lastSwitch_<30000000)return {};
        if(over&&level_<4)++level_;
        else if(low&&level_>0&&lowSince_&&now100ns-*lowSince_>=30000000)--level_;
        else return {};
        switches_.push_back(now100ns);lastSwitch_=now100ns;lowSince_.reset();
        return level_;
    }
    void discardObservations(int64_t now100ns) {lowSince_.reset();next_=now100ns+10000000;}
    unsigned level()const{return level_;}
    static constexpr unsigned percent(unsigned level) {return level==0?100:level==1?85:level==2?70:level==3?55:40;}
private:
    unsigned level_=0;
    int64_t next_=0,lastSwitch_=0;
    std::optional<int64_t> lowSince_;
    std::deque<int64_t> switches_;
};
}
