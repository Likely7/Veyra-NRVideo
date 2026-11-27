// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Types.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace veyra::remoteplay {
HostTime monotonic100ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()/100;
}
std::optional<std::string> VideoProfile::validate() const {
    if (!((width==1280 && height==720) || (width==1920 && height==1080)))
        return "Only 720p/1080p are supported by this integration profile";
    if (fps!=30 && fps!=60) return "Frame rate must be 30 or 60";
    if (bitrateKbps<1000 || bitrateKbps>100000) return "Bitrate must be 1000..100000 kbit/s";
    if (codec!=Codec::H264 && codec!=Codec::H265 && codec!=Codec::H265Hdr) return "Unsupported codec";
    return {};
}
namespace {
constexpr std::string_view b64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int sextet(char c) { const auto p=b64.find(c); return p==b64.npos ? -1 : static_cast<int>(p); }
bool digit(char c) noexcept { return c>='0' && c<='9'; }
bool alpha(char c) noexcept { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
}
std::optional<AccountId> accountIdFromBase64(std::string_view text) {
    // Exactly 8 decoded bytes => 11 alphabet characters and one '='.
    if (text.size()!=12 || text[11]!='=') return {};
    AccountId out{}; std::uint32_t acc=0; unsigned bits=0; std::size_t n=0;
    for (std::size_t i=0;i<11;++i) {
        const int v=sextet(text[i]); if (v<0) return {};
        acc=(acc<<6)|static_cast<unsigned>(v); bits+=6;
        if (bits>=8) { bits-=8; if(n>=out.size()) return {}; out[n++]=static_cast<std::uint8_t>(acc>>bits); }
    }
    if(n!=out.size() || bits!=2 || (acc&3u)!=0) return {}; // Reject non-canonical padding bits.
    return out;
}
std::optional<AccountId> accountIdFromDecimal(std::string_view text) {
    if(text.empty() || text.size()>20 || !std::all_of(text.begin(),text.end(),digit)) return {};
    std::uint64_t value=0;
    auto [end,err]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(err!=std::errc{} || end!=text.data()+text.size()) return {};
    AccountId out{}; for(auto& b:out){b=static_cast<std::uint8_t>(value&255u);value>>=8;}
    return out;
}
std::string accountIdToBase64(const AccountId& id) {
    std::string out; out.reserve(12); std::uint32_t acc=0; unsigned bits=0;
    for(auto b:id){acc=(acc<<8)|b;bits+=8;while(bits>=6){bits-=6;out.push_back(b64[(acc>>bits)&63u]);}}
    if(bits)out.push_back(b64[(acc<<(6-bits))&63u]);
    while(out.size()%4) { out.push_back('='); }
    return out;
}
std::optional<std::uint32_t> parsePairingPin(std::string_view text) {
    if(text.size()!=8 || !std::all_of(text.begin(),text.end(),digit))return {};
    std::uint32_t pin=0;for(char c:text)pin=pin*10+static_cast<unsigned>(c-'0');return pin;
}
bool validLoginPin(std::string_view pin) noexcept {
    return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool validHost(std::string_view host) noexcept {
    if(host.empty() || host.size()>253 || host.front()=='.' || host.back()=='.')return false;
    bool numeric=true;
    for(char c:host){if(!digit(c)&&c!='.')numeric=false;if(!(digit(c)||alpha(c)||c=='.'||c=='-'))return false;}
    std::size_t begin=0,labels=0;
    while(begin<host.size()){
        const auto end=host.find('.',begin);auto label=host.substr(begin,end==host.npos?host.size()-begin:end-begin);
        if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-')return false;
        if(numeric){
            if(label.size()>3 || (label.size()>1&&label.front()=='0'))return false;
            unsigned value=0;for(char c:label)value=value*10+static_cast<unsigned>(c-'0');if(value>255)return false;
        }
        ++labels;if(end==host.npos)break;begin=end+1;
    }
    return !numeric || labels==4;
}
bool validProfileId(std::string_view id) noexcept {
    if(id.size()!=32)return false;
    for(char c:id) { if(!digit(c)&&!(c>='a'&&c<='f')) return false; }
    return true;
}
PaddedBytes PaddedBytes::copy(std::span<const std::uint8_t> data) {
    // Bound before allocation; config has a stricter bound at the ingress.
    if(data.size()>8u*1024u*1024u)throw std::length_error("Encoded sample exceeds 8 MiB");
    PaddedBytes p;p.size_=data.size();p.storage_.resize(data.size()+Padding,0);
    std::copy(data.begin(),data.end(),p.storage_.begin());return p;
}
bool PaddedBytes::paddingIsZero() const noexcept {
    if(storage_.size()<size_+Padding)return size_==0 && storage_.empty();
    return std::all_of(storage_.begin()+static_cast<std::ptrdiff_t>(size_),storage_.end(),[](auto b){return b==0;});
}
bool PcmBlock::valid() const noexcept {
    return generation>0 && (channels==1||channels==2) && rate>=8000 && rate<=192000 &&
        !samples.empty() && samples.size()%channels==0 && frames()<=rate/5u; // <=200 ms/block
}
} // namespace veyra::remoteplay
