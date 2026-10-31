// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/pipeline/Rdna3NrBackend.h"
#include "veyra/pipeline/Rdna3NrProtocol.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <algorithm>

namespace veyra::pipeline {
namespace {
using namespace rdna3;
bool hrFailed(int line,HRESULT hr){if(SUCCEEDED(hr))return false;log::error("amd-nr-rdna3",std::format("parent line={} hr=0x{:08X}",line,unsigned(hr)));return true;}
void drop(HANDLE& h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);
}
bool send(HANDLE pipe,const void* p,DWORD size){DWORD n=0;return WriteFile(pipe,p,size,&n,nullptr)&&n==size;}
template<size_t N> bool pathTo(std::array<wchar_t,N>& dest,const std::filesystem::path& p){
    const auto s=p.wstring();if(s.size()>=N)return false;std::copy(s.begin(),s.end(),dest.begin());return true;
}
constexpr auto kCommon=D3D12_RESOURCE_STATE_COMMON;
constexpr auto kRead=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto kUav=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}
struct Rdna3NrBackend::Impl {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Resource> image[4],decoded,valid;
    ComPtr<ID3D12Fence> producer,done;
    ComputePass encode,decode;DescriptorStager views;
    ComPtr<ID3D12DescriptorHeap> clearHeap;
    ID3D12Resource* boundColor=nullptr;
    HANDLE process=nullptr,job=nullptr,toWorker=nullptr,fromWorker=nullptr,stop=nullptr;
    std::jthread monitor;
    std::atomic<bool> failed=false;
    std::atomic<bool> closing=false;
    std::atomic<uint64_t> queued=0,queuedAt=0;
    uint32_t w=0,h=0;uint64_t sequence=0,sourceFrame=0;
    rdna3::Evaluate pending;
    bool opened=false,prepared=false,enqueued=false,outputs=false,bound=false;
    std::string error;
    bool fail(std::string s){error=std::move(s);log::error("amd-nr-rdna3",error);return false;}
    bool healthy(){return !failed.load()&&process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    bool reply(uint64_t seq,DWORD timeout){
        Reply r{};size_t got=0;const auto end=GetTickCount64()+timeout;
        while(got<sizeof(r)&&GetTickCount64()<end){
            DWORD available=0,n=0;
            if(!PeekNamedPipe(fromWorker,nullptr,0,nullptr,&available,nullptr))return fail("RDNA3 worker status pipe failed");
            if(!available){if(!healthy())return fail("RDNA3 inference worker exited or timed out; NR is unavailable");Sleep(2);continue;}
            if(!ReadFile(fromWorker,reinterpret_cast<char*>(&r)+got,DWORD(std::min<size_t>(available,sizeof(r)-got)),&n,nullptr)||!n)return fail("RDNA3 worker reply truncated");
            got+=n;
        }
        if(got!=sizeof(r))return fail("RDNA3 worker response exceeded its deadline");
        if(!validHeader(r.h,Op::Reply,sizeof(r))||r.h.sequence!=seq)return fail("RDNA3 worker protocol/version/frame mismatch");
        if(r.result)return fail(std::string(r.message.data(),strnlen(r.message.data(),r.message.size())));
        return true;
    }
    void watch(){
        while(WaitForSingleObject(stop,50)==WAIT_TIMEOUT){
            const auto q=queued.load();const auto at=queuedAt.load();
            const bool exited=WaitForSingleObject(process,0)!=WAIT_TIMEOUT;
            if(exited&&closing.load())break;
            const bool expired=q&&at&&done->GetCompletedValue()<q&&GetTickCount64()-at>5000;
            if(!exited&&!expired)continue;
            failed=true;
            log::error("amd-nr-rdna3",expired?"worker GPU job exceeded 5s; stopping only this worker":"inference worker exited; releasing the owned queue wait for teardown");
            if(!exited){TerminateProcess(process,125);WaitForSingleObject(process,1000);}
            // Never leave the shared D3D12 queue waiting for a dead process.
            // Input is seeded into output before enqueue; the engine rejects the
            // failed provider on its next operation. This is failure teardown.
            const auto hr=done->Signal(UINT64_MAX-1);
            if(hrFailed(__LINE__,hr))log::error("amd-nr-rdna3",std::format("failed to release worker wait hr=0x{:X}",unsigned(hr)));
            break;
        }
    }
};
Rdna3NrBackend::Rdna3NrBackend():impl_(std::make_unique<Impl>()){}
Rdna3NrBackend::~Rdna3NrBackend(){close();}
bool Rdna3NrBackend::runtimeAvailable(const std::filesystem::path& p){
    std::error_code ec;
    for(const auto* n:{L"veyra_amd_nr_worker.exe",L"nvngx_dlssnr.dll",L"zluda/nvcuda.dll",L"zluda/nvngx.dll",L"zluda/nvapi64.dll",L"zluda/zluda_real.dll",L"kernels/kernels.txt",L"cache/ComputeCache/coverage.txt",L"cache/ComputeCache/zluda2.db"})
        if(!std::filesystem::is_regular_file(p/n,ec))return false;
    return true;
}
bool Rdna3NrBackend::hipRuntimeAvailable(){
    if(GetModuleHandleW(L"amdhip64_7.dll"))return true;
    wchar_t system[MAX_PATH]{};std::error_code ec;
    if(GetSystemDirectoryW(system,MAX_PATH)&&std::filesystem::is_regular_file(std::filesystem::path(system)/L"amdhip64_7.dll",ec))return true;
    wchar_t sdk[32768]{};const auto n=GetEnvironmentVariableW(L"HIP_PATH",sdk,DWORD(std::size(sdk)));
    const std::filesystem::path root=sdk;
    return n&&n<std::size(sdk)&&root.is_absolute()&&std::filesystem::is_regular_file(root/L"bin"/L"amdhip64_7.dll",ec);
}
bool Rdna3NrBackend::open(const std::filesystem::path& p,ID3D12Device* d,ID3D12CommandQueue* q,uint32_t w,uint32_t h,bool smallModel){return start(p,d,q,w,h,smallModel,false);}
bool Rdna3NrBackend::openContractTest(const std::filesystem::path& p,ID3D12Device* d,ID3D12CommandQueue* q,uint32_t w,uint32_t h,bool smallModel){return start(p,d,q,w,h,smallModel,true);}
bool Rdna3NrBackend::start(const std::filesystem::path& directory,ID3D12Device* device,ID3D12CommandQueue* queue,uint32_t w,uint32_t h,bool smallModel,bool test){
    close();impl_=std::make_unique<Impl>();auto& s=*impl_;
    if(!device||!queue||!directory.is_absolute()||!validExtent(w,h))return s.fail("invalid RDNA3 device, queue, absolute runtime path or internal size");
    if(!test&&!runtimeAvailable(directory))return s.fail("RX7000 NR runtime is incomplete; install the RDNA3 worker, original NR network, kernels and translation cache");
    s.device=device;s.queue=queue;s.w=w;s.h=h;
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
    for(unsigned i=0;i<4;++i){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;r.DepthOrArraySize=r.MipLevels=1;
        r.Format=formats[i];r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&r,kCommon,nullptr,IID_PPV_ARGS(&s.image[i]));
        if(hrFailed(__LINE__,hr))return s.fail(std::format("RDNA3 shared texture {} allocation hr=0x{:X}",i,unsigned(hr)));
    }
    s.decoded=makeTexture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    D3D12_HEAP_PROPERTIES validityHeap{};validityHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC validityDesc{};validityDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;validityDesc.Width=16;validityDesc.Height=validityDesc.DepthOrArraySize=validityDesc.MipLevels=1;validityDesc.SampleDesc.Count=1;validityDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;validityDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(hrFailed(__LINE__,device->CreateCommittedResource(&validityHeap,D3D12_HEAP_FLAG_SHARED,&validityDesc,kCommon,nullptr,IID_PPV_ARGS(&s.valid))))return s.fail("RDNA3 shared validity buffer allocation failed");
    std::vector<uint8_t> code;
    if(!s.decoded||!s.encode.loadShader("ParityEncode.dxil",code)||!s.encode.create(device,code,5,1,1)||
       !s.decode.loadShader("Rdna3NrDecode.dxil",code)||!s.decode.create(device,code,5,3,1,8,1)||!s.views.initialize(device,8))return s.fail("RDNA3 shared parity shader setup failed");
    D3D12_DESCRIPTOR_HEAP_DESC clearDesc{};clearDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;clearDesc.NumDescriptors=3;
    if(hrFailed(__LINE__,device->CreateDescriptorHeap(&clearDesc,IID_PPV_ARGS(&s.clearHeap))))return s.fail("RDNA3 clear descriptor heap failed");
    if(hrFailed(__LINE__,device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&s.producer)))||hrFailed(__LINE__,device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&s.done))))return s.fail("RDNA3 shared fence creation failed");
    HANDLE readChild=nullptr,writeChild=nullptr;SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    if(!CreatePipe(&readChild,&s.toWorker,&sa,65536)||!CreatePipe(&s.fromWorker,&writeChild,&sa,65536)){drop(readChild);drop(writeChild);return s.fail("RDNA3 control pipe creation failed");}
    SetHandleInformation(s.toWorker,HANDLE_FLAG_INHERIT,0);SetHandleInformation(s.fromWorker,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> storage(bytes);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    HANDLE inherited[]={readChild,writeChild};
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)||!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)){
        drop(readChild);drop(writeChild);return s.fail("RDNA3 inherited-handle allowlist failed");
    }
    const auto executable=directory/(test?L"veyra_amd_nr_test_worker.exe":L"veyra_amd_nr_worker.exe");
    auto command=std::format(L"\"{}\" {} {}",executable.wstring(),uintptr_t(readChild),uintptr_t(writeChild));
    PROCESS_INFORMATION process{};
    const bool created=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,directory.c_str(),&startup.StartupInfo,&process)!=FALSE;
    DeleteProcThreadAttributeList(startup.lpAttributeList);drop(readChild);drop(writeChild);
    if(!created)return s.fail(std::format("RDNA3 worker launch failed win32={}",GetLastError()));
    s.process=process.hProcess;s.job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!s.job||!SetInformationJobObject(s.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(s.job,s.process)){
        TerminateProcess(s.process,126);drop(process.hThread);return s.fail("RDNA3 worker could not be confined to its owned job");
    }
    Init init;const auto luid=device->GetAdapterLuid();init.adapterLuid=uint64_t(uint32_t(luid.HighPart))<<32|luid.LowPart;
    init.width=w;init.height=h;init.preset=smallModel?2:1;
    std::error_code ec;const auto data=directory/L"cache";std::filesystem::create_directories(data/L"tmp",ec);
    if(!pathTo(init.runtime,directory)||!pathTo(init.data,data)||!pathTo(init.log,data/std::format(L"worker-{}.log",process.dwProcessId))){TerminateProcess(s.process,126);drop(process.hThread);return s.fail("RDNA3 runtime path exceeds protocol limit");}
    ID3D12DeviceChild* shared[]={s.image[0].Get(),s.image[1].Get(),s.image[2].Get(),s.image[3].Get(),s.producer.Get(),s.done.Get(),s.valid.Get()};
    for(unsigned i=0;i<7;++i){
        HANDLE local=nullptr,remote=nullptr;
        const auto hr=device->CreateSharedHandle(shared[i],nullptr,GENERIC_ALL,nullptr,&local);
        const bool ok=SUCCEEDED(hr)&&DuplicateHandle(GetCurrentProcess(),local,s.process,&remote,0,FALSE,DUPLICATE_SAME_ACCESS);
        drop(local);if(!ok){TerminateProcess(s.process,126);drop(process.hThread);return s.fail(std::format("RDNA3 shared handle {} transfer hr=0x{:X} win32={}",i,unsigned(hr),GetLastError()));}
        init.handles[i]=uintptr_t(remote);
    }
    ResumeThread(process.hThread);drop(process.hThread);
    if(!send(s.toWorker,&init,sizeof(init))||!s.reply(0,180000))return false;
    s.stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!s.stop)return s.fail("RDNA3 worker monitor event failed");
    s.monitor=std::jthread([&s]{s.watch();});s.opened=true;
    log::info("amd-nr-rdna3",std::format("{}worker ready pid={} model={} internal={}x{} same-frame GPU interop; Veyra owns history",test?"CONTRACT TEST (not inference) ":"",process.dwProcessId,smallModel?"Small":"Original",w,h));
    return true;
}
bool Rdna3NrBackend::ready()const{return impl_->opened&&!impl_->failed.load();}
ID3D12Resource* Rdna3NrBackend::diagnosticProxyForTest()const{return impl_->image[0].Get();}
bool Rdna3NrBackend::admits(uint32_t w,uint32_t h)const{return ready()&&validExtent(w,h)&&w==impl_->w&&h==impl_->h;}
const std::string& Rdna3NrBackend::error()const{static const std::string dead="RX7000 NR worker failed; see amd-nr-rdna3 log";return impl_->failed&&impl_->error.empty()?dead:impl_->error;}
bool Rdna3NrBackend::recordInputs(ID3D12GraphicsCommandList* list,ID3D12Resource* color,uint64_t frame,bool reset,float intensity){
    auto& s=*impl_;if(!ready()||!s.healthy()||s.prepared||!list||!color||!frame||!std::isfinite(intensity))return s.fail("invalid/failed RDNA3 source transaction");
    const auto d=color->GetDesc();if(d.Width!=s.w||d.Height!=s.h||d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return s.fail("RDNA3 needs the selected linear RGBA16F internal extent");
    if(s.bound&&s.boundColor!=color)return s.fail("RDNA3 layer input resource changed without rebuilding the provider");
    if(!s.bound){
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        auto bindSrv=[&](ComputePass& pass,UINT i,ID3D12Resource* r){srv.Format=r->GetDesc().Format;s.views.stageSrv(r,&srv,pass.heap.Get(),i);};
        auto bindUav=[&](ComputePass& pass,UINT i,ID3D12Resource* r){D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;u.Format=r->GetDesc().Format;auto c=pass.heap->GetCPUDescriptorHandleForHeapStart();c.ptr+=i*pass.increment;s.device->CreateUnorderedAccessView(r,nullptr,&u,c);if(&pass==&s.encode&&i>=2){auto clear=s.clearHeap->GetCPUDescriptorHandleForHeapStart();clear.ptr+=(i-2)*pass.increment;s.device->CreateUnorderedAccessView(r,nullptr,&u,clear);}};
        bindSrv(s.encode,0,color);bindUav(s.encode,1,s.image[0].Get());bindUav(s.encode,2,s.image[2].Get());bindUav(s.encode,3,s.image[3].Get());
        bindSrv(s.decode,0,color);bindSrv(s.decode,1,s.image[0].Get());bindSrv(s.decode,2,s.image[1].Get());bindUav(s.decode,3,s.decoded.Get());s.bound=true;s.boundColor=color;
        D3D12_UNORDERED_ACCESS_VIEW_DESC flagUav{};flagUav.Format=DXGI_FORMAT_R32_UINT;flagUav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;flagUav.Buffer.NumElements=4;
        auto flagCpu=s.clearHeap->GetCPUDescriptorHandleForHeapStart();flagCpu.ptr+=2*s.encode.increment;s.device->CreateUnorderedAccessView(s.valid.Get(),nullptr,&flagUav,flagCpu);
        auto flagVisible=s.encode.heap->GetCPUDescriptorHandleForHeapStart();flagVisible.ptr+=4*s.encode.increment;s.device->CopyDescriptorsSimple(1,flagVisible,flagCpu,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC flagSrv{};flagSrv.Format=DXGI_FORMAT_R32_TYPELESS;flagSrv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;flagSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;flagSrv.Buffer.NumElements=4;flagSrv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;s.views.stageSrv(s.valid.Get(),&flagSrv,s.decode.heap.Get(),4);
    }
    barrier(list,s.image[0].Get(),kCommon,kUav);
    float constants[8]={1,1,1,0};memcpy(constants+4,&s.w,4);memcpy(constants+5,&s.h,4);
    const auto gpu=s.encode.heap->GetGPUDescriptorHandleForHeapStart();s.encode.bind(list,constants,gpu.ptr,gpu.ptr+s.encode.increment);list->Dispatch((s.w+15)/16,(s.h+15)/16,1);
    // Zero guidance deliberately starts the network over every frame. Estimated
    // temporal guidance/composition remains Veyra's, like the existing AMD route.
    ID3D12DescriptorHeap* heaps[]={s.encode.heap.Get()};list->SetDescriptorHeaps(1,heaps);
    const float zeros[4]={};
    for(unsigned i=2;i<4;++i){barrier(list,s.image[i].Get(),kCommon,kUav);auto g=gpu;g.ptr+=i*s.encode.increment;auto c=s.clearHeap->GetCPUDescriptorHandleForHeapStart();c.ptr+=(i-2)*s.encode.increment;
        list->ClearUnorderedAccessViewFloat(g,c,s.image[i].Get(),zeros,0,nullptr);barrier(list,s.image[i].Get(),kUav,kCommon);}
    barrier(list,s.valid.Get(),kCommon,kUav);auto flagGpu=gpu;flagGpu.ptr+=4*s.encode.increment;auto flagCpu=s.clearHeap->GetCPUDescriptorHandleForHeapStart();flagCpu.ptr+=2*s.encode.increment;const UINT clearFlag[4]={};list->ClearUnorderedAccessViewUint(flagGpu,flagCpu,s.valid.Get(),clearFlag,0,nullptr);barrier(list,s.valid.Get(),kUav,kCommon);
    barrier(list,s.image[0].Get(),kUav,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(list,s.image[1].Get(),kCommon,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(s.image[1].Get(),s.image[0].Get());
    barrier(list,s.image[0].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,kCommon);barrier(list,s.image[1].Get(),D3D12_RESOURCE_STATE_COPY_DEST,kCommon);
    ++s.sequence;s.sourceFrame=frame;s.prepared=true;s.enqueued=s.outputs=false;
    // Parameters are part of this source transaction, not process-global flags.
    rdna3::Evaluate request;request.h.sequence=s.sequence;request.intensity=std::clamp(intensity,0.f,1.f);request.reset=1;
    // Only send after producer commands have actually been submitted (enqueue).
    static_assert(sizeof(request)<=64); // stored without pointers
    s.error.clear();
    // Preserve controls between record and enqueue without touching another layer.
    s.pending=request;
    (void)reset;return true;
}
bool Rdna3NrBackend::enqueue(){
    auto& s=*impl_;if(!ready()||!s.prepared||s.enqueued||!s.healthy())return s.fail("RDNA3 enqueue without a prepared source");
    const auto hr=s.queue->Signal(s.producer.Get(),s.sequence);if(hrFailed(__LINE__,hr))return s.fail(std::format("RDNA3 producer signal hr=0x{:X}",unsigned(hr)));
    if(!send(s.toWorker,&s.pending,sizeof(s.pending))||!s.reply(s.sequence,5000))return false;
    const auto completed=s.done->GetCompletedValue();if(completed==UINT64_MAX)return s.fail("RDNA3 device removed");
    if(completed>=s.queued.load())s.queuedAt=GetTickCount64();s.queued=s.sequence;
    const auto wait=s.queue->Wait(s.done.Get(),s.sequence);if(hrFailed(__LINE__,wait))return s.fail(std::format("RDNA3 consumer wait hr=0x{:X}",unsigned(wait)));
    s.enqueued=true;return true;
}
bool Rdna3NrBackend::recordOutputs(ID3D12GraphicsCommandList* list,ID3D12Resource* destination){
    auto& s=*impl_;if(!ready()||!s.enqueued||s.outputs||!list||!destination)return s.fail("RDNA3 output without current-frame evaluation");
    barrier(list,s.image[0].Get(),kCommon,kRead);barrier(list,s.image[1].Get(),kCommon,kRead);barrier(list,s.decoded.Get(),kCommon,kUav);barrier(list,s.valid.Get(),kCommon,kRead);
    // Same AMD host contract as transfer_strength in the existing runtime:
    // strength changes mix the full model result, never churn model instances.
    float constants[8]={1,s.pending.intensity,1,0};memcpy(constants+4,&s.w,4);memcpy(constants+5,&s.h,4);
    const auto gpu=s.decode.heap->GetGPUDescriptorHandleForHeapStart();s.decode.bind(list,constants,gpu.ptr,gpu.ptr+3*s.decode.increment,gpu.ptr+4*s.decode.increment);list->Dispatch((s.w+15)/16,(s.h+15)/16,1);
    barrier(list,s.decoded.Get(),kUav,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyResource(destination,s.decoded.Get());
    barrier(list,s.decoded.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,kCommon);barrier(list,s.image[0].Get(),kRead,kCommon);barrier(list,s.image[1].Get(),kRead,kCommon);
    barrier(list,s.valid.Get(),kRead,kCommon);
    s.outputs=true;return true;
}
bool Rdna3NrBackend::retireSubmitted(){auto& s=*impl_;if(!s.outputs)return true;if(!ready())return false;s.prepared=s.enqueued=s.outputs=false;return true;}
void Rdna3NrBackend::close()noexcept{
    if(!impl_)return;auto& s=*impl_;
    s.closing=true;
    if(s.process&&WaitForSingleObject(s.process,0)==WAIT_TIMEOUT){
        // This signal follows the last output consumer on the actual graph queue.
        Header end{kMagic,kVersion,sizeof(Header),Op::Stop,s.queued.load()+1};
        if(s.queue&&s.producer)s.queue->Signal(s.producer.Get(),end.sequence);
        send(s.toWorker,&end,sizeof(end));
        if(WaitForSingleObject(s.process,3000)==WAIT_TIMEOUT){log::warn("amd-nr-rdna3","worker close timed out; terminating owned worker");TerminateProcess(s.process,125);WaitForSingleObject(s.process,1000);if(s.done)s.done->Signal(UINT64_MAX-1);}
    }
    if(s.stop)SetEvent(s.stop);if(s.monitor.joinable())s.monitor.join();
    drop(s.toWorker);drop(s.fromWorker);drop(s.process);drop(s.job);drop(s.stop);s.opened=false;
}
}
