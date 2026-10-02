#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <iostream>
using namespace std::chrono_literals;
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const std::filesystem::path output=argv[2];
    std::filesystem::create_directories(output);
    veyra::Logger::instance().openFile((output/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"NR runtime switching test",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;
    int failures=0;
    {
        veyra::engine::EngineController engine;
        engine.setVolume(0,true);
        auto wait=[&](auto predicate,int seconds=15) {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
            while(std::chrono::steady_clock::now()<end) {
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                auto s=engine.snapshot();if(s.failed)return false;
                if(predicate(s))return true;
                std::this_thread::sleep_for(10ms);
            }
            return false;
        };
        auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;return ok;};
        engine.open(window,argv[1],{});
        check(wait([](const auto& s){return s.frames>30&&!s.nrActive&&!s.srActive&&!s.fgActive&&s.nrEvaluated==0&&s.generated==0;}),"fresh defaults play without enhancement");
        int cycle=0;
        for(const auto runtime:{veyra::engine::NrRuntime::Original,veyra::engine::NrRuntime::Ampere,veyra::engine::NrRuntime::NvidiaOriginal,veyra::engine::NrRuntime::Original}) {
            if(failures)break;
            auto settings=engine.snapshot().desired;settings.nr=true;settings.nrRuntime=runtime;
            if(!check(engine.requestSettings(settings),"request runtime switch"))break;
            const auto revision=engine.snapshot().desired.revision;
            if(!check(wait([&](const auto& s){return !s.applying&&s.applied.revision==revision&&s.applied.nrRuntime==runtime&&s.nrActive&&s.nrEvaluated>10;},25),"selected runtime produces NR output"))break;
            const auto path=output/(L"runtime-"+std::to_wstring(cycle++)+L".png");
            engine.saveFrame(path.wstring());
            check(wait([&](const auto&){return std::filesystem::exists(path)&&std::filesystem::file_size(path)>1024;}),"actual processed output saved");
        }
        if(!failures) {
            auto settings=engine.snapshot().desired;settings.nr=false;settings.sr=false;settings.multiplier=1;
            check(engine.requestSettings(settings),"disable all enhancements after switching");
            const auto revision=engine.snapshot().desired.revision;
            check(wait([&](const auto& s){return !s.applying&&s.applied.revision==revision&&!s.nrActive&&!s.srActive&&!s.fgActive;}),"plain playback restored in same process");
        }
        engine.stop();
        check(wait([&](const auto&){return engine.idle();}),"GPU drained and engine stopped");
    }
    DestroyWindow(window);CoUninitialize();
    std::cout<<"RTX30 hardware performance and visual quality NOT TESTED\n";
    return failures?1:0;
}
