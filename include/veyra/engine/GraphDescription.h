#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"

namespace veyra::engine {
// Shared by graph creation and drained live-parameter updates. Disabled list
// nodes stay in settings, but never allocate or evaluate an NR feature.
inline void describeNrLayers(const EnhancementSettings& settings,pipeline::EnhanceGraphDesc& desc){
    desc.nrRuntime=settings.nrRuntime;desc.nrTemporal=settings.nrTemporal;desc.nrAntiFlicker=settings.nrAntiFlicker;
    desc.model=settings.model;desc.residual=settings.residual;desc.protection=settings.protection;
    desc.nrHoldStrength=settings.nrHoldStrength;desc.nrHoldTolerance=settings.nrHoldTolerance;
    desc.nrLayersModel.clear();desc.nrLayersResidual.clear();
    desc.nrLayersTemporal.clear();desc.nrLayersProtection.clear();
    desc.nrLayersSizePolicy={settings.nrPolicy};
    if(!settings.nrLayerCount||!settings.nr)return;
    desc.nrLayersSizePolicy.clear();
    for(uint32_t i=0;i<settings.nrLayerCount&&i<kMaxNrInstances;++i){
        const auto& n=settings.nrLayers[i];
        if(!n.enabled)continue;
        desc.nrLayersModel.push_back(n.model);desc.nrLayersResidual.push_back(n.residual);
        desc.nrLayersTemporal.push_back(n.temporal);
        desc.nrLayersSizePolicy.push_back(n.sizePolicy);
        desc.nrLayersProtection.push_back(ProtectionSettings{});
    }
    // Protection is a single list stage after the complete NR stack.
    if(!desc.nrLayersProtection.empty())desc.nrLayersProtection.back()=settings.protection;
}
// Stage selection for one graph build. Preview and export used to fill the
// EnhanceGraphDesc in five separate places with slightly different formulas;
// every build now goes through describeStages() so the rules live in one spot.
struct StageRequest {
    bool nr=false,sr=false,fg=false;
    uint32_t fgMultiplier=2;
    uint32_t width=0,height=0;
    bool stillImage=false;   // image preview/processing: no NR-first order
    bool exportJob=false;    // offline export: native NR size, no present-sink FG, no NR-first order
    bool nvidiaAdapter=true;
    bool amdNr=false;        // AMD adapter: NR runs on the lmxxf runtime (no NGX)
    const ChainRuntimeOrder* nodeOrder=nullptr; // paired with this settings transaction
};

// Fills the stage, size and parameter fields of `desc` from `settings`.
// Source-format fields (hdrInput, rgbInput, packedInput, ...), hdrOutput and
// host hooks are left untouched: the caller owns those.
inline pipeline::ResolutionPlan describeStages(const StageRequest& request,const EnhancementSettings& settings,
                                               pipeline::EnhanceGraphDesc& desc) {
    desc.fixedExecutionPlan.reset();desc.fixedExecutionPlanError.clear();
    desc.runtimeNodeOrder=request.nodeOrder!=nullptr;
    if(settings.nrLayerCount>settings.nrLayers.size()||settings.additionalColorCount>settings.additionalColors.size()){
        desc.fixedExecutionPlanError="too many runtime chain instances";return {};
    }
    auto chain=toChain(settings);
    bool lowLatency=!request.exportJob&&settings.lowLatency&&request.nr;
    if(request.nodeOrder){
        const auto restored=restoreRuntimeOrder(settings,*request.nodeOrder,chain);
        if(!restored.accepted){desc.fixedExecutionPlanError=restored.message;return {};}
        uint32_t nr=chain.nodeCount,sr=chain.nodeCount;
        for(uint32_t i=0;i<chain.nodeCount;++i){
            if(!chain.nodes[i].enabled)continue;
            if(chain.nodes[i].type==EffectType::NrEnhance&&nr==chain.nodeCount)nr=i;
            if(chain.nodes[i].type==EffectType::SuperResolution)sr=i;
        }
        lowLatency=request.nr&&nr<sr&&sr<chain.nodeCount;
        if(lowLatency&&(request.stillImage||request.exportJob)){
            desc.fixedExecutionPlanError="node NR-before-SR offline execution requires ordered dispatcher";return {};
        }
    }
    const auto policy=request.exportJob?pipeline::NrSizePolicy::Native:settings.nrPolicy;
    if(!pipeline::Extent{request.width,request.height}.valid()||!pipeline::validSrTarget(settings.srTarget)||
       !pipeline::validNrSizePolicy(policy)){
        // ResolutionPlan::make throws for these inputs. Report the same
        // invalid contract before any graph allocation, including on reuse.
        desc.fixedExecutionPlanError="invalid fixed-plan source extent, SR target or NR size policy";
        return {};
    }
    const auto plan=pipeline::ResolutionPlan::make({request.width,request.height},request.sr,policy,
        request.stillImage||request.exportJob,settings.revision,settings.srTarget,lowLatency);
    desc.sourceWidth=request.width;desc.sourceHeight=request.height;
    desc.workWidth=plan.base.width;desc.workHeight=plan.base.height;
    desc.nrWidth=plan.nr.width;desc.nrHeight=plan.nr.height;
    desc.flowWidth=plan.flow.width;desc.flowHeight=plan.flow.height;
    desc.enableSr=plan.srApplied&&(request.nvidiaAdapter||settings.videoSrQuality==kVideoSrFsr);
    desc.enableNr=request.nr&&(request.nvidiaAdapter||request.amdNr)&&(!settings.nrLayerCount||settings.activeNrLayerCount()>0);
    desc.nrBeforeSr=!request.stillImage&&!request.exportJob&&lowLatency&&desc.enableNr&&desc.enableSr;
    desc.enableNvofStandalone=desc.enableNr;
    const bool temporalMotion=settings.nrTemporal||std::any_of(settings.nrLayers.begin(),settings.nrLayers.begin()+settings.nrLayerCount,[](const auto& n){return n.enabled&&n.temporal;});
    desc.enableNvofStandalone=desc.enableNr&&(motionUsesFlow(settings.nrMotion)||temporalMotion);
    desc.enableFg=request.fg&&(request.nvidiaAdapter||(!request.exportJob&&crossVendorFrameGeneration(settings.frameGenerationBackend)));
    desc.fgMultiplier=request.fgMultiplier;
    desc.videoSrQuality=settings.videoSrQuality;
    describeNrLayers(settings,desc);
    desc.frameGenerationBackend=settings.frameGenerationBackend;
    desc.vfgQuality=settings.vfgQuality;
    desc.color=settings.color;
    desc.additionalColors=settings.additionalColors;
    desc.additionalColorCount=settings.additionalColorCount;
    desc.videoHdr=settings.videoHdr;
    desc.hdrOutputMode=settings.hdrOutputMode;
    desc.fgMotion=settings.fgMotion;desc.srMotion=settings.srMotion;desc.nrMotion=settings.nrMotion;
    desc.settingsRevision=settings.revision;
    desc.flowQuality=settings.flow;
    desc.contentRate=settings.content;
    desc.opticalFlowBackend=settings.opticalFlowBackend;
    desc.amdFlowHalfResolution=settings.amdFlowHalfResolution;
    desc.nrLayersExtent.clear();
    for(uint32_t i=0;i<chain.nodeCount;++i){
        auto& node=chain.nodes[i];
        // Request flags are authoritative for the legacy flat API. Retain a
        // resolution step even when AI SR is unavailable: the existing graph
        // still performs its ordinary source-to-work resize in that slot.
        if(node.type==EffectType::SuperResolution)node.enabled=request.sr;
        if(node.type==EffectType::NrEnhance)
            node.enabled=desc.enableNr&&(!settings.nrLayerCount||node.enabled);
        if(node.type==EffectType::Protection)node.enabled=node.enabled&&desc.enableNr;
        if(node.type==EffectType::FrameGeneration)node.enabled=desc.enableFg;
    }
    ChainExecutionPlan execution;
    const auto validation=compileChainExecutionPlan(chain,{{request.width,request.height},settings.srTarget,
        request.stillImage,request.exportJob},execution);
    if(!validation.accepted){desc.fixedExecutionPlanError=validation.message;return plan;}
    if(request.nodeOrder){
        // Node order owns the first NR stage, even for an ordinary resize or
        // same-size SR bypass. Each NR resource keeps its own compiled input.
        uint32_t firstNr=execution.stepCount,firstSr=execution.stepCount;
        for(uint32_t i=0;i<execution.stepCount;++i){
            if(execution.steps[i].type==EffectType::NrEnhance&&firstNr==execution.stepCount)firstNr=i;
            if(execution.steps[i].type==EffectType::SuperResolution)firstSr=i;
        }
        desc.nrBeforeSr=desc.enableNr&&firstSr<execution.stepCount&&firstNr<firstSr;
    }
    for(uint32_t i=0;i<execution.stepCount;++i)
        if(execution.steps[i].type==EffectType::NrEnhance)desc.nrLayersExtent.push_back(execution.steps[i].processing);
    // Preserve disabled-stage diagnostic dimensions; these allocate no NR
    // feature. Active NR extents now come exclusively from the compiled plan.
    if(!desc.enableNr)for(const auto size:desc.nrLayersSizePolicy){
        desc.nrLayersExtent.push_back(pipeline::ResolutionPlan::make({request.width,request.height},request.sr,
            request.exportJob?pipeline::NrSizePolicy::Native:size,request.stillImage||request.exportJob,
            settings.revision,settings.srTarget,lowLatency).nr);
    }
    desc.fixedExecutionPlan=execution;
    return plan;
}
}
