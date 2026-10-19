#include "veyra/source/DirectShowFormatDiscovery.h"
#include <array>
#include <iostream>

using namespace veyra::source;
using namespace veyra::source::dshow;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
OwnedCaptureType media(bool info2=false,LONG width=1920,LONG height=1080,REFERENCE_TIME interval=166667){
    AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;
    type.subtype={captureFourcc('N','V','1','2'),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    type.formattype=info2?FORMAT_VideoInfo2:FORMAT_VideoInfo;
    type.bFixedSizeSamples=TRUE;
    VIDEOINFOHEADER v1{};VIDEOINFOHEADER2 v2{};
    auto& bitmap=info2?v2.bmiHeader:v1.bmiHeader;
    bitmap.biSize=sizeof(BITMAPINFOHEADER);bitmap.biWidth=width;bitmap.biHeight=height;bitmap.biBitCount=12;bitmap.biCompression=type.subtype.Data1;bitmap.biPlanes=1;
    v1.AvgTimePerFrame=v2.AvgTimePerFrame=interval;
    type.cbFormat=info2?sizeof(v2):sizeof(v1);type.pbFormat=reinterpret_cast<BYTE*>(info2?static_cast<void*>(&v2):static_cast<void*>(&v1));
    return cloneCaptureType(type);
}
AM_MEDIA_TYPE* detached(const AM_MEDIA_TYPE& type){
    auto owned=cloneCaptureType(type);if(!owned)return nullptr;
    auto* copy=static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if(!copy)return nullptr;*copy=*owned;owned->pbFormat=nullptr;owned->pUnk=nullptr;owned->cbFormat=0;return copy;
}
struct MediaEnum final:IEnumMediaTypes {
    std::vector<OwnedCaptureType> types;size_t index=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE Next(ULONG count,AM_MEDIA_TYPE** out,ULONG* fetched) override{
        if(fetched)*fetched=0;if(count!=1)return E_INVALIDARG;if(index>=types.size())return S_FALSE;
        *out=detached(*types[index++]);if(fetched)*fetched=1;return *out?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override{index+=count;return index<=types.size()?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset() override{index=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumMediaTypes**) override{return E_NOTIMPL;}
};
struct Driver final:IAMStreamConfig {
    HRESULT capsResult=S_OK;int failIndex=-1,bytes=sizeof(VIDEO_STREAM_CONFIG_CAPS);
    std::vector<OwnedCaptureType> types;
    VIDEO_STREAM_CONFIG_CAPS range{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** out) override{if(types.empty())return E_NOTIMPL;*out=detached(*types.front());return S_OK;}
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* count,int* size) override{*count=int(types.size());*size=bytes;return capsResult;}
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int index,AM_MEDIA_TYPE** out,BYTE* caps) override{
        if(index==failIndex)return E_FAIL;if(index<0||size_t(index)>=types.size())return E_INVALIDARG;
        if(bytes>=int(sizeof(range)))std::memcpy(caps,&range,sizeof(range));*out=detached(*types[size_t(index)]);return *out?S_OK:E_OUTOFMEMORY;
    }
};
struct Pin final:IPin,IKsPropertySet {
    Driver* driver=nullptr;MediaEnum enumerator;
    PIN_DIRECTION direction=PINDIR_OUTPUT;bool categorized=false;GUID category=PIN_CATEGORY_CAPTURE;unsigned enumCalls=0;HRESULT categoryResult=S_OK;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        *out=nullptr;
        if(id==IID_IAMStreamConfig&&driver)*out=static_cast<IAMStreamConfig*>(driver);
        else if(id==IID_IKsPropertySet&&categorized)*out=static_cast<IKsPropertySet*>(this);
        else if(id==IID_IPin||id==IID_IUnknown)*out=static_cast<IPin*>(this);
        else return E_NOINTERFACE;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE Connect(IPin*,const AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin*,const AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Disconnect() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin**) override{return VFW_E_NOT_CONNECTED;}
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* out) override{*out=direction;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes** out) override{++enumCalls;enumerator.index=0;*out=&enumerator;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin**,ULONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EndOfStream() override{return S_OK;}HRESULT STDMETHODCALLTYPE BeginFlush() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EndFlush() override{return S_OK;}HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME,REFERENCE_TIME,double) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Set(REFGUID,DWORD,LPVOID,DWORD,LPVOID,DWORD) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Get(REFGUID,DWORD,LPVOID,DWORD,LPVOID out,DWORD size,DWORD* bytes) override{
        if(FAILED(categoryResult))return categoryResult;
        if(size<sizeof(GUID))return E_INVALIDARG;std::memcpy(out,&category,sizeof(category));*bytes=sizeof(category);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID,DWORD,DWORD*) override{return E_NOTIMPL;}
};
struct PinEnum final:IEnumPins {
    std::vector<Pin*> pins;size_t index=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE Next(ULONG count,IPin** out,ULONG* fetched) override{
        if(fetched)*fetched=0;if(count!=1)return E_INVALIDARG;if(index>=pins.size())return S_FALSE;
        *out=static_cast<IPin*>(pins[index++]);if(fetched)*fetched=1;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override{index+=count;return S_OK;}
    HRESULT STDMETHODCALLTYPE Reset() override{index=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumPins**) override{return E_NOTIMPL;}
};
struct Filter final:IBaseFilter {
    PinEnum enumerator;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Stop() override{return S_OK;}HRESULT STDMETHODCALLTYPE Pause() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetState(DWORD,FILTER_STATE*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** out) override{enumerator.index=0;*out=&enumerator;return S_OK;}
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR,IPin**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph*,LPCWSTR) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR*) override{return E_NOTIMPL;}
};
}
int main(){
    Driver driver;Pin pin;pin.driver=&driver;pin.enumerator.types={media(false,1920,1080,0)};
    driver.capsResult=E_NOTIMPL;
    auto fallback=discoverCaptureTypes(&driver,&pin);
    check(fallback.types.size()==1&&fallback.fallback&&SUCCEEDED(fallback.result),"unimplemented caps use EnumMediaTypes");
    check(*captureVideoHeader(*fallback.types.front().media).interval==0,"unknown fallback rate is preserved");
    driver.capsResult=E_ACCESSDENIED;pin.enumCalls=0;
    const auto denied=discoverCaptureTypes(&driver,&pin);
    check(denied.types.empty()&&denied.result==E_ACCESSDENIED&&pin.enumCalls==0,"access denied is not concealed by fallback");
    driver.capsResult=S_OK;driver.bytes=0;
    check(discoverCaptureTypes(&driver,&pin).types.size()==1,"empty size-zero capability list can fall back");
    driver.bytes=sizeof(VIDEO_STREAM_CONFIG_CAPS);driver.types={media(true,0,0,333667)};
    driver.range.MinOutputSize={640,480};driver.range.MaxOutputSize={1920,1080};driver.range.OutputGranularityX=16;driver.range.OutputGranularityY=2;
    driver.range.MinFrameInterval=166666;driver.range.MaxFrameInterval=333667;
    auto ranged=discoverCaptureTypes(&driver,&pin);
    check(ranged.types.size()==1&&ranged.types.front().range.acceptsSize(1280,720),"range remains usable with an empty template size");
    auto resized=captureTypeForMode(ranged.types.front(),1280,720,166667);
    check(resized&&resized->lSampleSize==1280*720*3/2,"NV12 range request rebuilds image sample size");
    auto resizedHeader=captureVideoHeader(*resized);
    check(resizedHeader.bitmap->biWidth==1280&&resizedHeader.bitmap->biHeight==720&&*resizedHeader.interval==166667,"concrete type has selected dimensions and exact interval");
    check(captureVideoHeader(*ranged.types.front().media).bitmap->biWidth==0,"template is never mutated by a selection");
    check(!captureTypeForMode(ranged.types.front(),1279,720,166667),"out-of-step dimensions are rejected");
    auto same=cloneCaptureType(*resized);check(captureTypesMatch(*resized,*same),"exact connected contract accepted");
    ++*captureVideoHeader(*same).interval;check(captureTypesMatch(*resized,*same),"one 100ns integer rounding tick accepted");
    *captureVideoHeader(*same).interval=166833;check(!captureTypesMatch(*resized,*same),"59.94 cannot replace selected 60fps");
    same=cloneCaptureType(*resized);captureVideoHeader(*same).bitmap->biWidth=1920;check(!captureTypesMatch(*resized,*same),"size substitution rejected");
    same=cloneCaptureType(*resized);same->subtype=MEDIASUBTYPE_YUY2;check(!captureTypesMatch(*resized,*same),"pixel substitution rejected");
    same=cloneCaptureType(*resized);reinterpret_cast<VIDEOINFOHEADER2*>(same->pbFormat)->dwInterlaceFlags=AMINTERLACE_IsInterlaced;
    check(!captureTypesMatch(*resized,*same),"interlaced substitution rejected");
    driver.types.push_back(media());driver.failIndex=1;
    const auto partial=discoverCaptureTypes(&driver,&pin);
    check(partial.types.size()==1&&partial.failed==1&&FAILED(partial.result),"partial caps failure retains good types and an explicit error");
    driver.types.resize(1);driver.failIndex=0;
    const auto failedCapsFallback=discoverCaptureTypes(&driver,&pin);
    check(failedCapsFallback.types.size()==1&&failedCapsFallback.fallback&&failedCapsFallback.result==E_FAIL&&
          std::wstring_view(failedCapsFallback.stage)==L"GetStreamCaps","successful fallback preserves the real earlier failure stage");
    auto malformed=media();malformed->cbFormat=1;driver.types={malformed};driver.failIndex=-1;
    check(discoverCaptureTypes(&driver,&pin).fallback,"malformed format block is rejected before fallback");
    Driver normal;normal.types={media()};Pin preview,capture,unknown,other,input;
    for(auto* p:{&preview,&capture,&unknown,&other,&input}){p->driver=&normal;p->enumerator.types=normal.types;}
    preview.categorized=true;preview.category=PIN_CATEGORY_PREVIEW;capture.categorized=true;input.direction=PINDIR_INPUT;
    Filter filter;filter.enumerator.pins={&preview,&unknown,&capture};ComPtr<IPin> selected;
    check(SUCCEEDED(findCaptureVideoPin(&filter,selected))&&selected.Get()==static_cast<IPin*>(&capture),"explicit capture pin wins over preview and uncategorized video");
    filter.enumerator.pins={&preview,&input,&unknown};
    check(SUCCEEDED(findCaptureVideoPin(&filter,selected))&&selected.Get()==static_cast<IPin*>(&unknown),"unique uncategorized video output is compatible");
    unknown.categorized=true;unknown.categoryResult=E_PROP_ID_UNSUPPORTED;
    check(SUCCEEDED(findCaptureVideoPin(&filter,selected))&&selected.Get()==static_cast<IPin*>(&unknown),"missing category property is compatible");
    unknown.categoryResult=E_PROP_SET_UNSUPPORTED;
    check(SUCCEEDED(findCaptureVideoPin(&filter,selected))&&selected.Get()==static_cast<IPin*>(&unknown),"missing category property set is compatible");
    unknown.categoryResult=E_ACCESSDENIED;
    check(FAILED(findCaptureVideoPin(&filter,selected)),"category access failure is not treated as missing metadata");
    unknown.categorized=false;
    filter.enumerator.pins={&unknown,&other};
    check(FAILED(findCaptureVideoPin(&filter,selected))&&!selected,"ambiguous uncategorized outputs cannot be silently guessed");
    std::cout<<"DirectShow format discovery: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
