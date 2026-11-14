#include "veyra/engine/MicrophoneCapture.h"

// The audio client headers come before AudioFormat.h (ks.h), as in
// source/WasapiAudioInput.cpp.
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/RecordingAudioTap.h"
#include "veyra/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace veyra::engine {
using Microsoft::WRL::ComPtr;
namespace {

constexpr int kRate=48000;
// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h), defined here so
// this library does not depend on another translation unit instantiating it.
const PROPERTYKEY kFriendlyName{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};

std::string utf8(const std::wstring& text){
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
struct ComScope {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
std::wstring friendlyName(IMMDevice* device){
    std::wstring name;
    ComPtr<IPropertyStore> properties;
    if(FAILED(device->OpenPropertyStore(STGM_READ,&properties)))return name;
    PROPVARIANT value;PropVariantInit(&value);
    if(SUCCEEDED(properties->GetValue(kFriendlyName,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)name=value.pwszVal;
    PropVariantClear(&value);
    return name;
}
std::wstring describe(HRESULT hr){
    if(hr==E_ACCESSDENIED)return L"Windows 不允许 Veyra 使用麦克风，请在 设置 → 隐私和安全性 → 麦克风 中允许桌面应用访问";
    if(hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)||hr==AUDCLNT_E_DEVICE_INVALIDATED||hr==E_NOTFOUND)
        return L"找不到所选麦克风，请插回设备或重新选择";
    if(hr==AUDCLNT_E_DEVICE_IN_USE)return L"麦克风被其他程序独占，请关闭独占模式后再试";
    return std::format(L"麦克风无法录音（0x{:08X}）",uint32_t(hr));
}

// Mix-format packets to interleaved float stereo at the endpoint's rate.
void toStereo(const BYTE* data,UINT frames,const sink::WavePcmFormat& format,std::vector<float>& out){
    const unsigned channels=format.layout.channels,bytes=format.wave.wBitsPerSample/8;
    out.resize(size_t(frames)*2);
    auto sample=[&](const BYTE* p)->float{
        if(format.floating){float v;std::memcpy(&v,p,4);return std::isfinite(v)?v:0.0f;}
        if(bytes==2){int16_t v;std::memcpy(&v,p,2);return float(v)/32768.0f;}
        if(bytes==3){const int32_t v=int32_t(uint32_t(p[0])<<8|uint32_t(p[1])<<16|uint32_t(p[2])<<24)>>8;return float(v)/8388608.0f;}
        int32_t v;std::memcpy(&v,p,4);return float(double(v)/2147483648.0);
    };
    for(UINT i=0;i<frames;++i){
        const BYTE* frame=data+size_t(i)*format.wave.nBlockAlign;
        const float left=sample(frame);
        out[size_t(i)*2]=left;
        out[size_t(i)*2+1]=channels>1?sample(frame+bytes):left;
    }
}

struct Hub {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::shared_ptr<MicrophoneSink>> sinks;
    std::wstring id;
    uint64_t generation=0;               // +1 per change of the endpoint or of "anyone listening"
    std::atomic<uint64_t> current{0};
    MicrophoneStatus status;
    bool started=false;

    void deliver(const float* pcm,size_t frames,int64_t capturedUs){
        std::lock_guard lock(m);
        for(const auto& sink:sinks)sink->microphonePcm(pcm,frames,capturedUs);
        ++status.packets;
    }
    bool wanted(uint64_t gen) const {return current.load()==gen;}

    // One open of the endpoint; returns when nobody listens, the endpoint
    // changes or the stream fails.
    HRESULT stream(const std::wstring& endpointId,uint64_t gen){
        ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDevice> device;ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
        HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator));
        if(FAILED(hr))return hr;
        hr=endpointId.empty()?enumerator->GetDefaultAudioEndpoint(eCapture,eConsole,&device):enumerator->GetDevice(endpointId.c_str(),&device);
        if(FAILED(hr))return hr;
        if(!endpointId.empty()){
            ComPtr<IMMEndpoint> endpoint;EDataFlow flow=eAll;
            if(FAILED(device.As(&endpoint))||FAILED(endpoint->GetDataFlow(&flow))||flow!=eCapture)return E_INVALIDARG;
        }
        const std::wstring name=friendlyName(device.Get());
        {std::lock_guard lock(m);status.device=name.empty()?L"WASAPI recording endpoint":name;}
        hr=device->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));
        if(FAILED(hr))return hr;
        WAVEFORMATEX* raw=nullptr;
        hr=client->GetMixFormat(&raw);
        if(FAILED(hr))return hr;
        auto freeWave=[](WAVEFORMATEX* value){CoTaskMemFree(value);};
        std::unique_ptr<WAVEFORMATEX,decltype(freeWave)> wave(raw,freeWave);
        sink::WavePcmFormat format;
        if(!raw||!sink::parseWavePcm(raw,sizeof(WAVEFORMATEX)+raw->cbSize,format))return AUDCLNT_E_UNSUPPORTED_FORMAT;
        hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,200000,0,raw,nullptr);
        if(FAILED(hr))return hr;
        Handle ready;ready.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!ready.value)return HRESULT_FROM_WIN32(GetLastError());
        if(FAILED(hr=client->SetEventHandle(ready.value)))return hr;
        if(FAILED(hr=client->GetService(IID_PPV_ARGS(&capture))))return hr;
        const unsigned rate=raw->nSamplesPerSec;
        // Endpoints at another rate are resampled here; the recorder takes 48 kHz.
        SwrContext* swr=nullptr;
        struct FreeSwr {SwrContext** swr;~FreeSwr(){if(*swr)swr_free(swr);}} freeSwr{&swr};
        if(rate!=unsigned(kRate)){
            const AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_FLT,kRate,&stereo,AV_SAMPLE_FMT_FLT,int(rate),0,nullptr)<0||!swr||swr_init(swr)<0)
                return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        // QPC packet times to the recording clock (both steady, same origin in
        // practice; the measured offset keeps it exact if they ever differ).
        LARGE_INTEGER frequency{},counter{};
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&counter);
        const int64_t offsetUs=sink::recordingClockUs()-int64_t(double(counter.QuadPart)*1e6/double(std::max<LONGLONG>(1,frequency.QuadPart)));
        if(FAILED(hr=client->Start()))return hr;
        struct StopClient {IAudioClient* client;~StopClient(){client->Stop();}} stopClient{client.Get()};
        {std::lock_guard lock(m);status.open=true;status.error.clear();status.rate=rate;status.channels=raw->nChannels;++status.opens;}
        veyra::log::info("microphone",std::format("capture open device={} rate={} channels={} bits={} float={} resample={}",
            utf8(name),rate,raw->nChannels,raw->wBitsPerSample,format.floating,swr!=nullptr));
        std::vector<float> stereo,resampled;
        int64_t nextUs=0;bool haveNext=false;
        auto lastPacket=std::chrono::steady_clock::now();
        while(wanted(gen)){
            const DWORD wait=WaitForSingleObject(ready.value,100);
            if(wait==WAIT_FAILED)return HRESULT_FROM_WIN32(GetLastError());
            UINT next=0;
            if(FAILED(hr=capture->GetNextPacketSize(&next)))return hr;
            if(!next&&std::chrono::steady_clock::now()-lastPacket>std::chrono::seconds(3))return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            while(next&&wanted(gen)){
                BYTE* data=nullptr;UINT frames=0;DWORD flags=0;UINT64 position=0,qpc=0;
                hr=capture->GetBuffer(&data,&frames,&flags,&position,&qpc);
                if(FAILED(hr))return hr;
                if(hr==AUDCLNT_S_BUFFER_EMPTY)break;
                if(frames){
                    if((flags&AUDCLNT_BUFFERFLAGS_SILENT)||!data){stereo.assign(size_t(frames)*2,0.0f);}
                    else toStereo(data,frames,format,stereo);
                }
                if(FAILED(hr=capture->ReleaseBuffer(frames)))return hr;
                lastPacket=std::chrono::steady_clock::now();
                if(frames){
                    int64_t at=(qpc&&!(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))?int64_t(qpc/10)+offsetUs
                              :haveNext?nextUs:sink::recordingClockUs()-int64_t(frames)*1000000/int64_t(rate);
                    nextUs=at+int64_t(frames)*1000000/int64_t(rate);haveNext=true;
                    if(swr){
                        // Output starts with what the resampler still held from earlier input.
                        at-=swr_get_delay(swr,int64_t(rate))*1000000/int64_t(rate);
                        const int capacity=swr_get_out_samples(swr,int(frames))+32;
                        resampled.resize(size_t(std::max(capacity,0))*2);
                        uint8_t* out[1]={reinterpret_cast<uint8_t*>(resampled.data())};
                        const uint8_t* in[1]={reinterpret_cast<const uint8_t*>(stereo.data())};
                        const int got=swr_convert(swr,out,capacity,in,int(frames));
                        if(got>0)deliver(resampled.data(),size_t(got),at);
                    }else deliver(stereo.data(),frames,at);
                }
                if(FAILED(hr=capture->GetNextPacketSize(&next)))return hr;
            }
        }
        return S_OK;
    }

    // Tests only (VEYRA_TEST_MICROPHONE_TONE=1): no device is opened. A 2 kHz
    // tone, 50 ms long at every x.5 s of the recording clock, arrives in 10 ms
    // packets stamped as captured ones are, so the microphone path after
    // WASAPI (lanes, mixing, tracks, timing) can be checked from the file.
    HRESULT tone(uint64_t gen){
        {std::lock_guard lock(m);status.open=true;status.error.clear();status.device=L"test tone";status.rate=kRate;status.channels=2;++status.opens;}
        veyra::log::info("microphone","test tone open: 2 kHz, 50 ms at every x.5 s of the recording clock");
        constexpr int kBlock=kRate/100;
        std::vector<float> block(size_t(kBlock)*2);
        int64_t at=sink::recordingClockUs()/10000*10000;
        while(wanted(gen)){
            const int64_t now=sink::recordingClockUs();
            if(now<at+10000){std::this_thread::sleep_for(std::chrono::microseconds(std::min<int64_t>(at+10000-now,20000)));continue;}
            for(int i=0;i<kBlock;++i){
                const int64_t t=at+int64_t(i)*1000000/kRate;
                const int64_t phase=t%1000000;
                const float v=phase>=500000&&phase<550000?0.25f*float(std::sin(2.0*3.14159265358979*2000.0*double(phase)/1e6)):0.0f;
                block[size_t(i)*2]=block[size_t(i)*2+1]=v;
            }
            deliver(block.data(),size_t(kBlock),at);
            at+=10000;
        }
        return S_OK;
    }

    void run(){
        ComScope apartment;
        if(FAILED(apartment.hr)){std::lock_guard lock(m);status.error=describe(apartment.hr);return;}
        const bool testTone=GetEnvironmentVariableW(L"VEYRA_TEST_MICROPHONE_TONE",nullptr,0)>0;
        unsigned attempt=0;
        for(;;){
            std::wstring endpoint;uint64_t gen=0;
            {
                std::unique_lock lock(m);
                cv.wait(lock,[&]{return !sinks.empty();});
                endpoint=id;gen=generation;
            }
            const HRESULT hr=testTone?tone(gen):stream(endpoint,gen);
            bool retry=false;
            {
                std::lock_guard lock(m);
                status.open=false;
                if(FAILED(hr)&&generation==gen){
                    status.error=describe(hr);++status.failures;retry=true;
                    veyra::log::warn("microphone",std::format("capture failed hr=0x{:08X} device={} attempt={}",uint32_t(hr),utf8(status.device),attempt+1));
                }else{
                    attempt=0;
                    veyra::log::info("microphone",std::format("capture closed hr=0x{:08X} listeners={}",uint32_t(hr),sinks.size()));
                }
            }
            if(retry){
                // A missing or refused microphone is retried while recording,
                // so plugging it back in or allowing it in Windows recovers.
                const auto backoff=std::chrono::milliseconds(std::min(attempt+1,5u)*1000);++attempt;
                std::unique_lock lock(m);
                cv.wait_for(lock,backoff,[&]{return generation!=gen;});
            }
        }
    }
};

Hub& hub(){
    // Process lifetime: the thread may still sit in WaitForSingleObject at exit.
    static Hub* instance=new Hub;
    return *instance;
}

} // namespace

std::vector<MicrophoneDevice> microphoneDevices(){
    std::vector<MicrophoneDevice> out;
    ComScope apartment;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return out;
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDeviceCollection> collection;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)))||
       FAILED(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection)))return out;
    UINT count=0;
    if(FAILED(collection->GetCount(&count)))return out;
    for(UINT i=0;i<count;++i){
        ComPtr<IMMDevice> device;LPWSTR raw=nullptr;
        if(FAILED(collection->Item(i,&device))||FAILED(device->GetId(&raw)))continue;
        MicrophoneDevice entry{raw,friendlyName(device.Get())};CoTaskMemFree(raw);
        if(entry.name.empty())entry.name=L"WASAPI recording endpoint";
        out.push_back(std::move(entry));
    }
    return out;
}

void attachMicrophone(std::shared_ptr<MicrophoneSink> sink,const std::wstring& endpointId){
    if(!sink)return;
    auto& h=hub();
    {
        std::lock_guard lock(h.m);
        const bool first=h.sinks.empty();
        if(std::find(h.sinks.begin(),h.sinks.end(),sink)==h.sinks.end())h.sinks.push_back(std::move(sink));
        if(first||h.id!=endpointId){h.id=endpointId;h.current=++h.generation;}
        if(!h.started){
            h.started=true;
            std::thread([&h]{h.run();}).detach();
        }
    }
    h.cv.notify_all();
}

void detachMicrophone(const MicrophoneSink* sink){
    auto& h=hub();
    {
        std::lock_guard lock(h.m);
        const auto before=h.sinks.size();
        std::erase_if(h.sinks,[sink](const auto& s){return s.get()==sink;});
        if(before&&h.sinks.empty())h.current=++h.generation;
    }
    h.cv.notify_all();
}

MicrophoneStatus microphoneStatus(){
    auto& h=hub();
    std::lock_guard lock(h.m);
    return h.status;
}

} // namespace veyra::engine
