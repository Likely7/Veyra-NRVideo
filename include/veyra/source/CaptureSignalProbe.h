#pragma once
#include "veyra/source/CaptureMediaType.h"
#include <string_view>
#include <cwctype>
#include <algorithm>

namespace veyra::source {
inline bool isBlackmagicCaptureDevice(std::wstring_view name,std::wstring_view path={}){
    const auto contains=[](std::wstring_view value,std::wstring_view needle){
        return std::search(value.begin(),value.end(),needle.begin(),needle.end(),[](wchar_t a,wchar_t b){return towlower(a)==b;})!=value.end();
    };
    return contains(name,L"blackmagic")||contains(name,L"decklink")||contains(path,L"decklink#");
}
struct CaptureSignalSample {bool known=false,blackLike=false;unsigned points=0,minimum=255,maximum=0,chromaDeviation=0;double mean=0;};
// Diagnostic only: <=32x18 points from the existing CPU sample, no readback.
// Black content is NOT evidence of absent input; never alter/reject it.
inline CaptureSignalSample sampleCaptureSignal(const CaptureMediaLayout& l,const uint8_t* data,size_t bytes){
    CaptureSignalSample s;if(!data||bytes<l.sampleBytes||!l.width||!l.height)return s;
    const bool rgb=l.packing==CapturePacking::Bgr32||l.packing==CapturePacking::Bgra32;
    const bool uy=l.packing==CapturePacking::Uyvy,yuy=l.packing==CapturePacking::Yuy2,yvy=l.packing==CapturePacking::Yvyu;
    if(!rgb&&!uy&&!yuy&&!yvy)return s;
    const unsigned nx=std::min(32u,l.width),ny=std::min(18u,l.height);uint64_t sum=0;
    for(unsigned gy=0;gy<ny;++gy){
        const unsigned y=ny==1?0:gy*(l.height-1)/(ny-1);const auto* row=data+size_t(y)*l.stride;
        for(unsigned gx=0;gx<nx;++gx){
            const unsigned x=nx==1?0:gx*(l.width-1)/(nx-1);unsigned value;
            if(rgb){const auto* p=row+x*4;value=std::max({unsigned(p[0]),unsigned(p[1]),unsigned(p[2])});}
            else{const auto* p=row+(x&~1u)*2;value=p[uy?(x%2?3:1):(x%2?2:0)];
                const unsigned u=uy?0:yvy?3:1,v=uy?2:yvy?1:3;
                s.chromaDeviation=std::max({s.chromaDeviation,unsigned(std::abs(int(p[u])-128)),unsigned(std::abs(int(p[v])-128))});}
            s.minimum=std::min(s.minimum,value);s.maximum=std::max(s.maximum,value);sum+=value;++s.points;
        }
    }
    s.known=s.points>0;s.mean=s.points?double(sum)/s.points:0;
    const unsigned black=rgb||l.color.range==pipeline::ColorRange::Full?0:16;
    s.blackLike=s.known&&s.maximum<=black+2&&(rgb||s.chromaDeviation<=2);return s;
}
struct CaptureSignalProbeState {
    uint64_t samples=0;unsigned blackStreak=0;CaptureSignalSample latest;
    void observe(const CaptureSignalSample& sample){latest=sample;++samples;if(!sample.known){blackStreak=0;return;}blackStreak=sample.blackLike?std::min(blackStreak+1,3600u):0;}
    bool persistentBlack()const{return latest.known&&blackStreak>=3;}
};
}
