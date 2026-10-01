#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <functional>
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/PresentationSettings.h"
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/engine/PreviewView.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/remoteplay/SessionInbox.h"
#ifdef VEYRA_ENABLE_MOONLIGHT
#include "veyra/source/MoonlightSessionSource.h"
#endif
#ifdef VEYRA_ENABLE_XBOX
#include "veyra/source/XboxSessionSource.h"
#endif
#include "veyra/media/AudioTrack.h"
#include "veyra/sink/VideoEncoder.h"
#include "veyra/engine/ExportOptions.h"
namespace veyra::source { struct RemotePlayConnectDesc; class RemotePlaySessionSource; struct MoonlightConnectDesc; class MoonlightSessionSource; struct XboxConnectDesc; class XboxSessionSource; }
namespace veyra::remoteplay { struct ControllerState;struct ControllerFeedback; }
namespace veyra::sink { struct RgbaImage; }
namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::engine {
class FrameFlowWindow;
struct PlayerOptions { bool nr=false,sr=false,fg=false,realtime=true; uint32_t fgMultiplier=2; EnhancementSettings settings;
    std::optional<ChainRuntimeOrder> nodeOrder; // in-process only; not export shared memory
    int audioStreamIndex=-1; // export selection; -1 selects the container default
    double exportStartSeconds=0.0; // export-only trim; first complete frame at/after this time
    double exportEndSeconds=0.0; // export-only trim; 0 means source end
    sink::ExportRateControl exportRateControl=sink::ExportRateControl::Cq;
    ExportMediaOptions exportMedia;
    std::wstring exportTemporaryPath; // owned, uniquely reserved by this attempt
    bool captureReplayForTest=false; // file-backed live scheduler test; never enabled by UI
    bool captureCpuUnpack=false; // N1 diagnostic: legacy per-pixel CPU unpack
    bool softwareDecode=false; // user setting: files skip D3D12VA and decode on the CPU
    bool hardwareDecodeOnly=false; // user setting "强制硬解": files fail instead of falling back to software
    EnhancementSettings snapshot()const{auto s=settings;s.nr=nr;s.sr=sr;s.multiplier=fg?fgMultiplier:1;s.nrPolicy=realtime?(settings.nrPolicy==pipeline::NrSizePolicy::Native?pipeline::NrSizePolicy::Realtime:settings.nrPolicy):pipeline::NrSizePolicy::Native;return s;}
    static PlayerOptions from(EnhancementSettings s,std::optional<ChainRuntimeOrder> order=std::nullopt){PlayerOptions o;o.nr=s.nr;o.sr=s.sr;o.fg=s.multiplier>1;o.fgMultiplier=std::max(2u,s.multiplier);o.realtime=s.nrPolicy!=pipeline::NrSizePolicy::Native;o.settings=s;o.nodeOrder=order;return o;}
};
enum class TransportState { Empty, Opening, Playing, Paused, Ended, Stopping, Failed };
struct PlayerSnapshot {
    PresentationSettings presentation, presentationEffective;
    // XeSS/FSR owns Present; the UI greys the pacing controls when true.
    bool presentationProviderOwned=false;
    uint64_t presentationRevision=0;
    std::wstring presentationStatus;
    TransportState transport=TransportState::Empty;
    uint64_t sessionId=0,rejectedRevision=0;float volume=1;bool muted=false,audioAvailable=false;
    // Video memory that kept growing with no settings change (MiB above the session's low point),
    // 0 until the watchdog fires. Field logs 2026-10-01: +3 GB a minute in fullscreen only.
    uint64_t vramRunawayMiB=0;
    std::wstring status=L"请打开视频或图片";
    EnhancementSettings desired,applied;bool applying=false;
    bool nrActive=false,srActive=false,fgActive=false;
    bool videoHdrActive=false; // TrueHDR actually running this graph (needs an HDR display path)
    std::wstring backendWarning;
    std::wstring sourceNotice;
    std::wstring colorStatus;
    std::wstring videoHdrStatus;
    sink::CaptureAudioState captureAudio;
    unsigned audioInputChannels=0,audioOutputChannels=0;
    std::vector<media::AudioTrack> audioTracks;
    int selectedAudioTrack=-1;
    bool audioRebuffering=false;
    uint64_t audioVideoWaits=0;
    bool audioEndpointRecovering=false;HRESULT audioEndpointError=S_OK;uint64_t audioEndpointRecoveries=0;
    uint64_t seekRequested=0,seekPresented=0;double seekTarget=0;
    double position=0,duration=0,fps=0,lateMs=0,lateP95Ms=0;
    double nominalSourceFps=0;
    // Source geometry for the UI: coded size and the container's display aspect
    // ratio (0 = use coded size). Known at open time, before the first frame.
    uint32_t sourceWidth=0,sourceHeight=0;
    double sourceDisplayAspect=0.0;
    int sourceRotationDegrees=0;
    // Poster frame for the minimal-mode bar, as a small RGBA8 image. Empty when
    // unavailable; the UI must not fabricate one.
    std::vector<uint8_t> posterRgba;
    uint32_t posterWidth=0,posterHeight=0;
    diagnostics::FrameMetrics metrics;
    std::optional<double> submissionFps;
    uint32_t flowPerf=0;int contentFps=0;
    uint64_t frames=0,generated=0;
    uint64_t captureReceived=0,captureDropped=0,nrEvaluated=0,nvofExecuted=0;
    uint64_t captureRateSkipped=0,processedCompleted=0;
    bool captureHalfRate=false;
    bool captureRecovering=false;unsigned captureReconnectAttempts=0;
    double captureFps=0,captureReadAgeMs=0,captureAgeMs=0,captureAgeP95Ms=0;
    double schedulingWaitP95Ms=0,processCpuP95Ms=0,presentCpuP95Ms=0;
    // Realtime file preview: dropped enhancement opportunities this metrics
    // window, and measured media-advance / wall-clock playback speed (1.0 =
    // normal speed). Zero previewSkipped means every decoded frame was enhanced.
    uint64_t previewSkipped=0;
    // Paused/still-image frames re-rendered because a live parameter changed
    // (colour is fused into the ingest dispatch, so a paused picture only
    // updates when the cached source frame is processed again). Test-visible
    // proof that "adjust while paused" works.
    uint64_t pausedFrameRefreshes=0;
    double playbackSpeed=0;
    bool fgBudgetLimited=false,xessGenerationSuppressed=false;
    unsigned previewFgMultiplier=0;
    // Runtime-reported DLSSG MultiFrameCountMax: 1 = 2X only, 5 = 6X.
    // 0 = not queried yet (no session). The UI uses this to offer only the
    // multipliers the active GPU can actually honour.
    int fgMultiFrameMax=0;
    bool fgCapabilityKnown=false;
    // XeSS frame-generation ceiling (generated frames; 1 = stock 2X, 3 = 4X).
    // 0 = no XeSS session has reported yet; the settings UI then falls back to
    // the audited-provider probe.
    int xessMaxInterpolatedFrames=0;
    // AMD FSR frame-generation ceiling (generated frames per presented frame;
    // 1 = 2X). 0 = no FSR session has reported yet.
    int fsrMaxGeneratedFrames=0;
    std::string fsrProviderVersion;
    bool running=false,failed=false,image=false,capture=false,remotePlay=false;
    int remotePlayState=0; uint64_t remotePlaySkipped=0;
    bool remoteRecovering=false;unsigned remoteReconnectAttempts=0;std::wstring remoteRecoveryMessage;
    remoteplay::SessionInbox::Snapshot remoteStream;
    double remoteReceivedFps=0,remoteDecodedFps=0;bool remoteRatesReady=false;uint64_t remoteReceived=0,remoteDecoded=0,remoteIngressDropped=0;
#ifdef VEYRA_ENABLE_MOONLIGHT
    bool moonlightActive=false;source::MoonlightStats moonlight;
#endif
#ifdef VEYRA_ENABLE_XBOX
    bool xboxActive=false;source::XboxStats xbox;
#endif
};
class EngineController {
public:
    EngineController();
    ~EngineController();
    bool idle()const;
    bool requestSettings(EnhancementSettings,std::optional<ChainRuntimeOrder> = std::nullopt,uint64_t* acceptedRevision=nullptr);
    void requestPresentation(PresentationSettings);
    void open(HWND video,const std::wstring& path,PlayerOptions options);
#ifdef VEYRA_ENABLE_REMOTEPLAY
    void openRemotePlay(HWND, source::RemotePlayConnectDesc, PlayerOptions);
    remoteplay::ControllerFeedback remotePlayFeedback();
    void remotePlayController(const remoteplay::ControllerState&);
    void remotePlayLoginPin(std::string);
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    void openMoonlight(HWND, source::MoonlightConnectDesc, PlayerOptions);
    // Input for the running Moonlight session; all are no-ops when none is streaming.
    void moonlightController(const remoteplay::ControllerState&);
    remoteplay::ControllerFeedback moonlightFeedback();
    void moonlightKey(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down);
    void moonlightMouseMove(int dx, int dy);
    void moonlightMouseButton(int button, bool down);
    void moonlightScroll(int delta, bool horizontal);
    void moonlightReleaseInput();
#endif
#ifdef VEYRA_ENABLE_XBOX
    void openXbox(HWND, source::XboxConnectDesc, PlayerOptions);
    // Pad and rumble for the running Xbox session; no-ops when none is streaming.
    void xboxController(const remoteplay::ControllerState&);
    remoteplay::ControllerFeedback xboxFeedback();
#endif
    void stop();
    void comparison(int mode,bool base,float split=.5f){comparisonMode_=mode;comparisonBase_=base;comparisonSplit_=std::clamp(split,0.0f,1.0f);}
    void pause(bool p);
    void previewView(PreviewView view){if(!std::isfinite(view.zoom)||!std::isfinite(view.centerX)||!std::isfinite(view.centerY)||!std::isfinite(view.displayAspect)||view.displayAspect<0||view.mode<0||view.mode>6)return;std::lock_guard lock(mutex_);view.zoom=std::clamp(view.zoom,.05f,64.0f);if(view.displayAspect==0)view.displayAspect=previewView_.displayAspect;previewView_=view;}
    PreviewView previewView()const{std::lock_guard lock(mutex_);return previewView_;}
    void setVolume(float gain,bool mute);
    bool selectAudioTrack(uint64_t sessionId,int streamIndex);
    void seek(double seconds){if(!std::isfinite(seconds)||seconds<0)return;std::lock_guard lock(mutex_);snapshot_.seekTarget=seconds;++snapshot_.seekRequested;seekSeconds_.store(seconds);}
    void saveFrame(const std::wstring& path);
    void startExport(const std::wstring& input,const std::wstring& output,PlayerOptions,bool hevc);
    PlayerSnapshot snapshot()const;
private:
    void run(HWND,std::wstring,PlayerOptions,std::shared_ptr<source::RemotePlayConnectDesc> remoteRequest={},std::shared_ptr<source::MoonlightConnectDesc> moonlightRequest={},std::shared_ptr<source::XboxConnectDesc> xboxRequest={});
    void runLargeImage(HWND,const sink::RgbaImage&,PlayerOptions,gfx::D3D12DeviceContext&,gfx::CommandSlotRing&);
    void post(std::function<void()>);
    void dispatch();
    void status(const std::wstring&,bool failed=false);
    mutable std::mutex mutex_;
    PlayerSnapshot snapshot_;
    PresentationSettings presentation_;
    uint64_t presentationRevision_=1;
    std::shared_ptr<FrameFlowWindow> activeFlow_;
    std::shared_ptr<source::RemotePlaySessionSource> activeRemote_;
    std::shared_ptr<source::MoonlightSessionSource> activeMoonlight_;
    std::shared_ptr<source::XboxSessionSource> activeXbox_;
    PreviewView previewView_;
    std::wstring savePath_;
    std::thread worker_;
    std::condition_variable wake_;std::function<void()> pending_;bool shutdown_=false,busy_=false;
    EnhancementSettings desired_;uint64_t nextRevision_=1;
    std::optional<ChainRuntimeOrder> desiredNodeOrder_; // protected by mutex_, paired with desired_
    std::atomic<bool> stop_{false},paused_{false},muted_{false};
    std::atomic<float> volume_{1};uint64_t sessionId_=0;
    std::atomic<double> seekSeconds_{-1};
    int pendingAudioTrack_=-1;uint64_t pendingAudioSession_=0; // mutex_
    std::atomic<int> comparisonMode_{0};std::atomic<bool> comparisonBase_{false};std::atomic<float> comparisonSplit_{.5f};
    // Guarded by mutex_: DLSSG MultiFrameCountMax of the active session.
    int fgMultiFrameMaxCap_=0;
    // Guarded by mutex_: XeSS provider ceiling of the active session (0 = unknown).
    int xessMaxInterpolatedFramesCap_=0;
    // Guarded by mutex_: AMD FSR ceiling of the active session (0 = unknown).
    int fsrMaxGeneratedFramesCap_=0;
};
}
