#include "veyra/engine/AsyncDisplayQuery.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/NvidiaVrr.h"
#include <future>
#include <iostream>
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
    std::cout<<"ASYNC_DISPLAY_QUERY failures="<<failures<<std::endl;
    return failures?1:0;
}
