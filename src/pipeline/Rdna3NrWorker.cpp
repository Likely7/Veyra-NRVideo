// SPDX-License-Identifier: GPL-3.0-only
// Isolated host for RedDukeDev's MIT network layer. Never a game proxy/add-on.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "veyra/pipeline/Rdna3NrProtocol.h"
#include "veyra/pipeline/AmdNrProvider.h"
#ifndef VEYRA_RDNA3_CONTRACT_WORKER
#include "runtime/core/network.h"
#endif
#include <cstdio>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace veyra::pipeline::rdna3;
namespace {
FILE* logfile=nullptr;
bool hrFailed(int line,HRESULT hr){if(SUCCEEDED(hr))return false;if(logfile){fprintf(logfile,"worker line=%d hr=0x%08X\n",line,unsigned(hr));fflush(logfile);}return true;}
void logLine(int level,const char* message){if(logfile){fprintf(logfile,"level=%d %s\n",level,message);fflush(logfile);}}
bool readAll(HANDLE pipe,void* data,DWORD size){DWORD offset=0;while(offset<size){DWORD n=0;if(!ReadFile(pipe,static_cast<char*>(data)+offset,size-offset,&n,nullptr)||!n)return false;offset+=n;}return true;}
bool answer(HANDLE pipe,uint64_t sequence,int result,const std::string& message){Reply r;r.h.sequence=sequence;r.result=result;memcpy(r.message.data(),message.data(),std::min(message.size(),r.message.size()-1));DWORD n=0;return WriteFile(pipe,&r,sizeof(r),&n,nullptr)&&n==sizeof(r);}
bool safePath(const std::array<wchar_t,1024>& s){return s.back()==0&&std::filesystem::path(s.data()).is_absolute();}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);}
bool waitCpu(ID3D12Fence* fence,uint64_t value,DWORD timeout){
    if(fence->GetCompletedValue()>=value)return true;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;
    const bool ok=SUCCEEDED(fence->SetEventOnCompletion(value,event))&&WaitForSingleObject(event,timeout)==WAIT_OBJECT_0;CloseHandle(event);return ok;
}
struct Worker {
    Init init;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> image[4],valid;
    ComPtr<ID3D12Fence> producer,done;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    std::array<ComPtr<ID3D12CommandAllocator>,8> resultAlloc;
    std::array<ComPtr<ID3D12GraphicsCommandList2>,8> resultList;
    uint64_t last=0;bool started=false;
    std::string error;
#ifndef VEYRA_RDNA3_CONTRACT_WORKER
    dlss5nr::Network network;
    dlss5nr::SharedImage shared[4];
    void* producerSem=nullptr;void* doneSem=nullptr;
    ComPtr<ID3D12Fence> cudaDone;
    HANDLE cudaDoneHandle=nullptr;
    dlss5nr::NetworkControls controls;
#endif
    bool open(){
        if(!validHeader(init.h,Op::Init,sizeof(init))||!validExtent(init.width,init.height)||init.reserved||init.h.sequence||
           (init.preset!=1&&init.preset!=2)||!safePath(init.runtime)||!safePath(init.data)||!safePath(init.log)){error="invalid initialization/protocol/model/extent";return false;}
        if(_wfopen_s(&logfile,init.log.data(),L"wb")||!logfile){error="worker diagnostic log could not be created";return false;}
        std::error_code ec;std::filesystem::create_directories(std::filesystem::path(init.data.data())/L"tmp",ec);
        const auto tmp=std::filesystem::path(init.data.data())/L"tmp";SetEnvironmentVariableW(L"TEMP",tmp.c_str());SetEnvironmentVariableW(L"TMP",tmp.c_str());
        SetEnvironmentVariableW(L"DLSS5NR_ALLOW_TRANSLATION",nullptr); // packaged cache only, no surprise 30-minute build
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;
        const LUID luid{uint32_t(init.adapterLuid),LONG(init.adapterLuid>>32)};
        if(hrFailed(__LINE__,CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||hrFailed(__LINE__,factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)))||
           hrFailed(__LINE__,D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){error="selected D3D12 adapter LUID is unavailable";return false;}
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
#ifndef VEYRA_RDNA3_CONTRACT_WORKER
        if(veyra::pipeline::amdNrProvider(desc.VendorId,desc.Description)!=veyra::pipeline::AmdNrProvider::RedDukeRdna3){error="this worker requires the selected AMD RX7000 adapter";return false;}
        // HIP SDK installs may set HIP_PATH without changing global PATH. Load
        // the actual HIP7 library absolutely in this worker only.
        wchar_t system[MAX_PATH]{},sdk[32768]{};GetSystemDirectoryW(system,MAX_PATH);
        auto hip=std::filesystem::path(system)/L"amdhip64_7.dll";
        if(!std::filesystem::is_regular_file(hip)){
            const auto n=GetEnvironmentVariableW(L"HIP_PATH",sdk,DWORD(std::size(sdk)));
            if(!n||n>=std::size(sdk)||!std::filesystem::path(sdk).is_absolute()){error="AMD HIP7 is unavailable in the driver or HIP_PATH";return false;}
            hip=std::filesystem::path(sdk)/L"bin"/L"amdhip64_7.dll";
        }
        if(!LoadLibraryExW(hip.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)){error="AMD HIP7 load failed win32="+std::to_string(GetLastError());return false;}
        const auto snippet=std::filesystem::path(init.runtime.data())/L"nvngx_dlssnr.dll";
        DWORD ignored=0;const DWORD size=GetFileVersionInfoSizeW(snippet.c_str(),&ignored);std::vector<uint8_t> version(size);VS_FIXEDFILEINFO* info=nullptr;UINT count=0;
        if(!size||!GetFileVersionInfoW(snippet.c_str(),0,size,version.data())||!VerQueryValueW(version.data(),L"\\",reinterpret_cast<void**>(&info),&count)||
           !info||info->dwFileVersionMS!=MAKELONG(9,310)||info->dwFileVersionLS!=MAKELONG(0,16)){error="RDNA3 packaged kernels/cache require NR 310.9.16.0 (Small and Original)";return false;}
#endif
        for(unsigned i=0;i<4;++i){
            const HANDLE handle=reinterpret_cast<HANDLE>(uintptr_t(init.handles[i]));
            const auto hr=device->OpenSharedHandle(handle,IID_PPV_ARGS(&image[i]));if(hrFailed(__LINE__,hr)){error="shared texture could not be opened on the selected adapter";return false;}
            const auto d=image[i]->GetDesc();const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
            if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=init.width||d.Height!=init.height||d.Format!=formats[i]){error="shared texture extent/format mismatch";return false;}
        }
        if(hrFailed(__LINE__,device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[4])),IID_PPV_ARGS(&producer)))||
           hrFailed(__LINE__,device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[5])),IID_PPV_ARGS(&done)))){error="shared fence open failed";return false;}
        if(hrFailed(__LINE__,device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[6])),IID_PPV_ARGS(&valid)))||valid->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||valid->GetDesc().Width<16){error="shared validity buffer is missing or malformed";return false;}
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 features{};
        if(hrFailed(__LINE__,device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&features,sizeof(features)))||!(features.WriteBufferImmediateSupportFlags&D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT)){error="direct-queue GPU completion markers are unavailable";return false;}
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(hrFailed(__LINE__,device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||hrFailed(__LINE__,device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)))||
           hrFailed(__LINE__,device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))){error="worker initialization queue creation failed";return false;}
        for(size_t i=0;i<resultAlloc.size();++i){
            ComPtr<ID3D12GraphicsCommandList> commands;
            if(hrFailed(__LINE__,device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&resultAlloc[i])))||hrFailed(__LINE__,device->CreateCommandList(0,q.Type,resultAlloc[i].Get(),nullptr,IID_PPV_ARGS(&commands)))||hrFailed(__LINE__,commands.As(&resultList[i]))||hrFailed(__LINE__,resultList[i]->Close())){error="bounded worker result queue creation failed";return false;}
        }
        // Initialization only: deterministic blank warm-up. There is no pixel
        // readback and the steady-state inference path has no CPU fence wait.
        ComPtr<ID3D12DescriptorHeap> gpuHeap,cpuHeap;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=4;
        if(hrFailed(__LINE__,device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&cpuHeap))))return false;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(hrFailed(__LINE__,device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&gpuHeap))))return false;
        const auto increment=device->GetDescriptorHandleIncrementSize(hd.Type);ID3D12DescriptorHeap* heaps[]={gpuHeap.Get()};list->SetDescriptorHeaps(1,heaps);const float zero[4]={};
        for(unsigned i=0;i<4;++i){
            auto cpu=cpuHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=i*increment;auto visible=gpuHeap->GetCPUDescriptorHandleForHeapStart();visible.ptr+=i*increment;auto gpu=gpuHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=i*increment;
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;u.Format=image[i]->GetDesc().Format;
            device->CreateUnorderedAccessView(image[i].Get(),nullptr,&u,cpu);device->CopyDescriptorsSimple(1,visible,cpu,hd.Type);
            transition(list.Get(),image[i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);list->ClearUnorderedAccessViewFloat(gpu,cpu,image[i].Get(),zero,0,nullptr);
            transition(list.Get(),image[i].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        }
        if(hrFailed(__LINE__,list->Close()))return false;ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        ComPtr<ID3D12Fence> initial;if(hrFailed(__LINE__,device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&initial)))||hrFailed(__LINE__,queue->Signal(initial.Get(),1))||!waitCpu(initial.Get(),1,30000)){error="worker blank warm-up submission failed";return false;}
#ifndef VEYRA_RDNA3_CONTRACT_WORKER
        dlss5nr::set_network_log(logLine);
        dlss5nr::NetworkPaths paths;paths.runtime_dir=init.runtime.data();paths.snippet=snippet;paths.data_dir=init.data.data();paths.adapter_luid=init.adapterLuid;
        if(!network.load(paths,error)||!network.can_share(device.Get())){if(error.empty())error="native D3D12/HIP shared images are unavailable; CPU pixel-copy fallback is forbidden";return false;}
        network.set_copy_mode(false);controls.preset=int(init.preset);
        if(!network.create_feature(init.width,init.height,controls,error))return false;
        for(unsigned i=0;i<4;++i){auto& t=shared[i];t.resource=image[i].Get();t.nt_handle=reinterpret_cast<void*>(uintptr_t(init.handles[i]));t.width=init.width;t.height=init.height;t.dxgi_format=image[i]->GetDesc().Format;t.writable=i==1;
            if(!network.import(t,error))return false;}
        if(hrFailed(__LINE__,device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&cudaDone)))||hrFailed(__LINE__,device->CreateSharedHandle(cudaDone.Get(),nullptr,GENERIC_ALL,nullptr,&cudaDoneHandle))){error="private CUDA completion fence creation failed";return false;}
        if(!network.import_fence(reinterpret_cast<void*>(uintptr_t(init.handles[4])),&producerSem,error)||!network.import_fence(cudaDoneHandle,&doneSem,error))return false;
        dlss5nr::EvaluateDesc warm;warm.color=&shared[0];warm.output=&shared[1];warm.depth=&shared[2];warm.motion=&shared[3];warm.controls=controls;warm.reset=true;
        if(!network.evaluate(warm,error))return false;
        logLine(2,"native Small/Original warm-up complete; shared GPU semaphores required, no async old-frame residual");
#else
        logLine(2,"CONTRACT TEST WORKER: copies proxy; does not execute NR or validate Radeon inference");
#endif
        started=true;return true;
    }
    bool publish(uint64_t sequence,bool copy){
        const size_t index=size_t((sequence-1)%resultAlloc.size());
        if(sequence>resultAlloc.size()&&done->GetCompletedValue()<sequence-resultAlloc.size()){error="worker exceeded its bounded GPU result queue";return false;}
        auto* commands=resultList[index].Get();
        if(hrFailed(__LINE__,resultAlloc[index]->Reset())||hrFailed(__LINE__,commands->Reset(resultAlloc[index].Get(),nullptr))){error="worker result allocator reset failed";return false;}
#ifdef VEYRA_RDNA3_CONTRACT_WORKER
        if(hrFailed(__LINE__,queue->Wait(producer.Get(),sequence))){error="test producer GPU wait failed";return false;}
#else
        if(hrFailed(__LINE__,queue->Wait(cudaDone.Get(),sequence))){error="network completion GPU wait failed";return false;}
#endif
        if(copy){
            transition(commands,image[0].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(commands,image[1].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            commands->CopyResource(image[1].Get(),image[0].Get());transition(commands,image[0].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);transition(commands,image[1].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        }
        bool mark=true;
#ifdef VEYRA_RDNA3_CONTRACT_WORKER
        mark=!std::filesystem::exists(std::filesystem::path(init.runtime.data())/L"contract-no-result");
#endif
        if(mark){
            transition(commands,valid.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_WRITEBUFFERIMMEDIATE_PARAMETER marker{valid->GetGPUVirtualAddress(),1};
            commands->WriteBufferImmediate(1,&marker,nullptr);
            transition(commands,valid.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        }
        if(hrFailed(__LINE__,commands->Close())){error="result marker list close failed";return false;}
        ID3D12CommandList* submitted[]={commands};queue->ExecuteCommandLists(1,submitted);
        if(hrFailed(__LINE__,queue->Signal(done.Get(),sequence))){error="validated result fence signal failed";return false;}
        return true;
    }
    bool evaluate(const Evaluate& e){
        if(!validHeader(e.h,Op::Evaluate,sizeof(e))||e.h.sequence!=last+1||e.reset!=1||!std::isfinite(e.intensity)||e.intensity<0||e.intensity>1){error="invalid/stale source frame transaction";return false;}
#ifdef VEYRA_RDNA3_CONTRACT_WORKER
        if(!publish(e.h.sequence,true))return false;
#else
        // Like the existing AMD provider's transfer_strength, model strength is
        // applied by the host decoder. Create once: upstream explicitly warns
        // that rebuilding a feature on every slider update can hang the driver.
        if(!network.wait_fence(producerSem,e.h.sequence,error))return false;
        dlss5nr::EvaluateDesc d;d.color=&shared[0];d.output=&shared[1];d.depth=&shared[2];d.motion=&shared[3];d.controls=controls;d.reset=true;d.synchronise=false;
        if(!network.evaluate(d,error)||!network.signal_fence(doneSem,e.h.sequence,error)||!publish(e.h.sequence,false))return false;
#endif
        last=e.h.sequence;return true;
    }
    void close(uint64_t finalConsumer){
        // The parent signals producer after its final output consumer. Releasing
        // HIP imports before that would race another queue's read of the image.
        if(started&&finalConsumer&&!waitCpu(producer.Get(),finalConsumer,5000))logLine(0,"final consumer drain failed");
#ifndef VEYRA_RDNA3_CONTRACT_WORKER
        if(started){for(auto& image:shared)network.release(image);network.destroy_fence(producerSem);network.destroy_fence(doneSem);network.shutdown();}
        if(cudaDoneHandle)CloseHandle(cudaDoneHandle);
#endif
        for(auto h:init.handles)if(h)CloseHandle(reinterpret_cast<HANDLE>(uintptr_t(h)));
        if(logfile){fclose(logfile);logfile=nullptr;}
    }
};
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    HANDLE input=reinterpret_cast<HANDLE>(uintptr_t(_wcstoui64(argv[1],nullptr,10)));
    HANDLE output=reinterpret_cast<HANDLE>(uintptr_t(_wcstoui64(argv[2],nullptr,10)));
    Worker w;if(!readAll(input,&w.init,sizeof(w.init)))return 3;
    const bool opened=w.open();if(!answer(output,0,opened?0:1,w.error)||!opened)return 4;
    for(;;){
        Header h;if(!readAll(input,&h,sizeof(h)))return 5;
        if(validHeader(h,Op::Stop,sizeof(h))&&h.sequence==w.last+1){w.close(h.sequence);return 0;}
        if(!validHeader(h,Op::Evaluate,sizeof(Evaluate)))return 6;
        Evaluate e;e.h=h;if(!readAll(input,reinterpret_cast<char*>(&e)+sizeof(h),sizeof(e)-sizeof(h)))return 7;
        const bool ok=w.evaluate(e);if(!answer(output,e.h.sequence,ok?0:1,w.error)||!ok)return 8;
#ifdef VEYRA_RDNA3_CONTRACT_WORKER
        if(std::filesystem::exists(std::filesystem::path(w.init.runtime.data())/L"contract-crash-after-ack"))return 99;
#endif
    }
}
