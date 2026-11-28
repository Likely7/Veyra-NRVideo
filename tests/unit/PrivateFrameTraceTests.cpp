// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/diagnostics/CpuStallTrace.h"
#include "veyra/diagnostics/DiagnosticTrace.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
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

// 性能诊断 streaming recorder cases (no environment variable set).
int streamCase(std::wstring_view name,const fs::path& path){
    if(privateFrameTraceActive())return 11;
    if(name==L"stream"){
        if(!startDiagnosticTrace(path,std::chrono::minutes(5)))return 12;
        if(startDiagnosticTrace(path.wstring()+L"-second.csv",std::chrono::minutes(5)))return 13;
        if(!privateFrameTraceActive())return 14;
        const auto produce=[](unsigned start){for(unsigned i=0;i<20000;++i)source(start+i);};
        std::thread other(produce,100000);
        produce(1);other.join();
        std::this_thread::sleep_for(450ms);   // at least one streamed chunk
        recordDiagnosticMark(1);
        const auto mid=diagnosticTraceStatus();
        if(!mid.active||mid.recorded!=40001)return 15;
        const auto done=stopDiagnosticTrace();
        if(done.active||!done.published||done.recorded!=40001||done.dropped||privateFrameTraceActive())return 16;
        source(999999);                       // after stop: ignored
        // A second recording reuses the recorder.
        if(!startDiagnosticTrace(path.wstring()+L"-again.csv",std::chrono::minutes(1)))return 17;
        source(7);
        if(stopDiagnosticTrace().recorded!=1)return 18;
        return 0;
    }
    if(name==L"stream-duration"){
        if(!startDiagnosticTrace(path,300ms))return 21;
        source(1);
        std::this_thread::sleep_for(900ms);
        source(2);                            // past the deadline: ignored
        const auto status=diagnosticTraceStatus();
        if(status.active||!status.durationReached)return 22;
        const auto done=stopDiagnosticTrace();
        return done.published&&done.recorded==1?0:23;
    }
    if(name==L"stream-crash"){
        if(!startDiagnosticTrace(path,std::chrono::minutes(5)))return 31;
        for(unsigned i=1;i<=500;++i)source(i);
        std::this_thread::sleep_for(600ms);
        TerminateProcess(GetCurrentProcess(),0);   // no stop: the .partial must stay readable
    }
    return 39;
}

std::map<std::string,std::string> readHeader(std::istream& in,size_t& rows,bool& complete,std::string& firstRow){
    std::map<std::string,std::string> header;
    rows=0;complete=false;
    for(std::string line;std::getline(in,line);){
        if(line=="# complete=1"){complete=true;continue;}
        if(line.starts_with("# ")){
            const auto eq=line.find('=');
            if(eq!=line.npos)header[line.substr(2,eq-2)]=line.substr(eq+1);
            continue;
        }
        if(line.starts_with("event,")||line.empty())continue;
        if(firstRow.empty())firstRow=line;
        ++rows;
    }
    return header;
}

bool verifyStream(const fs::path& path,std::wstring_view name){
    auto partial=path;partial+=L".partial";
    if(name==L"stream-crash"){
        if(fs::exists(path))return false;
        std::ifstream in(partial);size_t rows=0;bool complete=false;std::string first;
        auto header=readHeader(in,rows,complete,first);
        return !complete&&rows==500&&header["schema"]=="veyra-frame-trace/1"&&header["finalized"]=="0";
    }
    if(fs::exists(partial))return false;
    std::ifstream in(path);size_t rows=0;bool complete=false;std::string first;
    auto header=readHeader(in,rows,complete,first);
    const auto number=[&](const char* key){return std::stoll(header[key]);};
    const size_t expected=name==L"stream"?40001:1;
    bool ok=complete&&rows==expected&&header["schema"]=="veyra-frame-trace/1"&&header["finalized"]=="1"&&
        number("recorded")==int64_t(expected)&&number("dropped")==0&&number("started_host")>0&&
        number("ended_host")>=number("started_host")&&number("started_utc_ms")>1700000000000LL&&
        header["termination"]==(name==L"stream-duration"?"duration":"shutdown")&&first.starts_with("Source,");
    if(name==L"stream"){
        std::ifstream again(path.wstring()+L"-again.csv");size_t againRows=0;bool againComplete=false;std::string againFirst;
        readHeader(again,againRows,againComplete,againFirst);
        ok=ok&&againComplete&&againRows==1&&!fs::exists(path.wstring()+L"-second.csv");
        // The mark is the last streamed row.
        std::ifstream marks(path);std::string line,last;
        while(std::getline(marks,line))if(!line.empty()&&line[0]!='#')last=line;
        ok=ok&&last.starts_with("Mark,");
    }
    return ok;
}

int recordCase(std::wstring_view name,const fs::path& path){
    if(name.starts_with(L"stream"))return streamCase(name,path);
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
    const bool stream=name.starts_with(L"stream");
    SetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_FILE",name==L"disabled"||stream?nullptr:path.c_str());
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
    if(name.starts_with(L"stream"))return verifyStream(path,name);
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
                                    L"partial-collision",L"overflow",L"invalid",L"disabled",L"pause",
                                    L"stream",L"stream-duration",L"stream-crash"}){
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
