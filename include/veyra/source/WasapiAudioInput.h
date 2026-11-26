#pragma once
#include "veyra/sink/CaptureAudioSession.h"
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
    struct Stamp {double ptsMs;bool discontinuity,estimated;};
    void reset(unsigned rate,double qpcToHostMs){rate_=rate;offset_=qpcToHostMs;have_=false;}
    Stamp stamp(uint64_t position,uint64_t qpc100ns,unsigned frames,bool discontinuity,bool timestampError,double nowMs){
        static constexpr double kMaxForwardReanchorMs=100.0;
        const bool gap=have_&&!timestampError&&position!=nextPosition_;
        const double durationMs=1000.0*frames/rate_;
        const double qpcPts=double(qpc100ns)/10000.0+offset_;
        // Some USB UAC drivers emit spurious DATA_DISCONTINUITY flags and
        // sawtooth-quantized QPC jumps (-16..-126 ms) while device sample
        // position advances contiguously without dropped frames. Trust the
        // sample timeline unless samples are missing or an explicit discontinuity
        // also jumps forward past the bounded jitter window.
        const bool forwardJump=have_&&!gap&&!timestampError&&discontinuity&&(qpcPts-nextPts_)>kMaxForwardReanchorMs;
        const double pts=(!have_||gap||forwardJump)
            ?(timestampError?nowMs-durationMs:qpcPts)
            :nextPts_;
        const bool reset=!have_||gap||forwardJump||pts<lastPts_;
        lastPts_=pts;nextPts_=pts+durationMs;nextPosition_=(timestampError&&have_?nextPosition_:position)+frames;have_=true;
        return {pts,reset,timestampError};
    }
private:
    unsigned rate_=48000;double offset_=0,nextPts_=0,lastPts_=0;uint64_t nextPosition_=0;bool have_=false;
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
