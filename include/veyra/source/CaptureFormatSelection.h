#pragma once
#include "veyra/source/CaptureFrameRate.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace veyra::source {
// `pixel` / `scan` / `interlaced` describe the same media type as `key`.
// The capture dialog offers them as three linked choices; `key` stays the
// persisted identity so an old combined selection still restores one type.
struct CaptureFormat {int index=0;unsigned width=0,height=0;double fps=0;std::wstring label;std::wstring key;int rank=0;int tier=0;bool driverCurrent=false;std::wstring pixel;std::wstring scan;bool interlaced=false;int64_t interval100ns=0;bool supported=true;bool automatic=false;int capability=-1;bool rangeCandidate=false;};
enum class CaptureFormatAxis { Resolution, FrameRate };
// Frame-rate rows also carry the rate itself and whether it is a common one (see
// captureVisibleFrameRates); the other menus leave those fields at their defaults.
struct CaptureFormatOption { std::wstring id, label; bool disabled=false, discouraged=false; double fps=0; bool interlaced=false, common=true; };

inline const CaptureFormat* selectCaptureFormat(std::span<const CaptureFormat> formats,int legacyIndex,std::wstring_view key){
    for(const auto& format:formats)
        if(key.empty()?format.index==legacyIndex:format.key==key)return &format;
    // Old VideoInfo2 keys omitted interlace/field-order flags. A single match
    // is safe to migrate; p30/i60 or top/bottom-field ambiguity requires the
    // user to choose explicitly. Never guess a mode from driver list order.
    if(!key.empty()&&key.find(L":scan=")==key.npos){
        const CaptureFormat* match=nullptr;const std::wstring prefix=std::wstring(key)+L":scan=";
        for(const auto& format:formats)if(format.key.starts_with(prefix)){
            if(match)return nullptr;match=&format;
        }
        return match;
    }
    return nullptr;
}

inline std::wstring captureResolutionId(const CaptureFormat& format){
    if(format.automatic)return L"auto";
    return std::format(L"{}x{}",format.width,format.height);
}
// A draft resolution/rate may exist before the user has chosen a concrete
// pixel format. Representatives populate dependent menus; they are never a
// capture selection and must not silently replace a missing media identity.
inline const CaptureFormat* captureResolutionSample(std::span<const CaptureFormat> formats,std::wstring_view resolution){
    if(resolution.empty())return nullptr;
    for(const auto& f:formats)if(captureResolutionId(f)==resolution)return &f;
    return nullptr;
}
inline std::wstring captureFrameRateId(const CaptureFormat& format){
    if(format.automatic)return L"auto";
    // Progressive and interlaced modes that share a numeric rate stay distinct.
    // Field order stays on the pixel list, so it does not create two identical fps rows.
    return std::format(L"{}|{}",format.interval100ns>0?format.interval100ns:format.fps>0?captureFrameInterval(format.fps):0,format.interlaced?1:0);
}
// Display groups absorb only integer 100ns rounding around standard rates.
// The exact interval and media key above remain untouched for negotiation.
inline std::wstring captureFrameRateGroupId(const CaptureFormat& format){
    if(format.automatic)return L"auto";
    const auto interval=format.interval100ns>0?format.interval100ns:captureFrameInterval(format.fps);
    static constexpr double rates[]{1,2,3,4,5,10,15,20,24,25,30,40,48,50,60,90,100,120,144,165,180,200,240,360,480,960};
    for(double rate:rates)for(double divisor:{1.,1001./1000.}){
        const auto standard=captureFrameInterval(rate/divisor);
        if(interval>0&&std::abs(interval-standard)<=1)
            return std::format(L"{}|{}",standard,format.interlaced?1:0);
    }
    return captureFrameRateId(format);
}
inline const CaptureFormat* captureRateSample(std::span<const CaptureFormat> formats,std::wstring_view resolution,std::wstring_view rate){
    if(resolution.empty()||rate.empty())return nullptr;
    for(const auto& f:formats)if(captureResolutionId(f)==resolution&&captureFrameRateId(f)==rate)return &f;
    for(const auto& f:formats)if(captureResolutionId(f)==resolution&&captureFrameRateGroupId(f)==rate)return &f;
    return nullptr;
}
inline const CaptureFormat* selectCapturePixelFormat(std::span<const CaptureFormat> formats,std::wstring_view resolution,std::wstring_view rate,std::wstring_view key){
    if(key.empty())return nullptr;
    const auto* f=selectCaptureFormat(formats,-1,key);
    const auto* sample=captureRateSample(formats,resolution,rate);
    return f&&sample&&captureResolutionId(*f)==resolution&&captureFrameRateGroupId(*f)==captureFrameRateGroupId(*sample)?f:nullptr;
}
inline bool captureMjpegFormat(const CaptureFormat& format){
    std::wstring pixel=format.pixel;
    for(auto& c:pixel)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
    return pixel==L"MJPEG"||pixel==L"MJPG";
}
inline std::wstring captureResolutionLabel(const CaptureFormat& format){
    if(format.automatic)return L"自动检测实际信号";
    return std::format(L"{} × {}",format.width,format.height);
}
// The rates people pick. A driver rate counts as one of them within 0.15 %, which also covers
// the 1000/1001 rates cards report instead (59.94 for 60, 29.97 for 30, 119.88 for 120) and
// nominal rates such as 60.00024.
inline bool captureCommonFrameRate(double fps){
    static constexpr double common[]{24,25,30,50,60,100,120,144,165,240};
    return std::any_of(std::begin(common),std::end(common),[fps](double rate){return std::abs(fps-rate)<=rate*0.0015;});
}
inline std::wstring captureFrameRateLabel(const CaptureFormat& format){
    if(format.automatic)return L"跟随实际信号";
    if(format.fps<=0)return L"设备默认帧率";
    auto label=std::format(L"{:.3f} fps",format.fps);
    if(!format.scan.empty())label+=L" · "+format.scan;
    return label;
}
inline std::wstring capturePixelLabel(const CaptureFormat& format,bool ambiguous){
    std::wstring label=format.pixel.empty()?L"未知格式":format.pixel;
    if(ambiguous){
        if(!format.scan.empty())label+=L" · "+format.scan;
        label+=std::format(L" [format {}] · {} x100ns",format.index,format.interval100ns);
    }
    if(format.driverCurrent)label+=L" · 驱动默认";
    if(!format.supported)label+=L" · 当前播放器不支持";
    return label;
}
inline std::vector<CaptureFormatOption> captureResolutionOptions(std::span<const CaptureFormat> formats){
    std::vector<CaptureFormatOption> out;
    std::unordered_set<std::wstring> seen;
    for(const auto& format:formats){
        const auto id=captureResolutionId(format);
        if(!seen.insert(id).second)continue;
        out.push_back({id,captureResolutionLabel(format)});
    }
    return out;
}
inline std::vector<CaptureFormatOption> captureFrameRateOptions(std::span<const CaptureFormat> formats,const CaptureFormat* current){
    std::vector<CaptureFormatOption> out;
    std::unordered_set<std::wstring> seen;
    if(!current)return out;
    const auto resolution=captureResolutionId(*current);
    for(const auto& format:formats){
        if(captureResolutionId(format)!=resolution)continue;
        const auto id=captureFrameRateGroupId(format);
        if(!seen.insert(id).second)continue;
        auto label=captureFrameRateLabel(format);
        if(std::any_of(out.begin(),out.end(),[&](const CaptureFormatOption& option){return option.label==label;}))
            label+=std::format(L" [format {}]",format.index);
        out.push_back({id,std::move(label),false,false,format.fps,format.interlaced,format.automatic||format.fps<=0||captureCommonFrameRate(format.fps)});
    }
    return out;
}
// Some drivers list every rate from 1 to 120 fps for each format (logs(18)). The menu shows
// one row per common rate; the rest wait behind 显示全部可用帧率. The selected rate is always
// listed, and a resolution offering no common rate lists everything. Either list runs from the
// highest rate down (signal-following/default rows first; progressive before interlaced).
// `hidden` counts the rows the short list leaves out (0 when there is nothing more to show).
inline std::vector<CaptureFormatOption> captureVisibleFrameRates(const std::vector<CaptureFormatOption>& all,std::wstring_view selected,bool showAll,size_t* hidden=nullptr){
    if(hidden)*hidden=0;
    std::vector<CaptureFormatOption> shown;
    if(std::none_of(all.begin(),all.end(),[](const CaptureFormatOption& option){return option.common;}))shown=all;
    else{
        for(const auto& option:all){
            if(option.id!=selected&&!option.common)continue;
            shown.push_back(option);
        }
        if(hidden)*hidden=all.size()-shown.size();
        if(showAll)shown=all;
    }
    std::stable_sort(shown.begin(),shown.end(),[](const CaptureFormatOption& a,const CaptureFormatOption& b){
        if((a.fps<=0)!=(b.fps<=0))return a.fps<=0;
        if(a.fps!=b.fps)return a.fps>b.fps;
        return !a.interlaced&&b.interlaced;
    });
    return shown;
}
// The rate a newly chosen resolution starts with when the current one is missing there: its
// first common rate in list order, or else its first rate. Null when the resolution is unknown.
inline const CaptureFormat* captureDefaultFrameRate(std::span<const CaptureFormat> formats,std::wstring_view resolution){
    const CaptureFormat* first=nullptr;
    for(const auto& f:formats){
        if(captureResolutionId(f)!=resolution)continue;
        if(f.automatic||f.fps<=0||captureCommonFrameRate(f.fps))return &f;
        if(!first)first=&f;
    }
    return first;
}
inline std::vector<CaptureFormatOption> capturePixelOptions(std::span<const CaptureFormat> formats,const CaptureFormat* current){
    std::vector<CaptureFormatOption> out;
    if(!current)return out;
    const auto resolution=captureResolutionId(*current);
    const auto rate=captureFrameRateGroupId(*current);
    std::vector<const CaptureFormat*> matched;
    std::unordered_map<std::wstring,int> copies;
    std::unordered_set<std::wstring> seen;
    for(const auto& format:formats)
        if(captureResolutionId(format)==resolution&&captureFrameRateGroupId(format)==rate){matched.push_back(&format);++copies[format.pixel];}
    for(const auto* format:matched){
        if(!seen.insert(format->key).second)continue;
        out.push_back({format->key,capturePixelLabel(*format,copies[format->pixel]>1),!format->supported,captureMjpegFormat(*format)});
    }
    return out;
}
// Keep the other two axes when the newly chosen resolution or frame rate still
// offers them. A missing pixel format must be selected explicitly; never route
// an existing choice to another pixel format. A null current is only a menu
// representative, not permission to connect that media type.
inline const CaptureFormat* retargetCaptureFormat(std::span<const CaptureFormat> formats,const CaptureFormat* current,CaptureFormatAxis axis,std::wstring_view id){
    if(id.empty()||(axis==CaptureFormatAxis::FrameRate&&!current))return nullptr;
    const auto resolution=current?captureResolutionId(*current):std::wstring{};
    const auto rate=current?captureFrameRateGroupId(*current):std::wstring{};
    const auto pixel=current?current->pixel:std::wstring{};
    const CaptureFormat* fallback=nullptr;
    const CaptureFormat* samePixel=nullptr;
    const CaptureFormat* sameBoth=nullptr;
    for(const auto& format:formats){
        const bool match=axis==CaptureFormatAxis::Resolution?captureResolutionId(format)==id
            :captureResolutionId(format)==resolution&&(captureFrameRateId(format)==id||captureFrameRateGroupId(format)==id);
        if(!match)continue;
        if(!fallback)fallback=&format;
        if(!current)continue;
        const bool rateMatch=captureFrameRateGroupId(format)==rate;
        const bool pixelMatch=!pixel.empty()&&format.pixel==pixel;
        if(rateMatch&&pixelMatch){if(!sameBoth)sameBoth=&format;}
        else if(pixelMatch){if(!samePixel)samePixel=&format;}
    }
    if(!current)return fallback;
    if(axis==CaptureFormatAxis::FrameRate)return samePixel;
    return sameBoth?sameBoth:samePixel;
}

inline std::wstring capturePathOptions(double fps,std::wstring_view encodedKey){
    std::wstring result;
    if(fps>0)result=std::format(L"?fps={:.6f}",fps);
    if(!encodedKey.empty())result+=std::format(L"{}format={}",result.empty()?L"?":L"&",encodedKey);
    return result;
}

inline bool parseCapturePathOptions(std::wstring_view query,double& fps,std::wstring& encodedKey){
    fps=0;encodedKey.clear();bool haveFps=false,haveKey=false;
    if(query.empty())return false;
    while(!query.empty()){
        const auto end=query.find(L'&');const auto option=query.substr(0,end);
        if(option.starts_with(L"fps=")){
            if(haveFps||!parseCaptureFrameRate(option.substr(4),fps))return false;
            haveFps=true;
        }else if(option.starts_with(L"format=")){
            const auto key=option.substr(7);
            if(haveKey||key.empty()||key.size()>4096||key.size()%4||key.find_first_not_of(L"0123456789abcdefABCDEF")!=std::wstring_view::npos)return false;
            haveKey=true;encodedKey=key;
        }else return false;
        if(end==std::wstring_view::npos)break;
        query.remove_prefix(end+1);
        if(query.empty())return false;
    }
    return true;
}
}
