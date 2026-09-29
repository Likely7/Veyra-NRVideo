#pragma once
#include <cstdint>
namespace veyra::ui {
// Visibility policy for the transport bar while it floats over the video:
// in fullscreen, and in the daily immersive sub-state (windowed or
// fullscreen). Pure so the contract test can drive every branch; the shell
// samples the inputs from its 100 ms timer and its message loop.
struct OverlayTransportInput {
    uint64_t now=0;            // GetTickCount64 at the sample
    uint64_t lastActivity=0;   // last pointer/key activity that revealed the bar
    uint64_t holdUntil=0;      // grace after entering the state or starting up
    bool visible=true;         // the bar is currently on screen
    bool immersive=false;      // immersive rule set (else the plain fullscreen one)
    bool locked=false;         // fullscreen lock: never reveal on input
    bool pointerInBand=false;  // pointer inside the window and in the bottom band
    bool interacting=false;    // popup/menu open or a mouse capture in progress
    bool mediaOpen=true;       // false while nothing is open or playback failed
    bool toastActive=false;    // a status toast is showing in the bar
    bool sizing=false;         // inside the system SC_SIZE loop (windowed immersive)
    bool bandArmed=true;       // false after sizing until the pointer has left the band
};
struct OverlayTransport {
    static constexpr uint64_t idleMs=1600;   // hide this long after the last reveal
    static constexpr uint64_t graceMs=3000;  // keep the bar after entering/startup
    static constexpr int bandDip=98;         // bottom reveal band: bar + 10 dip
    // Plain fullscreen reveals on any pointer motion; immersive only when the
    // pointer enters the bottom band, so a mouse crossing a windowed player
    // does not flash the bar. Keys reveal in both (transport feedback).
    // Dragging the bottom edge or a corner puts the pointer in the band, so
    // immersive keeps the bar away during the system sizing loop and, once it
    // ends, until the pointer has left the band and comes back: the picture's
    // bottom edge stays visible for alignment.
    static bool revealOnPointer(const OverlayTransportInput& in){return !in.locked&&!(in.immersive&&in.sizing)&&(!in.immersive||(in.pointerInBand&&in.bandArmed));}
    static bool revealOnKey(const OverlayTransportInput& in){return !in.locked&&!(in.immersive&&in.sizing);}
    // The visible bar goes away once the pointer has left the band and no
    // reveal happened for idleMs. Immersive additionally keeps it while
    // nothing is open, while a toast shows, and during the entry grace.
    static bool shouldHide(const OverlayTransportInput& in){
        if(in.immersive&&in.sizing)return in.visible;
        if(!in.visible||in.interacting||in.pointerInBand)return false;
        if(in.immersive&&(!in.mediaOpen||in.toastActive||in.now<in.holdUntil))return false;
        return in.now-in.lastActivity>idleMs;
    }
};
}
