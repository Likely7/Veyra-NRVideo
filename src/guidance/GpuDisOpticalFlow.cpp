#include "veyra/guidance/GpuDisOpticalFlow.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "gpu_frame_contract.h"
#include "native_strict_dis_provider.h"

namespace veyra::guidance {
using namespace pipeline;
struct GpuDisOpticalFlow::Impl {
    ID3D12Device* device=nullptr;
    uint32_t w=0,h=0;
    std::unique_ptr<xess_gpu::MotionProvider> provider;
    struct Slot {
        ComPtr<ID3D12Resource> luma[2];
        ComputePass convert,validate;
        xess_gpu::FencePoint completion;
    } slots[2];
    DescriptorStager stager;
    void srv(ComputePass& pass,UINT offset,ID3D12Resource* r) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=r->GetDesc().Format;
        d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        stager.stageSrv(r,&d,pass.heap.Get(),offset);
    }
    static D3D12_CPU_DESCRIPTOR_HANDLE cpu(ComputePass& p,UINT i){return {p.heap->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(i)*p.increment};}
    static uint64_t gpu(ComputePass& p,UINT i){return p.heap->GetGPUDescriptorHandleForHeapStart().ptr+UINT64(i)*p.increment;}
};
GpuDisOpticalFlow::GpuDisOpticalFlow():p_(std::make_unique<Impl>()){}
GpuDisOpticalFlow::~GpuDisOpticalFlow()=default;
bool GpuDisOpticalFlow::initialize(ID3D12Device* device,uint32_t w,uint32_t h){
    auto& p=*p_;p.device=device;p.w=w;p.h=h;
    if(!device||w<32||h<32||w>4096||h>4096){log::error("gpu-dis","unsupported flow extent (32..4096 each axis)");return false;}
    p.provider=xess_gpu::make_strict_dis_provider(runtime::pathUtf8(runtime::shaderDirectory()/"dis"),xess_gpu::StrictDisPreset::Fast);
    xess_gpu::ProviderConfig config;config.source={w,h};config.slots=2;config.bidirectional=true;
    std::string error;
    if(!p.provider->initialize(device,config,error)){log::error("gpu-dis",error);return false;}
    if(!p.stager.initialize(device,8))return false;
    std::vector<uint8_t> convert,validate;
    if(!loadShaderBytes("GpuDisLuma.dxil",convert)||!loadShaderBytes("GpuDisValidate.dxil",validate))return false;
    for(auto& s:p.slots){
        for(auto& r:s.luma){r=makeTexture(device,w,h,DXGI_FORMAT_R8_UNORM,true);if(!r)return false;}
        if(!s.convert.create(device,convert,4,1,1)||!s.validate.create(device,validate,6,4,2))return false;
        for(UINT i=0;i<2;++i)makeUav(device,s.luma[i].Get(),DXGI_FORMAT_R8_UNORM,Impl::cpu(s.convert,2+i));
    }
    log::info("gpu-dis",std::format("initialized FAST bidirectional {}x{} slots=2; photometric/forward-backward confidence; experimental",w,h));return true;
}
bool GpuDisOpticalFlow::dispatch(ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,ID3D12Fence* fence,
    uint64_t completion,uint32_t slot,ID3D12Resource* previous,ID3D12Resource* current,ID3D12Resource* motion,
    ID3D12Resource* confidence,uint64_t previousId,uint64_t currentId){
    auto& p=*p_;
    if(!p.provider||!list||!queue||!fence||!completion||slot>=2||!previous||!current||!motion||!confidence)return false;
    auto& s=p.slots[slot];
    if(!s.completion.complete()){log::error("gpu-dis","attempted in-flight descriptor reuse");return false;}
    StateTracker states;
    const uint32_t dims[8]={p.w,p.h};
    ID3D12Resource* inputs[]={previous,current};
    for(UINT i=0;i<2;++i){
        const auto d=inputs[i]->GetDesc();
        if(d.Width!=p.w||d.Height!=p.h||d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM){log::error("gpu-dis","BGRA input geometry/format mismatch");return false;}
        p.srv(s.convert,i,inputs[i]);
        states.transition(list,inputs[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list,s.luma[i].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s.convert.bind(list,reinterpret_cast<const float*>(dims),Impl::gpu(s.convert,i),Impl::gpu(s.convert,2+i));
        list->Dispatch((p.w+15)/16,(p.h+15)/16,1);
        states.transition(list,s.luma[i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list,inputs[i],D3D12_RESOURCE_STATE_COMMON);
    }
    xess_gpu::FrameLease frames[2];
    for(UINT i=0;i<2;++i){
        frames[i].color={s.luma[i],DXGI_FORMAT_R8_UNORM,{p.w,p.h},{0,0,p.w,p.h},D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        frames[i].metadata.adapter_luid=p.device->GetAdapterLuid();
        frames[i].metadata.source_frame_id=i?currentId:previousId;
        frames[i].metadata.previous_source_frame_id=previousId;
    }
    xess_gpu::RecordContext context;context.device=p.device;context.list=list;context.queue=queue;
    context.adapter_luid=p.device->GetAdapterLuid();context.completion={fence,completion};
    xess_gpu::MotionPacket packet;xess_gpu::Counters counters;std::string error;
    if(!p.provider->record(&frames[0],frames[1],slot,context,packet,counters,error)){log::error("gpu-dis",error);return false;}
    p.srv(s.validate,0,packet.current_to_previous.resource.Get());p.srv(s.validate,1,packet.previous_to_current.resource.Get());
    p.srv(s.validate,2,s.luma[0].Get());p.srv(s.validate,3,s.luma[1].Get());
    makeUav(p.device,motion,motion->GetDesc().Format,Impl::cpu(s.validate,4));
    makeUav(p.device,confidence,confidence->GetDesc().Format,Impl::cpu(s.validate,5));
    states.transition(list,motion,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);states.transition(list,confidence,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    s.validate.bind(list,reinterpret_cast<const float*>(dims),Impl::gpu(s.validate,0),Impl::gpu(s.validate,4));
    list->Dispatch((p.w+15)/16,(p.h+15)/16,1);
    states.transition(list,motion,D3D12_RESOURCE_STATE_COMMON);states.transition(list,confidence,D3D12_RESOURCE_STATE_COMMON);
    for(auto& r:s.luma)states.transition(list,r.Get(),D3D12_RESOURCE_STATE_COMMON);
    s.completion=context.completion;
    if(log::verboseFrameLogs())log::info("gpu-dis",std::format("record previous={} current={} slot={} completion={} dispatches={}",previousId,currentId,slot,completion,xess_gpu::strict_dis_shader_dispatches(*p.provider)));
    return true;
}
}
