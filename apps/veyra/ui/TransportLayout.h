#pragma once
#include <algorithm>
namespace veyra::ui {
struct TransportSlot {int x=0,width=0;};
struct TransportLayout {
    TransportSlot open,capture,recent,master,sr,stop,play,mute,volume,subtitle,fullscreen,lock,mode,minimize,close;
    bool captions;
    TransportLayout(int width,bool daily,bool fullscreenMode=false):captions(daily&&width>=1100){
        int left=0,right=width;
        auto takeLeft=[&](int size){TransportSlot result{left,size};left+=size+4;return result;};
        auto takeRight=[&](int size){right-=size;TransportSlot result{right,size};right-=4;return result;};
        if(daily){
            open=takeLeft(captions?80:32);capture=takeLeft(captions?92:32);recent=takeLeft(32);left+=10;
            master=takeLeft(captions?160:36);sr=takeLeft(captions?78:60);
            close=takeRight(26);minimize=takeRight(26);right-=8;mode=takeRight(captions?154:32);
        }
        fullscreen=takeRight(32);if(fullscreenMode)lock=takeRight(32);subtitle=takeRight(32);right-=6;volume=takeRight(captions?76:48);mute=takeRight(32);
        int center=width/2;play={center-22,44};stop={center-62,32};
        if(!daily&&width<480){play={0,44};stop={48,32};}
    }
};
}
