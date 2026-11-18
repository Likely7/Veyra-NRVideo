// Assemble the product's camera transport/registration with a real DirectShow
// consumer. Pattern generation here is a fixture, never a second output path.
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include "CameraRegistration.h"
#include "virtualcam-guid.h"
#include "shared-memory-queue.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
// Same standard interfaces used by CaptureCardSource.cpp; qedit.h was removed
// from recent SDKs, while the system Sample Grabber COM server remains usable.
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
static HRESULT direct(IGraphBuilder* graph,IBaseFilter* from,IBaseFilter* to){
    auto pin=[](IBaseFilter* filter,PIN_DIRECTION wanted){ComPtr<IEnumPins> all;ComPtr<IPin> found;filter->EnumPins(&all);
        for(;;){ComPtr<IPin> item;if(all->Next(1,&item,nullptr)!=S_OK)break;PIN_DIRECTION direction;
            if(SUCCEEDED(item->QueryDirection(&direction))&&direction==wanted){found=item;break;}}return found;};
    auto out=pin(from,PINDIR_OUTPUT),in=pin(to,PINDIR_INPUT);
    return out&&in?graph->ConnectDirect(out.Get(),in.Get(),nullptr):E_NOINTERFACE;
}
struct Receiver final:ISampleGrabberCB {
    std::atomic<ULONG> refs{1};
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    unsigned wanted=0,frames=0,nonMonotonic=0,delayMs=0;
    REFERENCE_TIME last=-1,firstTime=0;
    std::vector<uint8_t> first,lastFrame;
    std::mutex mutex;
    explicit Receiver(unsigned n):wanted(n){}
    ~Receiver(){CloseHandle(done);}
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==__uuidof(ISampleGrabberCB)){*out=static_cast<ISampleGrabberCB*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs;}
    STDMETHODIMP_(ULONG) Release()override{const auto n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP SampleCB(double,IMediaSample* sample)override{
        std::lock_guard lock(mutex);
        BYTE* pixels=nullptr;REFERENCE_TIME at=0,end=0;
        if(FAILED(sample->GetPointer(&pixels))||FAILED(sample->GetTime(&at,&end)))return E_FAIL;
        if(last>=0&&at<=last)++nonMonotonic;last=at;
        if(first.empty()){first.assign(pixels,pixels+sample->GetActualDataLength());firstTime=at;}
        ++frames;
        if(delayMs&&frames<=10)std::printf("sample=%u start=%lld end=%lld wall=%llu\n",frames,at,end,GetTickCount64());
        if(frames>=wanted){lastFrame.assign(pixels,pixels+sample->GetActualDataLength());SetEvent(done);}
        if(delayMs)Sleep(delayMs);return S_OK;
    }
    STDMETHODIMP BufferCB(double,BYTE*,long)override{return E_NOTIMPL;}
};
static int capture(unsigned width,unsigned height,unsigned fps,unsigned frames,const wchar_t* output,unsigned delayMs=0){
    ComPtr<ICreateDevEnum> devices;ComPtr<IEnumMoniker> entries;
    HRESULT hr=CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&devices));
    if(FAILED(hr))return failure("device enumerator",hr);
    hr=devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&entries,0);
    if(hr!=S_OK)return failure("camera category",hr);
    ComPtr<IBaseFilter> camera;
    while(true){
        ComPtr<IMoniker> moniker;if(entries->Next(1,&moniker,nullptr)!=S_OK)break;
        ComPtr<IPropertyBag> properties;
        if(FAILED(moniker->BindToStorage(nullptr,nullptr,IID_PPV_ARGS(&properties))))continue;
        VARIANT name;VariantInit(&name);
        const bool match=SUCCEEDED(properties->Read(L"FriendlyName",&name,nullptr))&&name.vt==VT_BSTR&&std::wstring(name.bstrVal)==VeyraCameraName;
        VariantClear(&name);
        if(match){hr=moniker->BindToObject(nullptr,nullptr,IID_PPV_ARGS(&camera));break;}
    }
    if(!camera)return failure("Veyra camera binding",hr);
    ComPtr<IEnumPins> pins;camera->EnumPins(&pins);ComPtr<IAMStreamConfig> config;
    while(true){ComPtr<IPin> pin;if(pins->Next(1,&pin,nullptr)!=S_OK)break;PIN_DIRECTION direction;
        if(SUCCEEDED(pin->QueryDirection(&direction))&&direction==PINDIR_OUTPUT&&SUCCEEDED(pin.As(&config)))break;}
    if(!config)return failure("stream config",E_NOINTERFACE);
    int count=0,bytes=0;hr=config->GetNumberOfCapabilities(&count,&bytes);
    if(FAILED(hr)||bytes<1)return failure("capabilities",hr);
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
    if(!selected)return failure("requested native format",hr);
    ComPtr<IGraphBuilder> graph;ComPtr<ICaptureGraphBuilder2> builder;ComPtr<IMediaControl> control;
    ComPtr<IBaseFilter> grabFilter,sink;ComPtr<ISampleGrabber> grab;
    hr=CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&graph));if(FAILED(hr))return failure("graph",hr);
    graph.As(&control);
    hr=CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&builder));if(FAILED(hr))return failure("capture graph",hr);
    builder->SetFiltergraph(graph.Get());graph->AddFilter(camera.Get(),VeyraCameraName);
    hr=CoCreateInstance(sampleGrabber,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&grabFilter));if(FAILED(hr))return failure("sample grabber",hr);
    grabFilter.As(&grab);AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;type.subtype=MEDIASUBTYPE_NV12;
    grab->SetMediaType(&type);graph->AddFilter(grabFilter.Get(),L"Veyra QA sample grabber");
    hr=CoCreateInstance(nullRenderer,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&sink));if(FAILED(hr))return failure("null sink",hr);
    graph->AddFilter(sink.Get(),L"Veyra QA sink");
    auto* receiver=new Receiver(frames);receiver->delayMs=delayMs;grab->SetCallback(receiver,0);
    // A direct graph isolates source backpressure. RenderStream inserts a Smart
    // Tee whose separate capture FIFO hides a slow receiver for several frames.
    hr=direct(graph.Get(),camera.Get(),grabFilter.Get());
    if(SUCCEEDED(hr))hr=direct(graph.Get(),grabFilter.Get(),sink.Get());
    if(FAILED(hr)){grab->SetCallback(nullptr,0);receiver->Release();return failure("real camera connection",hr);}
    if(delayMs){ComPtr<IEnumFilters> filters;graph->EnumFilters(&filters);for(;;){ComPtr<IBaseFilter> filter;if(filters->Next(1,&filter,nullptr)!=S_OK)break;FILTER_INFO info{};filter->QueryFilterInfo(&info);std::wprintf(L"filter=%ls\n",info.achName);if(info.pGraph)info.pGraph->Release();}}
    const auto started=std::chrono::steady_clock::now();
    hr=control->Run();if(FAILED(hr)){grab->SetCallback(nullptr,0);receiver->Release();return failure("real camera start",hr);}
    const DWORD wait=WaitForSingleObject(receiver->done,15000);
    const HRESULT stopped=control->Stop();grab->SetCallback(nullptr,0);
    int result=0;
    {
        std::lock_guard lock(receiver->mutex);
        if(wait!=WAIT_OBJECT_0||FAILED(stopped)||receiver->nonMonotonic||receiver->first.size()!=size_t(width)*height*3/2)result=2;
        if(!receiver->first.empty()){
            std::ofstream out(std::filesystem::path(output),std::ios::binary);out.write(reinterpret_cast<const char*>(receiver->first.data()),std::streamsize(receiver->first.size()));
        }
        if(!receiver->lastFrame.empty()){
            std::ofstream out(std::filesystem::path(std::wstring(output)+L".last.nv12"),std::ios::binary);out.write(reinterpret_cast<const char*>(receiver->lastFrame.data()),std::streamsize(receiver->lastFrame.size()));
        }
        std::printf("time100ns=%lld wallMs=%lld\n",receiver->last-receiver->firstTime,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
        std::printf("{\"bits\":%zu,\"width\":%u,\"height\":%u,\"frames\":%u,\"bytes\":%zu,\"nonMonotonic\":%u,\"stopHr\":%lu,\"ok\":%s}\n",
            sizeof(void*)*8,width,height,receiver->frames,receiver->first.size(),receiver->nonMonotonic,static_cast<unsigned long>(stopped),result?"false":"true");
    }
    receiver->Release();return result;
}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc==3&&std::wstring(argv[1])==L"--install"){
        const HRESULT hr=veyra::virtualcam::registerModule(argv[2],sizeof(void*)==4);
        std::printf("registration hr=0x%08X bits=%zu\n",unsigned(hr),sizeof(void*)*8);return FAILED(hr)?2:0;
    }
    if(argc==2&&std::wstring(argv[1])==L"--uninstall")return FAILED(veyra::virtualcam::unregisterModule(sizeof(void*)==4))?2:0;
    if((argc==7||argc==8)&&std::wstring(argv[1])==L"--capture")return capture(unsigned(_wtoi(argv[2])),unsigned(_wtoi(argv[3])),unsigned(_wtoi(argv[4])),unsigned(_wtoi(argv[5])),argv[6],argc==8?unsigned(_wtoi(argv[7])):0);
    if(argc>=6&&std::wstring(argv[1])==L"--serve"){
        const unsigned width=unsigned(_wtoi(argv[2])),height=unsigned(_wtoi(argv[3])),fps=unsigned(_wtoi(argv[4])),seconds=unsigned(_wtoi(argv[5]));
        if(!fps||fps>240||!seconds||seconds>250)return 2;
        auto* queue=video_queue_create(width,height,10000000ull/fps);if(!queue)return failure("fixture queue",E_FAIL);
        std::vector<uint8_t> pixels(size_t(width)*height*3/2,128);uint8_t* data[2]={pixels.data(),pixels.data()+size_t(width)*height};uint32_t pitch[2]={width,width};
        const auto begin=std::chrono::steady_clock::now();uint64_t written=0,dropped=0;
        for(unsigned f=0;f<seconds*fps;++f){
            memset(pixels.data(),40+(f%180),size_t(width)*height);
            for(size_t i=size_t(width)*height;i<pixels.size();i+=2){pixels[i]=64;pixels[i+1]=192;}
            if(video_queue_write(queue,data,pitch,video_queue_now_100ns()))++written;else ++dropped; // protocol 2: the camera clock
            if(f==0){std::puts("READY");std::fflush(stdout);}
            std::this_thread::sleep_until(begin+std::chrono::nanoseconds((uint64_t(f)+1)*1000000000ull/fps));
        }
        video_queue_close(queue);std::printf("fixture frames=%llu dropped=%llu\n",written,dropped);return 0;
    }
    std::fprintf(stderr,"--install DLL | --uninstall | --serve W H FPS SECONDS | --capture W H FPS FRAMES OUTPUT\n");return 2;
}
