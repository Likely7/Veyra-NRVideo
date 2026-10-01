#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace veyra::ui {
// A single reversible animation, never a queue of layout operations.
struct WorkspaceTransition {
    float value=0,from=0,to=0;uint64_t started=0;bool running=false;
    static constexpr uint64_t duration=320;
    float sample(uint64_t now){if(!running)return value;float t=std::clamp(float(now-started)/duration,0.f,1.f);float eased=t*t*t*(t*(t*6.f-15.f)+10.f);value=from+(to-from)*eased;if(t>=1){value=to;running=false;}return value;}
    void start(bool professional,uint64_t now){sample(now);from=value;to=professional?1.f:0.f;started=now;running=from!=to;}
    void finish(){value=to;running=false;}
    int mix(int a,int b)const{return int(std::lround(a+(b-a)*value));}
};
}
