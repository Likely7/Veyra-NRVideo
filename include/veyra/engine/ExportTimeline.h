#pragma once
#include <cmath>
#include <limits>

namespace veyra::engine {
// The export's continuous video timeline over source timestamps. A joined TS or a
// broadcast splice steps back for good: the timeline is realigned once to continue
// at `next` and keeps that offset. A single bad timestamp steps back too, but the
// frame after it returns to the old timeline, so that realignment is undone.
struct ContinuousTimeline {
    double offset=0;
    double last=-std::numeric_limits<double>::infinity();
    double undo=std::numeric_limits<double>::quiet_NaN();
    // `next` is where the stream continues after the previous frame. A time equal to
    // the previous one steps back. A non-finite `raw` continues at `next`.
    double map(double raw,double next,bool* stepped=nullptr){
        bool back=false;
        double t=next;
        if(std::isfinite(raw)){
            t=raw+offset;
            if(!std::isnan(undo)){
                const double old=raw+undo;
                if(old>last&&old<t){offset=undo;t=old;}
                undo=std::numeric_limits<double>::quiet_NaN();
            }
            back=t<=last;
            if(back){undo=offset;offset+=next-t;t=next;}
        }
        if(stepped)*stepped=back;
        last=t;
        return t;
    }
};
}
