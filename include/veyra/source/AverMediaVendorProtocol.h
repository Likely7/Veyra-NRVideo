#pragma once

// ABI/identity contract referenced from AVerMedia's public OBS headers at
// obs-MultichannelAudio 7d1250b42e42f964b4fa3a6bcb7269cc09a1e798.
// This is a local interface mirror, not the proprietary runtime or SDK.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cwchar>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace veyra::source::avermedia {
struct SampleInfo { uint32_t channels=0,bits=0,rate=0; };
static_assert(sizeof(SampleInfo)==12);
struct DeviceInfo { std::wstring friendlyName,devicePath; };
class AudioDealer {
public:
    virtual ~AudioDealer() = default;
    virtual int onAudioData(SampleInfo, unsigned char*, long) = 0;
};
inline std::wstring lower(std::wstring_view value) {
    std::wstring out(value); for(auto& c:out)c=wchar_t(towlower(c)); return out;
}
inline bool vendorPath(std::wstring_view path) {
    return lower(path).find(L"vid_07ca")!=std::wstring::npos;
}
inline std::wstring usbIdentity(std::wstring_view path) {
    auto p=lower(path); std::replace(p.begin(),p.end(),L'\\',L'#');
    const auto vid=p.find(L"vid_07ca&pid_"); if(vid==p.npos)return {};
    const auto first=p.find(L'#',vid),last=first==p.npos?p.npos:p.find(L'#',first+1);
    if(first==p.npos)return {};
    auto product=p.substr(vid,first-vid),instance=p.substr(first+1,last==p.npos?p.npos:last-first-1);
    const auto mi=product.find(L"&mi_");
    if(mi!=product.npos){
        product.erase(mi);const auto tail=instance.rfind(L'&');
        if(tail!=instance.npos&&instance.size()-tail==5
            &&std::all_of(instance.begin()+tail+1,instance.end(),[](wchar_t c){return iswxdigit(c)!=0;}))instance.erase(tail);
    }
    return instance.empty()?std::wstring():product+L"#"+instance;
}
inline std::wstring deviceName(std::wstring_view text) {
    auto n=lower(text);
    // Windows wraps the actual device name, e.g. HDMI/Line In (...-Audio).
    const auto left=n.find(L'('),right=n.rfind(L')');
    if(left!=n.npos&&right>left)n=n.substr(left+1,right-left-1);
    for(auto suffix:{L"-audio",L"-video",L" (audio)",L" (video)"}){
        const auto at=n.find(suffix);if(at!=n.npos)n.erase(at,wcslen(suffix));
    }
    while(!n.empty()&&iswspace(n.back()))n.pop_back();
    return n;
}
inline bool vendorName(std::wstring_view name) {
    const auto n=lower(name);return n.find(L"avermedia")!=n.npos||n.find(L"live gamer")!=n.npos;
}
inline bool eligible(std::wstring_view name,std::wstring_view path,unsigned ingress) {
    // PCM-only never arms the non-PCM graph. A component installed for another
    // card is not evidence that the explicitly selected microphone is ours.
    const auto p=lower(path);
    if(p.find(L"vid_")!=p.npos&&!vendorPath(p))return false;
    return ingress!=1&&(vendorPath(path)||vendorName(name));
}
inline std::optional<size_t> selectDevice(std::span<const DeviceInfo> devices,
                                         std::wstring_view name,std::wstring_view path) {
    const auto exact=lower(path),identity=usbIdentity(path),wanted=deviceName(name);
    std::optional<size_t> match;
    for(size_t i=0;i<devices.size();++i){
        const auto& d=devices[i];if(d.devicePath.empty())continue;
        if(!exact.empty()&&lower(d.devicePath)==exact)return i;
        if(!identity.empty()){
            if(usbIdentity(d.devicePath)!=identity)continue;
        }else if(wanted.empty()||deviceName(d.friendlyName)!=wanted)continue;
        if(!vendorPath(d.devicePath)&&!vendorName(d.friendlyName))continue;
        if(match)return {}; // Ambiguous identical cards: never donate audio.
        match=i;
    }
    return match;
}
} // namespace veyra::source::avermedia
