// SPDX-License-Identifier: GPL-3.0-only
// Adapted from the private Veyra frame trace patch supplied by the Magpie author.
#include "veyra/diagnostics/FrameTrace.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

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

bool privateFrameTraceActive() noexcept {
    const auto* current=recording();
    return current&&current->active();
}
void recordPrivateFrame(const FrameTraceEvent& event) noexcept {
    if(auto* current=recording())current->add(event);
}
}
