#pragma once
#include <cstdint>

namespace veyra::source {
// A driver can continue advancing nominal PTS while delivering substantially
// fewer callbacks. Use both clocks and ignore runs with our own queue drops.
class CaptureDriverCadence {
public:
    static constexpr int64_t kSecond=10'000'000;

    void reset(){*this={};}

    bool observe(int64_t host,int64_t pts,int64_t age,int64_t period,uint64_t drops){
        const auto delta=pts-lastPts_;
        const bool continuous=window_&&host>lastHost_&&host-lastHost_<kSecond&&drops==drops_&&
            period>0&&delta>=period*85/100&&delta<=period*115/100;
        lastPts_=pts;
        lastHost_=host;
        drops_=drops;
        if(!continuous||age<kSecond){
            window_=host;
            lowSince_=0;
            intervals_=0;
            return false;
        }

        ++intervals_;
        const auto elapsed=host-window_;
        if(elapsed<kSecond)return false;
        const bool low=intervals_>=3&&int64_t(intervals_)*period*5<elapsed*3;
        if(!low)lowSince_=0;
        else if(!lowSince_)lowSince_=window_;
        window_=host;
        intervals_=0;
        if(!lowSince_||host-lowSince_<5*kSecond)return false;
        lowSince_=0;
        return true;
    }
private:
    int64_t window_=0,lastHost_=0,lastPts_=0,lowSince_=0;
    uint64_t drops_=0;
    unsigned intervals_=0;
};
}
