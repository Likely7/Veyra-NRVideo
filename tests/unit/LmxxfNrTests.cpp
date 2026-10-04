// SPDX-License-Identifier: GPL-3.0-only
// GPU/ABI contract tests with an identity-copy provider; NOT AMD inference evidence.
#include "veyra/pipeline/LmxxfNrBackend.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
using namespace veyra;
int main(){
    unsigned checks=0,failed=0;
    const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
    gfx::D3D12DeviceContext ctx;Status st=Status::Ok;gfx::DeviceContextDesc desc{};desc.enableDebugLayer=true;
    if(!ctx.initialize(desc,st)){std::puts("FAIL D3D12 unavailable");return 2;}
    gfx::CommandSlotRing ring;if(!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 2;
    const auto directory=std::filesystem::absolute("lmxxf-test-runtime");
    std::filesystem::create_directories(directory/"assets");
    pipeline::LmxxfNrBackend nr;
    check(!nr.open(directory,ctx.device(),ctx.directQueue(),0x10DE),"NVIDIA path does not load AMD runtime");
    check(!nr.open(directory/"missing",ctx.device(),ctx.directQueue(),0x1002),"missing runtime refuses NR");
    SetEnvironmentVariableW(L"VEYRA_TEST_LMXXF_MODE",L"bad-table");
    check(!nr.open(directory,ctx.device(),ctx.directQueue(),0x1002),"incomplete ABI is rejected");
    SetEnvironmentVariableW(L"VEYRA_TEST_LMXXF_MODE",nullptr);
    _putenv_s("DLSS5_CODEC_SRGB","1");
    check(!nr.open(directory,ctx.device(),ctx.directQueue(),0x1002),"display-referred codec environment rejects linear input");
    _putenv_s("DLSS5_CODEC_SRGB","");_putenv_s("DLSS5_VIT_ADAPTIVE","1");
    check(!nr.open(directory,ctx.device(),ctx.directQueue(),0x1002),"hidden adaptive history rejected without reset support");
    _putenv_s("DLSS5_VIT_ADAPTIVE","");
    SetEnvironmentVariableW(L"VEYRA_TEST_LMXXF_MODE",L"oldabi");
    check(nr.open(directory,ctx.device(),ctx.directQueue(),0x1002),"old function table negotiated");
    check(nr.admits(1920,1080)&&nr.admits(2560,800)&&!nr.admits(2560,1440)&&!nr.admits(3840,540),"input budget and ultrawide bounds");
    auto input=pipeline::makeTexture(ctx.device(),128,72,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto output=pipeline::makeTexture(ctx.device(),128,72,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    gfx::ComPtr<ID3D12DescriptorHeap> heap;if(FAILED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))return 2;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ctx.device()->CreateUnorderedAccessView(input.Get(),nullptr,&uav,heap->GetCPUDescriptorHandleForHeapStart());
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    for(uint64_t frame=1;frame<=12;++frame){
        uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return 2;
        b.Transition={input.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,frame==1?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};list->ResourceBarrier(1,&b);
        ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);const float clear[4]={0.25f,0.5f,0.75f,1};
        list->ClearUnorderedAccessViewFloat(heap->GetGPUDescriptorHandleForHeapStart(),heap->GetCPUDescriptorHandleForHeapStart(),input.Get(),clear,0,nullptr);
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
        const bool recorded=nr.recordInputs(list,input.Get(),frame,frame==1);check(recorded,"record input, legacy frame ABI");if(!recorded)break;
        check(ring.submitAndSignal(slot)&&nr.enqueue(),"producer then same-queue enqueue");
        list=ring.acquireNext(slot,st);if(!list)return 2;
        b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,frame==1?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
        check(nr.recordOutputs(list,output.Get()),"consumer writes owned linear output");
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
        check(ring.submitAndSignal(slot)&&nr.retireSubmitted(),"retire after consumer submission without frame drain");
    }
    check(ring.waitIdle(),"final fence completes");nr.close();
    SetEnvironmentVariableW(L"VEYRA_TEST_LMXXF_MODE",L"fail-enqueue");
    check(nr.open(directory,ctx.device(),ctx.directQueue(),0x1002),"failure provider starts");
    uint32_t failSlot=0;auto* failList=ring.acquireNext(failSlot,st);if(!failList)return 2;
    check(nr.recordInputs(failList,input.Get(),13,true),"prepare failure job");
    check(ring.submitAndSignal(failSlot)&&!nr.enqueue(),"enqueue failure is not reported as successful NR");
    check(!nr.recordOutputs(failList,output.Get()),"failed enqueue exposes no output");
    check(ring.waitIdle(),"failure producer completes before teardown");nr.close();
    SetEnvironmentVariableW(L"VEYRA_TEST_LMXXF_MODE",nullptr);
    uint32_t removed=0;check(ctx.checkDeviceAlive(removed),"GPU stays alive across command allocator reuse");
    std::printf("Lmxxf C ABI/identity GPU copy: %u checks %u failures. Not AMD inference.\n",checks,failed);
    return failed?1:0;
}
