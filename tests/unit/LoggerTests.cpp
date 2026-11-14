#include "veyra/Log.h"
#include <windows.h>
#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <io.h>
#include <memory>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
static std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    int failures=0;
    const auto check=[&](bool ok,const char* label){std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;};
    {
        auto owned=std::make_unique<veyra::Logger>();auto& logger=*owned;logger.setConsoleEnabled(false);
        check(logger.openFile((out/L"threads.log").wstring())&&logger.startAsync(),"asynchronous log opens and starts");
        std::vector<std::thread> producers;
        for(int t=0;t<4;++t)producers.emplace_back([&,t]{for(int n=0;n<800;++n)logger.write(veyra::LogLevel::Info,"producer","producer="+std::to_string(t)+" item="+std::to_string(n));});
        for(auto& t:producers)t.join();logger.flush();
        const auto contents=read(out/L"threads.log");std::istringstream lines(contents);std::string line;
        std::array<int,4> previous{-1,-1,-1,-1};std::set<std::pair<int,int>> seen;bool order=true;
        while(std::getline(lines,line)){
            const auto at=line.find("producer=");if(at==line.npos)continue;
            int t=-1,n=-1;
            if(sscanf_s(line.c_str()+at,"producer=%d item=%d",&t,&n)==2&&t>=0&&t<4){order&=n==previous[t]+1;previous[t]=n;seen.emplace(t,n);}
        }
        check(seen.size()==3200&&order&&logger.asyncStats().dropped==0,"concurrent producers retain every event and each producer order after explicit flush");
        logger.closeFile();
        check(logger.openFile((out/L"reopen.log").wstring()),"open after asynchronous close does not deadlock");
        logger.write(veyra::LogLevel::Error,"teardown","last durable marker");logger.flush();
        check(read(out/L"reopen.log").find("last durable marker")!=std::string::npos,"error and teardown marker are durable when flush returns");
    }
    {
        // Make stdout a pipe with no reader for a while. The output thread must
        // block in real I/O while the playback-like producer keeps running.
        HANDLE pipeRead=nullptr,pipeWrite=nullptr;
        if(!CreatePipe(&pipeRead,&pipeWrite,nullptr,4096))return 3;
        const int saved=_dup(_fileno(stdout));
        const int pipeFd=_open_osfhandle(reinterpret_cast<intptr_t>(pipeWrite),_O_WRONLY|_O_BINARY);
        if(saved<0||pipeFd<0)return 4;
        std::fflush(stdout);_dup2(pipeFd,_fileno(stdout));_close(pipeFd);
        auto owned=std::make_unique<veyra::Logger>();auto& logger=*owned;const bool started=logger.openFile((out/L"blocked.log").wstring())&&logger.startAsync();
        logger.write(veyra::LogLevel::Info,"blocked",std::string(16384,'x'));
        std::this_thread::sleep_for(50ms);
        const auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<5000;++i)logger.write(veyra::LogLevel::Info,"overflow","item="+std::to_string(i)+" "+std::string(1024,'p'));
        logger.write(veyra::LogLevel::Error,"critical","critical event retained through telemetry overflow");
        const auto producerMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const auto stats=logger.asyncStats();
        const auto report=logger.diagnosticReport();
        std::atomic<bool> flushed=false;
        std::thread barrier([&]{logger.flush();flushed=true;});
        std::this_thread::sleep_for(500ms);const bool waited=!flushed.load();
        std::thread reader([&]{char bytes[32768];DWORD received=0;while(ReadFile(pipeRead,bytes,sizeof(bytes),&received,nullptr)&&received){};});
        barrier.join();logger.setConsoleEnabled(false);
        std::fflush(stdout);_dup2(saved,_fileno(stdout));_close(saved);reader.join();CloseHandle(pipeRead);
        logger.closeFile();
        check(started&&producerMs<1000,"5000 producer calls finish while stdout is blocked (no producer sink I/O)");
        check(stats.queuedBytes<=veyra::Logger::kMaxQueuedBytes&&stats.queuedLines<=veyra::Logger::kMaxQueuedLines&&stats.dropped>0,"blocked output has bounded bytes/lines and counts overflow");
        check(report.find("dropped=0")==std::string::npos&&report.find("Log output: async=1")!=std::string::npos,"overflow remains visible in diagnostic report even before output recovers");
        const auto contents=read(out/L"blocked.log");
        check(waited&&flushed&&contents.find("critical event retained")!=std::string::npos,"flush waits for actual output and retains a recent critical event");
        check(contents.find("[log-backpressure]")!=std::string::npos,"recovered sink records explicit backpressure warning");
        std::printf("PRODUCER milliseconds=%.3f dropped=%llu highWaterBytes=%zu\n",producerMs,static_cast<unsigned long long>(stats.dropped),stats.highWaterBytes);
    }
    {
        auto owned=std::make_unique<veyra::Logger>();auto& logger=*owned;logger.setConsoleEnabled(false);
        logger.openFile((out/L"oversized.log").wstring());logger.startAsync();
        logger.write(veyra::LogLevel::Info,"oversized",std::string(veyra::Logger::kMaxQueuedBytes+1000,'z'));
        logger.flush();const auto stats=logger.asyncStats();const auto text=read(out/L"oversized.log");
        check(stats.highWaterBytes<=veyra::Logger::kMaxQueuedBytes&&stats.dropped>0&&text.find("oversized event truncated")!=std::string::npos,
              "one oversized event cannot bypass the byte bound and reports truncation");
    }
    {
        auto owned=std::make_unique<veyra::Logger>();auto& logger=*owned;logger.setConsoleEnabled(false);logger.setRotation(0,512);
        logger.openFile((out/L"rotation.log").wstring());logger.startAsync();
        for(int i=0;i<24;++i)logger.write(veyra::LogLevel::Info,"rotation","item="+std::to_string(i)+" "+std::string(80,'r'));
        logger.write(veyra::LogLevel::Info,"rotation","FINAL_ROTATION_MARKER");logger.closeFile();
        check(std::filesystem::exists(out/L"rotation.old.log")&&read(out/L"rotation.log").find("FINAL_ROTATION_MARKER")!=std::string::npos,"asynchronous rotation preserves final marker and old log");
    }
    {
        {auto owned=std::make_unique<veyra::Logger>();auto& logger=*owned;logger.setConsoleEnabled(false);logger.openFile((out/L"destructor.log").wstring());logger.startAsync();logger.write(veyra::LogLevel::Info,"shutdown","DESTRUCTOR_MARKER");}
        check(read(out/L"destructor.log").find("DESTRUCTOR_MARKER")!=std::string::npos,"destructor drains and flushes queued output");
    }
    return failures?1:0;
}
