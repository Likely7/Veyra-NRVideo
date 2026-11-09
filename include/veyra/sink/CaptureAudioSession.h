#pragma once
#include <windows.h>
#include <mmreg.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
namespace veyra::sink {
struct WavePcmFormat;
struct CaptureAudioState {
    bool available=false,running=false,clockEstimated=true,limited=false;
    bool outputRecovering=false;
    unsigned inputChannels=0,outputChannels=0;uint32_t inputChannelMask=0,outputChannelMask=0;
    uint32_t inputSampleRate=0;unsigned inputContainerBits=0,inputValidBits=0;bool inputFloating=false;
    // Non-empty when the input is a compressed bitstream (Dolby/DTS) that was
    // decoded to PCM before this session saw it, e.g. "E-AC-3/DD+".
    std::wstring inputBitstream;
    double compensationMs=0,bufferedMs=0,bufferHighWaterMs=0,endpointBufferedMs=0;
    double driftCorrectionPpm=0;
    uint64_t recoveryDiscardedFrames=0;
    bool syncClockFallback=false;
    double rawCompensationMs=0,localVideoDelayMs=0;
    double inputBlockMs=0,inputIntervalMs=0;
    double inputPeak=0; // Historical API name: peak AFTER sample conversion.
    uint64_t inputBlocks=0;
    std::optional<double> skewMs;
    uint64_t resets=0,overflows=0,underruns=0,underrunFrames=0,silenceFrames=0,
        invalidPaddingSamples=0,nonFiniteSamples=0,clippedSamples=0,peakProtectedSamples=0,endpointRetries=0,
        overRangeSamples=0,emptyPulls=0,recoveryFades=0,clockStalledGaps=0;
    std::wstring error;
};
class CaptureAudioSession {
public:
    CaptureAudioSession();
    ~CaptureAudioSession();
    bool configure(const WavePcmFormat&);
    bool configure(const WAVEFORMATEX&,size_t formatBytes=sizeof(WAVEFORMATEX));
    bool start(std::wstring excludedEndpoint={});
    void stop();
    bool push(const void* data,size_t bytes,double ptsMs,bool discontinuity);
    // Arrival is the matching original capture frame, not the latest callback.
    // Omit only for sources with their own existing timestamp contract (PS5).
    void videoPresented(double ptsMs,int64_t host100ns,std::optional<int64_t> arrival100ns={});
    // Invalidate software presentation.  A video graph rebuild can preserve
    // the audio clock; callers that are actually pausing the transport may
    // request the old audio reset explicitly.
    void videoReset(bool resetAudio=true);
    void setGain(float);
    // Records that the input arrived as a decoded bitstream (capture passthrough).
    void setInputBitstream(std::wstring kind);
    // 0 automatic, 1 manual, 2 off. Positive offset delays sound.
    void setSync(unsigned mode,int offsetMs);
    CaptureAudioState snapshot()const;
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
