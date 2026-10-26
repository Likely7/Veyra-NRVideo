#include "veyra/engine/AsyncDisplayQuery.h"
#include "veyra/engine/DisplayStateService.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/NvidiaVrr.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using Clock=std::chrono::steady_clock;
    using veyra::engine::AsyncDisplayQuery;
    // Read-only field probe: no device/queue/swapchain/capture initialization.
    if(argc==3&&std::wstring(argv[1])==L"--probe"){
        const auto requested=std::stoull(argv[2]);
        const auto window=requested?reinterpret_cast<HWND>(requested):GetDesktopWindow();
        if(!IsWindow(window))return 2;
        for(unsigned i=0;i<8;++i){
            const auto start=Clock::now();
            const auto hz=veyra::gfx::PresentSink::displayRefreshFps(window);
            const auto refresh=Clock::now();
            const auto vrr=veyra::gfx::queryVrr(window);
            const auto finish=Clock::now();
            std::cout<<"DISPLAY_QUERY_PROBE refreshMs="<<std::chrono::duration<double,std::milli>(refresh-start).count()
                <<" vrrMs="<<std::chrono::duration<double,std::milli>(finish-refresh).count()<<" hz="<<hz<<" vrrLevel="<<veyra::gfx::vrrLevel(vrr)<<std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return 0;
    }
    int failures=0;auto check=[&](bool ok,const char* why){std::cout<<(ok?"PASS ":"FAIL ")<<why<<std::endl;failures+=!ok;};
    AsyncDisplayQuery<int> query;
    check(!query.poll()&&!query.pending(),"empty cache has no fabricated display state");
    std::promise<void> entered,release;
    auto wait=release.get_future().share();const auto owner=std::this_thread::get_id();
    check(query.request([&]{entered.set_value();wait.wait();return std::this_thread::get_id()!=owner?320:-1;}),"one background query accepted");
    entered.get_future().wait();
    bool nonblocking=true;
    for(unsigned i=0;i<1000;++i)nonblocking&=!query.poll()&&!query.request([]{return -2;});
    check(nonblocking&&query.pending(),"blocked driver query cannot block owner polls or enqueue a second worker");
    release.set_value();
    const auto take=[&]{std::optional<int> result;const auto until=Clock::now()+std::chrono::seconds(2);while(query.pending()&&Clock::now()<until){result=query.poll();if(result)break;std::this_thread::yield();}return result;};
    check(take()==320&&!query.pending(),"completed result published once from another thread");
    check(!query.poll(),"a result cannot be consumed twice");
    std::promise<void> staleEntered,staleRelease;auto staleWait=staleRelease.get_future().share();
    query.request([&]{staleEntered.set_value();staleWait.wait();return 60;});staleEntered.get_future().wait();
    query.invalidate();query.invalidate();
    check(!query.poll()&&!query.request([]{return -3;}),"close/reopen invalidates without joining or spawning more work");
    staleRelease.set_value();check(!take()&&!query.pending(),"old monitor/window generation never updates replacement");
    query.request([]{return 144;});check(take()==144,"fresh monitor query recovers after discarded result");
    query.request([]()->int{throw std::runtime_error("simulated driver query failure");});
    bool caught=false;try{take();}catch(const std::runtime_error&){caught=true;}
    check(caught&&!query.pending(),"query failure leaves no stuck pending future");
    query.request([]{return 120;});check(take()==120,"query can retry after driver exception");

    // DisplayStateService (2026-10-25): shared states, queried on events only.
    {
        using veyra::engine::DisplayState;using veyra::engine::DisplayStateService;
        const auto windowA=reinterpret_cast<HWND>(uintptr_t(0x10)),windowB=reinterpret_cast<HWND>(uintptr_t(0x20));
        const auto monitorA=reinterpret_cast<HMONITOR>(uintptr_t(0x1000)),monitorB=reinterpret_cast<HMONITOR>(uintptr_t(0x2000));
        std::atomic<unsigned> calls{0};std::atomic<int> status{0};std::atomic<bool> moved{false},raise{false};
        std::mutex gateMutex;std::condition_variable gateWake;bool gateOpen=true;
        auto setGate=[&](bool open){{std::lock_guard lock(gateMutex);gateOpen=open;}gateWake.notify_all();};
        auto fakeQuery=[&](HWND window,HMONITOR monitor){
            ++calls;
            {std::unique_lock lock(gateMutex);gateWake.wait(lock,[&]{return gateOpen;});}
            if(raise.load())throw std::runtime_error("simulated NVAPI crash");
            DisplayState state;state.monitor=moved.load()?nullptr:monitor;state.refreshHz=window==windowA?144.0:60.0;
            state.vrr.known=true;state.vrr.capsKnown=state.vrr.supportsVrr=state.vrr.currentlyCapable=true;state.vrr.status=status.load();
            // Windows HDR on monitor A only; the panel peak comes from the same answer.
            state.color.hdr=window==windowA;state.color.peakValid=true;state.color.peakNits=window==windowA?1000:400;
            return state;
        };
        auto monitorOf=[&](HWND window){return window==windowA?monitorA:window==windowB?monitorB:HMONITOR(nullptr);};
        {
            DisplayStateService service(fakeQuery,monitorOf,std::chrono::milliseconds(50));
            auto settle=[&](HMONITOR monitor){
                const auto until=Clock::now()+std::chrono::seconds(2);
                while(service.busy()&&Clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return service.state(monitor);
            };
            // The worker counts a call when it starts, which may trail request().
            auto waitCalls=[&](unsigned n){
                const auto until=Clock::now()+std::chrono::seconds(2);
                while(calls<n&&Clock::now()<until)std::this_thread::yield();
                return calls==n;
            };
            check(!service.state(monitorA)&&!service.busy(),"no display state is fabricated before the first answer");
            check(service.request(windowA,"test"),"the first request starts one query");
            auto a=settle(monitorA);
            check(a&&a->refreshHz==144.0&&a->generation==service.generation()&&calls==1,"the answer is stored for its monitor in the current generation");
            check(a&&a->color.hdr==true&&a->color.peakValid&&a->color.peakNits==1000,"Windows HDR and the panel peak arrive with the same answer");
            bool quiet=true;for(unsigned i=0;i<1000;++i)quiet&=!service.request(windowA,"test");
            check(quiet&&calls==1&&!service.busy(),"requests every frame/tick never query a current monitor again");

            const auto first=a->serial;setGate(false);
            const auto generation=service.invalidate("test");
            check(service.request(windowA,"display-change"),"a display change queries the monitor once more");
            const auto start=Clock::now();bool held=true;
            for(unsigned i=0;i<1000;++i)held&=!service.request(windowA,"test");
            const bool nonblocking=Clock::now()-start<std::chrono::milliseconds(500);
            const auto stale=service.state(monitorA);
            check(held&&nonblocking&&service.busy()&&waitCalls(2),"a blocked driver call never blocks or duplicates requests");
            check(stale&&stale->serial==first&&stale->generation<generation,"readers keep the stale state until the new answer");
            check(service.request(windowB,"monitor-change")&&calls==2,"another monitor waits in the queue behind the running query");
            setGate(true);
            auto fresh=settle(monitorA);auto b=service.state(monitorB);
            check(fresh&&fresh->generation==generation&&fresh->serial!=first&&b&&b->refreshHz==60.0&&calls==3,"both monitors answered once each after the release");
            check(b&&b->color.hdr==false&&b->color.peakNits==400,"each monitor keeps its own HDR state");

            check(service.request(windowA,"vrr-confirm",true)&&!service.request(windowA,"vrr-confirm",true),"a forced check runs once even while current");
            settle(monitorA);check(calls==4,"the forced check queried exactly once");

            status=-5;service.invalidate("test");
            check(service.request(windowA,"test"),"query with a failing driver call");
            auto failed=settle(monitorA);
            check(failed&&failed->vrr.status==-5&&!service.request(windowA,"test"),"an NVAPI error is stored and the retry waits for its delay");
            unsigned retries=0;
            for(unsigned i=0;i<4;++i){std::this_thread::sleep_for(std::chrono::milliseconds(70));if(service.request(windowA,"test")){++retries;settle(monitorA);}}
            check(retries==2&&calls==7,"two automatic retries per generation, then none");
            status=0;service.invalidate("test");
            check(service.request(windowA,"test"),"a new generation asks again after the failures");
            auto recovered=settle(monitorA);
            check(recovered&&recovered->vrr.status==0&&!service.request(windowA,"test"),"a good answer ends the retries");

            moved=true;service.invalidate("test");service.request(windowA,"test");
            auto discarded=settle(monitorA);
            check(discarded&&discarded->serial==recovered->serial,"a window that moved during the query never stores that answer");
            moved=false;std::this_thread::sleep_for(std::chrono::milliseconds(70));
            check(service.request(windowA,"test")&&settle(monitorA)->serial!=recovered->serial,"the moved-window answer is retried after the delay");

            raise=true;service.invalidate("test");service.request(windowA,"test");settle(monitorA);
            check(!service.busy(),"a query exception leaves nothing running or queued");
            raise=false;

            setGate(false);service.invalidate("test");service.request(windowA,"session-start");
            const auto waitStart=Clock::now();
            auto timedOut=service.wait(monitorA,std::chrono::milliseconds(40));
            const auto waited=Clock::now()-waitStart;
            check(timedOut&&timedOut->generation<service.generation()&&waited>=std::chrono::milliseconds(35)&&waited<std::chrono::milliseconds(1000),
                "session start waits at most its limit and then uses the stored state");
            std::jthread opener([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));setGate(true);});
            auto answered=service.wait(monitorA,std::chrono::milliseconds(1000));
            check(answered&&answered->generation==service.generation(),"session start uses the answer that arrives within the limit");
            const auto quick=Clock::now();
            auto notAnswering=service.wait(monitorB,std::chrono::milliseconds(500));
            check(notAnswering&&notAnswering->generation<service.generation()&&Clock::now()-quick<std::chrono::milliseconds(100),
                "waiting without a query for the monitor returns at once");
        }
        // Destruction waits for the running driver call, never for the queue.
        setGate(false);
        std::jthread opener([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));setGate(true);});
        const auto closing=Clock::now();
        {DisplayStateService service(fakeQuery,monitorOf,std::chrono::milliseconds(50));service.request(windowA,"test");service.request(windowB,"test");}
        check(Clock::now()-closing<std::chrono::seconds(2),"destruction returns after the running query, without starting queued ones");
    }
    std::cout<<"ASYNC_DISPLAY_QUERY failures="<<failures<<std::endl;
    return failures?1:0;
}
