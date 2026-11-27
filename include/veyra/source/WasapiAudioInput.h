#pragma once
#include "veyra/sink/CaptureAudioSession.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

namespace veyra::source {
struct WasapiInputDevice {std::wstring name,id;};
struct WasapiInputMetrics {uint64_t packets=0,retries=0,timestampErrors=0,discontinuities=0;};
// SILENT packets may have a null driver pointer; the preallocated zero buffer
// preserves their actual frame count. Reject malformed/oversized packets.
inline const void* wasapiPacketData(const void* data,size_t bytes,bool silent,std::span<const uint8_t> zeros){
    if(!bytes||bytes>zeros.size())return nullptr;
    return silent?zeros.data():data;
}
// Packet time in the process steady-clock domain; never compare raw device
// positions with DirectShow stream-relative PTS.
class WasapiPacketClock {
public:
    enum class Input { Capture, Loopback };
    struct Stamp {double ptsMs;bool discontinuity,estimated;};
    explicit WasapiPacketClock(Input input=Input::Capture):input_(input){}
    void reset(unsigned rate,double qpcToHostMs){
        rate_=rate;offset_=qpcToHostMs;have_=false;haveQpc_=false;stableQpcMs_=0;
    }
    Stamp stamp(uint64_t position,uint64_t qpc100ns,unsigned frames,bool discontinuity,bool timestampError,double nowMs){
        const bool gap=have_&&!timestampError&&position!=nextPosition_;
        const double blockMs=1000.0*frames/rate_;
        const double qpcPts=double(qpc100ns)/10000.0+offset_;
        double pts=timestampError?(have_?nextPts_:nowMs-blockMs):qpcPts;
        bool reset=!have_||discontinuity||gap||(have_&&pts<lastPts_);
        bool estimated=timestampError;
        if(input_==Input::Capture){
            const double extraQpcMs=qpcPts-lastQpcPts_-lastBlockMs_;
            const bool qpcInRange=!timestampError&&qpcPts<=nowMs+2.0;
            const bool pausedOnHost=haveQpc_&&extraQpcMs>100.0&&nowMs-lastArrivalMs_-lastBlockMs_>100.0;
            const bool markedClockBreak=discontinuity&&stableQpcMs_>=250.0&&extraQpcMs>100.0;
            const bool clockJump=have_&&qpcInRange&&qpcPts-nextPts_>100.0&&(markedClockBreak||pausedOnHost);
            reset=!have_||gap||clockJump||(timestampError&&discontinuity);
            if(have_&&!gap&&!clockJump){
                // Contiguous hardware samples carry through UAC QPC jitter.
                pts=nextPts_;
                if(qpcInRange&&haveQpc_&&!discontinuity&&qpcPts>lastQpcPts_&&std::abs(extraQpcMs)<=1.0){
                    stableQpcMs_=std::min(250.0,stableQpcMs_+lastBlockMs_);
                    if(stableQpcMs_>=250.0){
                        // Follow the real oscillator after a stable run. Limit
                        // phase adjustment to 1 ms per second of PCM.
                        const double limit=lastBlockMs_*0.001;
                        pts+=std::clamp((qpcPts-nextPts_)*0.04,-limit,limit);
                    }
                }else stableQpcMs_=0;
            }else{
                stableQpcMs_=0;
                if(!qpcInRange){pts=nowMs-blockMs;estimated=true;}
            }
            haveQpc_=qpcInRange&&!discontinuity;
            lastQpcPts_=qpcPts;lastBlockMs_=blockMs;lastArrivalMs_=nowMs;
        }
        lastPts_=pts;nextPts_=pts+blockMs;nextPosition_=(timestampError&&have_?nextPosition_:position)+frames;have_=true;
        return {pts,reset,estimated};
    }
private:
    Input input_;
    unsigned rate_=48000;double offset_=0,nextPts_=0,lastPts_=0;uint64_t nextPosition_=0;bool have_=false;
    double lastBlockMs_=0,lastQpcPts_=0,lastArrivalMs_=0,stableQpcMs_=0;bool haveQpc_=false;
};
class WasapiAudioInput {
public:
    WasapiAudioInput();~WasapiAudioInput();
    static std::vector<WasapiInputDevice> devices(bool virtualSpeaker=false, bool includeDisabled=false); // enumerate only, never capture
    bool configure(std::wstring endpointId, bool virtualSpeaker=false);
    bool start();void stop();
    void setGain(float);void setSync(unsigned,int);
    void videoPresented(double hostAxisPtsMs,int64_t host100ns,std::optional<int64_t> arrival100ns={});
    void videoReset(bool resetAudio);
    sink::CaptureAudioState snapshot()const;
    WasapiInputMetrics metrics()const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
