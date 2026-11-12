#pragma once
#include "veyra/engine/DeadlineWait.h"
#include <vector>

inline int deadlineWaitChecks(){
    using veyra::engine::DeadlineWait;
    DeadlineWait wait;int failures=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    const auto source=CreateEventW(nullptr,FALSE,FALSE,nullptr),gpu=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    check(source&&gpu,"create private scheduler wake events");
    if(!source||!gpu){if(source)CloseHandle(source);if(gpu)CloseHandle(gpu);return 1;}
    unsigned cancelled=0;
    wait.until(DeadlineWait::now100ns()+1000000,source,gpu,[&]{++cancelled;return true;});
    check(cancelled==1,"stop cancels before arming or spinning");
    SetEvent(source);
    check(wait.until(DeadlineWait::now100ns()+1000000,source,gpu,[]{return false;}),"source arrival interrupts timer phase");
    SetEvent(gpu);cancelled=0;
    check(!wait.until(DeadlineWait::now100ns()+15000,source,gpu,[&]{++cancelled;return false;})&&cancelled==1&&WaitForSingleObject(gpu,0)==WAIT_TIMEOUT,
        "GPU completion ends timer phase without entering spin tail");
    SetEvent(source);
    check(wait.until(DeadlineWait::now100ns()+10000,source,gpu,[]{return false;}),"source arrival interrupts spin tail");
    SetEvent(gpu);
    check(!wait.until(DeadlineWait::now100ns()+10000,source,gpu,[]{return false;})&&WaitForSingleObject(gpu,0)==WAIT_TIMEOUT,"GPU completion interrupts spin tail");
    cancelled=0;
    wait.until(DeadlineWait::now100ns()+10000,nullptr,nullptr,[&]{return ++cancelled>=2;});
    check(cancelled==2,"stop is rechecked inside spin tail");
    for(bool precise:{false,true}){
        std::vector<double> late;bool ordered=true;
        for(unsigned n=0;n<96;++n){
            const auto due=DeadlineWait::now100ns()+83333;
            while(DeadlineWait::now100ns()<due){
                if(precise)wait.until(due,nullptr,nullptr,[]{return false;});
                else wait.slice(std::min(1.0,double(due-DeadlineWait::now100ns())/10000));
            }
            const auto now=DeadlineWait::now100ns();ordered&=now>=due;late.push_back(double(now-due)/10000);
        }
        std::sort(late.begin(),late.end());
        std::cout<<"TIMER precise="<<precise<<" samples="<<late.size()<<" p50Ms="<<late[47]<<" p99Ms="<<late[95]<<'\n';
        check(ordered,"elapsed deadlines do not submit frames early");
    }
    CloseHandle(source);CloseHandle(gpu);return failures?1:0;
}
