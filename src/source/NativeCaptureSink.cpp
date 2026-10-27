#include "veyra/source/NativeCaptureSink.h"
#include "veyra/source/CaptureMediaType.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/BitstreamAudio.h"
#include <format>
#include <mmreg.h>
#include <atomic>
#include <mutex>
#include <string>

namespace veyra::source {
using Microsoft::WRL::ComPtr;
std::function<HRESULT(IMediaSample*)> timedCaptureDelivery(std::function<HRESULT(double,IMediaSample*)> deliver){
    return [deliver=std::move(deliver),untimed=uint64_t{0},pending=uint64_t{0},recoveries=uint64_t{0}](IMediaSample* sample) mutable -> HRESULT {
        if(!sample)return E_POINTER;
        REFERENCE_TIME begin=0,end=0;
        const auto hr=sample->GetTime(&begin,&end);
        if(hr==VFW_E_SAMPLE_TIME_NOT_SET){
            ++untimed;++pending;
            if(untimed==1||untimed%300==0)
                log::warn("capture-video-time",std::format("untimedSamples={} GetTime hr=0x{:08X} bytes={} acknowledged=1 delivered=0 (waiting for real video timestamps)",untimed,uint32_t(hr),sample->GetActualDataLength()));
            return S_OK;
        }
        if(FAILED(hr)){
            log::error("capture-video-time",std::format("GetTime hr=0x{:08X}",uint32_t(hr)));
            return hr;
        }
        if(pending){
            ++recoveries;
            if(recoveries==1||recoveries%300==0)
                log::info("capture-video-time",std::format("timed video resumed after {} untimed samples firstPts100ns={} recoveries={} fabricatedPts=0",pending,begin,recoveries));
            pending=0;
        }
        return deliver(double(begin)/1e7,sample);
    };
}
bool nativeCaptureContractsMatch(const AM_MEDIA_TYPE& a,const AM_MEDIA_TYPE& b){
    if(a.subtype!=b.subtype||!equivalentCaptureTypes(a,b))return false;
    const auto fields=[](const AM_MEDIA_TYPE& t){return t.formattype==FORMAT_VideoInfo2&&t.pbFormat&&t.cbFormat>=sizeof(VIDEOINFOHEADER2)?reinterpret_cast<const VIDEOINFOHEADER2*>(t.pbFormat)->dwInterlaceFlags:DWORD(0);};
    return fields(a)==fields(b);
}
namespace {
LONG audioBlockBytes(const WAVEFORMATEX& f){
    // Round up to whole sample frames, including uncommon rates such as 11025.
    return LONG(((uint64_t(f.nSamplesPerSec)+99)/100)*f.nBlockAlign);
}
void clearType(AM_MEDIA_TYPE& t){CoTaskMemFree(t.pbFormat);if(t.pUnk)t.pUnk->Release();t={};}
HRESULT copyType(AM_MEDIA_TYPE& dst,const AM_MEDIA_TYPE& src){
    dst=src;dst.pbFormat=nullptr;dst.pUnk=nullptr;
    if(src.cbFormat){if(!src.pbFormat)return E_INVALIDARG;dst.pbFormat=static_cast<BYTE*>(CoTaskMemAlloc(src.cbFormat));if(!dst.pbFormat)return E_OUTOFMEMORY;memcpy(dst.pbFormat,src.pbFormat,src.cbFormat);}
    dst.pUnk=src.pUnk;if(dst.pUnk)dst.pUnk->AddRef();return S_OK;
}
bool audioType(const AM_MEDIA_TYPE& t){
    if(t.majortype!=MEDIATYPE_Audio||t.formattype!=FORMAT_WaveFormatEx||!t.pbFormat||t.cbFormat<sizeof(WAVEFORMATEX))return false;
    sink::WavePcmFormat parsed;return sink::parseWavePcm(t.pbFormat,t.cbFormat,parsed);
}
bool sameAudio(const AM_MEDIA_TYPE& a,const AM_MEDIA_TYPE& b){
    return audioType(a)&&audioType(b)&&a.subtype==b.subtype&&
        a.cbFormat==b.cbFormat&&std::memcmp(a.pbFormat,b.pbFormat,a.cbFormat)==0;
}
bool sameCompressed(const AM_MEDIA_TYPE& a,const AM_MEDIA_TYPE& b){
    return a.majortype==b.majortype&&a.subtype==b.subtype&&a.formattype==b.formattype&&a.cbFormat==b.cbFormat&&
        (a.cbFormat==0||(a.pbFormat&&b.pbFormat&&std::memcmp(a.pbFormat,b.pbFormat,a.cbFormat)==0));
}
class PinEnum final:public IEnumPins {
    std::atomic<ULONG> refs_{1};ComPtr<IPin> pin_;bool used_=false;
public:
    explicit PinEnum(IPin* p,bool used=false):pin_(p),used_(used){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IEnumPins)return E_NOINTERFACE;*p=static_cast<IEnumPins*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Next(ULONG n,IPin** p,ULONG* fetched)override{if(!p||(!fetched&&n!=1))return E_POINTER;if(fetched)*fetched=0;if(!n)return S_OK;if(used_)return S_FALSE;*p=pin_.Get();(*p)->AddRef();used_=true;if(fetched)*fetched=1;return n==1?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Skip(ULONG n)override{if(!n)return S_OK;const bool one=!used_&&n==1;used_=true;return one?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset()override{used_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumPins** p)override{if(!p)return E_POINTER;*p=new PinEnum(pin_.Get(),used_);return S_OK;}
};
class TypeEnum final:public IEnumMediaTypes {
    std::atomic<ULONG> refs_{1};AM_MEDIA_TYPE type_{};bool used_=false;
public:
    TypeEnum(const AM_MEDIA_TYPE& type,bool used=false):used_(used){if(FAILED(copyType(type_,type)))throw std::bad_alloc();}
    ~TypeEnum(){clearType(type_);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IEnumMediaTypes)return E_NOINTERFACE;*p=static_cast<IEnumMediaTypes*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Next(ULONG n,AM_MEDIA_TYPE** p,ULONG* fetched)override{if(!p||(!fetched&&n!=1))return E_POINTER;if(fetched)*fetched=0;if(!n)return S_OK;if(used_)return S_FALSE;*p=static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));if(!*p)return E_OUTOFMEMORY;const auto hr=copyType(**p,type_);if(FAILED(hr)){CoTaskMemFree(*p);*p=nullptr;return hr;}used_=true;if(fetched)*fetched=1;return n==1?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Skip(ULONG n)override{if(!n)return S_OK;const bool one=!used_&&n==1;used_=true;return one?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset()override{used_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumMediaTypes** p)override{if(!p)return E_POINTER;try{*p=new TypeEnum(type_,used_);return S_OK;}catch(...){*p=nullptr;return E_OUTOFMEMORY;}}
};
class NativeSink final:public IBaseFilter,public IPin,public IMemInputPin,public IAMFilterMiscFlags {
    std::atomic<ULONG> refs_{1};std::recursive_mutex mutex_;
    FILTER_STATE state_=State_Stopped;bool flushing_=false;
    AM_MEDIA_TYPE desired_{},connected_{};CaptureMediaLayout layout_;
    ComPtr<IPin> peer_;ComPtr<IMemAllocator> allocator_;ComPtr<IReferenceClock> clock_;
    IFilterGraph* graph_=nullptr;std::wstring name_=L"Native capture mailbox";
    std::function<HRESULT(IMediaSample*)> callback_;
    bool audio_=false;
    bool compressed_=false;
    uint64_t videoSamples_=0;
    CaptureAllocatorMode allocatorMode_=CaptureAllocatorMode::SinkDefault;
public:
    NativeSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> cb,bool compressed=false,CaptureAllocatorMode allocatorMode=CaptureAllocatorMode::SinkDefault):callback_(std::move(cb)),allocatorMode_(allocatorMode){
        compressed_=compressed;audio_=!compressed&&audioType(type);
        if(compressed_){
            if(FAILED(copyType(desired_,type)))throw std::bad_alloc();
        }else{
            if((!audio_&&!captureMediaLayout(type,layout_))||FAILED(copyType(desired_,type)))throw std::bad_alloc();
            if(audio_)layout_.sampleBytes=reinterpret_cast<const WAVEFORMATEX*>(type.pbFormat)->nBlockAlign;
        }
    }
    ~NativeSink(){clearType(desired_);clearType(connected_);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{
        if(!p)return E_POINTER;*p=nullptr;
        if(id==IID_IUnknown||id==IID_IPersist||id==IID_IMediaFilter||id==IID_IBaseFilter)*p=static_cast<IBaseFilter*>(this);
        else if(id==IID_IPin)*p=static_cast<IPin*>(this);else if(id==IID_IMemInputPin)*p=static_cast<IMemInputPin*>(this);
        else if(id==IID_IAMFilterMiscFlags&&allocatorMode_==CaptureAllocatorMode::Upstream)*p=static_cast<IAMFilterMiscFlags*>(this);else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* p)override{if(!p)return E_POINTER;*p={0x02a47523,0x92af,0x4c6e,{0x97,0xe3,0x1a,0x14,0xf1,0x2d,0xab,0x5a}};return S_OK;}
    HRESULT STDMETHODCALLTYPE Stop()override{std::lock_guard lock(mutex_);state_=State_Stopped;return S_OK;}
    HRESULT STDMETHODCALLTYPE Pause()override{std::lock_guard lock(mutex_);state_=State_Paused;flushing_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME)override{std::lock_guard lock(mutex_);state_=State_Running;flushing_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetState(DWORD,FILTER_STATE* p)override{if(!p)return E_POINTER;std::lock_guard lock(mutex_);*p=state_;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock* p)override{std::lock_guard lock(mutex_);clock_=p;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock** p)override{if(!p)return E_POINTER;std::lock_guard lock(mutex_);return clock_.CopyTo(p);}
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** p)override{if(!p)return E_POINTER;*p=new(std::nothrow) PinEnum(this);return *p?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR id,IPin** p)override{if(!id||!p)return E_POINTER;*p=nullptr;if(wcscmp(id,L"Input"))return VFW_E_NOT_FOUND;*p=static_cast<IPin*>(this);AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO* p)override{if(!p)return E_POINTER;std::lock_guard lock(mutex_);*p={};wcsncpy_s(p->achName,name_.c_str(),_TRUNCATE);p->pGraph=graph_;if(graph_)graph_->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph* p,LPCWSTR name)override{std::lock_guard lock(mutex_);graph_=p;name_=name?name:L"Native capture mailbox";return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR* p)override{if(p)*p=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Connect(IPin*,const AM_MEDIA_TYPE*)override{return E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin* peer,const AM_MEDIA_TYPE* type)override{
        if(!peer||!type)return E_POINTER;std::lock_guard lock(mutex_);if(state_!=State_Stopped)return VFW_E_NOT_STOPPED;if(peer_)return VFW_E_ALREADY_CONNECTED;
        PIN_DIRECTION direction;if(FAILED(peer->QueryDirection(&direction))||direction!=PINDIR_OUTPUT)return VFW_E_INVALID_DIRECTION;
        if(QueryAccept(type)!=S_OK)return VFW_E_TYPE_NOT_ACCEPTED;
        const auto hr=copyType(connected_,*type);if(FAILED(hr))return hr;peer_=peer;if(!audio_&&!compressed_)captureMediaLayout(*type,layout_);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Disconnect()override{std::lock_guard lock(mutex_);if(state_!=State_Stopped)return VFW_E_NOT_STOPPED;const bool connected=bool(peer_);peer_.Reset();allocator_.Reset();clearType(connected_);return connected?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin** p)override{if(!p)return E_POINTER;std::lock_guard lock(mutex_);*p=nullptr;return peer_?peer_.CopyTo(p):VFW_E_NOT_CONNECTED;}
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE* p)override{if(!p)return E_POINTER;std::lock_guard lock(mutex_);*p={};return peer_?copyType(*p,connected_):VFW_E_NOT_CONNECTED;}
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO* p)override{if(!p)return E_POINTER;*p={};p->pFilter=this;AddRef();p->dir=PINDIR_INPUT;wcscpy_s(p->achName,L"Input");return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* p)override{if(!p)return E_POINTER;*p=PINDIR_INPUT;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR* p)override{if(!p)return E_POINTER;*p=static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"Input")));if(!*p)return E_OUTOFMEMORY;memcpy(*p,L"Input",sizeof(L"Input"));return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE* p)override{
        if(!p)return E_POINTER;std::lock_guard lock(mutex_);if(compressed_)return sameCompressed(*p,peer_?connected_:desired_)?S_OK:S_FALSE;if(audio_)return sameAudio(*p,peer_?connected_:desired_)?S_OK:S_FALSE;if(peer_)return (allocatorMode_==CaptureAllocatorMode::Upstream?nativeCaptureContractsMatch(*p,connected_):equivalentCaptureTypes(*p,connected_))?S_OK:S_FALSE;CaptureMediaLayout candidate;
        return captureMediaLayout(*p,candidate)&&p->subtype==desired_.subtype&&candidate.width==layout_.width&&candidate.height==layout_.height?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes** p)override{if(!p)return E_POINTER;try{*p=new TypeEnum(desired_);return S_OK;}catch(...){*p=nullptr;return E_OUTOFMEMORY;}}
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin**,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EndOfStream()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE BeginFlush()override{std::lock_guard lock(mutex_);flushing_=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE EndFlush()override{std::lock_guard lock(mutex_);flushing_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME,REFERENCE_TIME,double)override{return S_OK;}
    // Adapted from OBS libdshowcapture c13d4b7b0c66979396ba0a9060c9aafc15bb7b22,
    // CapturePin allocator methods (Lain Bailey, LGPL-2.1-or-later).
    // Unlike OBS, retain the selected allocator for truthful post-connect
    // diagnostics. Never replace a driver pool with the generic allocator.
    HRESULT STDMETHODCALLTYPE GetAllocator(IMemAllocator** p)override{
        if(!p)return E_POINTER;*p=nullptr;std::lock_guard lock(mutex_);
        if(!allocator_){
            if(allocatorMode_==CaptureAllocatorMode::Upstream)return VFW_E_NO_ALLOCATOR;
            const auto hr=CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator_));if(FAILED(hr))return hr;
        }
        return allocator_.CopyTo(p);
    }
    HRESULT STDMETHODCALLTYPE NotifyAllocator(IMemAllocator* p,BOOL readOnly)override{
        if(!p)return E_POINTER;std::lock_guard lock(mutex_);allocator_=p;
        if(allocatorMode_==CaptureAllocatorMode::Upstream){
            ALLOCATOR_PROPERTIES actual{};const auto hr=p->GetProperties(&actual);
            log::info("capture-bmd-allocator",std::format("upstreamSelected=1 readOnly={} buffers={} bytes={} align={} prefix={} hr=0x{:08X}",bool(readOnly),actual.cBuffers,actual.cbBuffer,actual.cbAlign,actual.cbPrefix,uint32_t(hr)));
        }
        if(audio_){
            ALLOCATOR_PROPERTIES actual{};const auto hr=p->GetProperties(&actual);
            const auto& f=*reinterpret_cast<const WAVEFORMATEX*>(desired_.pbFormat);
            log::info("capture-audio-buffer",std::format("actual allocator hr=0x{:08X} buffers={} bytes={} capacityPerBlockMs={:.3f} (capacity, not measured latency)",uint32_t(hr),actual.cBuffers,actual.cbBuffer,SUCCEEDED(hr)?1000.0*actual.cbBuffer/f.nAvgBytesPerSec:0.0));
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAllocatorRequirements(ALLOCATOR_PROPERTIES* p)override{
        if(!p)return E_POINTER;std::lock_guard lock(mutex_);
        if(allocatorMode_==CaptureAllocatorMode::Upstream){*p={};return E_NOTIMPL;}
        *p={compressed_?4:3,compressed_?LONG(512*1024):audio_?audioBlockBytes(*reinterpret_cast<const WAVEFORMATEX*>(desired_.pbFormat)):LONG(layout_.sampleBytes),1,0};return S_OK;
    }
    // Receive holds the pin mutex across the callback (DirectShow requires
    // Stop()/GetState() to observe a quiescent pin). The callback itself now
    // copies outside the source mailbox lock (CaptureCardSource C3), so the
    // remaining hold covers only validation and pointer publication.
    HRESULT STDMETHODCALLTYPE Receive(IMediaSample* sample)override{
        if(!sample)return E_POINTER;std::lock_guard lock(mutex_);if(flushing_)return S_FALSE;if(state_==State_Stopped)return VFW_E_WRONG_STATE;
        if(!audio_&&videoSamples_++<3){
            REFERENCE_TIME begin=0,end=0;const auto timeHr=sample->GetTime(&begin,&end);
            log::info("capture-video-delivery",std::format("sample={} bytes={} GetTime hr=0x{:08X} start100ns={} end100ns={} compressed={}",videoSamples_,sample->GetActualDataLength(),uint32_t(timeHr),begin,end,compressed_));
        }
        // A device mode change needs full source/graph recreation, never a
        // silent reinterpretation using the previous frame's pitch/format.
        AM_MEDIA_TYPE* changed=nullptr;const auto typeHr=sample->GetMediaType(&changed);
        if(FAILED(typeHr)){if(changed){clearType(*changed);CoTaskMemFree(changed);}log::error("capture",std::format("sample GetMediaType hr=0x{:08X}",uint32_t(typeHr)));return typeHr;}
        if(typeHr==S_OK&&changed){
            const bool same=audio_?sameAudio(*changed,connected_):compressed_?sameCompressed(*changed,connected_):allocatorMode_==CaptureAllocatorMode::Upstream?nativeCaptureContractsMatch(*changed,connected_):equivalentCaptureTypes(*changed,connected_);
            clearType(*changed);CoTaskMemFree(changed);if(!same){log::error("capture",std::format("dynamic capture contract changed; reopen required hr=0x{:08X}",uint32_t(VFW_E_INVALIDMEDIATYPE)));return VFW_E_INVALIDMEDIATYPE;}
        }
        if(compressed_){if(sample->GetActualDataLength()<=0)return VFW_E_BUFFER_UNDERFLOW;}
        else if(audio_){const auto block=reinterpret_cast<const WAVEFORMATEX*>(connected_.pbFormat)->nBlockAlign;if(sample->GetActualDataLength()<0||sample->GetActualDataLength()%block)return VFW_E_BUFFER_UNDERFLOW;}
        else if(sample->GetActualDataLength()<LONG(layout_.sampleBytes))return VFW_E_BUFFER_UNDERFLOW;
        try{return callback_(sample);}catch(...){return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE ReceiveMultiple(IMediaSample** p,long n,long* done)override{if(!p||!done||n<0)return E_INVALIDARG;*done=0;while(*done<n){const auto hr=Receive(p[*done]);if(hr!=S_OK)return hr;++*done;}return S_OK;}
    HRESULT STDMETHODCALLTYPE ReceiveCanBlock()override{return S_OK;}
    // OBS MiscFlagsHandler::GetMiscFlags, same source/license as above.
    // Use this object's canonical IUnknown; do not create a separate COM identity.
    ULONG STDMETHODCALLTYPE GetMiscFlags()override{return AM_FILTER_MISC_FLAGS_IS_RENDERER;}
};
}
HRESULT suggestCaptureAudioBuffering(IPin* pin,const WAVEFORMATEX& format){
    if(!pin)return E_POINTER;
    if(!format.nBlockAlign||format.nSamplesPerSec<8000||format.nSamplesPerSec>192000||format.nChannels<1||format.nChannels>8||
        (format.wBitsPerSample!=16&&format.wBitsPerSample!=24&&format.wBitsPerSample!=32)||
        format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8)||
        format.nAvgBytesPerSec!=uint64_t(format.nSamplesPerSec)*format.nBlockAlign)return E_INVALIDARG;
    ComPtr<IAMBufferNegotiation> negotiation;
    const auto queryHr=pin->QueryInterface(IID_PPV_ARGS(&negotiation));
    if(FAILED(queryHr)){
        log::warn("capture-audio-buffer",std::format("upstream buffer negotiation unavailable hr=0x{:08X}; retain driver defaults",uint32_t(queryHr)));
        return queryHr;
    }
    const ALLOCATOR_PROPERTIES wanted{-1,audioBlockBytes(format),-1,-1};
    const auto hr=negotiation->SuggestAllocatorProperties(&wanted);
    log::info("capture-audio-buffer",std::format("upstream request bytes={} blockMs={:.3f} rate={} channels={} bits={} hr=0x{:08X} (advisory; actual callbacks logged separately)",wanted.cbBuffer,1000.0*wanted.cbBuffer/format.nAvgBytesPerSec,format.nSamplesPerSec,format.nChannels,format.wBitsPerSample,uint32_t(hr)));
    return hr;
}
HRESULT suggestCaptureVideoBuffering(IPin* pin,long buffers,long bytes){
    if(!pin)return E_POINTER;
    if(buffers<=0||bytes<=0)return E_INVALIDARG;
    ComPtr<IAMBufferNegotiation> negotiation;
    const auto queryHr=pin->QueryInterface(IID_PPV_ARGS(&negotiation));
    if(FAILED(queryHr)){
        log::warn("capture-buffer",std::format("upstream negotiation unavailable hr=0x{:08X}; driver defaults retained",uint32_t(queryHr)));
        return queryHr;
    }
    const ALLOCATOR_PROPERTIES wanted{buffers,bytes,-1,-1};
    const auto hr=negotiation->SuggestAllocatorProperties(&wanted);
    log::info("capture-buffer",std::format("upstream request buffers={} bytes={} hr=0x{:08X} (advisory; actual allocator logged after connect)",buffers,bytes,uint32_t(hr)));
    return hr;
}
HRESULT queryCaptureAllocatorProperties(IPin* pin,ALLOCATOR_PROPERTIES& out){
    out={};
    if(!pin)return E_POINTER;
    ComPtr<IMemInputPin> input;
    auto hr=pin->QueryInterface(IID_PPV_ARGS(&input));
    if(FAILED(hr))return hr;
    ComPtr<IMemAllocator> allocator;
    hr=input->GetAllocator(&allocator);
    if(FAILED(hr))return hr;
    return allocator->GetProperties(&out);
}
HRESULT createCompressedCaptureSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> callback,ComPtr<IBaseFilter>& filter,ComPtr<IPin>& pin){
    filter.Reset();pin.Reset();if(type.majortype!=MEDIATYPE_Video)return VFW_E_INVALIDMEDIATYPE;
    try{filter.Attach(new NativeSink(type,std::move(callback),true));return filter.As(&pin);}catch(...){return E_OUTOFMEMORY;}
}
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> callback,ComPtr<IBaseFilter>& filter,ComPtr<IPin>& pin){
    return createNativeCaptureSink(type,std::move(callback),filter,pin,CaptureAllocatorMode::SinkDefault);
}
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> callback,ComPtr<IBaseFilter>& filter,ComPtr<IPin>& pin,CaptureAllocatorMode allocatorMode){
    filter.Reset();pin.Reset();CaptureMediaLayout layout;if(!captureMediaLayout(type,layout))return VFW_E_INVALIDMEDIATYPE;
    try{filter.Attach(new NativeSink(type,std::move(callback),false,allocatorMode));return filter.As(&pin);}catch(...){return E_OUTOFMEMORY;}
}
HRESULT createNativeAudioSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> callback,ComPtr<IBaseFilter>& filter,ComPtr<IPin>& pin){
    filter.Reset();pin.Reset();if(!audioType(type))return VFW_E_INVALIDMEDIATYPE;
    try{filter.Attach(new NativeSink(type,std::move(callback)));return filter.As(&pin);}catch(...){return E_OUTOFMEMORY;}
}
HRESULT createBitstreamAudioSink(const AM_MEDIA_TYPE& type,std::function<HRESULT(IMediaSample*)> callback,ComPtr<IBaseFilter>& filter,ComPtr<IPin>& pin){
    filter.Reset();pin.Reset();
    if(type.majortype!=MEDIATYPE_Audio||sink::classifyBitstreamSubtype(type.subtype.Data1)==sink::BitstreamKind::None||
       (type.cbFormat&&!type.pbFormat))return VFW_E_INVALIDMEDIATYPE;
    // Compressed packets have no PCM sample stride or byte-rate contract.
    try{filter.Attach(new NativeSink(type,std::move(callback),true));return filter.As(&pin);}catch(...){return E_OUTOFMEMORY;}
}
}
