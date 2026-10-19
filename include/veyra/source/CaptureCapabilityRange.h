#pragma once
#include "veyra/source/CaptureFrameRate.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace veyra::source {
struct CaptureModeHint { unsigned width=0,height=0;int64_t interval=0; };
inline bool captureUnsigned(std::wstring_view text,unsigned& value){
    if(text.empty()||text.size()>6)return false;
    unsigned result=0;
    for(wchar_t c:text){if(c<L'0'||c>L'9')return false;result=result*10+unsigned(c-L'0');}
    value=result;return result>0&&result<=16384;
}
inline CaptureModeHint captureResolutionHint(std::wstring_view text){
    CaptureModeHint hint;const auto separator=text.find_first_of(L"xX×");
    if(separator==text.npos||!captureUnsigned(text.substr(0,separator),hint.width)||
       !captureUnsigned(text.substr(separator+1),hint.height))return {};
    return hint;
}
inline CaptureModeHint captureKeyHint(std::wstring_view key){
    const auto first=key.find(L':');if(first==key.npos)return {};
    const auto second=key.find(L':',first+1);if(second==key.npos)return {};
    auto height=key.substr(first+1,second-first-1);if(height.starts_with(L"-"))height.remove_prefix(1);
    CaptureModeHint hint;
    if(!captureUnsigned(key.substr(0,first),hint.width)||!captureUnsigned(height,hint.height))return {};
    const auto end=key.find(L':',second+1);if(end==key.npos||end-second>15)return {};
    for(wchar_t c:key.substr(second+1,end-second-1)){
        if(c<L'0'||c>L'9')return {};
        hint.interval=hint.interval*10+(c-L'0');
    }
    if(hint.interval>10000000000LL)return {};
    return hint;
}
struct CaptureCapabilityRange {
    unsigned minWidth=0,maxWidth=0,minHeight=0,maxHeight=0,stepX=1,stepY=1;
    int64_t minInterval=0,maxInterval=0;
    bool validSize()const{return minWidth&&minHeight&&maxWidth>=minWidth&&maxHeight>=minHeight&&maxWidth<=16384&&maxHeight<=16384;}
    bool acceptsSize(unsigned width,unsigned height)const{
        return validSize()&&width>=minWidth&&width<=maxWidth&&height>=minHeight&&height<=maxHeight&&
            (width-minWidth)%std::max(1u,stepX)==0&&(height-minHeight)%std::max(1u,stepY)==0;
    }
    bool validRate()const{return minInterval>0&&maxInterval>=minInterval;}
    bool acceptsInterval(int64_t value)const{return validRate()&&value>=minInterval&&value<=maxInterval;}
};
inline std::vector<std::pair<unsigned,unsigned>> captureCandidateSizes(const CaptureCapabilityRange& range,
        unsigned width,unsigned height,std::span<const CaptureModeHint> hints={}){
    std::vector<std::pair<unsigned,unsigned>> out;
    const auto add=[&](unsigned w,unsigned h){const auto size=std::pair{w,h};
        if(w&&h&&w<=16384&&h<=16384&&std::find(out.begin(),out.end(),size)==out.end())out.push_back(size);};
    add(width,height); // the original advertised type always remains a candidate
    if(range.validSize()){
        add(range.minWidth,range.minHeight);
        const auto maxWidth=range.minWidth+(range.maxWidth-range.minWidth)/std::max(1u,range.stepX)*std::max(1u,range.stepX);
        const auto maxHeight=range.minHeight+(range.maxHeight-range.minHeight)/std::max(1u,range.stepY)*std::max(1u,range.stepY);
        add(maxWidth,maxHeight);
        static constexpr std::pair<unsigned,unsigned> common[]={{640,480},{720,480},{720,576},{1280,720},{1920,1080},{2560,1440},{3840,2160},{4096,2160}};
        for(auto [w,h]:common)if(range.acceptsSize(w,h))add(w,h);
        for(const auto& hint:hints)if(range.acceptsSize(hint.width,hint.height))add(hint.width,hint.height);
    }
    return out;
}
inline std::vector<int64_t> captureCandidateIntervals(int64_t advertised,std::span<const int64_t> discrete,
        const CaptureCapabilityRange& range,std::span<const CaptureModeHint> hints={}){
    std::vector<int64_t> out;
    const auto add=[&](int64_t value){if(value>=0&&std::find(out.begin(),out.end(),value)==out.end())out.push_back(value);};
    add(advertised);
    for(auto value:discrete)if(value>0)add(value);
    if(range.validRate()){
        add(range.minInterval);add(range.maxInterval);
        static constexpr double common[]{1,2,3,4,5,10,15,20,24,25,30,40,48,50,60,90,100,120,144,165,180,200,240,360,480,960};
        for(double fps:common)for(double divisor:{1.,1001./1000.}){
            auto value=captureFrameInterval(fps/divisor);
            // Integer endpoint rounding, never a percentage-based NTSC merge.
            if(value+1==range.minInterval)value=range.minInterval;
            if(value-1==range.maxInterval)value=range.maxInterval;
            if(range.acceptsInterval(value))add(value);
        }
        for(const auto& hint:hints)if(range.acceptsInterval(hint.interval))add(hint.interval);
    }
    return out;
}
}
