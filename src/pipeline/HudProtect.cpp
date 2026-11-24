// SPDX-License-Identifier: GPL-3.0-or-later
#include "HudProtect.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace veyra::pipeline {
namespace {
constexpr unsigned matchStride=7;
constexpr unsigned detectStride=9;
constexpr unsigned repairStride=10;
constexpr unsigned flowStride=3;
constexpr unsigned composeStride=9;
constexpr UINT64 counterPlacement=D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
constexpr UINT64 sampleStride=2*counterPlacement;
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
        const auto& r=regions[i];
        densityLimits_[i]=float(width)*height*
            std::max(0.f,std::min(r.right,1.f)-std::max(r.left,0.f))*
            std::max(0.f,std::min(r.bottom,1.f)-std::max(r.top,0.f))*(i==0?0.12f:0.55f);
    }
    for(unsigned i=0;i<2;++i){
        input_[i]=makeTexture(device,width,height,format,true);
        confidence_[i]=makeTexture(device,roiWidth,roiHeight,DXGI_FORMAT_R8_UNORM,true);
        appearance_[i]=makeTexture(device,roiWidth,roiHeight,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        density_[i]=makeTexture(device,regionCount*countersPerRegion,1,DXGI_FORMAT_R32_UINT,true);
        mask_[i]=makeTexture(device,roiWidth,roiHeight,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        motion_[i]=makeTexture(device,UINT(regions.size()),1,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        if(!input_[i]||!confidence_[i]||!appearance_[i]||!density_[i]||!mask_[i]||!motion_[i])return false;
    }
    if(generatedCount){
        scratch_=makeTexture(device,roiWidth,roiHeight,format,true);
        if(!scratch_)return false;
    }
    if(externalMotion){
        correctedFlow_=makeTexture(device,width,height,DXGI_FORMAT_R16G16_FLOAT,true);
        if(!correctedFlow_)return false;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width=sampleStride*diagnosticSlots;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;
    buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&diagnostics_));
    if(FAILED(hr))log::warn("hud-protect",std::format("counter readback unavailable hr=0x{:X}",unsigned(hr)));

    std::vector<uint8_t> cs;
    const auto build=[&](ComputePass& pass,const char* name,unsigned tables,unsigned srvs,unsigned uavs){
        return pass.loadShader(name,cs)&&pass.create(device,cs,tables*(srvs+uavs),srvs,uavs,UINT(constants_.size()));
    };
    if(!build(matchPass_,"HudProtectMotion.dxil",2,5,2)||
       !build(detectPass_,"HudProtectDetect.dxil",2,6,3)||
       !build(repairPass_,"HudProtectRepair.dxil",2,7,3)||
       (externalMotion&&!build(flowPass_,"HudProtectFlow.dxil",2,2,1))||
       (generatedCount&&!build(composePass_,"HudProtectCompose.dxil",generatedCount*4,8,1)))return false;
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
        srv(matchPass_,match+4,density_[prev].Get());
        uav(matchPass_,match+5,motion_[parity].Get());uav(matchPass_,match+6,density_[parity].Get());
        srv(detectPass_,detect,real[parity]);srv(detectPass_,detect+1,real[prev]);
        srv(detectPass_,detect+2,confidence_[prev].Get());srv(detectPass_,detect+3,motion_[parity].Get());
        srv(detectPass_,detect+4,appearance_[prev].Get());srv(detectPass_,detect+5,density_[prev].Get());
        uav(detectPass_,detect+6,confidence_[parity].Get());uav(detectPass_,detect+7,density_[parity].Get());
        uav(detectPass_,detect+8,appearance_[parity].Get());

        srv(repairPass_,repair,real[parity]);srv(repairPass_,repair+1,real[prev]);
        srv(repairPass_,repair+2,confidence_[parity].Get());srv(repairPass_,repair+3,mask_[prev].Get());
        srv(repairPass_,repair+4,motion_[parity].Get());srv(repairPass_,repair+5,appearance_[parity].Get());
        srv(repairPass_,repair+6,input_[prev].Get());
        uav(repairPass_,repair+7,input_[parity].Get());uav(repairPass_,repair+8,mask_[parity].Get());
        uav(repairPass_,repair+9,density_[parity].Get());

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
                srv(composePass_,base+4,input_[held].Get());srv(composePass_,base+5,real[1-held]);
                srv(composePass_,base+6,density_[maskParity].Get());
                srv(composePass_,base+7,density_[1-maskParity].Get());
                uav(composePass_,base+8,scratch_.Get());
            }
}

void HudProtect::dispatch(ID3D12GraphicsCommandList* list,const ComputePass& pass,const Constants& values,
                          unsigned table,unsigned srvCount) const {
    pass.bind(list,values.data(),gpuHandleOf(pass,table).ptr,gpuHandleOf(pass,table+srvCount).ptr);
    list->Dispatch((bounds_.right-bounds_.left+15)/16,(bounds_.bottom-bounds_.top+15)/16,1);
}

void HudProtect::prepare(ID3D12GraphicsCommandList* list,StateTracker& tracker,diagnostics::GpuTimer& timer,
                         unsigned parity,bool reset,bool haveFlow,ID3D12Fence* fence) {
    using diagnostics::GpuStage;
    collect(fence);
    const unsigned prev=1-parity;
    auto values=constants_;
    values[6]=bits(reset?1u:0u);
    values[28]=bits(haveFlow&&denseFlow_?1u:0u);

    timer.mark(list,GpuStage::HudMask);
    tracker.transition(list,real_[parity],readState);
    tracker.transition(list,real_[prev],readState);
    tracker.transition(list,motion_[prev].Get(),readState);
    tracker.transition(list,motion_[parity].Get(),writeState);
    tracker.transition(list,density_[parity].Get(),writeState);
    tracker.transition(list,density_[prev].Get(),readState);
    const auto flowState=denseFlow_?tracker.get(denseFlow_):commonState;
    if(denseFlow_)tracker.transition(list,denseFlow_,readState);
    const unsigned match=parity*matchStride;
    matchPass_.bind(list,values.data(),gpuHandleOf(matchPass_,match).ptr,gpuHandleOf(matchPass_,match+5).ptr);
    list->Dispatch(UINT(engine::HudProtectRegions{}.size()),1,1);
    if(denseFlow_)tracker.transition(list,denseFlow_,flowState);
    tracker.transition(list,motion_[parity].Get(),readState);
    tracker.uavBarrier(list,density_[parity].Get());
    tracker.transition(list,confidence_[prev].Get(),readState);
    tracker.transition(list,appearance_[prev].Get(),readState);
    tracker.transition(list,confidence_[parity].Get(),writeState);
    tracker.transition(list,appearance_[parity].Get(),writeState);
    dispatch(list,detectPass_,values,parity*detectStride,6);
    tracker.transition(list,confidence_[parity].Get(),readState);
    tracker.transition(list,appearance_[parity].Get(),readState);
    tracker.uavBarrier(list,density_[parity].Get());
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
    tracker.transition(list,density_[parity].Get(),readState);
    sample(list,tracker,parity,reset);
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
    tracker.transition(list,real_[1-heldParity],readState);
    tracker.transition(list,scratch_.Get(),writeState);
    const unsigned table=((slot*2+maskParity)*2+heldParity)*composeStride;
    tracker.transition(list,input_[heldParity].Get(),readState);
    dispatch(list,composePass_,constants_,table,8);
    if(sampleRecorded_)++samples_[(sampleWrite_+diagnosticSlots-1)%diagnosticSlots].generated;
    tracker.transition(list,input_[heldParity].Get(),commonState);
    tracker.transition(list,scratch_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list,generated,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto source=subresource(scratch_.Get());
    const auto destination=subresource(generated);
    list->CopyTextureRegion(&destination,bounds_.left,bounds_.top,0,&source,nullptr);
    tracker.transition(list,real_[heldParity],commonState);
    tracker.transition(list,real_[1-heldParity],commonState);
}

bool HudProtect::clean(const Counters& counters,unsigned region,float limit) {
    const auto* values=counters.data()+region*countersPerRegion;
    return values[Density]<=limit&&values[Core]>=(region==0?3u:2u)&&values[Repaired]>=values[Core]
        &&values[Core]*10>=values[Remembered]*9;
}

bool HudProtect::observed(const Counters& counters,unsigned region,float limit) {
    const auto* values=counters.data()+region*countersPerRegion;
    const unsigned minimum=region==0?3u:2u;
    return values[Density]<=limit&&(values[Core]>=minimum||(values[Evidence]>=minimum&&values[Evidence]<=limit));
}

void HudProtect::sample(ID3D12GraphicsCommandList* list,StateTracker& tracker,unsigned parity,bool reset) {
    sampleRecorded_=false;
    if(!diagnostics_)return;
    if(sampleCount_==diagnosticSlots){++period_.skipped;return;}
    samples_[sampleWrite_]={0,0,!reset};
    for(unsigned i=0;i<(reset?1u:2u);++i){
        auto* source=density_[parity^i].Get();
        tracker.transition(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dest{};
        dest.pResource=diagnostics_.Get();
        dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dest.PlacedFootprint.Offset=sampleWrite_*sampleStride+i*counterPlacement;
        dest.PlacedFootprint.Footprint={DXGI_FORMAT_R32_UINT,regionCount*countersPerRegion,1,1,
                                        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
        const auto src=subresource(source);
        list->CopyTextureRegion(&dest,0,0,0,&src,nullptr);
        tracker.transition(list,source,readState);
    }
    sampleRecorded_=true;
}

void HudProtect::submitted(uint64_t fence) {
    if(!sampleRecorded_)return;
    samples_[sampleWrite_].fence=fence;
    sampleWrite_=(sampleWrite_+1)%diagnosticSlots;
    ++sampleCount_;
}

void HudProtect::collect(ID3D12Fence* fence) {
    if(!diagnostics_||!fence)return;
    const auto completed=fence->GetCompletedValue();
    while(sampleCount_&&samples_[sampleRead_].fence<=completed){
        const auto pending=samples_[sampleRead_];
        const size_t from=sampleRead_*sampleStride;
        const D3D12_RANGE range{from,from+sampleStride};
        void* data=nullptr;
        if(SUCCEEDED(diagnostics_->Map(0,&range,&data))){
            Counters current{},previous{};
            const auto* bytes=static_cast<const uint8_t*>(data)+from;
            std::memcpy(current.data(),bytes,sizeof(current));
            if(pending.previousValid)std::memcpy(previous.data(),bytes+counterPlacement,sizeof(previous));
            const D3D12_RANGE noWrites{0,0};
            diagnostics_->Unmap(0,&noWrites);
            account(current,previous,pending.generated);
        }
        sampleRead_=(sampleRead_+1)%diagnosticSlots;
        --sampleCount_;
    }
}

void HudProtect::account(const Counters& current,const Counters& previous,unsigned generated) {
    unsigned maskPixels=0,remembered=0;
    bool trusted=false,cleaned=false,havePair=false,fallback=false;
    for(unsigned region=0;region<regionCount;++region){
        const auto* currentRegion=current.data()+region*countersPerRegion;
        const float limit=densityLimits_[region];
        if(limit<2)continue;
        trusted|=currentRegion[Trusted]>0;
        if(currentRegion[Density]<=limit){
            maskPixels+=currentRegion[Core];
            remembered+=currentRegion[Remembered];
        }
        const bool currentClean=clean(current,region,limit);
        cleaned|=currentRegion[Repaired]>0;
        if(!observed(current,region,limit)&&!observed(previous,region,limit))continue;
        havePair=true;
        fallback|=!currentClean||!clean(previous,region,limit);
    }
    const bool confirmed=remembered>=2;
    if(confirmed!=confirmed_){
        confirmed_=confirmed;
        everConfirmed_|=confirmed;
        log::info("hud-protect",std::format("mask={} pixels={}",confirmed?"confirmed":"lost",maskPixels));
    }
    ++period_.frames;
    period_.trusted+=unsigned(trusted);
    period_.confirmed+=unsigned(confirmed);
    period_.repaired+=unsigned(cleaned);
    period_.generated+=generated;
    if(havePair){
        if(fallback)period_.fallback+=generated;
        else period_.cleanPairs+=generated;
    }
    const auto now=Clock::now();
    if(now-summaryAt_<std::chrono::seconds(1))return;
    log::info("hud-protect",std::format(
        "frames={} trusted={} mask={} pixels={} confirmedFrames={} repairedFrames={} cleanPairGenerated={} composedGenerated={} fallback={} skippedSamples={}",
        period_.frames,period_.trusted,confirmed_?"confirmed":everConfirmed_?"lost":"learning",maskPixels,
        period_.confirmed,period_.repaired,period_.cleanPairs,period_.generated,period_.fallback,period_.skipped));
    summaryAt_=now;
    period_={};
}

}
