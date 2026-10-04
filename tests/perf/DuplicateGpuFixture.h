#pragma once
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <d3dcompiler.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace dupperf {
using namespace veyra;
inline double nowUs(){return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class ExactChecker {
    gfx::D3D12DeviceContext& ctx_;gfx::CommandSlotRing& ring_;
    pipeline::ComputePass pass_;pipeline::DescriptorStager stager_;pipeline::StateTracker states_;
    pipeline::ComPtr<ID3D12Resource> flag_,readback_,zero_,history_;
    pipeline::ComPtr<ID3D12QueryHeap> queries_;bool valid_=false;
    bool copyHistory(ID3D12Resource* current,D3D12_RESOURCE_STATES initial){
        Status status;uint32_t slot;auto* list=ring_.acquireNext(slot,status);if(!list)return false;
        states_.set(current,initial);states_.transition(list,current,D3D12_RESOURCE_STATE_COPY_SOURCE);
        states_.transition(list,history_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(history_.Get(),current);
        states_.transition(list,current,initial);states_.transition(list,history_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!ring_.submitAndSignal(slot)||!ring_.waitIdle())return false;valid_=true;return true;
    }
public:
    struct Result {bool first=false,duplicate=false;double gpuCompareMs=0,cpuFenceWaitUs=0,totalCpuUs=0,historyCopyCpuUs=0;};
    ExactChecker(gfx::D3D12DeviceContext& c,gfx::CommandSlotRing& r):ctx_(c),ring_(r){}
    void invalidate(){valid_=false;}
    bool initialize(const std::filesystem::path& shader){
        pipeline::ComPtr<ID3DBlob> code,errors;
        const auto hr=D3DCompileFromFile(shader.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)){std::cerr<<"compile HRESULT="<<unsigned(hr)<<' '<<(errors?static_cast<const char*>(errors->GetBufferPointer()):"")<<std::endl;return false;}
        std::vector<uint8_t> bytes(static_cast<const uint8_t*>(code->GetBufferPointer()),static_cast<const uint8_t*>(code->GetBufferPointer())+code->GetBufferSize());
        if(!pass_.create(ctx_.device(),bytes,3,2,1,1)||!stager_.initialize(ctx_.device(),2))return false;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=4;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if(FAILED(ctx_.device()->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&flag_))))return false;
        d.Width=24;d.Flags=D3D12_RESOURCE_FLAG_NONE;h.Type=D3D12_HEAP_TYPE_READBACK;
        if(FAILED(ctx_.device()->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_))))return false;
        zero_=pipeline::makeUploadBuffer(ctx_.device(),4);if(!zero_)return false;
        void* p=nullptr;D3D12_RANGE empty{};if(FAILED(zero_->Map(0,&empty,&p)))return false;std::memset(p,0,4);zero_->Unmap(0,nullptr);
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
        if(FAILED(ctx_.device()->CreateQueryHeap(&q,IID_PPV_ARGS(&queries_))))return false;
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_UINT;u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=1;
        auto handle=pass_.heap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=2*pass_.increment;
        ctx_.device()->CreateUnorderedAccessView(flag_.Get(),nullptr,&u,handle);return true;
    }
    bool check(ID3D12Resource* current,D3D12_RESOURCE_STATES initial,bool alpha,Result& result){
        result={};const auto start=nowUs();if(!current)return false;const auto d=current->GetDesc();
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.MipLevels!=1||d.SampleDesc.Count!=1)return false;
        if(!history_||history_->GetDesc().Width!=d.Width||history_->GetDesc().Height!=d.Height||history_->GetDesc().Format!=d.Format){
            history_=pipeline::makeTexture(ctx_.device(),uint32_t(d.Width),d.Height,d.Format,false);valid_=false;states_={};if(!history_)return false;
        }
        if(!valid_){result.first=true;const auto at=nowUs();const bool ok=copyHistory(current,initial);result.historyCopyCpuUs=nowUs()-at;result.totalCpuUs=nowUs()-start;return ok;}
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=d.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        stager_.stageSrv(current,&srv,pass_.heap.Get(),0);stager_.stageSrv(history_.Get(),&srv,pass_.heap.Get(),1);
        Status status;uint32_t slot;auto* list=ring_.acquireNext(slot,status);if(!list)return false;
        states_.set(current,initial);states_.transition(list,current,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states_.transition(list,flag_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(flag_.Get(),0,zero_.Get(),0,4);
        states_.transition(list,flag_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const uint32_t compareAlpha=alpha?1:0;auto gpu=pass_.heap->GetGPUDescriptorHandleForHeapStart();
        pass_.bind(list,reinterpret_cast<const float*>(&compareAlpha),gpu.ptr,gpu.ptr+2*pass_.increment);
        list->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);list->Dispatch((uint32_t(d.Width)+15)/16,(d.Height+15)/16,1);
        list->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);states_.uavBarrier(list,flag_.Get());
        states_.transition(list,flag_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(readback_.Get(),0,flag_.Get(),0,4);
        list->ResolveQueryData(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback_.Get(),8);states_.transition(list,current,initial);
        if(!ring_.submitAndSignal(slot))return false;const auto waitAt=nowUs();if(!ring_.waitIdle())return false;result.cpuFenceWaitUs=nowUs()-waitAt;
        D3D12_RANGE range{0,24};void* p=nullptr;if(FAILED(readback_->Map(0,&range,&p)))return false;
        uint32_t flag=0;uint64_t time[2]{};std::memcpy(&flag,p,4);std::memcpy(time,static_cast<uint8_t*>(p)+8,16);D3D12_RANGE empty{};readback_->Unmap(0,&empty);
        UINT64 frequency=0;if(FAILED(ctx_.directQueue()->GetTimestampFrequency(&frequency))||!frequency||time[1]<time[0])return false;
        result.duplicate=flag==0;result.gpuCompareMs=double(time[1]-time[0])*1000/frequency;
        if(!result.duplicate){const auto at=nowUs();if(!copyHistory(current,initial))return false;result.historyCopyCpuUs=nowUs()-at;}
        result.totalCpuUs=nowUs()-start;return true;
    }
};
}
