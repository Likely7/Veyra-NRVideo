// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/diagnostics/CpuStallTrace.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
using namespace veyra::diagnostics;
namespace fs=std::filesystem;

namespace {
constexpr unsigned capacity=262144,extra=64;
constexpr auto sentinel="existing trace";

void source(unsigned id=1){
    FrameTraceEvent event;
    event.kind=TraceKind::Source;event.session=1;event.identity={1,2,id};
    event.batch=id;event.stage="File";
    recordPrivateFrame(event);
}

int recordCase(std::wstring_view name,const fs::path& path){
    const bool active=privateFrameTraceActive();
    if(name==L"existing"||name==L"disabled"||name==L"invalid")return active?1:0;
    if(!active)return 1;
    if(name==L"empty")return 0;
    if(name==L"overflow"){
        const auto produce=[](unsigned start){
            for(unsigned i=0;i<(capacity+extra)/2;++i)source(start+i);
        };
        std::thread other(produce,1+(capacity+extra)/2);
        produce(1);other.join();
        return 0;
    }
    source();
    if(name==L"duration"){
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!fs::exists(path)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(10ms);
        if(!fs::exists(path)||privateFrameTraceActive())return 1;
        source(2);
    }else if(name==L"late-collision"){
        std::ofstream(path)<<sentinel;
    }else if(name==L"pause"){
        CpuStallTrace trace("trace-test");
        trace.mark("beforeIdle");
        const auto start=std::chrono::steady_clock::now();
        std::this_thread::sleep_for(120ms);
        trace.excludeIdle(std::chrono::steady_clock::now()-start);
        trace.mark("afterIdle");
        veyra::pipeline::FrameBatch batch;batch.identity={1,2,1};batch.batchId=1;
        trace.recordPrivate(1,batch);
    }
    return 0;
}

bool runChild(const fs::path& exe,const fs::path& path,std::wstring_view name){
    SetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_FILE",name==L"disabled"?nullptr:path.c_str());
    SetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_MS",name==L"invalid"?L"249":name==L"duration"?L"250":nullptr);
    std::wstring command=L"\""+exe.wstring()+L"\" --child "+std::wstring(name)+L" \""+path.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,20000);
    if(wait==WAIT_TIMEOUT){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,3000);}
    DWORD code=1;
    const bool ok=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==0;
    CloseHandle(process.hProcess);
    return ok;
}

bool verify(const fs::path& path,std::wstring_view name){
    if(name==L"disabled"||name==L"invalid")return !fs::exists(path);
    if(name==L"partial-collision"){
        auto partial=path;partial+=L".partial";
        std::ifstream in(partial);std::string line;
        return !fs::exists(path)&&std::getline(in,line)&&line==sentinel;
    }
    std::ifstream in(path);
    if(!in)return false;
    if(name==L"existing"||name==L"late-collision"){std::string line;return std::getline(in,line)&&line==sentinel;}
    size_t rows=0;
    bool complete=false,schema=false,count=false,dropped=false,termination=false;
    bool startedAtZero=false,endedAtZero=false,pauseOk=false;
    const unsigned expected=name==L"empty"?0:name==L"overflow"?capacity:name==L"pause"?3:1;
    for(std::string line;std::getline(in,line);){
        schema|=line=="# schema=veyra-frame-trace/1";
        complete|=line=="# complete=1";
        count|=line=="# recorded="+std::to_string(expected);
        dropped|=line=="# dropped="+std::to_string(name==L"overflow"?extra:0);
        termination|=line==(name==L"duration"?"# termination=duration":"# termination=shutdown");
        startedAtZero|=line=="# started_host=0";
        endedAtZero|=line=="# ended_host=0";
        if(line.empty()||line[0]=='#'||line.starts_with("event,"))continue;
        ++rows;
        if(line.find(",afterIdle,")!=line.npos){
            size_t field=0;
            for(int i=0;i<11;++i){field=line.find(',',field)+1;if(!field)return false;}
            const auto measured=std::stod(line.substr(field));
            pauseOk=measured>=0&&measured<30;
        }
    }
    return schema&&complete&&count&&dropped&&termination&&rows==expected&&
        startedAtZero==(name==L"empty")&&endedAtZero==(name==L"empty")&&(name!=L"pause"||pauseOk);
}
}

int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring_view(argv[1])==L"--child")return recordCase(argv[2],argv[3]);
    if(argc!=2)return 2;
    const auto directory=fs::absolute(argv[1]);
    const auto exe=fs::absolute(argv[0]);
    fs::create_directories(directory);
    bool passed=true;
    for(const std::wstring_view name:{L"duration",L"shutdown",L"empty",L"existing",L"late-collision",
                                    L"partial-collision",L"overflow",L"invalid",L"disabled",L"pause"}){
        const auto path=directory/(std::wstring(name)+L".csv");
        if(fs::exists(path)){std::cerr<<"Use a fresh test directory\n";return 2;}
        if(name==L"existing")std::ofstream(path)<<sentinel;
        if(name==L"partial-collision"){
            auto partial=path;partial+=L".partial";
            if(fs::exists(partial))return 2;
            std::ofstream(partial)<<sentinel;
        }
        const bool ok=runChild(exe,path,name)&&verify(path,name);
        std::wcout<<(ok?L"PASS ":L"FAIL ")<<name<<L'\n';passed&=ok;
    }
    return passed?0:1;
}
