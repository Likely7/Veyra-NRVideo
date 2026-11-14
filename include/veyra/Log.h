#pragma once

#include <cstdint>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include "veyra/diagnostics/DiagnosticEvent.h"
#include "veyra/diagnostics/FrameTrace.h"

namespace veyra {

enum class LogLevel : uint8_t {
    Trace,
    Info,
    Warn,
    Error,
};

// Process-wide structured logger (Playbook section 17.1).
// Line shape:
//   <UTC ISO8601 ms> t=<tid> [LEVEL] [component] message
// Thread-safe; writes to stdout and, when openFile() was called, to a file.
class Logger {
public:
    static Logger& instance();

    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Creates parent directories; returns false and logs to console on failure.
    bool openFile(const std::wstring& path, bool append = false);
    // Bounded size for the long-running app log (call before openFile). An existing
    // file of at least startBytes, or a file that reaches runningBytes while being
    // written, is renamed to "<name>.old<ext>" (replacing the previous one) and a
    // fresh file is started. 0 disables either check; off unless called, so worker
    // and test logs keep appending.
    void setRotation(uint64_t startBytes, uint64_t runningBytes);
    void closeFile();
    void setConsoleEnabled(bool enabled);
    // Enable before realtime producers start. Output I/O then belongs to a
    // worker; a slow stdout/file never holds the producer/diagnostic mutex.
    // Explicit flush/close remain barriers, including crash/teardown markers.
    bool startAsync();
    struct AsyncStats { size_t queuedLines=0,queuedBytes=0,highWaterBytes=0;uint64_t dropped=0; };
    AsyncStats asyncStats();
    static constexpr size_t kMaxQueuedLines=4096,kMaxQueuedBytes=2*1024*1024;

    void write(LogLevel level, const char* component, const std::string& message);

    // Flush the file sink immediately (staged-teardown markers must survive
    // a crash on the very next call).
    void flush();
    std::string diagnosticReport();
    std::string latestProblem();
    static void diagnosticContext(diagnostics::DiagnosticEvent);
    void recordFrame(diagnostics::FrameTraceEvent event);
    std::pair<size_t,uint64_t> frameTraceSize();

private:
    bool rotateLocked(const char* reason);
    void writeSink(LogLevel level, const std::string& line);
    void flushSink();
    void writerLoop();
    struct PendingLine { LogLevel level;uint64_t sequence;std::string text; };
    std::mutex mutex_;
    std::mutex sinkMutex_;
    std::condition_variable writerWake_,flushDone_;
    std::deque<PendingLine> pending_;
    std::thread writer_;
    bool async_=false,stopping_=false;
    size_t queuedBytes_=0,highWaterBytes_=0;
    uint64_t issued_=0,written_=0,dropped_=0,reportedDropped_=0;
    uint64_t flushRequested_=0,flushCompleted_=0,flushTarget_=0;
    std::mutex traceMutex_;
    diagnostics::FrameTrace frameTrace_;
    std::FILE* file_ = nullptr;
    bool consoleEnabled_ = true;
    uint64_t lastFileFlushTick_=0;
    size_t bufferedLogBytes_=0;
    std::wstring path_;
    uint64_t fileBytes_=0,rotateAtOpen_=0,rotateWhileRunning_=0,nextRotation_=0;
    diagnostics::DiagnosticHistory diagnostics_;std::string latestProblem_;
};

namespace log {

// Flush the log sink to disk immediately (teardown markers).

void trace(const char* component, const std::string& message);
void info(const char* component, const std::string& message);
void warn(const char* component, const std::string& message);
void error(const char* component, const std::string& message);
bool verboseFrameLogs();

// Key=value helper used by call sites that need structured fields.
inline std::string kv(const char* key, uint64_t value)
{
    return std::string(key) + "=" + std::to_string(value);
}

inline std::string kvHex(const char* key, uint64_t value)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return std::string(key) + "=" + buffer;
}

inline std::string kv(const char* key, const std::string& value)
{
    return std::string(key) + "=" + value;
}

} // namespace log

} // namespace veyra
