// SPDX-License-Identifier: GPL-3.0-only
// Virtual camera cadence check (2026-11-17). Measures what a DirectShow consumer
// such as OBS actually receives from the Veyra camera: when each sample arrives
// and which picture it carries.
//
//   consume <camera dll|registered> <seconds> <fps> <width> <height> <row> <out-prefix>
//       Loads the camera module straight from the given DLL (any version, no
//       registration) or binds the registered device, asks for NV12 at that size
//       and frame rate as OBS does, runs for the given time and writes
//       <out-prefix>.csv (arrival 100 ns, sample start 100 ns, frame bytes) and
//       <out-prefix>.rows (Y row `row` of every sample) for the analysis script.
//   produce <seconds> <presented fps> <width> <height> <jitter us> <delay ms min> <delay ms max>
//       A synthetic producer on the product's queue and grid (CameraGrid.h):
//       presents at the given rate with timing jitter, readbacks land after a random
//       delay, frames are published in order. Each picture holds a bar moving 2 px
//       per presented frame, so the analysis reads content time from its position.
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include "virtualcam-guid.h"
#include "shared-memory-queue.h"
#include "veyra/engine/CameraGrid.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
struct __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85")) ISampleGrabberCB: IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double,IMediaSample*)=0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double,BYTE*,long)=0;
};
struct __declspec(uuid("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")) ISampleGrabber:IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL)=0;virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*)=0;virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL)=0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*,long*)=0;virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**)=0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB*,long)=0;
};
static constexpr GUID sampleGrabber{0xc1f400a0,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
static constexpr GUID nullRenderer{0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
static int failure(const char* text,HRESULT hr){std::fprintf(stderr,"FAIL %s hr=0x%08X\n",text,unsigned(hr));return 2;}
static void freeType(AM_MEDIA_TYPE* type){if(!type)return;CoTaskMemFree(type->pbFormat);if(type->pUnk)type->pUnk->Release();CoTaskMemFree(type);}
static ComPtr<IPin> pinOf(IBaseFilter* filter,PIN_DIRECTION wanted){
    ComPtr<IEnumPins> all;filter->EnumPins(&all);
    for(;;){ComPtr<IPin> item;if(all->Next(1,&item,nullptr)!=S_OK)break;PIN_DIRECTION direction;
        if(SUCCEEDED(item->QueryDirection(&direction))&&direction==wanted)return item;}
    return nullptr;
}
static HRESULT direct(IGraphBuilder* graph,IBaseFilter* from,IBaseFilter* to){
    auto out=pinOf(from,PINDIR_OUTPUT),in=pinOf(to,PINDIR_INPUT);
    return out&&in?graph->ConnectDirect(out.Get(),in.Get(),nullptr):E_NOINTERFACE;
}
struct Receiver final:ISampleGrabberCB {
    std::atomic<ULONG> refs{1};
    unsigned width=0,row=0;
    struct Sample{uint64_t arrival;REFERENCE_TIME start;long bytes;};
    std::vector<Sample> samples;std::vector<uint8_t> rows;std::mutex mutex;
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*out=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs;}
    STDMETHODIMP_(ULONG) Release()override{const auto n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP SampleCB(double,IMediaSample* sample)override{
        const uint64_t arrival=video_queue_now_100ns();
        BYTE* pixels=nullptr;REFERENCE_TIME at=0,end=0;
        if(FAILED(sample->GetPointer(&pixels)))return E_FAIL;
        sample->GetTime(&at,&end);
        const long bytes=sample->GetActualDataLength();
        std::lock_guard lock(mutex);
        samples.push_back({arrival,at,bytes});
        if(bytes>=long((row+1)*width))rows.insert(rows.end(),pixels+size_t(row)*width,pixels+size_t(row+1)*width);
        else rows.insert(rows.end(),width,0);
        return S_OK;
    }
    STDMETHODIMP BufferCB(double,BYTE*,long)override{return E_NOTIMPL;}
};
static ComPtr<IBaseFilter> fromDll(const std::wstring& path,HRESULT& hr){
    HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!module){hr=HRESULT_FROM_WIN32(GetLastError());return nullptr;}
    using Get=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    const auto get=reinterpret_cast<Get>(GetProcAddress(module,"DllGetClassObject"));
    if(!get){hr=E_NOINTERFACE;return nullptr;}
    ComPtr<IClassFactory> factory;hr=get(CLSID_Veyra_VirtualVideo,IID_PPV_ARGS(&factory));if(FAILED(hr))return nullptr;
    ComPtr<IBaseFilter> filter;hr=factory->CreateInstance(nullptr,IID_PPV_ARGS(&filter));return filter;
}
static ComPtr<IBaseFilter> registered(HRESULT& hr){
    ComPtr<ICreateDevEnum> devices;ComPtr<IEnumMoniker> entries;
    hr=CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&devices));if(FAILED(hr))return nullptr;
    hr=devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&entries,0);if(hr!=S_OK)return nullptr;
    for(;;){
        ComPtr<IMoniker> moniker;if(entries->Next(1,&moniker,nullptr)!=S_OK)break;
        ComPtr<IPropertyBag> properties;if(FAILED(moniker->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&properties))))continue;
        VARIANT name;VariantInit(&name);
        const bool match=SUCCEEDED(properties->Read(L"FriendlyName",&name,nullptr))&&name.vt==VT_BSTR&&std::wstring(name.bstrVal)==VeyraCameraName;
        VariantClear(&name);
        if(match){ComPtr<IBaseFilter> camera;hr=moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&camera));return camera;}
    }
    hr=E_FAIL;return nullptr;
}
static int consume(const std::wstring& source,unsigned seconds,unsigned fps,unsigned width,unsigned height,unsigned row,const std::wstring& prefix,bool reopenPaused=false){
    std::unique_ptr<video_queue_t, decltype(&video_queue_close)> writer(nullptr, video_queue_close);
    if(reopenPaused){
        const auto tag=L"paused-reopen-"+std::to_wstring(GetCurrentProcessId());
        SetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_NAMESPACE",tag.c_str());
        writer.reset(video_queue_create(width,height,166667));
        if(!writer)return failure("fixture queue",E_FAIL);
        std::vector<uint8_t> pixels(size_t(width)*height*3/2,128);
        uint8_t* planes[]={pixels.data(),pixels.data()+size_t(width)*height};
        uint32_t pitches[]={width,width};
        if(!video_queue_write(writer.get(),planes,pitches,video_queue_now_100ns()))
            return failure("fixture frame",E_FAIL);
    }
    HRESULT hr=S_OK;
    auto camera=source==L"registered"?registered(hr):fromDll(source,hr);
    if(!camera)return failure("camera filter",hr);
    ComPtr<IAMStreamConfig> config;if(auto out=pinOf(camera.Get(),PINDIR_OUTPUT))out.As(&config);
    if(!config)return failure("stream config",E_NOINTERFACE);
    int count=0,bytes=0;hr=config->GetNumberOfCapabilities(&count,&bytes);if(FAILED(hr)||bytes<1)return failure("capabilities",hr);
    bool selected=false;std::vector<BYTE> caps(size_t(bytes),0);
    for(int i=0;i<count&&!selected;++i){
        AM_MEDIA_TYPE* type=nullptr;if(FAILED(config->GetStreamCaps(i,&type,caps.data())))continue;
        if(type->formattype==FORMAT_VideoInfo&&type->cbFormat>=sizeof(VIDEOINFOHEADER)&&type->subtype==MEDIASUBTYPE_NV12){
            auto* info=reinterpret_cast<VIDEOINFOHEADER*>(type->pbFormat);
            if(unsigned(info->bmiHeader.biWidth)==width&&unsigned(std::abs(info->bmiHeader.biHeight))==height){
                info->AvgTimePerFrame=10000000ll/fps;hr=config->SetFormat(type);selected=SUCCEEDED(hr);
            }
        }
        freeType(type);
    }
    if(!selected)return failure("NV12 format",hr);
    ComPtr<IGraphBuilder> graph;ComPtr<IMediaControl> control;ComPtr<IBaseFilter> grabFilter,sink;ComPtr<ISampleGrabber> grab;
    hr=CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&graph));if(FAILED(hr))return failure("graph",hr);
    graph.As(&control);graph->AddFilter(camera.Get(),VeyraCameraName);
    hr=CoCreateInstance(sampleGrabber,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&grabFilter));if(FAILED(hr))return failure("sample grabber",hr);
    grabFilter.As(&grab);AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;type.subtype=MEDIASUBTYPE_NV12;
    grab->SetMediaType(&type);graph->AddFilter(grabFilter.Get(),L"cadence grabber");
    hr=CoCreateInstance(nullRenderer,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&sink));if(FAILED(hr))return failure("null sink",hr);
    graph->AddFilter(sink.Get(),L"cadence sink");
    auto* receiver=new Receiver;receiver->width=width;receiver->row=std::min(row,height-1);grab->SetCallback(receiver,0);
    hr=direct(graph.Get(),camera.Get(),grabFilter.Get());if(SUCCEEDED(hr))hr=direct(graph.Get(),grabFilter.Get(),sink.Get());
    if(FAILED(hr)){grab->SetCallback(nullptr,0);receiver->Release();return failure("connection",hr);}
    hr=control->Run();if(FAILED(hr)){grab->SetCallback(nullptr,0);receiver->Release();return failure("run",hr);}
    if(reopenPaused){
        for(unsigned i=0;i<100&&!video_queue_consumer_interval(writer.get());++i)Sleep(10);
        const bool connected=video_queue_consumer_interval(writer.get())!=0;
        hr=control->Pause();
        FILTER_STATE state=State_Running;
        const bool paused=SUCCEEDED(hr)&&SUCCEEDED(camera->GetState(1000,&state))&&state==State_Paused;
        writer.reset();
        for(unsigned i=0;i<100&&!writer;++i){
            writer.reset(video_queue_create(width,height,166667));
            if(!writer)Sleep(10);
        }
        control->Stop();grab->SetCallback(nullptr,0);receiver->Release();
        if(!connected||!paused||!writer)return failure("paused graph did not release the closed producer queue",E_FAIL);
        std::puts("PASS DirectShow filter released the closed queue while paused");
        return 0;
    }
    Sleep(seconds*1000);
    control->Stop();grab->SetCallback(nullptr,0);
    {
        std::lock_guard lock(receiver->mutex);
        std::ofstream csv(std::filesystem::path(prefix+L".csv"));csv<<"arrival100ns,start100ns,bytes\n";
        for(auto& s:receiver->samples)csv<<s.arrival<<','<<s.start<<','<<s.bytes<<'\n';
        std::ofstream rows(std::filesystem::path(prefix+L".rows"),std::ios::binary);
        rows.write(reinterpret_cast<const char*>(receiver->rows.data()),std::streamsize(receiver->rows.size()));
        std::printf("CONSUMED samples=%zu fps=%u size=%ux%u row=%u\n",receiver->samples.size(),fps,width,height,receiver->row);
    }
    receiver->Release();return 0;
}
static int produce(unsigned seconds,double presented,unsigned width,unsigned height,unsigned jitterUs,double delayMin,double delayMax){
    auto* queue=video_queue_create(width,height,166667);if(!queue)return failure("queue",E_FAIL);
    std::vector<uint8_t> y(size_t(width)*height,32),uv(size_t(width)*height/2,128);
    uint8_t* planes[]={y.data(),uv.data()};uint32_t pitches[]={width,width};
    std::mt19937 random(20261117);std::uniform_real_distribution<double> jitter(-double(jitterUs),double(jitterUs)),delay(delayMin,delayMax);
    veyra::engine::CameraGrid grid;
    struct Pending{uint64_t due;int64_t stamp;uint64_t index;};std::vector<Pending> pending;
    const uint64_t start=video_queue_now_100ns()+5000000,end=start+uint64_t(seconds)*10000000;
    const double period=1e7/presented;uint64_t index=0,taken=0,written=0,lastWrite=0;
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    auto waitUntil=[&](uint64_t t){
        const uint64_t now=video_queue_now_100ns();if(t<=now)return;
        LARGE_INTEGER due;due.QuadPart=-LONGLONG(t-now);SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);WaitForSingleObject(timer,INFINITE);
    };
    auto presentAt=[&](uint64_t n){return start+uint64_t(double(n)*period+jitter(random)*10);};
    uint64_t present=presentAt(0);
    for(;;){
        const uint64_t nextWrite=pending.empty()?UINT64_MAX:pending.front().due;
        if(present>=end&&pending.empty())break;
        if(nextWrite<present||present>=end){
            waitUntil(nextWrite);
            const auto p=pending.front();pending.erase(pending.begin());
            const unsigned period2=width-64,bar=unsigned((p.index*2)%period2);
            std::fill(y.begin(),y.end(),uint8_t(32));
            for(unsigned row=0;row<height;++row)std::fill(y.begin()+size_t(row)*width+bar,y.begin()+size_t(row)*width+bar+64,uint8_t(220));
            memcpy(y.data(),&p.index,sizeof(p.index));
            if(video_queue_write(queue,planes,pitches,uint64_t(p.stamp)))++written;
            video_queue_heartbeat(queue);continue;
        }
        waitUntil(present);
        const uint64_t asked=video_queue_consumer_interval(queue);
        const int64_t stamp=grid.take(int64_t(video_queue_now_100ns()),int64_t(asked?asked:166667));
        if(stamp>=0){
            ++taken;
            // In order: a readback never lands before the previous one.
            const uint64_t due=std::max(lastWrite,video_queue_now_100ns()+uint64_t(delay(random)*10000));
            lastWrite=due;pending.push_back({due,stamp,index});
        }
        present=presentAt(++index);
    }
    std::printf("PRODUCED presented=%llu taken=%llu written=%llu fps=%.2f jitterUs=%u delayMs=%.1f..%.1f\n",
        (unsigned long long)index,(unsigned long long)taken,(unsigned long long)written,presented,jitterUs,delayMin,delayMax);
    Sleep(1500);video_queue_close(queue);if(timer)CloseHandle(timer);return 0;
}
int wmain(int argc,wchar_t** argv){
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    int result=2;
    if(argc==9&&std::wstring(argv[1])==L"consume")
        result=consume(argv[2],unsigned(_wtoi(argv[3])),unsigned(_wtoi(argv[4])),unsigned(_wtoi(argv[5])),unsigned(_wtoi(argv[6])),unsigned(_wtoi(argv[7])),argv[8]);
    else if(argc==9&&std::wstring(argv[1])==L"produce")
        result=produce(unsigned(_wtoi(argv[2])),_wtof(argv[3]),unsigned(_wtoi(argv[4])),unsigned(_wtoi(argv[5])),unsigned(_wtoi(argv[6])),_wtof(argv[7]),_wtof(argv[8]));
    else if(argc==3&&std::wstring(argv[1])==L"reopen-paused")
        result=consume(argv[2],0,60,640,360,0,L"",true);
    else std::fprintf(stderr,"usage: consume <dll|registered> <seconds> <fps> <width> <height> <row> <out-prefix> | produce <seconds> <fps> <width> <height> <jitter us> <delay ms min> <delay ms max> | reopen-paused <dll>\n");
    CoUninitialize();return result;
}
