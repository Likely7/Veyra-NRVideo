#pragma once

#include "veyra/sink/ArrivalClockMapping.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/CaptureAudioDsp.h"
#include "veyra/source/WasapiAudioInput.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace veyra::test {

struct WasapiClockReplay {
    uint64_t resets=0, nonMonotonic=0, frames=0;
    double finalErrorMs=0, maxErrorMs=0, maxArrivalMappingMs=0;
    bool pcmValid=false;
};

// Advance a fixed amount of interleaved PCM per packet. Raw QPC varies
// independently of packet arrivals and the actual device sample clock.
template<class Clock>
WasapiClockReplay replayWasapiClock(Clock& clock, unsigned rate, int ppm){
    constexpr int packets=30*60*100;
    constexpr double originMs=100000.0;
    const unsigned frames=rate/100;
    const auto bytes=size_t(frames)*2*sizeof(float);
    std::array<float,960> pcm{};
    for(unsigned sample=0;sample<frames;++sample){
        pcm[2*sample]=0.25f*float(std::sin(2*std::numbers::pi*sample/frames));
        pcm[2*sample+1]=-pcm[2*sample];
    }
    std::array<uint8_t,sizeof(pcm)> zeros{};
    const WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT,2,rate,rate*8,8,32,0};
    sink::WavePcmFormat parsed;
    const auto peak=sink::inspectCapturePcm(pcm.data(),size_t(frames)*2);
    WasapiClockReplay result;
    result.pcmValid=sink::parseWavePcm(&format,sizeof(format),parsed)&&peak.peak>0.24f&&peak.peak<=0.25f&&
                    !peak.nonFinite&&!peak.overRange;
    sink::ArrivalClockMapping mapping;
    clock.reset(rate,0);
    double previous=0;
    for(int index=0;index<packets;++index){
        const uint64_t devicePosition=uint64_t(index)*frames;
        const double actualMs=originMs+1000.0*double(devicePosition)/rate*(1.0+ppm*0.000001);
        const bool sawtooth=index%3000>=2000&&index%3000<2020;
        const double qpcNoise=index==0?0:sawtooth?-18.0*(index%8):0.04*(index%7-3);
        const double arrivalMs=actualMs+3.0+0.15*(index%9);
        const bool discontinuity=sawtooth&&index%4==0;
        const auto sample=clock.stamp(devicePosition,uint64_t(std::llround((actualMs+qpcNoise)*10000)),
                                      frames,discontinuity,false,arrivalMs);
        result.resets+=sample.discontinuity;
        result.nonMonotonic+=index>0&&sample.ptsMs<=previous;
        const double mapped=mapping.observe(arrivalMs,sample.ptsMs);
        result.maxArrivalMappingMs=std::max(result.maxArrivalMappingMs,std::abs(mapped));
        previous=sample.ptsMs;
        result.finalErrorMs=sample.ptsMs-actualMs;
        result.maxErrorMs=std::max(result.maxErrorMs,std::abs(result.finalErrorMs));
        if(source::wasapiPacketData(pcm.data(),bytes,false,zeros)==pcm.data()) result.frames+=frames;
    }
    return result;
}

inline void wasapiClockChecks(const auto& check){
    using Clock=source::WasapiPacketClock;
    Clock clock;
    clock.reset(48000,50);
    auto a=clock.stamp(0,10000000,480,false,false,2000);
    check(a.ptsMs==1050&&a.discontinuity&&!a.estimated,"QPC maps to host axis and starts a new epoch");
    auto b=clock.stamp(480,10100000,480,false,false,3000);
    check(b.ptsMs==1060&&!b.discontinuity,"arrival delay leaves the sample timestamp alone");
    auto c=clock.stamp(960,0,480,false,true,4000);
    check(c.ptsMs==1070&&c.estimated&&!c.discontinuity,"an uncertain timestamp continues from samples");
    auto d=clock.stamp(1920,10400000,480,false,false,5000);
    check(d.ptsMs==1090&&d.discontinuity,"a missing PCM packet reanchors");
    auto e=clock.stamp(2400,10500000,480,true,false,5000);
    check(e.ptsMs==1100&&!e.discontinuity,"a contiguous packet survives a spurious discontinuity flag");
    auto f=clock.stamp(2880,10000000,480,false,false,5000);
    check(f.ptsMs==1110&&!f.discontinuity,"an early or backwards contiguous QPC continues from samples");
    clock.reset(48000,50);
    for(uint64_t index=0;index<32;++index) clock.stamp(index*480,10000000+index*100000,480,false,false,1800);
    auto g=clock.stamp(15360,14700000,480,true,false,1800);
    check(g.ptsMs==1520&&g.discontinuity,"an explicit break of a trusted QPC reanchors even with delayed packet delivery");

    clock.reset(48000,0);
    clock.stamp(0,10000000,480,false,false,1010);
    auto future=clock.stamp(480,13000000,480,true,false,1020);
    check(future.ptsMs==1010&&!future.discontinuity,"a future QPC glitch cannot reanchor the clock");
    auto bad=clock.stamp(999999,0,480,false,true,1030);
    check(!bad.discontinuity&&bad.ptsMs==1020,"an uncertain position does not fabricate lost PCM");
    check(!clock.stamp(1440,10300000,480,false,false,1040).discontinuity,"valid timing can resume at the predicted position");
    auto pause=clock.stamp(1920,12800000,480,false,false,1290);
    check(pause.discontinuity&&pause.ptsMs==1280,"a host-confirmed forward pause starts a new epoch");
    auto unknown=clock.stamp(0,0,480,true,true,1300);
    check(unknown.discontinuity&&unknown.estimated,"a glitch with both clocks uncertain retains the reset");

    clock.reset(44100,0);
    auto reopen=clock.stamp(0,0,441,false,true,1000);
    check(reopen.ptsMs==990&&reopen.estimated&&reopen.discontinuity,"a reopened endpoint estimates the first invalid timestamp from its new rate");
    auto back=clock.stamp(0,10010000,441,false,false,1020);
    check(back.ptsMs==1001&&back.discontinuity,"a device position restart creates an epoch");
    clock.reset(44100,25);
    clock.stamp(500,10000000,220,false,false,1030);
    auto shortBlock=clock.stamp(720,10049887,221,false,false,1040);
    auto next=clock.stamp(941,10100000,441,false,false,1050);
    check(!shortBlock.discontinuity&&!next.discontinuity&&
          std::abs(shortBlock.ptsMs-(1025.0+220000.0/44100))<0.00001&&std::abs(next.ptsMs-1035.0)<0.00001,
          "uneven 44.1 kHz packets advance by sample count without rounding or resets");

    Clock loopback(Clock::Input::Loopback);
    loopback.reset(48000,0);
    loopback.stamp(0,10000000,480,false,false,1010);
    check(loopback.stamp(480,10100000,480,true,false,1020).discontinuity,"loopback preserves explicit discontinuities");
    check(loopback.stamp(960,10050000,480,false,false,1030).discontinuity,"loopback keeps its original QPC reanchor");

    clock.reset(48000,0);
    uint64_t unsupportedResets=0;
    for(uint64_t index=0;index<80000;++index){
        const uint64_t physicalQpc=10000000+index*100030;
        const bool early=index%2!=0;
        const auto provisional=clock.stamp(index*480,early?physicalQpc-1260000:physicalQpc,480,
                                           !early,false,double(physicalQpc)/10000+4.0);
        unsupportedResets+=provisional.discontinuity;
    }
    check(unsupportedResets==1,"continuously unreliable QPC and flags cannot repeatedly reanchor a continuous sample counter");

    for(unsigned rate:{44100u,48000u}){
        for(int ppm:{-300,300}){
            const auto replay=replayWasapiClock(clock,rate,ppm);
            printf("WASAPI_REPLAY rate=%u ppm=%d frames=%llu resets=%llu backwards=%llu finalMs=%.6f maxMs=%.6f arrivalMapMs=%.6f\n",
                   rate,ppm,replay.frames,replay.resets,replay.nonMonotonic,replay.finalErrorMs,replay.maxErrorMs,replay.maxArrivalMappingMs);
            check(replay.pcmValid&&replay.frames==uint64_t(rate)*30*60,"30 minutes of stereo float PCM accept the full frame count");
            check(replay.resets==1&&replay.nonMonotonic==0,"30 minutes with false flags and QPC sawteeth retain one clock epoch");
            check(std::abs(replay.finalErrorMs)<0.2&&replay.maxErrorMs<1.0&&replay.maxArrivalMappingMs<4.0,
                  "slow QPC correction keeps long-term phase and ingress mapping bounded");
        }
    }
}

}
