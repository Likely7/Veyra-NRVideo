#include "veyra/engine/EngineController.h"
#include "veyra/engine/PreviewFrameReadiness.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/HdrDisplayState.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/DeadlineWait.h"
#include "veyra/engine/EnhancementDelayEstimate.h"
#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/FrameFlowWindow.h"
#include "veyra/engine/LiveFgAdmission.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include "veyra/engine/LivePairLatency.h"
#include "veyra/engine/RealtimePreviewScheduling.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/ScreenCaptureSource.h"
#ifdef VEYRA_ENABLE_REMOTEPLAY
#include "veyra/source/RemotePlaySessionSource.h"
#endif
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResetCoordinator.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include "veyra/ngx/AmpereMfgUnlock.h"
#include "veyra/ngx/DlssgTransfusion.h"
#include "veyra/diagnostics/ResetCause.h"
#include "veyra/diagnostics/CpuStallTrace.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/PresentationHooks.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/PosterFrame.h"
#include <avrt.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <limits>
#include <cmath>
#include <deque>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
using Clock=std::chrono::steady_clock;
namespace {
double elapsedMs(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
int64_t monotonic100ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;}
struct OnExit {std::function<void()> action;~OnExit(){action();}};
void logFrameFlow(const diagnostics::FrameFlowMetrics& m,const char* state){
    const auto& c=m.counters;const auto& id=m.latest;
    if(c.xessSdkPresented)veyra::log::info("provider-flow",std::format("state={} session={} revision={} sdkPresented={} sdkGenerated={} sdkSubmitFps={:.2f} measurement=provider-report-not-scanout",state,id.sessionId,id.frame.settingsRevision,c.xessSdkPresented,c.xessSdkGenerated,m.xessSdkSubmitFps));
    const auto& r=m.reset; const auto ms=[&](diagnostics::ResetStage s){return r.stageMs[size_t(s)].value_or(-1.0);};
    veyra::log::info("frame-flow",std::format("state={} session={} revision={} epoch={} source={} batch={} readyFence={} consumerFence={} captureReceived={} mailboxOverwritten={} sourceAccepted={} sourceSkippedBeforeGraph={} realSubmitted={} fgCandidate={} fgSkippedBeforeEval={} fgEvaluated={} fgWarmup={} fgSkippedForReset={} fgReduced={} fgReadyValid={} fgInvalid={} realPresented={} generatedPresented={} generatedExpiredAfterEval={} cancelledBeforePresent={} commandSlots={} commandHighWater={} presentationHighWater={} slotWaitCount={} slotWaitMs={:.3f} generatedFps={:.2f} presentSubmitFps={:.2f} resetRevision={} resetEpoch={} resetReason={} resetOutcome={} resetDrainMs={:.3f} resetDestroyMs={:.3f} resetCreateMs={:.3f} resetWarmupMs={:.3f} resetFirstValidMs={:.3f} resetTotalMs={:.3f}",state,id.sessionId,id.frame.settingsRevision,id.frame.epoch,id.frame.sourceFrameId,id.batchId,id.readyFence,id.consumerFence,c.captureReceived,c.mailboxOverwritten,c.sourceAccepted,c.sourceSkippedBeforeGraph,c.realSubmitted,c.fgCandidate,c.fgSkippedBeforeEval,c.fgEvaluated,c.fgWarmup,c.fgSkippedForReset,c.fgReduced,c.fgReadyValid,c.fgInvalid,c.realPresented,c.generatedPresented,c.generatedExpiredAfterEval,c.cancelledBeforePresent,c.commandSlotsInFlight,c.commandSlotHighWater,c.presentationBatchHighWater,m.slotReuseWaitCount,m.slotReuseWaitMs.value_or(0),m.validGeneratedFps,m.presentSubmitFps,r.settingsRevision,r.epoch,unsigned(r.reason),unsigned(r.outcome),ms(diagnostics::ResetStage::Drain),ms(diagnostics::ResetStage::Destroy),ms(diagnostics::ResetStage::Create),ms(diagnostics::ResetStage::Warmup),ms(diagnostics::ResetStage::FirstValid),r.totalMs.value_or(-1.0)));
}
}
EngineController::EngineController(){worker_=std::thread(&EngineController::dispatch,this);}
EngineController::~EngineController(){ {std::lock_guard lock(mutex_);shutdown_=true;pending_={};stop_=true;}wake_.notify_one();if(worker_.joinable())worker_.join(); }
void EngineController::dispatch(){
    for(;;){std::function<void()> task;{std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return shutdown_||bool(pending_);});if(shutdown_)break;task=std::move(pending_);pending_={};busy_=true;stop_=false;}
        try{task();}catch(const std::exception&){status(L"任务异常，已停止；请查看诊断",true);}
        {std::lock_guard lock(mutex_);busy_=false;snapshot_.running=false;if(snapshot_.transport==TransportState::Stopping)snapshot_.transport=TransportState::Empty;}
    }
}
void EngineController::post(std::function<void()> task){ {std::lock_guard lock(mutex_);stop_=true;pending_=std::move(task);}wake_.notify_one(); }
bool EngineController::idle()const{std::lock_guard lock(mutex_);return !busy_&&!pending_;}
bool EngineController::setPlaybackRate(double rate){
    if(rate!=1.0&&rate!=1.5&&rate!=2.0&&rate!=3.0)return false;
    std::lock_guard lock(mutex_);
    if(!snapshot_.running||snapshot_.capture||snapshot_.image)return false;
    if(requestedPlaybackRate_==rate)return true;
    requestedPlaybackRate_=rate;snapshot_.playbackRate=rate;
    if(seekSeconds_<0){snapshot_.seekTarget=snapshot_.position;++snapshot_.seekRequested;seekSeconds_=snapshot_.position;}
    veyra::log::info("playback-rate",std::format("requested {:.2f}x at {:.3f}s",rate,snapshot_.position));
    return true;
}
void EngineController::setFullscreenMemoryProtection(bool enabled){
    fullscreenMemoryProtection_=enabled;
    if(!enabled){std::lock_guard lock(mutex_);snapshot_.vramFullscreenUnsafe=false;}
}
void EngineController::open(HWND video,const std::wstring& path,PlayerOptions opts){
    const bool captureReplay=opts.captureReplayForTest;
    // Diagnostics-only PlayerOptions fields never enter EnhancementSettings;
    // keep them across the snapshot round-trip below.
    const bool captureCpuUnpack=opts.captureCpuUnpack;
    const bool softwareDecode=opts.softwareDecode;const bool hardwareDecodeOnly=opts.hardwareDecodeOnly;
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_);}
    opts.captureReplayForTest=captureReplay;
    opts.captureCpuUnpack=captureCpuUnpack;
    opts.softwareDecode=softwareDecode;opts.hardwareDecodeOnly=hardwareDecodeOnly;
    post([this,video,path,opts]{paused_=false;seekSeconds_=-1;run(video,path,opts);});
}
#ifdef VEYRA_ENABLE_REMOTEPLAY
void EngineController::openRemotePlay(HWND window,source::RemotePlayConnectDesc desc,PlayerOptions opts){
    auto request=std::make_shared<source::RemotePlayConnectDesc>(std::move(desc));
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;snapshot_.remotePlay=true;snapshot_.capture=true;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_);}
    post([this,window,request,opts]{paused_=false;seekSeconds_=-1;run(window,L"remoteplay:",opts,request);});
}
remoteplay::ControllerFeedback EngineController::remotePlayFeedback(){std::lock_guard lock(mutex_);return activeRemote_?activeRemote_->takeFeedback():remoteplay::ControllerFeedback{};}
void EngineController::remotePlayController(const remoteplay::ControllerState& state){std::lock_guard lock(mutex_);if(activeRemote_)activeRemote_->controller(state);}
void EngineController::remotePlayLoginPin(std::string pin){std::lock_guard lock(mutex_);if(activeRemote_)activeRemote_->loginPin(std::move(pin));}
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
// Input goes straight to the session: the shared pointer is copied under the lock, the send is not.
#define VEYRA_MOONLIGHT_INPUT(call) std::shared_ptr<source::MoonlightSessionSource> m;{std::lock_guard lock(mutex_);m=activeMoonlight_;}if(m)m->call
void EngineController::moonlightController(const remoteplay::ControllerState& state){VEYRA_MOONLIGHT_INPUT(controller(state));}
remoteplay::ControllerFeedback EngineController::moonlightFeedback(){std::shared_ptr<source::MoonlightSessionSource> m;{std::lock_guard lock(mutex_);m=activeMoonlight_;}return m?m->takeFeedback():remoteplay::ControllerFeedback{};}
void EngineController::moonlightKey(uint32_t vk,uint32_t scan,bool extended,bool down){VEYRA_MOONLIGHT_INPUT(keyboard(vk,scan,extended,down));}
void EngineController::moonlightMouseMove(int dx,int dy){VEYRA_MOONLIGHT_INPUT(mouseMove(dx,dy));}
void EngineController::moonlightMouseButton(int button,bool down){VEYRA_MOONLIGHT_INPUT(mouseButton(button,down));}
void EngineController::moonlightScroll(int delta,bool horizontal){VEYRA_MOONLIGHT_INPUT(scroll(delta,horizontal));}
void EngineController::moonlightReleaseInput(){VEYRA_MOONLIGHT_INPUT(releaseInput());}
#undef VEYRA_MOONLIGHT_INPUT
#endif
#ifdef VEYRA_ENABLE_XBOX
void EngineController::xboxController(const remoteplay::ControllerState& state){std::shared_ptr<source::XboxSessionSource> x;{std::lock_guard lock(mutex_);x=activeXbox_;}if(x)x->controller(state);}
remoteplay::ControllerFeedback EngineController::xboxFeedback(){std::shared_ptr<source::XboxSessionSource> x;{std::lock_guard lock(mutex_);x=activeXbox_;}return x?x->takeFeedback():remoteplay::ControllerFeedback{};}
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
void EngineController::openMoonlight(HWND window,source::MoonlightConnectDesc desc,PlayerOptions opts){
    auto request=std::make_shared<source::MoonlightConnectDesc>(std::move(desc));
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;snapshot_.moonlightActive=true;snapshot_.capture=true;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_);}
    post([this,window,request,opts]{paused_=false;seekSeconds_=-1;run(window,L"moonlight:",opts,{},request);});
}
#endif
#ifdef VEYRA_ENABLE_XBOX
void EngineController::openXbox(HWND window,source::XboxConnectDesc desc,PlayerOptions opts){
    auto request=std::make_shared<source::XboxConnectDesc>(std::move(desc));
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;snapshot_.xboxActive=true;snapshot_.capture=true;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_);}
    post([this,window,request,opts]{paused_=false;seekSeconds_=-1;run(window,L"xbox:",opts,{},{},request);});
}
#endif
void EngineController::stop(){std::lock_guard lock(mutex_);stop_=true;pending_={};snapshot_.transport=busy_?TransportState::Stopping:TransportState::Empty;}
void EngineController::pause(bool p){paused_=p;std::lock_guard lock(mutex_);if(snapshot_.running)snapshot_.transport=p?TransportState::Paused:TransportState::Playing;}
void EngineController::setVolume(float gain,bool mute){if(!std::isfinite(gain))return;volume_=std::clamp(gain,0.0f,1.0f);muted_=mute;}
bool EngineController::selectAudioTrack(uint64_t session,int streamIndex){
    std::lock_guard lock(mutex_);
    if(session!=snapshot_.sessionId||!snapshot_.running||snapshot_.failed||snapshot_.capture||snapshot_.image)return false;
    if(std::none_of(snapshot_.audioTracks.begin(),snapshot_.audioTracks.end(),[&](const auto& t){return t.streamIndex==streamIndex;}))return false;
    pendingAudioSession_=session;pendingAudioTrack_=streamIndex;return true;
}
bool EngineController::requestSettings(EnhancementSettings s,std::optional<ChainRuntimeOrder> order,uint64_t* acceptedRevision){
    if(!s.validate().empty()){veyra::log::warn("settings","invalid whole settings transaction rejected");status(L"整套设置无效，未应用任何字段",false);return false;}
    if(order){
        // Fail before changing desired state. This is a shape admission check,
        // not a GPU test; actual dimensions are compiled again at graph creation.
        StageRequest r;r.width=64;r.height=64;r.nr=s.nr;r.sr=s.sr;
        r.fg=s.multiplier>1;r.fgMultiplier=s.multiplier;r.nodeOrder=&*order;
        pipeline::EnhanceGraphDesc desc;describeStages(r,s,desc);
        if(const auto error=desc.validateFixedExecutionPlan();!error.empty()){
            veyra::log::warn("settings",std::format("node order rejected before commit: {}",error));
            status(L"当前执行器尚不支持此节点顺序，原配置保持不变",false);return false;
        }
    }
    std::lock_guard lock(mutex_);if(snapshot_.image)s.multiplier=1;
    // Capability gate: refuse a multiplier the active GPU/runtime cannot honour
    // instead of letting the graph fail and silently turning frame generation
    // off. The previous value is preserved and the reason is surfaced.
    if(s.multiplier>1&&s.frameGenerationBackend==FrameGenerationBackend::Dlss&&fgMultiFrameMaxCap_>0&&
       int(s.multiplier)-1>fgMultiFrameMaxCap_){
        snapshot_.status=std::format(L"此显卡最多支持 {}X 帧生成；请求未应用",fgMultiFrameMaxCap_+1);
        snapshot_.failed=false;
        veyra::log::info("settings",std::format("multiplier gate: requested={} maxGeneratedFrames={} applied=unchanged",s.multiplier,fgMultiFrameMaxCap_));
        return false;
    }
    // XeSS's current ceiling describes this context, not the next one: 2X
    // intentionally runs without the MFG unlock. Validate a higher request in
    // XessPresenter after applying the unlock; rebuild failure rolls it back.
    // The AMD 3.1.x frame-generation provider delivers exactly one generated
    // frame per presented frame; the probe (tools/fsr_probe) measured this on
    // every requested count, so anything above 2X is refused instead of
    // silently presenting 2X under a 4X label.
    if(s.multiplier>2&&fsrFrameGeneration(s.frameGenerationBackend)){
        snapshot_.status=L"AMD FSR 帧生成上限为 2X；请求未应用";
        snapshot_.failed=false;
        veyra::log::info("settings",std::format("fsr multiplier gate: requested={} maxGeneratedFrames=1 applied=unchanged",s.multiplier));
        return false;
    }
    // Revision partitions GPU history and measurements. Audio-only edits must
    // not invalidate in-flight video, and identical notifications are no-ops.
    s.revision=desired_.revision;if(s==desired_&&order==desiredNodeOrder_){if(acceptedRevision)*acceptedRevision=desired_.revision;return true;}
    if(!s.sameVideoConfiguration(desired_)||order!=desiredNodeOrder_)s.revision=++nextRevision_;
    desiredNodeOrder_=order;desired_=s;snapshot_.desired=s;snapshot_.applying=snapshot_.applied!=desired_;
    if(acceptedRevision)*acceptedRevision=s.revision;return true;
}
void EngineController::saveFrame(const std::wstring& path){std::lock_guard lock(mutex_);savePath_=path;}
void EngineController::startExport(const std::wstring& input,const std::wstring& output,PlayerOptions opts,bool hevc){
    if(opts.nodeOrder){
        veyra::log::warn("node-order","export has no runtime node-order transport; request rejected before posting");
        status(L"节点链尚未接入导出执行器，请切换列表模式后导出",false);return;
    }
    const auto frozen=opts.snapshot();const int audioStream=opts.audioStreamIndex;
    post([this,input,output,frozen,audioStream,hevc]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto options=PlayerOptions::from(frozen);options.audioStreamIndex=audioStream;bool ok=exportVideo(input,output,options,hevc,stop_,[this](double p,const std::wstring& s){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.position=p;snapshot_.duration=1;snapshot_.running=true;});{std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.failed=!ok&&!stop_;}CoUninitialize();});
}
void EngineController::requestPresentation(PresentationSettings settings){
    if(!settings.valid())return;
    std::lock_guard lock(mutex_);if(presentation_==settings)return;
    presentation_=settings;++presentationRevision_;
}
PlayerSnapshot EngineController::snapshot()const{
    // The flow window has its own mutex and percentile work; take it after
    // releasing the engine mutex so UI polling does not hold the owner thread
    // (sweep 2026-09-22 B3). Consecutive UI callers within 50 ms share one
    // flow snapshot.
    PlayerSnapshot copy;std::shared_ptr<FrameFlowWindow> flowWindow;
    // The session pointers are copied under the lock: teardown resets them on the
    // engine thread while the UI polls.
#ifdef VEYRA_ENABLE_REMOTEPLAY
    std::shared_ptr<source::RemotePlaySessionSource> activeRemote;
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    std::shared_ptr<source::MoonlightSessionSource> activeMoonlight;
#endif
#ifdef VEYRA_ENABLE_XBOX
    std::shared_ptr<source::XboxSessionSource> activeXbox;
#endif
    {std::lock_guard lock(mutex_);copy=snapshot_;copy.presentation=presentation_;flowWindow=activeFlow_;
#ifdef VEYRA_ENABLE_REMOTEPLAY
        activeRemote=activeRemote_;
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
        activeMoonlight=activeMoonlight_;
#endif
#ifdef VEYRA_ENABLE_XBOX
        activeXbox=activeXbox_;
#endif
    }
    copy.volume=volume_;copy.muted=muted_;
    const bool playing=copy.running&&!copy.image&&copy.transport==TransportState::Playing;
    if(flowWindow){
        static std::mutex cacheMutex;static diagnostics::FrameFlowMetrics cached;static const FrameFlowWindow* cachedFor=nullptr;static int64_t cachedAt=0;
        const auto now=monotonic100ns();
        std::lock_guard lock(cacheMutex);
        if(cachedFor!=flowWindow.get()||now-cachedAt>500000){cached=flowWindow->snapshot(now);cachedFor=flowWindow.get();cachedAt=now;}
        copy.metrics.flow=cached;
    }
    copy.fgBudgetLimited=playing&&copy.metrics.flow.lastFgRejected100ns>0&&monotonic100ns()-copy.metrics.flow.lastFgRejected100ns<10000000;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if(copy.remotePlay&&activeRemote){
        const auto s=activeRemote->sessionSnapshot();copy.remoteStream=s;copy.remotePlayState=int(s.state);
        const auto recovery=activeRemote->recoveryStatus();copy.remoteRecovering=recovery.active;copy.remoteReconnectAttempts=recovery.attempts;copy.remoteRecoveryMessage=recovery.message;
        copy.remoteReceivedFps=s.receivedFps;copy.remoteDecodedFps=s.decodedFps;copy.remoteRatesReady=s.ratesReady;
        copy.remoteReceived=s.video.accessUnits;copy.remoteDecoded=s.decodedFrames;copy.remoteIngressDropped=s.video.dropped;
        copy.remotePlaySkipped=activeRemote->skipped();copy.captureAudio=activeRemote->audioState();copy.audioAvailable=copy.captureAudio.available;
    }
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    if(copy.moonlightActive&&activeMoonlight){
        copy.moonlight=activeMoonlight->stats();
        copy.captureAudio=activeMoonlight->audioState();copy.audioAvailable=copy.captureAudio.available;
    }
#endif
#ifdef VEYRA_ENABLE_XBOX
    if(copy.xboxActive&&activeXbox){
        copy.xbox=activeXbox->stats();
        copy.captureAudio=activeXbox->audioState();copy.audioAvailable=copy.captureAudio.available;
    }
#endif
    auto& flow=copy.metrics.flow;
    if(!playing){flow.sourceCompletedFps=flow.outputCompletedFps=flow.validGeneratedFps=flow.presentSubmitFps=flow.xessSdkSubmitFps=0;}
    copy.fps=flow.sourceCompletedFps;copy.submissionFps=flow.presentSubmitFps;
    return copy;
}
void EngineController::status(const std::wstring& s,bool failed){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.failed=failed;if(failed)snapshot_.transport=TransportState::Failed;}
namespace {
// MMCSS registration keeps the graph owner and capture callback threads ahead
// of ordinary UI/background work; failure is logged once and ignored.
struct MmcssScope {
    HANDLE handle=nullptr;
    explicit MmcssScope(const wchar_t* task){DWORD index=0;handle=AvSetMmThreadCharacteristicsW(task,&index);
        static std::atomic<bool> reported{false};
        if(!handle&&!reported.exchange(true))veyra::log::warn("scheduler",std::format("MMCSS unavailable error={} (continuing at normal priority)",GetLastError()));}
    ~MmcssScope(){if(handle)AvRevertMmThreadCharacteristics(handle);}
};
}
void EngineController::run(HWND window,std::wstring path,PlayerOptions options,std::shared_ptr<source::RemotePlayConnectDesc> remoteRequest,std::shared_ptr<source::MoonlightConnectDesc> moonlightRequest,std::shared_ptr<source::XboxConnectDesc> xboxRequest){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    MmcssScope mmcss(L"Pro Audio");
    status(L"正在初始化GPU与本地运行时…");
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;
    sink::AudioPipeline audioPipe;sink::AudioRenderer audio;VideoPresenter presenter;
   pipeline::EnhanceGraph graph(ctx,ring);AVFrame* imageFrame=nullptr;AVFrame* cachedFrame=nullptr;pipeline::FramePacket cachedPacket;
   source::CaptureCardSource captureSource;const bool physicalCapture=path.starts_with(L"capture:")||path.starts_with(L"capture2:");
    source::ScreenCaptureSource screenSource;const bool isScreen=path.starts_with(L"screen:");
    // Set when a live parameter change arrives while paused (or on a still
    // image): the cached source frame must be re-rendered before the next
    // present, otherwise the new colour tables never reach the GPU.
    bool refreshPausedFrame_=false;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto remote=remoteRequest?std::make_shared<source::RemotePlaySessionSource>():nullptr;
    const bool isRemote=bool(remote);
    {std::lock_guard lock(mutex_);activeRemote_=remote;}
#else
    (void)remoteRequest;const bool isRemote=false;
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    auto moon=moonlightRequest?std::make_shared<source::MoonlightSessionSource>():nullptr;
    const bool isMoonlight=bool(moon);
    {std::lock_guard lock(mutex_);activeMoonlight_=moon;}
#else
    (void)moonlightRequest;const bool isMoonlight=false;
#endif
#ifdef VEYRA_ENABLE_XBOX
    auto xb=xboxRequest?std::make_shared<source::XboxSessionSource>():nullptr;
    const bool isXbox=bool(xb);
    {std::lock_guard lock(mutex_);activeXbox_=xb;}
#else
    (void)xboxRequest;const bool isXbox=false;
#endif
    // PS5, Moonlight and Xbox are all decoded network streams with the same live scheduling.
    const bool isStream=isRemote||isMoonlight||isXbox;
    const bool isCapture=physicalCapture||options.captureReplayForTest||isStream||isScreen;
    // File replay has no hardware arrival clock; retain its continuous PTS anchor.
    const bool pairAnchoredLive=physicalCapture||isStream||isScreen;
    source::IFrameSource* activeSource=physicalCapture?static_cast<source::IFrameSource*>(&captureSource):&source;
    if(isScreen)activeSource=&screenSource;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if(remote)activeSource=remote.get();
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    if(moon)activeSource=moon.get();
#endif
#ifdef VEYRA_ENABLE_XBOX
    if(xb)activeSource=xb.get();
#endif
    if(options.captureReplayForTest)veyra::log::info("capture-test","file replay exercises live scheduler; no physical capture device or latency measurement");
    bool audioStarted=false;bool failed=false;
    try {
        do {
            Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
            // Two bounded jobs, each up to three enhancement lists plus five
            // MFG lists. Allocators must not stall the presentation owner.
            wchar_t testSlots[16]{};unsigned commandSlots=16;
            if(GetEnvironmentVariableW(L"VEYRA_TEST_COMMAND_SLOTS",testSlots,16))commandSlots=std::clamp(unsigned(_wtoi(testSlots)),6u,24u);
            if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),commandSlots,st)){status(L"D3D12初始化失败，请查看诊断",true);break;}
            auto ext=std::filesystem::path(path).extension().wstring();for(auto& c:ext)c=towlower(c);
            const bool isImage=ext==L".png"||ext==L".jpg"||ext==L".jpeg";
            sink::RgbaImage image;
            uint32_t width=0,height=0;double duration=0;
            if(isImage){
                if(!sink::loadImage(path,image)){status(L"无法解码PNG/JPEG",true);break;}
                if(!pipeline::Extent{image.width,image.height}.valid()||uint64_t(image.width)*image.height>16777216){
                    if(options.nodeOrder){
                        veyra::log::warn("node-order","large-image tiling has no runtime node-order transport; request rejected");
                        status(L"节点模式尚未接入大图分块处理，请切换列表模式后打开",true);break;
                    }
                    runLargeImage(window,image,options,ctx,ring);break;
                }
                imageFrame=av_frame_alloc();imageFrame->format=AV_PIX_FMT_RGBA;imageFrame->width=image.width;imageFrame->height=image.height;imageFrame->pts=0;imageFrame->color_range=AVCOL_RANGE_JPEG;imageFrame->colorspace=AVCOL_SPC_RGB;imageFrame->color_trc=AVCOL_TRC_IEC61966_2_1;
                if(av_frame_get_buffer(imageFrame,32)<0){status(L"图片资源分配失败",true);break;}
                for(unsigned y=0;y<image.height;++y)memcpy(imageFrame->data[0]+size_t(y)*imageFrame->linesize[0],image.pixels.data()+size_t(y)*image.width*4,size_t(image.width)*4);
                width=image.width;height=image.height;options.fg=false;
                {std::lock_guard lock(mutex_);desired_.multiplier=1;snapshot_.image=true;snapshot_.desired=desired_;
                 snapshot_.sourceWidth=image.width;snapshot_.sourceHeight=image.height;
                 snapshot_.sourceDisplayAspect=image.height>0?double(image.width)/image.height:0.0;
                 snapshot_.sourceRotationDegrees=0;}
            }else{
                source::SourceOpenDesc od;od.path=path;
                // Files use the same shared D3D12 device as the graph. Capture
                // uses it for the compressed-payload decoder (H.264/HEVC/AV1/
                // VP9 through D3D12VA; MJPEG stays on the software path inside
                // CaptureCompressedDecoder). Each source performs its own
                // capability check and records its fallback.
                od.preferHardwareDecode=!(options.softwareDecode&&!isCapture);
                od.requireHardwareDecode=options.hardwareDecodeOnly&&!options.softwareDecode&&!isCapture&&!isImage; // stills have no hardware decoder
                od.d3d12Device=ctx.device();od.d3d12Queue=ctx.directQueue();
                od.d3d12AdapterLuid=ctx.adapter().luid;
                if(!isCapture)veyra::log::info("decode",std::format("file decode preference={}",options.softwareDecode?"software":options.hardwareDecodeOnly?"hardware-only":"hardware-first"));
                // Diagnostic uses the production graph/presenter to validate
                // D3D12VA imports without needing a paired PS5 or credentials.
                if(!isCapture&&GetEnvironmentVariableW(L"VEYRA_TEST_FILE_HW_DECODE",nullptr,0)){
                    od.preferHardwareDecode=true;od.d3d12Device=ctx.device();od.d3d12Queue=ctx.directQueue();od.d3d12AdapterLuid=ctx.adapter().luid;
                }
                // Audio ingress policy has to be in place before the capture
                // graph negotiates its media type, otherwise the first connect
                // silently uses the previous mode. Kept above the branch chain:
                // inserting statements between `}else` and the open call
                // silently re-binds the `else` and made PS5 sessions fall
                // through to the file open check (fixed 2026-09-16).
                if(physicalCapture)captureSource.setAudioIngress(unsigned(options.settings.captureAudio));
                if(physicalCapture)captureSource.setVerticalFlip(options.settings.captureFlipVertical);
                if(physicalCapture)captureSource.setBufferMode(unsigned(options.settings.captureBuffer));
                if(physicalCapture)captureSource.setCpuUnpack(options.captureCpuUnpack);
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){
                    status(L"正在连接串流主机…");
                    ctx.device()->AddRef();moonlightRequest->decodeDevice=std::shared_ptr<ID3D12Device>(ctx.device(),[](ID3D12Device* device){device->Release();});
                    ctx.directQueue()->AddRef();moonlightRequest->decodeQueue=std::shared_ptr<ID3D12CommandQueue>(ctx.directQueue(),[](ID3D12CommandQueue* queue){queue->Release();});
                    if(!moon->connect(std::move(*moonlightRequest))){const auto message=moon->stats().message;status(message.empty()?L"串流连接失败，请查看诊断":message,true);break;}
                    moonlightRequest.reset();
                    const auto firstFrameDeadline=Clock::now()+std::chrono::seconds(20);
                    while(!stop_){
                        const AVFrame* first=nullptr;const auto result=moon->read(cachedPacket,&first);
                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);break;}
                        if(result==source::SourceReadStatus::Error){const auto message=moon->stats().message;status(message.empty()?L"主机没有返回可解码的画面，请检查主机和网络后重新连接。":message,true);break;}
                        if(Clock::now()>firstFrameDeadline){status(L"已连接，但 20 秒内没有收到画面。请检查主机画面输出和编码设置。",true);break;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if(stop_||!cachedFrame)break;
                }
#endif
#ifdef VEYRA_ENABLE_XBOX
                if(xb){
                    status(L"正在连接 Xbox…");
                    ctx.device()->AddRef();xboxRequest->decodeDevice=std::shared_ptr<ID3D12Device>(ctx.device(),[](ID3D12Device* device){device->Release();});
                    ctx.directQueue()->AddRef();xboxRequest->decodeQueue=std::shared_ptr<ID3D12CommandQueue>(ctx.directQueue(),[](ID3D12CommandQueue* queue){queue->Release();});
                    if(!xb->connect(std::move(*xboxRequest))){const auto message=xb->stats().message;status(message.empty()?L"Xbox 串流连接失败，请查看诊断":message,true);break;}
                    xboxRequest.reset();
                    const auto firstFrameDeadline=Clock::now()+std::chrono::seconds(20);
                    while(!stop_){
                        const AVFrame* first=nullptr;const auto result=xb->read(cachedPacket,&first);
                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);break;}
                        if(result==source::SourceReadStatus::Error){const auto message=xb->stats().message;status(message.empty()?L"Xbox 没有返回可解码的画面，请重新连接。":message,true);break;}
                        if(Clock::now()>firstFrameDeadline){status(L"已连接 Xbox，但 20 秒内没有收到画面。请重新连接。",true);break;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if(stop_||!cachedFrame)break;
                }
#endif
#ifdef VEYRA_ENABLE_REMOTEPLAY
                if(remote){
                    status(L"正在连接 PS5…");
                    ctx.device()->AddRef();remoteRequest->decodeDevice=std::shared_ptr<ID3D12Device>(ctx.device(),[](ID3D12Device* device){device->Release();});
                    if(!remote->connect(std::move(*remoteRequest))){status(L"PS5 连接失败，请查看诊断",true);break;}
                    remoteRequest.reset();
                    while(!stop_){
                        const AVFrame* first=nullptr;const auto result=remote->read(cachedPacket,&first);
                        const auto state=remote->sessionSnapshot().state;
                        {std::lock_guard lock(mutex_);snapshot_.remotePlayState=int(state);}
                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);break;}
                        if(result==source::SourceReadStatus::Error){const auto recovery=remote->recoveryStatus();status(recovery.message.empty()?L"PS5 未返回可解码画面，请检查主机及网络后重新连接。":recovery.message,true);break;}
                        if(state==remoteplay::SessionState::LoginPinRequired)status(L"PS5 要求登录 PIN，请在串流面板输入");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if(stop_||!cachedFrame)break;
                }else{
#endif
                if(!isMoonlight&&!isXbox&&!(physicalCapture?captureSource.configure(od):activeSource->open(od))){status(isScreen?screenSource.status():physicalCapture&&!captureSource.errorMessage().empty()?captureSource.errorMessage():!isCapture&&!source.errorMessage().empty()?source.errorMessage():L"无法打开视频，请查看诊断",true);break;}
#ifdef VEYRA_ENABLE_REMOTEPLAY
                }
#endif
                // A file's actual decoded pixel format and HDR VUI are only
                // reliable on AVFrame. Prime one frame before constructing
                // the graph so AV1/MOV 10-bit and HDR inputs do not get an
                // SDR graph by mistake. The cloned frame is consumed by the
                // normal owner-thread loop below, preserving its PTS.
                if(!isCapture){
                    pipeline::FramePacket firstPacket;const AVFrame* firstFrame=nullptr;
                    const auto firstStatus=activeSource->read(firstPacket,&firstFrame);
                    if(firstStatus!=source::SourceReadStatus::Frame||firstFrame==nullptr||firstPacket.pts.isUnknown()){
                        status(activeSource->info().kind==pipeline::SourceKind::File&&source.errorMessage().empty()
                            ?L"视频首帧无法解码或没有有效时间戳，请查看诊断":source.errorMessage().empty()?L"视频首帧读取失败":source.errorMessage(),true);
                        break;
                    }
                    cachedFrame=av_frame_clone(firstFrame);
                    if(!cachedFrame){status(L"视频首帧缓存失败",true);break;}
                    cachedPacket=firstPacket;
                }
                width=activeSource->info().width;height=activeSource->info().height;duration=isCapture?0:activeSource->info().duration.toDouble();
                {std::lock_guard lock(mutex_);
                 const auto& si=activeSource->info();
                 snapshot_.sourceWidth=si.width;snapshot_.sourceHeight=si.height;
                 snapshot_.sourceDisplayAspect=si.displayAspect;
                 if(si.width&&si.height&&std::isfinite(si.displayAspect)&&si.displayAspect>0&&std::abs(si.displayAspect-double(si.width)/si.height)>.001)
                     previewView_.displayAspect=si.displayAspect;
                 snapshot_.sourceRotationDegrees=si.rotationDegrees;
                 // Poster for the minimal-mode bar. Built once, from the frame
                 // we already decoded; failure leaves it empty on purpose.
                 std::vector<uint8_t> poster;uint32_t pw=0,ph=0;
                 const AVFrame* posterSource=cachedFrame?cachedFrame:imageFrame;
                 if(posterSource&&makePosterFrame(posterSource,320,180,poster,pw,ph)){
                     snapshot_.posterRgba=std::move(poster);snapshot_.posterWidth=pw;snapshot_.posterHeight=ph;
                 }else{snapshot_.posterRgba.clear();snapshot_.posterWidth=snapshot_.posterHeight=0;}}
                {std::lock_guard lock(mutex_);snapshot_.sourceNotice=activeSource->info().dolbyVision.description();}
            }
            if(!pipeline::Extent{width,height}.valid()){status(L"图像尺寸超出单张GPU纹理能力，需要分块处理",true);break;}
            HdrDisplayState displayHdrState;
            bool displayQueryFailed=false;
            auto displayHdrActive=[&]{
                HMONITOR target=nullptr;
                const auto queried=gfx::PresentSink::queryHdrDisplayActive(window,&target);
                const bool active=displayHdrState.update(reinterpret_cast<std::uintptr_t>(target),queried);
                if(!queried&&!displayQueryFailed)veyra::log::warn("display-color",std::format("HDR display query unavailable; using target-local hdr={} (unknown target defaults to SDR)",active));
                if(queried&&displayQueryFailed)veyra::log::info("display-color",std::format("HDR display query recovered hdr={}",active));
                displayQueryFailed=!queried.has_value();
                return active;
            };
            pipeline::EnhanceGraphDesc gd;gd.sourceWidth=width;gd.sourceHeight=height;gd.hdrInput=!isImage&&activeSource->info().color.isHdrPath();
            gd.highQualityPresentation=isRemote&&activeSource->info().color.reconstructChroma;
            gd.rgbInput=isImage||isScreen||(isCapture&&activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::Bgra8);
            gd.captureBitDepth=activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::P010?10:activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::P016?16:8;
            gd.yuy2Input=isCapture&&activeSource->info().color.pixelFormat==pipeline::SourcePixelFormat::Yuy2;gd.stillImage=isImage;
            gd.packedInput=isCapture?pipeline::packedInputCode(activeSource->info().color.pixelFormat):0;
            const bool nvidiaAdapter=ctx.adapter().isNvidia;
            auto stageRequest=[&](const PlayerOptions& o){
                StageRequest r;r.nr=o.nr;r.sr=o.sr;r.fg=o.fg;r.fgMultiplier=o.fgMultiplier;
                r.width=width;r.height=height;r.stillImage=isImage;r.nvidiaAdapter=nvidiaAdapter;
                r.nodeOrder=o.nodeOrder?&*o.nodeOrder:nullptr;return r;
            };
            describeStages(stageRequest(options),options.snapshot(),gd);
            const bool xessFg=crossVendorFrameGeneration(options.settings.frameGenerationBackend);
            gd.noFeatures=false;
            gd.hdrOutput=options.settings.useHdrPreview(gd.hdrInput,displayHdrActive());
            veyra::log::info("display-color",std::format("hdrInput={} hdrOutput={} forceSdrPreview={}",gd.hdrInput,gd.hdrOutput,options.settings.forceSdrPreview));
            gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            gd.compatibilityPreflight=[this](const auto& desc,const auto& device){return checkFgCompatibility(desc,device,stop_);};
            std::wstring backendRecoveryWarning;
            auto initializePreview=[&](pipeline::EnhanceGraphDesc& desc,PlayerOptions& selected,bool preserveWorkingFg=false,bool preserveWorkingNr=false){
                backendRecoveryWarning.clear();
                auto supported=selected.snapshot();
                if(disableUnsupportedNvidiaEffects(supported,nvidiaAdapter)){
                    selected=PlayerOptions::from(supported,selected.nodeOrder);
                    // Same stage rules as every other build: AMD FSR upscaling
                    // survives the normalization, it used to be dropped here.
                    describeStages(stageRequest(selected),selected.snapshot(),desc);
                    backendRecoveryWarning=L"当前 GPU 不支持 NVIDIA 增强；NR、超分及 DLSS 已关闭，XeSS选择保留";
                    veyra::log::warn("capability","normalized requested NVIDIA effects before graph creation; applied settings reflect actual disabled stages");
                }
                for(unsigned attempt=0;attempt<6;++attempt){
                    desc.videoHdr=selected.settings.videoHdr;
                    desc.hdrBrightness=selected.settings.hdrBrightness;
                    desc.hdrOutput=selected.settings.useHdrPreview(desc.hdrInput,displayHdrActive());
                    bool opened=graph.initialize(desc);
                    auto failure=graph.failedBackend();
                    if(opened){
                        opened=presenter.open(ctx,window,graph,selected.settings.captureCompatible,!isCapture&&!isImage)&&graph.createViews();
                        failure=FailedBackend::Infrastructure;
                        if(opened&&graph.xessEnabled()&&!presenter.xessActive()){opened=false;failure=FailedBackend::Fg;}
                        if(opened&&graph.fsrEnabled()&&!graph.fsrActive()){opened=false;failure=FailedBackend::Fg;}
                        // Requested AMD FSR upscaling that could not be created
                        // (missing local runtime, unsupported layout) degrades
                        // to the plain scale path with a visible warning.
                        if(opened&&graph.fsrSrRequested()&&!graph.fsrSrEnabled()){opened=false;failure=FailedBackend::Sr;}
                    }
                if(opened)return true;
                // A live backend/multiplier switch must not turn a working FG
                // session into effects-off playback when the new provider fails.
                if(preserveWorkingFg&&failure==FailedBackend::Fg)return false;
                if(preserveWorkingNr&&failure==FailedBackend::Nr)return false;
                auto reduced=selected.snapshot();
                // Requested multiplier above this GPU's capability: clamp to what
                // the runtime supports and say so, instead of dropping frame
                // generation entirely (the old behaviour users saw as "选了 4X
                // 之后补帧直接没了").
                bool clampedOverCapability=false;
                int capabilityGeneratedFrames=-1;
                if(failure==FailedBackend::Fg&&reduced.multiplier>1){
                    if(reduced.frameGenerationBackend==FrameGenerationBackend::Dlss)capabilityGeneratedFrames=graph.fgMultiFrameCountMax();
                    else if(reduced.frameGenerationBackend==FrameGenerationBackend::XeSS){
                        const auto xessState=gfx::XessMfgUnlock::snapshot();
                        if(xessState.applied)capabilityGeneratedFrames=int(xessState.maxInterpolations);
                    }
                    else if(fsrFrameGeneration(reduced.frameGenerationBackend)){
                        capabilityGeneratedFrames=int((graph.fsrActive()?1u:0u));
                    }
                }
                if(capabilityGeneratedFrames>0&&int(reduced.multiplier)-1>capabilityGeneratedFrames){
                    const uint32_t clamped=uint32_t(capabilityGeneratedFrames+1);
                    veyra::log::warn("backend-recovery",std::format("requested multiplier {} exceeds capability maxGeneratedFrames={}; applying {}X with frame generation kept",reduced.multiplier,capabilityGeneratedFrames,clamped));
                    reduced.multiplier=clamped;
                    clampedOverCapability=true;
                    if(!backendRecoveryWarning.empty())backendRecoveryWarning+=L"；";
                    backendRecoveryWarning+=std::format(L"当前最多支持 {}X 帧生成，已按 {}X 应用",clamped,clamped);
                }
                if(FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
                if(!clampedOverCapability&&!disableFailedBackend(reduced,failure))return false;
                    veyra::log::warn("backend-recovery",std::format("initialization failed component={} attempt={} revision={} -> nr={} sr={} multiplier={}; original SDK error above",unsigned(failure),attempt+1,reduced.revision,reduced.nr,reduced.sr,reduced.multiplier));
                    if(!backendRecoveryWarning.empty())backendRecoveryWarning+=L"；";
                    const uint32_t ngxResult=uint32_t(graph.failedNgxResult());
                    if(selected.settings.frameGenerationBackend==FrameGenerationBackend::Fsr4&&failure==FailedBackend::Fg)
                        backendRecoveryWarning+=L"FSR 4 ML 提供方不可用（需要支持的 RX 9000 和驱动），已关闭补帧；原因见日志";
                    else if((ngxResult&0xFFF00000u)==0xBAD00000u)
                        backendRecoveryWarning+=std::wstring(backendFailureName(failure))+L"："+ngxFailureHint(ngxResult)+std::format(L"（NGX 0x{:08X}，驱动 {}），已关闭依赖效果",ngxResult,ctx.adapter().driverVersion);
                    else if(!clampedOverCapability)
                        backendRecoveryWarning+=std::wstring(backendFailureName(failure))+L" 初始化失败，已关闭依赖效果；原因见诊断日志";
                    if(!graph.driverRequirementNotice().empty())backendRecoveryWarning+=L"；"+graph.driverRequirementNotice();
                    if(failure==FailedBackend::Fg&&(ngx::AmpereMfgUnlock::applied()||(ngx::DlssgTransfusion::applied()&&ngx::DlssgTransfusion::snapshot().target==ngx::DlssgTransfusion::Target::Ampere)))backendRecoveryWarning+=L"；RTX 30 系补帧解锁已应用，但当前驱动/运行库组合下运行时不开放补帧，可改用 AMD FSR 补帧";

                    if(!ring.drainQueue()||!ring.discardRecording())return false;
                    presenter.close();graph.shutdown();selected=PlayerOptions::from(reduced,selected.nodeOrder);
                    describeStages(stageRequest(selected),selected.snapshot(),desc);
                }
                return false;
            };
            auto publishFrameGenerationCapabilities=[&]{
                // Read the active provider after every rebuild or rollback;
                // an initial effects-off graph has no DLSS capability yet.
                std::lock_guard lock(mutex_);
                fgMultiFrameMaxCap_=graph.fgMultiFrameCountMax();
                snapshot_.fgMultiFrameMax=fgMultiFrameMaxCap_;
                snapshot_.fgCapabilityKnown=fgMultiFrameMaxCap_>0;
                const auto xessState=gfx::XessMfgUnlock::snapshot();
                // Unknown until the XeSS provider actually ran: a DLSS-only
                // session must not restrict the XeSS choices in the UI.
                xessMaxInterpolatedFramesCap_=presenter.xessActive()?int(std::max(1u,xessState.maxInterpolations)):0;
                snapshot_.xessMaxInterpolatedFrames=xessMaxInterpolatedFramesCap_;
                // AMD FSR ceiling comes from the active graph provider; 0 keeps the
                // settings UI on its static 2X fallback until a session ran.
                fsrMaxGeneratedFramesCap_=graph.fsrEnabled()&&graph.fsrActive()?int((graph.fsrActive()?1u:0u)):0;
                snapshot_.fsrMaxGeneratedFrames=fsrMaxGeneratedFramesCap_;
                snapshot_.fsrProviderVersion=graph.fsrProviderVersion();
                if(fgMultiFrameMaxCap_>0)veyra::log::info("settings",std::format("frame-generation capability: maxGeneratedFrames={} maxMultiplier={}",fgMultiFrameMaxCap_,fgMultiFrameMaxCap_+1));
                veyra::log::info("settings",std::format("xess capability: active={} maxInterpolatedFrames={} unlockApplied={} (active context only)",presenter.xessActive(),xessMaxInterpolatedFramesCap_,xessState.applied));
                if(fsrMaxGeneratedFramesCap_>0)veyra::log::info("settings",std::format("fsr capability: generatedPerPresent={} => maxMultiplier={}",fsrMaxGeneratedFramesCap_,fsrMaxGeneratedFramesCap_+1));
            };
            const auto initialRequested=options.snapshot();
            const bool previewInitialized=initializePreview(gd,options);
            publishFrameGenerationCapabilities();
            if(!previewInitialized){status(L"视频初始化失败，请查看对应组件的诊断日志",true);break;}
            if(!backendRecoveryWarning.empty()){
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(initialRequested,options.snapshot());snapshot_.desired=desired_;snapshot_.backendWarning=backendRecoveryWarning;
            }
            if(!nvidiaAdapter&&(options.nr||options.sr||(options.fg&&!xessFg))){
                veyra::log::warn("capability",std::format("non-NVIDIA adapter disabled requested features: nr={} sr={} fgBackend={} flowBackend={}",options.nr,options.sr,frameGenerationBackendName(options.settings.frameGenerationBackend),opticalFlowBackendName(options.settings.opticalFlowBackend)));
                status(L"当前非 NVIDIA 适配器：NR、NVIDIA 超分与 DLSS 已禁用；可使用 XeSS 预览和 AMD 光流",false);
            }
            if(gd.hdrInput)status(gd.hdrOutput?L"HDR输入 · HDR保留增强/显示":L"HDR输入 · SDR色调映射后增强/显示（1000nit参考峰值）",false);
            if(graph.xessEnabled()&&!presenter.xessActive())status(L"XeSS 未启用：运行时或设备不兼容；当前为普通呈现",false);
            {
                std::lock_guard lock(mutex_);
                if(!nvidiaAdapter&&(options.nr||options.sr||(options.fg&&!xessFg)))snapshot_.backendWarning=L"当前 GPU 不支持所选 NVIDIA 增强";
                if(graph.xessEnabled()&&!presenter.xessActive())snapshot_.backendWarning=L"XeSS 初始化失败；当前为普通呈现";
            }
            captureSource.setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
            captureSource.setAudioIngress(unsigned(options.settings.captureAudio));
            captureSource.setVerticalFlip(options.settings.captureFlipVertical);
            captureSource.setBufferMode(unsigned(options.settings.captureBuffer));
            if(physicalCapture&&!captureSource.start()){status(L"无法启动采集，请查看诊断",true);break;}
            {std::lock_guard lock(mutex_);snapshot_.duration=duration;snapshot_.nominalSourceFps=isImage?0:activeSource->info().averageFps;snapshot_.running=true;snapshot_.transport=TransportState::Playing;snapshot_.image=isImage;snapshot_.capture=isCapture;snapshot_.applied=options.snapshot();snapshot_.desired=desired_;}
            status(isImage?L"图片已增强，可保存PNG/JPEG":std::format(L"{} | 输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",isRemote?L"PS5 串流":isMoonlight?L"PC 串流":isXbox?L"Xbox 串流":isCapture?L"实时采集":L"播放",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrBeforeSr?L"低延迟 · NR先行后超分":gd.nrWidth<gd.workWidth?L"实时内部处理并回填":L"原生NR（性能成本较高）"));
            pipeline::EnhanceGraph::FrameOutputs out;bool reset=true,hasOutput=false,audioRebuffering=false,seekPreviewPending=false;
            bool initialRemoteFramePending=isStream;
            bool initialFileFramePending=!isImage&&!isCapture;
            uint64_t activeSeekId=0;
            Clock::time_point seekStarted{};
            uint64_t seekDecoded=0;
            bool fileAwaitingVideo=true,fileAudioAlignPending=true,fileInputEnded=false;double lastFilePresentedMs=0,lastFilePresentLateness=0;
            TimingWindow presentEntryDeviation,presentReturnDeviation;
            if(!isImage&&!isCapture&&audioPipe.open(path)){
                {std::lock_guard lock(mutex_);snapshot_.audioTracks=audioPipe.tracks();snapshot_.selectedAudioTrack=audioPipe.selectedTrack();}
                audioPipe.holdForVideo();audioPipe.startThread(&audio,true);audioStarted=true;
                std::lock_guard lock(mutex_);snapshot_.audioAvailable=true;
            }
            auto holdFileAudio=[&]{if(audioStarted)audioPipe.holdForVideo();fileAwaitingVideo=true;fileInputEnded=false;};
            TimingWindow captureAges,scheduleWaits,processTimes,presentTimes,decodeTimes,gpuReadyTimes;
            std::array<TimingWindow,size_t(diagnostics::GpuStage::Count)> gpuStageTimes;
            std::array<uint64_t,size_t(diagnostics::GpuStage::Count)> lastGpuSampleEnd{};
            auto nextTimingLog=Clock::now()+std::chrono::seconds(1);
            // Video memory watchdog. A field log (RTX 5060 Ti, 2026-10-01) grew +3 GB a minute while
            // fullscreen, kept it after leaving fullscreen, and every pass slowed down once usage
            // passed the budget (5 fps). Unreproduced here. On growth beyond max(2 GiB, budget/4)
            // with no settings change the log names every third-party module, and the loop
            // rebuilds first the presentation swapchain, then (if that released less than half)
            // the processing graph too, logging what each released: the swapchain step points
            // at the driver or an overlay hooked on it, the graph step at our own passes.
            uint64_t vramFloor=0,vramRevision=~0ull,vramRecoverBefore=0,vramRecoverGrowth=0;auto vramSettleUntil=Clock::now(),vramRecoverCheckAt=Clock::now();
            bool vramWarned=false;int vramRecoverStep=0,vramRecoverDone=0,vramRecoveries=0;
            bool vramFullscreenBlocked=false;unsigned vramGrowthSamples=0;
            bool vramProtectionEnabled=fullscreenMemoryProtection_.load();
            // VEYRA_TEST_VRAM_LEAK_MIB=n: hold on to n MiB more of video memory every second;
            // VEYRA_TEST_VRAM_LEAK_RELEASE=1 lets the swapchain rebuild free it (as a leak tied
            // to the swapchain would), otherwise nothing frees it.
            const uint64_t testLeakMiB=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_MIB",v,16)?uint64_t(_wtoi(v)):0ull;}();
            const bool testLeakReleases=GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_RELEASE",nullptr,0)>0;
            const bool testLeakFullscreenOnly=GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_FULLSCREEN_ONLY",nullptr,0)>0;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> testLeak;
            auto watchVram=[&](uint64_t usage,uint64_t budget){
                bool fullscreen=false;
                {std::lock_guard lock(mutex_);fullscreen=presentation_.fullscreen;}
                if(testLeakMiB&&(!testLeakFullscreenOnly||fullscreen)){
                    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
                    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=testLeakMiB<<20;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    Microsoft::WRL::ComPtr<ID3D12Resource> r;
                    if(SUCCEEDED(ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r))))testLeak.push_back(r);
                }
                const bool protection=fullscreenMemoryProtection_.load();
                if(protection!=vramProtectionEnabled){
                    vramProtectionEnabled=protection;vramFullscreenBlocked=false;vramGrowthSamples=0;vramFloor=0;
                    vramSettleUntil=Clock::now()+std::chrono::seconds(10);
                }
                if(!usage)return;
                // Leaving fullscreen stopped further growth in both field logs. Preserve
                // the source/effects and avoid repeated failed graph rebuilds. This only
                // contains the observed trigger; it does not identify the allocation owner
                // or promise that already retained memory is returned.
                if(vramFullscreenBlocked)return;
                if(vramRecoverDone){
                    if(Clock::now()<vramRecoverCheckAt)return;
                    const int64_t released=(int64_t(vramRecoverBefore)-int64_t(usage))/(1<<20);
                    veyra::log::warn("vram-watch",std::format("{} rebuild released {} MiB of {} MiB growth (before {} MiB, now {} MiB)",
                        vramRecoverDone==1?"swapchain":"graph+swapchain",released,vramRecoverGrowth>>20,vramRecoverBefore>>20,usage>>20));
                    // The graph is rebuilt only with room for it: a rebuild that runs out of budget
                    // stops the session, which is worse than running slowly.
                    if(vramRecoverDone==1&&released<int64_t(vramRecoverGrowth>>21)){
                        if(budget&&usage<budget/100*85)vramRecoverStep=2;
                        else veyra::log::warn("vram-watch","graph rebuild skipped: too close to the budget to rebuild safely");
                    }
                    vramRecoverDone=0;vramFloor=0;vramSettleUntil=Clock::now()+std::chrono::seconds(5);
                    return;
                }
                if(vramRecoverStep)return;
                if(options.settings.revision!=vramRevision){vramRevision=options.settings.revision;vramFloor=0;vramGrowthSamples=0;vramSettleUntil=Clock::now()+std::chrono::seconds(10);}
                if(Clock::now()<vramSettleUntil)return;
                if(!vramFloor||usage<vramFloor)vramFloor=usage;
                const uint64_t growth=usage-vramFloor;
                const uint64_t fullscreenLimit=std::max<uint64_t>(512ull<<20,std::min<uint64_t>(2048ull<<20,budget/8));
                if(vramProtectionEnabled&&fullscreen&&growth>fullscreenLimit){
                    if(++vramGrowthSamples<3)return;
                    vramFullscreenBlocked=true;vramWarned=true;
                    const std::wstring injected=gfx::riskyInjections();
                    {
                        std::lock_guard lock(mutex_);
                        snapshot_.vramRunawayMiB=growth>>20;snapshot_.vramInjected=injected;snapshot_.vramFullscreenUnsafe=true;
                    }
                    veyra::log::warn("vram-watch",std::format("sustained fullscreen growth {} MiB (usage {} / budget {} MiB); requesting windowed playback, preserving source/effects; allocation owner unconfirmed; modules: {}",
                        growth>>20,usage>>20,budget>>20,gfx::thirdPartyModules()));
                    return;
                }
                vramGrowthSamples=0;
                const uint64_t limit=std::max<uint64_t>(2048ull<<20,budget/4);
                if(growth<=limit)return;
                // Module presence is diagnostic context, not proof of allocation ownership.
                // Evaluate a bounded recovery by the measured memory result, regardless of
                // whether NVIDIA's plug-in (also present in stable local runs) is loaded.
                const std::wstring injected=gfx::riskyInjections();
                if(!vramWarned){
                    vramWarned=true;
                    veyra::log::warn("vram-watch",std::format("video memory grew {} MiB with no settings change (now {} of {} MiB budget); third-party modules: {}",
                        growth>>20,usage>>20,budget>>20,gfx::thirdPartyModules()));
                    std::lock_guard lock(mutex_);snapshot_.vramRunawayMiB=growth>>20;snapshot_.vramInjected=injected;
                }else veyra::log::warn("vram-watch",std::format("video memory grew {} MiB again (now {} MiB)",growth>>20,usage>>20));
                if(vramRecoveries<3){++vramRecoveries;vramRecoverStep=1;vramRecoverGrowth=growth;}
                else{vramFloor=usage;}   // give up rebuilding; report again only on further growth
            };
            double playbackRate=1.0;
            auto anchor=Clock::now(),statsStart=anchor;double anchorMs=0;uint64_t frames=0,sourceFrames=0;bool wasPaused=false;double discardBefore=0;
            double lastAudioClockMs=0;bool audioClockExhausted=false;auto audioTailAnchor=anchor;
            bool publishedAudioRecovery=false;HRESULT publishedAudioError=S_OK;uint64_t publishedAudioRecoveries=0;
            const bool injectFileEndpointLoss=GetEnvironmentVariableW(L"VEYRA_TEST_FILE_ENDPOINT_LOSS",nullptr,0)>0;
            bool fileEndpointLossInjected=false;
            auto publishAudioStatus=[&]{
                const bool recovering=audioPipe.endpointRecovering();const auto error=audioPipe.endpointError();const auto count=audioPipe.endpointRecoveries();
                {std::lock_guard lock(mutex_);snapshot_.audioRebuffering=audioPipe.waitingForVideo();snapshot_.audioVideoWaits=audioPipe.videoWaitCount();}
                if(recovering!=publishedAudioRecovery||error!=publishedAudioError||count!=publishedAudioRecoveries){
                    publishedAudioRecovery=recovering;publishedAudioError=error;publishedAudioRecoveries=count;
                    std::lock_guard lock(mutex_);snapshot_.audioInputChannels=audioPipe.pcmFormat().channels;snapshot_.audioOutputChannels=audio.outputFormat().channels;snapshot_.audioEndpointRecovering=recovering;snapshot_.audioEndpointError=error;snapshot_.audioEndpointRecoveries=count;
                }
            };
            PresentationScheduler liveTimeline;uint64_t submitted=0,expired=0;std::deque<int64_t> submissionTimes;
            DeadlineWait deadlineWait;
            // Live capture: the GPU fence of a submitted frame wakes this thread
            // directly. The 0.2 ms ready poll alone left a finished frame waiting
            // up to one timer slice (plus timer jitter) before its Present.
            struct EventHandle{HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);~EventHandle(){if(handle)CloseHandle(handle);}} gpuReadyEvent;
            // Realtime file preview scheduling (P2/P3): expired decoded
            // candidates lose their enhancement opportunity against the audio
            // master clock; FG admission predicts pair completion; XeSS-FG
            // generation is suppressed over stable intervals when sustained
            // late. Export/images/paused frames never enter these paths.
            uint64_t previewSkippedTotal=0,previewSkippedSinceSubmit=0,previewSkipRetained=0;int64_t nextPreviewSkipLog=0;
            // Media PTS of the last frame the graph actually processed. The
            // temporal stages care about the gap to that frame, not about how
            // many candidates were dropped on the way (see boundedSkip below).
            double lastProcessedPtsMs=std::numeric_limits<double>::quiet_NaN();
            bool previewSkipSinceProcess=false;
            // Cumulative provider-submission totals already fed to the frame
            // flow. The AMD provider reports presentation from its own thread,
            // so per-call deltas systematically miss updates that land just
            // after a submission; feeding the cumulative difference at the
            // next submission cannot lose them. Reset with the presenter.
            uint64_t fedXessPresented=0,fedXessGenerated=0;
            double playbackSpeedLastPts=0;Clock::time_point playbackSpeedLastWall{};
            CaptureHalfRate captureSampler;
            FrameLineageTracker lineageTracker;
            auto completedProcessing=[&](pipeline::FrameIdentity id){std::lock_guard lock(mutex_);if(snapshot_.applied.revision==id.settingsRevision)++snapshot_.processedCompleted;};
            auto host100ns=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;};
            // Live ingress stays latest-frame; file preview keeps the audio clock
            // and may skip expired enhancement opportunities, never PCM.
            // Graph, query collection and presentation remain on this owner.
            graph.recordGpuTimings();presenter.recordGpuTimings();
            struct LiveStats {uint64_t submitted=0,expired=0;double waitMs=0,presentMs=0,readyMs=0,ageMs=0,fps=0,ageP95=0,waitP95=0,presentP95=0,readyP95=0;diagnostics::GpuSample blit;};
            LiveStats liveStats;std::deque<int64_t> liveSubmissions;
            TimingWindow liveAges,liveWaits,liveReady;
            TimingWindow livePresent{std::chrono::seconds(1)};
            TimingWindow livePresentGpu{std::chrono::seconds(1)};
            FgRecoveryBudget fgBudget;uint64_t fgBudgetRevision=options.settings.revision;
            unsigned previewFgMultiplier=options.fgMultiplier;
            LivePairLatency pairLatency;
            const bool adaptiveCapturePhase=physicalCapture&&GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_CAPTURE_PHASE",nullptr,0)==0;
            int64_t nextFgAdmissionLog=0,nextFgRejectionLog=0;
            uint64_t fgAdmittedPairs=0,fgRejectedPairs=0,fgReducedPairs=0;int64_t nextFgReducedLog=0;
            std::atomic<uint64_t> historyResets=0,presentationDrains=0,presentationCompletedReal=0,presentationSkippedGenerated=0,presentationCancelledJobs=0;
            std::atomic<uint64_t> presentationGeneration{0};
            std::unique_ptr<LiveGpuScheduler> liveScheduler;
            struct CompletionWatch {
                uint64_t reflexFrame=0;
                int64_t captureArrival=0;
                uint64_t presentationGeneration=0;
                pipeline::EnhanceGraph::FrameOutputs output;
                std::shared_ptr<FrameFlowWindow> flow;
                Clock::time_point processStart;
                int64_t predictedGpuStart100ns=0;
                std::optional<double> gpuExecutionMs,fgExecutionMs;
                bool real=false;
                std::atomic<bool> ready=false;
                Clock::time_point readyObserved{};
                std::array<int64_t,pipeline::FrameBatch::Capacity> frameReadyObserved{};
            };
            int64_t nextFgDeadlineLog=0;
            std::vector<std::shared_ptr<CompletionWatch>> pendingCompletions;
            std::shared_ptr<FrameFlowWindow> frameFlow;
            const uint64_t runSessionId=[&]{std::lock_guard lock(mutex_);return snapshot_.sessionId;}();
            pipeline::ResetReason pendingResetCause=pipeline::ResetReason::Open;
            auto traceFrame=[&](diagnostics::TraceKind kind,pipeline::FrameIdentity identity,uint64_t batch,
                uint64_t fence,int64_t pts,uint32_t detail,uint32_t count,double ms){
                Logger::instance().recordFrame({host100ns(),runSessionId,identity,batch,fence,pts,kind,detail,count,ms});
            };
            std::optional<diagnostics::FrameFlowMetrics::ResetRecord> resetRecord;
            std::optional<diagnostics::FrameFlowMetrics::ResetRecord> lastResetRecord;
            Clock::time_point resetStart{},resetStageStart{};
            auto markResetStage=[&](diagnostics::ResetStage stage){
                if(!resetRecord)return;
                const auto now=Clock::now();
                resetRecord->stageMs[size_t(stage)]=std::chrono::duration<double,std::milli>(now-resetStageStart).count();
                resetStageStart=now;
                if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
            };
            auto finishReset=[&](diagnostics::ResetOutcome outcome){
                if(!resetRecord)return;
                resetRecord->outcome=outcome;resetRecord->totalMs=elapsedMs(resetStart);
                lastResetRecord=resetRecord;
                if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                const auto& r=*resetRecord;const auto ms=[&](diagnostics::ResetStage s){return r.stageMs[size_t(s)].value_or(-1.0);};
                traceFrame(diagnostics::TraceKind::Reset,{r.epoch,r.settingsRevision,r.sourceFrameId},0,0,0,r.reason,unsigned(outcome),*r.totalMs);
                veyra::log::info("reset-lifecycle",std::format("session={} revision={} epoch={} source={} reason={} outcome={} drainMs={:.3f} destroyMs={:.3f} createMs={:.3f} warmupSubmitMs={:.3f} firstValidObserveMs={:.3f} totalMs={:.3f} (CPU observed; -1=not measured)",r.sessionId,r.settingsRevision,r.epoch,r.sourceFrameId,unsigned(r.reason),unsigned(r.outcome),ms(diagnostics::ResetStage::Drain),ms(diagnostics::ResetStage::Destroy),ms(diagnostics::ResetStage::Create),ms(diagnostics::ResetStage::Warmup),ms(diagnostics::ResetStage::FirstValid),*r.totalMs));
                resetRecord.reset();
            };
            auto completeReset=[&](pipeline::FrameIdentity identity){
                if(!resetRecord||resetRecord->settingsRevision!=identity.settingsRevision||resetRecord->epoch!=identity.epoch||resetRecord->sourceFrameId!=identity.sourceFrameId)return;
                markResetStage(diagnostics::ResetStage::FirstValid);
                finishReset(diagnostics::ResetOutcome::Completed);
            };
            OnExit resetExit{[&]{finishReset(stop_?diagnostics::ResetOutcome::Cancelled:diagnostics::ResetOutcome::Failed);}};
            auto collectTimings=[&]{
                auto record=[&](const diagnostics::GpuFrameTiming& sample){
                    const auto& fgCost=sample.gpu[size_t(diagnostics::GpuStage::FgBatch)];
                    for(auto& watch:pendingCompletions){
                        const auto& id=watch->output.batch.identity;
                        if(id.epoch==sample.identity.epoch&&id.settingsRevision==sample.identity.settingsRevision&&id.sourceFrameId==sample.identity.sourceFrameId){
                            if(const auto span=diagnostics::graphExecutionSpanMs(sample))watch->gpuExecutionMs=span;
                            if(fgCost.state==diagnostics::SampleState::Measured)watch->fgExecutionMs=fgCost.milliseconds;
                        }
                    }
                    const auto& blit=sample.gpu[size_t(diagnostics::GpuStage::Blit)];
                    if(sample.identity.settingsRevision==fgBudgetRevision&&blit.state==diagnostics::SampleState::Measured&&blit.milliseconds)livePresentGpu.add(*blit.milliseconds);
                    if(frameFlow)frameFlow->gpuFrame(sample,host100ns());
                    for(size_t i=0;i<sample.gpu.size();++i){const auto& gpu=sample.gpu[i];
                        if(gpu.state==diagnostics::SampleState::Measured&&gpu.milliseconds){
                            traceFrame(diagnostics::TraceKind::Gpu,sample.identity,0,0,0,unsigned(i),1,*gpu.milliseconds);
                            if(*gpu.milliseconds>=80.0)veyra::log::info("gpu-stage-stall",std::format("source={} epoch={} revision={} stage={} gpuMs={:.3f}",sample.identity.sourceFrameId,sample.identity.epoch,sample.identity.settingsRevision,i,*gpu.milliseconds));
                        }
                    }
                };
                for(const auto& sample:graph.takeGpuTimings())record(sample);
                for(const auto& sample:presenter.takeGpuTimings(ctx.fence()))record(sample);
            };
            const bool traceSubframes=GetEnvironmentVariableW(L"VEYRA_TEST_TRACE_SUBFRAMES",nullptr,0)>0;
            // Shared with the presenter: resolve each batch on this single
            // object so SDK status and generated counts are consumed once.
            auto pollCompletions=[&]{
                diagnostics::CpuStallTrace trace("completion-stall");
                collectTimings();
                for(auto it=pendingCompletions.begin();it!=pendingCompletions.end();){
                    auto& watch=**it;auto& batch=watch.output;
                    for(unsigned i=0;i<batch.batch.count;++i){const auto& item=batch.batch.frames[i];
                        if(!watch.frameReadyObserved[i]&&item.lease&&item.lease->ready()){
                            watch.frameReadyObserved[i]=host100ns();
                            if(traceSubframes)traceFrame(diagnostics::TraceKind::FrameReady,item.identity,batch.batch.batchId,item.lease->readyFence,item.pts100ns,item.subframe,1,elapsedMs(watch.processStart));
                        }
                    }
                    if(!graph.resolveGeneration(batch)){++it;continue;}
                    // The GPU may finish between the first poll and resolve.
                    // Collect its timestamps before removing this watch.
                    if(!watch.gpuExecutionMs)collectTimings();
                    unsigned valid=0,invalid=0;
                    for(unsigned i=0;i<batch.batch.count;++i)if(batch.batch.frames[i].kind==pipeline::FrameKind::Generated){
                        if(batch.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++valid;else ++invalid;
                    }
                    watch.flow->ready(batch.batch.batchId,watch.real,valid,invalid,host100ns());
                    traceFrame(diagnostics::TraceKind::Ready,batch.batch.identity,batch.batch.batchId,std::max(batch.videoFenceValue,batch.genFenceValue),batch.batch.b100ns,invalid,valid,elapsedMs(watch.processStart));
                    if(watch.real)completedProcessing(batch.batch.identity);
                    // A reduced (2X) group must not lower the full-group FG
                    // cost estimate, but its base cost is a normal sample -
                    // see FgRecoveryBudget::complete. Only a history reset or
                    // an FG recovery is a warmup sample.
                    if(batch.batch.identity.settingsRevision==fgBudgetRevision)fgBudget.complete(watch.gpuExecutionMs,batch.fgEvaluated>0,batch.historyReset||batch.fgRecovery,host100ns(),watch.fgExecutionMs,!batch.fgReduced);
                    if(adaptiveCapturePhase&&watch.captureArrival>0&&valid==batch.fgCandidates&&
                       batch.hasGenerated&&!batch.historyReset&&!batch.fgRecovery&&
                       batch.batch.identity.settingsRevision==fgBudgetRevision&&watch.presentationGeneration==presentationGeneration.load()){
                        int64_t requiredDelay=0;
                        const auto observed=host100ns();
                        for(unsigned i=0;i<batch.batch.count;++i){const auto& item=batch.batch.frames[i];
                            // resolveGeneration can observe a fence after the
                            // first poll. Its current host time is a safe upper bound.
                            const auto ready=watch.frameReadyObserved[i]?watch.frameReadyObserved[i]:observed;
                            requiredDelay=std::max(requiredDelay,ready-watch.captureArrival+batch.batch.b100ns-item.pts100ns);
                        }
                        pairLatency.observe(observed,requiredDelay);
                    }
                    completeReset(batch.batch.identity);
                    watch.readyObserved=Clock::now();watch.ready=true;it=pendingCompletions.erase(it);
                }
                collectTimings();
            };
            if(!isImage){liveScheduler=std::make_unique<LiveGpuScheduler>();veyra::log::info("scheduler",std::format("single GPU owner thread={} capacity=2 source={} (bounded enhanced lookahead)",GetCurrentThreadId(),isCapture?"live":"file"));}
            PresentationCadence cadence;
            PresentationSettings presentationEffective;
            uint64_t pacingRevision=0,presenterGeneration=0;
            auto applyPresentation=[&]{
                PresentationSettings requested;uint64_t revision;
                {std::lock_guard lock(mutex_);requested=presentation_;revision=presentationRevision_;}
                if(revision==pacingRevision&&presenterGeneration==presenter.generation()){
                    // Reflex dropped out at runtime: the queue-depth part of the
                    // low-latency setting stays, so only the status line changes.
                    if(presentationEffective.enabled&&!presenter.reflexActive()&&presenter.reflexDisablePending()){
                        std::lock_guard lock(mutex_);snapshot_.presentationEffective=presentationEffective;snapshot_.presentationStatus=presenter.reflexDisablePending()?L"Reflex 调用及驱动撤销失败；请关闭视频后重试":L"Reflex 调用失败，当前使用低排队；详见日志";
                    }
                    if(presentationEffective.enabled&&!presenter.pacingActive()){
                        presentationEffective.enabled=false;
                        std::wstring message;
                        presentationEffective=presenter.configurePresentation(ctx,presentationEffective,options.fg,message);
                        cadence.reset();
                        std::lock_guard lock(mutex_);snapshot_.presentationEffective=presentationEffective;snapshot_.presentationStatus=L"显示队列等待失败；"+message;
                    }
                    return;
                }
                if(isImage)requested.enabled=false;
                std::wstring message;
                presentationEffective=presenter.configurePresentation(ctx,requested,options.fg,message);
                pacingRevision=revision;presenterGeneration=presenter.generation();cadence.reset();
                {std::lock_guard lock(mutex_);snapshot_.presentationEffective=presentationEffective;snapshot_.presentationRevision=revision;snapshot_.presentationStatus=message;snapshot_.presentationProviderOwned=presenter.providerOwnedPresentation();}
                veyra::log::info("pacing",std::format("revision={} requested={}/{}/{} effective={}/{}/{}",revision,requested.enabled,unsigned(requested.mode),unsigned(requested.display),presentationEffective.enabled,unsigned(presentationEffective.mode),unsigned(presentationEffective.display)));
            };
            auto drainLivePresentation=[&]{cadence.reset();if(liveScheduler){++presentationDrains;liveScheduler->cancel();pollCompletions();pendingCompletions.clear();}pairLatency.reset();};
            auto advanceLive=[&]{applyPresentation();if(liveScheduler){pollCompletions();if(stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0){liveScheduler->cancel();cadence.reset();}else liveScheduler->advance(host100ns());}};
            auto waitLive=[&]{const auto due=liveScheduler?liveScheduler->wakeAt():0;deadlineWait.slice(due>host100ns()?std::min(1.0,double(due-host100ns())/10000):1.0,physicalCapture?captureSource.frameEvent():nullptr,physicalCapture?gpuReadyEvent.handle:nullptr);};
            uint64_t metricsRevision=options.settings.revision,metricsEpoch=0,metricsWindowEpoch=0,statsSourceBase=0;
            uint64_t slotWaitBase=ring.cpuWaitCount(),submitBase=ring.submitCount();double slotWaitMsBase=ring.cpuWaitMilliseconds();
            std::shared_ptr<FrameCompletionRates> completionRates;
            source::CaptureMetrics captureFlowBase{},captureFlowLast{};
            uint64_t rateSkippedBase=0;
            // Queued steps capture these locals; cancel before their destruction.
            const bool injectSourceGap=options.captureReplayForTest&&GetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_SOURCE_GAP",nullptr,0)>0;
            std::optional<Clock::time_point> sourceGapUntil;
            // Master media clock for file playback: the audio device clock when
            // mapped, a monotonic anchor without audio, and its frozen value
            // while an endpoint recovers. Steady-state video scheduling reads
            // this clock; it never waits on audio coverage.
            auto nowMs=[&](){
                if(!audioStarted)return anchorMs+std::chrono::duration<double,std::milli>(Clock::now()-anchor).count()*playbackRate;
                publishAudioStatus();const double a=audio.mediaTimeMs();
                if(std::isfinite(a)){lastAudioClockMs=a;audioClockExhausted=false;return a;}
                // A disconnected endpoint freezes the shared media clock. Only a
                // fully exhausted audio stream may hand its tail to the wall clock.
                if(audioPipe.clockExhausted()){
                    if(!audioClockExhausted){audioClockExhausted=true;audioTailAnchor=Clock::now();}
                    return lastAudioClockMs+std::chrono::duration<double,std::milli>(Clock::now()-audioTailAnchor).count()*playbackRate;
                }
                return lastAudioClockMs;
            };
            uint64_t hdrDisplayCheck=0;
            bool captureRecovering=false;unsigned captureRetries=0;
            Clock::time_point captureRetryAt{};
            bool xessPresentationRecovery=false;
            auto recoverXessPresentation=[&]{
                if(!presenter.xessFailed()||FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
                auto reduced=options.snapshot();
                if(!disableFailedBackend(reduced,FailedBackend::Fg))return false;
                holdFileAudio();fileAudioAlignPending=true;
                drainLivePresentation();
                if(!ring.drainQueue()||!ring.discardRecording())return false;
                out={};hasOutput=false;
                // A failed scheduler stays failed after cancel. Recreate it on
                // its owner, then rebuild the SDK swapchain via settings below.
                if(liveScheduler)liveScheduler=std::make_unique<LiveGpuScheduler>();
                {
                    std::lock_guard lock(mutex_);
                    desired_.rejectVideoRequest(options.snapshot(),reduced);
                    desired_.revision=++nextRevision_;snapshot_.desired=desired_;snapshot_.applying=true;
                    snapshot_.backendWarning=L"XeSS 呈现失败，正在恢复基础播放；错误码见日志";
                }
                veyra::log::warn("backend-recovery",std::format("{} presentation failed; drained commands, requesting swapchain rebuild without FG","XeSS"));
                xessPresentationRecovery=true;
                reset=true;pendingResetCause=pipeline::ResetReason::Settings;
                return true;
            };
            // Declared LAST before the loop on purpose: it is destroyed first at
            // scope exit, so the scheduler (whose queued step/finish lambdas
            // capture the locals above by reference) is torn down before any of
            // those locals (sweep 2026-09-22 B1). Do not move declarations that
            // the lambdas reference below this line.
            OnExit stopPresentation{[&]{liveScheduler.reset();finishReset(stop_?diagnostics::ResetOutcome::Cancelled:diagnostics::ResetOutcome::Failed);std::lock_guard lock(mutex_);if(snapshot_.sessionId==runSessionId){if(frameFlow)snapshot_.metrics.flow=frameFlow->snapshot(host100ns());activeFlow_.reset();}}};
            while(!stop_){
                diagnostics::CpuStallTrace loopTrace("engine-stall",frames,45.0);
                collectTimings();
                loopTrace.mark("collect");
                if(captureRecovering){
                    if(Clock::now()<captureRetryAt){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
                    ++captureRetries;
                    {std::lock_guard lock(mutex_);snapshot_.captureReconnectAttempts=captureRetries;}
                    status(std::format(L"采集信号中断，正在重连原设备（第 {} 次）",captureRetries));
                    captureSource.setAudioIngress(unsigned(options.settings.captureAudio));
                    captureSource.setVerticalFlip(options.settings.captureFlipVertical);
                    captureSource.setBufferMode(unsigned(options.settings.captureBuffer));
                    if(captureSource.reconnect(muted_?0.0f:volume_.load(),unsigned(options.settings.audioSync),options.settings.audioOffsetMs)){
                        captureRecovering=false;reset=true;pendingResetCause=pipeline::ResetReason::DeviceLost;captureSampler.reset();
                        {std::lock_guard lock(mutex_);snapshot_.captureRecovering=false;}
                        status(L"采集设备已重新连接，等待新画面");
                    }else{
                        captureRetryAt=Clock::now()+std::chrono::seconds(std::min(5u,captureRetries));
                        continue;
                    }
                }
                if((gd.hdrInput||options.settings.videoHdr.enabled)&&GetTickCount64()-hdrDisplayCheck>2000){
                    diagnostics::CpuStallTrace hdrTrace("hdr-query-stall",frames);
                    hdrDisplayCheck=GetTickCount64();
                    const bool native=options.settings.useHdrPreview(gd.hdrInput,displayHdrActive());
                    if(native!=gd.hdrOutput){std::lock_guard lock(mutex_);if(desired_.revision==options.settings.revision){desired_.revision=++nextRevision_;snapshot_.desired=desired_;snapshot_.applying=true;}}
                }
                if(audioStarted)publishAudioStatus();
                if(audioStarted&&injectFileEndpointLoss&&!fileEndpointLossInjected&&frames>=20){audioPipe.requestEndpointLossForTest();fileEndpointLossInjected=true;}
                advanceLive();
                if(liveScheduler&&liveScheduler->failed()){
                    if(recoverXessPresentation())continue;
                    bool alive=ctx.reportDeviceFailure("live-presentation-failed");
                    // A GPU hang is noticed here about two seconds before Windows resets the
                    // GPU, and DRED only says which command list stopped once the device is
                    // removed (4070S field log 2026-10-01: every report still read alive).
                    // Wait for that, bounded, and report again.
                    for(int i=0;alive&&i<50;++i){Sleep(100);uint32_t removed=0;alive=ctx.checkDeviceAlive(removed);}
                    if(!alive)ctx.reportDeviceFailure("live-presentation-failed-after-reset");
                    status(alive?L"视频队列或呈现失败；GPU 未报告设备移除，请查看诊断":L"GPU 设备已移除或驱动已重置；请稍后重新连接，可切换 NR 运行库后重试",true);break;
                }
                const float gain=muted_?0.0f:volume_.load();audio.setGain(gain);
                if(physicalCapture)captureSource.recoverAudio(gain,unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                captureSource.setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                captureSource.setVerticalFlip(options.settings.captureFlipVertical);
                if(physicalCapture){const bool available=captureSource.setAudioGain(gain);const auto audioState=captureSource.audioState();std::lock_guard lock(mutex_);snapshot_.audioAvailable=available;snapshot_.captureAudio=audioState;snapshot_.audioInputChannels=audioState.inputChannels;snapshot_.audioOutputChannels=audioState.outputChannels;}
#ifdef VEYRA_ENABLE_REMOTEPLAY
                if(remote){remote->setAudioGain(gain);remote->setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                    const auto state=remote->audioState();const auto session=remote->sessionSnapshot();const auto rates=remote->rates();
                    std::lock_guard lock(mutex_);snapshot_.audioAvailable=state.available;snapshot_.captureAudio=state;snapshot_.remotePlayState=int(session.state);snapshot_.remotePlaySkipped=remote->skipped();snapshot_.remoteReceivedFps=rates.receivedFps;snapshot_.remoteDecodedFps=rates.decodedFps;snapshot_.remoteRatesReady=rates.ready;snapshot_.remoteReceived=rates.received;snapshot_.remoteDecoded=rates.decoded;snapshot_.remoteIngressDropped=rates.ingressDropped;}
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){moon->setAudioGain(gain);moon->setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                    const auto state=moon->audioState();
                    std::lock_guard lock(mutex_);snapshot_.audioAvailable=state.available;snapshot_.captureAudio=state;}
#endif
#ifdef VEYRA_ENABLE_XBOX
                if(xb){xb->setAudioGain(gain);xb->setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                    const auto state=xb->audioState();
                    std::lock_guard lock(mutex_);snapshot_.audioAvailable=state.available;snapshot_.captureAudio=state;}
#endif
                // Save the latest processed real frame before a settings transaction
                // invalidates it (live rendering can be one batch behind processing).
                std::wstring save;EnhancementSettings requested;std::optional<ChainRuntimeOrder> requestedOrder;
                {std::lock_guard lock(mutex_);requested=desired_;requestedOrder=desiredNodeOrder_;if(hasOutput)save.swap(savePath_);}
                if(!save.empty()){try{sink::RgbaImage result;const auto e=std::filesystem::path(save).extension().wstring();
                    drainLivePresentation();
                    if(graph.hdrOutput()){auto hdrPath=std::filesystem::path(save);hdrPath.replace_extension(L".jxr");if(sink::saveHdrScreenshot(hdrPath.wstring(),ctx,ring,graph.videoFrameResource(out.videoSlot)))status(L"HDR截图已保存："+hdrPath.wstring());else status(L"HDR截图保存失败，请查看日志",false);}else if(!sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),result)||!sink::saveImage(save,result,e==L".jpg"||e==L".jpeg"))status(L"保存失败（目标文件可能已存在），播放已保留",false);else{status(L"图片已保存："+save);veyra::log::info("image-save",std::format("saved extent={}x{} revision={}",gd.workWidth,gd.workHeight,options.settings.revision));}}
                    catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining session: {}",e.what()));status(L"保存异常，播放已保留；可再次保存",false);}}
                if(isImage)requested.multiplier=1;
                if(requested.revision==options.settings.revision&&requested!=options.settings){
                    options.settings.audioSync=requested.audioSync;options.settings.audioOffsetMs=requested.audioOffsetMs;
                    captureSource.setAudioSync(unsigned(requested.audioSync),requested.audioOffsetMs);
                    // Audio ingress is negotiated when the capture graph is
                    // built: record it now and tell the user a reconnect applies
                    // it instead of pretending it is already live.
                    if(requested.captureAudio!=options.settings.captureAudio){
                        options.settings.captureAudio=requested.captureAudio;
                        captureSource.setAudioIngress(unsigned(requested.captureAudio));
                        status(L"采集音频模式已记录；重新连接采集卡后生效",false);
                    }
                    // The manual capture flip is applied per sample on the
                    // DirectShow callback thread, so it is a live edit.
                    if(requested.captureFlipVertical!=options.settings.captureFlipVertical){
                        options.settings.captureFlipVertical=requested.captureFlipVertical;
                        captureSource.setVerticalFlip(requested.captureFlipVertical);
                        status(requested.captureFlipVertical?L"采集画面已上下翻转（仅影响本机采集画面）":L"采集画面方向已恢复",false);
                    }
                    // The video-pin allocator is created while the graph is
                    // built: record the mode now and let the reconnect below
                    // (or the next connect) apply it.
                    if(requested.captureBuffer!=options.settings.captureBuffer){
                        options.settings.captureBuffer=requested.captureBuffer;
                        captureSource.setBufferMode(unsigned(requested.captureBuffer));
                        status(L"设备缓冲模式已记录；重新连接采集卡后生效",false);
                    }
                    std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;
                    veyra::log::info("settings",std::format("Audio applied videoRevision={} mode={} offsetMs={} (video history retained)",requested.revision,unsigned(requested.audioSync),requested.audioOffsetMs));
                }
                const auto previous=options.snapshot();const auto previousDesc=gd;const auto previousOrder=options.nodeOrder;bool transaction=false;
                if(requested.revision!=previous.revision){
                    holdFileAudio();
                    finishReset(diagnostics::ResetOutcome::Cancelled);
                    resetStart=resetStageStart=Clock::now();
                    resetRecord=diagnostics::FrameFlowMetrics::ResetRecord{};
                    resetRecord->sessionId=runSessionId;resetRecord->settingsRevision=requested.revision;
                    resetRecord->reason=static_cast<uint8_t>(pipeline::ResetReason::Settings);
                    if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                    drainLivePresentation();
                    // Rebuilding the video graph must not tear down a healthy
                    // capture audio endpoint; only the new video mapping is
                    // invalidated here. Audio transport reset is reserved for
                    // an actual pause/stop or input discontinuity.
                    if(physicalCapture)captureSource.videoReset(false);
#ifdef VEYRA_ENABLE_REMOTEPLAY
                    if(remote)remote->videoReset(false);
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                    if(moon)moon->videoReset(false);
#endif
#ifdef VEYRA_ENABLE_XBOX
                    if(xb)xb->videoReset(false);
#endif

                    // A drain may outlive several slider notifications. Build
                    // only the latest pending configuration at this boundary.
                    {std::lock_guard lock(mutex_);requested=desired_;requestedOrder=desiredNodeOrder_;}
                    if(requested.revision==previous.revision){finishReset(diagnostics::ResetOutcome::Cancelled);continue;}
                    resetRecord->settingsRevision=requested.revision;
                    auto next=PlayerOptions::from(requested,requestedOrder);auto nextDesc=gd;
                    const bool nvidiaAdapter=ctx.adapter().isNvidia;
                    const bool xessFg=crossVendorFrameGeneration(next.settings.frameGenerationBackend);
                    describeStages(stageRequest(next),next.snapshot(),nextDesc);
                    nextDesc.hdrOutput=requested.useHdrPreview(nextDesc.hdrInput,displayHdrActive());
                    // Rebuild decision.
                    //  - requiresGraphRebuild(): everything derivable from the
                    //    settings (stages, order, sizes inputs, master switches,
                    //    LUT reference). Verified against the legacy
                    //    field-by-field expression over six settings smoke cases
                    //    (master, settings, fg-only, view, output-cap, rollback)
                    //    with zero disagreements before the legacy list was
                    //    removed.
                    //  - the rest cannot come from the settings alone: the
                    //    effective HDR output depends on the display, the
                    //    extents come from the resolution plan, and a non-NVIDIA
                    //    adapter resolves requested effects differently than the
                    //    settings record.
                    bool rebuild=engine::requiresGraphRebuild(previous,requested)||previousOrder!=requestedOrder;
                    if(gd.hdrOutput!=nextDesc.hdrOutput||
                       (!nvidiaAdapter&&(next.nr||next.sr||(next.fg&&!xessFg)))||
                       gd.enableNr!=nextDesc.enableNr||gd.enableFg!=nextDesc.enableFg||
                       gd.nrBeforeSr!=nextDesc.nrBeforeSr||
                       gd.workWidth!=nextDesc.workWidth||gd.workHeight!=nextDesc.workHeight||
                       gd.nrWidth!=nextDesc.nrWidth||gd.nrHeight!=nextDesc.nrHeight||
                       gd.flowWidth!=nextDesc.flowWidth||gd.flowHeight!=nextDesc.flowHeight)rebuild=true;
                    bool accepted=ring.drainQueue();out={};hasOutput=false;
                    resetRecord->rebuilt=rebuild;
                    markResetStage(diagnostics::ResetStage::Drain);
                    if(!accepted){finishReset(diagnostics::ResetOutcome::Failed);status(L"设置切换排空失败，已停止",true);break;}
                    backendRecoveryWarning.clear();
                    const bool preserveWorkingFg=previous.multiplier>1&&requested.multiplier>1&&
                        (previous.frameGenerationBackend!=requested.frameGenerationBackend||previous.multiplier!=requested.multiplier);
                    const bool preserveWorkingNr=previous.nr&&requested.nr&&previous.nrRuntime!=requested.nrRuntime;
                    if(accepted&&rebuild){presenter.close();graph.shutdown();markResetStage(diagnostics::ResetStage::Destroy);accepted=initializePreview(nextDesc,next,preserveWorkingFg,preserveWorkingNr);
                        markResetStage(diagnostics::ResetStage::Create);
                        if(!accepted){presenter.close();graph.shutdown();if(!graph.initialize(gd)||!presenter.open(ctx,window,graph,options.settings.captureCompatible,!isCapture&&!isImage)||!graph.createViews()){status(L"设置失败且旧资源恢复失败，已停止",true);break;}}
                    }else if(accepted)accepted=graph.applySettings(requested);
                    if(rebuild)publishFrameGenerationCapabilities();
                    if(accepted){
                        if(xessPresentationRecovery){backendRecoveryWarning=L"XeSS 呈现失败，已关闭补帧并恢复播放；错误码见日志";xessPresentationRecovery=false;}
                        if(!backendRecoveryWarning.empty()){
                            std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,next.snapshot());snapshot_.desired=desired_;snapshot_.backendWarning=backendRecoveryWarning;
                        }
                        options=next;gd=nextDesc;transaction=true;reset=true;
                        resetRecord->epoch=0;
                        if(!nvidiaAdapter&&(next.nr||next.sr||(next.fg&&!xessFg)))veyra::log::warn("capability",std::format("non-NVIDIA adapter disabled requested settings revision={} nr={} sr={} fgBackend={} flowBackend={}",requested.revision,next.nr,next.sr,frameGenerationBackendName(requested.frameGenerationBackend),opticalFlowBackendName(requested.opticalFlowBackend)));
                    }
                    else {
                        finishReset(diagnostics::ResetOutcome::RolledBack);
                        std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=previous;snapshot_.status=L"设置应用失败，已恢复上一套参数";snapshot_.backendWarning=requested.nrRuntime!=previous.nrRuntime?L"NR运行版本切换失败，已恢复上一套参数":requested.frameGenerationBackend==FrameGenerationBackend::XeSS?L"XeSS 未能启用，已恢复上一套参数":requested.frameGenerationBackend==FrameGenerationBackend::Fsr4?L"FSR 4 ML 未能启用（需要支持的 RX 9000 和驱动），已恢复上一套参数":fsrFrameGeneration(requested.frameGenerationBackend)?L"FSR 3.1 帧生成未能启用，已恢复上一套参数":L"后端切换失败，已恢复上一套参数";}
                }
                // Live uniform update. The revision identifies the graph *shape*
                // (NR/SR/FG/resolution and the colour master switch); colour
                // parameters keep the same revision on purpose, so they must be
                // pushed into the running graph here - no rebuild, no history
                // reset. Without this the panel only took effect after an
                // unrelated rebuild (toggling NR): the "sliders do nothing" report.
                if(requested.revision==previous.revision&&!(requested==previous)){
                    if(graph.applySettings(requested)){
                        options=PlayerOptions::from(requested,requestedOrder);
                        {std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;}
                        veyra::log::info("settings",std::format("live parameter update revision={} colourEnabled={} exposure={:.2f} contrast={:.2f} temperature={:.2f}",requested.revision,requested.color.enabled?1:0,requested.color.exposure,requested.color.contrast,requested.color.temperature));
                        // Colour lives inside the ingest dispatch, so a paused
                        // frame has no process() call to upload the new tables or
                        // re-run the chain: remember that the cached frame must
                        // be re-rendered before the next present.
                        if(paused_||isImage)refreshPausedFrame_=true;
                    }else{
                        std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.applying=desired_!=previous;snapshot_.status=L"这套参数需要重建管线，已回到上一套数值";
                    }
                }
                if(stop_)break;
                int track=-1;double trackPosition=0;
                {std::lock_guard lock(mutex_);if(pendingAudioSession_==snapshot_.sessionId)track=pendingAudioTrack_;pendingAudioTrack_=-1;trackPosition=snapshot_.position;}
                bool switchedAudio=false;
                if(track>=0&&audioStarted&&track!=audioPipe.selectedTrack()){
                    const double clock=audio.mediaTimeMs();if(std::isfinite(clock))trackPosition=clock/1000;
                    holdFileAudio();audioPipe.stopThread();
                    const bool changed=audioPipe.selectTrack(track);
                    audioPipe.setPaused(paused_);audioPipe.startThread(&audio,true,std::max(0.0,trackPosition*1000));
                    switchedAudio=true;
                    {std::lock_guard lock(mutex_);snapshot_.selectedAudioTrack=audioPipe.selectedTrack();snapshot_.audioInputChannels=audioPipe.pcmFormat().channels;
                        if(!changed)snapshot_.backendWarning=L"音轨切换失败，已保留原音轨";
                        if(seekSeconds_.load()<0){snapshot_.seekTarget=trackPosition;++snapshot_.seekRequested;seekSeconds_=trackPosition;}}
                    log::info("audio-track",std::format("requested={} selected={} changed={} positionSeconds={:.3f}",track,audioPipe.selectedTrack(),changed,trackPosition));
                }
                if(!isImage&&!isCapture&&playbackRate!=requestedPlaybackRate_.load()){
                    playbackRate=requestedPlaybackRate_.load();audioPipe.setPlaybackRate(playbackRate);
                    playbackSpeedLastWall={};cadence.reset();
                }
                double seek;{std::lock_guard lock(mutex_);seek=seekSeconds_.exchange(-1);if(seek>=0)activeSeekId=snapshot_.seekRequested;}
                if(seek>=0&&!isImage&&!isCapture){
                    seekStarted=Clock::now();seekDecoded=0;
                    holdFileAudio();fileAudioAlignPending=true;
                    drainLivePresentation();
                    if(!ring.drainQueue()||!activeSource->seek({static_cast<int64_t>(seek*1000000),1000000})){status(L"跳转失败",true);break;}
                    veyra::log::info("seek-latency",std::format("request={} targetSeconds={:.3f} drainAndDemuxMs={:.3f}",activeSeekId,seek,elapsedMs(seekStarted)));
                    initialFileFramePending=false;av_frame_free(&cachedFrame);out={};hasOutput=false;
                    discardBefore=seek*1000;seekPreviewPending=true;reset=true;pendingResetCause=pipeline::ResetReason::Seek;anchorMs=discardBefore;anchor=Clock::now();lastAudioClockMs=discardBefore;audioClockExhausted=false;audioRebuffering=false;if(audioStarted){audioPipe.setPaused(paused_);if(!switchedAudio||std::abs(seek-trackPosition)>.001)audioPipe.requestSeek(discardBefore);}
                }
                if(!transaction&&((paused_&&!seekPreviewPending)||(isImage&&hasOutput))){
                    // A parameter change while paused (or on a still image) only
                    // reaches the picture if the cached source frame is pushed
                    // through the graph again: the grade is fused into the ingest
                    // dispatch, so nothing else would upload the new tables.
                    if(refreshPausedFrame_){
                        refreshPausedFrame_=false;
                        if(cachedFrame){
                            pipeline::EnhanceGraph::FrameOutputs refreshed;
                            const double refreshPtsMs=cachedPacket.pts.toDouble()*1000.0;
                            if(graph.process(cachedFrame,refreshPtsMs,true,refreshed,cachedPacket.sequence,&cachedPacket.colorInfo,&cachedPacket.hardwareSurface,false)){
                                out=refreshed;
                                hasOutput=true;
                                {std::lock_guard lock(mutex_);++snapshot_.pausedFrameRefreshes;}
                                veyra::log::info("settings",std::format("paused frame re-rendered with the new parameters ptsMs={:.3f}",refreshPtsMs));
                            }else{
                                veyra::log::warn("settings","paused frame re-render failed; keeping the previous output");
                            }
                        }
                    }
                    drainLivePresentation();
                    if(!wasPaused){holdFileAudio();if(physicalCapture)captureSource.videoReset();
#ifdef VEYRA_ENABLE_REMOTEPLAY
                    if(remote)remote->videoReset();
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                    if(moon)moon->videoReset();
#endif
#ifdef VEYRA_ENABLE_XBOX
                    if(xb)xb->videoReset();
#endif
}
                    if(audioStarted)audioPipe.setPaused(true);wasPaused=true;
                    const bool referencesValid=hasOutput&&out.batch.count&&out.batch.frames[out.batch.count-1].lease&&out.batch.frames[out.batch.count-1].lease->referencesValid;
                    if(hasOutput&&!presenter.present(ctx,ring,graph,out.videoSlot,false,referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,out.batch.identity,previewView())){if(recoverXessPresentation())continue;status(L"画面呈现失败",true);break;}
                    if(hasOutput&&graph.resolveGeneration(out))completeReset(out.batch.identity);
                    // Paused: the swapchain already holds the frame; re-present
                    // (present() handles resize and takes the view each call)
                    // at 50 ms instead of 16 ms unless a parameter refresh is due.
                    std::this_thread::sleep_for(std::chrono::milliseconds(refreshPausedFrame_?1:50));continue;
                }
                if(wasPaused&&!paused_){holdFileAudio();audioRebuffering=false;if(audioStarted)audioPipe.setPaused(false);anchor=Clock::now();anchorMs=out.ptsMs;reset=true;pendingResetCause=pipeline::ResetReason::PauseResume;wasPaused=false;}
                // Backpressure before reading the capacity-one source mailbox:
                // when a lease frees we consume the newest available sample.
                if(liveScheduler&&!transaction){
                    const auto enhancementPending=[&]{
                        if(presentationEffective.enabled&&!options.fg&&(isCapture||!fileAwaitingVideo)&&liveScheduler->occupancy()>=1)return true;
                        // Live sources: never stack a second frame behind one the GPU
                        // is still enhancing. When NR/SR cost more than one source
                        // interval, a second queued frame doubled capture-to-present
                        // latency (3 NR layers at 1440p60: 49 ms for 20 ms of GPU
                        // work); waiting and then taking the newest mailbox sample
                        // keeps it at one GPU pass. No effect while the GPU keeps up.
                        return isCapture&&!(options.fg&&presentSinkFrameGeneration(options.settings.frameGenerationBackend))&&!pendingCompletions.empty();
                    };
                    const auto leaseWaitStart=Clock::now();
                    while((liveScheduler->occupancy()>=2||!graph.nextFrameSlotAvailable()||enhancementPending())&&!stop_&&(!paused_||seekPreviewPending)&&!liveScheduler->failed()){
                        advanceLive();if(seekSeconds_>=0)break;
                        if(liveScheduler->occupancy()==0&&!graph.nextFrameSlotAvailable()&&elapsedMs(leaseWaitStart)>2000){status(L"等待输出纹理释放超时",true);stop_=true;break;}
                        if(liveScheduler->occupancy()>=2||!graph.nextFrameSlotAvailable()||enhancementPending())waitLive();
                        std::lock_guard lock(mutex_);if(desired_.revision!=options.settings.revision)break;
                    }
                    if(stop_||seekSeconds_>=0||(paused_&&!seekPreviewPending)||liveScheduler->failed()||liveScheduler->occupancy()>=2||!graph.nextFrameSlotAvailable()||enhancementPending())continue;
                }
                // Wait before selecting the latest sample, not after enhancement.
                if(!presenter.beginSourceInput()){status(L"XeSS input timing failed",true);break;}
                const auto decodeStart=Clock::now();
                loopTrace.mark("controlAndSchedule");
                if(injectSourceGap&&frames>=12){
                    if(!sourceGapUntil){sourceGapUntil=Clock::now()+std::chrono::milliseconds(400);veyra::log::info("capture-test","inject 400ms Waiting before next source read");}
                    if(Clock::now()<*sourceGapUntil){advanceLive();waitLive();continue;}
                }
                pipeline::FramePacket pkt;const AVFrame* frame=imageFrame;
                if(cachedFrame&&((initialRemoteFramePending||initialFileFramePending)||(transaction&&(!isCapture||paused_)))){frame=cachedFrame;pkt=cachedPacket;initialRemoteFramePending=false;initialFileFramePending=false;}
                else if(!isImage){
                    auto rs=physicalCapture?captureSource.tryRead(pkt,&frame):activeSource->read(pkt,&frame);
                    // Keep the accepted transaction and rollback state alive until
                    // the next real capture sample arrives. Never bind old PTS to now.
                    while(transaction&&isCapture&&rs==source::SourceReadStatus::Waiting&&!stop_){
                        if(paused_&&cachedFrame){frame=cachedFrame;pkt=cachedPacket;rs=source::SourceReadStatus::Frame;break;}
                        advanceLive();waitLive();rs=physicalCapture?captureSource.tryRead(pkt,&frame):activeSource->read(pkt,&frame);
                    }
                    if(stop_)break;
                    if(rs==source::SourceReadStatus::Waiting){if(isScreen){std::lock_guard lock(mutex_);snapshot_.sourceNotice=screenSource.status();}advanceLive();waitLive();continue;}
                    if(rs==source::SourceReadStatus::Eos){
                        fileInputEnded=true;
                        while(liveScheduler&&liveScheduler->occupancy()&&!stop_&&(!paused_||seekPreviewPending)&&!liveScheduler->failed()){advanceLive();if(liveScheduler->occupancy())waitLive();}
                        if(stop_)break;
                        if(liveScheduler&&liveScheduler->failed()){status(L"视频尾帧呈现失败，请查看诊断",true);break;}
                        if(paused_)continue;
                        collectTimings();seekPreviewPending=false;status(L"视频已播放完毕");paused_=true;{std::lock_guard lock(mutex_);snapshot_.transport=TransportState::Ended;}continue;
                    }
                    if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()){
                        if(physicalCapture){
                            drainLivePresentation();if(!ring.drainQueue()){status(L"采集恢复时GPU排空失败",true);break;}
                            captureSource.videoReset();av_frame_free(&cachedFrame);hasOutput=false;out={};
                            captureRecovering=true;captureRetries=0;captureRetryAt=Clock::now()+std::chrono::milliseconds(500);
                            {std::lock_guard lock(mutex_);snapshot_.captureRecovering=true;snapshot_.captureFps=0;}
                            status(L"采集信号中断，等待原设备恢复");continue;
                        }
#ifdef VEYRA_ENABLE_REMOTEPLAY
                        if(remote){const auto recovery=remote->recoveryStatus();status(recovery.message.empty()?L"PS5 串流异常，请检查主机状态后重新连接。":recovery.message,true);break;}
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                        if(moon){const auto message=moon->stats().message;status(message.empty()?L"串流中断，请检查主机和网络后重新连接。":message,true);break;}
#endif
#ifdef VEYRA_ENABLE_XBOX
                        if(xb){const auto message=xb->stats().message;status(message.empty()?L"Xbox 串流中断，请重新连接。":message,true);break;}
#endif
                        status(isScreen?screenSource.status():isCapture?L"采集信号中断，请检查设备连接或格式":source.errorMessage().empty()?L"视频解码或时间戳错误":source.errorMessage(),true);break;}
                }
                if(isScreen){std::lock_guard lock(mutex_);snapshot_.sourceNotice=screenSource.status();}
                if((isStream||isScreen)&&frame&&(uint32_t(frame->width)!=width||uint32_t(frame->height)!=height||(isScreen&&gd.hdrInput!=activeSource->info().color.isHdrPath()))){
                    drainLivePresentation();if(!ring.drainQueue()){status(L"串流尺寸切换排空失败",true);break;}
                    width=uint32_t(frame->width);height=uint32_t(frame->height);
                    describeStages(stageRequest(options),options.snapshot(),gd);
                    gd.hdrInput=!isImage&&activeSource->info().color.isHdrPath();gd.hdrOutput=options.settings.useHdrPreview(gd.hdrInput,displayHdrActive());
                    presenter.close();graph.shutdown();out={};hasOutput=false;
                    if(!graph.initialize(gd)||!presenter.open(ctx,window,graph,options.settings.captureCompatible,!isCapture&&!isImage)||!graph.createViews()){status(L"串流尺寸切换失败",true);break;}
                    reset=true;pendingResetCause=pipeline::ResetReason::Resize;
                }
                if(vramRecoverStep){
                    // Video memory watchdog (see watchVram): rebuild between frames, measure later.
                    const int step=vramRecoverStep;vramRecoverStep=0;
                    drainLivePresentation();if(!ring.drainQueue()){status(L"显存回收时排空失败",true);break;}
                    uint64_t budget=0;ctx.videoMemoryInfo(budget,vramRecoverBefore);
                    veyra::log::warn("vram-watch",std::format("rebuilding the {} to release video memory (usage {} MiB)",step==1?"presentation swapchain":"processing graph and swapchain",vramRecoverBefore>>20));
                    presenter.close();
                    if(step==2){graph.shutdown();out={};hasOutput=false;}
                    if(step==1&&testLeakReleases)testLeak.clear();
                    if((step==2&&!graph.initialize(gd))||!presenter.open(ctx,window,graph,options.settings.captureCompatible,!isCapture&&!isImage)||(step==2&&!graph.createViews())){status(L"显存回收后重建失败，请重新打开片源",true);break;}
                    reset=true;pendingResetCause=pipeline::ResetReason::Resize;
                    vramRecoverDone=step;vramRecoverCheckAt=Clock::now()+std::chrono::seconds(3);
                }
                const bool rereadCached=transaction&&frame==cachedFrame;
                if(!isImage&&!isCapture&&seekSeconds_>=0)continue;
                const bool halfRate=isCapture&&options.settings.content==ContentRate::Capture60To30&&
                    CaptureHalfRate::supported(activeSource->info().averageFps);
                if(reset||pipeline::breaksHistory(pkt.flags))captureSampler.reset();
                if(halfRate&&!rereadCached){
                    const auto interval=liveSourceInterval100ns(pipeline::Rational::unknown(),activeSource->info().averageFps);
                    if(!pkt.pts.isUnknown()&&!captureSampler.accept(pkt.pts.to100ns(),interval,false)){
                        std::lock_guard lock(mutex_);++snapshot_.captureRateSkipped;continue;
                    }
                    pkt.duration={interval*2,10000000};
                }
                if(!halfRate)captureSampler.reset();
                if(isImage)pkt.sequence=1;
                if(!rereadCached)++sourceFrames;
                const double decodeMs=elapsedMs(decodeStart);decodeTimes.add(decodeMs);
                loopTrace.mark("read");
                double pts=isImage?0:pkt.pts.toDouble()*1000;
                if(isCapture&&frames==0){anchor=Clock::now();anchorMs=pts;}
                if(seekPreviewPending)++seekDecoded;
                if(pts+0.1<discardBefore)continue;
                auto retainCandidate=[&](){
                    if(frame==cachedFrame)return true;
                    AVFrame* owned=av_frame_clone(frame);
                    if(!owned)return false;
                    av_frame_free(&cachedFrame);cachedFrame=owned;cachedPacket=pkt;frame=cachedFrame;
                    return true;
                };
                // Realtime preview candidates: decode stays ordered and
                // reference-lossless; an expired candidate loses only its
                // enhancement/display opportunity (plan §4.3). The open/seek
                // anchor, paused seek preview and settings replay never drop.
                if(!isCapture&&!isImage&&!transaction&&!fileAwaitingVideo&&!seekPreviewPending){
                    const double previewIntervalMs=double(liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps))/10000.0;
                    bool previewSourceFailed=false;
                    while(!stop_&&previewCandidateExpired(nowMs(),pts,previewIntervalMs)){
                        // read() can clear the decoder's borrowed AVFrame even
                        // on EOS/EAGAIN. Own this candidate before looking ahead.
                        if(!retainCandidate()){previewSourceFailed=true;break;}
                        pipeline::FramePacket next;const AVFrame* raw=nullptr;
                        const auto rs=activeSource->read(next,&raw);
                        if(rs==source::SourceReadStatus::Eos)break; // final decoded candidate is kept
                        if(rs!=source::SourceReadStatus::Frame||next.pts.isUnknown()){previewSourceFailed=true;break;}
                        const double nextPts=next.pts.toDouble()*1000;
                        if(nextPts+0.1<discardBefore)continue; // post-seek decode residue keeps dropping
                        pkt=next;frame=raw;
                        pts=nextPts;
                        ++previewSkippedTotal;++previewSkippedSinceSubmit;previewSkipSinceProcess=true;
                    }
                    if(previewSourceFailed){status(source.errorMessage().empty()?L"视频读取或候选帧保存失败":source.errorMessage(),true);break;}
                }
                if(!retainCandidate()){status(L"无法保存解码帧，已停止播放",true);break;}
                const auto processStart=Clock::now();
                const auto captureArrival=pkt.arrivalHost100ns?pkt.arrivalHost100ns:host100ns();
                // A PS5 callback is a compressed AU, not a decoded video frame.
                // Its decode baseline must not consume the entire FG lookahead
                // budget. Still measure full ingress latency from captureArrival.
                const auto liveInputReady=isStream&&pkt.decodedHost100ns>0?pkt.decodedHost100ns:captureArrival;
                const auto sourceArrival=pkt.arrivalHost100ns?pkt.arrivalHost100ns:std::chrono::duration_cast<std::chrono::nanoseconds>(decodeStart.time_since_epoch()).count()/100;
                // A skipped preview candidate breaks the temporal span the
                // motion/NR/FG history was built on: the next processed frame
                // is an explicit history reset (plan §4.4). It is a SOFT
                // break: metrics windows, completion predictions and reset
                // lifecycle records continue (only hard resets — open/seek/
                // pause/discontinuity/settings — reopen them).
                const bool hardFrameReset=reset||pipeline::breaksHistory(pkt.flags&~static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop));
                // A bounded preview skip (at most two dropped candidates, no
                // source discontinuity) keeps NR/FG/XeSS history: the graph's
                // A is still the last PROCESSED frame with its real PTS, so
                // motion, generated-frame PTS and temporal blending stay on
                // the true timeline, merely spanning 2-3 source periods.
                // Clearing history here made 25% of presented real frames
                // reset frames under load (review 2026-09-22): XeSS skipped
                // generation, NR reseeded, DLSS lost the whole group. Larger
                // skips still reset. VEYRA_TEST_PREVIEW_SKIP_RESET restores
                // the unconditional reset for A/B runs.
                static const bool skipAlwaysResets=GetEnvironmentVariableW(L"VEYRA_TEST_PREVIEW_SKIP_RESET",nullptr,0)>0;
                // Retain temporal history across a skip when the gap to the
                // last processed frame is short enough that optical flow can
                // still track it. Two earlier criteria were wrong:
                //   - the count bound (<=2) used previewSkippedSinceSubmit,
                //     which is cleared on SUBMIT, not on process, so skips
                //     accumulated across frames that were processed but not
                //     submitted and tripped the bound spuriously;
                //   - a count says nothing about the actual temporal gap.
                // Measured 2026-09-22 on a user report of NR flicker under
                // load: when the scheduler falls behind it skips 4-10
                // candidates per second, so the bound never held, every burst
                // forced kReset=1 into the NR feature, and the denoiser
                // re-converged several times a second - visible as flicker at
                // exactly the drop rate. A gap this small is just a briefly
                // lower frame rate; it is not new content.
                // Capture drops land here too. Drop means the mailbox discarded
                // stale frames because WE were slow; the content is continuous
                // and the next frame is simply further along - the same
                // situation as a preview skip. It is already excluded from
                // hardFrameReset above and from the presentation reset policy,
                // but it still reached temporalBreak below, so every capture
                // drop reset NR and handed the XeSS/FSR provider reset=1. That
                // re-warm at the capture drop rate is the flicker reported for
                // live capture (2026-09-22). Real breaks still arrive as Seek,
                // Cut, Discontinuity, Resize, PauseResume or DeviceLost.
                constexpr double kRetainHistoryGapMs=250.0;
                const auto flagsWithoutDrop=pkt.flags&~static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);
                const bool captureDropped=pipeline::hasFrameFlag(pkt.flags,pipeline::FrameFlagBits::Drop);
                const bool gapCandidate=previewSkipSinceProcess||captureDropped;
                const double skipGapMs=gapCandidate&&std::isfinite(lastProcessedPtsMs)?pts-lastProcessedPtsMs:0.0;
                const bool boundedSkip=gapCandidate&&!skipAlwaysResets&&
                    std::isfinite(lastProcessedPtsMs)&&skipGapMs>0&&skipGapMs<=kRetainHistoryGapMs&&
                    !pipeline::breaksHistory(flagsWithoutDrop);
                if(boundedSkip){++previewSkipRetained;
                    if(host100ns()>=nextPreviewSkipLog){nextPreviewSkipLog=host100ns()+10000000;
                        veyra::log::info("preview-skip",std::format("retained history across a {:.1f} ms gap source={} retainedTotal={} skippedTotal={} (bounded skip; no NR/FG/XeSS reset)",skipGapMs,pkt.sequence,previewSkipRetained,previewSkippedTotal));}
                }
                const bool temporalBreak=pipeline::breaksHistory(flagsWithoutDrop)||(gapCandidate&&!boundedSkip);
                const bool previewOnlyReset=!hardFrameReset&&temporalBreak;
                if(hardFrameReset)++metricsWindowEpoch;
                const bool historyReset=hardFrameReset||temporalBreak;
                if(historyReset)++historyResets;
                if(presentationGenerationResetRequired(reset,pkt.flags))++presentationGeneration;
                // A mailbox Drop must reset SR/NR/flow/FG history, but draining
                // the two-batch presenter here discarded completed real frames.
                if(liveScheduler&&presentationDrainRequired(reset,pkt.flags))drainLivePresentation();
                const bool injectedReject=transaction&&!options.nr&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr,0)>0;
                if(injectedReject)veyra::log::error("settings-test","test-only reject NR-disable transaction before graph process; no driver failure");
                pipeline::EnhanceGraph::FgAdmission admitFg;
                const auto logAdmission=[&](const pipeline::FrameBatch& batch,int64_t now,int64_t deadline,double elapsed,double blit,bool admitted,bool warmingHistory,const char* timeline,int64_t firstDeadline=0,double queuedMs=0){
                    if(admitted)++fgAdmittedPairs;else ++fgRejectedPairs;
                    // Keep the first rejection even if the periodic sample just
                    // logged a success. Both channels remain rate bounded.
                    if(now<nextFgAdmissionLog&&(admitted||now<nextFgRejectionLog))return;
                    nextFgAdmissionLog=now+10000000;if(!admitted)nextFgRejectionLog=now+10000000;
                    veyra::log::info("live-fg-admission",std::format("timeline={} revision={} source={} multiplier={} requestedMultiplier={} admitted={} admittedPairs={} rejectedPairs={} remainingDeadlineMs={:.3f} firstDeadlineMs={:.3f} queuedGpuMs={:.3f} predictedMs={:.3f} elapsedMs={:.3f} blitGpuP95Ms={:.3f} presentCpuP95Ms={:.3f} warmingHistory={} (CPU Present is backpressure, not added GPU cost; firstDeadlineMs=-1 when unused)",timeline,batch.identity.settingsRevision,batch.identity.sourceFrameId,previewFgMultiplier,options.fgMultiplier,admitted,fgAdmittedPairs,fgRejectedPairs,double(deadline-now)/10000,firstDeadline?double(firstDeadline-now)/10000:-1,queuedMs,fgBudget.predicted(now,warmingHistory).value_or(-1),elapsed,blit,livePresent.p95(),warmingHistory));
                };
                if(fgBudgetRevision!=options.settings.revision){fgBudget.reset();pairLatency.reset();fgBudgetRevision=options.settings.revision;}
                previewFgMultiplier=options.fgMultiplier;
                // An output cap used to only drop candidates at present time, so
                // the GPU still generated frames that were thrown away: at 6X on
                // a 100 Hz panel we generated ~360/s, submitted ~276/s, and the
                // panel could show at most 100 (measured 2026-09-22). Derive the
                // smallest multiplier whose output still reaches the cap and use
                // that instead. Only ever lowers, never raises, the multiplier the
                // user asked for; the present-time cap below stays as the residual
                // after this integer rounding.
                if(options.fg&&previewFgMultiplier>2&&presentationEffective.outputRate==OutputRateMode::Custom&&
                   !presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    const double sourceFps=activeSource->info().averageFps;
                    const double cap=presentationEffective.customFps;
                    if(sourceFps>0.01&&std::isfinite(cap)&&cap>0.0){
                        const auto needed=unsigned(std::ceil(cap/sourceFps-1e-6));
                        previewFgMultiplier=std::clamp(needed,2u,options.fgMultiplier);
                    }
                }
                {std::lock_guard lock(mutex_);snapshot_.previewFgMultiplier=previewFgMultiplier;}
                const auto liveInterval=physicalCapture?
                    capturePairInterval100ns(pkt.duration,activeSource->info().averageFps,lastProcessedPtsMs,pts,historyReset):
                    livePhaseInterval100ns(pkt.duration,activeSource->info().averageFps/(isScreen&&halfRate?2:1),isScreen);
                const auto processingAllowance=isCapture&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)?
                    fgBudget.processingAllowance(host100ns(),liveInterval):0;
                // Present-sink providers own subframe pacing after receiving B.
                // Graph FG needs the A/B presentation phase; provider FG does not.
                const auto legacyPairDelay=presentSinkFrameGeneration(options.settings.frameGenerationBackend)?0:liveInterval+processingAllowance;
                const auto pairDelay=adaptiveCapturePhase&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)?
                    pairLatency.select(host100ns(),legacyPairDelay,liveInterval,previewFgMultiplier):legacyPairDelay;
                // DLSS can reseed after a skipped pair. XeSS
                // owns generation inside its presenter and has no graph admission.
                if(isCapture&&!rereadCached&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    const auto presentP95=livePresentGpu.p95();
                    admitFg=[&,presentP95](const pipeline::FrameBatch& batch,bool warmingHistory){
                        // Physical capture and PS5 delivery clocks are not
                        // guaranteed to match the host clock. Anchor each new
                        // input pair before enhancement, so a 59.94-vs-60Hz
                        // mismatch cannot accumulate into a stale deadline.
                        // Never extend it based on processing/ready completion.
                        if(pairAnchoredLive)liveTimeline.resetPair(batch.identity.epoch,batch.b100ns,liveInputReady,pairDelay);
                        else if(!liveTimeline.anchored(batch.identity.epoch))liveTimeline.reset(batch.identity.epoch,batch.b100ns,liveInputReady,liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps));
                        const auto interval=liveInterval;
                        const auto a=(historyReset||batch.b100ns<=batch.a100ns||batch.b100ns-batch.a100ns>10000000)?batch.b100ns-interval:batch.a100ns;
                        const auto lastGenerated=pipeline::FrameBatch::interpolate(a,batch.b100ns,previewFgMultiplier-1,previewFgMultiplier);
                        const auto now=host100ns(),deadline=liveTimeline.deadline(lastGenerated);
                        const double elapsed=elapsedMs(processStart);
                        const bool admitted=fgBudget.admit(now,deadline,elapsed,presentP95,warmingHistory);
                        logAdmission(batch,now,deadline,elapsed,presentP95,admitted,warmingHistory,pairAnchoredLive?(isStream?"decoded-pair":"capture-pair"):"continuous");
                        return admitted?pipeline::EnhanceGraph::FgDecision::Evaluate:pipeline::EnhanceGraph::FgDecision::Skip;
                    };
                }
                // File playback checks both the first output and whole-group
                // deadlines against the audio clock, including older queued
                // GPU work. The real frame retains its original media PTS.
                double queuedGpuMs=0;
                if(!isCapture&&!isImage&&!rereadCached&&!transaction&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    {
                        const auto start=std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100;
                        int64_t finish=0;
                        for(const auto& previous:pendingCompletions){
                            if(previous->output.gpuComplete())continue;
                            const auto began=std::chrono::duration_cast<std::chrono::nanoseconds>(previous->processStart.time_since_epoch()).count()/100;
                            const auto cost=previous->output.fgEvaluated?fgBudget.predicted(start,previous->output.historyReset||previous->output.fgRecovery):fgBudget.baseCost(start);
                            finish=std::max({finish,began,previous->predictedGpuStart100ns})+int64_t(cost.value_or(0)*10000);
                        }
                        queuedGpuMs=double(std::max<int64_t>(0,finish-start))/10000;
                    }
                    admitFg=[&,historyReset,queuedGpuMs](const pipeline::FrameBatch& batch,bool warmingHistory){
                        if(fileAwaitingVideo)return pipeline::EnhanceGraph::FgDecision::Evaluate; // Bounded startup lookahead; audio has not started.
                        const auto interval=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                        const auto a=(historyReset||batch.b100ns<=batch.a100ns||batch.b100ns-batch.a100ns>10000000)?batch.b100ns-interval:batch.a100ns;
                        const auto lastGenerated=pipeline::FrameBatch::interpolate(a,batch.b100ns,previewFgMultiplier-1,previewFgMultiplier);
                        const auto now=host100ns();
                        const auto media=int64_t(nowMs()*10000.0);
                        const auto firstPts=pipeline::FrameBatch::interpolate(a,batch.b100ns,1,previewFgMultiplier);
                        const auto firstDeadline=now+int64_t((firstPts-media)/playbackRate),deadline=now+int64_t((lastGenerated-media)/playbackRate);
                        const auto first=frameFlow->snapshot(now).gpuTiming[size_t(diagnostics::GpuStage::Fg1)].p95;
                        const auto elapsed=elapsedMs(processStart),blit=livePresentGpu.p95();
                        const auto admitted=fgBudget.admitFile(now,firstDeadline,deadline,int64_t(interval/playbackRate)/previewFgMultiplier,elapsed,blit,first,warmingHistory,queuedGpuMs,options.settings.fgStrictAdmission);
                        logAdmission(batch,now,deadline,elapsed,blit,admitted,warmingHistory,"file-audio",firstDeadline,queuedGpuMs);
                        if(admitted)return pipeline::EnhanceGraph::FgDecision::Evaluate;
                        // Over budget for the full group: emit a presentable 2X
                        // midpoint instead of a whole-group hole.
                        // ON by default. Measured 2026-09-22, same slot,
                        // interleaved, 2 reps (tests/.../b10/gaps-ab-*), 100 Hz
                        // panel, per DXGI GetFrameStatistics:
                        //   native 6X   displayed 100.3/s both ways,
                        //               intervals >16.67 ms  34 -> 0
                        //   video SR 6X displayed 100.7/s both ways,
                        //               intervals >16.67 ms 150 -> 90,
                        //               gpuReadyP95 25.5 -> 22.7 ms
                        // It submits ~10 fewer frames per second, but every one
                        // of those was discarded before scanout (notDisplayed
                        // 192 vs 183), and refreshesWithoutNewFrame is 0 either
                        // way. Judge this by displayed frames and interval
                        // evenness, never by submitted frames: at 6X on a
                        // 100 Hz panel submission oversupplies by ~3x, so a
                        // submitted-frame count says nothing about what is seen.
                        // VEYRA_TEST_FG_NO_REDUCED=1 disables it for A/B runs.
                        static const bool reducedGroups=GetEnvironmentVariableW(L"VEYRA_TEST_FG_NO_REDUCED",nullptr,0)==0;
                        if(reducedGroups&&!warmingHistory&&previewFgMultiplier>2){
                            const auto midpoint=now+int64_t((pipeline::FrameBatch::interpolate(a,batch.b100ns,1,2)-media)/playbackRate);
                            if(fgBudget.canAdmitReduced(now,midpoint,int64_t(interval/playbackRate)/2,elapsed,blit,first,queuedGpuMs)){
                                ++fgReducedPairs;
                                if(now>=nextFgReducedLog){nextFgReducedLog=now+10000000;
                                    veyra::log::info("live-fg-admission",std::format("timeline=file-audio revision={} source={} reduced=2X requestedMultiplier={} reducedPairs={} remainingMidpointMs={:.3f} predictedFullMs={:.3f} (history kept; presentable midpoint instead of whole-group hole)",batch.identity.settingsRevision,batch.identity.sourceFrameId,options.fgMultiplier,fgReducedPairs,double(midpoint-now)/10000,fgBudget.predicted(now,false).value_or(-1)));}
                                return pipeline::EnhanceGraph::FgDecision::Reduced;
                            }
                        }
                        if(!warmingHistory&&fgBudget.canAdmit(now,deadline,elapsed,blit,true,queuedGpuMs))
                            return pipeline::EnhanceGraph::FgDecision::Seed;
                        return pipeline::EnhanceGraph::FgDecision::Skip;
                    };
                }
                uint64_t processWaitBase=0,processSubmitBase=0;double processWaitMsBase=0,processSlotWaitMs=0;
                if(resetRecord&&resetRecord->epoch==0)resetStageStart=Clock::now();
                // Bounded integration-test delay on the owner thread. It
                // exercises missed deadlines and recovery without changing
                // settings/history or inventing GPU execution timestamps.
                wchar_t testWork[16]{};
                const auto currentReflexFrame=(!paused_&&!isImage)?presenter.beginReflex():0;
                if(!isImage&&GetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",testWork,16))std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(_wtoi(testWork),0,150)));
                loopTrace.mark("prepare");
                if(!presenter.beginSourceProcessing()){status(L"XeSS processing timing failed",true);break;}
                bool processed=false;{
                    processWaitBase=ring.cpuWaitCount();processWaitMsBase=ring.cpuWaitMilliseconds();processSubmitBase=ring.submitCount();
                    processed=!injectedReject&&graph.process(frame,pts,historyReset,out,pkt.sequence,&pkt.colorInfo,&pkt.hardwareSurface,comparisonMode_!=0,admitFg,previewFgMultiplier);
                    processSlotWaitMs=ring.cpuWaitMilliseconds()-processWaitMsBase;
                }
                loopTrace.mark("graphSubmit");
                previewSkipSinceProcess=false;
                if(processed)lastProcessedPtsMs=pts;
                if(!processed){
                    const auto failedComponent=graph.failedBackend();
                    if(!transaction){
                        auto reduced=options.snapshot();
                        if(SUCCEEDED(ctx.device()->GetDeviceRemovedReason())&&disableFailedBackend(reduced,failedComponent)){
                            const auto attempted=options.snapshot();
                            drainLivePresentation();
                            if(!ring.drainQueue()||!ring.discardRecording()){status(L"增强故障恢复排空失败",true);break;}
                            out={};hasOutput=false;presenter.close();graph.shutdown();
                            {std::lock_guard lock(mutex_);reduced.revision=++nextRevision_;}
                            options=PlayerOptions::from(reduced,options.nodeOrder);
                            describeStages(stageRequest(options),options.snapshot(),gd);
                            if(!initializePreview(gd,options)){status(L"增强故障后的基础图重建失败",true);break;}
                            publishFrameGenerationCapabilities();
                            backendRecoveryWarning=std::wstring(backendFailureName(failedComponent))+L"运行失败，已关闭对应效果；错误码见日志"+(backendRecoveryWarning.empty()?L"":L"；"+backendRecoveryWarning);
                            {std::lock_guard lock(mutex_);desired_.rejectVideoRequest(attempted,options.snapshot());snapshot_.desired=desired_;snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;snapshot_.backendWarning=backendRecoveryWarning;}
                            veyra::log::warn("backend-recovery",std::format("runtime component={} disabled; rebuilt revision={} nr={} sr={} multiplier={}; retry next source frame",unsigned(failedComponent),options.settings.revision,options.nr,options.sr,options.snapshot().multiplier));
                            finishReset(diagnostics::ResetOutcome::Failed);reset=true;pendingResetCause=pipeline::ResetReason::Settings;
                            holdFileAudio();fileAudioAlignPending=true;
                            continue;
                        }
                    }
                    if(transaction){
                        ring.drainQueue();out={};presenter.close();graph.shutdown();options=PlayerOptions::from(previous,previousOrder);
                        gd=previousDesc;
                        if(graph.initialize(gd)&&presenter.open(ctx,window,graph,options.settings.captureCompatible,!isCapture&&!isImage)&&graph.createViews()&&graph.process(frame,pts,true,out,pkt.sequence,&pkt.colorInfo,&pkt.hardwareSurface,comparisonMode_!=0)){
                            publishFrameGenerationCapabilities();
                            finishReset(diagnostics::ResetOutcome::RolledBack);
                            std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=previous;snapshot_.status=L"参数执行失败，已整套回滚";transaction=false;
                        }else{status(L"参数回滚失败，已停止",true);break;}
                    }else{status(std::wstring(backendFailureName(failedComponent))+L"执行失败；请查看日志",true);break;}
                }
                if(transaction){std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;snapshot_.status=std::format(L"输入 {}×{} / 底图 {}×{} / NR {}×{} / 光流 {}×{} / FG与输出 {}×{} | {}",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrBeforeSr?L"低延迟 · NR先行后超分":gd.nrWidth<gd.workWidth?L"实时内部处理并回填":L"原生NR（性能成本较高）");veyra::log::info("display-color",std::format("hdrInput={} hdrOutput={} forceSdrPreview={}",gd.hdrInput,gd.hdrOutput,options.settings.forceSdrPreview));veyra::log::info("settings",std::format("Applied revision={} sourcePtsMs={} fgBackend={} flowBackend={} multiplier={} (source kept open)",options.settings.revision,pts,frameGenerationBackendName(options.settings.frameGenerationBackend),opticalFlowBackendName(options.settings.opticalFlowBackend),options.snapshot().multiplier));}
                if(veyra::log::verboseFrameLogs())veyra::log::info("source-identity",std::format("source={} totalRead={} graphProcessed={} cached={} revision={} nvofStandalone={}",pkt.sequence,sourceFrames,frames+1,rereadCached,options.settings.revision,gd.enableNvofStandalone));
                presenter.sourceProcessed(out.batch.identity);
                if(out.fgReduced||previewFgMultiplier!=options.fgMultiplier){std::lock_guard lock(mutex_);snapshot_.previewFgMultiplier=out.fgReduced?2u:previewFgMultiplier;}
                reset=false;hasOutput=true;
                const auto processDone=Clock::now();
                if(physicalCapture&&veyra::log::verboseFrameLogs())veyra::log::info("capture-graph-sample",std::format("source={} epoch={} revision={} arrival={} start={} submitted={} slotWaitMs={:.6f}",pkt.sequence,out.batch.identity.epoch,out.batch.identity.settingsRevision,captureArrival,std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100,std::chrono::duration_cast<std::chrono::nanoseconds>(processDone.time_since_epoch()).count()/100,processSlotWaitMs));
                if(!resetRecord&&out.historyReset&&!previewOnlyReset){
                    resetStart=processStart;resetStageStart=processStart;
                    resetRecord=diagnostics::FrameFlowMetrics::ResetRecord{};resetRecord->sessionId=runSessionId;resetRecord->settingsRevision=out.batch.identity.settingsRevision;resetRecord->epoch=out.batch.identity.epoch;resetRecord->sourceFrameId=out.batch.identity.sourceFrameId;resetRecord->reason=static_cast<uint8_t>(diagnostics::resetCause(pkt.flags,pendingResetCause,out.detectedReset));
                    resetRecord->stageMs[size_t(diagnostics::ResetStage::Drain)]=0.0;resetRecord->stageMs[size_t(diagnostics::ResetStage::Destroy)]=0.0;resetRecord->stageMs[size_t(diagnostics::ResetStage::Create)]=0.0;
                    resetRecord->rebuilt=false;markResetStage(diagnostics::ResetStage::Warmup);
                    if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                }
                if(resetRecord&&resetRecord->epoch==0){
                    resetRecord->epoch=out.batch.identity.epoch;resetRecord->sourceFrameId=out.batch.identity.sourceFrameId;
                    markResetStage(diagnostics::ResetStage::Warmup);
                }
                pendingResetCause=pipeline::ResetReason::None;
                if(!frameFlow||metricsRevision!=options.settings.revision||metricsEpoch!=metricsWindowEpoch){
                    const bool settingsChanged=metricsRevision!=options.settings.revision;
                    metricsRevision=options.settings.revision;metricsEpoch=metricsWindowEpoch;statsStart=Clock::now();statsSourceBase=sourceFrames-uint64_t(!rereadCached);
                    slotWaitBase=processWaitBase;slotWaitMsBase=processWaitMsBase;submitBase=processSubmitBase;
                    captureAges.clear();scheduleWaits.clear();processTimes.clear();presentTimes.clear();decodeTimes.clear();gpuReadyTimes.clear();for(auto& window:gpuStageTimes)window.clear();lastGpuSampleEnd={};presentEntryDeviation.clear();presentReturnDeviation.clear();submissionTimes.clear();submitted=expired=0;
                    // Admission predictions and the XeSS gate must not carry
                    // samples across a settings revision (new backend/dimensions);
                    // soft preview-skip history breaks do NOT reopen them.
                    if(settingsChanged){fedXessPresented=fedXessGenerated=0;}
                    playbackSpeedLastWall={};
                    if(liveScheduler){liveStats={};liveSubmissions.clear();liveAges.clear();liveWaits.clear();livePresent.clear();livePresentGpu.clear();liveReady.clear();}
                    if(settingsChanged||!completionRates)completionRates=std::make_shared<FrameCompletionRates>(host100ns());
                    frameFlow=std::shared_ptr<FrameFlowWindow>(new FrameFlowWindow(runSessionId,out.batch.identity,host100ns(),completionRates),[](FrameFlowWindow* window){logFrameFlow(window->snapshot(monotonic100ns()),"closed");delete window;});
                    if(resetRecord)frameFlow->resetLifecycle(*resetRecord);else if(lastResetRecord)frameFlow->resetLifecycle(*lastResetRecord);
                    frameFlow->update([&](auto& m){m.counters.settingsResets+=uint64_t(settingsChanged);});
                    captureFlowBase=captureFlowLast;
                    {std::lock_guard lock(mutex_);rateSkippedBase=snapshot_.captureRateSkipped;activeFlow_=frameFlow;}
                    veyra::log::info("metrics",std::format("reset session={} appliedRevision={} epoch={} sourceBase={}",runSessionId,metricsRevision,metricsEpoch,statsSourceBase));
                }
                const double processMs=std::chrono::duration<double,std::milli>(processDone-processStart).count();
                frameFlow->advanceHistory(out.batch.identity);
                frameFlow->update([&](auto& m){m.counters.historyResets+=uint64_t(out.historyReset);m.counters.captureDropResets+=uint64_t(pipeline::hasFrameFlag(pkt.flags,pipeline::FrameFlagBits::Drop));});
                const auto lineage=lineageTracker.observe(out.batch,sourceArrival,pkt.arrivalHost100ns>0,rereadCached||isImage);
                if(lineage&&out.hasGenerated){
                    frameFlow->pairArrived(*lineage);
                    if(veyra::log::verboseFrameLogs())veyra::log::info("frame-lineage",std::format("batch={} epoch={} revision={} sourceA={} sourceB={} ptsA={} ptsB={} arrivalA={} arrivalB={} captureCallbacks={}",out.batch.batchId,out.batch.identity.epoch,out.batch.identity.settingsRevision,lineage->a.identity.sourceFrameId,lineage->b.identity.sourceFrameId,lineage->a.pts100ns,lineage->b.pts100ns,lineage->a.host100ns,lineage->b.host100ns,lineage->a.captureCallback&&lineage->b.captureCallback));
                }
                processTimes.add(processMs);
                traceFrame(diagnostics::TraceKind::Submitted,out.batch.identity,out.batch.batchId,std::max(out.videoFenceValue,out.genFenceValue),out.batch.b100ns,out.fgSkippedBeforeEval,out.fgEvaluated,processMs);
                frameFlow->cpu(diagnostics::CpuStage::Decode,decodeMs,host100ns());
                if(isStream&&pkt.decodedHost100ns>0&&!rereadCached)frameFlow->cpu(diagnostics::CpuStage::DecodedQueue,std::max(0.0,double(std::chrono::duration_cast<std::chrono::nanoseconds>(decodeStart.time_since_epoch()).count()/100-pkt.decodedHost100ns)/10000),host100ns());
                frameFlow->update([&](auto& m){m.lastSubmit100ns=host100ns();});
                frameFlow->cpu(diagnostics::CpuStage::Submit,processMs,host100ns());
                frameFlow->cpu(diagnostics::CpuStage::SlotWait,processSlotWaitMs,host100ns());
                frameFlow->update([&](auto& m){m.latest.frame=out.batch.identity;m.latest.batchId=out.batch.batchId;m.latest.readyFence=std::max(out.videoFenceValue,out.genFenceValue);m.counters.sourceAccepted+=!rereadCached;++m.counters.realSubmitted;m.counters.previewSkippedBeforeGraph+=previewSkippedSinceSubmit;m.counters.fgCandidate+=out.fgCandidates;m.counters.fgEvaluated+=out.fgEvaluated;m.counters.fgSkippedForReset+=out.fgSkippedForReset;m.counters.fgSkippedBeforeEval+=out.fgSkippedBeforeEval;m.counters.fgReduced+=uint64_t(out.fgReduced);if(out.fgSkippedBeforeEval||out.fgBudgetSeed)m.lastFgRejected100ns=host100ns();if(!out.hasGenerated)m.counters.fgWarmup+=out.fgEvaluated;});
                previewSkippedSinceSubmit=0;
                if(transaction||frames==0){
                    std::lock_guard lock(mutex_);snapshot_.captureHalfRate=halfRate;
                    veyra::log::info("capture-rate",std::format("revision={} requested60To30={} active={} transportFps={} originalPtsPreserved=true",options.settings.revision,options.settings.content==ContentRate::Capture60To30,halfRate,isCapture?activeSource->info().averageFps:0));
                }
                const bool pairPacing=pairAnchoredLive&&options.fg;
                if(isCapture&&!rereadCached&&(isStream||pairPacing||!liveTimeline.anchored(out.batch.identity.epoch))){
                    const auto duration100ns=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                    // File replay has no device pacing and still needs its PTS
                    // clock. Physical capture without FG presents as soon as ready.
                    const bool paceSourcePts=options.fg||!physicalCapture;
                    if(!liveTimeline.anchored(out.batch.identity.epoch)||(pairPacing&&frames==0))veyra::log::info("capture-timeline",std::format("interval100ns={} packetDurationKnown={} packetDurationPositive={} nominalFps={} FG={} pacing={}",duration100ns,!pkt.duration.isUnknown(),pkt.duration.num>0,activeSource->info().averageFps,options.fg,pairAnchoredLive?(isRemote?"decoded-pair":"capture-pair"):paceSourcePts?"source-pts":"capture-ready"));
                    if(pairPacing||isStream)liveTimeline.resetPair(out.batch.identity.epoch,out.batch.b100ns,liveInputReady,options.fg?pairDelay:0,paceSourcePts);
                    else if(!liveTimeline.anchored(out.batch.identity.epoch))liveTimeline.reset(out.batch.identity.epoch,out.batch.b100ns,liveInputReady,options.fg?duration100ns:0,paceSourcePts);
                }
                if(!isImage&&!isCapture&&frames==0){anchor=Clock::now();anchorMs=lastAudioClockMs=pts;}
                double frameWaitMs=0,framePresentMs=0;
                double gpuWaitMs=0;
                if(liveScheduler){
                    const auto timeline=liveTimeline;
                    const uint64_t jobGeneration=presentationGeneration.load();
                    auto watch=std::make_shared<CompletionWatch>();watch->output=out;watch->flow=frameFlow;watch->processStart=processStart;watch->real=!rereadCached;
                    watch->predictedGpuStart100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100+int64_t(queuedGpuMs*10000);
                    watch->captureArrival=captureArrival;watch->presentationGeneration=jobGeneration;
                    watch->reflexFrame=currentReflexFrame;
                    pendingCompletions.push_back(watch);
                    if(physicalCapture&&gpuReadyEvent.handle)for(unsigned i=0;i<out.batch.count;++i){
                        const auto& lease=out.batch.frames[i].lease;
                        if(lease&&lease->readyFenceObject&&lease->readyFence)lease->readyFenceObject->SetEventOnCompletion(lease->readyFence,gpuReadyEvent.handle);
                    }
                    struct LiveStepState {
                        unsigned next=0,handled=0,remaining=0;bool readyReported=false;
                        Clock::time_point readyStart=Clock::now();std::optional<Clock::time_point> deadlineStart;
                        double readyMs=0,waitMs=0,presentMs=0,ageMs=0;uint64_t count=0,dropped=0;
                        diagnostics::GpuSample blit;
                    };
                    auto step=std::make_shared<LiveStepState>();
                    for(unsigned i=0;i<out.batch.count;++i)if(out.batch.frames[i].kind!=pipeline::FrameKind::Generated||out.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++step->remaining;
                    const double sourceIntervalMs=double(isCapture?liveInterval:liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps))/10000;
                    const auto baselineReady=pkt.decodedHost100ns>0?pkt.decodedHost100ns:captureArrival;
                    const bool delayEnhanced=options.nr||options.sr||options.fg;
                    // Captures are explicit on purpose (sweep 2026-09-22 B1): this step runs
                    // from the scheduler queue for up to two source frames, so every
                    // by-reference capture below must outlive the scheduler. The guard
                    // that destroys the scheduler is declared immediately before the run
                    // loop, so all of these are declared above it. Adding a reference to a
                    // local declared after that guard is now a compile error instead of a
                    // silent dangling reference.
                    if(!liveScheduler->push([this,&anchor,&anchorMs,&captureSource,&fedXessGenerated,&fedXessPresented,&lastFilePresentLateness,&lastFilePresentedMs,&liveSubmissions,&presentationCompletedReal,
#ifdef VEYRA_ENABLE_REMOTEPLAY
                        &remote,
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                        &moon,
#endif
#ifdef VEYRA_ENABLE_XBOX
                        &xb,
#endif
                        &seekDecoded,&seekStarted,&audio,&audioPipe,&audioStarted,&cadence,&ctx,&fileAudioAlignPending,&fileAwaitingVideo,&fileInputEnded,&playbackRate,&frameFlow,&graph,&host100ns,&isCapture,&isImage,&livePresent,&liveScheduler,&nextFgDeadlineLog,&nowMs,&options,&pendingCompletions,&physicalCapture,&presentEntryDeviation,&presentReturnDeviation,&presentationEffective,&presentationGeneration,&presentationSkippedGenerated,&presenter,&ring,&runSessionId,&seekPreviewPending,&traceFrame,&traceSubframes,watch,step,timeline,captureArrival,baselineReady,delayEnhanced,activeSeekId,lineage,jobGeneration,rereadCached,sourceIntervalMs,flow=frameFlow](int64_t now)->LiveGpuScheduler::Step{
                        using State=LiveGpuScheduler::State;auto& batch=watch->output;auto& s=*step;
                        auto updatePending=[&](LiveStepState*){
                            unsigned left=0;for(unsigned i=s.next;i<batch.batch.count;++i)if(batch.batch.frames[i].kind!=pipeline::FrameKind::Generated||batch.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++left;
                            flow->update([&](auto& m){if(left>s.remaining)m.pendingOutputFrames+=left-s.remaining;else m.pendingOutputFrames-=std::min(m.pendingOutputFrames,s.remaining-left);});s.remaining=left;
                        };std::unique_ptr<LiveStepState,decltype(updatePending)> pendingUpdate(&s,updatePending);
                        if(watch->ready&&!s.readyReported){s.readyMs=std::max(0.0,std::chrono::duration<double,std::milli>(watch->readyObserved-s.readyStart).count());s.readyReported=true;flow->cpu(diagnostics::CpuStage::ReadyWait,s.readyMs,host100ns());}
                        if(!isCapture&&fileAwaitingVideo&&!seekPreviewPending&&!fileInputEnded&&
                           (liveScheduler->occupancy()<2||!pendingCompletions.empty()))return {State::Pending,now+2000};
                        if(!isCapture&&audioStarted&&fileAudioAlignPending){
                            if(audioPipe.endpointRecovering())return {State::Pending,now+10000};
                            const double ptsMs=double(batch.batch.b100ns)/10000,media=audio.mediaTimeMs();
                            if(std::isfinite(media)&&media+1<ptsMs&&!std::isfinite(audioPipe.requestSeek(ptsMs)))return {State::Failed};
                            fileAudioAlignPending=false;
                        }
                        while(s.next<batch.batch.count){auto& item=batch.batch.frames[s.next];const bool generated=item.kind==pipeline::FrameKind::Generated;
                            if(stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0)return {State::Complete};
                            const double itemPtsMs=double(item.pts100ns)/10000;
                            const auto decisionTime=host100ns();
                            const bool generationExpired=generated&&(isCapture?timeline.expired(item.pts100ns,decisionTime,100000):!fileAwaitingVideo&&previewGeneratedExpired(nowMs(),itemPtsMs));
                            const auto readiness=previewFrameReadiness(item,comparisonMode_!=0||!generatedPresentationCurrent(jobGeneration,presentationGeneration.load()),generationExpired,[&]{return graph.resolveFrame(batch,s.next);});
                            if(traceSubframes&&generated&&(readiness==PreviewFrameReadiness::Expired||readiness==PreviewFrameReadiness::Suppressed))
                                traceFrame(diagnostics::TraceKind::Discarded,item.identity,batch.batch.batchId,item.lease->readyFence,item.pts100ns,item.subframe,unsigned(readiness),isCapture?double(decisionTime-timeline.deadline(item.pts100ns))/10000:nowMs()-itemPtsMs);
                            if(readiness==PreviewFrameReadiness::Pending){
                                if(elapsedMs(s.readyStart)>2000){
                                    ctx.reportDeviceFailure("GPU-frame-ready-timeout",item.lease?item.lease->readyFence:0);
                                    veyra::log::error("capture-present",std::format("GPU frame ready timeout nr={} nrRuntime={} source={} epoch={} revision={} subframe={} dimensions={}x{} nrDimensions={}x{} flowBackend={}",graph.nrEnabled(),nrRuntimeName(options.settings.nrRuntime),item.identity.sourceFrameId,item.identity.epoch,item.identity.settingsRevision,item.subframe,graph.workWidth(),graph.workHeight(),graph.nrWidth(),graph.nrHeight(),opticalFlowBackendName(options.settings.opticalFlowBackend)));
                                    return {State::Failed};
                                }
                                return {State::Pending,host100ns()+2000};
                            }
                            if(readiness==PreviewFrameReadiness::Invalid){++s.handled;++s.next;s.deadlineStart.reset();continue;}
                            if(readiness==PreviewFrameReadiness::Suppressed){++presentationSkippedGenerated;++s.next;s.deadlineStart.reset();continue;}
                            if(isCapture&&readiness==PreviewFrameReadiness::Ready&&!s.readyReported){s.readyMs=elapsedMs(s.readyStart);s.readyReported=true;flow->cpu(diagnostics::CpuStage::ReadyWait,s.readyMs,host100ns());}
                            if(generated&&isCapture&&decisionTime>=nextFgDeadlineLog){
                                nextFgDeadlineLog=decisionTime+10000000;
                                const auto ready=watch->frameReadyObserved[s.next];
                                veyra::log::info("fg-deadline",std::format("revision={} batch={} subframe={} expired={} readyObservedLateMs={:.3f} decisionLateMs={:.3f} observedReadyToDecisionMs={:.3f} batchReady={} (CPU fence observations, not scanout; -1 means readiness unknown)",item.identity.settingsRevision,batch.batch.batchId,item.subframe,generationExpired,ready?double(ready-timeline.deadline(item.pts100ns))/10000:-1,double(decisionTime-timeline.deadline(item.pts100ns))/10000,ready?double(decisionTime-ready)/10000:-1,watch->ready.load()));
                            }
                            if(readiness==PreviewFrameReadiness::Expired){++s.dropped;++s.handled;++s.next;s.deadlineStart.reset();continue;}
                            if(!s.deadlineStart)s.deadlineStart=Clock::now();
                            if(isCapture){if(host100ns()<timeline.deadline(item.pts100ns))return {State::Pending,timeline.deadline(item.pts100ns)};}
                            else if(!fileAwaitingVideo){
                                if(audioStarted)audioPipe.videoReady(itemPtsMs);
                                if(nowMs()+.25<itemPtsMs)return {State::Pending,now+std::min<int64_t>(10000,int64_t((itemPtsMs-nowMs())*10000/playbackRate))};
                            }
                            const auto optionalNow=host100ns();
                            if(!isCapture&&!fileAwaitingVideo&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                                const auto interval=std::max<int64_t>(1,int64_t(sourceIntervalMs*10000/playbackRate)/std::max(1u,batch.batch.count));
                                const auto due=cadence.due(optionalNow+int64_t((itemPtsMs-nowMs())*10000/playbackRate),interval,20);
                                if(optionalNow<due)return {State::Pending,due};
                            }
                            int64_t capInterval=0;
                            if(presentationEffective.enabled||presentationEffective.outputRate==OutputRateMode::Custom){
                                const auto interval=std::max<int64_t>(1,int64_t(sourceIntervalMs*10000/playbackRate)/std::max(1u,batch.batch.count));
                                const auto mediaDeadline=isCapture?timeline.cadenceDeadline(item.pts100ns,optionalNow):fileAwaitingVideo?optionalNow:optionalNow+int64_t((itemPtsMs-nowMs())*10000/playbackRate);
                                // Media time is the deadline. The old "even" mode
                                // added a spacing floor on top; it measured as a
                                // no-op because production already lands within
                                // 0.8 ms of media time (b13/pacing-ab).
                                auto due=mediaDeadline;
                                if(presentationEffective.outputRate==OutputRateMode::Custom){
                                    capInterval=std::max<int64_t>(1,int64_t(10000000.0/presentationEffective.customFps));
                                    due=std::max(due,cadence.rateDue(optionalNow,capInterval));
                                }
                                // The cap selects displayed candidates, never
                                // changes processing/FG history or media time.
                                // If the next candidate is due before this
                                // slot, discard this stale display opportunity
                                // instead of accumulating latency.
                                if(capInterval>0&&cadence.rateSkipsCandidate(optionalNow,mediaDeadline,interval,capInterval)){++s.dropped;++s.handled;++s.next;s.deadlineStart.reset();continue;}
                                if(optionalNow<due)return {State::Pending,due};
                                if(!presenter.presentationReady())return {State::Pending,optionalNow+2000};
                            }
                            const auto waited=elapsedMs(*s.deadlineStart);s.deadlineStart.reset();s.waitMs+=waited;flow->cpu(diagnostics::CpuStage::DeadlineWait,waited,host100ns());
                            const bool deviationValid=!isCapture&&!isImage&&!fileAwaitingVideo&&!paused_;
                            const double entryDeviation=deviationValid?nowMs()-itemPtsMs:0;
                            const auto presentBeginHost=host100ns();
                            const auto begin=Clock::now();const auto before=presenter.submittedCount();
                            presenter.reflexFrame(watch->reflexFrame);
                            if(!presenter.present(ctx,ring,graph,item.lease->slot,generated,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView(),item.pts100ns))return {State::Failed};
                            const auto presentEndHost=host100ns();
                            const double returnDeviation=deviationValid?nowMs()-itemPtsMs:0;
                            item.lease->consumerFence=presenter.consumerFenceValue(ring.lastSignaledValue());const bool didPresent=presenter.submittedCount()>before;s.blit=presenter.blitTiming(ctx.fence());
                            const auto elapsed=elapsedMs(begin);s.presentMs+=elapsed;flow->cpu(diagnostics::CpuStage::Present,elapsed,host100ns());
                            // A soft history reset retains this metrics window. Older
                            // session/revision windows must not update its admission cost.
                            if(flow==frameFlow){
                                livePresent.add(elapsed);
                                // DXGI/driver blocking is backpressure, not GPU blit
                                // work. The bounded scheduler retains that pressure.
                            }
                            if(didPresent){
                                cadence.submitted(optionalNow);
                                cadence.rateSubmitted(optionalNow,capInterval);
                                if(physicalCapture&&veyra::log::verboseFrameLogs())veyra::log::info("capture-present-sample",std::format("source={} epoch={} revision={} batch={} subframe={} generated={} arrival={} arrivalA={} ready={} begin={} end={}",item.identity.sourceFrameId,item.identity.epoch,item.identity.settingsRevision,batch.batch.batchId,item.subframe,generated,captureArrival,lineage?lineage->a.host100ns:0,watch->frameReadyObserved[s.next],optionalNow,host100ns()));
                                if(veyra::log::verboseFrameLogs())veyra::log::info("pacing-sample",std::format("mode={} enabled={} sync={} pts={} begin={} end={} ready={} process={} generated={} queue={} lateMs={:.3f}",unsigned(presentationEffective.mode),presentationEffective.enabled,unsigned(presentationEffective.display),item.pts100ns,optionalNow,host100ns(),watch->frameReadyObserved[s.next],std::chrono::duration_cast<std::chrono::nanoseconds>(watch->processStart.time_since_epoch()).count()/100,generated,liveScheduler->occupancy(),isCapture?double(optionalNow-timeline.deadline(item.pts100ns))/10000:fileAwaitingVideo?0:nowMs()-itemPtsMs));
                                diagnostics::FrameTraceEvent event{presentEndHost,runSessionId,item.identity,batch.batch.batchId,item.lease->consumerFence,item.pts100ns,diagnostics::TraceKind::Present,item.subframe,1,elapsed};
                                event.decodedHost=baselineReady;event.processHost=std::chrono::duration_cast<std::chrono::nanoseconds>(watch->processStart.time_since_epoch()).count()/100;
                                event.readyHost=watch->frameReadyObserved[s.next];event.presentBeginHost=presentBeginHost;event.presentEndHost=presentEndHost;
                                event.entryDeviationMs=entryDeviation;event.returnDeviationMs=returnDeviation;event.queueDepth=unsigned(liveScheduler->occupancy());event.mediaDeviationValid=deviationValid;
                                Logger::instance().recordFrame(event);
                                if(deviationValid){presentEntryDeviation.add(std::abs(entryDeviation));presentReturnDeviation.add(std::abs(returnDeviation));}
                                if(veyra::log::verboseFrameLogs())veyra::log::info("submit",std::format("batch={} epoch={} revision={} subframe={} pts100ns={} host100ns={} fence={} (submission, display unmeasured)",batch.batch.batchId,item.identity.epoch,item.identity.settingsRevision,item.subframe,item.pts100ns,host100ns(),item.lease->consumerFence));
                            }
                            // Cumulative alignment: feed whatever the providers reported
                            // since the last submission. The AMD provider publishes from
                            // its own thread, so a per-call delta can miss an update that
                            // lands just after the call and would then never be counted.
                            {
                                const auto xessPresentedTotal=presenter.xessPresentedCount(),xessGeneratedTotal=presenter.xessGeneratedCount();
                                const uint64_t xessPresented=xessPresentedTotal-fedXessPresented,xessGenerated=xessGeneratedTotal-fedXessGenerated;
                                if(xessPresented){flow->xessSubmitted(xessPresented,xessGenerated,host100ns());fedXessPresented=xessPresentedTotal;fedXessGenerated=xessGeneratedTotal;}

                            }
                            if(didPresent&&!isCapture){
                                lastFilePresentedMs=itemPtsMs;lastFilePresentLateness=returnDeviation;
                                {std::lock_guard lock(mutex_);if(snapshot_.sessionId==runSessionId&&snapshot_.applied.revision==item.identity.settingsRevision){
                                    if(activeSeekId&&snapshot_.seekPresented!=activeSeekId)veyra::log::info("seek-latency",std::format("request={} firstPresentMs={:.3f} decodedToTarget={} ptsMs={:.3f}",activeSeekId,elapsedMs(seekStarted),seekDecoded,itemPtsMs));
                                    snapshot_.position=itemPtsMs/1000;snapshot_.lateMs=lastFilePresentLateness;snapshot_.seekPresented=activeSeekId;}}
                                double nextPts=itemPtsMs+sourceIntervalMs;
                                for(unsigned next=s.next+1;next<batch.batch.count;++next){const auto& candidate=batch.batch.frames[next];if(candidate.kind!=pipeline::FrameKind::Generated||(candidate.validity==pipeline::GenerationValidity::Valid&&comparisonMode_==0)){nextPts=double(candidate.pts100ns)/10000;break;}}
                                if(audioStarted){audioPipe.videoPresented(nextPts);audioPipe.setPaused(paused_);}
                                if(fileAwaitingVideo){
                                    veyra::log::info("audio-sync",std::format("file lookahead anchor ptsMs={:.3f} queued={} (continuous audio after bounded prefill)",itemPtsMs,liveScheduler->occupancy()));
                                    // Silent playback keeps one wall-clock origin between
                                    // transport changes; per-frame reanchoring hides lateness.
                                    anchor=Clock::now();anchorMs=itemPtsMs;
                                }
                                fileAwaitingVideo=false;seekPreviewPending=false;
                            }
                            // Real B owns captureArrival. Generated A/B frames and
                            // paused cached frames must not invent input anchors.
                            if(didPresent&&physicalCapture&&!generated&&!rereadCached)captureSource.videoPresented(double(item.pts100ns)/10000,host100ns(),captureArrival);
#ifdef VEYRA_ENABLE_REMOTEPLAY
                            if(didPresent&&remote)remote->videoPresented(double(item.pts100ns)/10000,host100ns());
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                            if(didPresent&&moon)moon->videoPresented(double(item.pts100ns)/10000,host100ns());
#endif
#ifdef VEYRA_ENABLE_XBOX
                            if(didPresent&&xb)xb->videoPresented(double(item.pts100ns)/10000,host100ns(),!generated&&!rereadCached);
#endif
                            if(didPresent&&!generated&&!rereadCached){
                                const auto returned=host100ns();
                                flow->latency(captureArrival,returned);
                                const double age=double(returned-captureArrival)/10000;
                                if(physicalCapture&&age>=80.0)veyra::log::info("capture-age-stall",std::format("source={} epoch={} revision={} callbackToReturnMs={:.3f} readyObservedAgeMs={:.3f} gpuSpanMs={:.3f} deadlineAgeMs={:.3f} decisionLateMs={:.3f} deadlineWaitMs={:.3f} presentMs={:.3f} (CPU observation includes scheduling; not scanout)",item.identity.sourceFrameId,item.identity.epoch,item.identity.settingsRevision,age,watch->frameReadyObserved[s.next]?double(watch->frameReadyObserved[s.next]-captureArrival)/10000:-1.0,watch->gpuExecutionMs.value_or(-1.0),double(timeline.deadline(item.pts100ns)-captureArrival)/10000,double(optionalNow-timeline.deadline(item.pts100ns))/10000,waited,elapsed));
                            }
                            if(didPresent&&!generated&&!rereadCached&&!paused_){
                                const auto stamp=host100ns();const auto stats=flow->snapshot(stamp);
                                const auto color=stats.gpuTiming[size_t(diagnostics::GpuStage::Color)].mean;
                                const auto blit=stats.gpuTiming[size_t(diagnostics::GpuStage::Blit)].mean;
                                std::optional<double> basic;
                                if(color&&blit)basic=*color+*blit+elapsed;
                                const auto estimate=enhancementDelayEstimate(delayEnhanced,isCapture,
                                    isCapture?double(stamp-baselineReady)/10000:lastFilePresentLateness,basic);
                                if(estimate)flow->cpu(diagnostics::CpuStage::EnhancementDelayEstimate,*estimate,stamp);
                            }
                            if(didPresent&&generated&&lineage)flow->generatedLatency(*lineage,host100ns());
                            if(didPresent){++s.handled;++s.count;flow->presented(generated,item.lease->consumerFence,host100ns());if(!generated)++presentationCompletedReal;s.ageMs=double(host100ns()-captureArrival)/10000;
                                if(flow==frameFlow){const auto time=host100ns();liveSubmissions.push_back(time);while(liveSubmissions.size()>1&&time-liveSubmissions.front()>10000000)liveSubmissions.pop_front();}}
                            ++s.next;
                        }
                        return {State::Complete};
                    },[&,watch,step,flow=frameFlow]{
                        const auto& batch=watch->output;const auto& s=*step;
                        flow->update([&](auto& m){m.pendingOutputFrames-=std::min(m.pendingOutputFrames,s.remaining);});
                        if(s.handled<batch.batch.count)++presentationCancelledJobs;
                        if(s.handled<batch.batch.count)traceFrame(diagnostics::TraceKind::Cancelled,batch.batch.identity,batch.batch.batchId,0,batch.batch.b100ns,0,batch.batch.count-s.handled,0);
                        flow->update([&](auto& m){m.counters.cancelledBeforePresent+=batch.batch.count-s.handled;m.gpuReadyWaitMs=s.readyMs;m.deadlineWaitMs=s.waitMs;if(s.count)m.captureArrivalToPresentReturnMs=s.ageMs;m.counters.generatedExpiredAfterEval+=s.dropped;});
                        if(flow!=frameFlow)return;
                        liveStats.submitted+=s.count;liveStats.expired+=s.dropped;
                        if(s.count){liveStats.waitMs=s.waitMs;liveStats.presentMs=s.presentMs;liveStats.readyMs=s.readyMs;liveStats.ageMs=s.ageMs;liveStats.blit=s.blit;
                            liveAges.add(s.ageMs);liveWaits.add(s.waitMs);liveReady.add(s.readyMs);liveStats.ageP95=liveAges.p95();liveStats.waitP95=liveWaits.p95();liveStats.presentP95=livePresent.p95();liveStats.readyP95=liveReady.p95();}
                        liveStats.fps=liveSubmissions.size()>1?double(liveSubmissions.size()-1)*1e7/(liveSubmissions.back()-liveSubmissions.front()):0;
                    })){status(L"视频呈现队列失败",true);break;}
                    frameFlow->update([&](auto& m){m.pendingOutputFrames+=step->remaining;});
                    advanceLive();
                    const auto occupancy=liveScheduler->occupancy();frameFlow->update([&](auto& m){m.counters.presentationBatchHighWater=std::max(m.counters.presentationBatchHighWater,occupancy);});
                    const auto completed=liveStats;
                    frameWaitMs=completed.waitMs;framePresentMs=completed.presentMs;gpuWaitMs=completed.readyMs;submitted=completed.submitted;expired=completed.expired;
                }else{
                    // Images have no media clock, audio or temporal batches.
                    const auto readyStart=Clock::now();
                    while(!stop_&&!graph.resolveGeneration(out)){
                        if(elapsedMs(readyStart)>2000){status(L"图片 GPU 就绪超时",true);stop_=true;break;}
                        deadlineWait.slice(.2);
                    }
                    if(stop_)break;
                    gpuWaitMs=elapsedMs(readyStart);gpuReadyTimes.add(gpuWaitMs);
                    frameFlow->cpu(diagnostics::CpuStage::ReadyWait,gpuWaitMs,host100ns());
                    frameFlow->ready(out.batch.batchId,false,0,0,host100ns());completeReset(out.batch.identity);
                    bool presentFailed=false;
                    for(auto& item:out.batch.frames){
                        if(!item.lease)continue;
                        const auto begin=Clock::now();const auto before=presenter.submittedCount();
                        if(!presenter.present(ctx,ring,graph,item.lease->slot,false,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView(),item.pts100ns)){presentFailed=true;break;}
                        item.lease->consumerFence=presenter.consumerFenceValue(ring.lastSignaledValue());framePresentMs+=elapsedMs(begin);
                        frameFlow->cpu(diagnostics::CpuStage::Present,elapsedMs(begin),host100ns());
                        if(presenter.submittedCount()>before){++submitted;frameFlow->presented(false,item.lease->consumerFence,host100ns());}
                    }
                    if(presentFailed){status(L"图片呈现失败",true);break;}
                }
                if(!liveScheduler){scheduleWaits.add(frameWaitMs);presentTimes.add(framePresentMs);}
                loopTrace.mark("scheduleAndPresent");
                audioRebuffering=audioStarted&&audioPipe.waitingForVideo();
                const double lateness=isCapture?0:isImage?nowMs()-pts:lastFilePresentLateness;
                // Sample only actual presentations above, not each engine iteration.
                auto captureStats=physicalCapture?captureSource.metrics():source::CaptureMetrics{};
                if(isScreen){const auto screen=screenSource.metrics();captureStats.received=screen.received;captureStats.delivered=screen.delivered;captureStats.dropped=screen.dropped;captureStats.readAgeMs=screen.ageMs;captureStats.frameAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.pts.to100ns()))/10000;}
#ifdef VEYRA_ENABLE_REMOTEPLAY
                if(remote){const auto rp=remote->sessionSnapshot();captureStats.received=rp.video.accessUnits;captureStats.dropped=remote->skipped();captureStats.delivered=sourceFrames;
                    captureStats.readAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.arrivalHost100ns))/10000;
                }
#endif
#ifdef VEYRA_ENABLE_XBOX
                if(xb){captureStats.received=xb->unitsReceived();captureStats.dropped=xb->skipped();captureStats.delivered=sourceFrames;
                    captureStats.readAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.arrivalHost100ns))/10000;
                }
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){captureStats.received=moon->unitsReceived();captureStats.dropped=moon->skipped();captureStats.delivered=sourceFrames;
                    captureStats.readAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.arrivalHost100ns))/10000;
                }
#endif
                loopTrace.mark("captureStats");
                diagnostics::FrameMetrics measured;pipeline::EnhanceGraph::Metrics graphStats;uint64_t slotWaitCount=0,commandSubmits=0;double slotWaitMilliseconds=0;uint32_t slotsInFlight=0;
                {measured=graph.gpuMetrics();graphStats=graph.metrics();measured.gpu[size_t(diagnostics::GpuStage::Blit)]=presenter.blitTiming(ctx.fence(),options.settings.revision,out.batch.identity.epoch);
                 // Present-sink FG backends record their application-side work on
                 // the presenter's list; surface that sample instead of leaving
                 // the stage unmeasured. The in-graph DLSS FG keeps its own.
                 if(graph.xessEnabled())measured.gpu[size_t(diagnostics::GpuStage::FgBatch)]=presenter.fgTiming(ctx.fence(),options.settings.revision,out.batch.identity.epoch);
                 slotWaitCount=ring.cpuWaitCount()-slotWaitBase;slotWaitMilliseconds=ring.cpuWaitMilliseconds()-slotWaitMsBase;commandSubmits=ring.submitCount()-submitBase;slotsInFlight=ring.inFlightCount();
                    loopTrace.mark("gpuStats");
                    collectTimings();
                    loopTrace.mark("tailCollectTimings");
                }
                if(measured.identity.settingsRevision!=options.settings.revision||measured.identity.epoch!=out.batch.identity.epoch){measured={};measured.identity=out.batch.identity;for(auto& sample:measured.gpu)sample.state=diagnostics::SampleState::Pending;}
                if(graph.xessEnabled()){
                    graphStats.fgGeneratedFrames=presenter.xessGeneratedCount();
                }


                for(size_t stage=0;stage<measured.gpu.size();++stage){const auto& sample=measured.gpu[stage];if(sample.state==diagnostics::SampleState::Measured&&sample.milliseconds&&sample.end&&sample.end!=lastGpuSampleEnd[stage]){gpuStageTimes[stage].add(*sample.milliseconds);lastGpuSampleEnd[stage]=sample.end;}}
                measured.resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.snapshot().nrPolicy,isImage,options.settings.revision,options.settings.srTarget,options.settings.lowLatency&&options.nr);measured.decodeCpuMs=decodeMs;measured.submitCpuMs=std::chrono::duration<double,std::milli>(processDone-processStart).count();measured.gpuWaitCpuMs=gpuWaitMs;measured.deadlineWaitCpuMs=frameWaitMs;measured.presentCpuMs=framePresentMs;measured.queueWatermark=out.batch.count;
                LiveStats completed;if(liveScheduler)completed=liveStats;
                uint64_t skipped=0;{std::lock_guard lock(mutex_);skipped=snapshot_.captureRateSkipped-rateSkippedBase;}
                frameFlow->update([&](auto& m){m.counters.captureReceived=captureStats.received-captureFlowBase.received;m.counters.mailboxOverwritten=captureStats.dropped-captureFlowBase.dropped;m.counters.sourceSkippedBeforeGraph=skipped;m.slotReuseWaitCount=slotWaitCount;m.slotReuseWaitMs=slotWaitMilliseconds;m.counters.commandSlotsInFlight=slotsInFlight;m.counters.commandSlotHighWater=std::max(m.counters.commandSlotHighWater,slotsInFlight);});
                frameFlow->update([&](auto& m){m.slotWaitPerFrameMs=processSlotWaitMs;});
                captureFlowLast=captureStats;
                loopTrace.mark("flowUpdate");
                measured.flow=frameFlow->snapshot(host100ns());
                loopTrace.mark("flowSnapshot");
                measured.sourceFrames=measured.flow.counters.sourceAccepted;measured.validGenerated=measured.flow.counters.fgReadyValid;measured.submitted=measured.flow.counters.realPresented+measured.flow.counters.generatedPresented;measured.expired=measured.flow.counters.generatedExpiredAfterEval;
                const double ageP95=liveScheduler?completed.ageP95:captureAges.p95(),waitP95=liveScheduler?completed.waitP95:scheduleWaits.p95(),presentP95=liveScheduler?completed.presentP95:presentTimes.p95();
                ++frames;{std::lock_guard lock(mutex_);snapshot_.metrics=measured;snapshot_.colorStatus=graph.videoHdrActive()?(gd.hdrInput?L"HDR → SDR → RTX Video HDR":L"SDR → RTX Video HDR"):options.settings.videoHdr.enabled&&!gd.hdrInput?L"SDR → SDR（HDR显示未启用）":gd.hdrOutput?(graph.hdr10Output()?L"HDR → HDR10 / PQ":L"HDR → scRGB / 浮点"):gd.hdrInput?L"HDR → SDR色调映射":L"SDR → SDR";snapshot_.position=(isCapture||isImage?pts:lastFilePresentedMs)/1000;snapshot_.frames=sourceFrames;snapshot_.generated=graphStats.fgGeneratedFrames;snapshot_.lateMs=lateness;snapshot_.lateP95Ms=presentReturnDeviation.p95();
                    snapshot_.videoHdrActive=graph.videoHdrActive();
                    // Custom: the switch can now also convert HDR sources, so the
                    // "not running" reasons have to name which case this is.
                    snapshot_.videoHdrStatus=graph.videoHdrActive()?(gd.hdrInput?L"已运行：HDR 片源先转 SDR，再由 RTX Video HDR 升回 HDR":L"预览：SDR 转 HDR 已运行"):
                        !options.settings.videoHdr.enabled?L"RTX Video HDR 已关闭":
                        gd.hdrInput?(options.settings.videoHdr.convertHdrSource?L"HDR 片源转换已开启，但当前没有 HDR 输出（检查显示器 HDR 或 SDR 预览）":L"原生 HDR 输入；「HDR 片源转换」未开启"):
                        L"当前为 SDR 预览，HDR 转换未运行";
                    // Custom: per-scene HDR brightness readout. Doubles as the
                    // field-visible evidence that the map follows the picture.
                    snapshot_.hdrBrightnessStatus=!options.settings.hdrBrightness.enabled?L"HDR 亮度管理已关闭":
                        !gd.hdrInput?L"仅 HDR 片源可用":
                        !graph.hdrBrightnessMeasured()?L"HDR 亮度管理：等待场景测量…":
                        std::format(L"HDR 亮度管理：本场景增益 ×{:.2f}，峰值 {:.0f} nit（目标 {} nit）",
                            graph.hdrBrightnessGain(),graph.hdrBrightnessPeakNits(),options.settings.hdrBrightness.targetPeakNits);
                    // Measured playback speed: media-PTS advance per wall time
                    // over ~1s windows (1.0 = normal speed), resampled on seek.
                    // A rejected LUT input space must be visible, not only logged
                    // (plan v5.2: never apply a mismatched LUT silently).
                    if(!graph.colorLutNotice().empty())snapshot_.colorStatus+=L"；"+graph.colorLutNotice();
                    const auto speedNow=Clock::now();
                    if(playbackSpeedLastWall==Clock::time_point()||pts+0.5<playbackSpeedLastPts){playbackSpeedLastPts=pts;playbackSpeedLastWall=speedNow;}
                    else if(std::chrono::duration<double>(speedNow-playbackSpeedLastWall).count()>=0.9){
                        const double wallS=std::chrono::duration<double>(speedNow-playbackSpeedLastWall).count();
                        const double mediaS=(pts-playbackSpeedLastPts)/1000.0;
                        if(mediaS>=0)snapshot_.playbackSpeed=mediaS/wallS;
                        playbackSpeedLastPts=pts;playbackSpeedLastWall=speedNow;
                    }
                    snapshot_.previewSkipped=measured.flow.counters.previewSkippedBeforeGraph;
                    snapshot_.fgBudgetLimited=measured.flow.lastFgRejected100ns>0&&host100ns()-measured.flow.lastFgRejected100ns<10000000;
                    snapshot_.xessGenerationSuppressed=graph.xessEnabled()&&presenter.xessGenerationSuppressed();
                    snapshot_.nrActive=graph.nrEnabled()&&graphStats.nrEvaluateCount>0;
                    snapshot_.audioRebuffering=audioRebuffering;
                    snapshot_.audioVideoWaits=audioPipe.videoWaitCount();
                    snapshot_.srActive=gd.enableSr&&graphStats.srEvaluateCount>0;
                    snapshot_.fgActive=options.fg&&(graph.xessEnabled()?presenter.xessActive()&&graphStats.fgGeneratedFrames>0:graph.fgEnabled()&&measured.flow.counters.fgReadyValid>0);
                    if(transaction&&backendRecoveryWarning.empty()&&nvidiaAdapter&&(!graph.xessEnabled()||presenter.xessActive()))snapshot_.backendWarning.clear();
                    snapshot_.flowPerf=graph.actualFlowPerf();snapshot_.contentFps=out.measuredContentRate;
                    snapshot_.nrEvaluated=graphStats.nrEvaluateCount;snapshot_.nvofExecuted=graphStats.nvofExecuteCount;
                    snapshot_.captureReceived=captureStats.received;snapshot_.captureDropped=captureStats.dropped;snapshot_.captureFps=captureStats.callbackFps;snapshot_.captureReadAgeMs=captureStats.readAgeMs;snapshot_.captureAgeMs=liveScheduler?completed.ageMs:captureStats.frameAgeMs;snapshot_.captureAgeP95Ms=ageP95;
                    snapshot_.schedulingWaitP95Ms=waitP95;snapshot_.processCpuP95Ms=processTimes.p95();snapshot_.presentCpuP95Ms=presentP95;
                }
                loopTrace.mark("publishSnapshot");
                const auto gpuP95=[&](diagnostics::GpuStage stage){return gpuStageTimes[size_t(stage)].p95();};
                if(veyra::log::verboseFrameLogs()&&Clock::now()>=nextTimingLog){const auto [retained,overwritten]=Logger::instance().frameTraceSize();
                    veyra::log::info("frame-trace",std::format("retained={} capacity=8192 overwritten={} (records available in diagnostic preview)",retained,overwritten));}
                if(options.settings.multiplier>1&&Clock::now()>=nextTimingLog)
                    veyra::log::info("fg-validity",std::format("revision={} candidate={} valid={} invalid={} providerDisabled={} contentDuplicate={} both={} contentPolicy={} sourceTransportFps={} backend={} (invalid union=provider+duplicate-both; no automatic content-rate/multiplier rewrite)",options.settings.revision,graphStats.fgSubmittedCandidates,graphStats.fgGeneratedFrames,graphStats.fgDisabledFrames,graphStats.fgProviderDisabled,graphStats.fgDuplicateSuppressed,graphStats.fgBothDisabled,int(options.settings.content),activeSource->info().averageFps,frameGenerationBackendName(options.settings.frameGenerationBackend)));
                if(!isCapture&&Clock::now()>=nextTimingLog){const double playbackSpeedNow=[&]{std::lock_guard lock(mutex_);return snapshot_.playbackSpeed;}();
                // present-deviation / output-queue are folded into the
                // player-timing line below (sweep E5); the separate lines stay
                // available under VEYRA_VERBOSE_FRAME_LOGS for old tooling.
                const auto dashboard=frameFlow->snapshot(host100ns());
                if(veyra::log::verboseFrameLogs()){
                veyra::log::info("present-deviation",std::format("samples={} entryAbsP95Ms={:.3f} returnAbsP95Ms={:.3f} (one sample per actual submission; media-clock deviation, not scanout latency)",presentReturnDeviation.size(),presentEntryDeviation.p95(),presentReturnDeviation.p95()));
                veyra::log::info("output-queue",std::format("pendingFrames={} enhancementProcessingMs={:.3f} extraDelayEstimateMs={:.3f}",dashboard.pendingOutputFrames,dashboard.enhancementProcessing.mean.value_or(-1),dashboard.cpuTiming[size_t(diagnostics::CpuStage::EnhancementDelayEstimate)].mean.value_or(-1)));}
                uint64_t vramBudget=0,vramUsage=0;ctx.videoMemoryInfo(vramBudget,vramUsage);watchVram(vramUsage,vramBudget);
                veyra::log::info("player-timing",std::format("vramMiB={} budgetMiB={} revision={} decodeP95Ms={:.3f} graphSubmitP95Ms={:.3f} gpuReadyP95Ms={:.3f} presentP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} previewSkipped={} playbackSpeed={:.3f} processed={} entryAbsP95Ms={:.3f} returnAbsP95Ms={:.3f} pendingFrames={} enhancementProcessingMs={:.3f}",vramUsage>>20,vramBudget>>20,options.settings.revision,decodeTimes.p95(),processTimes.p95(),liveScheduler?completed.readyP95:gpuReadyTimes.p95(),presentP95,gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,measured.flow.counters.previewSkippedBeforeGraph,playbackSpeedNow,sourceFrames-statsSourceBase,presentEntryDeviation.p95(),presentReturnDeviation.p95(),dashboard.pendingOutputFrames,dashboard.enhancementProcessing.mean.value_or(-1)));nextTimingLog=Clock::now()+std::chrono::seconds(1);}
                if(isCapture&&Clock::now()>=nextTimingLog){
                    logFrameFlow(measured.flow,"active");
                    const auto rates=snapshot();
                    // Adapter memory every second: a long session that slowly runs out of video
                    // memory gets slower in every pass at once (field log 2026-10-01).
                    uint64_t vramBudget=0,vramUsage=0;ctx.videoMemoryInfo(vramBudget,vramUsage);watchVram(vramUsage,vramBudget);
                    veyra::log::info("frame-rate",std::format("revision={} gpuCompletedFps={:.2f} completedReal={} capture60To30={} rateSkipped={} presentSubmitFps={:.2f} windowMs=1000 vramMiB={} budgetMiB={} (not scanout FPS)",options.settings.revision,rates.fps,rates.processedCompleted,rates.captureHalfRate,rates.captureRateSkipped,rates.submissionFps.value_or(0),vramUsage>>20,vramBudget>>20));
                    veyra::log::info("capture-timing",std::format("revision={} received={} processed={} dropped={} callbackFps={:.2f} readAgeMs={:.3f} callbackToPresentReturnP95Ms={:.3f} processCpuP95Ms={:.3f} gpuReadyP95Ms={:.3f} schedulingWaitP95Ms={:.3f} presentCpuP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} nr={} nvof={} generated={} historyResets={} presentationDrains={} presentationCompletedReal={} presentationSkippedGenerated={} presentationCancelledJobs={} singleGpuOwner=1 batchCapacity=2 (not HDMI-to-display latency)",options.settings.revision,captureStats.received,sourceFrames-statsSourceBase,captureStats.dropped,captureStats.callbackFps,captureStats.readAgeMs,ageP95,processTimes.p95(),liveScheduler?completed.readyP95:gpuReadyTimes.p95(),waitP95,presentP95,gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,graphStats.nrEvaluateCount,graphStats.nvofExecuteCount,graphStats.fgGeneratedFrames,historyResets.load(),presentationDrains.load(),presentationCompletedReal.load(),presentationSkippedGenerated.load(),presentationCancelledJobs.load()));
                    nextTimingLog=Clock::now()+std::chrono::seconds(1);
                }
                loopTrace.mark("periodicLogs");
            }
        }while(false);
    }catch(const std::exception& e){veyra::log::error("engine",e.what());status(L"引擎异常，请查看诊断",true);failed=true;}
    (void)failed;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    {std::lock_guard lock(mutex_);activeRemote_.reset();}
    if(remote)remote->close();
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    {std::lock_guard lock(mutex_);activeMoonlight_.reset();}
    if(moon)moon->close();
#endif
#ifdef VEYRA_ENABLE_XBOX
    {std::lock_guard lock(mutex_);activeXbox_.reset();}
    if(xb)xb->close();
#endif
    // Each step is logged: a session that stopped responding after a remote
    // play failure left "graph shutdown complete" as its last line.
    audioPipe.stopThread();audio.shutdown();veyra::log::info("engine","teardown: audio stopped");
    ring.drainQueue();presenter.close();graph.shutdown();
    veyra::log::info("engine","teardown: closing sources");
    captureSource.close();screenSource.close();source.close();av_frame_free(&cachedFrame);av_frame_free(&imageFrame);
    veyra::log::info("engine","teardown: sources closed");
    ring.shutdown();ctx.shutdown();
    {std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.audioEndpointRecovering=false;snapshot_.audioRebuffering=false;}
    CoUninitialize();
}
}
