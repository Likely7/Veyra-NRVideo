#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"

namespace veyra::engine {
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
};

// Fills the stage, size and parameter fields of `desc` from `settings`.
// Source-format fields (hdrInput, rgbInput, packedInput, ...), hdrOutput and
// host hooks are left untouched: the caller owns those.
inline pipeline::ResolutionPlan describeStages(const StageRequest& request,const EnhancementSettings& settings,
                                               pipeline::EnhanceGraphDesc& desc) {
    const bool lowLatency=!request.exportJob&&settings.lowLatency&&request.nr;
    const auto policy=request.exportJob?pipeline::NrSizePolicy::Native:settings.nrPolicy;
    const auto plan=pipeline::ResolutionPlan::make({request.width,request.height},request.sr,policy,
        request.stillImage||request.exportJob,settings.revision,settings.srTarget,lowLatency);
    desc.sourceWidth=request.width;desc.sourceHeight=request.height;
    desc.workWidth=plan.base.width;desc.workHeight=plan.base.height;
    desc.nrWidth=plan.nr.width;desc.nrHeight=plan.nr.height;
    desc.flowWidth=plan.flow.width;desc.flowHeight=plan.flow.height;
    desc.enableSr=plan.srApplied&&(request.nvidiaAdapter||settings.videoSrQuality==kVideoSrFsr);
    desc.enableNr=request.nr&&request.nvidiaAdapter;
    desc.nrBeforeSr=!request.stillImage&&!request.exportJob&&lowLatency&&desc.enableNr&&desc.enableSr;
    desc.enableNvofStandalone=desc.enableNr;
    desc.enableFg=request.fg&&(request.nvidiaAdapter||(!request.exportJob&&presentSinkFrameGeneration(settings.frameGenerationBackend)));
    desc.fgMultiplier=request.fgMultiplier;
    desc.videoSrQuality=settings.videoSrQuality;
    desc.nrRuntime=settings.nrRuntime;
    desc.nrTemporal=settings.nrTemporal;
    desc.frameGenerationBackend=settings.frameGenerationBackend;
    desc.model=settings.model;
    desc.residual=settings.residual;
    desc.protection=settings.protection;
    desc.color=settings.color;
    desc.videoHdr=settings.videoHdr;
    desc.settingsRevision=settings.revision;
    desc.flowQuality=settings.flow;
    desc.contentRate=settings.content;
    desc.opticalFlowBackend=settings.opticalFlowBackend;
    desc.amdFlowHalfResolution=settings.amdFlowHalfResolution;
    return plan;
}
}
