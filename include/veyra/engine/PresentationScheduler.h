#pragma once
#include <cstdint>
namespace veyra::engine {
// Maps source timestamps to a monotonic host timeline. File playback keeps a
// continuous source anchor; live sources may explicitly re-anchor each input
// pair before enhancement because their device/source clock is not guaranteed
// to match the host clock. Processing completion must never re-anchor a pair.
// Units: 100 ns.
class PresentationScheduler {
public:
    void reset(uint64_t epoch,int64_t source,int64_t host,int64_t lookahead,bool paceSourcePts=true) {
        epoch_=epoch;source_=source;host_=host;delay_=lookahead;anchored_=true;paced_=paceSourcePts;
    }
    void resetPair(uint64_t epoch,int64_t source,int64_t host,int64_t lookahead,bool paceSourcePts=true) {
        reset(epoch,source,host,lookahead,paceSourcePts);
    }
    // An anchor of the other pacing mode is stale: FG on/off rebuilds the graph,
    // whose epochs restart, so a paced FG pair anchor could outlive FG.
    bool anchored(uint64_t epoch,bool paceSourcePts=true)const{return anchored_&&epoch==epoch_&&paced_==paceSourcePts;}
    bool paced()const{return paced_;}
    // Unbuffered capture is ready-driven. A late first callback or clock drift
    // must not turn its source timestamps into a persistent presentation hold.
    int64_t deadline(int64_t pts)const{return paced_?host_+(pts-source_)+delay_:host_;}
    // The fixed readiness anchor is not an expiry deadline for unpaced input.
    int64_t cadenceDeadline(int64_t pts,int64_t now)const{return paced_?deadline(pts):now;}
    bool expired(int64_t pts,int64_t now,int64_t tolerance)const{return now>deadline(pts)+tolerance;}
private:
    uint64_t epoch_=0;int64_t source_=0,host_=0,delay_=0;bool anchored_=false,paced_=true;
};
}
