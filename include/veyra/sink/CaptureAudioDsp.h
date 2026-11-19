#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
struct SwrContext;
namespace veyra::sink {
// Prepare on a fresh epoch BEFORE consuming PCM. Once active, zero correction
// preserves the same FIR history and delay; it is not a stream reset.
class CaptureRateCorrection {
public:
    int prepare(SwrContext* context,bool automatic);
    int set(SwrContext* context,int delta,int distance=48000);
private:
    bool prepared_=false;
};
struct CaptureSampleStats {float peak=0;uint64_t nonFinite=0,overRange=0;};
// Clock alignment cannot consume the live renderer's scheduling reserve.
// Negative SWR compensation makes fewer output samples. At the latency floor,
// chasing an unachievable earlier PTS otherwise causes periodic endpoint gaps.
inline double captureSafeCorrectionPpm(double requested,double current,double queuedMs,double reserveMs){
    const double slewed=current+std::clamp(requested-current,-250.0,250.0);
    const double reservoirLimit=std::clamp((reserveMs-queuedMs)*500.0,-5000.0,5000.0);
    // Safety overrides the downward slew immediately; keep the FIR history.
    return std::clamp(std::max(slewed,reservoirLimit),-5000.0,5000.0);
}
// One callback's worth of reserve covers the phase difference between the
// capture and render devices, including 44.1 -> 48 kHz resampling blocks.
inline double capturePrefillMs(double endpointMs,double inputBlockMs){
    return std::clamp(endpointMs+std::clamp(inputBlockMs,4.0,12.0),20.0,40.0);
}
// Float PCM can legitimately exceed unity before final user gain/downmix.
// Do not introduce block AGC or clamp finite samples in the conversion stage.
inline CaptureSampleStats inspectCapturePcm(float* samples,size_t count){
    CaptureSampleStats stats;
    for(size_t i=0;i<count;++i){
        auto& sample=samples[i];
        if(!std::isfinite(sample)){sample=0;++stats.nonFinite;continue;}
        stats.peak=std::max(stats.peak,std::abs(sample));
        if(std::abs(sample)>1.0f)++stats.overRange;
    }
    return stats;
}
// Device-clock evidence, not unfilled writable capacity. An empty software
// pull with queued endpoint PCM does not alter the following waveform.
class LiveAudioGapTracker {
public:
    struct Observation {uint64_t frames=0;bool began=false;};
    Observation observe(uint32_t padding,uint64_t consumed,uint64_t submittedEnd){
        if(padding||consumed<=submittedEnd)return {};
        const bool began=!open_;open_=true;
        return {consumed-submittedEnd,began};
    }
    // Some WASAPI drivers stop advancing IAudioClock when the queue empties.
    // Require TWO empty observations spanning a full endpoint period before
    // treating this as starvation. Do not invent device-clock missing frames.
    bool observeEmpty(uint32_t padding,size_t got,double nowMs,double periodMs){
        const bool confirmed=!padding&&emptySince_&&nowMs-*emptySince_>=periodMs;
        const bool began=confirmed&&!open_;
        if(confirmed)open_=true;
        if(padding||got)emptySince_.reset();
        else if(!emptySince_)emptySince_=nowMs;
        return began;
    }
    void submitted(size_t frames){if(frames){open_=false;emptySince_.reset();}}
    void reset(){open_=false;emptySince_.reset();}
private:
    bool open_=false;
    std::optional<double> emptySince_;
};
}
