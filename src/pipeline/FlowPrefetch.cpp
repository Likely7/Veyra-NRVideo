#include "veyra/pipeline/FlowPrefetch.h"
#include "veyra/Log.h"
#include <format>
namespace veyra::pipeline {
bool FlowPrefetch::initialize(gfx::D3D12DeviceContext& owner,const EnhanceGraphDesc& input){
    shutdown();Status status;
    if(!context_.initializeSharedQueue(owner,D3D12_COMMAND_LIST_TYPE_COMPUTE,4,status)||
        !ring_.initialize(context_.device(),context_.directQueue(),context_.fence(),context_.fenceEvent(),4,status))return false;
    auto desc=input;desc.noFeatures=false;desc.noNgx=true;desc.enableSr=desc.enableNr=desc.enableFg=false;desc.enableNvofStandalone=true;
    desc.nrBeforeSr=false;desc.workWidth=desc.nrWidth=desc.sourceWidth;desc.workHeight=desc.nrHeight=desc.sourceHeight;
    desc.nrLayers.clear();desc.nrLayersExtent.clear();desc.nrLayersModel.clear();desc.nrLayersSizePolicy.clear();
    desc.nrLayersResidual.clear();desc.nrLayersTemporal.clear();desc.nrLayersAntiFlicker.clear();desc.nrLayersProtection.clear();
    desc.fixedExecutionPlan.reset();desc.fixedExecutionPlanError.clear();desc.runtimeNodeOrder=false;desc.additionalColorCount=0;
    graph_=std::make_unique<EnhanceGraph>(context_,ring_);
    if(!graph_->initialize(desc)||!graph_->createViews())return false;
    for(auto& slot:slots_){
        slot.flow=makeTexture(context_.device(),graph_->flowWidth(),graph_->flowHeight(),DXGI_FORMAT_R16G16_FLOAT,false);
        slot.confidence=makeTexture(context_.device(),graph_->flowWidth(),graph_->flowHeight(),DXGI_FORMAT_R8_UNORM,false);
        if(!slot.flow||!slot.confidence)return false;
    }
    revision_=input.settingsRevision;
    veyra::log::info("flow-prefetch",std::format("initialized slots=2 sameDevice=true flow={}x{} default=false",graph_->flowWidth(),graph_->flowHeight()));return true;
}
bool FlowPrefetch::prepare(const AVFrame* frame,double pts,bool reset,uint64_t source,
    const ColorDescription* color,EnhanceGraph::PreparedFlow& output){
    output={};if(!graph_)return false;activeSlot_=unsigned(sequence_%2);auto& snapshot=slots_[activeSlot_];
    if(snapshot.consumer&&snapshot.consumerValue){
        const HRESULT hr=ring_.queue()->Wait(snapshot.consumer.Get(),snapshot.consumerValue);
        if(FAILED(hr)){veyra::log::error("flow-prefetch",std::format("producer reuse Wait hr=0x{:X}",unsigned(hr)));return false;}
    }
    EnhanceGraph::FrameOutputs result;
    if(!graph_->process(frame,pts,reset,result,source,color,nullptr,false))return false;
    Status status;uint32_t slot;auto* list=ring_.acquireNext(slot,status);if(!list)return false;
    StateTracker states;
    auto copy=[&](ID3D12Resource* from,ID3D12Resource* to){
        const auto original=graph_->diagnosticResourceState(from);states.set(from,original);
        states.transition(list,from,D3D12_RESOURCE_STATE_COPY_SOURCE);states.transition(list,to,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(to,from);states.transition(list,from,original);states.transition(list,to,D3D12_RESOURCE_STATE_COMMON);
    };
    copy(graph_->flowResource(),snapshot.flow.Get());copy(graph_->confidenceResource(),snapshot.confidence.Get());
    if(!ring_.submitAndSignal(slot))return false;
    output.flow=snapshot.flow;output.confidence=snapshot.confidence;output.readyFence=context_.fence();output.readyValue=ring_.lastSignaledValue();
    output.source=result.batch.identity.sourceFrameId;output.previous=previous_;output.settingsRevision=revision_;
    output.ptsMs=pts;output.historyReset=result.historyReset;output.valid=sequence_>0&&!result.historyReset;
    previous_=output.source;++sequence_;return true;
}
void FlowPrefetch::consumed(ID3D12Fence* fence,uint64_t value){
    auto& slot=slots_[activeSlot_];slot.consumer=fence;slot.consumerValue=value;
}
void FlowPrefetch::shutdown(){
    if(ring_.initialized()){
        for(auto& slot:slots_)if(slot.consumer&&slot.consumerValue){
            const HRESULT hr=ring_.queue()->Wait(slot.consumer.Get(),slot.consumerValue);
            if(FAILED(hr))veyra::log::error("flow-prefetch",std::format("teardown Wait hr=0x{:X}",unsigned(hr)));
        }
        ring_.drainQueue();
    }
    if(graph_)graph_->shutdown();graph_.reset();slots_={};ring_.shutdown();context_.shutdown();
    sequence_=previous_=revision_=0;activeSlot_=0;
}
}
