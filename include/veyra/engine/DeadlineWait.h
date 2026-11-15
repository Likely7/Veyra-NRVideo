#pragma once
#include <windows.h>
#include <algorithm>
#include <chrono>
#include "veyra/Log.h"
namespace veyra::engine {
class DeadlineWait {
public:
    DeadlineWait(){
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_){const auto code=GetLastError();timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);log::warn("scheduler","high resolution wait timer unavailable; error="+std::to_string(code));}
        else log::info("scheduler","high resolution waitable timer active; no global timer resolution changes");
    }
    ~DeadlineWait(){if(timer_)CloseHandle(timer_);}
    DeadlineWait(const DeadlineWait&)=delete;
    DeadlineWait& operator=(const DeadlineWait&)=delete;
    static int64_t now100ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
    // A bounded tail for an already-ready frame. Return to the owner on a
    // new source/fence/control event, so precision never delays input or stop.
    // Long waits still sleep; this is not used for fence/capacity polling.
    template<class Cancel>
    bool until(int64_t deadline,HANDLE wakeEvent,HANDLE secondEvent,Cancel&& cancelled){
        constexpr int64_t tail100ns=10000; // at most 1 ms of one CPU core
        auto remaining=deadline-now100ns();
        if(remaining<=0||cancelled())return false;
        if(remaining>tail100ns){
            const bool sourceWoke=slice(std::min(1.0,double(remaining-tail100ns)/10000),wakeEvent,secondEvent);
            if(sourceWoke||eventWoke_)return sourceWoke;
            remaining=deadline-now100ns();
            if(remaining>tail100ns)return false;
        }
        unsigned polls=0;
        while(now100ns()<deadline){
            if((polls++&31)==0){
                if(cancelled())return false;
                if(wakeEvent&&WaitForSingleObject(wakeEvent,0)==WAIT_OBJECT_0)return true;
                if(secondEvent&&WaitForSingleObject(secondEvent,0)==WAIT_OBJECT_0)return false;
            }
            YieldProcessor();
        }
        return false;
    }
    // Waits for the timer or, when supplied, an external event (capture
    // sample arrival). Returns true when the event fired first.
    // A second event (GPU fence completion) also ends the wait; only the first
    // event decides the return value.
    bool slice(double milliseconds=1,HANDLE wakeEvent=nullptr,HANDLE secondEvent=nullptr){
        eventWoke_=false;
        if(!timer_){Sleep(1);return false;}
        LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,static_cast<LONGLONG>(std::clamp(milliseconds,0.05,2.0)*10000));
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){log::error("scheduler","SetWaitableTimer error="+std::to_string(GetLastError()));return false;}
        if(wakeEvent&&secondEvent){
            HANDLE handles[3]={wakeEvent,secondEvent,timer_};
            const auto result=WaitForMultipleObjects(3,handles,FALSE,10);
            eventWoke_=result==WAIT_OBJECT_0||result==WAIT_OBJECT_0+1;
            if(result==WAIT_OBJECT_0)return true;
            if(result!=WAIT_OBJECT_0+1&&result!=WAIT_OBJECT_0+2)log::warn("scheduler","wait result="+std::to_string(result));
            return false;
        }
        if(wakeEvent||secondEvent){
            HANDLE handles[2]={wakeEvent?wakeEvent:secondEvent,timer_};
            const auto result=WaitForMultipleObjects(2,handles,FALSE,10);
            eventWoke_=result==WAIT_OBJECT_0;
            if(result==WAIT_OBJECT_0)return wakeEvent!=nullptr;
            if(result!=WAIT_OBJECT_0+1)log::warn("scheduler","wait result="+std::to_string(result));
            return false;
        }
        const auto result=WaitForSingleObject(timer_,10);
        if(result!=WAIT_OBJECT_0)log::warn("scheduler","wait timer result="+std::to_string(result));
        return false;
    }
private:HANDLE timer_=nullptr;bool eventWoke_=false;
};
}
