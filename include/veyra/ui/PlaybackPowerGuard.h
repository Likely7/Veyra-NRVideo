#pragma once
#include <windows.h>
#include <format>
#include "veyra/Log.h"

namespace veyra::ui {
// SetThreadExecutionState must be acquired and cleared on the same UI thread.
class PlaybackPowerGuard {
    bool active_=false;
public:
    ~PlaybackPowerGuard(){update(false);}
    bool active()const{return active_;}
    void update(bool playing){
        if(playing==active_)return;
        const auto flags=ES_CONTINUOUS|(playing?(ES_DISPLAY_REQUIRED|ES_SYSTEM_REQUIRED):0);
        if(!SetThreadExecutionState(flags)){
            log::error("playback-power",std::format("SetThreadExecutionState failed win32={}",GetLastError()));return;
        }
        active_=playing;
        log::info("playback-power",playing?"playback display/system request acquired":"playback display/system request released");
        throttle(playing);
    }
private:
    // While media plays, opt the process out of Windows power throttling
    // (EcoQoS execution speed and timer-resolution throttling). Windows 11
    // applies both to processes it considers background once their window is
    // not in the foreground or is occluded; on affected builds the 1 ms waits
    // of the presentation/capture loops then stretch and live capture with
    // frame generation falls behind after switching to another window (field
    // log 2026-10-04: 60 -> ~22 fps until refocus). Released with playback so
    // an idle player is still throttled normally. Documented opt-out:
    // SetProcessInformation(ProcessPowerThrottling).
    static void throttle(bool playing){
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
        constexpr ULONG PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION=0x4;
#endif
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask=playing?(PROCESS_POWER_THROTTLING_EXECUTION_SPEED|PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION):0;
        state.StateMask=0; // controlled bits off = never throttle; ControlMask 0 = system default
        const bool ok=SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&state,sizeof(state))!=FALSE;
        log::info("playback-power",std::format("process power throttling {} ok={} win32={}",playing?"disabled for playback":"returned to system policy",ok,ok?0ul:GetLastError()));
    }
};
}
