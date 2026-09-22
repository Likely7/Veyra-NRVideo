#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/AjaCaptureSource.h"
#include "veyra/source/CaptureFrameRate.h"
#include "veyra/source/WasapiAudioInput.h"
#include "veyra/source/CaptureTiming.h"
#include "veyra/source/CaptureMediaType.h"
#include "veyra/source/NativeCaptureSink.h"
#include "veyra/source/CaptureBuffer.h"
#include "veyra/source/CaptureFormatRank.h"
#include "veyra/source/CaptureCodec.h"
#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/source/AverMediaAudioSwitch.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/BitstreamAudio.h"
#include "veyra/sink/BitstreamAudioSink.h"
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <ks.h>
#include <ksmedia.h>
#include <d3d12.h>
#include <wrl/client.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <vector>
#include <algorithm>
#include <deque>
#include <thread>
#include <array>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <format>
#include <chrono>
#include "veyra/source/CaptureAudioClock.h"
#include <cmath>
#include <string_view>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::source {
using Microsoft::WRL::ComPtr;
namespace {
// Legacy qedit interfaces are intentionally declared without obsolete qedit.h,
// which conflicts with modern D3D headers. ABI follows Microsoft DirectShow.
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCB: IUnknown {virtual HRESULT STDMETHODCALLTYPE SampleCB(double,IMediaSample*)=0;virtual HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)=0;};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) ISampleGrabber:IUnknown {virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*,long*)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**)=0;virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB*,long)=0;};
const CLSID SampleGrabberClass={0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
const CLSID NullRendererClass={0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
void freeType(AM_MEDIA_TYPE* t,bool pointer=true){if(!t)return;CoTaskMemFree(t->pbFormat);if(t->pUnk)t->pUnk->Release();if(pointer)CoTaskMemFree(t);}
// MPEG2VIDEOINFO carries the codec's sequence header (SPS/PPS) for compressed
// capture formats. Drivers are inconsistent: some store Annex-B with start
// codes, some store 4-byte length-prefixed NAL units, some an
// AVCDecoderConfigurationRecord / HEVCDecoderConfigurationRecord. Normalize
// everything to the Annex-B form the FFmpeg decoders consume. An empty result
// means the caller relies on the bitstream's in-band parameter sets, and a
// decoder that still cannot start falls back to the RGB32 compatibility path.
std::vector<uint8_t> captureCompressedExtradata(const AM_MEDIA_TYPE& type,CaptureCodec codec){
    std::vector<uint8_t> result;
    if(codec==CaptureCodec::None||codec==CaptureCodec::Mjpeg)return result;
    if(type.formattype!=FORMAT_MPEG2Video||!type.pbFormat||type.cbFormat<sizeof(MPEG2VIDEOINFO))return result;
    const auto& info=*reinterpret_cast<const MPEG2VIDEOINFO*>(type.pbFormat);
    const size_t bytes=size_t(info.cbSequenceHeader);
    if(bytes<4||bytes>256*1024)return result;
    if(offsetof(MPEG2VIDEOINFO,dwSequenceHeader)+bytes>size_t(type.cbFormat))return result;
    const uint8_t* data=reinterpret_cast<const uint8_t*>(info.dwSequenceHeader);
    const auto appendNal=[&result](const uint8_t* nal,size_t length){
        result.insert(result.end(),{0,0,0,1});
        result.insert(result.end(),nal,nal+length);
    };
    // Already Annex-B (start code with 3 or 4 bytes).
    if(data[0]==0&&data[1]==0&&(data[2]==1||(data[2]==0&&data[3]==1))){result.assign(data,data+bytes);return result;}
    // AVCDecoderConfigurationRecord: version 1, SPS/PPS length-prefixed lists.
    if(codec==CaptureCodec::H264&&data[0]==1&&bytes>=7){
        size_t offset=5;
        const unsigned spsCount=data[offset++]&0x1f;
        for(unsigned i=0;i<spsCount;++i){
            if(offset+2>bytes)return {};
            const size_t length=(size_t(data[offset])<<8)|data[offset+1];offset+=2;
            if(!length||offset+length>bytes)return {};
            appendNal(data+offset,length);offset+=length;
        }
        if(offset>=bytes)return {};
        const unsigned ppsCount=data[offset++];
        for(unsigned i=0;i<ppsCount;++i){
            if(offset+2>bytes)return {};
            const size_t length=(size_t(data[offset])<<8)|data[offset+1];offset+=2;
            if(!length||offset+length>bytes)return {};
            appendNal(data+offset,length);offset+=length;
        }
        return result;
    }
    // HEVCDecoderConfigurationRecord: version 1, array of NAL units.
    if(codec==CaptureCodec::Hevc&&data[0]==1&&bytes>=23){
        const unsigned arrayCount=data[22];
        size_t offset=23;
        for(unsigned i=0;i<arrayCount;++i){
            if(offset+3>bytes)return {};
            const unsigned nalType=data[offset]&0x3f;++offset;
            const unsigned count=(unsigned(data[offset])<<8)|data[offset+1];offset+=2;
            for(unsigned j=0;j<count;++j){
                if(offset+2>bytes)return {};
                const size_t length=(size_t(data[offset])<<8)|data[offset+1];offset+=2;
                if(!length||offset+length>bytes)return {};
                // Keep VPS(32)/SPS(33)/PPS(34) only; other arrays are optional.
                if(nalType==32||nalType==33||nalType==34)appendNal(data+offset,length);
                offset+=length;
            }
        }
        return result;
    }
    // Consecutive 4-byte length-prefixed NAL units.
    std::vector<uint8_t> converted;
    size_t offset=0;
    while(offset+4<=bytes){
        const size_t length=(size_t(data[offset])<<24)|(size_t(data[offset+1])<<16)|(size_t(data[offset+2])<<8)|size_t(data[offset+3]);
        offset+=4;
        if(!length||offset+length>bytes)return {};
        converted.insert(converted.end(),{0,0,0,1});
        converted.insert(converted.end(),data+offset,data+offset+length);
        offset+=length;
    }
    if(offset!=bytes||converted.empty())return {};
    return converted;
}
std::wstring propertyString(IMoniker* moniker,LPCOLESTR property){
    if(!moniker)return {};
    ComPtr<IPropertyBag> bag;VARIANT value;VariantInit(&value);std::wstring result;
    if(SUCCEEDED(moniker->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&bag)))&&
       SUCCEEDED(bag->Read(property,&value,nullptr))&&value.vt==VT_BSTR&&value.bstrVal)result=value.bstrVal;
    VariantClear(&value);return result;
}
std::wstring monikerPath(IMoniker* moniker){
    if(auto path=propertyString(moniker,L"DevicePath");!path.empty())return path;
    ComPtr<IBindCtx> context;LPOLESTR display=nullptr;
    if(SUCCEEDED(CreateBindCtx(0,&context))&&SUCCEEDED(moniker->GetDisplayName(context.Get(),nullptr,&display))&&display){
        std::wstring path(display);CoTaskMemFree(display);return path;
    }
    return {};
}
bool audioOutputPin(IPin* pin){
    if(!pin)return false;PIN_DIRECTION direction{};if(FAILED(pin->QueryDirection(&direction))||direction!=PINDIR_OUTPUT)return false;
    ComPtr<IAMStreamConfig> config;
    if(SUCCEEDED(pin->QueryInterface(IID_PPV_ARGS(&config)))){
        AM_MEDIA_TYPE* type=nullptr;const auto hr=config->GetFormat(&type);
        const bool audio=SUCCEEDED(hr)&&type&&type->majortype==MEDIATYPE_Audio;
        if(type)freeType(type);if(audio)return true;
    }
    ComPtr<IEnumMediaTypes> types;if(FAILED(pin->EnumMediaTypes(&types)))return false;
    for(;;){AM_MEDIA_TYPE* type=nullptr;const HRESULT hr=types->Next(1,&type,nullptr);if(hr!=S_OK||!type)break;const bool audio=type->majortype==MEDIATYPE_Audio;freeType(type);if(audio)return true;}
    return false;
}
HRESULT findAudioOutputPin(IBaseFilter* filter,ComPtr<IPin>& result){
    result.Reset();if(!filter)return E_POINTER;ComPtr<IEnumPins> pins;HRESULT hr=filter->EnumPins(&pins);if(FAILED(hr))return hr;
    ComPtr<IPin> fallback;
    for(;;){ComPtr<IPin> pin;hr=pins->Next(1,&pin,nullptr);if(hr!=S_OK)break;if(!audioOutputPin(pin.Get()))continue;
        if(!fallback)fallback=pin;
        // Prefer a pin explicitly classified as capture, but accept a driver
        // that omits the category and exposes only an audio output pin.
        ComPtr<IKsPropertySet> properties;if(SUCCEEDED(pin.As(&properties))){GUID category{};DWORD returned=0;
            if(SUCCEEDED(properties->Get(AMPROPSETID_Pin,AMPROPERTY_PIN_CATEGORY,nullptr,0,&category,sizeof(category),&returned))&&category==PIN_CATEGORY_CAPTURE){result=pin;return S_OK;}}
    }
    if(fallback){result=fallback;return S_OK;}return VFW_E_NOT_FOUND;
}
std::vector<ComPtr<IMoniker>> monikers(bool audio){std::vector<ComPtr<IMoniker>> out;ComPtr<ICreateDevEnum> de;ComPtr<IEnumMoniker> en;if(FAILED(CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&de)))||de->CreateClassEnumerator(audio?CLSID_AudioInputDeviceCategory:CLSID_VideoInputDeviceCategory,&en,0)!=S_OK)return out;for(;;){ComPtr<IMoniker> m;if(en->Next(1,&m,nullptr)!=S_OK)break;out.push_back(m);}return out;}
bool bind(unsigned index,bool audio,ComPtr<IBaseFilter>& filter){auto list=monikers(audio);return index<list.size()&&SUCCEEDED(list[index]->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)));}
bool bindPath(std::wstring_view wanted,bool audio,ComPtr<IBaseFilter>& filter){
    if(wanted.empty())return false;for(auto& moniker:monikers(audio))if(monikerPath(moniker.Get())==wanted)return SUCCEEDED(moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)));return false;
}
bool createConfiguration(ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b){return SUCCEEDED(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g)))&&SUCCEEDED(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&b)))&&SUCCEEDED(b->SetFiltergraph(g.Get()));}
bool configuration(unsigned device,ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b,ComPtr<IBaseFilter>& f,ComPtr<IAMStreamConfig>& c){return createConfiguration(g,b)&&bind(device,false,f)&&SUCCEEDED(g->AddFilter(f.Get(),L"Capture card"))&&SUCCEEDED(b->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,f.Get(),IID_PPV_ARGS(&c)));}
bool configuration(std::wstring_view path,ComPtr<IGraphBuilder>& g,ComPtr<ICaptureGraphBuilder2>& b,ComPtr<IBaseFilter>& f,ComPtr<IAMStreamConfig>& c){return createConfiguration(g,b)&&bindPath(path,false,f)&&SUCCEEDED(g->AddFilter(f.Get(),L"Capture card"))&&SUCCEEDED(b->FindInterface(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,f.Get(),IID_PPV_ARGS(&c)));}
HRESULT audioPinFor(ICaptureGraphBuilder2* builder,IBaseFilter* filter,ComPtr<IPin>& pin){
    if(!builder||!filter)return E_POINTER;const HRESULT categorized=builder->FindPin(filter,PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Audio,FALSE,0,&pin);
    if(SUCCEEDED(categorized))return categorized;
    pin.Reset();return findAudioOutputPin(filter,pin);
}
std::wstring encodePath(std::wstring_view value){
    static constexpr wchar_t digits[]=L"0123456789ABCDEF";std::wstring encoded;encoded.reserve(value.size()*4);
    for(const wchar_t character:value){const uint16_t unit=static_cast<uint16_t>(character);for(int shift=12;shift>=0;shift-=4)encoded.push_back(digits[(unit>>shift)&0xF]);}return encoded;
}
std::string pathTag(std::wstring_view value){const auto encoded=encodePath(value);std::string tag;tag.reserve(encoded.size());for(const wchar_t character:encoded)tag.push_back(static_cast<char>(character));return tag;}
// Audio devices are selected by ordinal in the legacy path form, so the log has
// to carry the friendly name: without it a field log cannot say which endpoint
// actually produced the samples.
std::string narrowForLog(const std::wstring& value){if(value.empty())return {};const int size=WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),nullptr,0,nullptr,nullptr);if(size<=0)return {};std::string out(size_t(size),'\0');WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),out.data(),size,nullptr,nullptr);return out;}
std::wstring filterName(IBaseFilter* filter){if(!filter)return {};FILTER_INFO info{};if(FAILED(filter->QueryFilterInfo(&info)))return {};std::wstring name=info.achName;if(info.pGraph)info.pGraph->Release();return name;}
int hexValue(wchar_t character){if(character>=L'0'&&character<=L'9')return character-L'0';if(character>=L'A'&&character<=L'F')return character-L'A'+10;if(character>=L'a'&&character<=L'f')return character-L'a'+10;return -1;}
bool decodePath(std::wstring_view encoded,std::wstring& value){
    if(encoded.size()%4!=0)return false;value.clear();value.reserve(encoded.size()/4);
    for(size_t i=0;i<encoded.size();i+=4){int unit=0;for(size_t j=0;j<4;++j){const int nibble=hexValue(encoded[i+j]);if(nibble<0)return false;unit=(unit<<4)|nibble;}value.push_back(static_cast<wchar_t>(unit));}return true;
}
bool parseInt(std::wstring_view text,int& value){
    if(text.empty())return false;const std::wstring copy(text);size_t consumed=0;try{value=std::stoi(copy,&consumed);}catch(...){return false;}return consumed==copy.size();
}
bool parseUnsigned(std::wstring_view text,unsigned& value){int parsed=0;if(!parseInt(text,parsed)||parsed<0)return false;value=static_cast<unsigned>(parsed);return true;}
struct CaptureSelection {unsigned videoIndex=0;int format=0;int audio=kCaptureAudioDisabled;unsigned colorOverride=0;double requestedFps=0;bool stable=false;std::wstring videoPath,audioPath;};
bool parseCapturePath(std::wstring_view path,CaptureSelection& selection){
    selection={};
    if(const auto query=path.find(L'?');query!=std::wstring_view::npos){
        const auto value=path.substr(query+1);
        if(!value.starts_with(L"fps=")||!parseCaptureFrameRate(value.substr(4),selection.requestedFps))return false;
        path=path.substr(0,query);
    }
    constexpr std::wstring_view prefix=L"capture2:";
    if(path.starts_with(prefix)){
        std::array<std::wstring_view,5> fields{};size_t cursor=prefix.size();
        for(size_t i=0;i<fields.size();++i){const size_t end=path.find(L':',cursor);if(i+1<fields.size()){if(end==std::wstring_view::npos)return false;fields[i]=path.substr(cursor,end-cursor);cursor=end+1;}else{if(end!=std::wstring_view::npos)return false;fields[i]=path.substr(cursor);}}
        if(fields[0].empty()||!decodePath(fields[0],selection.videoPath)||!parseInt(fields[1],selection.format)||!parseInt(fields[2],selection.audio)||!decodePath(fields[3],selection.audioPath)||!parseUnsigned(fields[4],selection.colorOverride)||selection.format<0||selection.colorOverride>2)return false;
        if(selection.audio!=kCaptureAudioDisabled&&selection.audio!=kCaptureAudioFromVideoDevice&&selection.audio!=kCaptureAudioWasapi&&selection.audio<0)return false;
        if((selection.audio>=0||selection.audio==kCaptureAudioWasapi)&&selection.audioPath.empty())return false;selection.stable=true;return true;
    }
    unsigned videoIndex=0,colorOverride=0;int format=0,audio=kCaptureAudioDisabled;const std::wstring legacy(path);
    const int fields=swscanf_s(legacy.c_str(),L"capture:%u:%d:%d:%u",&videoIndex,&format,&audio,&colorOverride);
    if(fields<3||format<0||audio<kCaptureAudioFromVideoDevice||colorOverride>2)return false;
    selection.videoIndex=videoIndex;selection.format=format;selection.audio=audio;selection.colorOverride=fields>=4?colorOverride:0;return true;
}
}
struct CaptureCardSource::Impl:ISampleGrabberCB {
    std::unique_ptr<AjaCaptureSource> aja;
    using Clock=std::chrono::steady_clock;
    std::atomic<ULONG> refs{1};std::mutex mutex;std::condition_variable wake;
    // One pending frame plus one reader-owned frame, never an IMediaSample
    // reference. Holding the producer's sole RGB32 sample starves its allocator.
    AVFrame* frame=nullptr;AVFrame* pendingFrame=nullptr;bool pending=false,callbackError=false,configured=false;
    double pendingTime=0,lastPts=0,readAgeMs=0;bool pendingDiscontinuity=false,forceDiscontinuity=false;
    int64_t nominalDuration100ns=0;pipeline::Rational pendingDuration=pipeline::Rational::unknown();
    uint64_t received=0,dropped=0,lastDrop=0,sequence=0;
    uint64_t discontinuitySamples=0;
    Clock::time_point pendingArrival{},readArrival{},firstArrival{},latestArrival{};
    std::deque<Clock::time_point> recentArrivals;
    ComPtr<IGraphBuilder> graph;ComPtr<ICaptureGraphBuilder2> builder;ComPtr<IBaseFilter> device,grabFilter,nullFilter,audioFilter;ComPtr<IAMStreamConfig> config;ComPtr<ISampleGrabber> grab;ComPtr<IMediaControl> control;ComPtr<IMediaEvent> events;
    float lastAudioGain=-1;bool audioGainSupported=false;
    ComPtr<IBaseFilter> audioSink;ComPtr<IReferenceClock> referenceClock;
    std::unique_ptr<sink::CaptureAudioSession> audioSession;
    // Dolby/DTS passthrough: when the device offers only compressed media types
    // the raw bursts are decoded here and the session is configured with the
    // decoded layout on the first frame (that is why its start is deferred).
    std::shared_ptr<sink::BitstreamDecoder> audioBitstream;
    // Bitstream-first mode: the compressed stream is forwarded unmodified to a
    // receiver over an exclusive WASAPI IEC 61937 carrier when one accepts it.
    std::shared_ptr<sink::BitstreamAudioSink> audioPassthrough;
    bool audioSessionDeferred=false;
    std::wstring audioBitstreamKind;
    // Read-only IEC 61937 probe on the PCM carrier. AVerMedia cards can deliver
    // the Dolby stream inside a media type that still says PCM, and the same
    // card downmixes to real 2.0 when passthrough is not armed - only the bytes
    // tell the two apart. Observational: it never changes what is played.
    std::shared_ptr<sink::Iec61937Probe> carrierProbe;
    // Friendly name of the DirectShow audio device being connected, resolved
    // before the graph is built. The log has to carry it: the stored path may
    // be an opaque class-manager display name, which made the 2026-09-17 field
    // log impossible to interpret.
    std::wstring pendingAudioName;
    // AVerMedia GC553G2 / GC553PRO / GC575 only forward compressed HDMI audio
    // after the installed vendor component arms non-PCM passthrough. The
    // component is loaded from the user's own OBS installation (never
    // redistributed); see AverMediaAudioSwitch.h for the measured behaviour.
    AverMediaAudioSwitch averMediaSwitch;
    // "Use the video device's built-in audio" is unrecoverable when the video
    // filter has no audio pin at all: retrying that is pure log noise.
    bool embeddedAudioUnavailable=false;
    // 0 automatic, 1 PCM only, 2 bitstream preferred (see
    // engine::CaptureAudioIngress). Read when the audio graph is built.
    unsigned audioIngressMode=0;
    // Video-pin allocator policy (0 auto, 1 minimum, 2 driver default); read
    // when the capture graph is built. See CaptureBuffer.h.
    unsigned bufferMode=0;
    // N1 diagnostic: legacy per-pixel CPU unpack instead of GPU unpack.
    bool cpuUnpack=false;
    // MPEG chain stage 2: MJPEG direct-connect + our own FFmpeg decode backend.
    // Decoding happens on the DirectShow callback thread for now (one decoder);
    // a bounded parallel decode queue is a later refinement.
    bool compressedPath=false;CaptureCodec codec=CaptureCodec::None;
    CaptureCompressedDecoder compressedDecoder;
    ID3D12Device* decodeDevice=nullptr;ID3D12CommandQueue* decodeQueue=nullptr;
    uint64_t compressedDecoded=0,compressedErrors=0;
    // D3D12VA frames are decoder-owned surfaces, so they travel through their
    // own mailbox slot instead of the preallocated NV12 buffer.
    AVFrame* pendingHardware=nullptr;AVFrame* hardwareRead=nullptr;bool pendingIsHardware=false;
    // Decode worker: keeps the DirectShow callback cheap (payload copy only)
    // and lets the decode overlap with graph work. The worker owns workerFrame
    // and swaps it into the mailbox once a frame is ready.
    struct CompressedSample{std::vector<uint8_t> payload;double time=0;Clock::time_point arrival;REFERENCE_TIME start=0,end=0;bool completeTime=false,bad=false,reset=false;};
    std::deque<CompressedSample> compressedQueue;size_t compressedQueueLimit=3;
    std::thread decodeThread;std::condition_variable decodeWake;bool decodeStop=false;
    AVFrame* workerFrame=nullptr;uint64_t compressedDropped=0;double lastCallbackTime=0;
    // NV12 frames the decode worker may write into. A frame enters this pool
    // only when it was never delivered, or when a read() call released it
    // (the caller's ownership ends at the next read), so a frame the caller
    // still holds is never overwritten underneath it.
    std::vector<AVFrame*> compressedFree;
    // Manual capture flip; read by the DirectShow callback thread.
    std::atomic<bool> verticalFlip{false};
    std::unique_ptr<WasapiAudioInput> wasapi;
    std::wstring audioError;
    AudioInputRecovery audioRecovery;
    SourceInfo info;CaptureMediaLayout layout;Clock::time_point lastFrame;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** pp)override{if(!pp)return E_POINTER;*pp=nullptr;if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*pp=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    // Decode worker loop: pop a compressed payload, decode it with our own
    // backend (software MJPEG / D3D12VA for H.264/HEVC/AV1/VP9), then hand the
    // result to the mailbox. No source lock is held during decoding.
    void decodeLoop(){
        std::deque<CompressedSample> metadata;
        bool recoveryBoundary=false;
        bool draining=false;
        for(;;){
            CompressedSample sample;AVFrame* target=nullptr;
            {
                std::unique_lock lock(mutex);
                // The worker needs both a payload and a frame it may write
                // into; the pool is refilled by read() releasing the caller's
                // previous frame.
                decodeWake.wait(lock,[&]{return decodeStop||((draining||!compressedQueue.empty())&&workerFrame!=nullptr);});
                if(decodeStop)return; // close() clears the queue; do not decode without a write target
                if(!draining){sample=std::move(compressedQueue.front());compressedQueue.pop_front();}
                target=workerFrame;workerFrame=nullptr;
            }
            AVFrame* decodedFrame=nullptr;bool hardware=false;
            if(sample.reset){compressedDecoder.recoverAtKeyframe();metadata.clear();recoveryBoundary=true;}
            CompressedSample stamp;
            stamp.time=sample.time;stamp.arrival=sample.arrival;stamp.start=sample.start;stamp.end=sample.end;
            stamp.completeTime=sample.completeTime;stamp.bad=sample.bad;
            if(!draining){metadata.push_back(stamp);if(metadata.size()>64)metadata.pop_front();}
            const bool produced=compressedDecoder.decode(draining?nullptr:sample.payload.data(),draining?0:sample.payload.size(),
                int64_t(sample.time*1e7),target,&decodedFrame,hardware);
            if(!produced){
                draining=false;
                // A decoder that needs more input before it can emit a frame is
                // normal for the first payload; a real error is not.
                if(!compressedDecoder.waitingForInput())++compressedErrors;
                std::lock_guard lock(mutex);
                workerFrame=target; // no output consumed the frame
                continue;
            }
            draining=true;
            const int64_t decodedPts=decodedFrame->pts!=AV_NOPTS_VALUE?decodedFrame->pts:decodedFrame->best_effort_timestamp;
            if(decodedPts==AV_NOPTS_VALUE){
                log::warn("capture-decode","discarding decoded frame without a presentation timestamp");
                recoveryBoundary=true;
                std::lock_guard lock(mutex);workerFrame=target;++compressedErrors;++dropped;continue;
            }
            const auto match=std::find_if(metadata.begin(),metadata.end(),[&](const auto& item){return int64_t(item.time*1e7)==decodedPts;});
            if(match!=metadata.end()){stamp=*match;metadata.erase(match);}
            else{
                log::warn("capture-decode","discarding decoded frame without matching input metadata");
                recoveryBoundary=true;
                std::lock_guard lock(mutex);workerFrame=target;++compressedErrors;++dropped;continue;
            }
            {
                std::lock_guard lock(mutex);
                if(hardware){
                    AVFrame* cloned=av_frame_clone(decodedFrame);
                    if(!cloned){++compressedErrors;workerFrame=target;continue;}
                    if(pending)++dropped;          // mailbox semantics: newest wins
                    if(pendingHardware)av_frame_free(&pendingHardware);
                    pendingHardware=cloned;
                    workerFrame=target;
                }else{
                    // The caller only releases its frame on the next read, so
                    // a replaced pending frame goes back to the pool instead of
                    // being reused directly.
                    if(pending){compressedFree.push_back(pendingFrame);++dropped;}
                    pendingFrame=target;
                    if(!compressedFree.empty()){workerFrame=compressedFree.back();compressedFree.pop_back();}
                }
                pendingIsHardware=hardware;
                pendingDiscontinuity=(pending&&pendingDiscontinuity)||stamp.bad||recoveryBoundary;
                recoveryBoundary=false;
                pending=true;pendingTime=stamp.time;pendingArrival=stamp.arrival;
                pendingDuration=captureDuration(stamp.start,stamp.end,stamp.completeTime,nominalDuration100ns);
                ++compressedDecoded;
                if((compressedDecoded%600)==0)log::info("capture-decode",std::format("decoded={} errors={} queueDrops={} backend={} (decode worker)",compressedDecoded,compressedErrors,compressedDropped,compressedDecoder.backendName()));
            }
            wake.notify_one();decodeWake.notify_one();
        }
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double time,IMediaSample* sample)override{
        const auto arrival=Clock::now();BYTE* data=nullptr;
        REFERENCE_TIME sampleStart=0,sampleEnd=0;
        const bool sampleTime=sample&&sample->GetTime(&sampleStart,&sampleEnd)==S_OK;
        const bool valid=sample&&std::isfinite(time)&&SUCCEEDED(sample->GetPointer(&data))&&data&&
            (compressedPath?sample->GetActualDataLength()>0:sample->GetActualDataLength()>=LONG(layout.sampleBytes));
        bool enqueued=false;uint64_t timingSequence=0;int64_t copied100ns=0;double lockWaitMs=0,arrivalDeltaMs=0,ptsDeltaMs=0;
        {
            std::lock_guard lock(mutex);
            lockWaitMs=std::chrono::duration<double,std::milli>(Clock::now()-arrival).count();
            // The compressed path decodes in its own worker and keeps its own
            // frame pool, so the preallocated NV12 mailbox is legitimately
            // empty between reads; only the native path requires it here.
            if(!valid||(!compressedPath&&!pendingFrame)){callbackError=true;}
            else {
                const double previous=compressedPath?lastCallbackTime:pendingTime;
                if(received){arrivalDeltaMs=std::chrono::duration<double,std::milli>(arrival-latestArrival).count();ptsDeltaMs=(time-previous)*1000;}
                const bool driverBreak=sample->IsDiscontinuity()==S_OK;
                // Interframe packet PTS may move backwards in decode order.
                // Apply cadence checks to decoded output, not B-frame packets.
                const bool clockBreak=received&&(!compressedPath||codec==CaptureCodec::Mjpeg)&&
                    (time<=previous||time-previous>(info.averageFps>0?2.5/info.averageFps:.1));
                if(driverBreak||clockBreak){
                    ++discontinuitySamples;
                    if(discontinuitySamples<=4||discontinuitySamples%120==0)
                        log::warn("capture-discontinuity",std::format("source={} count={} driverFlag={} clockBreak={} previousPtsMs={:.4f} ptsMs={:.4f} deltaMs={:.4f} arrivalDeltaMs={:.4f} sampleStart={} sampleEnd={} completeTime={} nominalFps={:.4f}",received+1,discontinuitySamples,driverBreak,clockBreak,previous*1000,time*1000,(time-previous)*1000,received?std::chrono::duration<double,std::milli>(arrival-latestArrival).count():0.0,sampleStart,sampleEnd,sampleTime,info.averageFps));
                }
                // Copy directly into our bounded mailbox; read() swaps frames
                // under this lock, so the frame consumed by the GPU is untouched.
                // Compressed payloads are decoded by our own backend instead of
                // being expanded by the system graph's decoder/color converter.
                if(compressedPath){
                    // MPEG chain: the callback only copies the compressed
                    // payload; the decode worker produces the NV12 frame.
                    const auto* payload=reinterpret_cast<const uint8_t*>(data);
                    const size_t bytes=size_t(sample->GetActualDataLength());
                    CompressedSample entry;entry.payload.assign(payload,payload+bytes);
                    if(compressedQueue.size()>=compressedQueueLimit){
                        if(codec==CaptureCodec::Mjpeg){compressedQueue.pop_front();++compressedDropped;++dropped;entry.bad=true;}
                        else{
                            compressedDropped+=compressedQueue.size();dropped+=compressedQueue.size();
                            compressedQueue.clear();entry.reset=true;entry.bad=true;
                            log::warn("capture-decode","compressed queue overflow: discard damaged history and recover at keyframe");
                        }
                    }
                    entry.time=time;entry.arrival=arrival;entry.start=sampleStart;entry.end=sampleEnd;entry.completeTime=sampleTime;
                    entry.bad=entry.bad||driverBreak||clockBreak;
                    entry.reset=entry.reset||((driverBreak||clockBreak)&&codec!=CaptureCodec::Mjpeg);
                    lastCallbackTime=time;
                    compressedQueue.push_back(std::move(entry));enqueued=true;
                }else{
                    if(!copyCaptureSample(layout,data,size_t(sample->GetActualDataLength()),*pendingFrame,verticalFlip.load())){callbackError=true;wake.notify_one();return S_OK;}
                    if(pending)++dropped;
                    // Inspect consecutive callbacks, not consecutive mailbox reads.
                    // Preserve a driver/clock break when its sample is overwritten.
                    pendingDiscontinuity=captureDiscontinuity(pending,pendingDiscontinuity,
                        driverBreak,received>0,pendingTime,time,info.averageFps);
                    pending=true;pendingTime=time;pendingArrival=arrival;
                    pendingDuration=captureDuration(sampleStart,sampleEnd,sampleTime,nominalDuration100ns);
                }
                if(!received)firstArrival=arrival;
                ++received;latestArrival=arrival;
                recentArrivals.push_back(arrival);
                while(recentArrivals.size()>1&&(recentArrivals.size()>1024||arrival-recentArrivals.front()>std::chrono::seconds(2)))recentArrivals.pop_front();
                timingSequence=received;copied100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            }
        }
        if(enqueued)decodeWake.notify_one();
        if(timingSequence&&(timingSequence%120==0||lockWaitMs>5))log::info("capture-callback",std::format("source={} entryToLockMs={:.3f} entryToCopiedMs={:.3f} bytes={} compressed={} arrivalDeltaMs={:.3f} ptsDeltaMs={:.3f} sampleDurationMs={:.3f} (callback cost, not display latency)",timingSequence,lockWaitMs,(copied100ns-std::chrono::duration_cast<std::chrono::nanoseconds>(arrival.time_since_epoch()).count()/100)/10000.0,sample->GetActualDataLength(),compressedPath,arrivalDeltaMs,ptsDeltaMs,sampleTime?double(sampleEnd-sampleStart)/10000:-1));
        if(log::verboseFrameLogs()&&timingSequence)log::info("capture-ingress-sample",std::format("source={} arrival={} copied={} compressed={}",timingSequence,std::chrono::duration_cast<std::chrono::nanoseconds>(arrival.time_since_epoch()).count()/100,copied100ns,compressedPath));
        wake.notify_one();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)override{return E_NOTIMPL;}
};
CaptureCardSource::CaptureCardSource():p_(std::make_unique<Impl>()){}
CaptureCardSource::~CaptureCardSource(){close();}
std::vector<CaptureDevice> CaptureCardSource::deviceDetails(bool audio){
    std::vector<CaptureDevice> result;
    for(auto& moniker:monikers(audio)){
        CaptureDevice device;device.name=propertyString(moniker.Get(),L"FriendlyName");
        if(device.name.empty())device.name=L"Unknown capture device";
        device.path=monikerPath(moniker.Get());
        if(!audio){ComPtr<IBaseFilter> filter;ComPtr<IPin> audioPin;
            if(SUCCEEDED(moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&filter)))&&SUCCEEDED(findAudioOutputPin(filter.Get(),audioPin)))device.hasEmbeddedAudio=true;
        }
        result.push_back(std::move(device));
    }
    if(!audio){auto native=AjaCaptureSource::devices();result.insert(result.end(),native.begin(),native.end());}
    if(audio)for(auto& endpoint:WasapiAudioInput::devices())result.push_back({std::move(endpoint.name),std::move(endpoint.id),false,true});
    return result;
}
std::vector<std::wstring> CaptureCardSource::devices(bool audio){std::vector<std::wstring> result;for(auto& device:deviceDetails(audio))result.push_back(std::move(device.name));return result;}
std::wstring CaptureCardSource::makeCapturePath(unsigned videoIndex,const CaptureDevice& video,int format,int audioMode,const CaptureDevice* audio,unsigned colorOverride,double requestedFps){
    if(!validCaptureFrameRate(requestedFps))return {};
    const auto suffix=requestedFps>0?std::format(L"?fps={:.6f}",requestedFps):L"";
    if(audio&&audio->wasapi){
        if(video.path.empty()||audio->path.empty())return {};
        return std::format(L"capture2:{}:{}:{}:{}:{}{}",encodePath(video.path),format,kCaptureAudioWasapi,encodePath(audio->path),colorOverride,suffix);
    }
    if(video.path.empty()||(audioMode>=0&&(!audio||audio->path.empty())))return std::format(L"capture:{}:{}:{}:{}{}",videoIndex,format,audioMode,colorOverride,suffix);
    return std::format(L"capture2:{}:{}:{}:{}:{}{}",encodePath(video.path),format,audioMode,audioMode>=0?encodePath(audio->path):L"",colorOverride,suffix);
}
std::vector<CaptureFormat> enumerateFormats(IAMStreamConfig* config){
    std::vector<CaptureFormat> out;if(!config)return out;int count=0,size=0;
    if(FAILED(config->GetNumberOfCapabilities(&count,&size))||size<1||size>65536)return out;
    std::vector<BYTE> caps(size);
    for(int i=0;i<count;++i){AM_MEDIA_TYPE* type=nullptr;if(FAILED(config->GetStreamCaps(i,&type,caps.data())))continue;BITMAPINFOHEADER* bitmap=nullptr;REFERENCE_TIME duration=0;
        if(type->formattype==FORMAT_VideoInfo&&type->cbFormat>=sizeof(VIDEOINFOHEADER)){auto* info=reinterpret_cast<VIDEOINFOHEADER*>(type->pbFormat);bitmap=&info->bmiHeader;duration=info->AvgTimePerFrame;}
        else if(type->formattype==FORMAT_VideoInfo2&&type->cbFormat>=sizeof(VIDEOINFOHEADER2)){auto* info=reinterpret_cast<VIDEOINFOHEADER2*>(type->pbFormat);bitmap=&info->bmiHeader;duration=info->AvgTimePerFrame;}
        // Device capabilities, not a 1080p/2160p 30/60 preset list. Keep native
        // indices so the selected row opens the exact driver media type.
        if(bitmap&&bitmap->biWidth>0&&bitmap->biWidth<=3840&&std::abs(int64_t(bitmap->biHeight))>0&&std::abs(int64_t(bitmap->biHeight))<=2160&&duration>0){unsigned width=bitmap->biWidth,height=unsigned(std::abs(int64_t(bitmap->biHeight)));double fps=1e7/duration;
            const auto pixel=capturePixelName(type->subtype);CaptureMediaLayout layout;const bool valid=captureMediaLayout(*type,layout);const bool knownRaw=capturePacking(type->subtype)!=CapturePacking::Unknown;
            const wchar_t* support=valid?((layout.format==AV_PIX_FMT_P010||layout.format==AV_PIX_FMT_P016)?L"原生 · SDR":L"原生"):knownRaw?L"布局/颜色暂不支持":L"需系统解码/转换";
            // N3: latency tier + recommended rank; compressed/unknown sorts last.
            const auto tier=valid?captureFormatTier(layout.packing):CaptureFormatTier::Decoded;
            const int rank=valid?captureFormatRank(layout.packing):captureFormatRank(CapturePacking::Unknown);
            wchar_t subtype[40]{},formatType[40]{};StringFromGUID2(type->subtype,subtype,40);StringFromGUID2(type->formattype,formatType,40);
            const auto key=std::format(L"{}:{}:{}:{}:{}:{}:{}",width,bitmap->biHeight,duration,subtype,formatType,bitmap->biBitCount,bitmap->biCompression);
            out.push_back({i,width,height,fps,std::format(L"{} x {} @ {:.2f} fps · {} · {} · {} [format {}]",width,height,fps,pixel,support,captureFormatTierLabel(tier),i),key,rank,int(tier)});
        }
        freeType(type);
    }
    // Recommended order first; equal ranks keep the driver's enumeration order.
    std::stable_sort(out.begin(),out.end(),[](const CaptureFormat& a,const CaptureFormat& b){return a.rank<b.rank;});
    return out;
}
std::vector<CaptureFormat> CaptureCardSource::formats(unsigned device){auto list=deviceDetails(false);if(device<list.size()&&AjaCaptureSource::isDevice(list[device].path))return AjaCaptureSource::formats(list[device].path);ComPtr<IGraphBuilder> g;ComPtr<ICaptureGraphBuilder2>b;ComPtr<IBaseFilter>f;ComPtr<IAMStreamConfig>c;if(!configuration(device,g,b,f,c))return {};return enumerateFormats(c.Get());}
std::vector<CaptureFormat> CaptureCardSource::formatsByPath(std::wstring_view devicePath){if(AjaCaptureSource::isDevice(devicePath))return AjaCaptureSource::formats(devicePath);ComPtr<IGraphBuilder> g;ComPtr<ICaptureGraphBuilder2>b;ComPtr<IBaseFilter>f;ComPtr<IAMStreamConfig>c;if(!configuration(devicePath,g,b,f,c))return {};return enumerateFormats(c.Get());}
const SourceInfo& CaptureCardSource::info()const{return p_->aja?p_->aja->info():p_->info;}
void CaptureCardSource::setAudioIngress(unsigned mode){
    auto& p=*p_;
    const unsigned clamped=mode>2?0:mode;
    if(p.audioIngressMode==clamped)return;
    p.audioIngressMode=clamped;
    log::info("capture-audio-ingress",std::format("mode={} ({}) takes effect on the next connect",clamped,
        clamped==1?"PCM only":clamped==2?"bitstream preferred":"automatic"));
}
void CaptureCardSource::setBufferMode(unsigned mode){
    auto& p=*p_;
    const unsigned clamped=mode>2?0:mode;
    if(p.bufferMode==clamped)return;
    p.bufferMode=clamped;
    const auto policy=static_cast<CaptureBufferMode>(clamped);
    log::info("capture-buffer",std::format("mode={} ({}) takes effect on the next connect",clamped,captureBufferModeKey(policy)));
}
void CaptureCardSource::setVerticalFlip(bool enabled){
    if(p_->aja)p_->aja->setVerticalFlip(enabled);
    auto& p=*p_;
    if(p.verticalFlip.exchange(enabled)==enabled)return;
    log::info("capture-flip",std::format("manual vertical flip={} (applies to the next sample; ingest only)",enabled?1:0));
}
void CaptureCardSource::setCpuUnpack(bool enabled){
    auto& p=*p_;
    if(p.cpuUnpack==enabled)return;
    p.cpuUnpack=enabled;
    log::warn("capture-unpack",std::format("legacy CPU per-pixel unpack={} (diagnostic; takes effect on the next connect)",enabled?1:0));
}
bool CaptureCardSource::setAudioGain(float gain){
    if(p_->aja)return p_->aja->setAudioGain(gain);
    if(p_->wasapi){p_->wasapi->setGain(gain);return p_->wasapi->snapshot().available;}
    auto& p=*p_;if(p.audioSession){p.audioSession->setGain(gain);return p.audioSession->snapshot().available;}if(!p.graph||!p.audioFilter)return false;
    if(gain==p.lastAudioGain)return p.audioGainSupported;
    ComPtr<IBasicAudio> audio;HRESULT hr=p.graph.As(&audio);
    if(SUCCEEDED(hr)){long attenuation=gain<=0?-10000:long(std::clamp(2000.0*std::log10(double(gain)),-10000.0,0.0));hr=audio->put_Volume(attenuation);}
    p.lastAudioGain=gain;p.audioGainSupported=SUCCEEDED(hr);log::info("capture-audio",std::format("application gain={} hr=0x{:X}",gain,unsigned(hr)));return p.audioGainSupported;
}
void CaptureCardSource::videoPresented(double pts,int64_t time,int64_t arrival){
    if(p_->aja){p_->aja->videoPresented(pts,time,arrival);return;}
    if(p_->wasapi)p_->wasapi->videoPresented(double(arrival)/10000,time,arrival);
    else if(p_->audioSession)p_->audioSession->videoPresented(pts,time,arrival);
}
void CaptureCardSource::videoReset(bool resetAudio){if(p_->aja){p_->aja->videoReset(resetAudio);return;}if(p_->wasapi)p_->wasapi->videoReset(resetAudio);else if(p_->audioSession)p_->audioSession->videoReset(resetAudio);}
void CaptureCardSource::setAudioSync(unsigned mode,int offset){if(p_->aja){p_->aja->setAudioSync(mode,offset);return;}if(p_->wasapi)p_->wasapi->setSync(mode,offset);else if(p_->audioSession)p_->audioSession->setSync(mode,offset);}
sink::CaptureAudioState CaptureCardSource::audioState()const{if(p_->aja)return p_->aja->audioState();auto state=p_->wasapi?p_->wasapi->snapshot():p_->audioSession?p_->audioSession->snapshot():sink::CaptureAudioState{};if(!p_->audioError.empty())state.error=p_->audioError;return state;}
bool CaptureCardSource::open(const SourceOpenDesc& desc){return configure(desc)&&start();}
bool CaptureCardSource::configure(const SourceOpenDesc& desc){close();error_.clear();p_->lastAudioGain=-1;auto& p=*p_;CaptureSelection selection;if(!parseCapturePath(desc.path,selection))return false;const unsigned index=selection.videoIndex;const int format=selection.format;const int audio=selection.audio;
    if(selection.stable&&AjaCaptureSource::isDevice(selection.videoPath)){
        if(selection.format!=0||selection.colorOverride!=0){error_=L"AJA 使用自动检测格式与 SDR 颜色，请恢复默认选项。";return false;}
        p.aja=std::make_unique<AjaCaptureSource>();
        if(!p.aja->configure(selection.videoPath,audio,selection.requestedFps)){error_=p.aja->error();p.aja.reset();return false;}
        p.aja->setVerticalFlip(p.verticalFlip.load());reconnectDesc_=desc;return true;
    }
    if(selection.stable?!configuration(selection.videoPath,p.graph,p.builder,p.device,p.config):!configuration(index,p.graph,p.builder,p.device,p.config))return false;
    int count=0,size=0;if(FAILED(p.config->GetNumberOfCapabilities(&count,&size))||format<0||format>=count||size<=0||size>65536)return false;
    std::vector<BYTE> caps(size);AM_MEDIA_TYPE* native=nullptr;if(FAILED(p.config->GetStreamCaps(format,&native,caps.data())))return false;
    if(selection.requestedFps>0){
        REFERENCE_TIME* interval=nullptr;
        if(native->formattype==FORMAT_VideoInfo&&native->cbFormat>=sizeof(VIDEOINFOHEADER))interval=&reinterpret_cast<VIDEOINFOHEADER*>(native->pbFormat)->AvgTimePerFrame;
        else if(native->formattype==FORMAT_VideoInfo2&&native->cbFormat>=sizeof(VIDEOINFOHEADER2))interval=&reinterpret_cast<VIDEOINFOHEADER2*>(native->pbFormat)->AvgTimePerFrame;
        if(!interval){error_=L"该采集格式不支持设备帧率协商，请将采集帧率设为 0。";freeType(native);return false;}
        *interval=captureFrameInterval(selection.requestedFps);
        log::info("capture-rate",std::format("requestFps={:.6f} interval100ns={} deviceNegotiation=1 softwareLimiter=0",selection.requestedFps,*interval));
    }
    HRESULT hr=p.config->SetFormat(native);const GUID requestedSubtype=native->subtype;
    log::info("capture",std::format("SetFormat device={} nativeIndex={} subtype=0x{:08X} hr=0x{:08X}",index,format,native->subtype.Data1,uint32_t(hr)));freeType(native);if(FAILED(hr)){if(selection.requestedFps>0)error_=std::format(L"采集卡拒绝 {:.3f} FPS（0x{:08X}）；请改用设备支持的帧率，或填 0 恢复默认。",selection.requestedFps,uint32_t(hr));return false;}
    // Read the driver-negotiated type back. Native YUY2/NV12/RGB32 connects
    // directly to our terminal filter: no intelligent-connect converter.
    native=nullptr;hr=p.config->GetFormat(&native);if(FAILED(hr)||!native){freeType(native);return false;}
    CaptureMediaLayout nativeLayout;const bool nativeSupported=captureMediaLayout(*native,nativeLayout);
    if(!nativeSupported&&capturePacking(native->subtype)!=CapturePacking::Unknown){log::error("capture","Known raw format has unsupported layout/color metadata; refusing implicit RGB conversion");freeType(native);return false;}
    if(nativeSupported&&(nativeLayout.format==AV_PIX_FMT_P010||nativeLayout.format==AV_PIX_FMT_P016)&&desc.legacyCaptureRgbForDiagnostic){log::error("capture","10/16-bit capture cannot use the legacy 8-bit RGB diagnostic converter");freeType(native);return false;}
    // Some drivers advertise a bottom-up DIB (biHeight>0) while their samples
    // actually arrive top-down, which users reported as "RGB24 captures upside
    // down". Ask for an explicit top-down connection first: an accepted request
    // normalizes the mismatch (connected type is then negative and no flip is
    // applied), a refusal keeps the driver-declared sign and the existing flip.
    // Truthful bottom-up devices stay correct in both cases.
    if(nativeSupported&&captureIsRgbDib(nativeLayout.packing)){
        if(auto* header=captureBitmapHeader(*native);header&&header->biHeight>0){
            const LONG advertised=header->biHeight;header->biHeight=-advertised;
            const HRESULT topDownHr=p.config->SetFormat(native);
            if(SUCCEEDED(topDownHr)){
                AM_MEDIA_TYPE* refreshed=nullptr;CaptureMediaLayout refreshedLayout;
                if(SUCCEEDED(p.config->GetFormat(&refreshed))&&refreshed&&captureMediaLayout(*refreshed,refreshedLayout)){
                    freeType(native);native=refreshed;nativeLayout=refreshedLayout;
                }else freeType(refreshed);
                log::info("capture",std::format("DIB top-down request hr=0x{:08X} subtype=0x{:08X} accepted=1 bottomUp={}",uint32_t(topDownHr),native->subtype.Data1,nativeLayout.bottomUp));
            }else{
                header->biHeight=advertised;
                log::info("capture",std::format("DIB top-down request hr=0x{:08X} subtype=0x{:08X} accepted=0 bottomUp={} (keeping driver-declared orientation)",uint32_t(topDownHr),native->subtype.Data1,nativeLayout.bottomUp));
            }
        }
    }
    const bool direct=nativeSupported&&!desc.legacyCaptureRgbForDiagnostic;
    const CaptureCodec codec=captureCodecOf(native->subtype);
    unsigned compressedWidth=0,compressedHeight=0;int64_t compressedDuration=0;
    // MPEG chain: every compressed subtype (MJPEG/H.264/HEVC/AV1/VP9) now
    // bypasses the system decoder stack; our own backend decodes it. Any
    // failure below falls back to the RGB32 compatibility path.
    const bool compressedPath=!direct&&!nativeSupported&&codec!=CaptureCodec::None&&!desc.legacyCaptureRgbForDiagnostic;
    if(compressedPath){p.compressedPath=true;p.codec=codec;}
    AM_MEDIA_TYPE connected{};
    if(compressedPath){
        ComPtr<IPin> input,output;
        hr=createCompressedCaptureSink(*native,[&p](IMediaSample* sample){REFERENCE_TIME a=0,b=0;const auto timeHr=sample->GetTime(&a,&b);if(FAILED(timeHr))return timeHr;return p.SampleCB(double(a)/1e7,sample);},p.grabFilter,input);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Compressed frame mailbox");
        if(SUCCEEDED(hr))hr=p.builder->FindPin(p.device.Get(),PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,FALSE,0,&output);
        if(SUCCEEDED(hr))suggestCaptureVideoBuffering(output.Get(),4,512*1024);
        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(output.Get(),input.Get(),native);
        if(SUCCEEDED(hr))hr=input->ConnectionMediaType(&connected);
        if(SUCCEEDED(hr)){
            if(connected.formattype==FORMAT_VideoInfo2&&connected.cbFormat>=sizeof(VIDEOINFOHEADER2)){const auto& v=*reinterpret_cast<const VIDEOINFOHEADER2*>(connected.pbFormat);compressedWidth=unsigned(v.bmiHeader.biWidth);compressedHeight=unsigned(std::abs(int64_t(v.bmiHeader.biHeight)));compressedDuration=int64_t(v.AvgTimePerFrame);}
            else if(connected.formattype==FORMAT_VideoInfo&&connected.cbFormat>=sizeof(VIDEOINFOHEADER)){const auto& v=*reinterpret_cast<const VIDEOINFOHEADER*>(connected.pbFormat);compressedWidth=unsigned(v.bmiHeader.biWidth);compressedHeight=unsigned(std::abs(int64_t(v.bmiHeader.biHeight)));compressedDuration=int64_t(v.AvgTimePerFrame);}
            if(!compressedWidth||!compressedHeight||compressedWidth%2||compressedHeight%2){log::warn("capture-decode","compressed dimensions unusable; falling back to the RGB32 compatibility path");hr=VFW_E_INVALIDMEDIATYPE;}
        }
        if(SUCCEEDED(hr)){
            const auto extradata=captureCompressedExtradata(*native,codec);
            if(!p.compressedDecoder.open(codec,compressedWidth,compressedHeight,extradata.data(),extradata.size(),
                static_cast<ID3D12Device*>(desc.d3d12Device),static_cast<ID3D12CommandQueue*>(desc.d3d12Queue))){
                log::warn("capture-decode",std::format("no usable decoder for codec={}; falling back to the RGB32 compatibility path",captureCodecKey(codec)));
                hr=VFW_E_INVALIDMEDIATYPE;
            }else{
                // Keep the shared device/queue alive for the decode worker.
                if(desc.d3d12Device){p.decodeDevice=static_cast<ID3D12Device*>(desc.d3d12Device);p.decodeDevice->AddRef();}
                if(desc.d3d12Queue){p.decodeQueue=static_cast<ID3D12CommandQueue*>(desc.d3d12Queue);p.decodeQueue->AddRef();}
            }
        }
        log::info("capture-decode",std::format("compressed ConnectDirect codec={} subtype=0x{:08X} size={}x{} hr=0x{:08X}",captureCodecKey(codec),native->subtype.Data1,compressedWidth,compressedHeight,uint32_t(hr)));
        if(FAILED(hr)){log::warn("capture-decode","compressed direct connect failed; retrying the RGB32 compatibility path");SourceOpenDesc retry=desc;retry.legacyCaptureRgbForDiagnostic=true;return configure(retry);}
    }else if(direct){
        // N4: suggest the video-pin allocator size before ConnectDirect. The
        // suggestion is advisory; a refusal keeps the driver's defaults and is
        // reported after the connect instead of failing the device.
        const auto bufferPolicy=static_cast<CaptureBufferMode>(p.bufferMode);
        const long suggestedBuffers=captureDesiredVideoBuffers(bufferPolicy,long(nativeLayout.width),long(nativeLayout.height));
        ComPtr<IPin> input,output;
        hr=createNativeCaptureSink(*native,[&p](IMediaSample* sample){REFERENCE_TIME a=0,b=0;const auto timeHr=sample->GetTime(&a,&b);if(FAILED(timeHr))return timeHr;return p.SampleCB(double(a)/1e7,sample);},p.grabFilter,input);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Native frame mailbox");
        if(SUCCEEDED(hr))hr=p.builder->FindPin(p.device.Get(),PINDIR_OUTPUT,&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,FALSE,0,&output);
        if(SUCCEEDED(hr)&&suggestedBuffers>0)suggestCaptureVideoBuffering(output.Get(),suggestedBuffers,LONG(nativeLayout.sampleBytes));
        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(output.Get(),input.Get(),native);
        if(SUCCEEDED(hr))hr=input->ConnectionMediaType(&connected);
        if(SUCCEEDED(hr)){
            ALLOCATOR_PROPERTIES actual{};
            const auto allocatorHr=queryCaptureAllocatorProperties(input.Get(),actual);
            const bool negotiated=suggestedBuffers>0;
            const bool honored=negotiated&&SUCCEEDED(allocatorHr)&&actual.cBuffers==suggestedBuffers;
            log::info("capture-buffer",std::format("mode={} requested={} actual buffers={} bytes={} align={} hr=0x{:08X} {}",captureBufferModeKey(bufferPolicy),suggestedBuffers,actual.cBuffers,actual.cbBuffer,actual.cbAlign,uint32_t(allocatorHr),!negotiated?"(driver default)":honored?"(driver honored)":"(driver ignored; negotiation not applied)"));
        }
        log::info("capture",std::format("native ConnectDirect subtype=0x{:08X} hr=0x{:08X} converters=0",native->subtype.Data1,uint32_t(hr)));
    }else{
        log::warn("capture",std::format("explicit RGB32 compatibility path subtype=0x{:08X} diagnostic={} (decoder/color converter may be inserted)",requestedSubtype.Data1,desc.legacyCaptureRgbForDiagnostic));
        hr=CoCreateInstance(SampleGrabberClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.grabFilter));
        if(SUCCEEDED(hr))hr=p.grabFilter.As(&p.grab);
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.grabFilter.Get(),L"Decoded RGB compatibility mailbox");
        AM_MEDIA_TYPE want{};want.majortype=MEDIATYPE_Video;want.subtype=MEDIASUBTYPE_RGB32;want.formattype=FORMAT_VideoInfo;
        if(SUCCEEDED(hr))hr=p.grab->SetMediaType(&want);
        if(SUCCEEDED(hr))hr=CoCreateInstance(NullRendererClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.nullFilter));
        if(SUCCEEDED(hr))hr=p.graph->AddFilter(p.nullFilter.Get(),L"Video sink");
        if(SUCCEEDED(hr))hr=p.builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,p.device.Get(),p.grabFilter.Get(),p.nullFilter.Get());
        if(SUCCEEDED(hr))hr=p.grab->GetConnectedMediaType(&connected);
    }
    bool layoutValid=false;
    if(compressedPath){
        freeType(native);freeType(&connected,false);
        p.layout={};
        p.layout.width=compressedWidth;p.layout.height=compressedHeight;
        p.layout.planes=2;p.layout.format=AV_PIX_FMT_NV12;
        p.layout.rowBytes=p.layout.width;p.layout.stride=p.layout.rowBytes;p.layout.chromaStride=p.layout.stride;p.layout.chromaRowBytes=p.layout.rowBytes;
        p.layout.chromaOffset=size_t(p.layout.stride)*p.layout.height;
        p.layout.sampleBytes=p.layout.chromaOffset+size_t(p.layout.chromaStride)*(p.layout.height/2);
        p.layout.duration=compressedDuration;
        AVFrame probe{};probe.format=AV_PIX_FMT_NV12;probe.width=int(p.layout.width);probe.height=int(p.layout.height);
        p.layout.color=pipeline::resolveFrameColor(probe);
        if(codec==CaptureCodec::Mjpeg){
            // JPEG planes are full-range by definition and our MJPEG software
            // decoder emits full-range NV12 (the pre-MPEG colour contract).
            p.layout.color.range=pipeline::ColorRange::Full;
            p.layout.color.transfer=pipeline::TransferFunction::SRGB;
            p.layout.color.preserveSdrCodeValues=true;
        }
        // Compressed video keeps the standard limited-range NV12 contract as
        // its declared fallback; the bitstream's own VUI metadata overrides it
        // per frame through resolveFrameColor().
        p.layout.color.rangeAssumed=p.layout.color.matrixAssumed=p.layout.color.transferAssumed=true;
        layoutValid=true;
    }else{
        freeType(native);layoutValid=SUCCEEDED(hr)&&captureMediaLayout(connected,p.layout);freeType(&connected,false);
    }
    if(!layoutValid){log::error("capture",std::format("unsupported negotiated layout/connect failure hr=0x{:08X}",uint32_t(hr)));return false;}
    if(p.cpuUnpack&&captureLegacyCpuLayout(p.layout))log::warn("capture-unpack",std::format("legacy CPU unpack path active packing={} format={} (per-pixel conversion stays on the callback thread)",int(p.layout.packing),int(p.layout.format)));
    const unsigned colorOverride=selection.colorOverride;
    log::info("capture-color",std::format("driver controlFlags=0x{:08X} colorInfoPresent={} transfer={} matrix={} primaries={} chroma={} override={}",p.layout.colorControlFlags,bool(p.layout.colorControlFlags&AMCONTROL_COLORINFO_PRESENT),int(p.layout.color.transfer),int(p.layout.color.matrix),int(p.layout.color.primaries),int(p.layout.color.chromaLocation),colorOverride));
    if(colorOverride>2)return false;
    if(colorOverride&&!compressedPath){
        if(p.layout.format!=AV_PIX_FMT_P010&&p.layout.format!=AV_PIX_FMT_P016){log::error("capture-color","Explicit HDR requires P010/P016; select a 10/16-bit capture format");return false;}
        p.layout.color.transfer=colorOverride==1?pipeline::TransferFunction::PQ:pipeline::TransferFunction::HLG;
        p.layout.color.matrix=pipeline::YuvMatrix::BT2020NCL;p.layout.color.primaries=pipeline::ColorPrimaries::BT2020;
        p.layout.color.transferAssumed=p.layout.color.matrixAssumed=p.layout.color.primariesAssumed=false;
        log::info("capture-color",std::format("manual override={} BT2020; range retains negotiated metadata",colorOverride==1?"PQ":"HLG"));
    }
    p.info={};p.info.kind=pipeline::SourceKind::CaptureCard;p.info.width=p.layout.width;p.info.height=p.layout.height;p.info.averageFps=p.layout.duration>0?1e7/p.layout.duration:0;p.info.duration=pipeline::Rational::unknown();p.info.color=p.layout.color;
    if(selection.requestedFps>0){
        const bool accepted=captureFrameRateMatches(selection.requestedFps,p.layout.duration);
        log::info("capture-rate",std::format("requestedFps={:.6f} connectedFps={:.6f} accepted={} softwareLimiter=0",selection.requestedFps,p.info.averageFps,accepted));
        if(!accepted){error_=std::format(L"请求 {:.3f} FPS，但采集卡返回 {:.3f} FPS；请改用支持的帧率，或填 0。",selection.requestedFps,p.info.averageFps);return false;}
    }
    if(!compressedPath&&!colorOverride&&(p.layout.format==AV_PIX_FMT_P010||p.layout.format==AV_PIX_FMT_P016)&&p.info.color.transferAssumed)
        log::warn("capture-color","No explicit HDR transfer from driver; bit depth does not identify HDR. Keeping SDR fallback; manual PQ/HLG remains available.");
    if(p.info.color.isHdrPath())log::info("capture-color",std::format("HDR input transfer={} matrix={} assumed={} primaries={} assumed={}",int(p.info.color.transfer),int(p.info.color.matrix),p.info.color.matrixAssumed,int(p.info.color.primaries),p.info.color.primariesAssumed));
    p.nominalDuration100ns=p.layout.duration;
    log::info("capture-color",std::format("format={} stride={} rowBytes={} bytes={} bottomUp={} matrix={} assumed={} range={} assumed={} workingTransfer={} assumed={} (explicit transfer contract)",int(p.layout.format),p.layout.stride,p.layout.rowBytes,p.layout.sampleBytes,p.layout.bottomUp,int(p.info.color.matrix),p.info.color.matrixAssumed,int(p.info.color.range),p.info.color.rangeAssumed,int(p.info.color.transfer),p.info.color.transferAssumed));
    if(audio==kCaptureAudioWasapi){
        p.wasapi=std::make_unique<WasapiAudioInput>();
        if(!p.wasapi->configure(selection.audioPath)){p.wasapi.reset();p.audioError=L"WASAPI 音频端点ID无效；视频继续运行";}
        log::info("capture-audio","binding=wasapi shared=1 explicitEndpoint=1 videoClock=ingress-host-estimate");
    }else if(audio!=kCaptureAudioDisabled){
        if(audio>=0){
            // Resolve the DirectShow device identity before the graph is built.
            // The AVerMedia switch is keyed on the device path, and it has to
            // happen first so media-type enumeration sees the post-switch state.
            std::wstring audioName,audioPath;
            auto list=monikers(true);
            for(size_t i=0;i<list.size();++i){
                const std::wstring path=monikerPath(list[i].Get());
                const bool wanted=selection.stable?(path==selection.audioPath):(i==size_t(audio));
                if(!wanted)continue;
                audioName=propertyString(list[i].Get(),L"FriendlyName");audioPath=path;break;
            }
            p.pendingAudioName=audioName;
            if(!audioPath.empty())applyVendorAudioSwitch(audioName,audioPath);
        }
        const bool audioReady=connectDirectShowAudio(desc);
        if(!audioReady){
            p.audioError=L"采集音频设备或 PCM 格式不可用；视频继续运行";
            log::warn("capture-audio","audio connection unavailable; retaining video capture");
            if(p.audioFilter)p.graph->RemoveFilter(p.audioFilter.Get());
            p.audioFilter.Reset();
        }
        // Pin the common graph clock before Run; removing DirectShow's audio
        // renderer must not silently change the capture graph's reference.
        ComPtr<IMediaFilter> mediaFilter;
        if(FAILED(CoCreateInstance(CLSID_SystemClock,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&p.referenceClock)))||
            FAILED(p.graph.As(&mediaFilter))||FAILED(mediaFilter->SetSyncSource(p.referenceClock.Get())))return false;
    }
    if(FAILED(p.graph.As(&p.control))||FAILED(p.graph.As(&p.events)))return false;
    if(p.grab&&(FAILED(p.grab->SetBufferSamples(FALSE))||FAILED(p.grab->SetCallback(&p,0))))return false;
    for(auto** f:{&p.frame,&p.pendingFrame}){
        *f=av_frame_alloc();if(!*f)return false;
        (*f)->format=p.layout.format;(*f)->width=p.info.width;(*f)->height=p.info.height;
        if(av_frame_get_buffer(*f,32)<0)return false;
    }
    if(compressedPath){
        // Write targets: the frame the worker is filling plus one spare handed
        // out by read(). Frames are never reused while the caller still owns
        // them (see the pool contract in Impl).
        // Four frames total (delivered / pending / worker / one spare). A
        // larger pool was measured with two and three spares: the 4K18
        // read-age effect was not monotonic (23.6 vs 25.2 ms across runs), so
        // the extra memory buys nothing reproducible.
        for(int i=0;i<2;++i){
            AVFrame* frame=av_frame_alloc();if(!frame)return false;
            frame->format=AV_PIX_FMT_NV12;frame->width=int(p.layout.width);frame->height=int(p.layout.height);
            if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);return false;}
            if(i==0)p.workerFrame=frame;else p.compressedFree.push_back(frame);
        }
        p.decodeStop=false;p.decodeThread=std::thread([&p]{p.decodeLoop();});
        log::info("capture-decode",std::format("decode worker started queue={} backend={} (single decode thread; the parallel pool is a later refinement)",p.compressedQueueLimit,p.compressedDecoder.backendName()));
    }
    p.configured=true;
    reconnectDesc_=desc;reconnectInfo_=p.info;reconnectFormat_.clear();
    for(const auto& candidate:enumerateFormats(p.config.Get()))if(candidate.index==format){reconnectFormat_=candidate.key;break;}
    // Log the upstream type after DirectShow has finished negotiation. The
    // RGB32 output's nominal FPS alone is not proof of actual callback cadence.
    AM_MEDIA_TYPE* actual=nullptr;const auto formatHr=p.config->GetFormat(&actual);
    veyra::log::info("capture",std::format("configured {}x{} nominalFps={:.3f} upstreamSubtype=0x{:08X} formatHr=0x{:X} mailbox=1 ownedBuffers=2 deferredRun=1 audioDevice={}",
        p.info.width,p.info.height,p.info.averageFps,actual?unsigned(actual->subtype.Data1):0,unsigned(formatHr),audio));freeType(actual);
    return true;
}
bool CaptureCardSource::applyVendorAudioSwitch(const std::wstring& audioName,const std::wstring& audioPath){
    auto& p=*p_;
    std::wstring name=audioName,path=audioPath;
    bool applied=false;
    if(!AverMediaAudioSwitch::isAverMediaDevicePath(path)){
        // These cards' DirectShow audio monikers often carry no DevicePath, so
        // the stored path is the class-manager display name
        // ("@device:cm:{...}\wave:{...}") and contains no vid_/pid_ - exactly
        // the tokens the vendor component parses. Ask the device tree instead:
        // the USB functions are still there.
        const auto functions=AverMediaAudioSwitch::findUsbFunctions(L"vid_07ca");
        for(const auto& function:functions)
            log::info("capture-audio-vendor",std::format("device tree: instance=\"{}\" friendly=\"{}\" interfacePath={}",
                narrowForLog(function.instanceId),narrowForLog(function.friendlyName),
                function.interfacePath.empty()?std::string("<none>"):pathTag(function.interfacePath)));
        const auto* chosen=AverMediaAudioSwitch::pickAudioFunction(functions);
        if(chosen==nullptr){
            log::info("capture-audio-vendor",std::format("skipped: no AVerMedia USB audio function present (selected device=\"{}\" directShowPath={})",
                narrowForLog(name),pathTag(path)));
            return false;
        }
        // Use the *device-tree* name, not the DirectShow endpoint name. The
        // component turns the UVC name into the camera name by replacing
        // "-Audio" with "-Video"; the DirectShow name here is
        // "HDMI/Line In (Live Gamer Ultra 2.1-Audio)", which would map to
        // "HDMI/Line In (Live Gamer Ultra 2.1-Video)" and never match the real
        // "Live Gamer Ultra 2.1-Video" function.
        const std::wstring directShowName=name;
        if(!chosen->friendlyName.empty())name=chosen->friendlyName;
        path=chosen->interfacePath.empty()?chosen->instanceId:chosen->interfacePath;
        log::info("capture-audio-vendor",std::format("using device-tree audio function instance=\"{}\" name=\"{}\" (directShowName=\"{}\")",
            narrowForLog(chosen->instanceId),narrowForLog(name),narrowForLog(directShowName)));
        // One hedge, no extra round trip: if the audio function is refused, try
        // the card's other USB function before giving up. The component maps
        // "-Audio" to "-Video" internally, so both are plausible inputs.
        applied=p.averMediaSwitch.apply(name,path);
        if(!applied){
            for(const auto& function:functions){
                if(&function==chosen)continue;
                std::wstring fallbackName=function.friendlyName.empty()?directShowName:function.friendlyName;
                const std::wstring fallbackPath=function.interfacePath.empty()?function.instanceId:function.interfacePath;
                log::info("capture-audio-vendor",std::format("retrying switch with function instance=\"{}\" name=\"{}\"",
                    narrowForLog(function.instanceId),narrowForLog(fallbackName)));
                if(p.averMediaSwitch.apply(fallbackName,fallbackPath)){applied=true;name=fallbackName;path=fallbackPath;break;}
            }
        }
    }
    else applied=p.averMediaSwitch.apply(name,path);
    const auto& status=p.averMediaSwitch.status();
    if(applied){
        log::info("capture-audio-vendor",std::format("non-PCM switch armed device=\"{}\" component=\"{}\" chipFormat={} nonPcmNow={} monitoring={}",
            narrowForLog(name),narrowForLog(status.componentRoot),status.chipAudioFormat,status.nonPcmActive?1:0,
            p.averMediaSwitch.active()?1:0));
    }else{
        log::warn("capture-audio-vendor",std::format("non-PCM switch unavailable device=\"{}\" componentFound={} dllsLoaded={} detail=\"{}\"",
            narrowForLog(name),status.componentFound?1:0,status.dllsLoaded?1:0,status.detail));
    }
    return applied;
}
bool CaptureCardSource::connectDirectShowAudio(const SourceOpenDesc& desc){
    auto& p=*p_;CaptureSelection selection;if(!parseCapturePath(desc.path,selection))return false;
    const int audio=selection.audio;HRESULT hr=S_OK;
    ComPtr<IBaseFilter> audioFilter;const bool embedded=audio==kCaptureAudioFromVideoDevice;
    if(embedded){
        // Some capture cards expose video and HDMI audio on one
        // DirectShow filter. OBS calls this "use video device". Do not
        // AddFilter/RemoveFilter here: p.device owns the graph filter.
        audioFilter=p.device;log::info("capture-audio",std::format("binding=video-filter embedded=1 videoPathTag={}",pathTag(selection.videoPath)));
    }else{
        const bool bound=selection.stable?bindPath(selection.audioPath,true,audioFilter):bind(unsigned(audio),true,audioFilter);
        if(!bound){log::warn("capture-audio",std::format("binding=separate failed mode={} audioIndex={} audioPathTag={}",audio,audio,pathTag(selection.audioPath)));return false;}
        hr=p.graph->AddFilter(audioFilter.Get(),L"Capture audio");
        if(FAILED(hr)){log::warn("capture-audio",std::format("binding=separate AddFilter hr=0x{:08X}",uint32_t(hr)));return false;}
        const std::wstring deviceName=p.pendingAudioName.empty()?filterName(audioFilter.Get()):p.pendingAudioName;
        p.audioFilter=audioFilter;log::info("capture-audio",std::format("binding=separate embedded=0 audioIndex={} device=\"{}\" pathTag={}",audio,narrowForLog(deviceName),pathTag(selection.audioPath)));
    }
    ComPtr<IPin> audioPin;hr=audioPinFor(p.builder.Get(),audioFilter.Get(),audioPin);
    if(FAILED(hr)){
        if(embedded)p.embeddedAudioUnavailable=true;
        log::warn("capture-audio",std::format("audio output pin not found embedded={} hr=0x{:08X}{}",embedded?1:0,uint32_t(hr),
            embedded?" (this video filter exposes no audio pin; select the separate audio device)":""));
        return false;
    }
    ComPtr<IEnumMediaTypes> types;hr=audioPin->EnumMediaTypes(&types);if(FAILED(hr)){types.Reset();log::warn("capture-audio",std::format("EnumMediaTypes hr=0x{:08X}; trying current driver format",uint32_t(hr)));}
    // Preserve the device's actual speaker layout. Enumeration order is
    // commonly stereo first even when native 5.1 is available.
    auto releaseType=[](AM_MEDIA_TYPE* type){freeType(type);};
    using AudioType=std::unique_ptr<AM_MEDIA_TYPE,decltype(releaseType)>;
    std::vector<AudioType> candidates;
    ComPtr<IAMStreamConfig> audioConfig;
    if(SUCCEEDED(audioPin.As(&audioConfig))){
        AM_MEDIA_TYPE* current=nullptr;const auto currentHr=audioConfig->GetFormat(&current);
        log::info("capture-audio",std::format("current driver format hr=0x{:08X}",uint32_t(currentHr)));
        if(SUCCEEDED(currentHr)&&current)candidates.emplace_back(current,releaseType);else if(current)freeType(current);
    }
    while(types){AM_MEDIA_TYPE* type=nullptr;const auto next=types->Next(1,&type,nullptr);
        if(next!=S_OK||!type){if(type)freeType(type);break;}candidates.emplace_back(type,releaseType);}
std::vector<AudioType> audioTypes;std::vector<AudioType> bitstreamTypes;unsigned typeIndex=0;
// Dolby/DTS bitstream capability probe: the capture card may expose AC-3 /
// E-AC-3 (Dolby Digital Plus, includes Atmos over DD+) / TrueHD / DTS instead
// of PCM. Veyra currently consumes PCM only, so a compressed stream is reported
// here (and then ignored) instead of being silently mis-parsed. Dolby passthrough
// work builds on this inventory.
unsigned bitstreamTypeCount=0;std::string bitstreamSummary;
auto bitstreamName=[](const GUID& subtype)->const char*{
    switch(subtype.Data1){
    case 0x00000092u:return "AC-3(SPDIF)";
    case 0x00002000u:return "AC-3";
    case 0x0000000Au:return "E-AC-3/DD+";
    case 0x0000010Au:return "E-AC-3/DD+ Atmos";
    case 0x0000000Cu:return "TrueHD/MLP";
    case 0x00000008u:return "DTS";
    case 0x0000000Bu:return "DTS-HD";
    case 0x0000010Bu:return "DTS:X(E1)";
    case 0x0000030Bu:return "DTS:X(E2)";
    default:return nullptr;
    }
};
for(auto& owned:candidates){AM_MEDIA_TYPE* type=owned.get();if(type->majortype!=MEDIATYPE_Audio)continue;
sink::WavePcmFormat pcm;bool supported=false;
if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat)supported=sink::parseWavePcm(type->pbFormat,type->cbFormat,pcm);
if(!supported){
    const char* name=bitstreamName(type->subtype);
if(name){
    ++bitstreamTypeCount;if(!bitstreamSummary.empty())bitstreamSummary+=",";bitstreamSummary+=name;
    log::info("capture-audio-bitstream",std::string("mediaType=")+std::to_string(typeIndex)+" subtype=0x"+std::format("{:08X}",unsigned(type->subtype.Data1))+" kind="+name+" (passthrough candidate)");
}
}
        if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat&&type->cbFormat>=sizeof(WAVEFORMATEX)){
            const auto* wave=reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat);
            log::info("capture-audio",std::format("mediaType={} major=0x{:08X} subtype=0x{:08X} tag={} channels={} mask=0x{:X} rate={} containerBits={} validBits={} floating={} pcm={}",typeIndex++,type->majortype.Data1,type->subtype.Data1,wave->wFormatTag,wave->nChannels,supported?pcm.layout.mask:0,wave->nSamplesPerSec,wave->wBitsPerSample,supported?pcm.validBits:0,supported&&pcm.floating?1:0,supported?1:0));
        }else log::info("capture-audio",std::format("mediaType={} major=0x{:08X} subtype=0x{:08X} format=0x{:08X} pcm=0",typeIndex++,type->majortype.Data1,type->subtype.Data1,type->formattype.Data1));
        if(supported)audioTypes.push_back(std::move(owned));
        else if(bitstreamName(type->subtype))bitstreamTypes.push_back(std::move(owned));
    }
log::info("capture-audio-bitstream",std::format("device bitstream types={} [{}] pcmTypes={}",bitstreamTypeCount,bitstreamSummary.empty()?"none":bitstreamSummary,audioTypes.size()));
const bool wantBitstreamFirst=p.audioIngressMode==2;
const bool allowBitstream=p.audioIngressMode!=1;
if(p.audioIngressMode==1&&bitstreamTypeCount>0)log::info("capture-audio-ingress","manual PCM-only selected: bitstream types are ignored even though the device offers them");
if(audioTypes.empty()&&(!allowBitstream||bitstreamTypes.empty())){log::warn("capture-audio","audio pin has no usable media type for the selected ingress mode");return false;}
    std::stable_sort(audioTypes.begin(),audioTypes.end(),[](const auto& a,const auto& b){
        sink::WavePcmFormat lhs{},rhs{};
        if(!sink::parseWavePcm(a->pbFormat,a->cbFormat,lhs)||!sink::parseWavePcm(b->pbFormat,b->cbFormat,rhs))return false;
        return sink::preferCaptureAudioFormat(lhs,rhs);
    });
    bool connectedAudio=false;
    // PCM attempt as a lambda so the bitstream-preferred mode can try it after
    // the compressed types instead of before them.
    auto connectPcm=[&]()->bool{
    for(const auto& owned:audioTypes){
        auto* type=owned.get();
        auto session=std::make_unique<sink::CaptureAudioSession>();ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
        sink::WavePcmFormat parsed{};
        if(type->formattype==FORMAT_WaveFormatEx&&type->pbFormat&&type->cbFormat>=sizeof(WAVEFORMATEX)&&sink::parseWavePcm(type->pbFormat,type->cbFormat,parsed)&&session->configure(parsed)){
            auto* target=session.get();
            auto probe=std::make_shared<sink::Iec61937Probe>();
            hr=createNativeAudioSink(*type,[target,probe,clock=CaptureAudioClock{},rate=parsed.wave.nAvgBytesPerSec,warned=false](IMediaSample* sample) mutable {
                BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
                const auto pointerHr=sample->GetPointer(&bytes);if(FAILED(pointerHr))return pointerHr;
                const auto timeHr=sample->GetTime(&begin,&end);const bool timed=SUCCEEDED(timeHr);
                if(!timed&&!warned){log::warn("capture-audio",std::format("PCM timestamp missing hr=0x{:X}; using sample-count clock",unsigned(timeHr)));warned=true;}
                const double arrival=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
                const double pts=clock.observe(timed?std::optional<double>(double(begin)/10000):std::nullopt,arrival,1000.0*sample->GetActualDataLength()/rate,sample->IsDiscontinuity()==S_OK);
                probe->feed(bytes,size_t(sample->GetActualDataLength()));
                return target->push(bytes,size_t(sample->GetActualDataLength()),pts,sample->IsDiscontinuity()==S_OK)?S_OK:E_FAIL;
            },candidate,terminal);
            if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio PCM");
            // Request small input blocks before connection; downstream
            // playback cannot undo time spent filling a driver buffer.
            // Some devices reject this advisory API, so do not fail capture.
            if(SUCCEEDED(hr))suggestCaptureAudioBuffering(audioPin.Get(),*reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat));
            if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
            if(SUCCEEDED(hr)){p.audioSink=candidate;p.audioSession=std::move(session);p.carrierProbe=probe;connectedAudio=true;log::info("capture-audio",std::format("selected media type channels={} mask=0x{:X} rate={} containerBits={} validBits={} floating={}",parsed.layout.channels,parsed.layout.mask,parsed.wave.nSamplesPerSec,parsed.wave.wBitsPerSample,parsed.validBits,parsed.floating?1:0));}
            else if(candidate)p.graph->RemoveFilter(candidate.Get());
            log::info("capture-audio",std::format("PCM ConnectDirect hr=0x{:X}",unsigned(hr)));
        }
        if(connectedAudio)return true;
    }
    return false;
    };
    if(!wantBitstreamFirst)connectedAudio=connectPcm();
    // Dolby/DTS passthrough fallback: no PCM type connected, so take the best
    // compressed type the device offers and decode it back to PCM. This is the
    // path a PS5 feeding Dolby Atmos / Dolby Audio / DTS needs.
    if(!connectedAudio&&allowBitstream&&!bitstreamTypes.empty()){
        std::stable_sort(bitstreamTypes.begin(),bitstreamTypes.end(),[](const auto& a,const auto& b){
            return sink::bitstreamPreferenceOrder(sink::classifyBitstreamSubtype(a->subtype.Data1))>
                   sink::bitstreamPreferenceOrder(sink::classifyBitstreamSubtype(b->subtype.Data1));
        });
        for(const auto& owned:bitstreamTypes){
            auto* type=owned.get();
            const auto kind=sink::classifyBitstreamSubtype(type->subtype.Data1);
            const bool iec=sink::bitstreamIsIec61937(type->subtype.Data1);
            // Bitstream-first mode with an IEC 61937 framed input: forward the
            // compressed stream unchanged to a receiver that accepts the
            // matching carrier (exclusive WASAPI). Only the receiver can
            // decode Dolby Atmos / DTS:X object audio; the in-player decode
            // below would reduce it to plain 5.1 PCM. Falls back silently when
            // no endpoint advertises the format.
            if(wantBitstreamFirst&&iec){
                const GUID* carrier=nullptr;
                switch(kind){
                case sink::BitstreamKind::Ac3:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;break;
                case sink::BitstreamKind::Eac3:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS;break;
                case sink::BitstreamKind::TrueHd:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP;break;
                case sink::BitstreamKind::Dts:carrier=&KSDATAFORMAT_SUBTYPE_IEC61937_DTS;break;
                // DTS-HD has no standard IEC 61937 subtype on Windows; it
                // stays on the decode path.
                default:break;
                }
                uint32_t rate=48000;
                if(type->pbFormat!=nullptr&&type->cbFormat>=sizeof(WAVEFORMATEX)){
                    const auto* wave=reinterpret_cast<const WAVEFORMATEX*>(type->pbFormat);
                    if(wave->nSamplesPerSec==44100||wave->nSamplesPerSec==48000||wave->nSamplesPerSec==96000||wave->nSamplesPerSec==192000)rate=wave->nSamplesPerSec;
                }
                if(carrier!=nullptr){
                    auto passthrough=std::make_shared<sink::BitstreamAudioSink>();
                    if(passthrough->open(*carrier,rate)){
                        ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
                        auto sink=passthrough;
                        hr=createBitstreamAudioSink(*type,[sink](IMediaSample* sample){
                            BYTE* bytes=nullptr;
                            if(FAILED(sample->GetPointer(&bytes)))return VFW_E_SAMPLE_TIME_NOT_SET;
                            return sink->write(bytes,size_t(sample->GetActualDataLength()))?S_OK:E_FAIL;
                        },candidate,terminal);
                        if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio bitstream passthrough");
                        if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
                        if(SUCCEEDED(hr)){
                            p.audioSink=candidate;p.audioPassthrough=passthrough;
                            {const std::string name=sink::bitstreamKindName(kind);p.audioBitstreamKind.assign(name.begin(),name.end());}
                            const auto& sinkState=passthrough->state();
                            log::info("capture-audio-bitstream",std::string("passthrough to receiver kind=")+sink::bitstreamKindName(kind)+
                                std::format(" rate={} endpoint=\"{}\" (no in-player decode)",rate,
                                    std::string(sinkState.endpointName.begin(),sinkState.endpointName.end())));
                            connectedAudio=true;
                        }else if(candidate)p.graph->RemoveFilter(candidate.Get());
                    }else{
                        log::info("capture-audio-bitstream",std::string("receiver passthrough unavailable for ")+sink::bitstreamKindName(kind)+"; decoding to PCM");
                    }
                    if(connectedAudio)break;
                }
            }
            auto decoder=std::make_shared<sink::BitstreamDecoder>();
            if(!decoder->open(kind,iec)){
                log::warn("capture-audio-bitstream",std::string("decoder unavailable for ")+sink::bitstreamKindName(kind)+": "+decoder->lastError());
                continue;
            }
            auto session=std::make_unique<sink::CaptureAudioSession>();
            ComPtr<IBaseFilter> candidate;ComPtr<IPin> terminal;
            auto* target=session.get();
            auto pcmBuffer=std::make_shared<std::vector<float>>();
            auto started=std::make_shared<bool>(false);
            const std::string kindName=sink::bitstreamKindName(kind);
            const std::wstring kindWide(kindName.begin(),kindName.end());
            hr=createBitstreamAudioSink(*type,[target,decoder,pcmBuffer,started,kindWide,clock=CaptureAudioClock{},warned=false](IMediaSample* sample) mutable {
                BYTE* bytes=nullptr;REFERENCE_TIME begin=0,end=0;
                const auto pointerHr=sample->GetPointer(&bytes);if(FAILED(pointerHr))return pointerHr;
                const auto timeHr=sample->GetTime(&begin,&end);const bool timed=SUCCEEDED(timeHr);
                if(!timed&&!warned){log::warn("capture-audio",std::format("bitstream timestamp missing hr=0x{:X}; using decoded sample-count clock",unsigned(timeHr)));warned=true;}
                pcmBuffer->clear();
                if(!decoder->push(bytes,size_t(sample->GetActualDataLength()),*pcmBuffer))return S_OK;
                if(pcmBuffer->empty())return S_OK;
                const unsigned channels=decoder->channels();
                const unsigned rate=decoder->sampleRate();
                if(channels==0||rate==0)return S_OK;
                if(!*started){
                    WAVEFORMATEXTENSIBLE wfx{};
                    wfx.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
                    wfx.Format.nChannels=WORD(channels);
                    wfx.Format.nSamplesPerSec=rate;
                    wfx.Format.wBitsPerSample=32;
                    wfx.Format.nBlockAlign=WORD(channels*4);
                    wfx.Format.nAvgBytesPerSec=rate*channels*4;
                    wfx.Format.cbSize=sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
                    wfx.Samples.wValidBitsPerSample=32;
                    // Standard layouts: 6 = 5.1, 8 = 7.1, otherwise 5.1 as the safe default.
                    wfx.dwChannelMask=channels==8?0x63Fu:channels==6?0x3Fu:channels==2?0x3u:0x3Fu;
                    wfx.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
                    if(!target->configure(wfx.Format,sizeof(wfx))||!target->start())return E_FAIL;
                    target->setInputBitstream(kindWide);
                    *started=true;
                }
                const double arrival=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
                const double pts=clock.observe(timed?std::optional<double>(double(begin)/10000):std::nullopt,arrival,1000.0*pcmBuffer->size()/channels/rate,sample->IsDiscontinuity()==S_OK);
                return target->push(pcmBuffer->data(),pcmBuffer->size()*sizeof(float),pts,sample->IsDiscontinuity()==S_OK)?S_OK:E_FAIL;
            },candidate,terminal);
            if(SUCCEEDED(hr))hr=p.graph->AddFilter(candidate.Get(),L"Veyra audio bitstream");
            if(SUCCEEDED(hr))hr=p.graph->ConnectDirect(audioPin.Get(),terminal.Get(),type);
            if(SUCCEEDED(hr)){
                p.audioSink=candidate;p.audioSession=std::move(session);p.audioBitstream=decoder;
                p.audioSessionDeferred=true;
                {const std::string name=sink::bitstreamKindName(kind);p.audioBitstreamKind.assign(name.begin(),name.end());}
                connectedAudio=true;
                log::info("capture-audio-bitstream",std::string("passthrough selected kind=")+sink::bitstreamKindName(kind)+(iec?" (IEC 61937)":"")+" -> decoded to PCM on the first frame");
            }else if(candidate)p.graph->RemoveFilter(candidate.Get());
            log::info("capture-audio-bitstream",std::string("ConnectDirect hr=0x")+std::format("{:X}",unsigned(hr)));
            if(connectedAudio)break;
        }
    }
    if(!connectedAudio&&wantBitstreamFirst)connectedAudio=connectPcm();
    return connectedAudio;
}
bool CaptureCardSource::start(){
    if(p_->aja){const bool ok=p_->aja->start();if(!ok)error_=p_->aja->error();return ok;}
    auto& p=*p_;if(p.info.opened)return true;if(!p.configured||!p.control)return false;
    if(p.audioSession&&p.audioSessionDeferred)log::info("capture-audio-bitstream","audio session starts with the first decoded bitstream frame");
    else if(p.audioSession&&!p.audioSession->start())log::warn("capture-audio","audio start failed; retaining video capture");
    p.audioRecovery.reset(GetTickCount64());
    p.lastFrame=Impl::Clock::now();const auto hr=p.control->Run();p.info.opened=SUCCEEDED(hr);
    if(p.info.opened&&p.wasapi&&!p.wasapi->start())p.audioError=L"WASAPI 音频启动失败；视频继续运行";
    veyra::log::info("capture",std::format("Run hr=0x{:X} actual={}x{} nominalFps={:.3f} mailbox=1 ownedBuffers=2",unsigned(hr),p.info.width,p.info.height,p.info.averageFps));return p.info.opened;
}
void CaptureCardSource::recoverAudio(float gain,unsigned syncMode,int offsetMs){
    auto& p=*p_;if(!p.info.opened||!p.control||p.wasapi)return;
    CaptureSelection selection;if(!parseCapturePath(reconnectDesc_.path,selection)||!selection.stable||selection.audio==kCaptureAudioDisabled||selection.audio==kCaptureAudioWasapi)return;
    // A video filter without an audio pin can never recover: in the field log
    // this retried seven times over twenty seconds with an identical failure.
    if(selection.audio==kCaptureAudioFromVideoDevice&&p.embeddedAudioUnavailable){
        static bool reported=false;
        if(!reported){reported=true;log::warn("capture-audio-reconnect","embedded audio pin is absent on this device; recovery disabled, select a separate audio device");}
        return;
    }
    const auto state=p.audioSession?p.audioSession->snapshot():sink::CaptureAudioState{};
    if(!p.audioRecovery.due(state.inputBlocks,GetTickCount64()))return;
    // DirectShow audio/video pins share one graph. Briefly stop it to mutate
    // only the audio branch; do not renegotiate or replace the video device.
    const HRESULT stopped=p.control->Stop();
    log::warn("capture-audio-reconnect",std::format("PCM stalled; Stop hr=0x{:08X} videoFilterRetained=1",uint32_t(stopped)));
    if(FAILED(stopped)){p.audioError=L"音频恢复等待采集驱动停止；稍后重试";return;}
    const bool connected=[&]{
        if(p.audioSink){const HRESULT hr=p.graph->RemoveFilter(p.audioSink.Get());log::info("capture-audio-reconnect",std::format("Remove PCM sink hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;p.audioSink.Reset();}
        if(p.audioSession)p.audioSession->stop();
        p.audioSession.reset();
        if(p.audioFilter){const HRESULT hr=p.graph->RemoveFilter(p.audioFilter.Get());log::info("capture-audio-reconnect",std::format("Remove audio device hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;p.audioFilter.Reset();}
        if(!connectDirectShowAudio(reconnectDesc_))return false;
        p.audioSession->setGain(gain);p.audioSession->setSync(syncMode,offsetMs);
        return p.audioSession->start();
    }();
    {std::lock_guard lock(p.mutex);p.pending=false;p.forceDiscontinuity=true;}
    ++epoch_;p.lastFrame=Impl::Clock::now();
    const HRESULT resumed=p.control->Run();p.info.opened=SUCCEEDED(resumed);
    p.audioError=connected&&p.info.opened?L"":L"采集音频暂不可用，正在重试原音频设备";
    log::info("capture-audio-reconnect",std::format("connected={} Run hr=0x{:08X} epoch={} awaitingActualPCM=1",connected,uint32_t(resumed),epoch_));
}
bool CaptureCardSource::reconnect(float gain,unsigned syncMode,int offsetMs){
    if(p_->aja){const auto desc=reconnectDesc_;const auto previous=info();if(!configure(desc))return false;if(info().width!=previous.width||info().height!=previous.height||info().nominalRateNum!=previous.nominalRateNum||info().nominalRateDen!=previous.nominalRateDen){close();error_=L"AJA 输入格式已变化，请重新连接。";return false;}setAudioGain(gain);setAudioSync(syncMode,offsetMs);++epoch_;return start();}
    CaptureSelection selection;
    if(!parseCapturePath(reconnectDesc_.path,selection)||!selection.stable||reconnectFormat_.empty()){
        log::warn("capture-reconnect","Stable device and format identity unavailable; manual selection required");return false;
    }
    const auto desc=reconnectDesc_;const auto key=reconnectFormat_;const auto expected=reconnectInfo_;
    // Stop callbacks before carrying counters into the next device session.
    // Never hold the mailbox lock while DirectShow Stop waits for a callback.
    if(p_->control){const HRESULT hr=p_->control->Stop();log::info("capture-reconnect",std::format("Stop hr=0x{:08X}",uint32_t(hr)));if(FAILED(hr))return false;}
    {std::lock_guard lock(p_->mutex);
        receivedOffset_+=p_->received;deliveredOffset_+=p_->sequence;droppedOffset_+=p_->dropped;}
    close();
    int format=-1;for(const auto& candidate:formatsByPath(selection.videoPath))if(candidate.key==key){format=candidate.index;break;}
    if(format<0)return false;
    auto reopen=desc;reopen.path=std::format(L"capture2:{}:{}:{}:{}:{}",encodePath(selection.videoPath),format,selection.audio,encodePath(selection.audioPath),selection.colorOverride);
    if(selection.requestedFps>0)reopen.path+=std::format(L"?fps={:.6f}",selection.requestedFps);
    bool ok=configure(reopen);
    if(ok){const auto& current=p_->info;
        ok=current.width==expected.width&&current.height==expected.height&&current.color.pixelFormat==expected.color.pixelFormat&&
           current.color.transfer==expected.color.transfer&&current.color.matrix==expected.color.matrix&&current.color.primaries==expected.color.primaries&&
           current.color.range==expected.color.range&&current.color.displayReferred709==expected.color.displayReferred709&&current.color.preserveSdrCodeValues==expected.color.preserveSdrCodeValues;
        if(!ok)log::warn("capture-reconnect","Negotiated input contract changed; explicit reselection required");
    }
    if(ok){setAudioGain(gain);setAudioSync(syncMode,offsetMs);ok=start();}
    if(ok){++epoch_;log::info("capture-reconnect",std::format("reconnected epoch={} formatIndex={} history reset required",epoch_,format));return true;}
    close();reconnectDesc_=desc;reconnectFormat_=key;reconnectInfo_=expected;return false;
}
CaptureMetrics CaptureCardSource::metrics()const{
    if(p_->aja)return p_->aja->metrics();
    auto& p=*p_;std::lock_guard lock(p.mutex);CaptureMetrics m;
    m.received=receivedOffset_+p.received;m.delivered=deliveredOffset_+p.sequence;m.dropped=droppedOffset_+p.dropped;m.readAgeMs=p.readAgeMs;
    if(p.recentArrivals.size()>1&&Impl::Clock::now()-p.latestArrival<std::chrono::seconds(1)){const double elapsed=std::chrono::duration<double>(p.recentArrivals.back()-p.recentArrivals.front()).count();if(elapsed>0)m.callbackFps=(p.recentArrivals.size()-1)/elapsed;}
    if(p.sequence)m.frameAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();return m;
}
SourceReadStatus CaptureCardSource::read(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,30);}
SourceReadStatus CaptureCardSource::tryRead(pipeline::FramePacket& packet,const AVFrame** frame){return readWithWait(packet,frame,0);}
SourceReadStatus CaptureCardSource::readWithWait(pipeline::FramePacket& packet,const AVFrame** frame,unsigned milliseconds){auto& p=*p_;if(p.aja){auto status=p.aja->read(packet,frame,milliseconds);if(status==SourceReadStatus::Frame)packet.sourceEpoch=epoch_;else if(status==SourceReadStatus::Error)error_=p.aja->error();return status;}*frame=nullptr;if(!p.info.opened)return SourceReadStatus::Error;
    long code=0;LONG_PTR a=0,b=0;while(p.events&&p.events->GetEvent(&code,&a,&b,0)==S_OK){if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)log::warn("capture-reconnect",std::format("DirectShow event={} detail=0x{:X}",code,uint64_t(a)));p.events->FreeEventParams(code,a,b);if(code==EC_DEVICE_LOST||code==EC_ERRORABORT)return SourceReadStatus::Error;}
    double time=0;uint32_t flags=0;uint64_t sequence=0;pipeline::Rational duration;AVFrame* delivered=nullptr;AVFrame* expiredHardware=nullptr;bool feedDecode=false;
    {
        std::unique_lock lock(p.mutex);if(milliseconds)p.wake.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return p.pending||p.callbackError;});
        if(p.callbackError)return SourceReadStatus::Error;
        if(!p.pending)return Impl::Clock::now()-p.lastFrame>std::chrono::seconds(3)?SourceReadStatus::Error:SourceReadStatus::Waiting;
        if(p.pendingIsHardware){
            // D3D12VA surfaces cannot be copied into the preallocated NV12
            // buffer; the read slot owns the cloned frame and the previous one
            // is released after the lock is dropped.
            expiredHardware=p.hardwareRead;p.hardwareRead=p.pendingHardware;p.pendingHardware=nullptr;delivered=p.hardwareRead;
        }else if(p.compressedPath){
            // The caller's ownership of the previous frame ends with this read,
            // so it goes back into the decode pool and the worker continues.
            if(p.frame)p.compressedFree.push_back(p.frame);
            p.frame=p.pendingFrame;p.pendingFrame=nullptr;delivered=p.frame;
            if(p.workerFrame==nullptr&&!p.compressedFree.empty()){p.workerFrame=p.compressedFree.back();p.compressedFree.pop_back();feedDecode=true;}
        }else{
            // Native path keeps the original mailbox rotation: the frame the
            // caller releases becomes the next ingest target, and pendingFrame
            // must never be null here (SampleCB rejects that as an error).
            std::swap(p.frame,p.pendingFrame);delivered=p.frame;
        }
        p.pending=false;time=p.pendingTime;p.readArrival=p.pendingArrival;
        duration=p.pendingDuration;
        p.readAgeMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-p.readArrival).count();
        if(!p.sequence)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Open);
        if(p.dropped!=p.lastDrop)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Drop);
        if(p.pendingDiscontinuity)flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);
        if(p.forceDiscontinuity){flags|=static_cast<uint32_t>(pipeline::FrameFlagBits::Discontinuity);p.forceDiscontinuity=false;}
        p.lastDrop=p.dropped;p.lastPts=time;++p.sequence;sequence=p.compressedPath?p.compressedDecoded:p.received;
    }
    if(feedDecode)p.decodeWake.notify_one();
    if(expiredHardware)av_frame_free(&expiredHardware);
    if(delivered==nullptr)return SourceReadStatus::Error;
    auto colorInfo=p.info.color;
    if(p.compressedPath){
        // Compressed video carries its own VUI metadata; the negotiated layout
        // stays the declared fallback. D3D12VA surfaces report NV12 by
        // contract (the capture ingress contract is 8-bit for this path).
        colorInfo=pipeline::resolveFrameColor(*delivered,p.layout.color);
        if(delivered->format==AV_PIX_FMT_D3D12&&colorInfo.pixelFormat==pipeline::SourcePixelFormat::Unknown)colorInfo.pixelFormat=pipeline::SourcePixelFormat::NV12;
    }
    delivered->pts=static_cast<int64_t>(time*10000000);delivered->duration=duration.isUnknown()?0:duration.to100ns();delivered->time_base={1,10000000};packet={};packet.pts={delivered->pts,10000000};packet.duration=duration;packet.colorInfo=colorInfo;packet.sourceKind=pipeline::SourceKind::CaptureCard;packet.sequence=sequence;packet.flags=flags;packet.sourceEpoch=1;
    packet.sequence+=receivedOffset_;packet.sourceEpoch=epoch_;
    packet.arrivalHost100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(p.readArrival.time_since_epoch()).count()/100;
    if(log::verboseFrameLogs())log::info("capture-read-sample",std::format("source={} arrival={} read={} width={} height={} format={}",packet.sequence,packet.arrivalHost100ns,std::chrono::duration_cast<std::chrono::nanoseconds>(Impl::Clock::now().time_since_epoch()).count()/100,delivered->width,delivered->height,delivered->format));
    *frame=delivered;p.lastFrame=Impl::Clock::now();return SourceReadStatus::Frame;
}
void CaptureCardSource::close()noexcept{
    if(p_->aja){p_->aja->close();p_->aja.reset();}
    auto& p=*p_;if(p.control){const HRESULT hr=p.control->Stop();if(FAILED(hr))log::error("capture-close",std::format("Stop failed hr=0x{:08X}; releasing graph",uint32_t(hr)));}if(p.grab){const HRESULT hr=p.grab->SetCallback(nullptr,0);if(FAILED(hr))log::error("capture-close",std::format("detach callback hr=0x{:08X}",uint32_t(hr)));}
    if(p.wasapi)p.wasapi->stop();p.wasapi.reset();
    p.averMediaSwitch.stop();p.embeddedAudioUnavailable=false;
    if(p.audioSession)p.audioSession->stop();p.audioError.clear();
    if(p.audioPassthrough)p.audioPassthrough->close();
    p.events.Reset();p.control.Reset();p.grab.Reset();p.nullFilter.Reset();p.grabFilter.Reset();p.audioSink.Reset();p.audioFilter.Reset();p.config.Reset();p.device.Reset();p.builder.Reset();p.graph.Reset();p.referenceClock.Reset();p.audioSession.reset();p.audioPassthrough.reset();
    if(p.decodeThread.joinable()){{std::lock_guard lock(p.mutex);p.decodeStop=true;p.compressedQueue.clear();}p.decodeWake.notify_all();p.decodeThread.join();}
    if(p.workerFrame)av_frame_free(&p.workerFrame);
    for(auto*& frame:p.compressedFree)if(frame)av_frame_free(&frame);
    p.compressedFree.clear();
    av_frame_free(&p.frame);av_frame_free(&p.pendingFrame);
    if(p.pendingHardware)av_frame_free(&p.pendingHardware);if(p.hardwareRead)av_frame_free(&p.hardwareRead);
    p.compressedDecoder.close();
    if(p.decodeDevice){p.decodeDevice->Release();p.decodeDevice=nullptr;}if(p.decodeQueue){p.decodeQueue->Release();p.decodeQueue=nullptr;}
    p.pendingIsHardware=false;p.compressedPath=false;p.codec=CaptureCodec::None;p.compressedDecoded=p.compressedErrors=0;
    p.info={};
    p.sequence=p.received=p.dropped=p.lastDrop=0;p.pending=p.callbackError=p.configured=p.forceDiscontinuity=false;p.lastPts=p.readAgeMs=0;
    p.recentArrivals.clear();
}
}
