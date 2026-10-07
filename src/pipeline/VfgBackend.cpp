#include "veyra/pipeline/VfgBackend.h"
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>
#include <vector>
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
#include <cuda.h>
#endif

namespace veyra::pipeline {
namespace {
namespace fs=std::filesystem;
constexpr unsigned kOutputs=2*(FrameBatch::Capacity-1);
constexpr DWORD kDllFlags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS;
bool regularFile(const fs::path& path){std::error_code error;return fs::is_regular_file(path,error);}
HMODULE load(const fs::path& path){
    const auto dll=LoadLibraryExW(path.c_str(),nullptr,kDllFlags);
    if(!dll){const auto error=GetLastError();const auto bytes=path.u8string();
        log::error("vfg",std::format("LoadLibrary failed path={} win32={}",std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()),error));}
    return dll;
}
template<class T> bool bind(HMODULE dll,const char* name,T& fn){
    fn=reinterpret_cast<T>(GetProcAddress(dll,name));
    if(!fn)log::error("vfg",std::format("missing API {} win32={}",name,GetLastError()));
    return fn!=nullptr;
}
bool hr(HRESULT value,const char* operation){
    if(SUCCEEDED(value))return true;
    log::error("vfg",std::format("{} HRESULT=0x{:08X}",operation,unsigned(value)));return false;
}
}

std::wstring VfgBackend::runtimeDirectory(const std::wstring& root){
    std::array<wchar_t,32768> overridePath{};
    const auto size=GetEnvironmentVariableW(L"VEYRA_VFG_RUNTIME",overridePath.data(),DWORD(overridePath.size()));
    if(size){
        if(size>=overridePath.size()||!fs::path(overridePath.data()).is_absolute())return {};
        return fs::path(overridePath.data()).lexically_normal().wstring();
    }
    if(!fs::path(root).is_absolute())return {};
    const std::array<fs::path,3> choices={fs::path(root)/L"vfg",fs::path(root).parent_path()/L"nvidia-vfg",fs::path(root)};
    for(const auto& directory:choices)if(regularFile(directory/L"NVVideoEffects.dll"))return directory.lexically_normal().wstring();
    return choices[0].lexically_normal().wstring();
}
bool VfgBackend::runtimeAvailable(const std::wstring& root){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    const auto directory=runtimeDirectory(root);
    return !directory.empty()&&regularFile(fs::path(directory)/L"NVVideoEffects.dll")&&
        regularFile(fs::path(directory)/L"NVCVImage.dll")&&
        regularFile(fs::path(directory)/L"nvVFXVideoFrameGeneration.dll");
#else
    return false;
#endif
}

struct VfgBackend::Impl {
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    // Only function signatures from the public C API. NVCV owns its opaque
    // image descriptors; format conversion and the tested RGB10A2/P32 mapping
    // do not duplicate proprietary image struct definitions.
    int (__cdecl* create)(const char*,void**)=nullptr;
    void (__cdecl* destroy)(void*)=nullptr;
    int (__cdecl* setU32)(void*,const char*,unsigned)=nullptr;
    int (__cdecl* setImage)(void*,const char*,void*)=nullptr;
    int (__cdecl* setStream)(void*,const char*,CUstream)=nullptr;
    int (__cdecl* loadEffect)(void*)=nullptr;
    int (__cdecl* run)(void*,int)=nullptr;
    int (__cdecl* getVersion)(unsigned*)=nullptr;
    int (__cdecl* imageCreate)(unsigned,unsigned,int,int,unsigned,unsigned,unsigned,void**)=nullptr;
    void (__cdecl* imageDealloc)(void*)=nullptr;
    void (__cdecl* imageDestroy)(void*)=nullptr;
    int (__cdecl* imageInit)(void*,unsigned,unsigned,int,void*,int,int,unsigned,unsigned)=nullptr;
    int (__cdecl* fromD3D)(DXGI_FORMAT,int*,int*,unsigned char*)=nullptr;
    const char* (__cdecl* errorString)(int)=nullptr;
#define CU_FN(name) decltype(&name) name##Fn=nullptr
    CU_FN(cuInit);CU_FN(cuDeviceGetCount);CU_FN(cuDeviceGet);CU_FN(cuDeviceGetLuid);CU_FN(cuDeviceGetAttribute);
    CU_FN(cuDevicePrimaryCtxRetain);CU_FN(cuDevicePrimaryCtxRelease);CU_FN(cuCtxPushCurrent);CU_FN(cuCtxPopCurrent);
    CU_FN(cuStreamCreate);CU_FN(cuStreamDestroy);CU_FN(cuStreamSynchronize);
    CU_FN(cuImportExternalMemory);CU_FN(cuExternalMemoryGetMappedBuffer);CU_FN(cuDestroyExternalMemory);CU_FN(cuMemFree);
    CU_FN(cuImportExternalSemaphore);CU_FN(cuDestroyExternalSemaphore);CU_FN(cuWaitExternalSemaphoresAsync);CU_FN(cuSignalExternalSemaphoresAsync);
    CU_FN(cuGetErrorName);CU_FN(cuGetErrorString);
#undef CU_FN
    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        CUexternalMemory memory=nullptr;
        CUdeviceptr pointer=0;
        void* image=nullptr;
    };
    std::array<Buffer,2> inputs;
    std::array<Buffer,kOutputs> outputs;
    gfx::D3D12DeviceContext* context=nullptr;
    Microsoft::WRL::ComPtr<ID3D12Fence> producer,completion;
    CUexternalSemaphore inputSemaphore=nullptr,outputSemaphore=nullptr;
    CUcontext cudaContext=nullptr;CUstream stream=nullptr;CUdevice device=0;
    bool retained=false;
    void* effect=nullptr;
    std::vector<HMODULE> libraries;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t bytes=0,signalValue=0;
    unsigned width=0,height=0,maxMultiplier=2;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool ready=false;
    uint64_t inputRevision=0,boundInputRevision=0;
    unsigned boundParity=2,boundMultiplier=0;
    bool boundReset=false;
    struct Job {unsigned parity,sub,multiplier;bool reset;uint64_t revision,value;};
    std::array<std::atomic<SubmissionState>,kOutputs> states{};
    std::atomic<bool> failed=false;
    std::mutex jobsMutex,profileMutex;
    std::condition_variable jobsChanged;
    std::deque<Job> jobs;
    std::thread worker;
    bool asynchronous=false,stopping=false;
    unsigned outstanding=0;
    unsigned rejectRun=0,runCount=0; // process-local deterministic failure test
    bool profileCpu=false;
    std::array<double,13> cpuSum{},cpuMax{};
    std::array<unsigned,13> cpuSamples{};
    std::chrono::steady_clock::time_point nextCpuLog{};
    template<class F> auto cpuCall(unsigned stage,F&& operation){
        if(!profileCpu)return operation();
        const auto start=std::chrono::steady_clock::now();const auto result=operation();
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::lock_guard lock(profileMutex);
        cpuSum[stage]+=ms;cpuMax[stage]=std::max(cpuMax[stage],ms);++cpuSamples[stage];return result;
    }
    void logCpu(){
        if(!profileCpu)return;
        std::lock_guard lock(profileMutex);
        if(std::chrono::steady_clock::now()<nextCpuLog)return;
        static constexpr std::array<const char*,13> names={"push","producer","cudaWait","src0","src1","dst","shot","multiplier","index","run","cudaSignal","queueWait","pop"};
        std::string message;
        for(unsigned i=0;i<names.size();++i)message+=std::format(" {}MeanMs={:.4f} {}MaxMs={:.4f} {}Calls={}",names[i],cpuSamples[i]?cpuSum[i]/cpuSamples[i]:0,names[i],cpuMax[i],names[i],cpuSamples[i]);
        log::info("vfg-cpu",message);cpuSum={};cpuMax={};cpuSamples={};nextCpuLog=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    }
    bool cv(int code,const char* name){
        if(code==0)return true;
        log::error("vfg",std::format("{} status={} detail={}",name,code,errorString?errorString(code):"unavailable"));return false;
    }
    bool cu(CUresult code,const char* name){
        if(code==CUDA_SUCCESS)return true;
        const char* label=nullptr;const char* detail=nullptr;
        if(cuGetErrorNameFn)cuGetErrorNameFn(code,&label);
        if(cuGetErrorStringFn)cuGetErrorStringFn(code,&detail);
        log::error("vfg-cuda",std::format("{} result={} name={} detail={}",name,unsigned(code),label?label:"?",detail?detail:"?"));return false;
    }
    struct Current {
        Impl& owner;bool active=false;
        explicit Current(Impl& p):owner(p){active=p.cu(p.cpuCall(0,[&]{return p.cuCtxPushCurrentFn(p.cudaContext);}),"cuCtxPushCurrent");}
        ~Current(){if(active){CUcontext previous=nullptr;owner.cu(owner.cpuCall(12,[&]{return owner.cuCtxPopCurrentFn(&previous);}),"cuCtxPopCurrent");}}
    };
    bool importFence(ID3D12Fence* fence,CUexternalSemaphore& semaphore){
        HANDLE handle=nullptr;
        if(!hr(context->device()->CreateSharedHandle(fence,nullptr,GENERIC_ALL,nullptr,&handle),"CreateSharedHandle fence"))return false;
        CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};desc.type=CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;desc.handle.win32.handle=handle;
        const auto code=cuImportExternalSemaphoreFn(&semaphore,&desc);CloseHandle(handle);
        return cu(code,"cuImportExternalSemaphore");
    }
    bool makeBuffer(Buffer& buffer,int pixelFormat,int componentType,unsigned layout){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(!hr(context->device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&buffer.resource)),"Create shared buffer"))return false;
        HANDLE handle=nullptr;
        if(!hr(context->device()->CreateSharedHandle(buffer.resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"CreateSharedHandle buffer"))return false;
        CUDA_EXTERNAL_MEMORY_HANDLE_DESC imported{};
        imported.type=CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;imported.handle.win32.handle=handle;
        imported.size=context->device()->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;imported.flags=CUDA_EXTERNAL_MEMORY_DEDICATED;
        const auto code=cuImportExternalMemoryFn(&buffer.memory,&imported);CloseHandle(handle);
        if(!cu(code,"cuImportExternalMemory"))return false;
        CUDA_EXTERNAL_MEMORY_BUFFER_DESC mapped{};mapped.size=bytes;
        if(!cu(cuExternalMemoryGetMappedBufferFn(&buffer.pointer,buffer.memory,&mapped),"cuExternalMemoryGetMappedBuffer"))return false;
        // Allocate descriptor storage through the SDK and release its tiny owned
        // pixel allocation before binding the borrowed CUDA mapping. Init does
        // not transfer ownership of external pixels to NvCVImage_Destroy.
        if(!cv(imageCreate(1,1,pixelFormat,componentType,layout,0,1,&buffer.image),"NvCVImage_Create"))return false;
        imageDealloc(buffer.image);
        if(!cv(imageInit(buffer.image,width,height,int(footprint.Footprint.RowPitch),reinterpret_cast<void*>(buffer.pointer),pixelFormat,componentType,layout,1),"NvCVImage_Init"))return false;
        return true;
    }
    void releaseBuffer(Buffer& buffer){
        if(buffer.image){imageDestroy(buffer.image);buffer.image=nullptr;}
        if(buffer.pointer){cu(cuMemFreeFn(buffer.pointer),"cuMemFree external mapping");buffer.pointer=0;}
        if(buffer.memory){cu(cuDestroyExternalMemoryFn(buffer.memory),"cuDestroyExternalMemory");buffer.memory=nullptr;}
        buffer.resource.Reset();
    }
    bool cudaJob(const Job& job){
        const auto [parity,sub,multiplier,reset,revision,value]=job;
        Current current(*this);if(!current.active)return false;
        CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait{};wait.params.fence.value=value;
        bool ok=cu(cpuCall(2,[&]{return cuWaitExternalSemaphoresAsyncFn(&inputSemaphore,&wait,1,stream);}),"cuWaitExternalSemaphoresAsync")&&!failed.load();
        // NVIDIA MIT sample, commit 52011f89c1741d06b40ea312af1f20be8be9ec62,
        // VideoFrameGenerationEffectApp: both input images must be rebound for
        // each NEW pair, even when their pointers are unchanged. Rebinding for
        // every subframe instead starts a new pair inside the SDK. Keep the
        // same pair bound while selecting its remaining intermediate frames.
        // capture() revisions also handle callers that start at another index.
        if(ok&&(sub==1||revision!=boundInputRevision||parity!=boundParity||reset!=boundReset)){
            ok=cv(cpuCall(3,[&]{return setImage(effect,"SrcImage0",inputs[reset?parity:1-parity].image);}),"SetImage previous")&&
                cv(cpuCall(4,[&]{return setImage(effect,"SrcImage1",inputs[parity].image);}),"SetImage current")&&
                cv(cpuCall(6,[&]{return setU32(effect,"ShotChange",reset?1u:0u);}),"SetU32 ShotChange");
            if(ok){boundInputRevision=revision;boundParity=parity;boundReset=reset;}
        }
        if(ok&&boundMultiplier!=multiplier){
            ok=cv(cpuCall(7,[&]{return setU32(effect,"FrameMultiplier",multiplier);}),"SetU32 FrameMultiplier");
            if(ok)boundMultiplier=multiplier;
        }
        ok=ok&&cv(cpuCall(5,[&]{return setImage(effect,"DstImage0",outputs[parity+2*(sub-1)].image);}),"SetImage output")&&
            cv(cpuCall(8,[&]{return setU32(effect,"FrameIndex",sub);}),"SetU32 FrameIndex");
        if(ok){
            if(rejectRun&&++runCount==rejectRun){log::warn("vfg","injected VFG Run rejection (test hook)");ok=false;}
            else ok=cv(cpuCall(9,[&]{return run(effect,1);}),"NvVFX_Run async");
        }
        // Publish rejection before releasing the consumer. Its GPU copy still
        // drains, but resolveFrame must never present that slot as a success.
        if(!ok)failed.store(true);
        CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal{};signal.params.fence.value=value;
        if(!cu(cpuCall(10,[&]{return cuSignalExternalSemaphoresAsyncFn(&outputSemaphore,&signal,1,stream);}),"cuSignalExternalSemaphoresAsync")){
            failed.store(true);
            // Exceptional recovery only: drain any earlier CUDA writes before
            // releasing a prequeued D3D wait. Normal frames never synchronize.
            cu(cuStreamSynchronizeFn(stream),"failed handoff cuStreamSynchronize");
            hr(completion->Signal(value),"release rejected VFG completion");
            ok=false;
        }
        return ok;
    }
    void work(){
        for(;;){
            Job job{};
            {std::unique_lock lock(jobsMutex);jobsChanged.wait(lock,[&]{return stopping||!jobs.empty();});
                if(jobs.empty())return;job=jobs.front();jobs.pop_front();}
            bool ok=false;
            try{ok=cudaJob(job);}catch(...){log::error("vfg","SDK worker exception; disabling VFG");}
            if(!ok){
                failed.store(true);
                // A context push failure/exception must also release its wait.
                // cudaJob normally signals on the CUDA stream, so only force a
                // CPU signal after draining it in the same primary context.
                Current current(*this);
                if(current.active&&stream)cu(cuStreamSynchronizeFn(stream),"rejected job drain");
                hr(completion->Signal(job.value),"release failed VFG job");
            }
            states[job.parity+2*(job.sub-1)].store(ok?SubmissionState::Succeeded:SubmissionState::Failed);
            logCpu();
            {std::lock_guard lock(jobsMutex);--outstanding;}
        }
    }
    bool handoff(unsigned parity,unsigned sub,unsigned multiplier,bool reset){
        const unsigned slot=parity+2*(sub-1);
        if(!asynchronous){
            const uint64_t value=++signalValue;
            if(!hr(cpuCall(1,[&]{return context->directQueue()->Signal(producer.Get(),value);}),"signal VFG producer"))return false;
            const bool ok=cudaJob({parity,sub,multiplier,reset,inputRevision,value});
            if(!ok){
                failed.store(true);Current current(*this);
                if(current.active&&stream)cu(cuStreamSynchronizeFn(stream),"rejected synchronous job drain");
                hr(completion->Signal(value),"release failed synchronous VFG job");
            }
            states[slot].store(ok?SubmissionState::Succeeded:SubmissionState::Failed);
            if(!hr(cpuCall(11,[&]{return context->directQueue()->Wait(completion.Get(),value);}),"queue wait VFG completion"))return false;
            logCpu();return ok;
        }
        std::lock_guard lock(jobsMutex);
        if(stopping||failed.load()||outstanding>=kOutputs){
            log::error("vfg","bounded VFG submission rejected; no GPU wait queued");return false;
        }
        const uint64_t value=++signalValue;
        if(!hr(cpuCall(1,[&]{return context->directQueue()->Signal(producer.Get(),value);}),"signal VFG producer"))return false;
        // Allocate the queue entry before adding a GPU dependency. Even a host
        // allocation exception cannot leave a wait with no job to signal it.
        jobs.push_back({parity,sub,multiplier,reset,inputRevision,value});
        if(!hr(cpuCall(11,[&]{return context->directQueue()->Wait(completion.Get(),value);}),"queue wait VFG completion")){
            jobs.pop_back();return false;
        }
        states[slot].store(SubmissionState::Pending);++outstanding;jobsChanged.notify_one();return true;
    }
    void stopWorker(){
        {std::lock_guard lock(jobsMutex);stopping=true;}
        jobsChanged.notify_one();if(worker.joinable())worker.join();
    }
#endif
};
VfgBackend::VfgBackend():impl_(std::make_unique<Impl>()){}
VfgBackend::~VfgBackend(){shutdown();}

bool VfgBackend::initialize(gfx::D3D12DeviceContext& context,const std::wstring& root,unsigned width,unsigned height,DXGI_FORMAT format,unsigned multiplier,unsigned quality,bool asynchronousSubmission){
    shutdown();
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    auto& p=*impl_;
    if(context.adapter().vendorId!=0x10DE||!runtimeAvailable(root)||!width||!height||multiplier<2||multiplier>FrameBatch::Capacity||quality>2||
       (format!=DXGI_FORMAT_R8G8B8A8_UNORM&&format!=DXGI_FORMAT_R10G10B10A2_UNORM)){
        log::error("vfg","unsupported adapter/runtime/dimensions/multiplier/quality/encoding");return false;
    }
    p.context=&context;p.width=width;p.height=height;p.format=format;p.maxMultiplier=multiplier;
    p.profileCpu=GetEnvironmentVariableW(L"VEYRA_TEST_VFG_CPU_PROFILE",nullptr,0)>0;
    wchar_t reject[16]{};
    const auto rejectLength=GetEnvironmentVariableW(L"VEYRA_TEST_VFG_REJECT_RUN",reject,16);
    if(rejectLength&&rejectLength<16)p.rejectRun=unsigned(std::clamp(_wtoi(reject),0,1000000));
    std::array<wchar_t,32768> system{};const auto n=GetSystemDirectoryW(system.data(),UINT(system.size()));if(!n||n>=system.size())return false;
    HMODULE driver=load(fs::path(system.data())/L"nvcuda.dll");if(!driver)return false;p.libraries.push_back(driver);
#define BIND_CU(name,exported) if(!bind(driver,exported,p.name##Fn))return false
    BIND_CU(cuInit,"cuInit");BIND_CU(cuDeviceGetCount,"cuDeviceGetCount");BIND_CU(cuDeviceGet,"cuDeviceGet");BIND_CU(cuDeviceGetLuid,"cuDeviceGetLuid");BIND_CU(cuDeviceGetAttribute,"cuDeviceGetAttribute");
    BIND_CU(cuDevicePrimaryCtxRetain,"cuDevicePrimaryCtxRetain");BIND_CU(cuDevicePrimaryCtxRelease,"cuDevicePrimaryCtxRelease_v2");
    BIND_CU(cuCtxPushCurrent,"cuCtxPushCurrent_v2");BIND_CU(cuCtxPopCurrent,"cuCtxPopCurrent_v2");
    BIND_CU(cuStreamCreate,"cuStreamCreate");BIND_CU(cuStreamDestroy,"cuStreamDestroy_v2");BIND_CU(cuStreamSynchronize,"cuStreamSynchronize");
    BIND_CU(cuImportExternalMemory,"cuImportExternalMemory");BIND_CU(cuExternalMemoryGetMappedBuffer,"cuExternalMemoryGetMappedBuffer");
    BIND_CU(cuDestroyExternalMemory,"cuDestroyExternalMemory");BIND_CU(cuMemFree,"cuMemFree_v2");
    BIND_CU(cuImportExternalSemaphore,"cuImportExternalSemaphore");BIND_CU(cuDestroyExternalSemaphore,"cuDestroyExternalSemaphore");
    BIND_CU(cuWaitExternalSemaphoresAsync,"cuWaitExternalSemaphoresAsync");BIND_CU(cuSignalExternalSemaphoresAsync,"cuSignalExternalSemaphoresAsync");
    BIND_CU(cuGetErrorName,"cuGetErrorName");BIND_CU(cuGetErrorString,"cuGetErrorString");
#undef BIND_CU
    if(!p.cu(p.cuInitFn(0),"cuInit"))return false;
    int count=0;if(!p.cu(p.cuDeviceGetCountFn(&count),"cuDeviceGetCount"))return false;
    bool matched=false;
    for(int i=0;i<count;++i){CUdevice device=0;uint64_t luid=0;unsigned node=0;
        if(!p.cu(p.cuDeviceGetFn(&device,i),"cuDeviceGet")||!p.cu(p.cuDeviceGetLuidFn(reinterpret_cast<char*>(&luid),&node,device),"cuDeviceGetLuid"))return false;
        if(luid==context.adapter().luid&&node){p.device=device;matched=true;break;}
    }
    if(!matched){log::error("vfg","CUDA/D3D12 adapter LUID mismatch");return false;}
    int major=0,minor=0;
    if(!p.cu(p.cuDeviceGetAttributeFn(&major,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,p.device),"compute major")||
       !p.cu(p.cuDeviceGetAttributeFn(&minor,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,p.device),"compute minor"))return false;
    if(!((major==8&&minor==9)||major==12)){log::error("vfg",std::format("Windows VFG requires Ada/Blackwell; compute={}.{}",major,minor));return false;}
    if(!p.cu(p.cuDevicePrimaryCtxRetainFn(&p.cudaContext,p.device),"cuDevicePrimaryCtxRetain"))return false;p.retained=true;
    Impl::Current current(p);if(!current.active)return false;
    if(!p.cu(p.cuStreamCreateFn(&p.stream,CU_STREAM_NON_BLOCKING),"cuStreamCreate"))return false;
    const fs::path runtime=runtimeDirectory(root);
    // VFG consumes our encoded shared CUDA buffers directly. Create/Init wrap
    // these pixels without NvCVImage_Transfer or other NPP conversion effects.
    // Keep the original core/plugin/CUDA bytes; unrelated SDK image-processing
    // DLLs are not dependencies of this path (native 8/10-bit tests cover it).
    const std::array<const wchar_t*,5> dependencies={L"cudart64_12.dll",L"NVCVImage.dll",L"nvngxruntime.dll",L"NVVideoEffects.dll",L"nvVFXVideoFrameGeneration.dll"};
    HMODULE core=nullptr,image=nullptr;
    for(auto name:dependencies){auto dll=load(runtime/name);if(!dll)return false;p.libraries.push_back(dll);
        if(std::wstring_view(name)==L"NVVideoEffects.dll")core=dll;if(std::wstring_view(name)==L"NVCVImage.dll")image=dll;}
#define BIND_VFX(member,name) if(!bind(core,name,p.member))return false
    BIND_VFX(create,"NvVFX_CreateEffect");BIND_VFX(destroy,"NvVFX_DestroyEffect");BIND_VFX(setU32,"NvVFX_SetU32");BIND_VFX(setImage,"NvVFX_SetImage");
    BIND_VFX(setStream,"NvVFX_SetCudaStream");BIND_VFX(loadEffect,"NvVFX_Load");BIND_VFX(run,"NvVFX_Run");BIND_VFX(getVersion,"NvVFX_GetVersion");
#undef BIND_VFX
#define BIND_IMAGE(member,name) if(!bind(image,name,p.member))return false
    BIND_IMAGE(imageCreate,"NvCVImage_Create");BIND_IMAGE(imageDealloc,"NvCVImage_Dealloc");BIND_IMAGE(imageDestroy,"NvCVImage_Destroy");
    BIND_IMAGE(imageInit,"NvCVImage_Init");BIND_IMAGE(fromD3D,"NvCVImage_FromD3DFormat");BIND_IMAGE(errorString,"NvCV_GetErrorStringFromCode");
#undef BIND_IMAGE
    unsigned version=0;if(!p.cv(p.getVersion(&version),"NvVFX_GetVersion"))return false;
    log::info("vfg",std::format("SDK version=0x{:08X} LUID={} compute={}.{} size={}x{} multiplier={} quality={} async=1",version,context.adapter().luidString,major,minor,width,height,multiplier,quality));
    D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=width;texture.Height=height;texture.DepthOrArraySize=1;texture.MipLevels=1;texture.Format=format;texture.SampleDesc.Count=1;
    UINT rows=0;uint64_t rowBytes=0,total=0;context.device()->GetCopyableFootprints(&texture,0,1,0,&p.footprint,&rows,&rowBytes,&total);
    p.bytes=uint64_t(p.footprint.Footprint.RowPitch)*height;
    int pixelFormat=0,componentType=0;unsigned char layout=0;
    if(format==DXGI_FORMAT_R10G10B10A2_UNORM){
        // SDK 1.3's older FromD3DFormat helper rejects this DXGI format. The
        // packed NvCV API uses the appended RGB10A2=13 and P32=11 (interleaved).
        // NvCVImage_Create/Init and Load admit this pair; the GPU identity test
        // additionally verifies the actual R10/G10/B10/A2 bit layout.
        pixelFormat=13;componentType=11;layout=0;
    }else if(!p.cv(p.fromD3D(format,&pixelFormat,&componentType,&layout),"NvCVImage_FromD3DFormat"))return false;
    log::info("vfg",std::format("encoded format={} NvCV pixel={} component={} layout={} pitch={} sharedBytes={}",unsigned(format),pixelFormat,componentType,layout,p.footprint.Footprint.RowPitch,p.bytes*(2+2*(multiplier-1))));
    for(auto& buffer:p.inputs)if(!p.makeBuffer(buffer,pixelFormat,componentType,layout))return false;
    for(unsigned i=0;i<2*(multiplier-1);++i)if(!p.makeBuffer(p.outputs[i],pixelFormat,componentType,layout))return false;
    if(!hr(context.device()->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&p.producer)),"CreateFence producer")||
       !hr(context.device()->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&p.completion)),"CreateFence completion")||
       !p.importFence(p.producer.Get(),p.inputSemaphore)||!p.importFence(p.completion.Get(),p.outputSemaphore))return false;
    if(!p.cv(p.create("VideoFrameGeneration",&p.effect),"NvVFX_CreateEffect")||
       !p.cv(p.setStream(p.effect,"CudaStream",p.stream),"SetCudaStream")||
       !p.cv(p.setU32(p.effect,"InputWidth",width),"InputWidth")||!p.cv(p.setU32(p.effect,"InputHeight",height),"InputHeight")||
       !p.cv(p.setU32(p.effect,"Mode",quality),"Mode")||!p.cv(p.setU32(p.effect,"AutomaticShotChangeDetectionEnabled",1),"AutomaticShotChangeDetectionEnabled")||
       !p.cv(p.setImage(p.effect,"SrcImage0",p.inputs[0].image),"initial previous")||!p.cv(p.setImage(p.effect,"SrcImage1",p.inputs[1].image),"initial current")||
       !p.cv(p.loadEffect(p.effect),"NvVFX_Load")||!p.cv(p.setImage(p.effect,"DstImage0",p.outputs[0].image),"initial output")||
       !p.cv(p.setU32(p.effect,"FrameMultiplier",multiplier),"initial multiplier"))return false;
    p.boundMultiplier=multiplier;
    p.asynchronous=asynchronousSubmission;
    if(p.asynchronous){
        try{p.worker=std::thread([&p]{p.work();});}
        catch(...){log::error("vfg","VFG worker creation failed");return false;}
    }
    log::info("vfg",std::format("SDK submission={} capacity={} (CUDA work remains serial)",p.asynchronous?"bounded worker":"graph owner",kOutputs));
    p.ready=true;log::info("vfg","native VFG initialized; GPU-only D3D12 buffer/fence bridge");return true;
#else
    (void)context;(void)root;(void)width;(void)height;(void)format;(void)multiplier;(void)quality;(void)asynchronousSubmission;
    log::error("vfg","VFG unavailable: configure VEYRA_CUDA_DRIVER_INCLUDE_DIR");return false;
#endif
}
bool VfgBackend::created()const{
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    return impl_->ready;
#else
    return false;
#endif
}
bool VfgBackend::capture(ID3D12GraphicsCommandList* list,ID3D12Resource* texture,unsigned parity){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    auto& p=*impl_;if(!p.ready||p.failed.load()||!list||!texture||parity>1)return false;
    const auto desc=texture->GetDesc();if(desc.Width!=p.width||desc.Height!=p.height||desc.Format!=p.format)return false;
    auto* buffer=p.inputs[parity].resource.Get();
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={buffer,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=buffer;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=p.footprint;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);++p.inputRevision;return true;
#else
    (void)list;(void)texture;(void)parity;return false;
#endif
}
bool VfgBackend::generate(unsigned parity,unsigned sub,unsigned multiplier,bool reset){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    auto& p=*impl_;if(!p.ready||p.failed.load()||parity>1||multiplier<2||multiplier>p.maxMultiplier||sub<1||sub>=multiplier)return false;
    return p.handoff(parity,sub,multiplier,reset);
#else
    (void)parity;(void)sub;(void)multiplier;(void)reset;return false;
#endif
}
VfgBackend::SubmissionState VfgBackend::submissionState(unsigned slot)const{
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    return slot<kOutputs?impl_->states[slot].load():SubmissionState::Failed;
#else
    (void)slot;return SubmissionState::Failed;
#endif
}
bool VfgBackend::copyOutput(ID3D12GraphicsCommandList* list,ID3D12Resource* texture,unsigned slot){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    auto& p=*impl_;if(!p.ready||!list||!texture||slot>=2*(p.maxMultiplier-1))return false;
    const auto desc=texture->GetDesc();if(desc.Width!=p.width||desc.Height!=p.height||desc.Format!=p.format)return false;
    auto* buffer=p.outputs[slot].resource.Get();
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={buffer,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=p.footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);return true;
#else
    (void)list;(void)texture;(void)slot;return false;
#endif
}
void VfgBackend::shutdown(){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    auto& p=*impl_;p.ready=false;p.stopWorker();
    if(p.cudaContext&&p.cuCtxPushCurrentFn&&p.cuCtxPopCurrentFn){
        Impl::Current current(p);
        if(current.active){
            // This is teardown, not per-frame synchronization. The graph owner
            // drains its direct queue before destroying shared staging buffers.
            if(p.stream)p.cu(p.cuStreamSynchronizeFn(p.stream),"teardown cuStreamSynchronize");
            if(p.effect){p.destroy(p.effect);p.effect=nullptr;log::info("vfg","NvVFX_DestroyEffect completed (void API)");}
            for(auto& b:p.outputs)p.releaseBuffer(b);for(auto& b:p.inputs)p.releaseBuffer(b);
            if(p.outputSemaphore)p.cu(p.cuDestroyExternalSemaphoreFn(p.outputSemaphore),"destroy output semaphore");
            if(p.inputSemaphore)p.cu(p.cuDestroyExternalSemaphoreFn(p.inputSemaphore),"destroy input semaphore");
            if(p.stream)p.cu(p.cuStreamDestroyFn(p.stream),"cuStreamDestroy");
        }
    }
    if(p.retained)p.cu(p.cuDevicePrimaryCtxReleaseFn(p.device),"cuDevicePrimaryCtxRelease");
    p.producer.Reset();p.completion.Reset();
    for(auto i=p.libraries.rbegin();i!=p.libraries.rend();++i)FreeLibrary(*i);
    impl_=std::make_unique<Impl>();
#endif
}
}
