#include "veyra/engine/EngineController.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/Log.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#ifdef VEYRA_ENABLE_REMOTEPLAY
#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/source/RemotePlaySource.h"
#endif
using namespace veyra;
using namespace std::chrono_literals;
namespace {
int inactiveEffectsTest(const wchar_t* input,const wchar_t* output,bool nodeMode){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(output);
    Logger::instance().openFile((std::filesystem::path(output)/"engine.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"Inactive effect settings",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    engine::EngineController player;player.setVolume(0,true);
    engine::PlayerOptions options;options.nr=false;options.sr=false;options.fg=true;options.fgMultiplier=2;
    options.settings.nrRuntime=engine::NrRuntime::NvidiaOriginal;
    options.settings.nrLayerCount=2;
    for(auto& layer:options.settings.nrLayers)layer.runtime=engine::NrRuntime::NvidiaOriginal;
    if(nodeMode){
        auto chain=engine::toChain(options.snapshot());chain.mode=engine::ChainMode::Node;
        engine::removeLegacyNodeProtection(chain);
        options.nodeOrder=engine::runtimeOrder(chain);
    }
    player.open(window,input,options);
    int failures=0;unsigned edits=0;
    auto check=[&](bool pass,const char* text){std::cout<<(pass?"PASS ":"FAIL ")<<text<<std::endl;failures+=!pass;};
    auto until=[&](auto predicate,int seconds=30){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        while(std::chrono::steady_clock::now()<deadline){
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto s=player.snapshot();if(s.failed)return false;if(predicate(s))return true;
            std::this_thread::sleep_for(5ms);
        }
        return false;
    };
    auto store=[&](auto edit,const char* label){
        if(failures)return;
        const auto before=player.snapshot();auto next=before.desired;edit(next);next.revision=before.applied.revision;
        check(player.requestSettings(next,options.nodeOrder),"inactive edit accepted");
        check(player.snapshot().desired.revision==before.applied.revision,"inactive edit retains GPU revision immediately");
        if(failures)return;
        check(until([&](const auto& s){return !s.applying&&s.applied==next&&s.metrics.flow.counters.realPresented>=before.metrics.flow.counters.realPresented+8;}),label);
        const auto after=player.snapshot();
        check(after.metrics.identity.epoch==before.metrics.identity.epoch&&after.metrics.flow.reset.settingsRevision==before.metrics.flow.reset.settingsRevision&&after.metrics.flow.reset.sourceFrameId==before.metrics.flow.reset.sourceFrameId,"inactive edit retains epoch and reset record");
        if(before.fgActive)check(after.metrics.flow.counters.generatedPresented>before.metrics.flow.counters.generatedPresented,"generated presentation continues across inactive edit");
        std::cout<<"INACTIVE_EDIT index="<<++edits<<" revision="<<after.applied.revision<<" epoch="<<after.metrics.identity.epoch<<" realDelta="<<after.metrics.flow.counters.realPresented-before.metrics.flow.counters.realPresented<<" generatedDelta="<<after.metrics.flow.counters.generatedPresented-before.metrics.flow.counters.generatedPresented<<std::endl;
    };
    auto activate=[&](auto edit,const char* label){
        if(failures)return;
        const auto before=player.snapshot();auto next=before.desired;edit(next);
        check(player.requestSettings(next,options.nodeOrder),"active resource change accepted");
        const auto revision=player.snapshot().desired.revision;
        check(revision!=before.applied.revision,"active resource change advances GPU revision");
        check(until([&](const auto& s){return !s.applying&&s.applied.revision==revision&&s.metrics.flow.reset.settingsRevision==revision&&s.metrics.flow.reset.outcome==diagnostics::ResetOutcome::Completed&&s.metrics.flow.counters.realPresented>=8;}),label);
    };
    check(until([](const auto& s){return !s.applying&&!s.nrActive&&!s.srActive&&s.fgActive&&s.metrics.flow.counters.realPresented>30&&s.metrics.flow.counters.generatedPresented>20;}),"GTA6 4K actual DLSS 2X starts with NR and SR off");
    store([](auto& s){s.model.style=2;s.residual.total=5;s.nrTemporal=true;s.nrRuntime=engine::NrRuntime::Ampere;s.nrPolicy=pipeline::NrSizePolicy::P720;s.nrMotion=engine::MotionSource::Zero;s.nrLayers[0].model.style=2;s.nrLayers[0].temporal=true;s.nrLayers[0].runtime=engine::NrRuntime::Ampere;s.nrLayers[0].sizePolicy=pipeline::NrSizePolicy::Native;auto& layer=s.nrLayers[1];layer.model.style=1;layer.model.tone=.37f;layer.residual.total=1.7f;layer.sizePolicy=pipeline::NrSizePolicy::P720;},"disabled NR model/runtime/resolution stored while DLSS keeps presenting");
    store([](auto& s){s.srTarget=pipeline::SrTarget::Qhd;s.videoSrQuality=4;s.srMotion=engine::MotionSource::Zero;},"disabled SR target/quality/motion stored");
    store([](auto& s){s.color.exposure=.4f;s.color.setLutName(L"deliberately-missing-inactive.cube");},"disabled colour LUT is stored without loading it");
    store([](auto& s){s.videoHdr.peakNits=800;s.protection.featherPixels=15;s.protection.regions[0]={.1f,.2f,.6f,.7f};s.nrHoldTolerance=.12f;s.hdrCurve.midGrayNits=300;s.hdrMetadata.maxCllNits=1500;},"disabled HDR/protection/stabiliser tuning stored");
    activate([](auto& s){s.flow=engine::FlowQuality::Performance;},"shared optical-flow quality still rebuilds for active DLSS");
    activate([](auto& s){s.multiplier=1;},"frame generation turns off normally");
    store([](auto& s){s.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;s.vfgQuality=0;s.fgMotion=engine::MotionSource::Zero;s.fgStrictAdmission=true;},"disabled FG backend/quality/motion stored");
    store([](auto& s){s.flow=engine::FlowQuality::Quality;s.opticalFlowBackend=engine::OpticalFlowBackend::GpuDis;s.amdFlowHalfResolution=true;},"unused shared flow settings stored when every consumer is off");
    if(!failures){
        player.pause(true);std::this_thread::sleep_for(250ms);
        const auto before=player.snapshot();auto next=before.desired;next.model.tone=.37f;
        check(player.requestSettings(next,options.nodeOrder),"inactive edit accepted while paused");
        check(until([&](const auto& s){return !s.applying&&s.applied.model.tone==.37f;}),"paused inactive parameters stored");
        std::this_thread::sleep_for(250ms);const auto after=player.snapshot();
        check(after.applied.revision==before.applied.revision&&after.processedCompleted==before.processedCompleted,"paused inactive edit does not process the cached frame");
        player.pause(false);
        check(until([&](const auto& s){return s.frames>after.frames+10;}),"resume continues after inactive edits");
    }
    activate([](auto& s){
        s.nr=true;s.nrMotion=engine::MotionSource::OpticalFlow;
        s.flow=engine::FlowQuality::Balanced;s.opticalFlowBackend=engine::OpticalFlowBackend::Nvidia;s.amdFlowHalfResolution=false;
        auto& layer=s.nrLayers[1];layer.enabled=true;
        s.model=layer.model;s.residual=layer.residual;s.nrRuntime=layer.runtime;s.nrTemporal=layer.temporal;s.nrPolicy=layer.sizePolicy;
    },"original NR runs with one active layer and one bypassed layer");
    store([](auto& s){auto& layer=s.nrLayers[0];layer.model.intensity=.21f;layer.residual.total=4;layer.residual.correction.enabled=true;layer.sizePolicy=pipeline::NrSizePolicy::P480;},"bypassed NR layer does not interrupt the active NR layer");
    activate([](auto& s){s.color.enabled=true;s.color.setLutName(L"");},"colour enables with its previously stored exposure");
    store([](auto& s){s.color.exposure=.6f;s.videoSrQuality=2;s.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;s.nrLayers[0].sizePolicy=pipeline::NrSizePolicy::P900;},"mixed live colour and disabled settings apply in one transaction");
    activate([](auto& s){s.nrLayers[1].model.intensity=.6f;s.model=s.nrLayers[1].model;},"active NR model edit still resets its history");
    activate([](auto& s){s.sr=true;},"previously stored SR quality is used on activation");
    if(!failures){const auto s=player.snapshot();check(s.nrActive&&s.srActive&&s.applied.color.exposure==.6f&&s.applied.videoSrQuality==2&&s.applied.nrLayers[0].residual.total==4,"active effects and all bypassed values remain independent");}
    store([](auto& s){s.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;s.vfgQuality=0;},"VFG selection is saved while FG is off and NR/SR are on");
    activate([](auto& s){s.multiplier=2;},"previously stored VFG backend/quality generates real frames on activation");
    if(!failures)check(until([](const auto& s){return s.fgActive&&s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Vfg&&s.applied.vfgQuality==0&&s.metrics.flow.counters.generatedPresented>4;}),"VFG actually presents generated frames using the saved selection");
    player.stop();check(until([&](const auto&){return player.idle();},8),"inactive-effects session drains normally");
    DestroyWindow(window);CoUninitialize();
    std::cout<<"INACTIVE_RESULT nodeMode="<<nodeMode<<" edits="<<edits<<" failures="<<failures<<std::endl;
    return failures?1:0;
}
}
// --dedup-* replay (ContentDedup.h). Not inlined, snapshots built on the heap:
// wmain already holds many PlayerSnapshot locals, and two more overflowed its
// 1 MiB stack before the first line ran.
__declspec(noinline) static int runContentCadenceReplay(engine::EngineController& engine,const std::wstring& mode,bool fg,const std::filesystem::path& output,bool requireNr=false,bool requireSr=false){
    using namespace std::chrono_literals;
    auto pumpUntil=[&](auto predicate,std::chrono::seconds limit){
        const auto deadline=std::chrono::steady_clock::now()+limit;
        while(std::chrono::steady_clock::now()<deadline){
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(predicate())return true;std::this_thread::sleep_for(20ms);
        }
        return false;
    };
    std::unique_ptr<engine::PlayerSnapshot> start,end;
    const bool started=pumpUntil([&]{start.reset(new engine::PlayerSnapshot(engine.snapshot()));return start->frames>30&&(!fg||start->fgActive)&&(!requireNr||start->nrActive)&&(!requireSr||start->srActive);},25s);
    std::cout<<(started?"PASS ":"FAIL ")<<(fg?"replay starts with frame generation":"replay starts without frame generation")<<std::endl;
    const auto begin=std::chrono::steady_clock::now();
    wchar_t durationText[16]{};
    const int sampleSeconds=GetEnvironmentVariableW(L"VEYRA_TEST_CADENCE_SECONDS",durationText,16)?std::clamp(_wtoi(durationText),5,75):20;
    pumpUntil([&]{end.reset(new engine::PlayerSnapshot(engine.snapshot()));return end->failed||std::chrono::steady_clock::now()-begin>=std::chrono::seconds(sampleSeconds);},std::chrono::seconds(sampleSeconds+5));
    std::wcout<<L"DEDUP_RESULT mode="<<mode;
    std::cout<<" frames="<<end->frames-start->frames<<" skipped="<<end->captureRateSkipped-start->captureRateSkipped<<" generated="<<end->generated-start->generated
             <<" contentFps="<<end->contentFps<<" realPresented="<<end->metrics.flow.counters.realPresented-start->metrics.flow.counters.realPresented
             <<" generatedPresented="<<end->metrics.flow.counters.generatedPresented-start->metrics.flow.counters.generatedPresented
             <<" fgInvalid="<<end->metrics.flow.counters.fgInvalid-start->metrics.flow.counters.fgInvalid<<std::endl;
    const bool ran=started&&!end->failed&&end->frames>start->frames+300;
    std::cout<<(ran?"PASS ":"FAIL ")<<"content-cadence replay runs"<<std::endl;
    engine.stop();const bool drained=pumpUntil([&]{return engine.idle();},8s);
    std::cout<<(drained?"PASS ":"FAIL ")<<"content-cadence replay drains"<<std::endl;
    std::ofstream trace(output/"frame-trace.txt",std::ios::binary);trace<<Logger::instance().diagnosticReport();trace.close();
    Logger::instance().flush();
    return started&&ran&&drained?0:1;
}
int wmain(int argc,wchar_t**argv){
    if(argc==4&&(wcscmp(argv[3],L"--inactive-effects")==0||wcscmp(argv[3],L"--inactive-effects-node")==0))return inactiveEffectsTest(argv[1],argv[2],wcscmp(argv[3],L"--inactive-effects-node")==0);
#ifdef VEYRA_ENABLE_REMOTEPLAY
    // Explicit opt-in; uses existing local pairing without changing it. Runs
    // the actual shared engine, audio endpoint, GPU admission and presenter.
    if((argc==3||(argc==4&&(std::wstring_view(argv[3])==L"--reconnect"||std::wstring_view(argv[3])==L"--cancel-reconnect"||std::wstring_view(argv[3])==L"--manual-reconnect"||std::wstring_view(argv[3])==L"--repeat-outage")))&&std::wstring_view(argv[1])==L"--last-paired-ps5"){
        const bool repeatOutage=argc==4&&std::wstring_view(argv[3])==L"--repeat-outage";
        const bool manualReconnect=argc==4&&std::wstring_view(argv[3])==L"--manual-reconnect";
        const bool reconnect=argc==4,cancelReconnect=reconnect&&(std::wstring_view(argv[3])==L"--cancel-reconnect"||manualReconnect);
        const auto dir=remoteplay::profileDirectory();wchar_t name[80]{};
        GetPrivateProfileStringW(L"RemotePlay",L"LastProfile",L"",name,80,(dir/L"settings.ini").c_str());
        const std::wstring profile=name;if(profile.empty()||profile.find_first_of(L"/\\:")!=profile.npos)return 3;
        auto saved=remoteplay::loadProfile(dir/profile);if(!saved)return 4;
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        std::filesystem::create_directories(argv[2]);Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());Logger::instance().setConsoleEnabled(false);
        HWND window=CreateWindowExW(0,L"STATIC",L"PS5 timing regression",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return 5;
        engine::EngineController engine;engine.setVolume(0,true);
        engine::PlayerOptions options;options.nr=options.sr=options.fg=true;options.fgMultiplier=2;options.settings.videoSrQuality=2;
        source::RemotePlayConnectDesc request;request.request=std::move(*saved);request.request.video={1920,1080,60,80000,remoteplay::Codec::H264};request.decodeMode=source::RemotePlayConnectDesc::DecodeMode::Hardware;
        if(reconnect)SetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_DISCONNECT_AFTER_FRAMES",cancelReconnect?L"120":L"600");
        if(repeatOutage)SetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_REPEAT_OUTAGE",L"1");
        engine.openRemotePlay(window,std::move(request),options);
        const auto start=std::chrono::steady_clock::now();unsigned overloadSamples=0,overloadTimings=0;double early=-1,late=-1;uint64_t maxGenerated=0;bool failed=false;int lastSecond=-1;
        unsigned recoveryEpisodes=0;bool wasRecovering=false;
        bool sawRecovery=false,resumed=false,cancelled=false,playedBefore=false;uint64_t recoveryFrames=0,recoveryGenerated=0;unsigned attempts=0,postHealthySamples=0;
        while(std::chrono::steady_clock::now()-start<(repeatOutage?115s:reconnect?50s:120s)){
            const int second=int(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count());
            SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",!reconnect&&second>=30&&second<38?L"35":nullptr);
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto s=engine.snapshot();if(s.failed){failed=true;char message[1024]{};WideCharToMultiByte(CP_UTF8,0,s.status.c_str(),-1,message,sizeof(message),nullptr,nullptr);std::cout<<"ENGINE_FAILED "<<message<<std::endl;break;}
            maxGenerated=std::max(maxGenerated,s.generated);
            attempts=std::max(attempts,s.remoteReconnectAttempts);
            if(!sawRecovery&&s.frames>30&&s.generated>20)playedBefore=true;
            if(s.remoteRecovering&&!wasRecovering)++recoveryEpisodes;wasRecovering=s.remoteRecovering;
            if(s.remoteRecovering){sawRecovery=true;recoveryFrames=s.frames;recoveryGenerated=s.generated;}
            if(sawRecovery&&!s.remoteRecovering&&attempts>=1&&attempts<=3&&s.frames>recoveryFrames+60&&s.metrics.flow.validGeneratedFps>10&&s.captureAudio.bufferedMs>0)resumed=true;
            if(resumed&&s.frames>recoveryFrames+300&&s.generated>recoveryGenerated+100&&s.nrActive&&s.srActive&&s.captureAudio.running&&s.captureAudio.error.empty())++postHealthySamples;
            if(cancelReconnect&&s.remoteRecovering&&attempts==1){cancelled=true;break;}
            if(second>=33&&second<38){++overloadSamples;overloadTimings+=s.metrics.flow.gpuTiming[size_t(diagnostics::GpuStage::Nr)].mean.has_value();}
            if(second==25)early=s.captureAudio.compensationMs;
            if(second>=110)late=s.captureAudio.compensationMs;
            if(second!=lastSecond&&second%5==0){lastSecond=second;std::cout<<"PS5 t="<<second<<" presented="<<s.metrics.flow.presentSubmitFps<<" generated="<<s.generated<<" nrMs="<<s.metrics.flow.gpuTiming[size_t(diagnostics::GpuStage::Nr)].mean.value_or(-1)<<" audioDelay="<<s.captureAudio.compensationMs<<" pcm="<<s.captureAudio.bufferedMs<<" resets="<<s.captureAudio.resets<<" recovering="<<s.remoteRecovering<<" attempts="<<attempts<<std::endl;}
            std::this_thread::sleep_for(20ms);
        }
        SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
        SetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_DISCONNECT_AFTER_FRAMES",nullptr);
        SetEnvironmentVariableW(L"VEYRA_TEST_REMOTEPLAY_REPEAT_OUTAGE",nullptr);
        const auto final=engine.snapshot();engine.stop();
        const auto stopDeadline=std::chrono::steady_clock::now()+10s;while(!engine.idle()&&std::chrono::steady_clock::now()<stopDeadline)std::this_thread::sleep_for(10ms);
        if(reconnect){
            if(manualReconnect){
                bool manualOk=!failed&&cancelled&&engine.idle();
                // Reuse the same process/engine after cancelling recovery, then
                // immediately reopen again. No global reset or new pairing.
                for(int cycle=0;cycle<2&&manualOk;++cycle){
                    auto again=remoteplay::loadProfile(dir/profile);if(!again){manualOk=false;break;}
                    source::RemotePlayConnectDesc next;next.request=std::move(*again);
                    next.request.video={1920,1080,60,80000,remoteplay::Codec::H264};next.decodeMode=source::RemotePlayConnectDesc::DecodeMode::Hardware;
                    engine.openRemotePlay(window,std::move(next),options);
                    const auto deadline=std::chrono::steady_clock::now()+55s;bool played=false;
                    while(std::chrono::steady_clock::now()<deadline){
                        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                        const auto snap=engine.snapshot();if(snap.failed)break;
                        if(snap.frames>=180&&snap.generated>=100&&snap.captureAudio.running){played=true;break;}
                        std::this_thread::sleep_for(20ms);
                    }
                    engine.stop();const auto end=std::chrono::steady_clock::now()+10s;
                    while(!engine.idle()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(10ms);
                    manualOk=played&&engine.idle();
                    std::cout<<"MANUAL cycle="<<cycle<<" played="<<played<<" idle="<<engine.idle()<<std::endl;
                }
                std::cout<<(manualOk?"PASS ":"FAIL ")<<"same-process manual reconnect after cancelling recovery"<<std::endl;
                DestroyWindow(window);CoUninitialize();return manualOk?0:1;
            }
            // Observe beyond the first retry backoff: Stop must not reopen the session.
            if(cancelReconnect)std::this_thread::sleep_for(2s);
            const bool ok=(!repeatOutage||(recoveryEpisodes>=2&&!final.remoteRecovering&&final.frames>recoveryFrames+300&&final.metrics.flow.validGeneratedFps>10))&&!failed&&engine.idle()&&sawRecovery&&attempts>=1&&attempts<=3&&(cancelReconnect?cancelled:playedBefore&&resumed&&postHealthySamples>=250&&final.captureAudio.compensationMs<150);
            std::cout<<(ok?"PASS ":"FAIL ")<<"PS5 owned transport interruption episodes="<<recoveryEpisodes<<" playedBefore="<<playedBefore<<" recovery="<<sawRecovery<<" attempts="<<attempts<<" resumedWithFgAndAudio="<<resumed<<" postHealthySamples="<<postHealthySamples<<" manualCancel="<<cancelled<<" idle="<<engine.idle()<<std::endl;
            DestroyWindow(window);CoUninitialize();return ok?0:1;
        }
        const bool ok=!failed&&engine.idle()&&maxGenerated>100&&final.metrics.flow.validGeneratedFps>10&&overloadSamples>0&&overloadTimings*100>=overloadSamples*95&&early>=0&&late>=0&&late<150&&std::abs(late-early)<40;
        std::cout<<(ok?"PASS ":"FAIL ")<<"PS5 actual NR/SR/DLSS 2X, overload timings="<<overloadTimings<<'/'<<overloadSamples<<" earlyAudio="<<early<<" lateAudio="<<late<<" finalValidFgFps="<<final.metrics.flow.validGeneratedFps<<std::endl;
        DestroyWindow(window);CoUninitialize();return ok?0:1;
    }
#endif
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",L"1");
    const bool settingsRebuild=argc==4&&wcscmp(argv[3],L"--settings-rebuild")==0;
    if(!settingsRebuild&&argc!=3&&!(argc==4&&(wcscmp(argv[3],L"--backend-recovery")==0||wcscmp(argv[3],L"--nr-first")==0||wcscmp(argv[3],L"--seek-stress")==0||wcscmp(argv[3],L"--half-rate")==0||wcscmp(argv[3],L"--overload")==0||wcscmp(argv[3],L"--file-overload")==0||wcscmp(argv[3],L"--source-gap")==0||wcscmp(argv[3],L"--file-endpoint")==0||wcscmp(argv[3],L"--file-continuity")==0||wcscmp(argv[3],L"--file-fg-recovery")==0||wcscmp(argv[3],L"--reset-rollback")==0||std::wstring(argv[3]).rfind(L"--dedup-",0)==0)))return 2;SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);Logger::instance().startAsync();Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"Live scheduler replay",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;engine::EngineController engine;engine::PlayerOptions options;options.nr=false;options.fg=false;options.captureReplayForTest=true;
    const bool nrFirst=argc==4&&wcscmp(argv[3],L"--nr-first")==0;
    if(settingsRebuild){
        options.captureReplayForTest=false;options.nr=options.fg=true;options.fgMultiplier=2;
        options.settings.nrPolicy=pipeline::NrSizePolicy::Realtime;
        options.settings.nrRuntime=engine::NrRuntime::NvidiaOriginal;
        options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Dlss;
        engine.setVolume(0,true);ShowWindow(window,SW_SHOWNOACTIVATE);
    }
    if(nrFirst){options.captureReplayForTest=false;options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=2;options.settings.lowLatency=true;options.settings.videoSrQuality=2;engine.setVolume(0,true);}
    const bool seekStress=argc==4&&wcscmp(argv[3],L"--seek-stress")==0;
    const bool backendRecovery=argc==4&&wcscmp(argv[3],L"--backend-recovery")==0;
    if(backendRecovery){options.captureReplayForTest=false;options.nr=true;options.fg=false;engine.setVolume(0,true);SetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",L"1");}
    if(seekStress){options.captureReplayForTest=false;options.nr=options.fg=true;options.realtime=false;options.fgMultiplier=3;engine.setVolume(0,true);}
    const bool halfRate=argc==4&&wcscmp(argv[3],L"--half-rate")==0;
    const bool sourceGap=argc==4&&wcscmp(argv[3],L"--source-gap")==0;
    if(sourceGap){SetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_SOURCE_GAP",L"1");options.nr=options.fg=true;options.fgMultiplier=2;}
    const bool overload=(argc==4&&std::wstring(argv[3]).find(L"--overload")==0);
    const bool fileOverload=argc==4&&wcscmp(argv[3],L"--file-overload")==0;
    const bool fileEndpoint=argc==4&&wcscmp(argv[3],L"--file-endpoint")==0;
    const bool fileRecovery=argc==4&&wcscmp(argv[3],L"--file-fg-recovery")==0;
    if(fileRecovery){options.captureReplayForTest=false;options.nr=options.fg=true;options.fgMultiplier=2;engine.setVolume(0,true);}
    const bool fileContinuity=argc==4&&wcscmp(argv[3],L"--file-continuity")==0;
    if(fileContinuity){options.captureReplayForTest=false;options.nr=options.fg=true;options.realtime=false;options.settings.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;engine.setVolume(0,true);}
    if(fileEndpoint){options.captureReplayForTest=false;SetEnvironmentVariableW(L"VEYRA_TEST_FILE_ENDPOINT_LOSS",L"1");engine.setVolume(0,true);}
    if(overload){options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=4;options.settings.videoSrQuality=4;SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",nullptr);}
    if(halfRate)options.settings.content=engine::ContentRate::Capture60To30;
    // --dedup-transport|auto|half|fps30: file replay through the live capture path
    // with CPU decode and DLSS 2X, for content-cadence A/B runs (ContentDedup.h).
    // A "-nofg" suffix runs it without frame generation (light GPU load).
    const bool dedupRun=argc==4&&std::wstring(argv[3]).rfind(L"--dedup-",0)==0;
    std::wstring dedupMode=dedupRun?std::wstring(argv[3]).substr(8):L"";
    const bool dedupNoFg=dedupMode.ends_with(L"-nofg");if(dedupNoFg)dedupMode.resize(dedupMode.size()-5);
    if(dedupRun){
        options.softwareDecode=true;options.fg=!dedupNoFg;options.fgMultiplier=2;
        if(dedupMode.starts_with(L"nr-")){options.nr=true;dedupMode.erase(0,3);}
        if(dedupMode.starts_with(L"sr-")){options.sr=true;options.settings.videoSrQuality=2;dedupMode.erase(0,3);}
        options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Dlss;
        if(dedupMode.starts_with(L"anr-")){options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Anr;dedupMode.erase(0,4);}
        else if(dedupMode.starts_with(L"vfg-")){options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Vfg;dedupMode.erase(0,4);}
        else if(dedupMode.starts_with(L"xess-")){options.settings.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;dedupMode.erase(0,5);}
        else if(dedupMode.starts_with(L"fsr-")){options.settings.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;dedupMode.erase(0,4);}
        options.settings.content=dedupMode==L"auto"?engine::ContentRate::Auto:dedupMode==L"half"?engine::ContentRate::Capture60To30:
            dedupMode==L"fps30"?engine::ContentRate::Fps30:engine::ContentRate::Transport;
        engine.setVolume(0,true);ShowWindow(window,SW_SHOWNOACTIVATE);
    }
    if(fileOverload){options.captureReplayForTest=false;options.nr=options.sr=options.fg=true;options.realtime=false;options.fgMultiplier=4;options.settings.videoSrQuality=4;}
    int failures=0;auto check=[&](bool pass,const char* s){std::cout<<(pass?"PASS ":"FAIL ")<<s<<std::endl;if(!pass)++failures;};
    auto until=[&](auto predicate,int seconds=8){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<deadline){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}auto s=engine.snapshot();if(s.failed){std::wcerr<<s.status<<'\n';return false;}if(predicate(s))return true;std::this_thread::sleep_for(5ms);}return false;};
    engine.open(window,argv[1],options);
    if(dedupRun){const int result=runContentCadenceReplay(engine,dedupMode,!dedupNoFg,argv[2],options.nr,options.sr);DestroyWindow(window);CoUninitialize();return result;}
    if(settingsRebuild){
        check(until([](const auto& s){return s.frames>20&&s.nrActive&&s.fgActive;},30),"original NR 1080p and DLSS 2X start on the actual video");
        for(unsigned round=0;round<8&&!failures;++round){
            const auto before=engine.snapshot();auto changed=before.applied;
            changed.nr=round%2!=0;changed.multiplier=round==4?1:2;
            changed.sr=round==2||round==3;changed.videoSrQuality=4;
            check(engine.requestSettings(changed),"hot settings accepted");
            const auto revision=engine.snapshot().desired.revision;
            check(until([&](const auto& s){return !s.applying&&s.applied.revision==revision&&s.nrActive==changed.nr&&s.fgActive==(changed.multiplier>1)&&s.srActive==changed.sr&&s.metrics.flow.counters.realPresented>before.metrics.flow.counters.realPresented+12;},30),"new graph presents after NR/FG/SR transaction without restarting");
            const auto after=engine.snapshot();
            std::cout<<"SETTINGS_VIDEO round="<<round<<" revision="<<revision<<" nr="<<after.nrActive<<" sr="<<after.srActive<<" fg="<<after.fgActive<<" frames="<<after.frames<<" presents="<<after.metrics.flow.counters.realPresented<<" generated="<<after.metrics.flow.counters.generatedPresented<<" processingMs="<<after.metrics.flow.enhancementProcessing.mean.value_or(-1)<<std::endl;
            if(round==3){
                MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
                SetWindowPos(window,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_NOZORDER|SWP_NOACTIVATE);
            }
        }
        if(!failures){
            // Rebuild and seek in the same owner-loop turn: the old cached
            // frame is before the seek target and must not lose the applied
            // acknowledgement when that frame is discarded.
            const auto before=engine.snapshot();auto changed=before.applied;
            changed.nr=changed.sr=false;changed.multiplier=1;
            check(engine.requestSettings(changed),"all-effects-off transaction accepted before a seek");
            const auto revision=engine.snapshot().desired.revision;
            check(engine.setPlaybackRate(.5),"half-speed request accepted during the transaction");
            engine.seek(before.position+1);
            check(until([&](const auto& s){return !s.applying&&s.applied.revision==revision&&s.applied.multiplier==1&&!s.fgActive&&!s.nrActive&&!s.srActive&&s.frames>before.frames+12;},30),"discarded transaction frame still confirms the actual settings after rebuild plus seek");
        }
        engine.stop();check(until([&](const auto&){return engine.idle();},8),"settings session drains");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(backendRecovery){
        check(until([](const auto& s){return s.frames>20&&!s.applied.nr&&!s.backendWarning.empty();},25),"injected NR init failure retains real basic playback");
        SetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",nullptr);
        auto enabled=engine.snapshot().applied;enabled.nr=true;
        check(engine.requestSettings(enabled),"retry NR explicitly after initialization failure");
        check(until([](const auto& s){return !s.applying&&s.applied.nr&&s.nrActive;},25),"real NR runs after explicit retry");
        const auto before=engine.snapshot();
        SetEnvironmentVariableW(L"VEYRA_TEST_NR_RUNTIME_FAILURE",L"1");
        check(until([&](const auto& s){return s.frames>before.frames+20&&!s.applied.nr&&!s.backendWarning.empty();},25),"injected NR Evaluate failure releases commands and keeps playback advancing");
        SetEnvironmentVariableW(L"VEYRA_TEST_NR_RUNTIME_FAILURE",nullptr);
        enabled=engine.snapshot().applied;enabled.nr=true;
        check(engine.requestSettings(enabled),"retry NR after runtime recovery");
        check(until([](const auto& s){return !s.applying&&s.applied.nr&&s.nrActive;},25),"real NR resumes in same session after runtime recovery");
        enabled=engine.snapshot().applied;enabled.nr=false;enabled.multiplier=2;enabled.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
        check(engine.requestSettings(enabled),"enable real XeSS before presentation fault");
        check(until([](const auto& s){return !s.applying&&s.fgActive&&s.generated>10;},25),"XeSS generates real frames before injected fault");
        const auto xessBefore=engine.snapshot();
        SetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_FAILURE",L"1");
        check(until([&](const auto& s){return s.frames>xessBefore.frames+20&&s.applied.multiplier==1&&!s.applying&&!s.backendWarning.empty();},25),"XeSS presentation fault recovers through new scheduler and swapchain");
        SetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_FAILURE",nullptr);
        enabled=engine.snapshot().applied;enabled.multiplier=2;
        check(engine.requestSettings(enabled),"explicitly retry XeSS after presentation recovery");
        check(until([](const auto& s){return !s.applying&&s.fgActive&&s.generated>10;},25),"XeSS generates again in the same session");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"recovered session drains");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(nrFirst){
        check(until([](const auto& s){return s.frames>30&&s.srActive&&s.nrActive&&s.fgActive;},25),"NR-first RTX Video SR and FG execute");
        auto snapshot=engine.snapshot();check(snapshot.metrics.resolution.nr==snapshot.metrics.resolution.source,"NR processes source extent before SR");
        engine.saveFrame((std::filesystem::path(argv[2])/"nr-first-video-sr.png").wstring());
        check(until([&](const auto&){return std::filesystem::exists(std::filesystem::path(argv[2])/"nr-first-video-sr.png");}),"save actual output");
        auto settings=snapshot.applied;settings.videoSrQuality=0;check(engine.requestSettings(settings),"switch NR-first to DLSS SR");
        check(until([](const auto& s){return !s.applying&&s.applied.videoSrQuality==0&&s.srActive&&s.frames>60;},25),"NR-first DLSS SR runs");
        engine.seek(5);const auto request=engine.snapshot().seekRequested;
        check(until([&](const auto& s){return s.seekPresented==request;},15),"NR-first seek resets history");
        settings=engine.snapshot().applied;settings.lowLatency=false;check(engine.requestSettings(settings),"restore normal order");
        check(until([](const auto& s){return !s.applying&&!s.applied.lowLatency&&s.metrics.resolution.nr==s.metrics.resolution.output&&s.srActive;},25),"normal order restores native output NR");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"NR-first session closes");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(seekStress){
        check(until([](const auto& s){return s.position>1&&s.frames>15;},20),"initial native NR/FG playback");
        for(double target:{344.93,643.709,12.0,450.0,28.0,700.0}){
            engine.seek(target);const auto requested=engine.snapshot().seekRequested;
            check(until([&](const auto& s){return s.seekPresented==requested&&s.position>=target-.1;},12),"seek acknowledged by new presented frame");
            const auto base=engine.snapshot().metrics.flow.counters.realPresented;
            check(until([&](const auto& s){return s.metrics.flow.counters.realPresented>=base+45;},8),"continued playback releases output leases");
        }
        engine.pause(true);std::this_thread::sleep_for(100ms);engine.seek(32);engine.seek(44);engine.seek(61);
        const auto requested=engine.snapshot().seekRequested;
        check(until([&](const auto& s){return s.seekPresented==requested&&s.position>=60.9;},12),"paused rapid seeks keep latest request");
        engine.pause(false);check(until([](const auto& s){return s.position>62;},8),"resume after paused seek");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"seek session closes");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(fileRecovery){
        check(until([](const auto& s){return s.metrics.flow.generatedPresentFps>15&&s.metrics.flow.presentSubmitFps>45;},15),"NR plus 2X starts with real generated presentation");
        const auto before=engine.snapshot();const auto wallStart=std::chrono::steady_clock::now();
        SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"70");
        check(until([&](const auto& s){return s.processedCompleted>=before.processedCompleted+40;},15),"slow owner still advances source output");
        const auto slow=engine.snapshot();const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wallStart).count();
        check(slow.audioVideoWaits==before.audioVideoWaits,"insufficient output rate never stops steady audio");
        check(std::abs(slow.position-before.position-seconds)<.3,"slow preview follows one-times media timeline");
        check(slow.previewSkipped>0&&slow.lateMs<250,"overload skips distributed source opportunities and bounds latency");
        SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
        check(until([&](const auto& s){return s.applied.revision==before.applied.revision&&s.metrics.flow.generatedPresentFps>15&&s.metrics.flow.presentSubmitFps>45;},12),"FG recovers without changing settings or restarting audio");
        const auto recovered=engine.snapshot();check(recovered.audioVideoWaits==before.audioVideoWaits,"recovery retains continuous audio");
        std::cout<<"FILE_RECOVERY slowPresent="<<slow.metrics.flow.presentSubmitFps<<" recoveredPresent="<<recovered.metrics.flow.presentSubmitFps<<" generatedPresent="<<recovered.metrics.flow.generatedPresentFps<<" skipped="<<slow.previewSkipped<<"\n";
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"recovery session drains");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(fileContinuity){
        // Cold NGX/XeSS initialization can exceed the generic 8 s deadline.
        // Measure steady-state continuity only after real playback has started;
        // otherwise initialization's first audio release contaminates the sample.
        const bool started=until([](const auto& s){return s.frames>=20;},30);
        check(started,"XeSS/NR file starts with actual audio");
        if(!started){engine.stop();until([&](const auto&){return engine.idle();},5);DestroyWindow(window);CoUninitialize();return 1;}
        const auto before=engine.snapshot();const auto begin=std::chrono::steady_clock::now();
        check(until([&](const auto& s){return s.frames>=before.frames+100;},15),"XeSS/NR advances one hundred real source frames");
        const auto after=engine.snapshot();const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"CONTINUITY audioWaits="<<after.audioVideoWaits-before.audioVideoWaits<<" mediaAdvance="<<after.position-before.position<<" wall="<<wall<<" lateMs="<<after.lateMs<<'\n';
        check(after.audioVideoWaits==before.audioVideoWaits,"SDK-owned XeSS subframes never repeatedly stop audio");
        check(std::abs(after.position-before.position-wall)<.15,"file maintains continuous one-times playback");
        check(after.nrEvaluated>50&&after.generated>20&&after.fgActive,"NR and XeSS execute real evaluations during continuity test");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"continuous file closes cleanly");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(fileEndpoint){
        check(until([](const auto& s){return s.frames>=20&&s.audioEndpointRecovering;}),"actual player exposes endpoint recovery");
        const auto held=engine.snapshot();
        std::this_thread::sleep_for(300ms);const auto stillHeld=engine.snapshot();
        check(stillHeld.frames<=held.frames+2&&stillHeld.position<=held.position+.04,"video timeline stays held during audio endpoint outage");
        check(until([](const auto& s){return s.audioEndpointRecoveries==1&&!s.audioEndpointRecovering&&s.frames>=50;}),"player resumes after endpoint reanchor");
        check(std::abs(engine.snapshot().lateMs)<30,"restored software A/V skew under 30ms");
        engine.pause(true);engine.seek(.5);
        check(until([](const auto& s){return s.transport==engine::TransportState::Paused&&s.position>=.49&&s.position<.6;}),"paused seek after endpoint recovery");
        const auto pausedFrames=engine.snapshot().frames;engine.pause(false);
        check(until([&](const auto& s){return s.frames>=pausedFrames+10;}),"resume after recovered paused seek");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"recovered file closes without audio thread deadlock");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(sourceGap){
        // First NR+FG creation can take >8 s with a cold driver cache. The
        // assertion still has to catch the same 400 ms wait at exactly frame 12.
        check(until([](const auto& s){return s.frames==12&&s.metrics.flow.counters.realPresented==12&&s.processedCompleted==12;},30),"source Waiting still completes and presents all outstanding real frames");
        check(until([](const auto& s){return s.frames>=20;}),"source resumes after gap without deadlock");
        // The fixture can take roughly a minute in this host and this path intentionally runs
        // every source frame through the live scheduler; leave headroom for
        // host scheduling without changing the bounded 290s test watchdog.
        check(until([](const auto& s){return s.transport==engine::TransportState::Ended;},90),"EOF drains final live batch before marking ended");
        const auto s=engine.snapshot();const auto& c=s.metrics.flow.counters;
        std::cout<<"EOS frames="<<s.frames<<" ready="<<s.processedCompleted<<" presented="<<c.realPresented<<" cancelled="<<c.cancelledBeforePresent<<'\n';
        check(s.frames>20&&s.processedCompleted==s.frames&&c.realPresented==c.realSubmitted,"last real source frame is completed and current epoch fully presented");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"source-gap session stops cleanly");
        Logger::instance().flush();std::ifstream log(std::filesystem::path(argv[2])/"engine.log");std::string line;uint64_t totalPresented=0,totalSubmitted=0;
        const std::regex submittedPattern("realSubmitted=([0-9]+)"),presentedPattern("realPresented=([0-9]+)");
        while(std::getline(log,line))if(line.find("[frame-flow] state=closed ")!=std::string::npos){std::smatch match;if(std::regex_search(line,match,submittedPattern))totalSubmitted+=std::stoull(match[1]);if(std::regex_search(line,match,presentedPattern))totalPresented+=std::stoull(match[1]);}
        check(totalPresented==s.frames&&totalSubmitted==s.frames,"all source frames across EOF discontinuity epochs present exactly once");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(fileOverload){
        engine.setVolume(0,true);bool audioAnchorReleased=false;double maxLag=0;
        check(until([&](const auto& s){audioAnchorReleased|=s.frames>2&&!s.audioRebuffering;maxLag=std::max(maxLag,s.lateMs);return s.frames>=100;},90),"overloaded file keeps advancing without deadlocking");
        check(audioAnchorReleased,"startup audio anchor releases despite overload");
        // Sustained-overload steady window: steady-state audio never pauses —
        // the realtime preview drops enhancement opportunities instead (P1/P2).
        const auto steadyStart=engine.snapshot();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const auto steady=engine.snapshot();const auto& c=steady.metrics.flow.counters;
        std::cout<<"FILE_OVERLOAD steadyFrames="<<steadyStart.frames<<"->"<<steady.frames<<" addedWaits="<<steady.audioVideoWaits-steadyStart.audioVideoWaits<<" lateMs="<<steady.lateMs<<" startLateMs="<<steadyStart.lateMs<<" previewSkipped="<<c.previewSkippedBeforeGraph<<" fgSkipped="<<c.fgSkippedBeforeEval<<" fgEvaluated="<<c.fgEvaluated<<'\n';
        check(steady.audioVideoWaits==steadyStart.audioVideoWaits,"sustained overload adds no steady-state audio stops");
        check(steady.frames>steadyStart.frames+10,"overloaded preview keeps submitting frames");
        check(steady.lateMs<500&&std::abs(steady.lateMs-steadyStart.lateMs)<250,"overload display lag stays bounded instead of compounding");
        check(c.previewSkippedBeforeGraph>0||c.fgSkippedBeforeEval>0,"overload reduces work before evaluation (preview skips or FG admission)");
        std::cout<<"FILE_OVERLOAD maxObservedLagMs="<<maxLag<<'\n';
        check(engine.snapshot().nrEvaluated>50&&engine.snapshot().srActive&&engine.snapshot().generated>0,"sync test actually executes NR, SR and generated frames");
        auto settings=engine.snapshot().desired;settings.multiplier=2;engine.requestSettings(settings);
        check(until([](const auto& s){return !s.applying&&s.applied.multiplier==2&&s.metrics.flow.counters.realPresented>4;},30),"file audio resumes after FG backend rebuild");
        check(std::abs(engine.snapshot().lateMs)<500,"rebuilt overloaded graph retains bounded continuity lag");
        engine.seek(.3);
        check(until([](const auto& s){return s.position>=.3&&s.position<.6&&std::abs(s.lateMs)<60;}),"playing seek warms video before releasing audio");
        const auto before=engine.snapshot();engine.pause(true);engine.seek(.5);
        check(until([](const auto& s){return s.transport==engine::TransportState::Paused&&s.position>=.49&&s.position<.6;}),"paused seek survives an audio overload hold");
        engine.pause(false);check(until([&](const auto& s){return s.frames>before.frames+8;}),"file resumes after overload and seek");
        settings=engine.snapshot().desired;settings.nr=false;settings.sr=false;settings.multiplier=1;engine.requestSettings(settings);
        check(until([](const auto& s){return !s.applying&&!s.applied.nr&&!s.applied.sr&&s.applied.multiplier==1&&s.metrics.flow.counters.realPresented>=30;}),"leave overload and restore a sustainable video graph");
        check(std::abs(engine.snapshot().lateMs)<35,"recovered graph has no retained overload offset");
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"file overload shutdown");
        // Presented real PTS must cover the played span uniformly: under-rate
        // drops preview frames spread over time, never a long black stretch.
        Logger::instance().flush();std::ifstream log(std::filesystem::path(argv[2])/"engine.log");std::string line;
        std::vector<double> realPts;const std::regex ptsPattern("subframe=0 pts100ns=([0-9]+)");
        while(std::getline(log,line))if(line.find("[submit]")!=std::string::npos){std::smatch m;if(std::regex_search(line,m,ptsPattern))realPts.push_back(std::stod(m[1])/10000.0);}
        double maxPresentGap=0;unsigned resyncGaps=0;bool afterSeek=false;for(size_t i=1;i<realPts.size();++i){
            if(realPts[i]<realPts[i-1]){afterSeek=true;continue;} // backward seek boundary
            const double gap=realPts[i]-realPts[i-1];
            if(afterSeek){afterSeek=false;continue;} // seek forward-jump/resync window is explicit
            if(gap>400){++resyncGaps;continue;} // explicit rebuild/pause resync windows
            maxPresentGap=std::max(maxPresentGap,gap);
        }
        std::cout<<"FILE_OVERLOAD realPresents="<<realPts.size()<<" maxSteadyGapMs="<<maxPresentGap<<" resyncGaps="<<resyncGaps<<'\n';
        check(realPts.size()>=60,"overload presents a real frame sequence");
        check(maxPresentGap<200&&resyncGaps<=5,"steady presents cover the timeline without long gaps; only bounded explicit resyncs interrupt");
        DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    if(overload){
        check(until([](const auto& s){return s.frames>=150;},35),"native4K NR + VSR high + FG4 completes bounded overload replay");
        const auto s=engine.snapshot();const auto& c=s.metrics.flow.counters;
        check(c.fgCandidate==c.fgEvaluated+c.fgSkippedBeforeEval+c.fgSkippedForReset&&c.presentationBatchHighWater<=2&&c.commandSlotHighWater<=6,"overload work accounting and resource bounds");
        std::cout<<"OVERLOAD frames="<<s.frames<<" processedFps="<<s.fps<<" realPresented="<<c.realPresented<<" generatedPresented="<<c.generatedPresented<<" expired="<<c.generatedExpiredAfterEval<<" skipped="<<c.fgSkippedBeforeEval<<" evaluated="<<c.fgEvaluated<<" ageP95Ms="<<s.captureAgeP95Ms<<std::endl;
        engine.stop();check(until([&](const auto&){return engine.idle();},5),"overload shutdown releases leases");DestroyWindow(window);CoUninitialize();return failures?1:0;
    }
    check(until([](const auto& s){return s.capture&&s.frames>=8&&s.metrics.submitted>=4;}),"file replay runs actual live worker and presents");
    if(halfRate){auto s=engine.snapshot();check(s.captureHalfRate&&s.captureRateSkipped>=s.frames-2,"60fps input is sampled before GPU processing");}
    check(engine.snapshot().processedCompleted>0,"real-only GPU completions are counted without FG");
    if(halfRate){
        check(until([](const auto& s){return s.processedCompleted>=80;}),"sampled capture sustains more than two seconds");
        const auto s=engine.snapshot();check(s.fps>=27&&s.fps<=33,"GPU completed throughput is about 30fps, not transport60");
        const auto colorSamples=s.metrics.flow.gpuTiming[size_t(diagnostics::GpuStage::Color)].samples;
        std::cout<<"GPU_TIMING sourceFps="<<s.fps<<" colorSamples="<<colorSamples<<'\n';
        check(colorSamples>=20&&colorSamples<=35,"one-second stage window receives completed source-frame timings");
    }
    auto state=engine.snapshot();auto settings=state.desired;settings.nr=true;settings.multiplier=2;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.nr&&s.applied.multiplier==2&&!s.applying&&s.nrEvaluated>2&&s.generated>0;},30),"NR and 2x transaction while two batches are in flight");
    check(until([](const auto& s){const auto& r=s.metrics.flow.reset;return r.settingsRevision==s.applied.revision&&r.outcome==diagnostics::ResetOutcome::Completed&&r.totalMs.has_value();}),"settings reset completes only after matching GPU-ready output");
    {
        const auto s=engine.snapshot();const auto& r=s.metrics.flow.reset;double stages=0;bool measured=true;
        for(const auto& ms:r.stageMs){measured&=ms.has_value()&&*ms>=0;if(ms)stages+=*ms;}
        check(r.sessionId==s.sessionId&&r.epoch==s.metrics.identity.epoch&&r.sourceFrameId>0&&measured&&r.totalMs&&*r.totalMs+.01>=stages,"reset identity and disjoint drain/destroy/create/warmup/ready intervals are measured");
    }
    check(until([](const auto& s){const auto& m=s.metrics.flow;return m.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromA)].samples>0&&m.pairTiming[size_t(diagnostics::PairTiming::GeneratedFromB)].samples>0;}),"presented generated frames have actual A/B latency samples");
    state=engine.snapshot();const auto liveRevision=state.applied.revision;check(state.metrics.identity.settingsRevision==state.applied.revision,"GPU metrics belong to applied revision");
    const auto& flow=state.metrics.flow;
    check(flow.latest.sameWindow(state.sessionId,state.metrics.identity)&&flow.counters.fgEvaluated>0,"flow counters carry actual session, epoch and revision");
    check(flow.counters.commandSlotHighWater<=6&&flow.counters.presentationBatchHighWater<=2,"command and presentation queues remain bounded");
    check(flow.counters.fgCandidate==flow.counters.fgSkippedBeforeEval+flow.counters.fgEvaluated+flow.counters.fgSkippedForReset,"each candidate is accounted before evaluate");
    if(argc==4&&wcscmp(argv[3],L"--reset-rollback")==0){
        SetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",L"1");
        settings=state.desired;settings.nr=false;engine.requestSettings(settings);
        const auto rejected=engine.snapshot().desired.revision;
        check(until([&](const auto& s){return s.rejectedRevision==rejected&&!s.applying&&s.applied.nr;}),"failed first evaluation restores previous NR configuration");
        SetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr);
        check(until([&](const auto& s){const auto& r=s.metrics.flow.reset;return r.settingsRevision==rejected&&r.outcome==diagnostics::ResetOutcome::RolledBack&&r.totalMs.has_value();}),"failed reset keeps rejected revision and rollback timing instead of success");
        check(until([&](const auto& s){return s.frames>state.frames+5&&s.metrics.flow.counters.realReady>0;}),"restored graph produces completed real frames after rollback");
        state=engine.snapshot();
    }
    const auto audioEpoch=state.metrics.identity.epoch;const auto audioFrames=state.frames;
    settings=state.desired;settings.audioSync=engine::AudioSyncMode::Manual;settings.audioOffsetMs=75;
    check(engine.requestSettings(settings),"audio-only change accepted while NR/FG are active");
    check(until([&](const auto& s){return !s.applying&&s.applied.audioOffsetMs==75&&s.frames>=audioFrames+4;}),"audio-only transaction completes while video advances");
    state=engine.snapshot();
    check(state.applied.revision==liveRevision&&state.metrics.identity.epoch==audioEpoch,"audio-only edit retains GPU revision and temporal history");
    settings=state.desired;check(engine.requestSettings(settings)&&engine.snapshot().desired.revision==liveRevision&&!engine.snapshot().applying,"identical settings notification does not enqueue a GPU transaction");
    engine.pause(true);check(until([](const auto& s){return s.transport==engine::TransportState::Paused;}),"pause returns without queue deadlock");
    check(engine.snapshot().fps==0,"paused FPS is zero rather than a retained session average");
    settings=engine.snapshot().desired;settings.audioOffsetMs=90;engine.requestSettings(settings);
    check(until([&](const auto& s){return !s.applying&&s.applied.audioOffsetMs==90&&s.applied.revision==liveRevision;}),"audio edit applies while paused without rerunning video");
    settings=engine.snapshot().desired;settings.model.style=1;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.model.style==1&&!s.applying;}),"settings apply while paused");
    check(until([](const auto& s){return s.metrics.flow.reset.settingsRevision==s.applied.revision&&s.metrics.flow.reset.outcome==diagnostics::ResetOutcome::Completed;}),"paused cached preview reset observes its actual GPU completion");
    const auto pausedRevision=engine.snapshot().applied.revision;
    engine.pause(false);const auto before=engine.snapshot().frames;check(until([&](const auto& s){return s.frames>=before+8&&s.metrics.submitted>0;}),"resume resets history and restarts bounded presentation");
    settings=engine.snapshot().desired;settings.multiplier=4;engine.requestSettings(settings);
    check(until([](const auto& s){return s.applied.multiplier==4&&!s.applying&&s.metrics.submitted>8;}),"4x uses worker without lease overwrite");
    settings=engine.snapshot().desired;settings.nr=false;settings.multiplier=1;engine.requestSettings(settings);
    check(until([](const auto& s){return !s.applied.nr&&s.applied.multiplier==1&&!s.applying&&s.metrics.sourceFrames>4;}),"disable features and keep live source open");
    check(engine.snapshot().metrics.validGenerated==0,"disabled revision cannot retain prior generated-frame count");
    check(engine.snapshot().metrics.flow.counters.fgEvaluated==0&&engine.snapshot().metrics.flow.counters.generatedPresented==0,"late FG completion never enters disabled revision flow counters");
    if(halfRate){
        settings=engine.snapshot().desired;settings.content=engine::ContentRate::Transport;engine.requestSettings(settings);
        check(until([](const auto& s){return !s.applying&&!s.captureHalfRate&&s.metrics.sourceFrames>=80;}),"switch back to original capture cadence live");
        const auto s=engine.snapshot();check(s.fps>=55&&s.fps<=65,"GPU completed throughput returns to about 60fps");
    }
    uint64_t cancelledRevision=0;
    if(argc==4&&wcscmp(argv[3],L"--reset-rollback")==0){
        settings=engine.snapshot().desired;settings.nr=true;settings.multiplier=2;engine.requestSettings(settings);cancelledRevision=engine.snapshot().desired.revision;
        check(until([&](const auto& s){return s.metrics.flow.reset.settingsRevision==cancelledRevision&&s.metrics.flow.reset.outcome==diagnostics::ResetOutcome::InProgress;}),"pending rebuild is observable before first output");
    }
    engine.stop();const auto stopLimit=std::chrono::steady_clock::now()+5s;while(!engine.idle()&&std::chrono::steady_clock::now()<stopLimit)std::this_thread::sleep_for(5ms);check(engine.idle(),"stop drains presenter before graph shutdown");
    if(cancelledRevision){const auto s=engine.snapshot();const auto& r=s.metrics.flow.reset;check(r.settingsRevision==cancelledRevision&&r.outcome==diagnostics::ResetOutcome::Cancelled&&!r.stageMs[size_t(diagnostics::ResetStage::FirstValid)],"stopped rebuild records cancellation without invented valid output");}
    Logger::instance().flush();std::ifstream log(std::filesystem::path(argv[2])/"engine.log");std::string line;bool liveFresh=false,pausedCached=false;
    while(std::getline(log,line))if(line.find("source-identity")!=std::string::npos){
        if(line.find("cached=false revision="+std::to_string(liveRevision)+" ")!=std::string::npos)liveFresh=true;
        if(line.find("cached=true revision="+std::to_string(pausedRevision)+" ")!=std::string::npos)pausedCached=true;
        if(line.find("cached=true revision="+std::to_string(liveRevision)+" ")!=std::string::npos)++failures;
    }
    check(liveFresh,"running capture settings process fresh source frame, never cached frame");
    check(pausedCached,"paused settings retain cached-frame preview");
    const auto report=Logger::instance().diagnosticReport();
    check(report.find("event=Submitted")!=std::string::npos&&report.find("event=Ready")!=std::string::npos&&
        report.find("event=Present")!=std::string::npos&&report.find("event=Gpu")!=std::string::npos&&report.find("event=Reset")!=std::string::npos,
        "diagnostic preview contains real engine submission, completion, presentation, GPU and reset records");
    {std::ofstream trace(std::filesystem::path(argv[2])/"diagnostics.txt");trace<<report;}
    DestroyWindow(window);CoUninitialize();std::cout<<"failures="<<failures<<" (synthetic file replay, not physical capture)\n";return failures?1:0;
}
