// SPDX-License-Identifier: GPL-3.0-or-later
#include "HudProtect.h"

#include <bit>
#include <cmath>

namespace veyra::pipeline {
namespace {
constexpr unsigned matchStride=6;
constexpr unsigned detectStride=6;
constexpr unsigned repairStride=9;
constexpr unsigned flowStride=3;
constexpr unsigned composeStride=7;
constexpr auto readState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto writeState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr auto commonState=D3D12_RESOURCE_STATE_COMMON;
float bits(uint32_t value){return std::bit_cast<float>(value);}

D3D12_TEXTURE_COPY_LOCATION subresource(ID3D12Resource* value) {
    D3D12_TEXTURE_COPY_LOCATION result{};
    result.pResource=value;
    result.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    return result;
}
}

D3D12_BOX HudProtect::regionBounds(uint32_t width,uint32_t height,const engine::HudProtectRegions& regions) {
    D3D12_BOX box{width,height,0,0,0,1};
    for(const auto& r:regions){
        if(r.empty()||r.right<=0||r.bottom<=0||r.left>=1||r.top>=1)continue;
        box.left=std::min(box.left,UINT(std::floor(std::clamp(r.left,0.f,1.f)*width)));
        box.top=std::min(box.top,UINT(std::floor(std::clamp(r.top,0.f,1.f)*height)));
        box.right=std::max(box.right,UINT(std::ceil(std::clamp(r.right,0.f,1.f)*width)));
        box.bottom=std::max(box.bottom,UINT(std::ceil(std::clamp(r.bottom,0.f,1.f)*height)));
    }
    return box.left<box.right&&box.top<box.bottom?box:D3D12_BOX{};
}

bool HudProtect::initialize(ID3D12Device* device,DXGI_FORMAT format,uint32_t width,uint32_t height,
                            unsigned outputMode,const engine::HudProtectRegions& regions,
                            bool externalMotion,unsigned generatedCount) {
    bounds_=regionBounds(width,height,regions);
    generatedCount_=generatedCount;
    const UINT roiWidth=bounds_.right-bounds_.left,roiHeight=bounds_.bottom-bounds_.top;
    if(!roiWidth||!roiHeight)return false;
    constants_={bits(width),bits(height),bits(bounds_.left),bits(bounds_.top),
                bits(roiWidth),bits(roiHeight),bits(0),bits(outputMode)};
    for(unsigned i=0;i<regions.size();++i){
        const auto shape=engine::protectionConstants(regions[i]);
        std::copy(shape.begin(),shape.end(),constants_.begin()+8+i*4);
    }
    for(unsigned i=0;i<2;++i){
        input_[i]=makeTexture(device,width,height,format,true);
        confidence_[i]=makeTexture(device,roiWidth,roiHeight,DXGI_FORMAT_R8_UNORM,true);
        mask_[i]=makeTexture(device,roiWidth,roiHeight,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        motion_[i]=makeTexture(device,UINT(regions.size()),1,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        if(!input_[i]||!confidence_[i]||!mask_[i]||!motion_[i])return false;
    }
    density_=makeTexture(device,UINT(regions.size()),1,DXGI_FORMAT_R32_UINT,true);
    if(!density_)return false;
    if(generatedCount){
        scratch_=makeTexture(device,roiWidth,roiHeight,format,true);
        if(!scratch_)return false;
    }
    if(externalMotion){
        correctedFlow_=makeTexture(device,width,height,DXGI_FORMAT_R16G16_FLOAT,true);
        if(!correctedFlow_)return false;
    }

    std::vector<uint8_t> cs;
    const auto build=[&](ComputePass& pass,const char* name,unsigned tables,unsigned srvs,unsigned uavs){
        return pass.loadShader(name,cs)&&pass.create(device,cs,tables*(srvs+uavs),srvs,uavs,UINT(constants_.size()));
    };
    if(!build(matchPass_,"HudProtectMotion.dxil",2,4,2)||
       !build(detectPass_,"HudProtectDetect.dxil",2,4,2)||
       !build(repairPass_,"HudProtectRepair.dxil",2,7,2)||
       (externalMotion&&!build(flowPass_,"HudProtectFlow.dxil",2,2,1))||
       (generatedCount&&!build(composePass_,"HudProtectCompose.dxil",generatedCount*4,6,1)))return false;
    log::info("hud-protect",std::format("created work={}x{} roi={}x{} motion={} present={}",
        width,height,roiWidth,roiHeight,externalMotion,generatedCount==0));
    return true;
}

void HudProtect::createViews(ID3D12Device* device,DescriptorStager& stager,
                             ID3D12Resource* const (&real)[2],ID3D12Resource* const* generated,
                             ID3D12Resource* denseFlow) {
    denseFlow_=denseFlow;
    const auto srv=[&](const ComputePass& pass,unsigned slot,ID3D12Resource* texture){
        stager.stageSrv(texture,nullptr,pass.heap.Get(),slot);
    };
    const auto uav=[&](const ComputePass& pass,unsigned slot,ID3D12Resource* texture){
        makeUav(device,texture,texture->GetDesc().Format,cpuHandleOf(pass,slot));
    };
    // Each parity needs a fixed descriptor table until its work has retired.
    for(unsigned parity=0;parity<2;++parity){
        const unsigned prev=1-parity;
        real_[parity]=real[parity];
        const unsigned match=parity*matchStride,detect=parity*detectStride,repair=parity*repairStride;
        srv(matchPass_,match,real[parity]);srv(matchPass_,match+1,real[prev]);
        if(denseFlow_)srv(matchPass_,match+2,denseFlow_);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_R16G16_FLOAT;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels=1;
            stager.stageSrv(nullptr,&view,matchPass_.heap.Get(),match+2);
        }
        srv(matchPass_,match+3,motion_[prev].Get());
        uav(matchPass_,match+4,motion_[parity].Get());uav(matchPass_,match+5,density_.Get());
        srv(detectPass_,detect,real[parity]);srv(detectPass_,detect+1,real[prev]);
        srv(detectPass_,detect+2,confidence_[prev].Get());srv(detectPass_,detect+3,motion_[parity].Get());
        uav(detectPass_,detect+4,confidence_[parity].Get());uav(detectPass_,detect+5,density_.Get());

        srv(repairPass_,repair,real[parity]);srv(repairPass_,repair+1,real[prev]);
        srv(repairPass_,repair+2,confidence_[parity].Get());srv(repairPass_,repair+3,mask_[prev].Get());
        srv(repairPass_,repair+4,motion_[parity].Get());srv(repairPass_,repair+5,density_.Get());
        srv(repairPass_,repair+6,input_[prev].Get());
        uav(repairPass_,repair+7,input_[parity].Get());uav(repairPass_,repair+8,mask_[parity].Get());

        if(correctedFlow_){
            const unsigned flow=parity*flowStride;
            srv(flowPass_,flow,mask_[parity].Get());srv(flowPass_,flow+1,motion_[parity].Get());
            uav(flowPass_,flow+2,correctedFlow_.Get());
        }
    }
    for(unsigned maskParity=0;maskParity<2;++maskParity)
        for(unsigned held=0;held<2;++held)
            for(unsigned slot=0;slot<generatedCount_;++slot){
                const unsigned base=((slot*2+maskParity)*2+held)*composeStride;
                srv(composePass_,base,generated[slot]);srv(composePass_,base+1,real[held]);
                srv(composePass_,base+2,mask_[held].Get());srv(composePass_,base+3,mask_[maskParity].Get());
                srv(composePass_,base+4,input_[held].Get());srv(composePass_,base+5,real[maskParity]);
                uav(composePass_,base+6,scratch_.Get());
            }
}

void HudProtect::dispatch(ID3D12GraphicsCommandList* list,const ComputePass& pass,const Constants& values,
                          unsigned table,unsigned srvCount) const {
    pass.bind(list,values.data(),gpuHandleOf(pass,table).ptr,gpuHandleOf(pass,table+srvCount).ptr);
    list->Dispatch((bounds_.right-bounds_.left+15)/16,(bounds_.bottom-bounds_.top+15)/16,1);
}

void HudProtect::prepare(ID3D12GraphicsCommandList* list,StateTracker& tracker,diagnostics::GpuTimer& timer,
                         unsigned parity,bool reset,bool haveFlow) {
    using diagnostics::GpuStage;
    const unsigned prev=1-parity;
    auto values=constants_;
    values[6]=bits(reset?1u:0u);
    values[28]=bits(haveFlow&&denseFlow_?1u:0u);

    timer.mark(list,GpuStage::HudMask);
    tracker.transition(list,real_[parity],readState);
    tracker.transition(list,real_[prev],readState);
    tracker.transition(list,motion_[prev].Get(),readState);
    tracker.transition(list,motion_[parity].Get(),writeState);
    tracker.transition(list,density_.Get(),writeState);
    const auto flowState=denseFlow_?tracker.get(denseFlow_):commonState;
    if(denseFlow_)tracker.transition(list,denseFlow_,readState);
    const unsigned match=parity*matchStride;
    matchPass_.bind(list,values.data(),gpuHandleOf(matchPass_,match).ptr,gpuHandleOf(matchPass_,match+4).ptr);
    list->Dispatch(UINT(engine::HudProtectRegions{}.size()),1,1);
    if(denseFlow_)tracker.transition(list,denseFlow_,flowState);
    tracker.transition(list,motion_[parity].Get(),readState);
    tracker.uavBarrier(list,density_.Get());
    tracker.transition(list,confidence_[prev].Get(),readState);
    tracker.transition(list,confidence_[parity].Get(),writeState);
    dispatch(list,detectPass_,values,parity*detectStride,4);
    tracker.transition(list,confidence_[parity].Get(),readState);
    tracker.transition(list,density_.Get(),readState);
    timer.mark(list,GpuStage::HudMask,true);

    timer.mark(list,GpuStage::HudRepair);
    tracker.transition(list,real_[parity],D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list,input_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(input_[parity].Get(),real_[parity]);
    tracker.transition(list,real_[parity],readState);
    tracker.transition(list,input_[parity].Get(),writeState);
    tracker.transition(list,mask_[prev].Get(),readState);
    tracker.transition(list,mask_[parity].Get(),writeState);
    tracker.transition(list,input_[prev].Get(),readState);
    dispatch(list,repairPass_,values,parity*repairStride,7);
    tracker.transition(list,input_[prev].Get(),commonState);
    tracker.transition(list,mask_[parity].Get(),readState);
    tracker.transition(list,input_[parity].Get(),commonState);
    tracker.transition(list,real_[parity],commonState);
    tracker.transition(list,real_[prev],commonState);
    timer.mark(list,GpuStage::HudRepair,true);
}

ID3D12Resource* HudProtect::fixMotion(ID3D12GraphicsCommandList* list,StateTracker& tracker,
                                    diagnostics::GpuTimer& timer,unsigned parity,ID3D12Resource* input) {
    if(!correctedFlow_)return input;
    using diagnostics::GpuStage;
    timer.mark(list,GpuStage::HudMotion);
    const auto inputState=tracker.get(input);
    tracker.transition(list,input,D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list,correctedFlow_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(correctedFlow_.Get(),input);
    tracker.transition(list,input,inputState);
    tracker.transition(list,correctedFlow_.Get(),writeState);
    dispatch(list,flowPass_,constants_,parity*flowStride,2);
    tracker.transition(list,correctedFlow_.Get(),commonState);
    timer.mark(list,GpuStage::HudMotion,true);
    return correctedFlow_.Get();
}

void HudProtect::compose(ID3D12GraphicsCommandList* list,StateTracker& tracker,unsigned slot,
                         ID3D12Resource* generated,unsigned maskParity,unsigned heldParity) {
    tracker.transition(list,generated,readState);
    tracker.transition(list,real_[heldParity],readState);
    tracker.transition(list,real_[maskParity],readState);
    tracker.transition(list,scratch_.Get(),writeState);
    const unsigned table=((slot*2+maskParity)*2+heldParity)*composeStride;
    tracker.transition(list,input_[heldParity].Get(),readState);
    dispatch(list,composePass_,constants_,table,6);
    tracker.transition(list,input_[heldParity].Get(),commonState);
    tracker.transition(list,scratch_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list,generated,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto source=subresource(scratch_.Get());
    const auto destination=subresource(generated);
    list->CopyTextureRegion(&destination,bounds_.left,bounds_.top,0,&source,nullptr);
    tracker.transition(list,real_[heldParity],commonState);
    tracker.transition(list,real_[maskParity],commonState);
}

}
