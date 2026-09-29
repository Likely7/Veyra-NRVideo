#pragma once
#include <algorithm>
namespace veyra::ui {
struct TransportSlot {int x=0,width=0;};
struct TransportLayout {
    TransportSlot open,capture,recent,master,sr,stop,play,mute,volume,subtitle,fullscreen,immersive,lock,mode,minimize,close;
    bool captions;
    // immersiveSlot: the daily immersive toggle sits left of the fullscreen
    // button in every bar that can enter it (daily, and any fullscreen bar
    // reached from daily); the professional workbench bar leaves it out.
    TransportLayout(int width,bool daily,bool fullscreenMode=false,bool immersiveSlot=true):captions(daily&&width>=1040){
        int left=0,right=width;
        auto takeLeft=[&](int size){TransportSlot result{left,size};left+=size+4;return result;};
        auto takeRight=[&](int size){right-=size;TransportSlot result{right,size};right-=4;return result;};
        if(daily){
            open=takeLeft(captions?76:32);capture=takeLeft(captions?76:32);recent=takeLeft(32);left+=10;
            master=takeLeft(captions?114:36);sr=takeLeft(captions?70:60);
            close=takeRight(26);minimize=takeRight(26);right-=8;mode=takeRight(captions?126:32);
        }
        // The toggle is icon-only like fullscreen and lock (its tooltip carries
        // the wording). The immersive fullscreen bar carries every daily slot
        // plus the toggle; below 728 dip the lock button no longer fits beside
        // the centred play/stop pair (right group 342 + play half-width 22 >
        // width/2), so it yields there and Ctrl+L remains the way to lock.
        fullscreen=takeRight(32);if(immersiveSlot)immersive=takeRight(32);if(fullscreenMode&&!(daily&&immersiveSlot&&width<728))lock=takeRight(32);subtitle=takeRight(32);right-=6;volume=takeRight(captions?76:48);mute=takeRight(32);
        int center=width/2;play={center-22,44};stop={center-62,32};
        if(!daily&&width<480){play={0,44};stop={48,32};}
    }
};
}
