#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/pipeline/ResolutionPlan.h"
namespace veyra::diagnostics {
// NrLayer*/ColorNode* are per-instance node spans nested inside the stage
// spans above; they are never merged into enhancementProcessingMs.
enum class GpuStage { Color, Sr, Flow, Nr, Residual, Fg1, Fg2, Fg3, Fg4, Fg5, Fg6, Fg7, FgBatch, Blit, VideoHdr,
    NrLayer0, NrLayer1, NrLayer2, NrLayer3,
    ColorNode0, ColorNode1, ColorNode2, ColorNode3, ColorNode4, ColorNode5, Count };
static_assert(unsigned(GpuStage::Fg5)-unsigned(GpuStage::Fg1)==4);
static_assert(unsigned(GpuStage::FgBatch)>unsigned(GpuStage::Fg7));
static_assert(unsigned(GpuStage::Count)<=32,"GpuTimer keeps one mask bit per stage");
inline constexpr unsigned kTimedNrLayers=4,kTimedColorNodes=6;
// NR layers are indexed among EXECUTING layers; Color by parameter ordinal.
constexpr GpuStage nrLayerStage(unsigned layer){return GpuStage(unsigned(GpuStage::NrLayer0)+layer);}
constexpr GpuStage colorNodeStage(unsigned parameter){return GpuStage(unsigned(GpuStage::ColorNode0)+parameter);}
enum class SampleState { NotExecuted, Pending, Measured, Unavailable };
enum class CpuStage { Decode, Submit, SlotWait, ReadyWait, DeadlineWait, Present, DecodedQueue, EnhancementDelayEstimate, Count };
enum class PairTiming { ArrivalInterval, GeneratedFromA, GeneratedFromB, Count };
enum class ResetStage { Drain, Destroy, Create, Warmup, FirstValid, Count };
enum class ResetOutcome { InProgress, Completed, Failed, RolledBack, Cancelled };
struct TimingAggregate {std::optional<double> mean,p95;uint64_t samples=0;};
struct GpuSample {SampleState state=SampleState::NotExecuted;std::optional<double> milliseconds;uint64_t begin=0,end=0,frequency=0;};
struct GpuFrameTiming {pipeline::FrameIdentity identity;std::array<GpuSample,size_t(GpuStage::Count)> gpu{};};
// All graph stage timestamps use the direct queue's clock. The envelope
// includes its dependency waits but excludes a late CPU completion poll.
inline std::optional<double> graphExecutionSpanMs(const GpuFrameTiming& frame){
    uint64_t begin=UINT64_MAX,end=0,frequency=0;
    for(size_t i=0;i<size_t(GpuStage::Blit);++i){
        const auto& s=frame.gpu[i];
        if(s.state!=SampleState::Measured)continue;
        if(!s.frequency||s.end<s.begin||(frequency&&frequency!=s.frequency))return {};
        frequency=s.frequency;begin=std::min(begin,s.begin);end=std::max(end,s.end);
    }
    if(!frequency||begin==UINT64_MAX||end<begin)return {};
    return double(end-begin)*1000/frequency;
}
// Live presentation is asynchronous. Keep the identity of the window that
// owns these counters so a finished job from a prior reset cannot be reported
// as work performed by the current settings or temporal epoch.
struct FrameFlowIdentity {
    uint64_t sessionId=0;
    pipeline::FrameIdentity frame;
    uint64_t batchId=0,readyFence=0,consumerFence=0;
    bool sameWindow(uint64_t session,const pipeline::FrameIdentity& candidate)const{
        return sessionId==session&&frame.epoch==candidate.epoch&&frame.settingsRevision==candidate.settingsRevision;
    }
};
struct FrameFlowCounters {
    uint64_t captureReceived=0,mailboxOverwritten=0;
    uint64_t sourceAccepted=0,sourceSkippedBeforeGraph=0,realSubmitted=0;
    // Realtime file preview only: decoded source frames whose enhancement
    // opportunity was dropped (PTS window fully passed) before the graph.
    uint64_t previewSkippedBeforeGraph=0;
    uint64_t fgCandidate=0,fgEvaluated=0,fgSkippedBeforeEval=0,fgReadyValid=0,fgInvalid=0,fgWarmup=0,fgSkippedForReset=0,fgReduced=0;
    uint64_t xessSdkGenerated=0,xessSdkPresented=0,realReady=0;
    uint64_t realPresented=0,generatedPresented=0,generatedExpiredAfterEval=0,cancelledBeforePresent=0;
    uint64_t historyResets=0,captureDropResets=0,settingsResets=0;
    uint32_t commandSlotsInFlight=0,commandSlotHighWater=0,presentationBatchHighWater=0;
};
struct FrameFlowMetrics {
    int64_t lastReady100ns=0,lastPresent100ns=0,lastSubmit100ns=0,lastFgRejected100ns=0;
    FrameFlowIdentity latest;
    FrameFlowCounters counters;
    std::optional<double> slotReuseWaitMs,captureArrivalToPresentReturnMs,gpuReadyWaitMs,deadlineWaitMs;
    uint64_t slotReuseWaitCount=0;
    double validGeneratedFps=0,presentSubmitFps=0,sourceCompletedFps=0,outputCompletedFps=0,xessSdkSubmitFps=0;
    double realPresentFps=0,generatedPresentFps=0;
    bool rateWindowReady=false;
    std::optional<double> softwareLatencyMs,softwareLatencyP95Ms,slotWaitPerFrameMs;
    uint64_t latencySamples=0;
    uint32_t pendingOutputFrames=0; // valid frame opportunities retained by presenter jobs
    std::array<TimingAggregate,size_t(GpuStage::Count)> gpuTiming;
    TimingAggregate enhancementProcessing; // same-frame measured enhancement intervals, excluding presentation
    std::array<TimingAggregate,size_t(CpuStage::Count)> cpuTiming;
    std::array<TimingAggregate,size_t(PairTiming::Count)> pairTiming;
    uint64_t pairSourceA=0,pairSourceB=0;
    bool pairCaptureCallbacks=false;
    uint64_t timingOverflow=0;
    struct ResetRecord {
        uint64_t sessionId=0, settingsRevision=0, epoch=0, sourceFrameId=0;
        uint8_t reason=0;
        bool rebuilt=false;
        ResetOutcome outcome=ResetOutcome::InProgress;
        std::array<std::optional<double>,size_t(ResetStage::Count)> stageMs{};
        std::optional<double> totalMs;
    } reset;
};
struct FrameMetrics {
    pipeline::FrameIdentity identity;
    pipeline::ResolutionPlan resolution;
    std::array<GpuSample,size_t(GpuStage::Count)> gpu{};
    std::optional<double> decodeCpuMs,submitCpuMs,gpuWaitCpuMs,deadlineWaitCpuMs,presentCpuMs,displayFps;
    uint64_t sourceFrames=0,validGenerated=0,submitted=0,expired=0;
    uint32_t queueWatermark=0;
    FrameFlowMetrics flow;
};
}
