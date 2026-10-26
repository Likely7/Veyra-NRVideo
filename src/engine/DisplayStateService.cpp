#include "veyra/engine/DisplayStateService.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/Log.h"
#include <algorithm>
#include <exception>
#include <format>
#include <string>

namespace veyra::engine {
namespace {
constexpr unsigned kRetries=2;
std::string monitorName(HMONITOR monitor){return std::format("0x{:X}",reinterpret_cast<uintptr_t>(monitor));}
}

DisplayState DisplayStateService::queryDisplayState(HWND window,HMONITOR monitor){
    using Ms=std::chrono::duration<double,std::milli>;
    DisplayState state;
    if(!IsWindow(window)||MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)!=monitor)return state;
    const auto began=Clock::now();
    state.refreshHz=gfx::PresentSink::displayRefreshFps(window);
    const auto refreshed=Clock::now();
    state.vrr=gfx::queryVrr(window);
    const auto vrrDone=Clock::now();
    gfx::PresentSink::DisplayPeakInfo peak;
    state.color.hdr=gfx::PresentSink::queryHdrDisplayActive(window,nullptr,&peak);
    state.color.peakValid=peak.valid;state.color.peakNits=peak.peakNits;
    state.color.fullFrameNits=peak.fullFrameNits;state.color.minNits=peak.minNits;
    const auto finished=Clock::now();
    state.refreshMs=Ms(refreshed-began).count();state.vrrMs=Ms(vrrDone-refreshed).count();state.colorMs=Ms(finished-vrrDone).count();
    if(IsWindow(window)&&MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)==monitor)state.monitor=monitor;
    return state;
}

DisplayStateService::DisplayStateService()
    :DisplayStateService(&DisplayStateService::queryDisplayState,[](HWND window){return MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);}){}
DisplayStateService::DisplayStateService(Query query,MonitorOf monitorOf,std::chrono::milliseconds retryDelay)
    :query_(std::move(query)),monitorOf_(std::move(monitorOf)),retryDelay_(retryDelay){}
DisplayStateService::~DisplayStateService(){
    std::shared_future<DisplayState> running;
    {std::lock_guard lock(mutex_);queue_.clear();running=result_;}
    if(running.valid())running.wait();
}

uint64_t DisplayStateService::invalidate(const char* reason){
    std::lock_guard lock(mutex_);
    ++generation_;
    veyra::log::info("display-query",std::format("invalidate reason={} generation={} (stored states are stale; each monitor in use is queried once more)",reason,generation_));
    return generation_;
}
uint64_t DisplayStateService::generation()const{std::lock_guard lock(mutex_);return generation_;}

bool DisplayStateService::currentLocked(const Entry& entry,Clock::time_point now)const{
    // A failure of this generation waits for its retry, and gives up after kRetries.
    if(entry.failedGeneration==generation_&&entry.failures)return entry.failures>kRetries||now<entry.retryAt;
    return entry.state&&entry.state->generation==generation_;
}

bool DisplayStateService::request(HWND window,const char* reason,bool force){
    if(!window)return false;
    const auto monitor=monitorOf_(window);
    if(!monitor)return false;
    std::lock_guard lock(mutex_);
    collectLocked();
    auto& entry=entries_[monitor];
    const auto now=Clock::now();
    if(!force&&currentLocked(entry,now))return false;
    // This generation's answer for the monitor is already on its way.
    if(running_&&running_->monitor==monitor&&running_->generation==generation_)return false;
    for(auto& job:queue_)if(job.monitor==monitor){job.window=window;job.force=job.force||force;return false;}
    const bool retry=!force&&entry.failedGeneration==generation_&&entry.failures;
    const Job job{window,monitor,generation_,retry?"retry":reason,force};
    if(running_)queue_.push_back(job);
    else startLocked(job);
    return true;
}

void DisplayStateService::startLocked(Job job){
    veyra::log::info("display-query",std::format("query reason={} monitor={} generation={} force={} (background thread; nothing waits for it)",
        job.reason,monitorName(job.monitor),job.generation,job.force));
    try{
        result_=std::async(std::launch::async,[query=query_,job]{return query(job.window,job.monitor);}).share();
        running_=job;
    }catch(const std::exception& error){
        result_={};
        failedLocked(entries_[job.monitor],job,std::format("worker start failed: {}",error.what()));
    }
}

void DisplayStateService::failedLocked(Entry& entry,const Job& job,const std::string& why){
    if(entry.failedGeneration!=job.generation){entry.failedGeneration=job.generation;entry.failures=0;}
    ++entry.failures;entry.retryAt=Clock::now()+retryDelay_;
    veyra::log::warn("display-query",std::format("query failed reason={} monitor={} generation={} attempt={} {} ({})",
        job.reason,monitorName(job.monitor),job.generation,entry.failures,why,
        entry.failures>kRetries?"no more automatic retries in this generation; a display change or the re-check button asks again":"retried on the next request after the delay"));
}

void DisplayStateService::collectLocked(){
    while(running_&&result_.valid()&&result_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        const Job job=*running_;running_.reset();
        const auto finished=std::move(result_);result_={};
        auto& entry=entries_[job.monitor];
        try{
            auto state=finished.get();
            if(state.monitor!=job.monitor)failedLocked(entry,job,"the window closed or moved to another monitor during the query");
            else{
                state.generation=job.generation;state.serial=++serial_;
                if(!entry.state||entry.state->generation<=job.generation)entry.state=state;
                veyra::log::info("display-query",std::format("result reason={} monitor={} generation={} refreshHz={:.3f} vrrLevel={} status={} hdr={} peakNits={} refreshMs={:.3f} vrrMs={:.3f} colorMs={:.3f} (driver wall time on the worker thread)",
                    job.reason,monitorName(job.monitor),job.generation,state.refreshHz,gfx::vrrLevel(state.vrr),state.vrr.status,
                    state.color.hdr?(*state.color.hdr?"on":"off"):"unknown",state.color.peakValid?int(state.color.peakNits):-1,state.refreshMs,state.vrrMs,state.colorMs));
                if(state.vrr.status!=0)failedLocked(entry,job,std::format("NVAPI status={}",state.vrr.status));
                else if(!state.color.hdr.has_value())failedLocked(entry,job,"HDR output description unavailable");
                else if(entry.failedGeneration==job.generation)entry.failures=0;
            }
        }catch(const std::exception& error){failedLocked(entry,job,std::format("exception: {}",error.what()));}
        catch(...){failedLocked(entry,job,"unknown exception");}
        // The next waiting monitor that still lacks this generation's state.
        const auto now=Clock::now();
        while(!running_&&!queue_.empty()){
            Job next=queue_.front();queue_.erase(queue_.begin());
            next.generation=generation_;
            if(!next.force&&currentLocked(entries_[next.monitor],now))continue;
            startLocked(next);
        }
    }
}

std::optional<DisplayState> DisplayStateService::state(HMONITOR monitor){
    std::lock_guard lock(mutex_);
    collectLocked();
    const auto found=entries_.find(monitor);
    return found==entries_.end()?std::nullopt:found->second.state;
}

std::optional<DisplayState> DisplayStateService::wait(HMONITOR monitor,std::chrono::milliseconds limit){
    const auto until=Clock::now()+limit;
    for(;;){
        std::shared_future<DisplayState> running;
        {
            std::lock_guard lock(mutex_);
            collectLocked();
            const auto found=entries_.find(monitor);
            const auto stored=found==entries_.end()?std::nullopt:found->second.state;
            const bool current=stored&&stored->generation==generation_;
            const bool answering=(running_&&running_->monitor==monitor)||
                std::any_of(queue_.begin(),queue_.end(),[monitor](const Job& job){return job.monitor==monitor;});
            if(current||!answering||!result_.valid()||Clock::now()>=until)return stored;
            running=result_;
        }
        // Outside the lock: the UI keeps reading while a session starts.
        if(running.wait_until(until)!=std::future_status::ready){
            std::lock_guard lock(mutex_);
            const auto found=entries_.find(monitor);
            return found==entries_.end()?std::nullopt:found->second.state;
        }
    }
}

bool DisplayStateService::busy(){
    std::lock_guard lock(mutex_);
    collectLocked();
    return running_.has_value()||!queue_.empty();
}
}
