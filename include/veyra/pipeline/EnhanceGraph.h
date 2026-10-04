#pragma once
#include "veyra/engine/EffectChain.h"
#include <optional>
#include "veyra/engine/BackendRecovery.h"
#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/pipeline/ColorGradeInstance.h"
#include "veyra/pipeline/NrInstance.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/pipeline/NrTemporalPass.h"
#include "veyra/pipeline/NrHoldPass.h"

// EnhanceGraph - the real unified processing graph (Playbook R3.2).
// Chains, per real frame:
//   NV12 source (D3D12VA texture or CPU upload) -> YUV->linear RGB ->
//   SR upscale or 1:1 bypass -> parity encode -> Feature 18 evaluate ->
//   parity decode -> videoFrame slot + NVOF A/B inputs ->
//   NVOF execute + densify/confidence -> optional DLSSG 2X generate ->
//   genFrame slot. The graph submits real command-list work through the
//   shared CommandSlotRing; the probe/player/capture only assemble sources,
//   schedule, and present.
//
// The ordering constraints baked into initialize() are load-bearing on this
// system (injected-layer era evidence, 2026-09-04): all committed resources
// and NGX/NVOF objects are created BEFORE any descriptor view; the FG
// warm-up evaluate runs before views exist; static views are created last.
#include <cstdint>
#include "veyra/pipeline/ResetCoordinator.h"
#include "veyra/diagnostics/DiagnosticEvent.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <d3d12.h>

#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/core/SceneCadenceAnalyzer.h"
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/ContentCadence.h"
#include "veyra/diagnostics/GpuTimer.h"

struct AVFrame;
struct SwsContext;
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;
namespace veyra::guidance { class AmdOpticalFlow; class GpuDisOpticalFlow; }
namespace veyra::pipeline { class VfgBackend; }

namespace veyra::gfx {
class D3D12DeviceContext;
class CommandSlotRing;
class FsrSrBackend;
class FsrFgPresenter;
}

namespace veyra::ngx {
class NgxCoreHost;
class NgxCoreCache;
class DlssSrBackend;
class VideoSrBackend;
class TrueHdrBackend;
class DlssFgBackend;
class FgCompatibilitySession;
class DlssNrRuntimeAdapter;
class NvOfSession;
}

namespace veyra::pipeline {
struct ColorDescription;
// Decoded-surface view for the D3D11VA ingress (defined in FramePacket.h).
struct HardwareSurfaceInput;

// One generated texture per (parity, subframe), through seven subframes at 8X.
// Each backend keeps its own supported multiplier and resource requirements.
inline constexpr unsigned kGeneratedPoolSlots=2*(FrameBatch::Capacity-1);
inline constexpr unsigned kOutputPoolSlots=2+kGeneratedPoolSlots;

struct EnhanceGraphDesc {
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    uint32_t workWidth = 3840;
    uint32_t workHeight = 2160;
    uint32_t videoSrQuality=0;
    bool enableSr = false;       // upscale source -> work extent (1:1 bypass otherwise)
    bool enableNr = true;
    bool nrBeforeSr = false;
    engine::NrRuntime nrRuntime=engine::NrRuntime::Original;
    bool nrTemporal=false;
    engine::NrAntiFlicker nrAntiFlicker=engine::NrAntiFlicker::Flow;
    bool enableFg = true;
    bool validateMotion = true; // disable only in isolated legacy A/B diagnostics
    bool enableNvofStandalone = false; // run NVOF+densify per frame without FG (quality core)
    bool noFeatures = false;     // VEYRA_NO_FEATURES: NVOF/NGX objects skipped
    bool noNgx = false;          // VEYRA_NO_NGX: core/features skipped, NVOF only
    bool hdrInput = false;       // PQ/HLG YUV ingress; SDR tone mapping unless hdrOutput.
    unsigned captureBitDepth = 8; // SDR P010/P016 storage, independent of HDR transfer.
    bool wideYuvInput() const { return hdrInput || captureBitDepth > 8; }
    bool hdrOutput = false;      // HDR-preserving output; HDR10 by default, FG requires HDR10.
    engine::HdrOutputMode hdrOutputMode=engine::HdrOutputMode::Hdr10;
    engine::MotionSource fgMotion=engine::MotionSource::Automatic,srMotion=engine::MotionSource::OpticalFlow,nrMotion=engine::MotionSource::OpticalFlow;
    bool fgUsesFlow()const{return engine::motionUsesFlow(fgMotion,frameGenerationBackend);}
    bool srUsesFlow()const{return engine::motionUsesFlow(srMotion);}
    bool nrUsesFlow()const{return engine::motionUsesFlow(nrMotion);}
    engine::VideoHdrSettings videoHdr;
    bool hdrWorking() const {return hdrInput&&hdrOutput;}
    bool convertVideoHdr() const {return videoHdr.enabled&&!hdrInput&&hdrOutput;}
    bool highQualityPresentation = false; // PS5 ordinary scaling, no AI SR
    bool rgbInput = false;       // allocate direct RGBA ingestion before NGX creation
    bool yuy2Input = false;      // packed Y0 U Y1 V -> linear FP16; never subsample to NV12
    // N1: packed capture ingress (1 BGR24, 2 RGB555, 3 RGB565, 4 UYVY, 5 YVYU).
    // The driver's bytes are uploaded 1:1 and unpacked by the upload shader.
    uint32_t packedInput = 0;
    bool stillImage = false;     // no temporal motion/FG history for a single image
    uint32_t nrWidth=0,nrHeight=0; // zero preserves legacy native working extent
    uint32_t flowWidth=0,flowHeight=0; // zero preserves legacy source-space NVOF extent
    uint32_t fgMultiplier=2;
    engine::FrameGenerationBackend frameGenerationBackend=engine::FrameGenerationBackend::Dlss;
    uint32_t vfgQuality=1;
    uint64_t settingsRevision=1;
    engine::FlowQuality flowQuality=engine::FlowQuality::Balanced;
    engine::OpticalFlowBackend opticalFlowBackend=engine::OpticalFlowBackend::Nvidia;
    bool amdFlowHalfResolution=false;
    engine::ContentRate contentRate=engine::ContentRate::Transport;
    engine::NrSettings model;
    engine::ResidualSettings residual;
    // NR layers, in order. One entry (the default) is the single-layer product
    // path; extra entries stack further Feature-18 instances, each with its own
    // parameters and history. src/pipeline/NrInstance.h.
    std::vector<engine::NrSettings> nrLayersModel;
    std::vector<NrSizePolicy> nrLayersSizePolicy;
    std::vector<Extent> nrLayersExtent; // active instances; full output stays unchanged
    std::vector<engine::ResidualSettings> nrLayersResidual;
    std::vector<bool> nrLayersTemporal;
    // Per-layer anti-flicker tier, parallel to nrLayersTemporal. Empty means the
    // flat nrAntiFlicker applies to every layer.
    std::vector<engine::NrAntiFlicker> nrLayersAntiFlicker;
    std::vector<engine::ProtectionSettings> nrLayersProtection;
    engine::ProtectionSettings protection;
    // Stage-5 output stabiliser (anti-flicker). 0 keeps the pass unallocated and
    // the dispatch skipped, so a default session is byte-identical to a build
    // without it. See shaders/NrHold.hlsl.
    float nrHoldStrength=0.0f;
    float nrHoldTolerance=0.02f;
    // Colour grade (plan v4). When color.enabled is false the stage does not
    // exist: no tables are allocated, no constants are packed and the ingest
    // shaders return their linear RGB untouched.
    engine::ColorSettings color;
    // Instance zero keeps the fused ingress path. Additional instances execute
    // in order on RGBA16F textures; never on a CPU copy of a video frame.
    std::array<engine::ColorSettings,engine::kMaxColorInstances-1> additionalColors{};
    uint32_t additionalColorCount=0;
    // Production builders compile once and use this plan for NR resource
    // sizes and node tail Color routing after the fixed base block. This is
    // NOT an arbitrary-order GPU dispatcher. Direct
    // diagnostic builders may omit it. Neither field crosses the worker ABI.
    std::optional<engine::ChainExecutionPlan> fixedExecutionPlan;
    bool runtimeNodeOrder=false;
    std::string fixedExecutionPlanError;
    // Supported non-ingress positions include the SR/NR boundaries and
    // contiguous NR layers. Other interleaving still fails admission.
    uint32_t nrBeforeSrLayerCount() const {
        if(!runtimeNodeOrder||!fixedExecutionPlan)return 0;
        uint32_t count=0;
        for(uint32_t i=0;i<fixedExecutionPlan->stepCount&&i<fixedExecutionPlan->steps.size();++i){
            const auto type=fixedExecutionPlan->steps[i].type;
            if(type==engine::EffectType::SuperResolution)return count;
            if(type==engine::EffectType::NrEnhance)++count;
        }
        return 0;
    }
    bool splitNrAcrossSr() const {
        const auto before=nrBeforeSrLayerCount();
        return before&&fixedExecutionPlan&&before<fixedExecutionPlan->resourceCounts[size_t(engine::EffectType::NrEnhance)];
    }
    // The source-size prefix has an unenhanced reference before SR. After
    // SR, workRgba already contains that prefix's NR and is not an original.
    bool protectionBeforeSr() const {
        if(!splitNrAcrossSr())return false;
        const auto& plan=*fixedExecutionPlan;
        for(uint32_t i=1;i<plan.stepCount&&i<plan.steps.size();++i){
            if(plan.steps[i].type==engine::EffectType::SuperResolution)return false;
            if(plan.steps[i].type!=engine::EffectType::Protection)continue;
            if(plan.steps[i-1].type!=engine::EffectType::NrEnhance)return false;
            for(uint32_t j=i+1;j<plan.stepCount&&j<plan.steps.size();++j){
                if(plan.steps[j].type==engine::EffectType::SuperResolution)return true;
                if(plan.steps[j].type!=engine::EffectType::Color)return false;
            }
            return false;
        }
        return false;
    }
    // Protection may restore the upstream NR stack before another same-size
    // NR consumes it. A split chain still has no all-NR original after SR.
    uint32_t interNrProtectionTarget() const {
        if(!runtimeNodeOrder||!fixedExecutionPlan)return engine::kMaxNrInstances;
        const auto& plan=*fixedExecutionPlan;bool afterSr=false;
        for(uint32_t i=0;i<plan.stepCount&&i<plan.steps.size();++i){
            if(plan.steps[i].type==engine::EffectType::SuperResolution)afterSr=true;
            if(plan.steps[i].type!=engine::EffectType::Protection)continue;
            if(!i||plan.steps[i-1].type!=engine::EffectType::NrEnhance||
               (afterSr&&splitNrAcrossSr()))return engine::kMaxNrInstances;
            for(uint32_t j=i+1;j<plan.stepCount&&j<plan.steps.size();++j){
                if(plan.steps[j].type==engine::EffectType::NrEnhance)return plan.steps[j].resourceIndex;
                if(plan.steps[j].type!=engine::EffectType::Color)break;
            }
            return engine::kMaxNrInstances;
        }
        return engine::kMaxNrInstances;
    }
    Extent nrFullExtent(size_t resource) const {
        if(runtimeNodeOrder&&fixedExecutionPlan){
            for(uint32_t i=0;i<fixedExecutionPlan->stepCount&&i<fixedExecutionPlan->steps.size();++i){
                const auto& step=fixedExecutionPlan->steps[i];
                if(step.type==engine::EffectType::NrEnhance&&step.resourceIndex==resource)return step.input;
            }
        }
        return nrBeforeSr?Extent{sourceWidth,sourceHeight}:Extent{workWidth,workHeight};
    }
    bool finalNrAfterSr() const { return !nrBeforeSr||splitNrAcrossSr(); }
    bool preSrColor(uint32_t parameter) const {
        if(!runtimeNodeOrder||!fixedExecutionPlan||!nrBeforeSr)return false;
        using engine::EffectType;
        const auto& plan=*fixedExecutionPlan;
        bool afterNr=false;
        for(uint32_t i=0;i<plan.stepCount&&i<plan.steps.size();++i){
            const auto& step=plan.steps[i];
            if(step.type==EffectType::SuperResolution)return false;
            if(step.type==EffectType::NrEnhance)afterNr=true;
            if(step.type!=EffectType::Color||step.parameterIndex!=parameter)continue;
            if(!afterNr)return false;
            for(uint32_t j=i+1;j<plan.stepCount&&j<plan.steps.size();++j){
                if(plan.steps[j].type==EffectType::SuperResolution)return true;
                if(plan.steps[j].type!=EffectType::Color)break;
            }
            return false;
        }
        return false;
    }
    bool preNrColor(uint32_t parameter) const {
        if(!runtimeNodeOrder||!fixedExecutionPlan||(nrBeforeSr&&!splitNrAcrossSr()))return false;
        using engine::EffectType;
        bool afterSr=false;
        for(uint32_t i=0;i<fixedExecutionPlan->stepCount&&i<fixedExecutionPlan->steps.size();++i){
            const auto& step=fixedExecutionPlan->steps[i];
            if(step.type==EffectType::NrEnhance&&afterSr)return false;
            if(step.type==EffectType::SuperResolution)afterSr=true;
            if(step.type!=EffectType::Color||step.parameterIndex!=parameter)continue;
            if(!afterSr)return false;
            for(uint32_t j=i+1;j<fixedExecutionPlan->stepCount&&j<fixedExecutionPlan->steps.size();++j)
                if(fixedExecutionPlan->steps[j].type==EffectType::NrEnhance)return true;
            return false;
        }
        return false;
    }
    // Compact NR resource ordinal consuming this grade. Only a contiguous
    // NR -> [Protection] -> Color(s) -> NR segment is admitted here.
    // Crossing SR needs a separate boundary, not a relaxed rank check.
    uint32_t interNrColorTarget(uint32_t parameter) const {
        if(!runtimeNodeOrder||!fixedExecutionPlan)return engine::kMaxNrInstances;
        using engine::EffectType;
        const auto& plan=*fixedExecutionPlan;
        bool afterNr=false;
        for(uint32_t i=0;i<plan.stepCount&&i<plan.steps.size();++i){
            const auto& step=plan.steps[i];
            if(step.type==EffectType::NrEnhance)afterNr=true;
            else if(step.type!=EffectType::Color&&
                    !(step.type==EffectType::Protection&&interNrProtectionTarget()<engine::kMaxNrInstances))afterNr=false;
            if(step.type!=EffectType::Color||step.parameterIndex!=parameter)continue;
            if(!afterNr)return engine::kMaxNrInstances;
            for(uint32_t j=i+1;j<plan.stepCount&&j<plan.steps.size();++j){
                const auto& next=plan.steps[j];
                if(next.type==EffectType::NrEnhance)return next.resourceIndex;
                if(next.type!=EffectType::Color)break;
            }
            return engine::kMaxNrInstances;
        }
        return engine::kMaxNrInstances;
    }
    bool nodeColor(uint32_t parameter) const {
        return preNrColor(parameter)||preSrColor(parameter)||tailColor(parameter)||interNrColorTarget(parameter)<engine::kMaxNrInstances;
    }
    bool tailColor(uint32_t parameter) const {
        if(!runtimeNodeOrder||!fixedExecutionPlan)return false;
        using engine::EffectType;
        bool afterBase=false;
        for(uint32_t i=0;i<fixedExecutionPlan->stepCount&&i<fixedExecutionPlan->steps.size();++i){
            const auto& step=fixedExecutionPlan->steps[i];
            if(step.type==EffectType::SuperResolution||step.type==EffectType::NrEnhance||step.type==EffectType::Protection)afterBase=true;
            if(step.type!=EffectType::Color||step.parameterIndex!=parameter)continue;
            if(!afterBase)return false;
            for(uint32_t j=i+1;j<fixedExecutionPlan->stepCount&&j<fixedExecutionPlan->steps.size();++j){
                const auto type=fixedExecutionPlan->steps[j].type;
                if(type==EffectType::SuperResolution||type==EffectType::NrEnhance||type==EffectType::Protection)return false;
            }
            return true;
        }
        return false;
    }
    std::string_view validateFixedExecutionPlan() const {
        if(!fixedExecutionPlanError.empty())return fixedExecutionPlanError;
        if(!fixedExecutionPlan)return {};
        using engine::EffectType;
        const auto& plan=*fixedExecutionPlan;
        if(plan.stepCount>plan.steps.size()||plan.output!=Extent{workWidth,workHeight})
            return "fixed execution plan output/count mismatch";
        std::array<uint32_t,engine::effectTypeCount> counts{};
        Extent cursor{sourceWidth,sourceHeight}; int lastRank=-1;
        const bool split=splitNrAcrossSr();
        const bool interProtection=interNrProtectionTarget()<engine::kMaxNrInstances;
        if(interProtection&&(noFeatures||noNgx))return "inter-NR protection requires active NR resources";
        if(split&&(noFeatures||noNgx))return "split NR execution requires active NR resources";
        if(split&&protection.enabled&&!protectionBeforeSr()&&!interProtection)return "split NR protection requires a stage-local original reference";
        bool afterSr=false;
        for(uint32_t i=0;i<plan.stepCount;++i){
            const auto& step=plan.steps[i];const auto type=size_t(step.type);
            if(type>=counts.size())return "unknown fixed execution stage";
            int rank=int(type);
            if(nrBeforeSr){
                if(step.type==EffectType::NrEnhance)rank=1;
                if(step.type==EffectType::Protection)rank=2;
                if(step.type==EffectType::SuperResolution)rank=3;
            }
            if(runtimeNodeOrder){
                if(step.type==EffectType::Protection&&interProtection)rank=nrBeforeSr?1:2;
                if(step.type==EffectType::Color){
                    if(step.parameterIndex>additionalColorCount||step.parameterIndex>=engine::kMaxColorInstances||
                       !(step.parameterIndex?additionalColors[step.parameterIndex-1].enabled:color.enabled))
                        return "node Color parameter index/enabled mismatch";
                    if(tailColor(step.parameterIndex))rank=4;
                    else if(preNrColor(step.parameterIndex))rank=2;
                    else if(preSrColor(step.parameterIndex))rank=3;
                    else if(interNrColorTarget(step.parameterIndex)<engine::kMaxNrInstances)rank=nrBeforeSr?1:2;
                }
                if(step.type==EffectType::VideoHdr)rank=5;
                if(step.type==EffectType::FrameGeneration)rank=6;
            }
            if(split){
                if(step.type==EffectType::Protection){
                    if(!protectionBeforeSr()&&!interProtection)return "split NR protection requires a stage-local original reference";
                    rank=interProtection?1:2;
                }
                if(step.type==EffectType::SuperResolution){rank=3;afterSr=true;}
                if(step.type==EffectType::NrEnhance)rank=afterSr?4:1;
                if(step.type==EffectType::Color){
                    if(tailColor(step.parameterIndex))rank=6;
                    else if(preNrColor(step.parameterIndex))rank=4;
                    else if(preSrColor(step.parameterIndex))rank=2;
                    else if(interNrColorTarget(step.parameterIndex)<engine::kMaxNrInstances)rank=afterSr?4:1;
                }
                if(step.type==EffectType::VideoHdr)rank=7;
                if(step.type==EffectType::FrameGeneration)rank=8;
            }
            if(rank<lastRank)return "ordered node execution is not supported by fixed dispatcher";
            lastRank=rank;
            if(step.input!=cursor||!step.processing.valid()||!step.output.valid()||
               step.resourceIndex!=counts[type]++)return "invalid fixed stage dimensions/resource index";
            if(step.type!=EffectType::SuperResolution&&step.output!=cursor)
                return "fixed non-SR stage changes output dimensions";
            if(step.type==EffectType::NrEnhance){
                const Extent full=split?nrFullExtent(step.resourceIndex):(nrBeforeSr?Extent{sourceWidth,sourceHeight}:Extent{workWidth,workHeight});
                if(!enableNr||step.input!=full||step.resourceIndex>=nrLayersExtent.size()||
                   step.processing!=nrLayersExtent[step.resourceIndex]||step.parameterIndex>=engine::kMaxNrInstances)
                    return "fixed NR plan/resource dimensions mismatch";
            }
            cursor=step.output;
        }
        const size_t expectedNr=enableNr?(nrLayersModel.empty()?1:nrLayersModel.size()):0;
        if(cursor!=plan.output||counts!=plan.resourceCounts||counts[size_t(EffectType::NrEnhance)]!=expectedNr)
            return "fixed execution plan resource count mismatch";
        return {};
    }
    std::wstring runtimeAbsPath; // absolute runtime_local/nvidia path
    // Optional stage instrumentation hook (GPU timing experiments).
    std::function<void(const char*)> stageMark;
    // Product host supplies a cancellable, isolated compatibility startup probe.
    std::function<bool(const EnhanceGraphDesc&,const gfx::D3D12DeviceContext&)> compatibilityPreflight;
    // Output dither step for the 8/10-bit write paths, in coded units. -1 keeps
    // the product default (one LSB while the colour grade is active, nothing
    // otherwise) so ungraded output stays byte-identical; tests and diagnostics
    // can force a value (0 disables it).
    float outputDitherStep=-1.0f;
    // Isolated contract probes only. Unset by every product entry point.
    // Caller retains any supplied resources until graph drain/shutdown.
    std::function<void(NVSDK_NGX_Parameter*,ID3D12Resource*,ID3D12Resource*,uint32_t,uint32_t)> nrParameterProbe;
    // Isolated SR diagnostics only; unset by product entry points. Borrowed
    // working-extent RG16F current->previous pixel motion, ready in SRV state.
    // Caller retains it through graph drain/shutdown, synchronizing any writes.
    ID3D12Resource* srMotionProbe = nullptr;
    // Same borrowed diagnostic contract as srMotionProbe, for FG only.
    ID3D12Resource* fgMotionProbe = nullptr;
    // Isolated 3b experiment: one layer owns a bounded pool on this same
    // snippet session. Not set by any product entry point until proven.
    std::optional<uint32_t> nrAutoPoolLayer;
};

class EnhanceGraph {
public:
    EnhanceGraph(gfx::D3D12DeviceContext& context, gfx::CommandSlotRing& ring,
                 ngx::NgxCoreCache* coreCache = nullptr);
    ~EnhanceGraph();

    EnhanceGraph(const EnhanceGraph&) = delete;
    EnhanceGraph& operator=(const EnhanceGraph&) = delete;

    // Two-phase initialization: the sink (swapchain allocations) must be
    // created between the two phases - static descriptor views are only
    // safe after every allocation in the process has happened.
    bool initialize(const EnhanceGraphDesc& desc);
    // Uploads a .cube payload (size^3 RGB triples, 0..1) into the optional 3D
    // LUT the ingest shader samples. Without a LUT the strength stays 0.
    bool setColorLut(const float* rgb,unsigned size,unsigned instance=0);
    bool createViews();
    // Re-activating an inactive graph must seed a new full paused evaluation;
    // caller still passes reset=true to reset all temporal histories.
    void invalidatePausedResidualCache() noexcept {
        pausedNrCacheValid_=pausedNrResidualOnly_=false;pausedNrFrame_=nullptr;pausedNrSettings_.reset();
    }

    struct FrameOutputs {
        FrameBatch batch;
        uint32_t fgCandidates=0,fgEvaluated=0,fgSkippedBeforeEval=0,fgSkippedForReset=0;
        bool historyReset=false,fgRecovery=false,fgBudgetSeed=false,fgReduced=false;
        ResetReason detectedReset=ResetReason::None;
        bool contentDuplicate=false;int measuredContentRate=0;
        double ptsMs = 0.0;
        uint32_t videoSlot = 0;            // videoFrame[videoSlot] holds this real frame
        uint64_t videoFenceValue = 0;      // ring fence covering the video work
        bool hasGenerated = false;
        double generatedPtsMs = 0.0;       // strict midpoint of prev/current PTS
        uint32_t genSlot = 0;              // genFrame[genSlot] holds the DLSSG output
        uint64_t genFenceValue = 0;
        Microsoft::WRL::ComPtr<ID3D12Fence> videoFenceObject,genFenceObject;
        bool gpuComplete()const {
            return fenceComplete(videoFenceObject.Get(),videoFenceValue)&&
                fenceComplete(genFenceObject.Get(),genFenceValue);
        }
        uint64_t realFrameIndex = 0;
        bool passthrough = false;          // VEYRA_GRAPH_OFF: metadata only, no GPU work
    };

    // Processes one decoded frame. `ptsMs` is the frame PTS in the source
    // stream time base converted to milliseconds by the caller (the graph
    // does not own the demuxer). `reset` marks the first frame of a new
    // temporal epoch (open/seek/...): NVOF/FG history is not consumed.
    // Returns false on hard failure (run verdict must FAIL).
    // Reduced: the full group does not fit its deadline but one midpoint
    // frame does. Run a presentable 2X group WITHOUT resetting history
    // (fg_harness --fg-planar-alt verified 5/1/5 alternation keeps correct
    // interpolation positions). The pair keeps its true A/B PTS; only the
    // number of outputs changes, and it is reported as previewFgMultiplier.
    enum class FgDecision { Skip, Evaluate, Seed, Reduced };
    using FgAdmission=std::function<FgDecision(const FrameBatch&,bool warmingHistory)>;
    // `hardwareSurface` carries the decoded texture for paths whose surface
    // does not travel inside the AVFrame (D3D11VA). It must be provided exactly
    // when frame->format == AV_PIX_FMT_D3D11 and is unused otherwise.
    bool process(const AVFrame* frame, double ptsMs, bool reset, FrameOutputs& out, uint64_t sourceFrameId = 0, const ColorDescription* color = nullptr, const HardwareSurfaceInput* hardwareSurface = nullptr, bool retainReferences = true, const FgAdmission& admitFg = {}, unsigned previewMultiplier = 0, bool pausedResidualRefresh = false);
    bool nextFrameSlotAvailable()const {
        const unsigned slot=unsigned(realFrameIndex_%2);
        if(presentationFences_[slot]&&presentationFences_[slot]->GetCompletedValue()<presentationValues_[slot])return false;
        if(!realLeases_[slot].expired())return false;
        for(unsigned i=slot;i<kGeneratedPoolSlots;i+=2)if(!generatedLeases_[i].expired())return false;
        return true;
    }
    uint64_t presentationReadyFence(unsigned slot,bool generated)const {
        if(generated&&slot<kGeneratedPoolSlots){if(auto lease=generatedLeases_[slot].lock())return lease->readyFence;}
        return uploadFences_[slot%2];
    }
    ID3D12Fence* presentationReadyFenceObject(unsigned slot,bool generated)const;
    void presentationSubmitted(unsigned slot,ID3D12Fence* fence,uint64_t value){
        presentationFences_[slot%2]=fence;presentationValues_[slot%2]=value;
    }
    // Nonblocking. The scheduler polls at a GPU-ready/deadline boundary; no
    // full image readback and no waits inside the graph's individual passes.
    bool resolveGeneration(FrameOutputs& out);
    // Resolve one leased frame without waiting for later MFG subframes.
    // Full-batch consumers (export/file accounting) still use resolveGeneration.
    bool resolveFrame(FrameOutputs& out,uint32_t index);
    bool applySettings(const engine::EnhancementSettings&);
    bool selectAutoNrLevel(unsigned level);
    bool autoNrPoolReady()const{return nrAutoPoolReady_;}
    unsigned autoNrLevel()const{return nrAutoLevel_;}

    // s10 ownership-order teardown: NVOF fence drain must happen BEFORE this
    // call (it needs the ring and out-fence alive). Releases features, NVOF
    // (unregister -> textures -> destroy/unload), NGX params/shim/core, and
    // all graph textures in the proven staged order.
    void shutdown();

    struct Metrics {
        uint64_t nrEvaluateCount = 0;
        uint64_t pausedNrResidualReuses = 0;
        uint64_t srEvaluateCount = 0;
        uint64_t nvofExecuteCount = 0;
        uint64_t amdOfExecuteCount = 0;
        uint64_t gpuDisExecuteCount = 0;
        uint64_t nvofFrameFailures = 0;
        uint64_t fgGeneratedFrames = 0;
        uint64_t fgSubmittedCandidates = 0, fgDisabledFrames = 0;
        uint64_t fgProviderDisabled = 0, fgDuplicateSuppressed = 0, fgBothDisabled = 0;
        uint64_t nrMotionFrames = 0, sceneCutCount = 0, resetCount = 0;
    };
    const Metrics& metrics() const { return metrics_; }
    const std::string& mvecSource() const { return mvecSource_; }
    bool nrCreated() const { return !nrInstances_.empty() && (nrInstances_.front()->handle() != nullptr || (nrInstances_.front()->amd && nrInstances_.front()->amd->ready())); }
    bool initialized() const { return initialized_; }
    uint32_t sourceWidth() const { return srcW_; }
    uint32_t sourceHeight() const { return srcH_; }
    uint32_t workWidth() const { return workW_; }
    uint32_t workHeight() const { return workH_; }
    uint32_t nrWidth() const {return nrW_;}
    uint32_t nrLayerCount() const {return uint32_t(nrInstances_.size());}
    uint32_t nrHeight() const {return nrH_;}
    uint32_t flowWidth() const {return nvofW_;}
    uint32_t flowHeight() const {return nvofH_;}
    uint32_t actualFlowPerf() const;
    diagnostics::FrameMetrics gpuMetrics(){gpuTimer_.collect(contextFence());return gpuTimer_.last();}
    std::vector<diagnostics::GpuFrameTiming> takeGpuTimings(){gpuTimer_.collect(contextFence());return gpuTimer_.takeCompleted();}
    void recordGpuTimings(){gpuTimer_.recordCompleted();}
    ID3D12Fence* contextFence() const;
    ID3D12Resource* baseReference(unsigned slot=0)const{return baseReferences_[slot%2].Get();}
    ID3D12Resource* sourceReference(unsigned slot=0)const{return sourceReferences_[slot%2].Get();}
    bool srEnabled() const { return srEnabled_; }
    bool nrEnabled() const { return nrEnabled_; }
    bool fgEnabled() const { return fgEnabled_; }
    bool hdrOutput() const { return desc_.hdrOutput; }
    bool videoHdrActive() const {return desc_.convertVideoHdr();}
    bool hdr10Output() const { return desc_.hdrOutput && (desc_.enableFg||desc_.hdrOutputMode==engine::HdrOutputMode::Hdr10); }
    DXGI_FORMAT outputFormat() const { return hdr10Output()?DXGI_FORMAT_R10G10B10A2_UNORM:desc_.hdrOutput?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM; }
    bool highQualityPresentation() const { return desc_.highQualityPresentation; }
    bool xessEnabled() const { return desc_.enableFg && !desc_.noFeatures && !desc_.stillImage && desc_.frameGenerationBackend==engine::FrameGenerationBackend::XeSS; }
    bool fsrEnabled() const { return desc_.enableFg && !desc_.noFeatures && !desc_.stillImage && engine::fsrFrameGeneration(desc_.frameGenerationBackend); }
    bool fsrActive() const;
    const char* fsrProviderVersion() const;
    // XeSS generates in the present sink; FSR and DLSSG generate in the graph.
    // All backends consume the same guidance motion texture.
    bool presentSinkFg() const { return xessEnabled(); }
    // AMD FSR upscaling replaces the SR stage; it is a FidelityFX effect, not
    // an NGX feature, so it also works on non-NVIDIA adapters.
    bool fsrSrRequested() const { return desc_.enableSr && !desc_.stillImage && desc_.videoSrQuality==engine::kVideoSrFsr; }
    // Out of line: the backend type is only forward declared here.
    bool fsrSrEnabled() const;
    // Requested output multiplier (2 = one generated frame). Consumed by the
    // present sink so the XeSS provider knows how many frames to generate.
    uint32_t fgMultiplier() const { return desc_.fgMultiplier; }
    ID3D12Resource* presentMotion(uint32_t slot) const { return presentMotion_[slot%2].Get(); }
    ID3D12Resource* presentDepth() const { return depthTex_.Get(); }
    bool presentMotionValid(uint32_t slot) const { return presentMotionValid_[slot%2]; }
    uint64_t motionPreviousSource(uint32_t slot) const { return motionPreviousSource_[slot%2]; }
    uint64_t lastNvofSignal() const;
    bool usesComputeQueue() const;

    // Present-side access to the produced frame slots (probe sink path).
    ID3D12Resource* videoFrameResource(uint32_t slot) const;
    ID3D12Resource* generatedFrameResource(uint32_t slot) const;
    // Guidance outputs (quality statistics; diagnostic readback only).
    ID3D12Resource* flowResource() const { return flowTex_.Get(); }
    ID3D12Resource* confidenceResource() const { return confTex_.Get(); }
    // Test-only borrowed ingress output. Read after process, restore NON_PIXEL_SHADER_RESOURCE.
    ID3D12Resource* diagnosticLinearInput() const { return srcRgba_.Get(); }
    // Offline diagnostics only. Borrowed after process; restore NON_PIXEL_SHADER_RESOURCE.
    ID3D12Resource* diagnosticNrBase() const { return desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(); }
    // Last layer's pre-temporal result; stack protection is applied afterwards.
    ID3D12Resource* diagnosticNrRaw() const { return !nrInstances_.empty()&&nrInstances_.back()->stabilised?nrInstances_.back()->temporal().raw():(!nrInstances_.empty()?nrInstances_.back()->fullTarget():residualRgba_.Get()); }
    ID3D12Resource* diagnosticNrFiltered() const { return residualRgba_.Get(); }
    ID3D12Resource* diagnosticPreSrProtection() const { return preSrProtectionRgba_.Get(); }
    ID3D12Resource* diagnosticInterNrProtection() const { return interNrProtectionRgba_.Get(); }
    ID3D12Resource* diagnosticVideoSrInput() const { return videoSrInput_.Get(); }
    ID3D12Resource* diagnosticVideoSrOutput() const { return videoSrOutput_.Get(); }
    ID3D12Resource* diagnosticNrLayerInput(size_t index) const { return index < nrInstances_.size() ? nrInstances_[index]->input() : nullptr; }
    ID3D12Resource* diagnosticNrLayerOutput(size_t index) const { return index < nrInstances_.size() ? nrInstances_[index]->fullTarget() : nullptr; }
    engine::NrLayerSettings diagnosticNrLayerSettings(size_t index) const {
        if(index>=nrInstances_.size())return {};
        const auto& n=*nrInstances_[index];
        const bool beforeSr=desc_.nrBeforeSr&&(!desc_.splitNrAcrossSr()||index<desc_.nrBeforeSrLayerCount());
        return {n.model,n.residualSettings,desc_.nrRuntime,n.temporalEnabled,beforeSr,n.enabled};
    }
    uint64_t diagnosticNrInputRevision(size_t index) const { return index<nrInstances_.size()?nrInstances_[index]->inputRevision:0; }
    D3D12_RESOURCE_STATES diagnosticResourceState(ID3D12Resource* resource) const { return tracker_.get(resource); }
    // Non-empty when the colour stage refused the referenced LUT (input space
    // does not match the content domain). The engine surfaces this in the
    // status panel so the refusal is visible, not only logged.
    const std::wstring& colorLutNotice() const { return colorLutNotice_; }
    // True when the colour stage exists in this graph (master switch on and not
    // neutral). The encoder uses it to dither its 8/10-bit conversion output.
    bool colorGradeActive() const { return colorActive_||additionalColorActiveCount_!=0||tailColorActiveCount_!=0||preNrColorActiveCount_!=0||preSrColorActiveCount_!=0||interNrColorActiveCount_!=0; }
    // Effective dither step for the 8/10-bit output paths (0 = no dither).
    float outputDitherStep() const;
    // Isolated diagnostics only: existing constant guidance, borrowed lifetime.
    // A caller writing before the first real frame must restore COMMON state.
    ID3D12Resource* diagnosticDepthResource(bool frameGeneration) const { return frameGeneration?depthTex_.Get():nrZeroDepth_.Get(); }
    uint32_t nvofRawWidth() const { return rawW_; }
    uint32_t nvofRawHeight() const { return rawH_; }

    // Teardown/diagnostics accessors: the s10 NVOF out-fence drain runs while
    // the ring/fence/event are alive, i.e. BEFORE shutdown().
    ID3D12Fence* nvofOutFenceResource() const { return nvofOutFence_.Get(); }
    HANDLE nvofOutEventHandle() const { return nvofOutEvent_; }
    bool nvofSessionInitialized() const;
    uint64_t nvofNextOutValue() const;
    uint64_t nrCreateResult() const { return nrResult_; }
    uint64_t failedNgxResult() const;
    const std::wstring& driverRequirementNotice() const {return driverRequirementNotice_;}
    bool fgCapabilityAvailable() const { return fgCapsAvailable_; }
    int fgMultiFrameCountMax() const { return fgMultiFrameMax_; }
    // Scenario toggles (probe/UI): gates only; the feature handles stay alive.
    // setNrEnabled also refreshes the section-5 blit SRV (slot 2) so the
    // videoFrame source follows workRgba (NR off) or finalRgba (NR on).
    void setNrEnabled(bool on);
    void setFgEnabled(bool on) { fgEnabled_ = on && !desc_.stillImage; }
    bool fgCreated() const;
    engine::FailedBackend failedBackend() const { return failedBackend_; }

private:
    engine::FailedBackend failedBackend_=engine::FailedBackend::None;
    bool createResources();
    bool initZeroAndDepthTextures();
    bool initNvof();
    bool initNgxFeatures();
    bool initFsrSr();
    bool initFsrFg();
    bool initVfg();
    std::unique_ptr<VfgBackend> vfgBackend_;
    // RTX 40 series: opens the Blackwell-only multi-frame gate in the mapped
    // DLSS-G runtime. Never touched on any other architecture.
    void applyAdaMfgUnlock();
    void applyAmpereMfgUnlock();
    // RTX 30 only: must run before the NGX core initializes the provider, which
    // resolves NvAPI_GPU_GetArchInfo once and caches the architecture decision.
    void prepareAmpereFgSpoof();
    bool ampereSpoofed_ = false;
    std::unique_ptr<ngx::FgCompatibilitySession> fgCompatibility_;
    bool createComputePasses();

    gfx::D3D12DeviceContext& context_;
    gfx::CommandSlotRing& ring_;
    ngx::NgxCoreCache* coreCache_ = nullptr; // non-owning; device session outlives graph
    EnhanceGraphDesc desc_{};
    bool initialized_ = false;

    uint32_t srcW_ = 0, srcH_ = 0, workW_ = 3840, workH_ = 2160;
    uint32_t nrW_=0,nrH_=0;
    uint32_t nvofW_ = 0, nvofH_ = 0, rawW_ = 0, rawH_ = 0, nvofGrid_ = 4, selectedGrid_ = 4;
    bool srEnabled_ = false, nrEnabled_ = false, fgEnabled_ = false, nvofStandalone_ = false;
    size_t lumaPitch_ = 0, chromaPitch_ = 0, lumaSize_ = 0, chromaSize_ = 0, dPitch_ = 0;

    // Uploads + textures (creation order matters for teardown).
    ComPtr<ID3D12Resource> upLuma_[2];
    ComPtr<ID3D12Resource> upChroma_[2];
    ComPtr<ID3D12Resource> upDepth_;
    ComPtr<ID3D12Resource> upZeroDepth_;
    ComPtr<ID3D12Resource> upZeroMotion_;
    ComPtr<ID3D12Resource> lumaTex_;
    ComPtr<ID3D12Resource> chromaTex_;
    ComPtr<ID3D12Resource> srcRgba_;
    // Preserve the ungraded decode at FP32 when the first enabled grade is an
    // independent instance; later passes still use the RGBA16F chain.
    ComPtr<ID3D12Resource> preGradeRgba_;
    ComPtr<ID3D12Resource> rgbTex_,upRgb_[2];
    uint8_t* mappedRgb_[2]={};
    size_t rgbPitch_=0;
    ComPtr<ID3D12Resource> workRgba_;
    ComPtr<ID3D12Resource> videoSrInput_,videoSrOutput_;
    ComPtr<ID3D12Resource> videoHdrInput_,videoHdrOutput_;
    ComPtr<ID3D12Resource> residualRgba_,nrFlow_,baseFlow_;
    ComPtr<ID3D12Resource> preSrProtectionRgba_;
    ComPtr<ID3D12Resource> interNrProtectionRgba_;
    // NR layers. One entry means the single-layer product path; the probe in
    // tools/nr_probe verified several handles coexist on one snippet session.
    std::vector<std::unique_ptr<NrInstance>> nrInstances_;
    std::array<std::unique_ptr<NrInstance>,5> nrAutoPool_;
    unsigned nrAutoLevel_=0;
    bool nrAutoPoolReady_=false,nrAutoResetPending_=false;
    bool createNrFeature(NrInstance& layer);
    bool createAutoNrPool();
    bool lmxxfNr_ = false;
    bool submitGraph(uint32_t slot);
    // The layer count the current graph was built with, so a settings change
    // that adds or removes a layer is detected as a rebuild.
    uint32_t nrLayerCount_=1;
    ComPtr<ID3D12Resource> presentMotion_[2];
    bool presentMotionValid_[2]={};
    uint64_t motionPreviousSource_[2]={},previousSource_=0;
    ComPtr<ID3D12Resource> proxyTex_;
    ComPtr<ID3D12Resource> neuralTex_;
    ComPtr<ID3D12Resource> finalRgba_;
    ComPtr<ID3D12Resource> videoFrame_[2];
    ComPtr<ID3D12Resource> confTex_;
    ComPtr<ID3D12Resource> flowTex_;
    ComPtr<ID3D12Resource> depthTex_;
    ComPtr<ID3D12Resource> genFrame_[kGeneratedPoolSlots];
    ComPtr<ID3D12Resource> fgDisable_[kGeneratedPoolSlots],fgDisableReadback_[kGeneratedPoolSlots],fgDisableInit_;
    std::array<const volatile uint8_t*,kGeneratedPoolSlots> fgDisableMapped_{};
    ComPtr<ID3D12Resource> nrZeroMotion_;
    ComPtr<ID3D12Resource> nrZeroDepth_;
    ComPtr<ID3D12Resource> nvofRawTex_;
    ComPtr<ID3D12Resource> nvofCostTex_;
    ComPtr<ID3D12Resource> nvofInA_;
    ComPtr<ID3D12Resource> nvofInB_;
    uint8_t* mappedLuma_[2] = {};
    uint8_t* mappedChroma_[2] = {};
    std::vector<uint8_t> nv12Buf_;

    ComputePass yuvPass_, encPass_, decPass_, blitPass_, uploadPass_, densifyPass_;
    float toneMapPeakNits_=0; // latched per graph/source; never varies with frame brightness
    ComputePass rgbPass_,hdrVideoSrPass_;
    ComputePass downsamplePass_,residualPass_,stackProtectionPass_,flowAdaptPass_;
    // Stage-5 output stabiliser (anti-flicker). Constructed only when the
    // setting is non-zero; a default (0) session never allocates it and the
    // dispatch is skipped, so the disabled path is unchanged.
    std::unique_ptr<NrHoldPass> nrHoldPass_;
    // 纹理 / 清晰度 / 去朦胧 of the list grade fused into input conversion (srcRgba_).
    ColorDetailPass colorDetail_;
    // The texture the rest of the graph reads as "the NR result". Without the
    // stabiliser this is residualRgba_; with it, the stabilised copy the pass
    // produces. Set once per frame, read by the SR/FG/tail bindings below.
    ID3D12Resource* nrOutputView_=nullptr;
    bool createNrHoldPass();
    // --- colour grade (v4): CPU-baked tables read by the ingest shaders -----
    bool colorActive_=false;
    std::wstring colorLutNotice_;
    bool colorDirty_=true;
    ColorGradeTables colorTables_;
    ComPtr<ID3D12Resource> colorCurveTex_,colorHueTex_,colorLumTex_,colorLutTex_;
    // Only write the upload resources of the acquired (completed) command slot.
    std::vector<std::array<ComPtr<ID3D12Resource>,3>> colorTableUploads_;
    unsigned colorLutSize_=0;
    std::vector<float> colorLutUpload_;
    unsigned colorLutPending_=0;
    std::array<ComPtr<ID3D12Resource>,2> colorLutStaging_{};
    unsigned colorLutStagingSlot_=0;
    bool createColorResources();
    bool createAdditionalColorResources();
    bool createNodeColorResources();
    std::array<std::unique_ptr<ColorGradeInstance>,engine::kMaxColorInstances-1> additionalColorInstances_{};
    std::array<ComPtr<ID3D12Resource>,2> colorScratch_{};
    unsigned additionalColorActiveCount_=0;
    // Indexed by settings identity (including disabled slots), not compact
    // resource ordinal. Only enabled non-ingress nodes allocate resources.
    std::array<std::unique_ptr<ColorGradeInstance>,engine::kMaxColorInstances> nodeColorInstances_{};
    std::array<ComPtr<ID3D12Resource>,engine::kMaxColorInstances> nodeColorOutputs_{};
    unsigned tailColorActiveCount_=0,preNrColorActiveCount_=0,preSrColorActiveCount_=0,interNrColorActiveCount_=0;
    bool tailColorExecutionLogged_=false,preNrColorExecutionLogged_=false;
    bool preSrColorExecutionLogged_=false;
    bool interNrColorExecutionLogged_=false;
    ID3D12Resource* tailColorOutput_=nullptr; // borrowed from nodeColorOutputs_
    ID3D12Resource* preNrColorOutput_=nullptr;
    ID3D12Resource* preSrColorOutput_=nullptr;
    std::array<ID3D12Resource*,engine::kMaxNrInstances> interNrColorOutputs_{};
    ID3D12Resource* nrBaseInput() const { return desc_.nrBeforeSr?srcRgba_.Get():(preNrColorOutput_?preNrColorOutput_:workRgba_.Get()); }
    ID3D12Resource* preSrBaseInput() const {
        if(preSrProtectionRgba_)return preSrProtectionRgba_.Get();
        const auto before=desc_.nrBeforeSrLayerCount();
        return desc_.splitNrAcrossSr()&&before<=nrInstances_.size()?nrInstances_[before-1]->fullTarget():residualRgba_.Get();
    }
    ID3D12Resource* srStageInput() const {
        return preSrColorOutput_?preSrColorOutput_:(desc_.nrBeforeSr?preSrBaseInput():srcRgba_.Get());
    }
    ID3D12Resource* nrLayerBase(size_t layer) const {
        if(layer<interNrColorOutputs_.size()&&interNrColorOutputs_[layer])return interNrColorOutputs_[layer];
        if(interNrProtectionRgba_&&layer==desc_.interNrProtectionTarget())return interNrProtectionRgba_.Get();
        if(desc_.splitNrAcrossSr()&&layer==desc_.nrBeforeSrLayerCount())return preNrColorOutput_?preNrColorOutput_:workRgba_.Get();
        return layer?nrInstances_[layer-1]->fullTarget():nrBaseInput();
    }
    void refreshColorTables();
    bool uploadColorTables(ID3D12GraphicsCommandList* list,uint32_t slot);
    DescriptorStager stager_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sourceReferences_[2],baseReferences_[2];
    StateTracker tracker_;
    engine::ContentCadence cadence_;
    diagnostics::GpuTimer gpuTimer_;
    SwsContext* nv12Ctx_ = nullptr;

    // NVOF + NGX (owned by the graph, render-thread only).
    std::unique_ptr<ngx::NvOfSession> nvof_;
    ComPtr<ID3D12Fence> nvofOutFence_;
    HANDLE nvofOutEvent_ = nullptr;
    std::shared_ptr<ngx::NgxCoreHost> coreHost_;
    std::unique_ptr<ngx::DlssNrRuntimeAdapter> nrAdapter_;
    std::unique_ptr<ngx::DlssSrBackend> srBackend_;
    std::unique_ptr<guidance::AmdOpticalFlow> amdOf_;
    std::unique_ptr<guidance::GpuDisOpticalFlow> gpuDis_;
    std::unique_ptr<ngx::VideoSrBackend> videoSrBackend_;
    std::unique_ptr<ngx::TrueHdrBackend> videoHdrBackend_;
    std::unique_ptr<ngx::DlssFgBackend> fgBackend_;
    std::unique_ptr<gfx::FsrFgPresenter> fsrFgBackend_;
    std::unique_ptr<gfx::FsrSrBackend> fsrSrBackend_;
    ComPtr<ID3D12Resource> fsrSrDepth_, upFsrSrDepth_, fsrSrZeroMotion_, upFsrSrZeroMotion_;
    size_t fsrSrDepthPitch_=0;
    NVSDK_NGX_Parameter* ngxParams_ = nullptr;
    uint64_t nrResult_ = 0;
    uint64_t fgInitResult_ = 0;
    std::wstring driverRequirementNotice_;
    uint32_t nrSeh_ = 0;
    bool fgCapsAvailable_ = false;
    int fgMultiFrameMax_ = 0;

    // Per-run state.
    uint64_t realFrameIndex_ = 0;
    // Only a successful explicit paused/still-frame reset may seed this.
    // Ordinary playback, source changes, failures and non-residual edits
    // invalidate it. No cross-frame/duplicate-source reuse is performed.
    bool pausedNrCacheValid_=false,pausedNrResidualOnly_=false;
    const AVFrame* pausedNrFrame_=nullptr;
    uint64_t pausedNrSourceId_=0;
    double pausedNrPts_=0;
    std::optional<engine::EnhancementSettings> lastAppliedSettings_,pausedNrSettings_;
    uint64_t epoch_ = 0;
    std::weak_ptr<FrameLease> realLeases_[2],generatedLeases_[kGeneratedPoolSlots];
    uint32_t nextListSlot_ = 0;
    uint64_t uploadFences_[2] = {};
    std::array<Microsoft::WRL::ComPtr<ID3D12Fence>,2> presentationFences_;
    std::array<uint64_t,2> presentationValues_{};
    // FFmpeg may recycle a hardware surface as soon as its AVFrame is freed.
    // Retain each imported surface until our last consumer fence completes.
    std::shared_ptr<AVFrame> hardwareInputFrames_[2];
    core::SceneCadenceAnalyzer scene_;
    std::vector<uint8_t> previousLuma_;
    // Reused per-frame scratch (sweep A4/A5): diagnostic context and the
    // 64x36 luma sample / 256-bin histogram used by scene/cadence analysis.
    diagnostics::DiagnosticEvent frameDiagnostic_;
    uint32_t diagnosticFlowPerf_=~0u;
    std::vector<uint8_t> lumaSample_;
    std::vector<double> lumaHistogram_=std::vector<double>(256,0.0);
    double prevPtsMs_ = -1.0;
    bool prevValid_ = false;
    bool fgHistorySkipped_ = false;
    Metrics metrics_{};
    std::string mvecSource_ = "nvof";
};

} // namespace veyra::pipeline
