// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include "veyra/pipeline/Rdna3WorkerDiagnostics.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace d=veyra::pipeline::rdna3::diagnostics;
std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--child"){
        const std::filesystem::path root=argv[2];FILE* log=nullptr;
        if(_wfopen_s(&log,(root/L"worker.log").c_str(),L"wb")||!log||!d::begin(log,(root/L"worker.log").c_str()))return 7;
        d::phase("network-load");d::phase("create-feature");
        fprintf(stderr,"simulated runtime stderr\n");
        DWORD n=0;const char rustStyle[]="simulated native stderr handle\n";
        WriteFile(GetStdHandle(STD_ERROR_HANDLE),rustStyle,DWORD(sizeof(rustStyle)-1),&n,nullptr);
        if(std::wstring(argv[3])==L"crash")RaiseException(0xE0007001,EXCEPTION_NONCONTINUABLE,0,nullptr);
        d::phase("ready");d::finish();fclose(log);return 0;
    }
    if(argc!=2)return 2;
    const auto base=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(base);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    for(const auto* mode:{L"normal",L"crash"}){
        const auto root=base/mode;std::filesystem::create_directories(root);
        auto command=std::wstring(L"\"")+executable+L"\" --child \""+root.wstring()+L"\" "+mode;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        const bool created=CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process)!=FALSE;
        check(created,"owned no-console fixture starts");if(!created)continue;
        const auto wait=WaitForSingleObject(process.hProcess,10000);check(wait==WAIT_OBJECT_0,"fixture exits within 10 seconds");
        if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
        DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
        const bool crash=std::wstring(mode)==L"crash";
        check(code==(crash?0xE0007001u:0u),"original process exception/normal exit status preserved");
        const auto log=read(root/L"worker.log"),stderrLog=read(root/L"worker.log.stderr.log");
        check(log.find("diagnostics=2")!=std::string::npos&&log.find("phase=create-feature")!=std::string::npos,"version and initialization phase flushed");
        check(stderrLog.find("simulated runtime stderr")!=std::string::npos,"CRT stderr captured");
        check(stderrLog.find("simulated native stderr handle")!=std::string::npos,"native stderr handle captured without a console");
        const auto exceptionLog=read(root/L"worker.log.exception.log");
        check(crash?(exceptionLog.find("exception=0xE0007001")!=std::string::npos&&exceptionLog.find("phase=create-feature")!=std::string::npos):exceptionLog.empty(),"exception recorded only for crash and not swallowed");
    }
    std::cout<<"checks="<<checks<<" failures="<<failures<<" (CPU diagnostic fixture, not Radeon inference)\n";return failures?1:0;
}
