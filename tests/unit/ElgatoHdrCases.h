#pragma once
#include "veyra/source/ElgatoHdrControl.h"
#include <ks.h>
#include <ksproxy.h>
#include <cstring>
#include <vector>

namespace {
struct ElgatoProperties final:IKsPropertySet {
    std::array<unsigned char,32> packet{};
    DWORD mode=1;unsigned gets=0,sets=0;
    bool shortPacket=false,readableMode=true,rejectSet=false,ignoreSet=false,queryFails=false;
    void hdr(unsigned eotf=2){
        packet={};packet[0]=0x87;packet[1]=1;packet[2]=26;packet[4]=static_cast<unsigned char>(eotf);
        packet[22]=0xe8;packet[23]=3;packet[26]=0x90;packet[27]=1;packet[28]=100;
        checksum();
    }
    void checksum(){packet[3]=0;unsigned sum=0;for(unsigned i=0;i<30;++i)sum+=packet[i];packet[3]=static_cast<unsigned char>(0-sum);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=IID_IUnknown&&id!=__uuidof(IKsPropertySet))return E_NOINTERFACE;
        *out=static_cast<IKsPropertySet*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return 2;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE Set(REFGUID id,DWORD prop,LPVOID instance,DWORD instanceSize,LPVOID data,DWORD size)override{
        ++sets;if(id.Data1!=0xD1E5209F||prop!=722||instance||instanceSize||size!=4)return E_INVALIDARG;
        if(rejectSet)return E_FAIL;if(!ignoreSet)std::memcpy(&mode,data,4);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Get(REFGUID id,DWORD prop,LPVOID instance,DWORD instanceSize,LPVOID data,DWORD size,DWORD* returned)override{
        ++gets;*returned=0;if(id.Data1!=0xD1E5209F||instance||instanceSize)return E_INVALIDARG;
        if((prop==720||prop==721)&&size==16){std::memcpy(data,packet.data()+(prop==720?0:16),16);*returned=shortPacket?15:16;return S_OK;}
        if(prop==722&&size==4&&readableMode){std::memcpy(data,&mode,4);*returned=4;return S_OK;}
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID,DWORD,DWORD* flags)override{
        *flags=KSPROPERTY_SUPPORT_GET|KSPROPERTY_SUPPORT_SET;return queryFails?E_NOTIMPL:S_OK;
    }
};
// 4K X extension unit mailbox: selector 2 = length, selector 1 = request/reply.
struct Elgato4kxMailbox final:IKsControl {
    std::array<unsigned char,32> packet{};
    std::vector<unsigned char> reply;
    unsigned calls=0;bool supported=true,corrupt=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 2;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE KsProperty(PKSPROPERTY property,ULONG propertySize,LPVOID data,ULONG size,ULONG* returned)override{
        ++calls;*returned=0;
        if(!supported||property->Set.Data1!=0x961073C7||propertySize!=sizeof(KSP_NODE)||reinterpret_cast<KSP_NODE*>(property)->NodeId!=3)return E_FAIL;
        const bool set=property->Flags&KSPROPERTY_TYPE_SET;const auto* bytes=static_cast<const unsigned char*>(data);
        if(set&&property->Id==2)return size==2&&bytes[0]==9?S_OK:E_INVALIDARG;
        if(set&&property->Id==1) {
            if(size!=9)return E_INVALIDARG;
            unsigned sum=0;for(unsigned i=0;i<9;++i)sum+=bytes[i];
            if(sum&255)return E_INVALIDARG;
            if(bytes[4]==0x67)reply={0xA0,0x06,0,0,0x04,0,0,0,0x56};
            else {
                // Real card layout: InfoFrame header then payload, no InfoFrame checksum byte.
                reply={0xA1,0x22,0,0};reply.insert(reply.end(),packet.begin(),packet.begin()+3);
                reply.insert(reply.end(),packet.begin()+4,packet.end());reply.push_back(0);
                sum=0;for(auto b:reply)sum+=b;
                reply.push_back(static_cast<unsigned char>((corrupt?1:0)-sum));
            }
            return S_OK;
        }
        if(property->Id==2&&size==2){const auto length=static_cast<unsigned short>(reply.size());std::memcpy(data,&length,2);*returned=2;return S_OK;}
        if(property->Id==1&&size==reply.size()){std::memcpy(data,reply.data(),size);*returned=size;return S_OK;}
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE KsMethod(PKSMETHOD,ULONG,LPVOID,ULONG,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsEvent(PKSEVENT,ULONG,LPVOID,ULONG,ULONG*)override{return E_NOTIMPL;}
};
template<class Check> void elgato4kxMailboxCases(Check check) {
    using namespace veyra;
    ElgatoProperties source;source.hdr();
    Elgato4kxMailbox box;box.packet=source.packet;
    std::array<unsigned char,32> bytes{};
    check(source::readElgato4kxHdrPacket(&box,3,bytes)&&bytes==source.packet&&source::parseElgatoHdrPacket(bytes).eotf==2,"4K X mailbox returns the source PQ InfoFrame");
    box.packet={};bytes.fill(1);
    check(source::readElgato4kxHdrPacket(&box,3,bytes)&&source::parseElgatoHdrPacket(bytes).valid&&!source::parseElgatoHdrPacket(bytes).eotf,"4K X mailbox zero packet is a valid SDR report");
    box.packet=source.packet;box.corrupt=true;bytes={};
    check(!source::readElgato4kxHdrPacket(&box,3,bytes)&&bytes==std::array<unsigned char,32>{},"4K X mailbox rejects a reply with a bad checksum");
    box={};box.supported=false;
    check(!source::readElgato4kxHdrPacket(&box,3,bytes)&&box.calls==1,"a card without the 4K X unit gets one rejected write and nothing more");
}
template<class Check> void elgatoHdrCases(Check check) {
    elgato4kxMailboxCases(check);
    using namespace veyra;
    constexpr auto name=L"Game Capture 4K60 Pro MK.2";
    const auto defaults=[] {pipeline::ColorDescription c;c.matrixAssumed=c.primariesAssumed=c.transferAssumed=true;c.transfer=pipeline::TransferFunction::SRGB;c.range=pipeline::ColorRange::Limited;return c;};
    check(source::isElgatoMk2(name)&&source::isElgatoMk2(L"elgato game capture 4k60 pro mk.2")&&!source::isElgatoMk2(L"Elgato 4K Pro"),"MK.2 exact device aliases only");
    check(source::isElgatoDevice(name)&&source::isElgatoDevice(L"Elgato 4K X")&&source::isElgatoDevice(L"Game Capture HD60 X")&&
          !source::isElgatoDevice(L"Other Capture")&&!source::isElgatoDevice(L"USB Video")&&!source::isElgatoDevice(L"AVerMedia Live Gamer 4K"),"Elgato identity: vendor names and the Game Capture family only");
    ElgatoProperties ps;ps.hdr();
    auto parsed=source::parseElgatoHdrPacket(ps.packet);
    check(parsed.valid&&parsed.eotf==2&&parsed.peak==1000&&parsed.maxCll==400&&parsed.maxFall==100,"HDR InfoFrame PQ and little endian mastering metadata");
    auto broken=ps.packet;broken[20]^=1;
    check(!source::parseElgatoHdrPacket(broken).valid,"reject corrupt InfoFrame checksum");
    broken=ps.packet;broken[2]=255;check(!source::parseElgatoHdrPacket(broken).valid,"reject unbounded packet length");
    ps.packet[0]=0x82;ps.checksum();check(!source::parseElgatoHdrPacket(ps.packet).valid,"reject other InfoFrame type");
    ps.hdr(1);check(!source::parseElgatoHdrPacket(ps.packet).valid,"unknown HDR gamma is not PQ");
    ps.hdr();auto color=defaults();
    {
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,0,color)&&ps.mode==0&&color.transfer==pipeline::TransferFunction::PQ&&color.range==pipeline::ColorRange::Limited&&color.hdrMaxCllNits==400&&!color.fullRangeChroma,"auto HDR disables card tonemap and propagates metadata without changing range (MK.2 keeps standard chroma)");
    }
    check(ps.mode==1&&ps.sets==2,"close restores readable initial hardware state");
    ps={};ps.hdr();ps.mode=0;color=defaults();
    color.range=pipeline::ColorRange::Full;color.matrix=pipeline::YuvMatrix::BT709;color.matrixAssumed=false;
    color.primaries=pipeline::ColorPrimaries::BT709;color.primariesAssumed=false;
    {
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,0,color)&&ps.sets==0&&color.range==pipeline::ColorRange::Full&&color.matrix==pipeline::YuvMatrix::BT709&&color.primaries==pipeline::ColorPrimaries::BT709&&!color.matrixAssumed&&!color.primariesAssumed,"vendor EOTF never overwrites explicit driver gamut/range or re-writes native mode");
    }
    ps={};color=defaults();color.transfer=pipeline::TransferFunction::PQ;color.transferAssumed=false;
    {
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,0,color)&&ps.mode==0,"driver-declared HDR also disables hardware tonemap without vendor HDR packet");
    }
    ps={};ps.hdr(3);color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,8,color)&&color.transfer==pipeline::TransferFunction::HLG,"range-only override still allows auto HLG detection");
    }
    ps={};ps.hdr();color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,L"Other Capture",true,1,color)&&ps.gets==0&&ps.sets==0,"other devices receive zero vendor calls");
    }
    ps={};ps.hdr();ps.mode=0;color=defaults();{
        // OBS libdshowcapture parity: 8-bit formats ask the card to tone map HDR.
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,false,0,color)&&ps.mode==1&&!color.isHdrPath()&&!color.fullRangeChroma,"8-bit format enables the hardware tonemapper");
    }
    check(ps.mode==0,"8-bit close restores the original tonemapper state");
    ps={};ps.mode=1;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,false,0,color)&&ps.sets==0,"8-bit with tonemapper already on writes nothing");
    }
    ps={};ps.hdr();ps.rejectSet=true;ps.readableMode=false;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,L"Elgato 4K X",false,0,color),"8-bit on a card without the vendor set still connects");
    }
    constexpr auto x4k=L"Elgato 4K X";
    ps={};ps.hdr();color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,x4k,true,0,color)&&ps.mode==0&&color.transfer==pipeline::TransferFunction::PQ&&color.range==pipeline::ColorRange::Limited&&color.fullRangeChroma,
              "non-MK.2 P010: tonemapper off and limited luma with full-range chroma (Nitlink)");
    }
    ps={};ps.hdr();color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,x4k,true,4,color)&&!color.fullRangeChroma,"explicit limited range keeps standard chroma on non-MK.2");
    }
    ps={};ps.hdr();color=defaults();color.range=pipeline::ColorRange::Full;color.rangeAssumed=false;{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,x4k,true,0,color)&&!color.fullRangeChroma,"driver-declared full range is left as full");
    }
    ps={};ps.rejectSet=true;ps.readableMode=false;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,x4k,true,1,color)&&color.fullRangeChroma,"non-MK.2 without the vendor set connects with explicit PQ (control unavailable is not fatal)");
    }
    ps={};ps.hdr();color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,L"Other Capture",false,0,color)&&ps.gets==0&&ps.sets==0&&!color.fullRangeChroma,"other devices receive zero vendor calls for 8-bit formats");
    }
    ps={};color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,0,color)&&ps.sets==0&&color.transfer==pipeline::TransferFunction::SRGB,"zero SDR packet never enables HDR");
    }
    ps={};ps.hdr();ps.shortPacket=true;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,0,color)&&ps.sets==0&&color.transferAssumed,"short vendor reads never establish HDR");
    }
    ps={};ps.hdr();ps.rejectSet=true;color=defaults();{
        source::ElgatoHdrControl control;
        check(!control.configure(&ps,name,true,0,color)&&color.transferAssumed,"failed hardware change cannot relabel SDR output PQ");
    }
    ps={};ps.hdr();ps.ignoreSet=true;color=defaults();{
        source::ElgatoHdrControl control;
        check(!control.configure(&ps,name,true,1,color),"reject successful Set with contradictory readback");
    }
    ps={};ps.hdr();ps.mode=0;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,3,color)&&ps.mode==1&&!color.isHdrPath(),"manual Rec709 requests hardware SDR instead of relabeling raw PQ");
    }
    check(ps.mode==0,"manual SDR close restores original native mode");
    ps={};ps.queryFails=true;ps.readableMode=false;color=defaults();{
        source::ElgatoHdrControl control;
        check(control.configure(&ps,name,true,1,color)&&ps.mode==0,"explicit PQ works with write-only property and broken QuerySupported");
    }
    check(ps.sets==1,"unknown original state is never fabricated on restore");
}
}
