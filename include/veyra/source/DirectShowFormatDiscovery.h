#pragma once
// Capability-only compatibility informed by OBS 32.2.2's libdshowcapture
// 8878638324393815512f802640b0d5ce940161f1, dshow-enum.cpp / dshow-base.cpp.
// Independent implementation: no OBS capture, buffering or rendering code.
#include "veyra/source/CaptureCapabilityRange.h"
#include "veyra/source/CapturePixelFormat.h"
#include <dvdmedia.h>
#include <ks.h>
#include <ksproxy.h>
#include <wrl/client.h>
#include <cstring>
#include <memory>

namespace veyra::source::dshow {
using Microsoft::WRL::ComPtr;
using OwnedCaptureType=std::shared_ptr<AM_MEDIA_TYPE>;
inline void destroyCaptureType(AM_MEDIA_TYPE* type){
    if(!type)return;
    CoTaskMemFree(type->pbFormat);if(type->pUnk)type->pUnk->Release();CoTaskMemFree(type);
}
inline OwnedCaptureType ownCaptureType(AM_MEDIA_TYPE* type){return {type,destroyCaptureType};}
inline OwnedCaptureType cloneCaptureType(const AM_MEDIA_TYPE& type){
    if(type.cbFormat>1024*1024||(type.cbFormat&&!type.pbFormat))return {};
    auto* copy=static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));if(!copy)return {};
    *copy=type;copy->pbFormat=nullptr;copy->pUnk=nullptr;
    if(type.cbFormat){copy->pbFormat=static_cast<BYTE*>(CoTaskMemAlloc(type.cbFormat));
        if(!copy->pbFormat){CoTaskMemFree(copy);return {};}std::memcpy(copy->pbFormat,type.pbFormat,type.cbFormat);}
    copy->pUnk=type.pUnk;if(copy->pUnk)copy->pUnk->AddRef();return ownCaptureType(copy);
}
struct CaptureVideoHeader {BITMAPINFOHEADER* bitmap=nullptr;REFERENCE_TIME* interval=nullptr;DWORD scan=0;};
inline CaptureVideoHeader captureVideoHeader(AM_MEDIA_TYPE& type){
    if(type.majortype!=MEDIATYPE_Video||!type.pbFormat||type.cbFormat>1024*1024)return {};
    if(type.formattype==FORMAT_VideoInfo&&type.cbFormat>=sizeof(VIDEOINFOHEADER)){
        auto* header=reinterpret_cast<VIDEOINFOHEADER*>(type.pbFormat);return {&header->bmiHeader,&header->AvgTimePerFrame,0};}
    if(type.formattype==FORMAT_VideoInfo2&&type.cbFormat>=sizeof(VIDEOINFOHEADER2)){
        auto* header=reinterpret_cast<VIDEOINFOHEADER2*>(type.pbFormat);return {&header->bmiHeader,&header->AvgTimePerFrame,header->dwInterlaceFlags};}
    return {};
}
inline bool capturePinHasVideo(IPin* pin){
    ComPtr<IAMStreamConfig> config;
    if(SUCCEEDED(pin->QueryInterface(IID_PPV_ARGS(&config)))){
        AM_MEDIA_TYPE* raw=nullptr;const auto hr=config->GetFormat(&raw);auto current=ownCaptureType(raw);
        if(SUCCEEDED(hr)&&current&&current->majortype==MEDIATYPE_Video)return true;
        int count=0,size=0;
        if(SUCCEEDED(config->GetNumberOfCapabilities(&count,&size))&&count>0&&count<=65536&&size>0&&size<=65536){
            std::vector<BYTE> bytes(size);
            for(int i=0;i<count;++i){raw=nullptr;const auto result=config->GetStreamCaps(i,&raw,bytes.data());auto type=ownCaptureType(raw);
                if(SUCCEEDED(result)&&type&&type->majortype==MEDIATYPE_Video)return true;}
        }
    }
    ComPtr<IEnumMediaTypes> types;
    if(FAILED(pin->EnumMediaTypes(&types)))return false;
    for(unsigned i=0;i<65536;++i){AM_MEDIA_TYPE* raw=nullptr;const auto hr=types->Next(1,&raw,nullptr);auto type=ownCaptureType(raw);
        if(hr!=S_OK)return false;if(type&&type->majortype==MEDIATYPE_Video)return true;}
    return false;
}
inline HRESULT findCaptureVideoPin(IBaseFilter* filter,ComPtr<IPin>& result){
    result.Reset();if(!filter)return E_POINTER;ComPtr<IEnumPins> pins;auto hr=filter->EnumPins(&pins);if(FAILED(hr))return hr;
    ComPtr<IPin> fallback;unsigned fallbackCount=0;
    for(unsigned i=0;i<65536;++i){ComPtr<IPin> pin;hr=pins->Next(1,&pin,nullptr);if(hr!=S_OK)break;
        PIN_DIRECTION direction{};if(FAILED(pin->QueryDirection(&direction))||direction!=PINDIR_OUTPUT||!capturePinHasVideo(pin.Get()))continue;
        ComPtr<IKsPropertySet> properties;GUID category{};DWORD bytes=0;
        hr=pin.As(&properties);
        if(SUCCEEDED(hr))hr=properties->Get(AMPROPSETID_Pin,AMPROPERTY_PIN_CATEGORY,nullptr,0,&category,sizeof(category),&bytes);
        if(SUCCEEDED(hr)&&bytes==sizeof(category)){if(category==PIN_CATEGORY_CAPTURE){result=pin;return S_OK;}continue;}
        if(hr==E_NOINTERFACE||hr==E_NOTIMPL||hr==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)||
           hr==E_PROP_ID_UNSUPPORTED||hr==E_PROP_SET_UNSUPPORTED){
            fallback=pin;++fallbackCount;
        }
    }
    if(fallbackCount==1){result=fallback;return S_OK;}
    return fallbackCount>1?E_UNEXPECTED:VFW_E_NOT_FOUND;
}
enum class CaptureTypeOrigin { StreamCaps, MediaTypes };
struct CaptureNativeCapability {
    OwnedCaptureType media;
    CaptureCapabilityRange range;
    int nativeIndex=0;
    CaptureTypeOrigin origin=CaptureTypeOrigin::StreamCaps;
};
struct CaptureNativeCapabilities {
    std::vector<CaptureNativeCapability> types;
    HRESULT result=S_OK;
    const wchar_t* stage=L"GetStreamCaps";
    unsigned advertised=0,rejected=0,failed=0;
    bool fallback=false;
};
inline CaptureNativeCapabilities discoverCaptureTypes(IAMStreamConfig* config,IPin* pin){
    CaptureNativeCapabilities found;
    if(!config){found.result=E_NOINTERFACE;found.stage=L"IAMStreamConfig";return found;}
    int count=0,size=0;auto hr=config->GetNumberOfCapabilities(&count,&size);
    if(SUCCEEDED(hr)&&(count<0||count>65536||size<0||size>65536||(count>0&&size<1))){found.result=E_INVALIDARG;found.stage=L"GetNumberOfCapabilities";return found;}
    if(FAILED(hr)&&hr!=E_NOTIMPL&&hr!=E_NOINTERFACE){found.result=hr;found.stage=L"GetNumberOfCapabilities";return found;}
    if(SUCCEEDED(hr)){
        std::vector<BYTE> bytes(size);size_t allocated=0;
        for(int i=0;i<count;++i){
            AM_MEDIA_TYPE* raw=nullptr;std::fill(bytes.begin(),bytes.end(),0);
            const auto result=config->GetStreamCaps(i,&raw,bytes.data());auto media=ownCaptureType(raw);++found.advertised;
            if(FAILED(result)||!media){++found.failed;found.result=FAILED(result)?result:E_POINTER;continue;}
            const auto header=captureVideoHeader(*media);
            if(!header.bitmap){++found.rejected;continue;}
            allocated+=media->cbFormat;
            if(allocated>64*1024*1024){found.result=E_OUTOFMEMORY;++found.failed;break;}
            CaptureCapabilityRange range;
            if(bytes.size()>=sizeof(VIDEO_STREAM_CONFIG_CAPS)){
                VIDEO_STREAM_CONFIG_CAPS caps{};std::memcpy(&caps,bytes.data(),sizeof(caps));
                if(caps.MinOutputSize.cx>0&&caps.MaxOutputSize.cx>0&&caps.MinOutputSize.cy>0&&caps.MaxOutputSize.cy>0){
                    range.minWidth=unsigned(caps.MinOutputSize.cx);range.maxWidth=unsigned(caps.MaxOutputSize.cx);
                    range.minHeight=unsigned(caps.MinOutputSize.cy);range.maxHeight=unsigned(caps.MaxOutputSize.cy);
                    range.stepX=unsigned(std::max(1,caps.OutputGranularityX));range.stepY=unsigned(std::max(1,caps.OutputGranularityY));
                }
                range.minInterval=caps.MinFrameInterval;range.maxInterval=caps.MaxFrameInterval;
            }
            found.types.push_back({std::move(media),range,i,CaptureTypeOrigin::StreamCaps});
        }
    }
    // Explicitly unsupported/empty capability lists and entirely unusable lists.
    // Access denied/device-busy failures above do not become fictitious success.
    if(found.types.empty()&&pin){
        const auto previousStage=found.stage;const auto previousFailures=found.failed;
        found.fallback=true;found.stage=L"EnumMediaTypes";ComPtr<IEnumMediaTypes> enumerator;
        hr=pin->EnumMediaTypes(&enumerator);
        if(FAILED(hr)){found.result=hr;return found;}
        size_t allocated=0;
        int i=0;
        for(;i<65536;++i){AM_MEDIA_TYPE* raw=nullptr;hr=enumerator->Next(1,&raw,nullptr);auto media=ownCaptureType(raw);
            if(hr==S_FALSE)break;
            if(FAILED(hr)||!media){found.result=FAILED(hr)?hr:E_POINTER;++found.failed;break;}
            ++found.advertised;
            if(!captureVideoHeader(*media).bitmap){++found.rejected;continue;}
            allocated+=media->cbFormat;if(allocated>64*1024*1024){found.result=E_OUTOFMEMORY;++found.failed;break;}
            found.types.push_back({std::move(media),{},i,CaptureTypeOrigin::MediaTypes});
        }
        if(i==65536){found.result=HRESULT_FROM_WIN32(ERROR_MORE_DATA);++found.failed;}
        if(previousFailures&&found.failed==previousFailures)found.stage=previousStage;
    }
    if(!found.failed)found.result=S_OK;
    return found;
}
inline OwnedCaptureType captureTypeForMode(const CaptureNativeCapability& capability,unsigned width,unsigned height,int64_t interval){
    auto result=cloneCaptureType(*capability.media);if(!result)return {};
    auto header=captureVideoHeader(*result);if(!header.bitmap||!width||!height||width>16384||height>16384||interval<0)return {};
    auto& bitmap=*header.bitmap;
    const auto oldHeight=std::abs(int64_t(bitmap.biHeight));
    if(unsigned(bitmap.biWidth)!=width||oldHeight!=height){
        if(!capability.range.acceptsSize(width,height))return {};
        bitmap.biWidth=LONG(width);bitmap.biHeight=bitmap.biHeight<0?-LONG(height):LONG(height);
        const auto packing=capturePacking(result->subtype);uint64_t bytes=0;
        switch(packing){
        case CapturePacking::Nv12:case CapturePacking::Nv21:case CapturePacking::I420:case CapturePacking::Yv12:
            if(width%2||height%2)return {};bytes=uint64_t(width)*height*3/2;break;
        case CapturePacking::P010:case CapturePacking::P016:
            if(width%2||height%2)return {};bytes=uint64_t(width)*height*3;break;
        case CapturePacking::Yuy2:case CapturePacking::Uyvy:case CapturePacking::Yvyu:
            if(width%2)return {};bytes=uint64_t(width)*height*2;break;
        case CapturePacking::V210:bytes=uint64_t((width+47)/48)*128*height;break;
        case CapturePacking::Rgb10xLE:bytes=uint64_t(width)*height*4;break;
        case CapturePacking::Unknown:bytes=0;break;
        default:bytes=((uint64_t(width)*bitmap.biBitCount+31)/32)*4*height;break;
        }
        if(bytes>std::numeric_limits<DWORD>::max())return {};
        bitmap.biSizeImage=DWORD(bytes);result->lSampleSize=ULONG(bytes);
        if(result->formattype==FORMAT_VideoInfo){auto* info=reinterpret_cast<VIDEOINFOHEADER*>(result->pbFormat);info->rcSource={};info->rcTarget={};}
        else {auto* info=reinterpret_cast<VIDEOINFOHEADER2*>(result->pbFormat);info->rcSource={};info->rcTarget={};info->dwPictAspectRatioX=width;info->dwPictAspectRatioY=height;}
    }
    *header.interval=interval;return result;
}
inline bool captureTypesMatch(const AM_MEDIA_TYPE& requested,const AM_MEDIA_TYPE& actual){
    auto wanted=captureVideoHeader(const_cast<AM_MEDIA_TYPE&>(requested));
    auto got=captureVideoHeader(const_cast<AM_MEDIA_TYPE&>(actual));
    return wanted.bitmap&&got.bitmap&&requested.subtype==actual.subtype&&
        wanted.bitmap->biWidth==got.bitmap->biWidth&&std::abs(int64_t(wanted.bitmap->biHeight))==std::abs(int64_t(got.bitmap->biHeight))&&
        wanted.scan==got.scan&&(*wanted.interval<=0||(*got.interval>0&&std::abs(*wanted.interval-*got.interval)<=1));
}
}
