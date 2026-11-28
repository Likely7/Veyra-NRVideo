// SPDX-License-Identifier: GPL-3.0-only
// Adapted from the private Veyra frame trace patch supplied by the Magpie author.
#include "veyra/diagnostics/FrameTrace.h"
#include "veyra/diagnostics/DiagnosticTrace.h"
#include <windows.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

namespace veyra::diagnostics {
namespace {
using Clock=std::chrono::steady_clock;
int64_t tick100ns(Clock::time_point point){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch()).count()/100;
}

class Recording {
public:
    static constexpr size_t capacity=262144;
    Recording(std::filesystem::path path,std::chrono::milliseconds duration)
        : path_(std::move(path)),duration_(duration){
        events_.reserve(capacity);
        writer_=std::thread([this]{finish();});
    }
    ~Recording(){
        {std::lock_guard lock(mutex_);stopping_=true;}
        wake_.notify_one();
        writer_.join();
    }
    bool active() const noexcept {return active_.load(std::memory_order_relaxed);}
    void add(const FrameTraceEvent& event) noexcept {
        if(!active())return;
        std::lock_guard lock(mutex_);
        if(!active()||stopping_)return;
        const auto now=Clock::now();
        if(deadline_==Clock::time_point{}){
            startedHost_=event.host100ns?event.host100ns:tick100ns(now);
            deadline_=now+duration_;
            wake_.notify_one();
        }
        if(now>=deadline_)return;
        if(events_.size()<capacity){
            events_.push_back(event);
            if(!events_.back().host100ns)events_.back().host100ns=tick100ns(now);
        }else ++dropped_;
    }

private:
    void finish() noexcept {
        int64_t endHost=0;
        bool early=true;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock,[&]{return deadline_!=Clock::time_point{}||stopping_;});
            if(deadline_!=Clock::time_point{}){
                wake_.wait_until(lock,deadline_,[&]{return stopping_;});
                const auto now=Clock::now();
                early=now<deadline_;
                endHost=tick100ns(std::min(now,deadline_));
            }
            active_.store(false,std::memory_order_relaxed);
        }
        // All producers are sealed before any file I/O. Do not use the logger
        // during static teardown, when the async log sink may be closing.
        bool published=false;
        try{published=write(endHost,early);}catch(...){}
        std::vector<FrameTraceEvent>{}.swap(events_);
        if(!published)OutputDebugStringW(L"Veyra private frame trace: could not publish the CSV.\n");
    }

    bool write(int64_t endHost,bool early) const {
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(),error);
        if(error||std::filesystem::exists(path_,error)||error)return false;
        auto partial=path_;partial+=L".partial";
        struct OutputFile {
            HANDLE handle;
            ~OutputFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        } file{CreateFileW(partial.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(file.handle==INVALID_HANDLE_VALUE)return false;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        const auto flush=[&]{
            if(!out)return false;
            const auto bytes=out.view();
            DWORD written=0;
            const bool ok=WriteFile(file.handle,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size();
            out.str(std::string{});
            return ok;
        };
        out<<std::setprecision(12)
           <<"# schema=veyra-frame-trace/1\n# pid="<<GetCurrentProcessId()
           <<"\n# host_ticks_per_second=10000000\n# started_host="<<startedHost_
           <<"\n# ended_host="<<endHost<<"\n# requested_ms="<<duration_.count()
           <<"\n# termination="<<(early?"shutdown":"duration")
           <<"\n# recorded="<<events_.size()<<"\n# dropped="<<dropped_<<"\n# capacity="<<capacity<<'\n'
           <<"event,host,session,revision,epoch,source,batch,fence,pts,detail,count,ms,"
             "decodedHost,processHost,readyHost,presentBeginHost,presentEndHost,entryDeviationMs,returnDeviationMs,"
             "queueDepth,mediaDeviationValid,providerInstance,providerCycle,preparationCycle,providerCallerRva,"
             "providerScheduleBeginHost,providerFenceSampled,providerFenceBefore,providerFenceAtDeadline,"
             "providerFenceTarget,frameKind,stage,arrivalHost,deadlineHost,sourceA,sourceAHost,"
             "gpuBegin,gpuEnd,gpuFrequency,presentCallHost\n";
        size_t pending=0;
        for(const auto& e:events_){
            out<<traceKindName(e.kind)<<','<<e.host100ns<<','<<e.session
               <<','<<e.identity.settingsRevision<<','<<e.identity.epoch<<','<<e.identity.sourceFrameId
               <<','<<e.batch<<','<<e.fence<<','<<e.pts100ns<<','<<e.detail<<','<<e.count<<','<<e.milliseconds
               <<','<<e.decodedHost<<','<<e.processHost<<','<<e.readyHost<<','<<e.presentBeginHost<<','<<e.presentEndHost
               <<','<<e.entryDeviationMs<<','<<e.returnDeviationMs<<','<<e.queueDepth<<','<<e.mediaDeviationValid
               <<','<<e.providerInstance<<','<<e.providerCycle<<','<<e.preparationCycle<<','<<e.providerCallerRva
               <<','<<e.providerScheduleBeginHost<<','<<e.providerFenceSampled<<','<<e.providerFenceBefore
               <<','<<e.providerFenceAtDeadline<<','<<e.providerFenceTarget
               <<','<<e.frameKind<<','<<e.stage<<','<<e.arrivalHost<<','<<e.deadlineHost<<','<<e.sourceA<<','<<e.sourceAHost
               <<','<<e.gpuBegin<<','<<e.gpuEnd<<','<<e.gpuFrequency<<','<<e.presentCallHost<<'\n';
            if(++pending==512){
                if(!flush())return false;
                pending=0;
            }
        }
        out<<"# complete=1\n";
        if(!flush()||!FlushFileBuffers(file.handle))return false;
        const BOOL closed=CloseHandle(file.handle);
        file.handle=INVALID_HANDLE_VALUE;
        // No replacement flag: two processes targeting the same path cannot
        // replace each other's final CSV, including a late collision.
        return closed&&MoveFileExW(partial.c_str(),path_.c_str(),MOVEFILE_WRITE_THROUGH);
    }

    const std::filesystem::path path_;
    const std::chrono::milliseconds duration_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread writer_;
    std::atomic<bool> active_=true;
    bool stopping_=false;
    int64_t startedHost_=0;
    Clock::time_point deadline_{};
    uint64_t dropped_=0;
    std::vector<FrameTraceEvent> events_;
};

Recording* recording() noexcept {
    static const auto instance=[]() -> std::unique_ptr<Recording>{
        const auto size=GetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_FILE",nullptr,0);
        if(!size)return {};
        try{
            std::wstring value(size,L'\0');
            const auto length=GetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_FILE",value.data(),size);
            if(!length||length>=size)return {};
            value.resize(length);
            std::filesystem::path path(value);
            if(!path.is_absolute()){
                OutputDebugStringW(L"Veyra private frame trace: use an absolute output path.\n");
                return {};
            }
            std::error_code error;
            if(std::filesystem::exists(path,error)||error){
                OutputDebugStringW(L"Veyra private frame trace: output exists or is inaccessible.\n");
                return {};
            }
            wchar_t durationText[16]{};
            const auto digits=GetEnvironmentVariableW(L"VEYRA_PRIVATE_FRAME_TRACE_MS",durationText,16);
            auto duration=std::chrono::milliseconds(30000);
            if(digits){
                if(digits>=std::size(durationText)||std::wstring_view(durationText).find_first_not_of(L"0123456789")!=std::wstring_view::npos)
                    return {};
                const auto requested=std::stoull(durationText);
                if(requested<250||requested>120000)return {};
                duration=std::chrono::milliseconds(requested);
            }
            return std::make_unique<Recording>(std::move(path),duration);
        }catch(...){
            OutputDebugStringW(L"Veyra private frame trace: initialization failed.\n");
            return {};
        }
    }();
    return instance.get();
}
}

namespace {
// --- 性能诊断 streaming recorder ------------------------------------------
// Same rows as the v1 CSV, written while the session runs. Producers append to
// a bounded vector under a mutex (a few producer threads at ~1–3 k events/s);
// a below-normal writer thread swaps it out every 200 ms and formats it. The
// object is intentionally never destroyed: a late producer during process
// teardown must never touch a destroyed mutex. Finalization happens through
// stopDiagnosticTrace(); a killed process leaves a readable .partial file.
constexpr const char* kColumns=
    "event,host,session,revision,epoch,source,batch,fence,pts,detail,count,ms,"
    "decodedHost,processHost,readyHost,presentBeginHost,presentEndHost,entryDeviationMs,returnDeviationMs,"
    "queueDepth,mediaDeviationValid,providerInstance,providerCycle,preparationCycle,providerCallerRva,"
    "providerScheduleBeginHost,providerFenceSampled,providerFenceBefore,providerFenceAtDeadline,"
    "providerFenceTarget,frameKind,stage,arrivalHost,deadlineHost,sourceA,sourceAHost,"
    "gpuBegin,gpuEnd,gpuFrequency,presentCallHost\n";

int64_t unixMsNow() noexcept {
    FILETIME ft{};GetSystemTimePreciseAsFileTime(&ft);
    const auto ticks=(uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;
    return int64_t(ticks/10000)-11644473600000LL;
}

template<class T> void appendNumber(std::string& out,T value){
    if constexpr(std::is_floating_point_v<T>){if(!std::isfinite(value))value=0;}
    char buffer[64];
    const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
    out.append(buffer,result.ptr);
}

void appendRow(std::string& out,const FrameTraceEvent& e){
    out+=traceKindName(e.kind);
    const auto n=[&](auto v){out+=',';appendNumber(out,v);};
    const auto s=[&](const char* text){out+=',';if(text)out+=text;};
    n(e.host100ns);n(e.session);n(e.identity.settingsRevision);n(e.identity.epoch);n(e.identity.sourceFrameId);
    n(e.batch);n(e.fence);n(e.pts100ns);n(e.detail);n(e.count);n(e.milliseconds);
    n(e.decodedHost);n(e.processHost);n(e.readyHost);n(e.presentBeginHost);n(e.presentEndHost);
    n(e.entryDeviationMs);n(e.returnDeviationMs);n(e.queueDepth);n(int(e.mediaDeviationValid));
    n(e.providerInstance);n(e.providerCycle);n(e.preparationCycle);n(e.providerCallerRva);
    n(e.providerScheduleBeginHost);n(int(e.providerFenceSampled));n(e.providerFenceBefore);
    n(e.providerFenceAtDeadline);n(e.providerFenceTarget);
    s(e.frameKind);s(e.stage);n(e.arrivalHost);n(e.deadlineHost);n(e.sourceA);n(e.sourceAHost);
    n(e.gpuBegin);n(e.gpuEnd);n(e.gpuFrequency);n(e.presentCallHost);
    out+='\n';
}

class StreamRecording {
public:
    static constexpr size_t pendingLimit=262144;
    static constexpr uint64_t capacity=200000000;

    bool active() const noexcept {return active_.load(std::memory_order_acquire);}

    bool start(const std::filesystem::path& path,std::chrono::milliseconds maxDuration) noexcept {
        std::lock_guard control(controlMutex_);
        if(active())return false;
        // A recording that ended on its own still owns a finished thread.
        if(writer_.joinable())writer_.join();
        try{
            if(!path.is_absolute())return false;
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(),error);
            if(std::filesystem::exists(path,error)||error)return false;
            auto partial=path;partial+=L".partial";
            const HANDLE file=CreateFileW(partial.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)return false;
            {
                std::lock_guard lock(mutex_);
                path_=path;partial_=partial;file_=file;
                maxDuration_=maxDuration.count()>0?maxDuration:std::chrono::minutes(15);
                startedHost_=tick100ns(Clock::now());startedUtcMs_=unixMsNow();
                deadline_=Clock::now()+maxDuration_;
                endedHost_=0;recorded_=0;dropped_=0;durationReached_=false;published_=false;
                stopping_=false;failed_=false;
                pending_.clear();pending_.reserve(16384);
                headerBytes_=0;
            }
            if(!writeHeader(false)){
                CloseHandle(file);DeleteFileW(partial.c_str());
                std::lock_guard lock(mutex_);file_=INVALID_HANDLE_VALUE;return false;
            }
            active_.store(true,std::memory_order_release);
            writer_=std::thread([this]{run();});
            return true;
        }catch(...){
            active_.store(false,std::memory_order_release);
            return false;
        }
    }

    DiagnosticTraceStatus stop() noexcept {
        std::lock_guard control(controlMutex_);
        if(writer_.joinable()){
            {std::lock_guard lock(mutex_);stopping_=true;}
            wake_.notify_one();
            writer_.join();
        }
        return status();
    }

    DiagnosticTraceStatus status() const noexcept {
        std::lock_guard lock(mutex_);
        DiagnosticTraceStatus result;
        result.active=active();result.published=published_;
        result.recorded=recorded_+pending_.size();result.dropped=dropped_;
        result.startedHost=startedHost_;result.endedHost=endedHost_;
        result.startedUtcMs=startedUtcMs_;result.durationReached=durationReached_;
        return result;
    }

    void add(const FrameTraceEvent& event) noexcept {
        if(!active())return;
        std::lock_guard lock(mutex_);
        if(!active()||stopping_)return;
        if(pending_.size()>=pendingLimit||recorded_+pending_.size()>=capacity){++dropped_;return;}
        try{pending_.push_back(event);}catch(...){++dropped_;return;}
        if(!pending_.back().host100ns)pending_.back().host100ns=tick100ns(Clock::now());
        if(pending_.size()==pendingLimit/2)wake_.notify_one();
    }

private:
    // Fixed-width header so the counters can be rewritten in place.
    std::string header(bool final) const {
        char line[96];
        std::string out;
        const auto put=[&](const char* key,int64_t value){
            std::snprintf(line,sizeof(line),"# %s=%020lld\n",key,static_cast<long long>(value));out+=line;
        };
        out+="# schema=veyra-frame-trace/1\n";
        std::snprintf(line,sizeof(line),"# pid=%lu\n",GetCurrentProcessId());out+=line;
        out+="# host_ticks_per_second=10000000\n";
        put("started_host",startedHost_);
        put("ended_host",final?endedHost_:startedHost_);
        put("requested_ms",maxDuration_.count());
        out+=final&&durationReached_?"# termination=duration\n":"# termination=shutdown\n";
        put("recorded",final?int64_t(recorded_):0);
        put("dropped",final?int64_t(dropped_):0);
        put("capacity",int64_t(capacity));
        put("started_utc_ms",startedUtcMs_);
        out+="# writer=veyra-diagnostic-stream/2\n";
        out+=final?"# finalized=1\n":"# finalized=0\n";
        out+=kColumns;
        return out;
    }

    bool writeAll(const std::string& bytes) noexcept {
        size_t offset=0;
        while(offset<bytes.size()){
            DWORD written=0;
            const auto chunk=DWORD(std::min<size_t>(bytes.size()-offset,1u<<24));
            if(!WriteFile(file_,bytes.data()+offset,chunk,&written,nullptr)||written!=chunk)return false;
            offset+=chunk;
        }
        return true;
    }

    bool writeHeader(bool final) noexcept {
        try{
            const auto text=header(final);
            if(final){
                if(text.size()!=headerBytes_)return false;
                LARGE_INTEGER zero{};
                if(!SetFilePointerEx(file_,zero,nullptr,FILE_BEGIN))return false;
                return writeAll(text);
            }
            headerBytes_=text.size();
            return writeAll(text);
        }catch(...){return false;}
    }

    void run() noexcept {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::vector<FrameTraceEvent> batch;batch.reserve(16384);
        std::string text;
        bool stoppingNow=false;
        while(!stoppingNow){
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock,std::chrono::milliseconds(200),[&]{return stopping_||pending_.size()>=pendingLimit/2;});
                const auto now=Clock::now();
                if(now>=deadline_&&!stopping_){durationReached_=true;stopping_=true;}
                if(stopping_){
                    // Seal producers before the last batch is taken.
                    active_.store(false,std::memory_order_release);
                    endedHost_=tick100ns(std::min(now,deadline_));
                    stoppingNow=true;
                }
                batch.swap(pending_);
            }
            if(!batch.empty()&&!failed_){
                try{
                    text.clear();text.reserve(batch.size()*220);
                    for(const auto& e:batch)appendRow(text,e);
                    if(writeAll(text)){std::lock_guard lock(mutex_);recorded_+=batch.size();}
                    else{std::lock_guard lock(mutex_);failed_=true;dropped_+=batch.size();}
                }catch(...){std::lock_guard lock(mutex_);failed_=true;dropped_+=batch.size();}
            }
            batch.clear();
        }
        bool ok=!failed_&&writeAll("# complete=1\n")&&writeHeader(true)&&FlushFileBuffers(file_);
        CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
        // No replacement flag: an existing final file is never overwritten.
        ok=ok&&MoveFileExW(partial_.c_str(),path_.c_str(),MOVEFILE_WRITE_THROUGH);
        {std::lock_guard lock(mutex_);published_=ok;}
        if(!ok)OutputDebugStringW(L"Veyra diagnostic trace: could not publish the CSV; the .partial file is kept.\n");
    }

    std::mutex controlMutex_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread writer_;
    std::atomic<bool> active_=false;
    bool stopping_=false,failed_=false,durationReached_=false,published_=false;
    std::filesystem::path path_,partial_;
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::chrono::milliseconds maxDuration_{0};
    Clock::time_point deadline_{};
    int64_t startedHost_=0,endedHost_=0,startedUtcMs_=0;
    uint64_t recorded_=0,dropped_=0;
    size_t headerBytes_=0;
    std::vector<FrameTraceEvent> pending_;
};

StreamRecording& stream() noexcept {
    // Leaked on purpose (see the class comment).
    static auto* instance=new StreamRecording();
    return *instance;
}
std::atomic<bool> streamUsed=false;
}

bool privateFrameTraceActive() noexcept {
    const auto* current=recording();
    if(current&&current->active())return true;
    return streamUsed.load(std::memory_order_relaxed)&&stream().active();
}
void recordPrivateFrame(const FrameTraceEvent& event) noexcept {
    if(auto* current=recording())current->add(event);
    if(streamUsed.load(std::memory_order_relaxed))stream().add(event);
}

bool startDiagnosticTrace(const std::filesystem::path& path,std::chrono::milliseconds maxDuration) noexcept {
    auto& recorder=stream();
    streamUsed.store(true,std::memory_order_relaxed);
    return recorder.start(path,maxDuration);
}
DiagnosticTraceStatus stopDiagnosticTrace() noexcept {
    if(!streamUsed.load(std::memory_order_relaxed))return {};
    return stream().stop();
}
DiagnosticTraceStatus diagnosticTraceStatus() noexcept {
    if(!streamUsed.load(std::memory_order_relaxed))return {};
    return stream().status();
}
int64_t traceHostNow() noexcept {return tick100ns(Clock::now());}
void recordDiagnosticMark(uint32_t number) noexcept {
    FrameTraceEvent event;
    event.kind=TraceKind::Mark;event.detail=number;event.count=1;
    event.host100ns=traceHostNow();event.stage="User";
    recordPrivateFrame(event);
}
}
