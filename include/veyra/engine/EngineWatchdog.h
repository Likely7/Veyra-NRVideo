#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "veyra/source/CaptureDriverStage.h"

namespace veyra::engine {
// Engine status while a stall is being reported (the bridge raises it once).
inline constexpr wchar_t kEngineHangStatus[] = L"画面引擎无响应，已记录诊断信息；如画面未恢复，请关闭软件后重新打开";
// Field logs(15), 2026-10-10: the engine owner thread stopped without a single
// log line while capture, audio and the interface kept running, and closing the
// app then waited for that thread forever. The watchdog notices an owner thread
// that stops making progress inside the playback loop, logs where it is blocked
// (call stack as module + offset and nearest export) and writes one small
// minidump beside the log, so the next report names the blocking call. Shutdown
// asks hung() so it can stop waiting for a thread that will not return.
class EngineWatchdog {
public:
    // Interface notice: stalledMs > 0 while blocked, 0 once the loop moves again.
    using Notify = std::function<void(uint64_t stalledMs, bool captureDriver)>;
    explicit EngineWatchdog(Notify notify);
    ~EngineWatchdog();
    EngineWatchdog(const EngineWatchdog&) = delete;
    EngineWatchdog& operator=(const EngineWatchdog&) = delete;
    // Owner thread. Start a fresh capture dump budget for this playback session.
    void beginSession();
    // Capture driver calls are monitored even before/after the playback loop.
    void setCaptureDriverStage(source::CaptureDriverStage);
    // Owner thread, top of every playback-loop iteration.
    void beat();
    // Owner thread, when it leaves the playback loop.
    void disarm();
    // Last completed loop stage (string literals only), written by CpuStallTrace.
    std::atomic<const char*>* stage();
    // A reported stall is still in progress.
    bool hung() const;
    // Where veyra-hang-*.dmp goes (the log directory); empty means no dump.
    static void setDumpDirectory(const std::wstring& directory);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
