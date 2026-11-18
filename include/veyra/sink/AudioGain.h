#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace veyra::sink {
// Linear application gain: 1 = 100%, 3 = 300% (+9.54 dB).
inline constexpr float kMaxAudioGain=3.0f;
inline float clampAudioGain(float gain){
    return std::isfinite(gain)?std::clamp(gain,0.0f,kMaxAudioGain):0.0f;
}
// 5 ms per unit at 48 kHz (at most 15 ms for 0 -> 300%), independent
// of PTS and renderer clock. Smoothing does not buffer or delay PCM.
inline void applyPcmGain(float* samples,size_t frames,unsigned channels,float target,float& current){
    target=clampAudioGain(target);current=clampAudioGain(current);
    for(size_t i=0;i<frames;++i){current+=std::clamp(target-current,-1.0f/240,1.0f/240);for(unsigned c=0;c<channels;++c)samples[i*channels+c]*=current;}
}
// Apply only after gain, fades and channel mapping, at the final PCM output.
// Quiet inputs retain the full requested gain; hot peaks saturate instead of
// overflowing. This is peak clipping, not loudness normalisation or a compressor.
inline void limitPcmPeaks(float* samples,size_t frames,unsigned channels){
    for(size_t i=0;i<frames*channels;++i)
        samples[i]=std::isfinite(samples[i])?std::clamp(samples[i],-1.0f,1.0f):0.0f;
}
// Full transition duration is fixed even when the current user gain is low.
inline void fadePcmTail(float* samples,size_t frames,unsigned channels,float gain){
    if(!frames)return;
    gain=clampAudioGain(gain);
    for(size_t i=0;i<frames;++i){const float envelope=frames>1?gain*float(frames-1-i)/float(frames-1):0;
        for(unsigned c=0;c<channels;++c)samples[channels*i+c]*=envelope;}
}
// Restart the waveform at zero after a live endpoint gap.  This is applied
// after the user gain, so it only changes the short edge envelope and never
// changes the requested volume.
inline void fadePcmHead(float* samples,size_t frames,unsigned channels,size_t& remaining){
    constexpr size_t total=240; // 5 ms at 48 kHz
    for(size_t i=0;i<frames&&remaining;++i){
        const size_t consumed=total-remaining;
        const float envelope=float(consumed+1)/float(total);
        for(unsigned c=0;c<channels;++c)samples[i*channels+c]*=envelope;
        --remaining;
    }
}
inline void applyStereoGain(float* p,size_t n,float target,float& current){applyPcmGain(p,n,2,target,current);}
inline void fadeStereoTail(float* p,size_t n,float gain){fadePcmTail(p,n,2,gain);}
}
