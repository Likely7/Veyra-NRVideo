#include "veyra/source/WasapiAudioInput.h"
#include "veyra/Log.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include "veyra/sink/AudioFormat.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <thread>

namespace veyra::source {
using Microsoft::WRL::ComPtr;
namespace {
double hostMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct ComScope {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);} };
bool checked(HRESULT hr,const char* operation){
    if(FAILED(hr))log::error("wasapi-input",std::format("{} hr=0x{:08X}",operation,uint32_t(hr)));
    return SUCCEEDED(hr);
}
}
std::vector<WasapiInputDevice> WasapiAudioInput::devices(){
    ComScope apartment;std::vector<WasapiInputDevice> result;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE){checked(apartment.hr,"enumeration COM");return result;}
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDeviceCollection> collection;
    if(!checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)),"enumerator")||
       !checked(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection),"EnumAudioEndpoints capture"))return result;
    UINT count=0;if(!checked(collection->GetCount(&count),"GetCount"))return result;
    for(UINT i=0;i<count;++i){
        ComPtr<IMMDevice> device;ComPtr<IPropertyStore> properties;LPWSTR raw=nullptr;
        if(!checked(collection->Item(i,&device),"Item")||!checked(device->GetId(&raw),"GetId"))continue;
        WasapiInputDevice input{L"WASAPI recording endpoint",raw};CoTaskMemFree(raw);
        PROPVARIANT name;PropVariantInit(&name);
        if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))&&SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR&&name.pwszVal)input.name=name.pwszVal;
        PropVariantClear(&name);result.push_back(std::move(input));
    }
    return result;
}
struct WasapiAudioInput::Impl {
    mutable std::mutex mutex;std::unique_ptr<sink::CaptureAudioSession> session;
    std::wstring id,error;std::thread worker;Handle stopEvent;std::atomic<bool> stopping{true};
    float gain=1;unsigned mode=0;int offset=0;double videoPts=0;int64_t videoHost=0;bool haveVideo=false;
    std::optional<int64_t> videoArrival;
    WasapiInputMetrics metrics;bool injectedLoss=false;
    HRESULT stream(){
        ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDevice> device;ComPtr<IMMEndpoint> endpoint;
        ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;Handle ready;
        HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator));
        if(!checked(hr,"create enumerator"))return hr;
        hr=enumerator->GetDevice(id.c_str(),&device);if(!checked(hr,"selected endpoint GetDevice"))return hr;
        hr=device.As(&endpoint);if(!checked(hr,"IMMEndpoint"))return hr;
        EDataFlow flow=eAll;hr=endpoint->GetDataFlow(&flow);if(!checked(hr,"GetDataFlow"))return hr;
        if(flow!=eCapture){checked(E_INVALIDARG,"render/loopback endpoint rejected");return E_INVALIDARG;}
        hr=device->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));
        if(!checked(hr,"Activate capture client"))return hr;
        WAVEFORMATEX* raw=nullptr;hr=client->GetMixFormat(&raw);
        if(!checked(hr,"GetMixFormat"))return hr;
        auto freeWave=[](WAVEFORMATEX* value){CoTaskMemFree(value);};std::unique_ptr<WAVEFORMATEX,decltype(freeWave)> wave(raw,freeWave);
        sink::WavePcmFormat format;
        if(!raw||!sink::parseWavePcm(raw,sizeof(WAVEFORMATEX)+raw->cbSize,format)){checked(AUDCLNT_E_UNSUPPORTED_FORMAT,"mix PCM format");return AUDCLNT_E_UNSUPPORTED_FORMAT;}
        hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,200000,0,raw,nullptr);
        if(!checked(hr,"Initialize shared capture"))return hr;
        ready.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!ready.value)return HRESULT_FROM_WIN32(GetLastError());
        hr=client->SetEventHandle(ready.value);if(!checked(hr,"SetEventHandle"))return hr;
        hr=client->GetService(IID_PPV_ARGS(&capture));if(!checked(hr,"GetService capture"))return hr;
        UINT capacity=0;hr=client->GetBufferSize(&capacity);if(!checked(hr,"GetBufferSize"))return hr;
        // A single input packet is bounded to the shared session's 500ms limit.
        const size_t maxFrames=std::min<size_t>(capacity,raw->nSamplesPerSec/2);
        if(!maxFrames)return E_INVALIDARG;
        std::vector<BYTE> silent(maxFrames*raw->nBlockAlign,0);
        LARGE_INTEGER frequency{},qpc{};
        if(!QueryPerformanceFrequency(&frequency)||!QueryPerformanceCounter(&qpc)||frequency.QuadPart<=0)return E_FAIL;
        WasapiPacketClock clock;clock.reset(raw->nSamplesPerSec,hostMs()-1000.0*double(qpc.QuadPart)/double(frequency.QuadPart));
        {
            std::lock_guard lock(mutex);session=std::make_unique<sink::CaptureAudioSession>();
            if(!session->configure(format)){session.reset();return AUDCLNT_E_UNSUPPORTED_FORMAT;}
            session->setGain(gain);session->setSync(mode,offset);
            if(!session->start()){session.reset();return E_FAIL;}
            if(haveVideo)session->videoPresented(videoPts,videoHost,videoArrival);
        }
        hr=client->Start();if(!checked(hr,"Start capture"))return hr;
        struct StopClient {IAudioClient* client;~StopClient(){checked(client->Stop(),"Stop capture");}} stopClient{client.Get()};
        {std::lock_guard lock(mutex);error.clear();}
        log::info("wasapi-input",std::format("capture started shared=1 loopback=0 rate={} channels={} bits={} validBits={} capacity={} timestamp=QPC-host-axis",raw->nSamplesPerSec,raw->nChannels,raw->wBitsPerSample,format.validBits,capacity));
        HANDLE events[]={stopEvent.value,ready.value};uint64_t packets=0,estimated=0,gaps=0;double lastPacket=hostMs();
        const bool injectLoss=GetEnvironmentVariableW(L"VEYRA_TEST_WASAPI_INPUT_LOSS",nullptr,0)>0;
        while(!stopping){
            const DWORD wait=WaitForMultipleObjects(2,events,FALSE,1000);
            if(wait==WAIT_OBJECT_0)break;
            if(wait==WAIT_FAILED)return HRESULT_FROM_WIN32(GetLastError());
            // Also inspect on timeout: some drivers lose notifications.
            UINT next=0;hr=capture->GetNextPacketSize(&next);if(!checked(hr,"GetNextPacketSize"))return hr;
            if(!next&&hostMs()-lastPacket>3000){checked(HRESULT_FROM_WIN32(ERROR_TIMEOUT),"no input packets for 3s");return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
            while(next&&!stopping){
                BYTE* data=nullptr;UINT frames=0;DWORD flags=0;UINT64 position=0,timestamp=0;
                hr=capture->GetBuffer(&data,&frames,&flags,&position,&timestamp);if(!checked(hr,"GetBuffer"))return hr;
                if(hr==AUDCLNT_S_BUFFER_EMPTY)break;
                struct PacketLease {IAudioCaptureClient* capture;UINT frames;bool held=true;~PacketLease(){if(held)checked(capture->ReleaseBuffer(frames),"ReleaseBuffer unwind");}} lease{capture.Get(),frames};
                const auto bytes=size_t(frames)*raw->nBlockAlign;
                const void* packet=wasapiPacketData(data,bytes,(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0,silent);
                if(!packet){checked(E_INVALIDARG,"invalid packet size/data");return E_INVALIDARG;}
                const auto stamp=clock.stamp(position,timestamp,frames,(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)!=0,(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)!=0,hostMs());
                bool accepted=false;
                {
                    std::lock_guard lock(mutex);
                    accepted=session->push(packet,bytes,stamp.ptsMs,stamp.discontinuity);
                }
                hr=capture->ReleaseBuffer(frames);lease.held=false;if(!checked(hr,"ReleaseBuffer"))return hr;
                if(!accepted){checked(E_FAIL,"shared PCM queue rejected packet");return E_FAIL;}
                ++packets;lastPacket=hostMs();estimated+=stamp.estimated;gaps+=stamp.discontinuity;
                {std::lock_guard lock(mutex);++metrics.packets;metrics.timestampErrors+=stamp.estimated;metrics.discontinuities+=stamp.discontinuity;}
                if(packets==1||packets%500==0)log::info("wasapi-input",std::format("packets={} timestampEstimated={} discontinuities={} position={} frames={}",packets,estimated,gaps,position,frames));
                if(injectLoss&&!injectedLoss&&packets>=25){injectedLoss=true;log::warn("wasapi-input","test-only injected input invalidation after actual PCM");return AUDCLNT_E_DEVICE_INVALIDATED;}
                hr=capture->GetNextPacketSize(&next);if(!checked(hr,"GetNextPacketSize"))return hr;
            }
        }
        return S_OK;
    }
    void run(){
        ComScope apartment;
        if(!checked(apartment.hr,"capture thread COM")){std::lock_guard lock(mutex);error=L"WASAPI input thread initialization failed";return;}
        for(uint64_t attempt=0;!stopping;++attempt){
            if(attempt){std::lock_guard lock(mutex);++metrics.retries;}
            const HRESULT hr=stream();
            {std::lock_guard lock(mutex);if(session)session->stop();session.reset();
                if(FAILED(hr))error=std::format(L"WASAPI input failed 0x{:08X}; waiting for the original device to reconnect, video continues",uint32_t(hr));}
            if(SUCCEEDED(hr)||stopping)break;
            const DWORD backoff=DWORD(std::min<uint64_t>(attempt+1,5)*1000);
            log::warn("wasapi-input",std::format("input attempt={} failed hr=0x{:08X} retryInMs={} defaultFallback=0",attempt+1,uint32_t(hr),backoff));
            if(WaitForSingleObject(stopEvent.value,backoff)==WAIT_OBJECT_0)break;
        }
    }
};
WasapiAudioInput::WasapiAudioInput():p_(std::make_unique<Impl>()){}
WasapiAudioInput::~WasapiAudioInput(){stop();}
bool WasapiAudioInput::configure(std::wstring id){if(p_->worker.joinable()||id.empty())return false;p_->id=std::move(id);return true;}
bool WasapiAudioInput::start(){
    if(p_->worker.joinable()||p_->id.empty())return false;
    if(p_->stopEvent.value)CloseHandle(p_->stopEvent.value);
    p_->stopEvent.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!p_->stopEvent.value)return false;
    p_->stopping=false;
    {std::lock_guard lock(p_->mutex);p_->error.clear();p_->metrics={};p_->injectedLoss=false;}
    try{p_->worker=std::thread([this]{try{p_->run();}catch(const std::exception& e){log::error("wasapi-input",e.what());std::lock_guard lock(p_->mutex);if(p_->session)p_->session->stop();p_->session.reset();p_->error=L"WASAPI input thread exception; please reconnect";}});}
    catch(...){p_->stopping=true;return false;}return true;
}
void WasapiAudioInput::stop(){p_->stopping=true;if(p_->stopEvent.value)SetEvent(p_->stopEvent.value);if(p_->worker.joinable())p_->worker.join();}
void WasapiAudioInput::setGain(float value){std::lock_guard lock(p_->mutex);p_->gain=std::clamp(value,0.f,1.f);if(p_->session)p_->session->setGain(p_->gain);}
void WasapiAudioInput::setSync(unsigned mode,int offset){std::lock_guard lock(p_->mutex);p_->mode=std::min(mode,2u);p_->offset=std::clamp(offset,-250,250);if(p_->session)p_->session->setSync(p_->mode,p_->offset);}
void WasapiAudioInput::videoPresented(double pts,int64_t host,std::optional<int64_t> arrival){std::lock_guard lock(p_->mutex);p_->haveVideo=true;p_->videoPts=pts;p_->videoHost=host;p_->videoArrival=arrival;if(p_->session)p_->session->videoPresented(pts,host,arrival);}
void WasapiAudioInput::videoReset(bool resetAudio){std::lock_guard lock(p_->mutex);p_->haveVideo=false;if(p_->session)p_->session->videoReset(resetAudio);}
sink::CaptureAudioState WasapiAudioInput::snapshot()const{std::lock_guard lock(p_->mutex);auto state=p_->session?p_->session->snapshot():sink::CaptureAudioState{};if(!p_->error.empty())state.error=p_->error;return state;}
WasapiInputMetrics WasapiAudioInput::metrics()const{std::lock_guard lock(p_->mutex);return p_->metrics;}
}
