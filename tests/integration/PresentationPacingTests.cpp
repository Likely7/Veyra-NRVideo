#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/ScreenCaptureSource.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include "DeadlineWaitChecks.h"
static HWND movingInput(){
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraPacingInput";
    wc.lpfnWndProc=[](HWND w,UINT m,WPARAM a,LPARAM b)->LRESULT{
        if(m==WM_TIMER){InvalidateRect(w,nullptr,FALSE);return 0;}
        if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));const LONG x=LONG(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/1000%2400/4);RECT bar{x,0,x+40,r.bottom};FillRect(dc,&bar,(HBRUSH)GetStockObject(WHITE_BRUSH));EndPaint(w,&ps);return 0;}
        return DefWindowProcW(w,m,a,b);
    };
    RegisterClassW(&wc);
    auto w=CreateWindowW(wc.lpszClassName,L"Moving capture input",WS_OVERLAPPEDWINDOW|WS_VISIBLE,1400,50,660,420,nullptr,nullptr,wc.hInstance,nullptr);
    if(w)SetTimer(w,1,16,nullptr);
    return w;
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;using Clock=std::chrono::steady_clock;
    if(argc==2&&std::wstring(argv[1])==L"--deadline-wait")return deadlineWaitChecks();
    if(argc==2&&std::wstring(argv[1])==L"--capture-input"){
        auto w=movingInput();const auto end=Clock::now()+std::chrono::seconds(240);
        // A chosen capture rate needs a window that changes faster than the
        // limit: the 16 ms message timer alone delivers about 30 fps under it.
        const bool repaint=GetEnvironmentVariableW(L"VEYRA_TEST_SCREEN_FPS",nullptr,0)>0;
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        while(IsWindow(w)&&Clock::now()<end){
            MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
            if(repaint&&IsWindow(w)){InvalidateRect(w,nullptr,FALSE);UpdateWindow(w);}
            LARGE_INTEGER due;due.QuadPart=-20000;
            if(timer&&SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,10);
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(timer)CloseHandle(timer);
        if(IsWindow(w))DestroyWindow(w);return w?0:2;
    }
    // media, evidence directory, mode(-1=off), display(0..2), multiplier, seconds, lifecycle/replay/xess
    if(argc<7)return 2;
    const int mode=_wtoi(argv[3]),display=_wtoi(argv[4]),multiplier=_wtoi(argv[5]),seconds=std::clamp(_wtoi(argv[6]),3,120);
    if(mode< -1||mode>2||display<0||display>2)return 2;
    const std::wstring scenario=argc>7?argv[7]:L"";
    // latency-screen[-sr|-vfg]: a live window capture, 60 fps unless
    // VEYRA_TEST_SCREEN_FPS says otherwise (near-refresh A/B: 53 fps x 6 = 318
    // on 320 Hz), for capture-to-present A/B runs. mode -1 off, 0 low queue
    // only, 1 NVIDIA 开.
    const bool latency=scenario.starts_with(L"latency-");
    if((latency||scenario.starts_with(L"native-"))&&mode>1)return 2;
    const bool screen=scenario==L"screen-xess"||scenario==L"screen-dlss"||scenario==L"screen-vfg"||latency;
    const bool xess=scenario==L"xess"||scenario==L"screen-xess"||scenario==L"native-xess";
    if(scenario==L"legacy"){if(mode!=-1)return 2;SetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_SWAPCHAIN",L"1");}
    std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",screen&&!latency?L"0":L"1");
    veyra::Logger::instance().openFile((dir/L"engine.log").wstring());veyra::Logger::instance().setConsoleEnabled(false);
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    const bool physical=scenario==L"capture-nr"||scenario==L"capture-nr-recovery";
    if(physical&&(multiplier<2||multiplier>6))return 2;
    std::wstring sourcePath=argv[1];
    if(physical){
        const auto devices=veyra::source::CaptureCardSource::deviceDetails();
        const auto device=std::find_if(devices.begin(),devices.end(),[](const auto& d){return d.name==L"VC-007PRO";});
        if(device==devices.end()){std::cout<<"FAIL VC-007PRO missing"<<std::endl;return 3;}
        const auto formats=veyra::source::CaptureCardSource::formatsByPath(device->path);
        const auto format=std::find_if(formats.begin(),formats.end(),[&](const auto& f){return f.width==3840&&f.height==2160&&std::abs(f.fps-30)<.01&&f.label.find(L"NV12")!=std::wstring::npos;});
        if(format==formats.end()){std::cout<<"FAIL exact 3840x2160 30fps NV12 format missing"<<std::endl;return 3;}
        const auto audios=veyra::source::CaptureCardSource::deviceDetails(true);
        const auto audio=std::find_if(audios.begin(),audios.end(),[](const auto& a){return a.wasapi&&a.name.find(L"HDMI (VC-007PRO)")!=std::wstring::npos;});
        const int audioMode=audio!=audios.end()?veyra::source::kCaptureAudioWasapi:veyra::source::kCaptureAudioDisabled;
        sourcePath=veyra::source::CaptureCardSource::makeCapturePath(unsigned(device-devices.begin()),*device,format->index,audioMode,audio!=audios.end()?&*audio:nullptr);
        std::cout<<"capture=VC-007PRO input=3840x2160 fps="<<format->fps<<" inputFormat=NV12 formatIndex="<<format->index<<" audioMode="<<audioMode<<" NR=realtime1080 FG=DLSS"<<multiplier<<"X SR=off"<<std::endl;
    }
    HWND window=CreateWindowExW(0,L"STATIC",L"Veyra pacing acceptance",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,1280,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;
    HWND inputWindow=nullptr;
    PROCESS_INFORMATION inputProcess{};
    if(screen){
        std::wstring command=L"\""+std::filesystem::absolute(argv[0]).wstring()+L"\" --capture-input";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&inputProcess))return 2;
        CloseHandle(inputProcess.hThread);
        for(unsigned attempt=0;attempt<500&&!inputWindow;++attempt){
            for(const auto& target:veyra::source::ScreenCaptureSource::targets(veyra::source::ScreenTargetKind::Window))
                if(target.processId==inputProcess.dwProcessId)inputWindow=reinterpret_cast<HWND>(target.handle);
            if(!inputWindow)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(!inputWindow){TerminateProcess(inputProcess.hProcess,2);WaitForSingleObject(inputProcess.hProcess,1000);CloseHandle(inputProcess.hProcess);return 2;}
        veyra::source::ScreenCaptureOptions input;input.target=reinterpret_cast<uint64_t>(inputWindow);input.fps=30;if(latency){wchar_t fps[16]{};input.fps=GetEnvironmentVariableW(L"VEYRA_TEST_SCREEN_FPS",fps,16)?std::clamp(_wtoi(fps),10,240):60;}input.cursor=false;
        sourcePath=input.uri();
    }
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    DEVMODEW dm{};dm.dmSize=sizeof(dm);EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&dm);
    std::cout<<"display="<<dm.dmPelsWidth<<"x"<<dm.dmPelsHeight<<" refreshHz="<<dm.dmDisplayFrequency<<std::endl;
    int failures=0;auto check=[&](bool value,const char* name){std::cout<<(value?"PASS ":"FAIL ")<<name<<std::endl;failures+=!value;};
    {
        EngineController engine;engine.setVolume(0,true);
        PresentationSettings p{mode>=0,PacingMode(std::max(0,mode)),DisplaySync(display)};engine.requestPresentation(p);
        const bool native=scenario.starts_with(L"native-");
        if(native){p.nvidiaLowLatency=mode<0?NvidiaLowLatency::Off:NvidiaLowLatency::On;engine.requestPresentation(p);}
        if(latency){p.nvidiaLowLatency=mode==1?NvidiaLowLatency::On:NvidiaLowLatency::Off;engine.requestPresentation(p);}
        EnhancementSettings effects;effects.multiplier=unsigned(multiplier);if(xess)effects.frameGenerationBackend=FrameGenerationBackend::XeSS;
        // latency-screen-sr-vfg: a mode the budget really rejects, for the failed probe path of live VFG.
        const bool overBudgetVfg=scenario==L"latency-screen-sr-vfg";
        if(scenario==L"native-vfg"||scenario==L"latency-screen-vfg"||scenario==L"screen-vfg"||overBudgetVfg)effects.frameGenerationBackend=FrameGenerationBackend::Vfg;
        if(scenario==L"native-fsr")effects.frameGenerationBackend=FrameGenerationBackend::Fsr;
        if(scenario==L"effects"||scenario==L"native-effects"){effects.nr=true;effects.sr=true;effects.srTarget=veyra::pipeline::SrTarget::Qhd;}
        if(scenario==L"latency-screen-sr"||overBudgetVfg){effects.sr=true;effects.srTarget=veyra::pipeline::SrTarget::Uhd4K;}
        if(overBudgetVfg)effects.vfgQuality=2;
        // replay-vfg-low|medium|high: a clip through the live scheduler (with VEYRA_TEST_REPLAY_AS_CARD, paced like a capture card).
        const bool replayVfg=scenario.starts_with(L"replay-vfg-");
        if(replayVfg){effects.frameGenerationBackend=FrameGenerationBackend::Vfg;effects.vfgQuality=scenario==L"replay-vfg-high"||scenario==L"replay-vfg-toggle"?2:scenario==L"replay-vfg-medium"?1:0;}
        if(GetEnvironmentVariableW(L"VEYRA_TEST_VFG_NO_AUTO_REGULATE",nullptr,0)>0)effects.vfgAutoRegulate=false;
        if(physical){effects.nr=true;effects.sr=false;}
        auto options=PlayerOptions::from(effects);options.captureReplayForTest=scenario==L"replay"||replayVfg;
        engine.open(window,sourcePath,options);
        auto pump=[] {MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}};
        auto wait=[&](auto predicate,int ms=15000){auto end=Clock::now()+std::chrono::milliseconds(ms);while(Clock::now()<end){pump();auto s=engine.snapshot();if(s.failed)return false;if(predicate(s))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
        check(wait([](auto& s){return s.frames>10;},scenario==L"native-effects"||scenario==L"latency-screen-sr"||overBudgetVfg?30000:15000),"first frames");
        if(physical){const auto warm=Clock::now();check(wait([&](auto& s){return Clock::now()-warm>=std::chrono::seconds(10)&&s.nrActive&&s.fgActive;},20000),"ten second warmup with NR and DLSS active");}
        std::ofstream samples(dir/L"snapshots.csv");samples<<"wall,position,frames,generated,lateMs,queueReadyMs,presentMs,skipped,effective,mode,sync,revision\n";
        const auto start=Clock::now();auto last=engine.snapshot();const auto first=last;
        const auto host=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;};
        std::cout<<"measurementStart="<<host()<<" requestedMode="<<mode<<std::endl;
        std::ofstream capture;
        if(physical){capture.open(dir/L"capture-snapshots.csv");capture<<"host,received,dropped,processed,generated,nr,fg,limited,callbackFps,submitFps,ageMs,ageP95,requested,effective,realPresented,generatedPresented,fgSkipped,fgExpired\n";}
        while(Clock::now()-start<std::chrono::seconds(seconds)&&!last.failed){
            pump();last=engine.snapshot();samples<<std::chrono::duration<double>(Clock::now()-start).count()<<','<<last.position<<','<<last.frames<<','<<last.generated<<','<<last.lateMs<<','<<last.schedulingWaitP95Ms<<','<<last.presentCpuP95Ms<<','<<last.previewSkipped<<','<<last.presentationEffective.enabled<<','<<unsigned(last.presentationEffective.mode)<<','<<unsigned(last.presentationEffective.display)<<','<<last.presentationRevision<<'\n';
            if(physical){const auto& c=last.metrics.flow.counters;capture<<host()<<','<<last.captureReceived<<','<<last.captureDropped<<','<<last.frames<<','<<last.generated<<','<<last.nrActive<<','<<last.fgActive<<','<<last.fgBudgetLimited<<','<<last.captureFps<<','<<last.submissionFps.value_or(0)<<','<<last.captureAgeMs<<','<<last.captureAgeP95Ms<<','<<mode<<','<<(last.presentationEffective.enabled?int(last.presentationEffective.mode):-1)<<','<<c.realPresented<<','<<c.generatedPresented<<','<<c.fgSkippedBeforeEval<<','<<c.generatedExpiredAfterEval<<'\n';}
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const auto speed=(last.position-first.position)/std::chrono::duration<double>(Clock::now()-start).count();
        std::cout<<"measurementEnd="<<host()<<" elapsedSeconds="<<std::chrono::duration<double>(Clock::now()-start).count()<<std::endl;
        if(physical){check(last.nrActive&&last.fgActive&&last.nrEvaluated>first.nrEvaluated,"real capture NR and DLSS remain active");std::cout<<"captureReceivedDelta="<<last.captureReceived-first.captureReceived<<" captureDroppedDelta="<<last.captureDropped-first.captureDropped<<" generatedDelta="<<last.generated-first.generated<<" requested="<<mode<<" effective="<<(last.presentationEffective.enabled?int(last.presentationEffective.mode):-1)<<std::endl;}
        std::cout<<"mediaSpeed="<<speed<<" processedDelta="<<last.frames-first.frames<<std::endl;
        {std::string status(4096,char(0));status.resize(std::max(0,WideCharToMultiByte(CP_UTF8,0,last.presentationStatus.c_str(),-1,status.data(),int(status.size()),nullptr,nullptr)-1));std::cout<<"presentationStatus="<<status<<std::endl;}
        check(!last.failed&&last.frames>first.frames+10&&speed>.8&&speed<1.2,"sustained playback at media speed");
        // Without FG the ready->Present wait is display capacity only, never a first-frame PTS anchor.
        if(latency&&multiplier==1){std::cout<<"schedulingWaitP95Ms="<<last.schedulingWaitP95Ms<<std::endl;check(last.schedulingWaitP95Ms<1000.0/std::max<DWORD>(dm.dmDisplayFrequency,30)+2,"screen capture without FG is held at most one display refresh");}
        if(scenario==L"replay-vfg-toggle")for(const bool on:{false,true}){
            effects.vfgAutoRegulate=on;check(engine.requestSettings(effects),"VFG auto regulation change accepted");
            const auto changed=Clock::now();
            check(wait([&](auto& s){return Clock::now()-changed>std::chrono::seconds(3)&&s.applied.vfgAutoRegulate==on&&s.fgActive;},20000),"VFG auto regulation change applied with VFG still running");
        }
        if(multiplier>1&&!overBudgetVfg)check(last.fgActive&&last.generated>first.generated,"real provider generated frames during measurement");
        if(scenario==L"effects"||scenario==L"native-effects")check(last.nrActive&&last.srActive&&last.nrEvaluated>first.nrEvaluated,"NR and SR remain active with pacing");
        if(mode>=0&&!xess)check(last.presentationEffective.enabled,"optional pacing active");
        if((native||latency)&&mode>=0&&!xess)check(last.presentationEffective.nvidiaLowLatency==p.nvidiaLowLatency,"requested native NVIDIA low latency mode actually active with or without FG");
        if(mode>=0&&xess)check(!last.presentationEffective.enabled&&last.fgActive,"provider remains pacing owner");
        if(screen&&!latency){
            for(unsigned cycle=0;cycle<3;++cycle){
                SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"80");
                auto before=engine.snapshot();
                check(wait([&](auto& s){return s.frames>before.frames+15;}),"live input progresses during 80ms stall");
                SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
                before=engine.snapshot();const auto recovered=Clock::now();
                check(wait([&](auto& s){return Clock::now()-recovered>std::chrono::seconds(3)&&s.generated>before.generated+30&&!s.fgBudgetLimited;}),"valid generation recovers after repeated stalls");
            }
        }
        if(scenario==L"lifecycle"){
            engine.pause(true);check(wait([](auto& s){return s.transport==TransportState::Paused;}),"pause");
            p.enabled=false;engine.requestPresentation(p);check(wait([](auto& s){return !s.presentationEffective.enabled;}),"disable while paused");
            engine.seek(5);auto seek=engine.snapshot().seekRequested;check(wait([&](auto& s){return s.seekPresented>=seek;}),"paused seek");
            engine.pause(false);check(wait([](auto& s){return s.position>5.3;}),"resume");
            for(unsigned i=0;i<3;++i){const auto revision=engine.snapshot().presentationRevision;p={true,PacingMode(i),DisplaySync(i)};engine.requestPresentation(p);check(wait([&](auto& s){return s.presentationRevision>revision&&s.presentation==p&&s.presentationEffective.enabled&&s.presentationEffective.mode==PacingMode(i)&&s.presentationEffective.display==DisplaySync(i);}),"live mode change applied");}
            SetWindowPos(window,nullptr,30,30,960,600,SWP_NOZORDER);p.enabled=false;engine.requestPresentation(p);check(wait([](auto& s){return !s.presentationEffective.enabled;}),"off after resize");
            const auto before=engine.snapshot().frames;check(wait([&](auto& s){return s.frames>before+10;}),"off keeps playing");
        }
        if(scenario==L"native-lifecycle"){
            const auto beforePause=engine.snapshot().frames;
            engine.pause(true);check(wait([](auto& s){return s.transport==TransportState::Paused;}),"pause with On retained");
            Sleep(150);engine.pause(false);check(wait([&](auto& s){return s.frames>beforePause+8&&s.presentationEffective.nvidiaLowLatency==NvidiaLowLatency::On;}),"resume native On without a stale prepared input");
            engine.pause(true);check(wait([](auto& s){return s.transport==TransportState::Paused;}),"native pause");
            p.enabled=false;p.nvidiaLowLatency=NvidiaLowLatency::Off;engine.requestPresentation(p);
            check(wait([](auto& s){return s.presentationEffective.nvidiaLowLatency==NvidiaLowLatency::Off&&!s.presentationEffective.enabled;}),"disable native latency while paused");
            engine.seek(5);const auto seek=engine.snapshot().seekRequested;check(wait([&](auto& s){return s.seekPresented>=seek;}),"native paused seek");
            engine.pause(false);check(wait([](auto& s){return s.position>5.3;}),"native resume");
            for(auto setting:{NvidiaLowLatency::On,NvidiaLowLatency::On,NvidiaLowLatency::Off,NvidiaLowLatency::On}){
                const auto before=engine.snapshot().frames;p.enabled=setting!=NvidiaLowLatency::Off;p.nvidiaLowLatency=setting;engine.requestPresentation(p);
                check(wait([&](auto& s){return s.frames>before+12&&s.presentationEffective.nvidiaLowLatency==setting;}),"native live mode transition retains playback");
            }
            SetWindowPos(window,nullptr,30,30,960,600,SWP_NOZORDER);
            const auto before=engine.snapshot().frames;check(wait([&](auto& s){return s.frames>before+20&&s.presentationEffective.nvidiaLowLatency==NvidiaLowLatency::On;}),"native On survives resize");
            const auto session=engine.snapshot().sessionId;engine.open(window,sourcePath,options);
            check(wait([&](auto& s){return s.sessionId!=session&&s.frames>12&&s.presentationEffective.nvidiaLowLatency==NvidiaLowLatency::On;}),"source replacement restores requested On on the new device");
        }
        if(scenario==L"overload"){
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"80");
            auto before=engine.snapshot();const auto begin=Clock::now();
            check(wait([&](auto& s){return s.position>before.position+2.5;}),"overload progresses");
            const auto after=engine.snapshot();const auto rate=(after.position-before.position)/std::chrono::duration<double>(Clock::now()-begin).count();
            std::cout<<"overloadSpeed="<<rate<<" skippedDelta="<<after.previewSkipped-before.previewSkipped<<std::endl;
            check(rate>.8&&rate<1.2&&after.previewSkipped>before.previewSkipped,"overload stays real time with reported skips");
            p.enabled=false;engine.requestPresentation(p);
            check(wait([](auto& s){return !s.presentationEffective.enabled;},1000),"disable during overload within one second");
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
            before=engine.snapshot();check(wait([&](auto& s){return s.frames>before.frames+20;}),"recover after overload");
        }
        if(scenario==L"capture-nr-recovery"){
            auto before=engine.snapshot();
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"80");
            const auto overloadStart=Clock::now();
            check(wait([&](auto& s){return Clock::now()-overloadStart>std::chrono::seconds(3)&&s.frames>before.frames+5;}),"capture progresses during injected 80ms work");
            check(engine.snapshot().captureDropped>before.captureDropped,"capture overload reports mailbox drops");
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
            const auto recoveryStart=Clock::now();
            check(wait([&](auto& s){return Clock::now()-recoveryStart>std::chrono::seconds(12)&&s.fgActive&&s.nrActive&&!s.fgBudgetLimited&&s.captureAgeP95Ms<80;},20000),"capture recovers full FG without accumulating latency");
            engine.pause(true);check(wait([](auto& s){return s.transport==TransportState::Paused;}),"capture pause");
            engine.pause(false);before=engine.snapshot();
            check(wait([&](auto& s){return s.frames>before.frames+30&&s.fgActive&&s.nrActive;}),"capture resume restores NR and FG");
            for(const unsigned factor:{2u,6u,unsigned(multiplier)}){
                effects.multiplier=factor;check(engine.requestSettings(effects),"capture multiplier request accepted");
                const auto changed=Clock::now();
                check(wait([&](auto& s){return Clock::now()-changed>std::chrono::seconds(3)&&s.applied.multiplier==factor&&s.fgActive&&s.nrActive;}),"capture multiplier applied with active NR and FG");
            }
        }
        engine.stop();check(wait([&](auto&){return engine.idle();}),"clean stop");
    }
    if(inputWindow){PostMessageW(inputWindow,WM_CLOSE,0,0);if(WaitForSingleObject(inputProcess.hProcess,3000)!=WAIT_OBJECT_0){TerminateProcess(inputProcess.hProcess,2);WaitForSingleObject(inputProcess.hProcess,1000);}CloseHandle(inputProcess.hProcess);}
    DestroyWindow(window);CoUninitialize();return failures?1:0;
}
