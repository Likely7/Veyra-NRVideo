#include "veyra/Log.h"
#include "veyra/diagnostics/Redaction.h"
#include <algorithm>
#include <sstream>
#include <optional>
#include <regex>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <share.h>

namespace veyra {

namespace {
thread_local diagnostics::DiagnosticEvent threadDiagnosticContext;

const char* levelTag(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    default: return "?????";
    }
}

std::string timestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
    const int milliseconds = static_cast<int>(ms % 1000);
    std::tm utc{};
    gmtime_s(&utc, &seconds);
    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
        utc.tm_hour, utc.tm_min, utc.tm_sec, milliseconds);
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    if(writer_.joinable()){
        flush();
        {std::lock_guard lock(mutex_);stopping_=true;}
        writerWake_.notify_one();writer_.join();
        {std::lock_guard lock(mutex_);async_=false;}
    }
    closeFile();
}

void Logger::setRotation(uint64_t startBytes, uint64_t runningBytes)
{
    std::lock_guard<std::mutex> lock(sinkMutex_);
    rotateAtOpen_=startBytes;rotateWhileRunning_=runningBytes;nextRotation_=runningBytes;
}

namespace {
std::filesystem::path oldLogPath(const std::filesystem::path& path)
{
    return path.parent_path()/(path.stem().wstring()+L".old"+path.extension().wstring());
}
}

bool Logger::openFile(const std::wstring& path, bool append)
{
    flush();
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(sinkMutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    path_=path;
    // Field logs reached 129-186 MB by appending every session forever (audit
    // 2026-10-15). A big log from earlier sessions moves aside once at start; a
    // second running instance holds the file, so the rename fails and nothing moves.
    uint64_t movedBytes=0;
    if(append&&rotateAtOpen_){
        const auto size=std::filesystem::file_size(fsPath,ec);
        if(!ec&&size>=rotateAtOpen_&&MoveFileExW(path.c_str(),oldLogPath(fsPath).c_str(),MOVEFILE_REPLACE_EXISTING))movedBytes=size;
    }
    // Diagnostics may read a running session without stopping capture. Keep
    // concurrent writers excluded so another instance cannot truncate this log.
    file_ = _wfsopen(path.c_str(), append ? L"ab" : L"wb", _SH_DENYWR);
    if (file_ == nullptr) {
        return false;
    }
    std::setvbuf(file_,nullptr,_IOFBF,64*1024);
    bufferedLogBytes_=0;lastFileFlushTick_=GetTickCount64();
    const auto size=append?std::filesystem::file_size(fsPath,ec):0;
    fileBytes_=ec?0:size;nextRotation_=rotateWhileRunning_;
    if(movedBytes){
        std::fprintf(file_,"%s t=%lu [INFO ] [log] earlier sessions moved to %s (%llu bytes)\n",timestampUtc().c_str(),GetCurrentThreadId(),
            oldLogPath(fsPath).filename().string().c_str(),static_cast<unsigned long long>(movedBytes));
        std::fflush(file_);
    }
    return true;
}

// Caller holds sinkMutex_. Starts a fresh file under the same name; when the rename is
// refused (the old copy is open elsewhere) logging continues in the current file and
// the next attempt waits for another rotateWhileRunning_ bytes.
bool Logger::rotateLocked(const char* reason)
{
    if(path_.empty()||file_==nullptr)return false;
    const std::filesystem::path current(path_);
    const uint64_t bytes=fileBytes_;
    std::fflush(file_);std::fclose(file_);file_=nullptr;
    const bool moved=MoveFileExW(path_.c_str(),oldLogPath(current).c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;
    const DWORD error=moved?0:GetLastError();
    file_=_wfsopen(path_.c_str(),L"ab",_SH_DENYWR);
    if(file_==nullptr)return false;
    std::setvbuf(file_,nullptr,_IOFBF,64*1024);
    std::error_code ec;const auto size=std::filesystem::file_size(current,ec);
    fileBytes_=ec?0:size;nextRotation_=moved?rotateWhileRunning_:fileBytes_+rotateWhileRunning_;
    if(moved)std::fprintf(file_,"%s t=%lu [INFO ] [log] rotated reason=%s; the earlier part of this session is in %s (%llu bytes)\n",timestampUtc().c_str(),
        GetCurrentThreadId(),reason,oldLogPath(current).filename().string().c_str(),static_cast<unsigned long long>(bytes));
    else std::fprintf(file_,"%s t=%lu [WARN ] [log] rotation refused error=%lu; continuing in this file\n",timestampUtc().c_str(),GetCurrentThreadId(),error);
    std::fflush(file_);bufferedLogBytes_=0;lastFileFlushTick_=GetTickCount64();
    return moved;
}

void Logger::closeFile()
{
    flush();
    std::lock_guard<std::mutex> lock(sinkMutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::setConsoleEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(sinkMutex_);
    consoleEnabled_ = enabled;
}

bool Logger::startAsync()
{
    std::lock_guard lock(mutex_);
    if(async_)return true;
    try{writer_=std::thread([this]{writerLoop();});async_=true;return true;}
    catch(...){return false;}
}

Logger::AsyncStats Logger::asyncStats()
{
    std::lock_guard lock(mutex_);
    return {pending_.size(),queuedBytes_,highWaterBytes_,dropped_};
}

void Logger::write(LogLevel level, const char* component, const std::string& message)
{
    const unsigned long threadId = GetCurrentThreadId();
    std::string line = std::format("{} t={} [{}] [{}] {}",
        timestampUtc(), threadId, levelTag(level), component, message);

    // Diagnostic extraction runs before taking the global lock and uses
    // precompiled expressions: this path is reached from the capture callback
    // and audio threads, and compiling four regexes under the lock stalled
    // them during warning bursts (sweep 2026-09-22 C1).
    std::optional<diagnostics::DiagnosticEvent> pendingEvent;
    if(level==LogLevel::Warn||level==LogLevel::Error){
        static const std::regex hrPattern("(?:HRESULT|hr)[=: ]+(0x[0-9a-f]+|[0-9]+)",std::regex::icase|std::regex::optimize);
        static const std::regex sehPattern("seh[=: ]+(0x[0-9a-f]+|[0-9]+)",std::regex::icase|std::regex::optimize);
        static const std::regex nvofPattern("(?:status|st)[=: ]+(0x[0-9a-f]+|[0-9]+)",std::regex::icase|std::regex::optimize);
        static const std::regex ngxPattern("(?:result[=: ]+|failed +)(0x[0-9a-f]+|[0-9]+)",std::regex::icase|std::regex::optimize);
        auto event=threadDiagnosticContext;event.timestamp=timestampUtc();event.severity=levelTag(level);event.component=component;
        auto extract=[&](const std::regex& pattern)->std::optional<uint64_t>{std::smatch m;if(std::regex_search(message,m,pattern)){try{return std::stoull(m[1].str(),nullptr,m[1].str().starts_with("0x")?16:10);}catch(...){}}return {};};
        if(auto c=extract(hrPattern))event.hresult=c;
        if(auto c=extract(sehPattern))event.seh=c;
        if(event.component.find("nvof")!=std::string::npos){if(auto c=extract(nvofPattern))event.nvof=c;}
        if(event.component=="ngx"||message.find("evaluate")!=std::string::npos){if(auto c=extract(ngxPattern))event.ngx=c;}
        event.stage=event.stage.empty()?"runtime":event.stage;event.message=message;event.fingerprint=event.component+"|"+event.stage+"|"+diagnostics::redact(message);
        pendingEvent=std::move(event);
    }
    std::unique_lock lock(mutex_);
    if(pendingEvent){latestProblem_=pendingEvent->component+": "+pendingEvent->message;diagnostics_.add(std::move(*pendingEvent));}
    if(async_){
        // Bounded in both line count and bytes, including a single huge event.
        // Preserve recent warnings/errors ahead of verbose frame telemetry.
        // A sustained blocked sink cannot have all three of lossless logging,
        // bounded memory and nonblocking producers: any loss is counted here
        // and reported by the writer and local diagnostic report.
        if(line.size()>kMaxQueuedBytes){line.resize(kMaxQueuedBytes-48);line+=" [log-backpressure: oversized event truncated]";++dropped_;}
        while(!pending_.empty()&&(pending_.size()>=kMaxQueuedLines||queuedBytes_+line.size()>kMaxQueuedBytes)){
            auto victim=std::find_if(pending_.begin(),pending_.end(),[](const auto& p){return p.level<LogLevel::Warn;});
            if(victim==pending_.end()&&level<LogLevel::Warn){++dropped_;lock.unlock();writerWake_.notify_one();return;}
            if(victim==pending_.end())victim=pending_.begin();
            queuedBytes_-=victim->text.size();pending_.erase(victim);++dropped_;
        }
        queuedBytes_+=line.size();highWaterBytes_=std::max(highWaterBytes_,queuedBytes_);
        pending_.push_back({level,++issued_,std::move(line)});
        lock.unlock();writerWake_.notify_one();return;
    }
    lock.unlock();
    std::lock_guard sinkLock(sinkMutex_);
    writeSink(level,line);
}

// Caller owns sinkMutex_. The writer never holds mutex_ during output I/O.
void Logger::writeSink(LogLevel level,const std::string& line)
{
    if (consoleEnabled_) {
        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
    }
    if (file_ != nullptr) {
        std::fprintf(file_, "%s\n", line.c_str());
        bufferedLogBytes_+=line.size()+1;fileBytes_+=line.size()+1;
        if(nextRotation_&&fileBytes_>=nextRotation_){rotateLocked("size");return;}
        const auto tick=GetTickCount64();
        // Keep every event, but avoid a write/flush syscall for every pass.
        // Errors and explicit crash/teardown flush() calls remain immediate.
        if(level==LogLevel::Warn||level==LogLevel::Error||bufferedLogBytes_>=64*1024||tick-lastFileFlushTick_>=250){std::fflush(file_);bufferedLogBytes_=0;lastFileFlushTick_=tick;}
    }
}

void Logger::flushSink()
{
    if(file_){std::fflush(file_);bufferedLogBytes_=0;lastFileFlushTick_=GetTickCount64();}
}

void Logger::writerLoop()
{
    for(;;){
        PendingLine next{};uint64_t dropped=0,ticket=0;
        {
            std::unique_lock lock(mutex_);
            writerWake_.wait_for(lock,std::chrono::milliseconds(250),[&]{return stopping_||!pending_.empty()||flushRequested_>flushCompleted_;});
            if(stopping_&&pending_.empty())break;
            if(dropped_!=reportedDropped_){dropped=dropped_-reportedDropped_;reportedDropped_=dropped_;}
            if(!pending_.empty()){
                next=std::move(pending_.front());pending_.pop_front();queuedBytes_-=next.text.size();
            }
            if(flushRequested_>flushCompleted_&&written_>=flushTarget_)ticket=flushRequested_;
        }
        {
            std::lock_guard sinkLock(sinkMutex_);
            if(dropped)writeSink(LogLevel::Warn,std::format("{} t={} [WARN ] [log-backpressure] dropped={} (bounded asynchronous output queue; realtime producers continued)",timestampUtc(),GetCurrentThreadId(),dropped));
            if(next.sequence)writeSink(next.level,next.text);
            if(ticket||!next.sequence)flushSink();
        }
        {
            std::lock_guard lock(mutex_);
            if(next.sequence)written_=next.sequence;
            if(ticket){flushCompleted_=ticket;flushDone_.notify_all();}
        }
    }
}

namespace log {

void trace(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Trace, component, message);
}

void info(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Info, component, message);
}

void warn(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Warn, component, message);
}

void error(const char* component, const std::string& message)
{
    Logger::instance().write(LogLevel::Error, component, message);
}

bool verboseFrameLogs()
{
    static const bool enabled = [] {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS", value, 2) == 1 && value[0] == L'1';
    }();
    return enabled;
}

} // namespace log


void Logger::diagnosticContext(diagnostics::DiagnosticEvent event){threadDiagnosticContext=std::move(event);}
std::string Logger::latestProblem(){std::lock_guard lock(mutex_);return diagnostics::redact(latestProblem_);}
void Logger::recordFrame(diagnostics::FrameTraceEvent event){std::lock_guard lock(traceMutex_);frameTrace_.add(event);}
std::pair<size_t,uint64_t> Logger::frameTraceSize(){std::lock_guard lock(traceMutex_);return {frameTrace_.size(),frameTrace_.overwritten()};}
std::string Logger::diagnosticReport(){std::ostringstream o;o<<"Veyra 本地诊断（复制前脱敏预览；不会上传）\n";
    {std::lock_guard lock(mutex_);
    o<<"Log output: async="<<async_<<" queuedLines="<<pending_.size()<<" queuedBytes="<<queuedBytes_<<" highWaterBytes="<<highWaterBytes_<<" dropped="<<dropped_<<'\n';
    for(size_t i=0;i<diagnostics_.size();++i){const auto& e=diagnostics_.events()[i];const auto& r=e.resolution;
        auto code=[](std::optional<uint64_t> v){return v?std::format("0x{:X}",*v):std::string("未提供");};
        o<<"\n时间="<<e.timestamp<<" 严重级别="<<e.severity<<"\n组件="<<e.component<<" 阶段="<<e.stage<<" 次数="<<e.occurrenceCount<<"\n"<<e.message<<"\nHRESULT="<<code(e.hresult)<<" NGX="<<code(e.ngx)<<" NVOF="<<code(e.nvof)<<" SEH="<<code(e.seh)<<"\n";
        o<<"source="<<r.source.width<<'x'<<r.source.height<<" base="<<r.base.width<<'x'<<r.base.height<<" NR="<<r.nr.width<<'x'<<r.nr.height<<" flow="<<r.flow.width<<'x'<<r.flow.height<<" FG="<<r.fg.width<<'x'<<r.fg.height<<" output="<<r.output.width<<'x'<<r.output.height<<"\nepoch="<<e.identity.epoch<<" frame="<<e.identity.sourceFrameId<<" batch="<<e.batch<<" subframe="<<e.subframe<<" revision="<<e.identity.settingsRevision<<"\nruntime="<<e.runtimeHash<<" flow="<<e.flowApplied<<" fallback="<<e.fallbackReason<<"\n";
    }}
    std::vector<diagnostics::FrameTraceEvent> trace;uint64_t overwritten=0;
    {std::lock_guard lock(traceMutex_);trace=frameTrace_.snapshot();overwritten=frameTrace_.overwritten();}
    o<<"\nFrame trace: records="<<trace.size()<<" capacity="<<diagnostics::FrameTrace::capacity<<" overwritten="<<overwritten
     <<"\nHost timestamps are monotonic 100ns; Ready is observed GPU completion, Present is submission, not scanout."
     <<"\nDetail: Submitted=skipped FG count, Ready=invalid FG count, Present=subframe, Gpu=stage index, Reset=reason enum."
     <<" Count: Submitted=FG evaluated, Ready=valid FG, Present=1, Reset=outcome enum, Cancelled=unpresented frames."
     <<" FrameReady: detail=subframe, ms=CPU-observed readiness since processing start (upper bound)."
     <<" Discarded: detail=subframe, count=PreviewFrameReadiness enum, ms=deadline lateness.\n";
    for(const auto& e:trace)o<<"event="<<diagnostics::traceKindName(e.kind)<<" host="<<e.host100ns<<" session="<<e.session
        <<" revision="<<e.identity.settingsRevision<<" epoch="<<e.identity.epoch<<" source="<<e.identity.sourceFrameId
        <<" batch="<<e.batch<<" fence="<<e.fence<<" pts="<<e.pts100ns<<" detail="<<e.detail<<" count="<<e.count<<" ms="<<e.milliseconds
        <<" decodedHost="<<e.decodedHost<<" processHost="<<e.processHost<<" readyHost="<<e.readyHost
        <<" presentBeginHost="<<e.presentBeginHost<<" presentEndHost="<<e.presentEndHost
        <<" entryDeviationMs="<<e.entryDeviationMs<<" returnDeviationMs="<<e.returnDeviationMs
        <<" queueDepth="<<e.queueDepth<<" mediaDeviationValid="<<e.mediaDeviationValid
        <<" providerInstance="<<e.providerInstance<<" providerCycle="<<e.providerCycle<<" preparationCycle="<<e.preparationCycle
        <<" providerCallerRva="<<e.providerCallerRva<<" providerScheduleBeginHost="<<e.providerScheduleBeginHost
        <<" providerFenceSampled="<<e.providerFenceSampled<<" providerFenceBefore="<<e.providerFenceBefore
        <<" providerFenceAtDeadline="<<e.providerFenceAtDeadline<<" providerFenceTarget="<<e.providerFenceTarget<<'\n';
    return diagnostics::redact(o.str());
}
void Logger::flush()
{
    {
        std::unique_lock lock(mutex_);
        if(async_){
            // A fault in the output worker can reach the process crash filter.
            // It cannot wait for its own queue barrier (or reacquire its sink).
            if(writer_.get_id()==std::this_thread::get_id())return;
            const auto ticket=++flushRequested_;flushTarget_=issued_;
            writerWake_.notify_one();
            flushDone_.wait(lock,[&]{return flushCompleted_>=ticket;});
            return;
        }
    }
    std::lock_guard sinkLock(sinkMutex_);flushSink();
}
} // namespace veyra
