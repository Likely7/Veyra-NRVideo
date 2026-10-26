#pragma once
#include "veyra/gfx/NvidiaVrr.h"
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace veyra::engine {
// Refresh rate and NVIDIA variable-refresh state of one monitor.
struct DisplayState {
    HMONITOR monitor=nullptr;
    double refreshHz=0;
    gfx::VrrState vrr{};
    uint64_t generation=0;      // display generation the query ran in
    uint64_t serial=0;          // stored-result counter (all monitors): a holder applies each state once
    double refreshMs=0,vrrMs=0; // driver call wall time on the worker thread
};
// Display states shared by the presenter and the UI (2026-10-25,
// docs/VRR_EVENT_QUERY_PLAN_2026-10-25.md). Nothing re-reads them on a timer:
// holders may call request() every frame or tick, and a query only runs when
// the monitor has no state of the current display generation. invalidate() (a
// display change, the 可变刷新 switch turned on, 重新检测) makes every state
// stale; force is for the session-start check and the VRR confirmation. One
// query runs at a time on a worker thread and the others wait in a queue.
// Reading never waits, except wait(), which only a new presentation chain uses.
// A failed query (an NVAPI error, the window closed or moved meanwhile, an
// exception) is retried at most twice per generation, retryDelay apart.
class DisplayStateService {
public:
    using Clock=std::chrono::steady_clock;
    using Query=std::function<DisplayState(HWND,HMONITOR)>;
    using MonitorOf=std::function<HMONITOR(HWND)>;
    // QueryDisplayConfig refresh + NVAPI VRR of the window's monitor.
    DisplayStateService();
    // Tests: fake query, monitor lookup and retry delay.
    DisplayStateService(Query,MonitorOf,std::chrono::milliseconds retryDelay=std::chrono::seconds(2));
    // Waits for a running query: one driver call, never a queue.
    ~DisplayStateService();
    DisplayStateService(const DisplayStateService&)=delete;
    DisplayStateService& operator=(const DisplayStateService&)=delete;
    uint64_t invalidate(const char* reason);
    uint64_t generation()const;
    // Never waits. True when a query for the window's monitor was started or
    // queued by this call.
    bool request(HWND window,const char* reason,bool force=false);
    // Publishes a finished query and returns the monitor's newest state, current
    // or stale (generation below generation()); none before its first answer.
    std::optional<DisplayState> state(HMONITOR monitor);
    // Waits up to `limit` until the monitor has a state of the current
    // generation, while a query for it runs or waits. New presentation chains only.
    std::optional<DisplayState> wait(HMONITOR monitor,std::chrono::milliseconds limit);
    // A query runs or waits (after publishing a finished one).
    bool busy();
    HMONITOR monitorOf(HWND window)const{return monitorOf_(window);}
    // The real query: values only, never presenter or swap-chain resources. A
    // window that closed or moved to another monitor meanwhile yields monitor=nullptr.
    static DisplayState queryDisplayState(HWND window,HMONITOR monitor);
private:
    struct Job {HWND window=nullptr;HMONITOR monitor=nullptr;uint64_t generation=0;const char* reason="";bool force=false;};
    struct Entry {std::optional<DisplayState> state;uint64_t failedGeneration=0;unsigned failures=0;Clock::time_point retryAt{};};
    bool currentLocked(const Entry&,Clock::time_point now)const;
    void startLocked(Job);
    void collectLocked();
    void failedLocked(Entry&,const Job&,const std::string& why);
    Query query_;
    MonitorOf monitorOf_;
    std::chrono::milliseconds retryDelay_;
    mutable std::mutex mutex_;
    std::map<HMONITOR,Entry> entries_;
    std::vector<Job> queue_;
    std::optional<Job> running_;
    std::shared_future<DisplayState> result_;
    uint64_t generation_=1,serial_=0;
};
}
