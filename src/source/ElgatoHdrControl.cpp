// Adapted from elgatosf/capture-device-support (MIT, Corsair Memory, Inc. 2022)
// and Nitlink (MIT, NitLink Contributors 2026). Pins and changes: THIRD_PARTY_NOTICES.md.
#include "veyra/source/ElgatoHdrControl.h"
#include "veyra/source/CaptureColorOverride.h"
#include "veyra/Log.h"
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <vidcap.h>
#include <algorithm>
#include <format>
#include <numeric>
#include <vector>

namespace veyra::source {
namespace {
constexpr GUID propertySet{0xD1E5209F,0x68FD,0x4529,{0xBE,0xE0,0x5E,0x7A,0x1F,0x47,0x92,0x26}};
constexpr DWORD packetLow=720,packetHigh=721,tonemap=722;
std::string narrowForLog(std::wstring_view value) {
    const int size=value.empty()?0:WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(std::max(size,0)),'\0');
    if(size>0)WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),size,nullptr,nullptr);
    return out;
}
void record(const char* op,DWORD id,HRESULT hr,DWORD size) {
    log::info("capture-elgato",std::format("{} property={} hr=0x{:08X} bytesOrFlags={}",op,id,uint32_t(hr),size));
}
HRESULT read(IKsPropertySet* ps,DWORD id,void* data,DWORD size) {
    DWORD returned=0;
    const HRESULT hr=ps->Get(propertySet,id,nullptr,0,data,size,&returned);
    record("Get",id,hr,returned);
    return FAILED(hr)?hr:returned==size?S_OK:HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
}
bool isElgatoMk2(std::wstring_view name) {
    for(const auto* alias:{L"Game Capture 4K60 Pro MK.2",L"Elgato Game Capture 4K60 Pro MK.2"})
        if(CompareStringOrdinal(name.data(),int(name.size()),alias,-1,TRUE)==CSTR_EQUAL)return true;
    return false;
}
bool isElgatoDevice(std::wstring_view name) {
    // Same identity rule as Nitlink plus the vendor's "Game Capture ..." filter
    // names (HD60 X/S+, 4K60 Pro/S+). Generic names never enter this path.
    std::wstring lower(name);
    std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return wchar_t(towlower(c));});
    return isElgatoMk2(name)||lower.find(L"elgato")!=std::wstring::npos||lower.rfind(L"game capture ",0)==0;
}
ElgatoHdrPacket parseElgatoHdrPacket(const std::array<unsigned char,32>& b) {
    // Unlike an absent/failed read, an all-zero packet is the driver's SDR report.
    if(std::all_of(b.begin(),b.end(),[](auto v){return v==0;}))return {true};
    if((b[0]&0x7f)!=7||b[1]!=1||b[2]!=26||b[5]!=0)return {};
    unsigned sum=0;for(unsigned i=0;i<30;++i)sum+=b[i];
    if((sum&255)!=0||(b[4]!=0&&b[4]!=2&&b[4]!=3))return {};
    const auto u16=[&](unsigned i){return float(unsigned(b[i])|(unsigned(b[i+1])<<8));};
    return {true,b[4],u16(22),u16(26),u16(28)};
}
bool readElgato4kxHdrPacket(IKsControl* control,ULONG node,std::array<unsigned char,32>& packet) {
    // Nitlink protocol: selector 2 carries a length, selector 1 the request or
    // reply; all bytes of a message sum to zero. Writes reach only this unit.
    constexpr GUID unit{0x961073C7,0x49F7,0x44F2,{0xAB,0x42,0xE9,0x40,0x40,0x59,0x40,0xC2}};
    const auto io=[&](ULONG selector,ULONG flag,void* data,ULONG size,ULONG* returned) {
        KSP_NODE p{};p.Property.Set=unit;p.Property.Id=selector;p.Property.Flags=flag|KSPROPERTY_TYPE_TOPOLOGY;p.NodeId=node;
        return control->KsProperty(&p.Property,sizeof(p),data,size,returned);
    };
    const auto round=[&](std::array<unsigned char,9> request,std::vector<unsigned char>& reply) {
        ULONG n=0;unsigned short length=static_cast<unsigned short>(request.size());
        // A card without this unit rejects the first write, so nothing else is sent.
        auto hr=io(2,KSPROPERTY_TYPE_SET,&length,sizeof(length),&n);
        if(FAILED(hr)){record("4K X XU length Set",2,hr,0);return false;}
        Sleep(3);
        hr=io(1,KSPROPERTY_TYPE_SET,request.data(),ULONG(request.size()),&n);
        if(FAILED(hr)){record("4K X XU request Set",1,hr,0);return false;}
        Sleep(3);
        length=0;
        for(int i=0;i<16&&!length;++i) {
            n=0;
            if(FAILED(io(2,KSPROPERTY_TYPE_GET,&length,sizeof(length),&n))||n<sizeof(length))length=0;
            if(!length)Sleep(4);
        }
        if(!length||length>256)return false;
        reply.assign(length,0);n=0;
        hr=io(1,KSPROPERTY_TYPE_GET,reply.data(),length,&n);
        reply.resize(SUCCEEDED(hr)?std::min<ULONG>(n,length):0);
        return !reply.empty();
    };
    std::vector<unsigned char> reply;
    // Readiness poll first, as the vendor application does; its reply is unused.
    if(!round({0xA0,0x06,0,0,0x67,0,0,0,0xF3},reply)||!round({0xA1,0x06,0,0,0x65,0,0,0,0xF4},reply))return false;
    const bool ok=reply.size()>=36&&reply[0]==0xA1&&reply[1]==0x22&&(std::accumulate(reply.begin(),reply.end(),0u)&255)==0;
    log::info("capture-elgato",std::format("4K X XU HDR reply bytes={} head={:02X}/{:02X} accepted={}",reply.size(),reply[0],reply.size()>1?reply[1]:0,ok));
    if(ok) {
        // The 4K X sends header then payload with no InfoFrame checksum byte (seen
        // on a PQ source: 87 01 1A 02 00...); the mailbox checksum covers it, so
        // restore the standard layout for parseElgatoHdrPacket.
        std::copy_n(reply.begin()+4,3,packet.begin());packet[3]=0;
        std::copy_n(reply.begin()+7,packet.size()-4,packet.begin()+4);
        packet[3]=static_cast<unsigned char>(0u-std::accumulate(packet.begin(),packet.begin()+30,0u));
    }
    return ok;
}
bool ElgatoHdrControl::setTonemap(DWORD desired,bool strict) {
    const bool originalKnown=SUCCEEDED(read(properties_.Get(),tonemap,&original_,sizeof(original_)))&&original_<=1;
    if(originalKnown&&original_==desired)return true;
    DWORD flags=0;auto hr=properties_->QuerySupported(propertySet,tonemap,&flags);
    record("QuerySupported",tonemap,hr,flags);
    // Official SDK issues Set directly. QuerySupported is advisory because
    // installed MK.2 drivers need not advertise every private property.
    DWORD value=desired;
    hr=properties_->Set(propertySet,tonemap,nullptr,0,&value,sizeof(value));
    record("Set",tonemap,hr,sizeof(value));
    if(FAILED(hr)) {
        // Cards without the vendor set (4K X/4K S answer E_PROP_SET_UNSUPPORTED)
        // keep their own state; only the MK.2 HDR contract depends on it.
        if(!strict)log::info("capture-elgato",std::format("hardware tonemap control not available on this card (hr=0x{:08X}); card state unchanged",uint32_t(hr)));
        return !strict;
    }
    restore_=originalKnown;
    DWORD actual=0;
    const auto readback=read(properties_.Get(),tonemap,&actual,sizeof(actual));
    if(SUCCEEDED(readback)&&actual!=desired) {
        log::error("capture-elgato","Tonemap readback disagrees; refusing a mislabeled color contract");
        return !strict;
    }
    if(!originalKnown)log::warn("capture-elgato","Tonemap original state unreadable; Set succeeded but close cannot restore an unknown value");
    return true;
}
bool ElgatoHdrControl::configure(IUnknown* filter,std::wstring_view name,bool p010,
                                unsigned overrideValue,pipeline::ColorDescription& color) {
    if(!filter||!isElgatoDevice(name))return true;
    if(!validCaptureColorOverride(overrideValue))return false;
    const bool mk2=isElgatoMk2(name);
    const unsigned space=captureColorSpace(overrideValue);
    // Nitlink: Elgato cards other than the MK.2 send HDR P010 with limited luma
    // and full-range chroma (MK.2 uses standard 64..960 chroma). An explicit
    // range choice by the user stays authoritative.
    if(p010&&!mk2&&!captureColorRange(overrideValue)&&color.range!=pipeline::ColorRange::Full) {
        color.fullRangeChroma=true;
        log::info("capture-elgato",std::format("non-MK.2 Elgato P010: limited luma, full-range chroma (Nitlink policy) device={}",narrowForLog(name)));
    }
    auto hr=filter->QueryInterface(IID_PPV_ARGS(&properties_));
    record("QueryInterface",0,hr,0);
    if(FAILED(hr)) {
        log::warn("capture-elgato","Elgato private control unavailable; keeping existing color contract");
        return true;
    }
    if(!p010) {
        // OBS libdshowcapture parity: an 8-bit format asks the card to tone map
        // an HDR source to SDR itself; without it HDR code values arrive as SDR.
        const bool ok=setTonemap(1,false);
        log::info("capture-elgato",std::format("8-bit format hardwareTonemap=1 requested (OBS parity) mk2={} applied={}",mk2,ok&&restore_));
        return true;
    }
    std::array<unsigned char,32> bytes{};
    const auto low=read(properties_.Get(),packetLow,bytes.data(),16);
    const auto high=read(properties_.Get(),packetHigh,bytes.data()+16,16);
    auto packet=SUCCEEDED(low)&&SUCCEEDED(high)?parseElgatoHdrPacket(bytes):ElgatoHdrPacket{};
    if(!packet.valid&&!mk2) {
        // The 4K X lacks the vendor property set; its extension unit is the
        // KSNODETYPE_DEV_SPECIFIC topology node of the same filter.
        Microsoft::WRL::ComPtr<IKsControl> control;Microsoft::WRL::ComPtr<IKsTopologyInfo> topology;DWORD nodes=0;
        if(SUCCEEDED(filter->QueryInterface(IID_PPV_ARGS(&control)))&&SUCCEEDED(filter->QueryInterface(IID_PPV_ARGS(&topology)))&&SUCCEEDED(topology->get_NumNodes(&nodes)))
            for(DWORD i=0;i<nodes;++i) {
                GUID type{};
                if(FAILED(topology->get_NodeType(i,&type))||type!=KSNODETYPE_DEV_SPECIFIC)continue;
                if(readElgato4kxHdrPacket(control.Get(),i,bytes))packet=parseElgatoHdrPacket(bytes);
                break;
            }
    }
    log::info("capture-elgato",std::format("HDR InfoFrame valid={} header={:02X}/{:02X}/{:02X} eotf={} peak={} maxCLL={} maxFALL={}",packet.valid,bytes[0],bytes[1],bytes[2],packet.eotf,packet.peak,packet.maxCll,packet.maxFall));
    const bool sourceHdr=packet.valid&&(packet.eotf==2||packet.eotf==3);
    // A bit depth alone never triggers HDR or a hardware write. Explicit space
    // is authoritative; auto only adopts a validated source InfoFrame.
    const bool wantHdr=space==1||space==2||(!space&&(sourceHdr||color.isHdrPath()));
    if(!wantHdr&&space!=3)return true;
    const DWORD desired=wantHdr?0:1;
    // Only the MK.2 contract is strict: other models may lack the vendor set.
    if(!setTonemap(desired,mk2))return false;
    if(!space&&sourceHdr) {
        const auto previous=color;
        applyCaptureColorOverride(color,packet.eotf==2?1:2);
        // InfoFrame declares EOTF, not YUV encoding or quantization. Preserve
        // explicit driver matrix/primaries; only fill their missing defaults.
        color.matrixAssumed=color.primariesAssumed=true;
        if(!previous.matrixAssumed&&previous.matrix!=pipeline::YuvMatrix::Unknown){color.matrix=previous.matrix;color.matrixAssumed=false;}
        if(!previous.primariesAssumed&&previous.primaries!=pipeline::ColorPrimaries::Unknown){color.primaries=previous.primaries;color.primariesAssumed=false;}
    }
    if(wantHdr&&sourceHdr&&(space==0||(space==1&&packet.eotf==2)||(space==2&&packet.eotf==3))) {
        color.hdrMasteringPeakNits=packet.peak;
        color.hdrMaxCllNits=packet.maxCll;
        color.hdrMaxFallNits=packet.maxFall;
    }
    log::info("capture-elgato",std::format("P010 hardwareTonemap={} mk2={} manualSpace={} sourceEotf={} nativeHdrRequested={} fullRangeChroma={}",desired,mk2,space,packet.eotf,wantHdr,color.fullRangeChroma));
    return true;
}
ElgatoHdrControl::~ElgatoHdrControl() {
    if(restore_&&properties_) {
        const auto hr=properties_->Set(propertySet,tonemap,nullptr,0,&original_,sizeof(original_));
        record("Restore",tonemap,hr,sizeof(original_));
    }
}
}
