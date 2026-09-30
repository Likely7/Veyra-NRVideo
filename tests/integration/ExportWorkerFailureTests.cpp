#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "ExportColorChainTest.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768))return 1;
    setFgCompatibilityProbeExecutable(executable);
    if(argc==3&&std::wstring_view(argv[1])==L"--export-worker"){
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        const int result=runExportWorker(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
        if(SUCCEEDED(com))CoUninitialize();
        return result;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--fg-compat-probe")
        return runFgCompatibilityProbe(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
    if(argc!=4)return 2;
    const std::wstring mode=argv[1];
    if(mode==L"color-chain"){
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com))return 1;
        bool ok=false;
        try{ok=runExportColorChainTest(argv[2],argv[3]);}
        catch(const std::exception& e){std::cerr<<"COLOR-WORKER exception="<<e.what()<<'\n';}
        CoUninitialize();return ok?0:1;
    }
    const bool encoder=mode==L"encoder",cancel=mode==L"cancel";
    if(encoder)SetEnvironmentVariableW(L"VEYRA_TEST_NVENC_FIRST_OPEN_FAILS",L"1");
    else{
        SetEnvironmentVariableW(L"VEYRA_TEST_FORCE_AMPERE_UNLOCK",L"1");
        SetEnvironmentVariableW(cancel?L"VEYRA_TEST_FG_PROBE_HANG":L"VEYRA_TEST_FG_PROBE_FAIL",L"1");
    }
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool ok=false;
    {
        ExportJobManager manager;
        EnhancementSettings settings;settings.nr=settings.sr=false;settings.multiplier=encoder?1:6;
        if(!manager.start(argv[2],argv[3],settings,true,12))return 1;
        const auto start=std::chrono::steady_clock::now();
        ExportJobSnapshot snapshot;
        bool requestedCancel=false;
        while(std::chrono::steady_clock::now()-start<std::chrono::seconds(20)){
            snapshot=manager.poll();
            if(!snapshot.active())break;
            if(cancel&&!requestedCancel&&std::chrono::steady_clock::now()-start>std::chrono::milliseconds(500)){
                manager.cancel();requestedCancel=true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Poll the process exit as well as its shared terminal state, so the
        // assertion covers the final error and appended worker log location.
        while(std::chrono::steady_clock::now()-start<std::chrono::seconds(25)){
            snapshot=manager.poll();
            if(snapshot.state==ExportState::Succeeded)break;
            if(cancel&&snapshot.state==ExportState::Cancelled)break;
            if(!cancel&&snapshot.message.find(snapshot.workerLog)!=std::wstring::npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ok=snapshot.workerPid&&std::filesystem::exists(snapshot.workerLog);
        if(cancel)ok=ok&&requestedCancel&&snapshot.state==ExportState::Cancelled&&!std::filesystem::exists(argv[3]);
        else if(encoder){
            ok=ok&&snapshot.state==ExportState::Failed&&snapshot.encoded==0&&snapshot.message.find(snapshot.workerLog)!=std::wstring::npos;
            ok=ok&&!std::filesystem::exists(argv[3])&&snapshot.message.find(L"OpenD3D12Session")!=std::wstring::npos&&snapshot.message.find(L"15")!=std::wstring::npos&&snapshot.message.find(L"HDR")!=std::wstring::npos;
        }else ok=ok&&snapshot.state==ExportState::Succeeded&&snapshot.sourceFrames==12&&snapshot.encoded==72&&std::filesystem::exists(argv[3]); // Probe refusal must not block actual FG initialization.
        std::wcout<<L"WORKER mode="<<mode<<L" state="<<int(snapshot.state)<<L" encoded="<<snapshot.encoded<<L" passed="<<ok<<L" message="<<snapshot.message<<L'\n';
    }
    CoUninitialize();return ok?0:1;
}
