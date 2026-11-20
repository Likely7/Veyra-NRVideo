#include "veyra/engine/VirtualCamera.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/Log.h"
#include "../virtualcam/CameraRegistration.h"
#include "shared-memory-queue.h"
#include "veyra/engine/CameraGrid.h"
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <format>
#include <mutex>
#include <thread>

namespace veyra::engine {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned count=4; // readback slots: 4K at 120 fps keeps about 33 ms of readback in flight
// The camera module's clock (QueryPerformanceCounter, 100 ns): frame stamps are compared there.
int64_t clock100ns(){return int64_t(video_queue_now_100ns());}
std::filesystem::path modules(){
    wchar_t path[32768]{};
    if(GetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_MODULE_DIR",path,32768))return path;
    GetModuleFileNameW(nullptr,path,32768);return std::filesystem::path(path).parent_path()/L"virtualcam";
}
bool machineRegistered(){
    for(bool x86:{false,true}){
        const auto path=virtualcam::registeredModule(x86,true);std::error_code ec;
        if(path.empty()||!std::filesystem::is_regular_file(path,ec))return false;
    }
    return true;
}
bool sameFile(const std::filesystem::path& a,const std::filesystem::path& b){
    std::ifstream x(a,std::ios::binary),y(b,std::ios::binary);if(!x||!y)return false;
    return std::equal(std::istreambuf_iterator<char>(x),std::istreambuf_iterator<char>(),std::istreambuf_iterator<char>(y),std::istreambuf_iterator<char>());
}
// The system-wide registration points at this package's camera modules. An older
// package's modules speak queue protocol 1 and show this producer's frames as nothing.
bool machineCurrent(){
    for(bool x86:{false,true}){
        const auto path=virtualcam::registeredModule(x86,true);
        if(path.empty()||!sameFile(path,modules()/(x86?L"veyra-virtualcam32.dll":L"veyra-virtualcam64.dll")))return false;
    }
    return true;
}
struct Surfaces {
    ComPtr<ID3D12Device> device;
    unsigned width=0,height=0,inputWidth=0,inputHeight=0,hdrMode=0;
    unsigned contentWidth=0,contentHeight=0;
    uint64_t epoch=0;
    pipeline::ComputePass convert;
    pipeline::DescriptorStager staging;
    pipeline::StateTracker states;
    ComPtr<ID3D12Resource> y,uv;
    struct Slot {
        // Free -> recording -> submitted -> worker -> free. Publication uses
        // release/acquire; the worker alone reads a submitted slot's payload.
        std::atomic<int> state{0};
        ComPtr<ID3D12Resource> yRead,uvRead;
        ComPtr<ID3D12Fence> fence;
        unsigned yPitch=0,uvPitch=0;
        uint64_t value=0;
        int64_t timestamp=0;
        uint64_t order=0; // record order: slots are published in it
    };
    std::array<Slot,count> buffers;
    bool idle() const {for(auto& b:buffers)if(b.state.load()!=0)return false;return true;}
    bool matches(const PresentCapture::Frame& frame,ID3D12Device* d,uint64_t e) const {
        return device.Get()==d&&inputWidth==frame.width&&inputHeight==frame.height&&hdrMode==frame.hdrMode&&epoch==e&&
            contentWidth==std::max(frame.width,frame.nativeWidth)&&contentHeight==std::max(frame.height,frame.nativeHeight);
    }
    bool create(ID3D12Device* d,const PresentCapture::Frame& frame,uint64_t e){
        device=d;inputWidth=frame.width;inputHeight=frame.height;
        contentWidth=std::max(frame.width,frame.nativeWidth);contentHeight=std::max(frame.height,frame.nativeHeight);
        width=(contentWidth+1)&~1u;height=(contentHeight+1)&~1u;hdrMode=frame.hdrMode;epoch=e;
        std::vector<uint8_t> shader;
        if(!convert.loadShader("VirtualCameraNv12.dxil",shader)||!convert.create(d,shader,count+2,2,2)||!staging.initialize(d,count))return false;
        y=pipeline::makeTexture(d,width,height,DXGI_FORMAT_R8_UNORM,true);
        uv=pipeline::makeTexture(d,width/2,height/2,DXGI_FORMAT_R8G8_UNORM,true);
        if(!y||!uv)return false;
        pipeline::makeUav(d,y.Get(),DXGI_FORMAT_R8_UNORM,pipeline::cpuHandleOf(convert,count));
        pipeline::makeUav(d,uv.Get(),DXGI_FORMAT_R8G8_UNORM,pipeline::cpuHandleOf(convert,count+1));
        for(auto& b:buffers){
            auto alloc=[&](unsigned rows,ComPtr<ID3D12Resource>& target,unsigned& pitch){
                pitch=(width+255)&~255u;
                D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=uint64_t(pitch)*rows;
                rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                const HRESULT hr=d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&target));
                if(FAILED(hr))log::error("virtual-camera",std::format("readback allocation {}x{} hr=0x{:X}",width,rows,unsigned(hr)));
                return SUCCEEDED(hr);
            };
            if(!alloc(height,b.yRead,b.yPitch)||!alloc(height/2,b.uvRead,b.uvPitch))return false;
        }
        return true;
    }
    void record(ID3D12GraphicsCommandList* list,const PresentCapture::Frame& frame,unsigned index){
        auto& b=buffers[index];
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=frame.format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        staging.stageSrv(frame.texture,&srv,convert.heap.Get(),index);
        D3D12_RESOURCE_BARRIER source{};source.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        source.Transition.pResource=frame.texture;source.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        source.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;source.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&source);
        states.transition(list,y.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);states.transition(list,uv.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8]={std::bit_cast<float>(width),std::bit_cast<float>(height),std::bit_cast<float>(inputWidth),std::bit_cast<float>(inputHeight),std::bit_cast<float>(hdrMode),std::bit_cast<float>(unsigned(frame.highQuality)),std::bit_cast<float>(contentWidth),std::bit_cast<float>(contentHeight)};
        convert.bind(list,constants,pipeline::gpuHandleOf(convert,index).ptr,pipeline::gpuHandleOf(convert,count).ptr);
        list->Dispatch((width+15)/16,(height+15)/16,1);
        states.uavBarrier(list,y.Get());states.uavBarrier(list,uv.Get());
        states.transition(list,y.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);states.transition(list,uv.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        std::swap(source.Transition.StateBefore,source.Transition.StateAfter);list->ResourceBarrier(1,&source);
        auto copy=[&](ID3D12Resource* from,ID3D12Resource* to,DXGI_FORMAT fmt,unsigned w,unsigned h,unsigned pitch){
            D3D12_TEXTURE_COPY_LOCATION a{},z{};a.pResource=from;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            z.pResource=to;z.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            z.PlacedFootprint.Footprint={fmt,w,h,1,pitch};list->CopyTextureRegion(&z,0,0,0,&a,nullptr);
        };
        copy(y.Get(),b.yRead.Get(),DXGI_FORMAT_R8_UNORM,width,height,b.yPitch);
        copy(uv.Get(),b.uvRead.Get(),DXGI_FORMAT_R8G8_UNORM,width/2,height/2,b.uvPitch);
    }
};
}
struct VirtualCamera::State {
    std::atomic<bool> armed{false},shutdown{false},installed{false},failed{false};
    std::atomic<bool> systemInstalled{false},installing{false};
    std::atomic<unsigned> fps{0},width{0},height{0};
    std::atomic<uint64_t> consumerInterval{0}; // fastest live consumer, from the queue
    std::atomic<int64_t> presentedInterval{0}; // CameraGrid's smoothed presentation interval
    std::atomic<bool> systemOutdated{false};
    std::atomic<uint64_t> epoch{1},frames{0},dropped{0};
    std::atomic<std::shared_ptr<Surfaces>> surfaces;
    std::mutex control,groupMutex;
    mutable std::mutex textMutex;
    std::wstring error;
    std::wstring setupError;
    HANDLE setupProcess=nullptr;
    bool setupRemoves=false;
    HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr),done=CreateEventW(nullptr,TRUE,FALSE,nullptr),ownership=nullptr;
    HANDLE gpuDone=CreateEventW(nullptr,FALSE,FALSE,nullptr); // set by the GPU fence when a readback lands
    std::shared_ptr<Surfaces> pending;
    int pendingIndex=-1;
    D3D12_RESOURCE_STATES pendingY=D3D12_RESOURCE_STATE_COMMON,pendingUV=D3D12_RESOURCE_STATE_COMMON;
    CameraGrid grid;uint64_t nextOrder=0,gridEpoch=0; // Engine thread only.
    // Every 10 s the presented rhythm the camera saw, for diagnosis (engine thread only).
    int64_t cadenceStart=0,cadencePrev=0;uint64_t cadencePresented=0,cadenceTaken=0;double cadenceDevSq=0,cadenceDevMax=0;
    void noteCadence(int64_t now,bool taken,int64_t interval){
        if(!cadenceStart)cadenceStart=now;
        if(cadencePrev&&grid.period){const double d=double(now-cadencePrev-grid.period)/1e4;cadenceDevSq+=d*d;cadenceDevMax=std::max(cadenceDevMax,std::abs(d));}
        cadencePrev=now;++cadencePresented;cadenceTaken+=taken;
        if(now-cadenceStart<100000000)return;
        const double seconds=double(now-cadenceStart)/1e7;const auto asked=consumerInterval.load();
        log::info("virtual-camera",std::format("cadence presented={:.1f}fps taken={:.1f}fps cameraInterval={:.2f}ms consumerFps={} presentJitterRms={:.2f}ms max={:.2f}ms frames={} dropped={}",
            double(cadencePresented)/seconds,double(cadenceTaken)/seconds,double(interval)/1e4,asked?(10000000+asked/2)/asked:0,
            cadencePresented>1?std::sqrt(cadenceDevSq/double(cadencePresented-1)):0.0,cadenceDevMax,frames.load(),dropped.load()));
        cadenceStart=now;cadencePresented=cadenceTaken=0;cadenceDevSq=cadenceDevMax=0;
    }
    ~State(){if(wake)CloseHandle(wake);if(done)CloseHandle(done);if(gpuDone)CloseHandle(gpuDone);if(ownership)CloseHandle(ownership);if(setupProcess)CloseHandle(setupProcess);}
    void pollSetupLocked(){
        if(!setupProcess||WaitForSingleObject(setupProcess,0)!=WAIT_OBJECT_0)return;
        DWORD code=ERROR_GEN_FAILURE;GetExitCodeProcess(setupProcess,&code);CloseHandle(setupProcess);setupProcess=nullptr;installing=false;
        systemInstalled=machineRegistered();systemOutdated=systemInstalled.load()&&!machineCurrent();
        if(!code&&systemInstalled.load()!=setupRemoves){
            const auto a=virtualcam::unregisterModule(false),b=virtualcam::unregisterModule(true);
            if(FAILED(a)||FAILED(b))code=DWORD(FAILED(a)?a:b);
        }else if(!code)code=ERROR_INVALID_DATA;
        installed=systemInstalled.load()||!virtualcam::registeredModule(false).empty();
        {std::lock_guard lock(textMutex);setupError=code?L"摄像机组件安装或卸载失败，请查看日志":L"";}
        if(code)log::error("virtual-camera",std::format("setup completed remove={} code=0x{:08X} machineRegistered={}",setupRemoves,code,systemInstalled.load()));
        else log::info("virtual-camera",std::format("setup completed remove={} machineRegistered={}",setupRemoves,systemInstalled.load()));
    }
    bool setup(bool remove,std::wstring& why){
        std::lock_guard lock(control);pollSetupLocked();if(setupProcess){why=L"正在安装摄像机组件，请等待";return false;}
        const auto file=modules()/L"veyra-camera-setup.exe";std::error_code ec;
        if(!std::filesystem::is_regular_file(file,ec)){why=L"虚拟摄像机组件不完整，请完整解压Veyra软件包";return false;}
        SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
        launch.lpVerb=L"runas";launch.lpFile=file.c_str();launch.lpParameters=remove?L"--uninstall":L"--install";launch.nShow=SW_HIDE;
        if(!ShellExecuteExW(&launch)){
            const auto code=GetLastError();why=code==ERROR_CANCELLED?L"已取消摄像机组件的管理员授权":L"无法启动摄像机安装程序，请查看日志";
            log::warn("virtual-camera",std::format("setup launch remove={} win32={}",remove,code));return false;
        }
        setupProcess=launch.hProcess;setupRemoves=remove;installing=true;
        {std::lock_guard text(textMutex);setupError.clear();}
        log::info("virtual-camera",std::format("setup requested remove={} elevation=Windows-consent",remove));return true;
    }
    void fail(std::wstring why){
        const int length=WideCharToMultiByte(CP_UTF8,0,why.data(),int(why.size()),nullptr,0,nullptr,nullptr);
        std::string detail(size_t(std::max(0,length)),'\0');
        if(length)WideCharToMultiByte(CP_UTF8,0,why.data(),int(why.size()),detail.data(),length,nullptr,nullptr);
        {std::lock_guard lock(textMutex);error=std::move(why);}
        if(!failed.exchange(true))log::error("virtual-camera",detail);
        armed=false;SetEvent(wake);
    }
    void run(){
        video_queue_t* queue=nullptr;
        uint64_t queueEpoch=0;unsigned queueW=0,queueH=0,queueFps=0;
        bool createFailureLogged=false;
        while(true){
            const auto generation=epoch.load();
            if(queue&&(!armed.load()||generation!=queueEpoch)){
                video_queue_close(queue);queue=nullptr;width=height=0;
            }
            if(queue)video_queue_heartbeat(queue);
            auto group=surfaces.load();bool pendingGpu=false;
            if(queue)consumerInterval=video_queue_consumer_interval(queue);
            if(group){
                // In record order: two finished slots published newest-first left the older
                // picture last in the queue, a step back in time at the consumer.
                for(;;){
                    Surfaces::Slot* pick=nullptr;
                    for(auto& slot:group->buffers){
                        const int stage=slot.state.load();
                        if(stage==1)pendingGpu=true;
                        if(stage==2&&(!pick||slot.order<pick->order))pick=&slot;
                    }
                    if(!pick)break;
                    auto& b=*pick;
                    const auto completed=b.fence->GetCompletedValue();
                    if(completed<b.value){pendingGpu=true;break;}
                    const bool deviceLost=completed==UINT64_MAX;
                    if(deviceLost){fail(L"虚拟摄像机：显卡已断开，请重新打开播放内容");b.state=0;continue;}
                    b.state=3;
                    if(armed.load()&&group->epoch==generation){
                        const unsigned rate=fps.load()?fps.load():60; // the format offered before a consumer asks
                        if(queue&&(queueW!=group->width||queueH!=group->height||queueFps!=rate)){
                            video_queue_close(queue);queue=nullptr;
                        }
                        if(!queue){
                            queue=video_queue_create(group->width,group->height,10000000ull/rate);
                            if(queue){
                                queueW=group->width;queueH=group->height;queueFps=rate;queueEpoch=generation;
                                width=queueW;height=queueH;createFailureLogged=false;
                                log::info("virtual-camera",std::format("NV12 BT709 limited {}x{} processing={}x{} offeredFps={} cap={} asyncReadback=1 slots={} queue=protocol2",queueW,queueH,group->inputWidth,group->inputHeight,queueFps,fps.load(),count));
                            }else if(!createFailureLogged){
                                createFailureLogged=true;
                                log::warn("virtual-camera",std::format("queue {}x{} fps={} not created: a consumer still maps the previous queue (paused or not responding); frames are dropped until it lets go",group->width,group->height,rate));
                            }
                        }
                        if(queue){
                            void* y=nullptr;void* uv=nullptr;
                            const D3D12_RANGE yr{0,size_t(b.yPitch)*group->height},ur{0,size_t(b.uvPitch)*group->height/2};
                            const HRESULT a=b.yRead->Map(0,&yr,&y);
                            const HRESULT z=SUCCEEDED(a)?b.uvRead->Map(0,&ur,&uv):E_FAIL;
                            if(SUCCEEDED(a)&&SUCCEEDED(z)){
                                uint8_t* data[2]={static_cast<uint8_t*>(y),static_cast<uint8_t*>(uv)};
                                uint32_t pitches[2]={b.yPitch,b.uvPitch};
                                // Recheck after mapping: a UI stop/source switch must not publish old content.
                                if(armed.load()&&epoch.load()==generation&&video_queue_write(queue,data,pitches,uint64_t(b.timestamp)))++frames;
                                else ++dropped;
                            }else{
                                log::error("virtual-camera",std::format("readback Map hrY=0x{:X} hrUV=0x{:X}",unsigned(a),unsigned(z)));
                                fail(L"虚拟摄像机：无法读取输出画面");
                            }
                            const D3D12_RANGE none{0,0};if(SUCCEEDED(z))b.uvRead->Unmap(0,&none);if(SUCCEEDED(a))b.yRead->Unmap(0,&none);
                        }else ++dropped; // A consumer still maps the previous queue; retry each frame without blocking playback.
                    }
                    b.fence.Reset();b.state=0;
                }
            }
            if(group&&(!armed.load()||group->epoch!=epoch.load())&&group->idle()){
                std::unique_lock lock(groupMutex,std::try_to_lock);
                if(lock&&surfaces.load()==group&&group->idle())surfaces.store(nullptr);
            }
            if(shutdown.load()&&(!group||group->idle()))break;
            // The GPU fence sets gpuDone when a readback lands; a 2 ms poll here could take 15.6 ms
            // (this process keeps the default timer resolution) and fell behind above 60 fps.
            HANDLE events[2]={wake,gpuDone};WaitForMultipleObjects(2,events,FALSE,pendingGpu?50:100);
        }
        video_queue_close(queue);SetEvent(done);
    }
};
struct VirtualCamera::Worker {std::thread thread;};
VirtualCamera::VirtualCamera():state_(std::make_shared<State>()),worker_(std::make_unique<Worker>()){
    state_->systemInstalled=machineRegistered();
    state_->systemOutdated=state_->systemInstalled.load()&&!machineCurrent();
    state_->installed=state_->systemInstalled.load()||!virtualcam::registeredModule(false).empty();
}
VirtualCamera::~VirtualCamera(){
    stop();state_->shutdown=true;SetEvent(state_->wake);
    if(worker_->thread.joinable()){
        if(WaitForSingleObject(state_->done,8000)==WAIT_OBJECT_0)worker_->thread.join();
        else{worker_->thread.detach();log::error("virtual-camera","GPU still pending at shutdown; resources retained by worker");}
    }
}
bool VirtualCamera::start(uint32_t rate,std::wstring& error){
    auto& s=*state_;std::lock_guard lock(s.control);
    if(rate!=0&&rate!=30&&rate!=60&&rate!=120)rate=0; // 0: follow the consumer
    if(s.armed.load()){
        if(s.fps.exchange(rate)!=rate){++s.epoch;SetEvent(s.wake);}return true;
    }
    if(s.ownership){CloseHandle(s.ownership);s.ownership=nullptr;}
    const auto directory=modules();
    const auto x64=directory/L"veyra-virtualcam64.dll",x86=directory/L"veyra-virtualcam32.dll";
    std::error_code ec;
    if(!std::filesystem::is_regular_file(x64,ec)||!std::filesystem::is_regular_file(x86,ec)){
        error=L"虚拟摄像机组件不完整，请完整解压Veyra软件包";s.fail(error);return false;
    }
    SetLastError(0);HANDLE owner=CreateMutexW(nullptr,FALSE,L"Local\\VeyraVirtualCameraProducerV1");
    const DWORD last=GetLastError();
    if(!owner||last==ERROR_ALREADY_EXISTS){
        if(owner)CloseHandle(owner);error=L"另一个Veyra实例已启用虚拟摄像机";s.fail(error);return false;
    }
    s.pollSetupLocked();s.systemInstalled=machineRegistered();
    s.systemOutdated=s.systemInstalled.load()&&!machineCurrent();
    HRESULT hr=S_OK;
    // An older system component: this package's modules for this user as well, which a
    // consumer that is not run as administrator prefers; the settings page offers the update.
    if(s.systemInstalled.load()&&!s.systemOutdated.load()){
        hr=virtualcam::unregisterModule(false);
        if(SUCCEEDED(hr))hr=virtualcam::unregisterModule(true);
    }else{
        hr=virtualcam::registerModule(x64.c_str(),false);
        if(SUCCEEDED(hr))hr=virtualcam::registerModule(x86.c_str(),true);
    }
    if(FAILED(hr)){
        CloseHandle(owner);error=L"虚拟摄像机注册失败，请查看日志";s.fail(error);
        log::error("virtual-camera",std::format("current-user registration hr=0x{:X}",unsigned(hr)));return false;
    }
    if(!s.wake||!s.done){CloseHandle(owner);error=L"虚拟摄像机工作线程初始化失败";s.fail(error);return false;}
    if(!worker_->thread.joinable()){
        try{worker_->thread=std::thread([state=state_]{state->run();});}
        catch(const std::exception&){CloseHandle(owner);error=L"虚拟摄像机工作线程初始化失败";s.fail(error);return false;}
    }
    {std::lock_guard text(s.textMutex);s.error.clear();}
    s.ownership=owner;s.installed=true;s.failed=false;s.fps=rate;s.frames=s.dropped=0;s.consumerInterval=0;++s.epoch;s.armed=true;
    SetEvent(s.wake);log::info("virtual-camera",std::format("enabled fpsCap={} registration={} videoOnly=1",rate,
        s.systemInstalled.load()?(s.systemOutdated.load()?"machine-outdated+current-user":"machine"):"current-user"));return true;
}
void VirtualCamera::stop(){
    auto& s=*state_;std::lock_guard lock(s.control);
    if(s.armed.exchange(false)){++s.epoch;log::info("virtual-camera","disabled; old picture invalidated");}
    if(s.ownership){CloseHandle(s.ownership);s.ownership=nullptr;}SetEvent(s.wake);
}
bool VirtualCamera::uninstall(std::wstring& error){
    stop();
    if(machineRegistered())return state_->setup(true,error);
    const auto a=virtualcam::unregisterModule(false),b=virtualcam::unregisterModule(true);
    if(FAILED(a)||FAILED(b)){
        log::error("virtual-camera",std::format("unregister HRESULT64=0x{:X} HRESULT32=0x{:X}",unsigned(a),unsigned(b)));
        error=L"虚拟摄像机卸载失败，请查看日志";return false;
    }
    state_->installed=false;state_->failed=false;
    {std::lock_guard lock(state_->textMutex);state_->error.clear();}return true;
}
bool VirtualCamera::installForAllUsers(std::wstring& error){return state_->setup(false,error);}
void VirtualCamera::sessionEnded(){++state_->epoch;state_->width=state_->height=0;SetEvent(state_->wake);}
VirtualCameraStatus VirtualCamera::status()const{
    auto& s=*state_;std::lock_guard control(s.control);s.pollSetupLocked();VirtualCameraStatus out;
    out.active=s.armed;out.installed=s.installed;out.failed=s.failed;out.width=s.width;out.height=s.height;
    out.systemInstalled=s.systemInstalled;out.installing=s.installing;out.systemOutdated=s.systemOutdated;
    out.fpsLimit=s.fps;out.frames=s.frames;out.dropped=s.dropped;
    if(const auto asked=s.consumerInterval.load())out.consumerFps=uint32_t((10000000+asked/2)/asked);
    if(const auto shown=s.presentedInterval.load();shown>0&&s.width.load())out.presentedFps=uint32_t((10000000+shown/2)/shown);
    if(const auto g=s.surfaces.load()){out.processingWidth=g->inputWidth;out.processingHeight=g->inputHeight;}
    {std::lock_guard lock(s.textMutex);out.error=s.error;out.setupError=s.setupError;}return out;
}
bool VirtualCamera::needsFrame()const{return state_->armed.load()&&state_->width.load()==0;}
bool VirtualCamera::record(ID3D12GraphicsCommandList* list,const Frame& frame){
    auto& s=*state_;if(!s.armed.load()||!list||!frame.texture||s.pending)return false;
    // The rate the consumer negotiated (its camera format), within the user's cap; 60 until
    // one says. The frame nearest each grid point goes, stamped with the point (CameraGrid.h):
    // even stamps whatever the frame generation multiple and the presentation jitter.
    const unsigned cap=s.fps.load();const uint64_t asked=s.consumerInterval.load();
    int64_t interval=asked?int64_t(asked):10000000/(cap?cap:60);
    if(cap)interval=std::max<int64_t>(interval,10000000/cap);
    if(const auto epoch=s.epoch.load();s.gridEpoch!=epoch){s.grid.reset();s.gridEpoch=epoch;s.cadenceStart=s.cadencePrev=0;}
    const int64_t now=clock100ns(),stamp=s.grid.take(now,interval);
    s.presentedInterval=s.grid.period;
    s.noteCadence(now,stamp>=0,interval);
    // Test only: every frame the camera saw (VEYRA_TEST_VIRTUALCAM_TRACE=<csv file>).
    static FILE* trace=[]{wchar_t path[1024]{};FILE* f=nullptr;
        if(GetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_TRACE",path,1024)&&_wfopen_s(&f,path,L"w")==0&&f)std::fputs("now100ns,stamp100ns,interval100ns,generated\n",f);
        return f;}();
    if(trace)std::fprintf(trace,"%lld,%lld,%lld,%d\n",(long long)now,(long long)stamp,(long long)interval,int(frame.generated));
    if(stamp<0)return false;
    std::unique_lock lock(s.groupMutex,std::try_to_lock);
    if(!lock){++s.dropped;return false;}
    const unsigned targetW=std::max(frame.width,frame.nativeWidth),targetH=std::max(frame.height,frame.nativeHeight);
    if(frame.width<1||frame.height<1||targetW>8192||targetH>8192||uint64_t((targetW+1)&~1u)*((targetH+1)&~1u)>33554432u){
        s.fail(L"虚拟摄像机输出尺寸超出范围（最大8192边长、约3355万像素）");return false;
    }
    ComPtr<ID3D12Device> device;const HRESULT hr=frame.texture->GetDevice(IID_PPV_ARGS(&device));
    if(FAILED(hr)){log::error("virtual-camera",std::format("GetDevice hr=0x{:X}",unsigned(hr)));s.fail(L"虚拟摄像机无法访问显卡");return false;}
    auto group=s.surfaces.load();const auto generation=s.epoch.load();
    if(!group||!group->matches(frame,device.Get(),generation)){
        if(group&&!group->idle()){++s.dropped;return false;}
        auto next=std::make_shared<Surfaces>();
        if(!next->create(device.Get(),frame,generation)){s.fail(L"虚拟摄像机GPU输出初始化失败");return false;}
        s.surfaces.store(next);group=std::move(next);
    }
    unsigned index=count;
    for(unsigned i=0;i<count;++i){int free=0;if(group->buffers[i].state.compare_exchange_strong(free,1)){index=i;break;}}
    if(index==count){++s.dropped;return false;}
    s.pendingY=group->states.get(group->y.Get());s.pendingUV=group->states.get(group->uv.Get());
    group->record(list,frame,index);group->buffers[index].timestamp=stamp;group->buffers[index].order=++s.nextOrder;
    s.pending=group;s.pendingIndex=int(index);
    return true;
}
void VirtualCamera::submitted(ID3D12Fence* fence,uint64_t value){
    auto& s=*state_;if(!s.pending)return;
    if(!fence||!value){discarded();s.fail(L"虚拟摄像机GPU提交栅栏无效");return;}
    auto& b=s.pending->buffers[size_t(s.pendingIndex)];b.fence=fence;b.value=value;b.state=2;
    if(s.gpuDone)fence->SetEventOnCompletion(value,s.gpuDone); // wakes the worker when the readback lands
    s.pending.reset();s.pendingIndex=-1;SetEvent(s.wake);
}
void VirtualCamera::discarded(){
    auto& s=*state_;if(!s.pending)return;
    s.pending->states.set(s.pending->y.Get(),s.pendingY);s.pending->states.set(s.pending->uv.Get(),s.pendingUV);
    s.pending->buffers[size_t(s.pendingIndex)].state=0;s.pending.reset();s.pendingIndex=-1;SetEvent(s.wake);
}
}
