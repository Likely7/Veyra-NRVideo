#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <limits>
#include <cmath>
#include <algorithm>
namespace veyra::source {
// The negotiated format is the maximum/cap for VRR capture, not a promise
// that every source frame arrives exactly at that interval. A 120 Hz format
// may continuously deliver 48-120 Hz content. Keep long stalls discontinuous.
inline double captureMaximumContinuousGap(double nominalFps){
    return std::max(.1,std::isfinite(nominalFps)&&nominalFps>0?2.5/nominalFps:0.1);
}
inline bool captureClockBreak(double previous,double current,double nominalFps){
    return !std::isfinite(previous)||!std::isfinite(current)||current<=previous||
        current-previous>captureMaximumContinuousGap(nominalFps);
}
class CaptureDriverDiscontinuity {
    unsigned continuousFlags_=0;
public:
    void reset(){continuousFlags_=0;}
    bool observe(bool flagged,bool native,bool completeTime,bool havePrevious,
                 double ptsDelta,double arrivalDelta,double nominalFps){
        // Some raw-video drivers leave the flag set on every sample. Only
        // discount a sustained flag when both clocks demonstrate continuity.
        const double interval=nominalFps>0?1.0/nominalFps:0;
        const bool continuous=native&&completeTime&&havePrevious&&interval>0&&
            std::isfinite(ptsDelta)&&std::isfinite(arrivalDelta)&&
            ptsDelta>=interval*.5&&ptsDelta<captureMaximumContinuousGap(nominalFps)&&
            arrivalDelta>0&&arrivalDelta<captureMaximumContinuousGap(nominalFps)&&
            std::abs(ptsDelta-arrivalDelta)<=std::max(.005,ptsDelta*.5);
        if(!flagged||!continuous){reset();return flagged;}
        if(continuousFlags_<3)++continuousFlags_;
        return continuousFlags_<3;
    }
};
inline bool captureDiscontinuity(bool pending,bool retained,bool driver,bool havePrevious,
                                 double previous,double current,double nominalFps){
    return (pending&&retained)||driver||
        (havePrevious&&captureClockBreak(previous,current,nominalFps));
}
inline pipeline::Rational captureDuration(int64_t start,int64_t end,bool completeSampleTime,int64_t nominal100ns){
    if(completeSampleTime&&end>start){
        const auto delta=uint64_t(end)-uint64_t(start);
        if(delta<=uint64_t((std::numeric_limits<int64_t>::max)()))return {int64_t(delta),10000000};
    }
    return nominal100ns>0?pipeline::Rational{nominal100ns,10000000}:pipeline::Rational::unknown();
}
}
