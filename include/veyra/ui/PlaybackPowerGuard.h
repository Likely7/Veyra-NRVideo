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
    }
};
}
