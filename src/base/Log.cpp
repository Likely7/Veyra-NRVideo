#include "veyra/Log.h"
#include "veyra/diagnostics/Redaction.h"
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
    closeFile();
}

bool Logger::openFile(const std::wstring& path, bool append)
{
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    // Diagnostics may read a running session without stopping capture. Keep
    // concurrent writers excluded so another instance cannot truncate this log.
    file_ = _wfsopen(path.c_str(), append ? L"ab" : L"wb", _SH_DENYWR);
    if (file_ == nullptr) {
        return false;
    }
    std::setvbuf(file_,nullptr,_IOFBF,64*1024);
    bufferedLogBytes_=0;lastFileFlushTick_=GetTickCount64();
    return true;
}

void Logger::closeFile()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::setConsoleEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

void Logger::write(LogLevel level, const char* component, const std::string& message)
{
    const unsigned long threadId = GetCurrentThreadId();
    const std::string line = std::format("{} t={} [{}] [{}] {}",
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
    std::lock_guard<std::mutex> lock(mutex_);
    if(pendingEvent){latestProblem_=pendingEvent->component+": "+pendingEvent->message;diagnostics_.add(std::move(*pendingEvent));}
    if (consoleEnabled_) {
        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
    }
    if (file_ != nullptr) {
        std::fprintf(file_, "%s\n", line.c_str());
        bufferedLogBytes_+=line.size()+1;
        const auto tick=GetTickCount64();
        // Keep every event, but avoid a write/flush syscall for every pass.
        // Errors and explicit crash/teardown flush() calls remain immediate.
        if(level==LogLevel::Warn||level==LogLevel::Error||bufferedLogBytes_>=64*1024||tick-lastFileFlushTick_>=250){std::fflush(file_);bufferedLogBytes_=0;lastFileFlushTick_=tick;}
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
std::string Logger::diagnosticReport(){std::ostringstream o;o<<"Veyra local diagnostics (redacted preview before copying; not uploaded)\n";
    {std::lock_guard lock(mutex_);
    for(size_t i=0;i<diagnostics_.size();++i){const auto& e=diagnostics_.events()[i];const auto& r=e.resolution;
        auto code=[](std::optional<uint64_t> v){return v?std::format("0x{:X}",*v):std::string("not provided");};
        o<<"\ntime="<<e.timestamp<<" severity="<<e.severity<<"\ncomponent="<<e.component<<" stage="<<e.stage<<" count="<<e.occurrenceCount<<"\n"<<e.message<<"\nHRESULT="<<code(e.hresult)<<" NGX="<<code(e.ngx)<<" NVOF="<<code(e.nvof)<<" SEH="<<code(e.seh)<<"\n";
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fflush(file_);
        bufferedLogBytes_=0;lastFileFlushTick_=GetTickCount64();
    }
}
} // namespace veyra
