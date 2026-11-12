#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <thread>
#include <stdexcept>

namespace veyra::engine {
// All steps execute on the graph owner. Waiting is represented as a deadline,
// never a sleeping callback; each queued entry retains one batch lease.
class LiveGpuScheduler {
public:
    enum class State { Pending, Complete, Failed };
    // Only a ready frame's presentation deadline warrants a precise wake.
    // Fence/capacity retries must remain event/timer waits, never busy polling.
    struct Step {State state=State::Pending;int64_t wakeAt=0;bool precise=false;};
    using Job=std::function<Step(int64_t)>;
    explicit LiveGpuScheduler():owner_(std::this_thread::get_id()){}
    LiveGpuScheduler(const LiveGpuScheduler&)=delete;
    LiveGpuScheduler& operator=(const LiveGpuScheduler&)=delete;
    ~LiveGpuScheduler(){cancel();}
    bool push(Job job,std::function<void()> finish){
        requireOwner();if(failed_||queue_.size()>=2)return false;
        queue_.push_back({std::move(job),std::move(finish)});return true;
    }
    void advance(int64_t now){
        requireOwner();wakeAt_=0;precise_=false;
        while(!queue_.empty()&&!failed_){
            Step result;
            try{result=queue_.front().job(now);}catch(...){result.state=State::Failed;}
            if(result.state==State::Pending){wakeAt_=result.wakeAt;precise_=result.precise;break;}
            auto entry=std::move(queue_.front());queue_.pop_front();entry.finish();entry.job={};
            if(result.state==State::Failed){failed_=true;cancel();}
        }
    }
    void cancel(){requireOwner();while(!queue_.empty()){auto entry=std::move(queue_.front());queue_.pop_front();entry.finish();}wakeAt_=0;precise_=false;}
    uint32_t occupancy()const{return static_cast<uint32_t>(queue_.size());}
    bool failed()const{return failed_;}
    int64_t wakeAt()const{return wakeAt_;}
    bool preciseWake()const{return precise_;}
private:
    struct Entry {Job job;std::function<void()> finish;};
    void requireOwner()const{if(std::this_thread::get_id()!=owner_)throw std::logic_error("GPU scheduler called outside owner thread");}
    std::thread::id owner_;std::deque<Entry> queue_;bool failed_=false,precise_=false;int64_t wakeAt_=0;
};
}
