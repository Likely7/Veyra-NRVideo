#pragma once
// The QML front end's only view of the player.
//
// Design rules this file follows, all of them deliberate:
//
//   * QML never sees EngineController, D3D12, decoded frames or the audio
//     clock. It sees plain values and calls plain commands.
//   * The video is NOT part of the QML scene graph. It stays a native child
//     HWND with its own flip-model swapchain (measured in S3: 499/499 presents
//     at 0.205 ms while the QML scene animated at full panel rate). QML is drawn
//     around and above it. Compositing video into QML would cost exactly the
//     latency that is this product's advantage over OBS and PotPlayer.
//   * One timer drives updates, and it publishes only what changed: the facade
//     hands out a revision counter, so a 60 Hz tick does not mean 60 property
//     writes. This is what keeps the UI from becoming the latency bottleneck.
//
// Threading: every method here is called on the UI thread. Commands go through
// the facade, never executed inline, so a slow engine cannot block a frame of
// QML animation.
//
// Scope note: this exposes what the engine can actually do today. Playback
// speed measurement and requested file-preview rate are separate values.
#include <QObject>
#include <QString>
#include <QRectF>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "veyra/engine/EffectChain.h"
#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/ui/PlayerUiFacade.h"

class QQmlEngine;
namespace veyra::ui {

class MoonlightModel;
class XboxModel;

class QmlPlayerBridge : public QObject {
    Q_OBJECT
    // --- professional-page readouts ---------------------------------------
    // Every one of these reports an engine value. Where the engine has no
    // measurement the getter says so (e.g. displayFpsKnown stays false, because
    // a display rate is only real if the system display event can be read - the
    // design requires exactly this distinction rather than reusing submit FPS).
    Q_PROPERTY(QString outputSummary READ outputSummary NOTIFY snapshotChanged)
    Q_PROPERTY(bool fullscreenMemorySafe READ fullscreenMemorySafe NOTIFY snapshotChanged)
    Q_PROPERTY(double displayFps READ displayFps NOTIFY snapshotChanged)
    Q_PROPERTY(bool displayFpsKnown READ displayFpsKnown NOTIFY snapshotChanged)
    Q_PROPERTY(double queuedFrames READ queuedFrames NOTIFY snapshotChanged)
    Q_PROPERTY(bool queuedFramesKnown READ queuedFramesKnown NOTIFY snapshotChanged)
    Q_PROPERTY(int skippedFrames READ skippedFrames NOTIFY snapshotChanged)
    Q_PROPERTY(QString flowBackend READ flowBackend NOTIFY snapshotChanged)
    Q_PROPERTY(int opticalFlowChoice READ opticalFlowChoice NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList stageTimings READ stageTimings NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap nodeTimings READ nodeTimings NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap nodeAnchors READ nodeAnchors NOTIFY nodeAnchorsChanged)
    Q_PROPERTY(double stageBudgetMs READ stageBudgetMs NOTIFY snapshotChanged)
    // Existing measured enhancement-interval P95, NOT full-chain latency.
    Q_PROPERTY(double chainTotalMs READ chainTotalMs NOTIFY snapshotChanged)
    Q_PROPERTY(bool chainTotalMsKnown READ chainTotalMsKnown NOTIFY snapshotChanged)
    Q_PROPERTY(double scheduleP95Ms READ scheduleP95Ms NOTIFY snapshotChanged)

    // --- frame generation settings ----------------------------------------
    Q_PROPERTY(QString fgBackendName READ fgBackendName WRITE setFgBackendName NOTIFY settingsChanged)
    Q_PROPERTY(int vfgQuality READ vfgQuality WRITE setVfgQuality NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList fgBackendChoices READ fgBackendChoices CONSTANT)
    Q_PROPERTY(QString fgProviderText READ fgProviderText NOTIFY fgChoicesChanged)
    Q_PROPERTY(int fgMaxMultiplier READ fgMaxMultiplier NOTIFY snapshotChanged)
    Q_PROPERTY(bool fgEnabled READ fgEnabled WRITE setFgEnabled NOTIFY chainChanged)
    Q_PROPERTY(QString submitFpsLabel READ submitFpsLabel NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList fgMultiplierChoices READ fgMultiplierChoices NOTIFY fgChoicesChanged)
    Q_PROPERTY(bool fgStrict READ fgStrict WRITE setFgStrict NOTIFY settingsChanged)
    Q_PROPERTY(bool fgLowQueue READ fgLowQueue WRITE setFgLowQueue NOTIFY settingsChanged)

    // --- colour settings ---------------------------------------------------
    Q_PROPERTY(double colorExposure READ colorExposure WRITE setColorExposure NOTIFY settingsChanged)
    Q_PROPERTY(double colorContrast READ colorContrast WRITE setColorContrast NOTIFY settingsChanged)
    Q_PROPERTY(double colorSaturation READ colorSaturation WRITE setColorSaturation NOTIFY settingsChanged)
    Q_PROPERTY(double colorTemperature READ colorTemperature WRITE setColorTemperature NOTIFY settingsChanged)

    // --- shell preferences -------------------------------------------------
    // Kept by the facade so they survive a restart, and read by the shell.
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultPage READ defaultPage WRITE setDefaultPage NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultPageLabel READ defaultPageLabel NOTIFY settingsChanged)
    // Used by the shell to open on the saved page. Set from the command line in
    // tests so a screenshot does not depend on clicking the dock.
    Q_PROPERTY(QString initialPage READ initialPage WRITE setInitialPage NOTIFY settingsChanged)
    // The page the shell is showing, pushed by the shell so a command can tell
    // where the user is: opening a file from home goes to the player, and
    // opening one from the professional page stays there.
    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY settingsChanged)

    // --- settings-page reporting -------------------------------------------
    Q_PROPERTY(QString remotePlayState READ remotePlayState NOTIFY snapshotChanged)
    // The loaded runtime components, as the engine reports them. A hand-written
    // list here could drift from what the package actually contains.
    Q_PROPERTY(QVariantList componentList READ componentList NOTIFY snapshotChanged)

    // --- capture dialog ----------------------------------------------------
    // Device and format lists come from the source's own enumeration, so the UI
    // can only offer what the machine actually reports.
    Q_PROPERTY(QVariantList captureDevices READ captureDevices NOTIFY captureChanged)
    Q_PROPERTY(QString captureDeviceId READ captureDeviceId WRITE setCaptureDeviceId NOTIFY captureChanged)
    Q_PROPERTY(QString captureDeviceLabel READ captureDeviceLabel NOTIFY captureChanged)
    Q_PROPERTY(bool captureForceSdr READ captureForceSdr WRITE setCaptureForceSdr NOTIFY settingsChanged)
    Q_PROPERTY(bool amdNrGpu READ amdNrGpu CONSTANT)
    Q_PROPERTY(QVariantMap effectCapabilities READ effectCapabilities NOTIFY fgChoicesChanged)
    Q_PROPERTY(QVariantList nrRuntimeChoices READ nrRuntimeChoices NOTIFY fgChoicesChanged)
    Q_PROPERTY(QVariantList srBackendChoices READ srBackendChoices NOTIFY fgChoicesChanged)
    Q_PROPERTY(bool captureFlipVertical READ captureFlipVertical WRITE setCaptureFlipVertical NOTIFY settingsChanged)
    // P4-e: the full capture connection, as the 1.4.4 panel: device details and
    // formats come from asynchronous DirectShow queries (never on the UI thread),
    // the choice is remembered in capture-preferences.v1 for "continue".
    Q_PROPERTY(QVariantList captureFormats READ captureFormats NOTIFY captureChanged)
    Q_PROPERTY(QString captureFormatKey READ captureFormatKey WRITE setCaptureFormatKey NOTIFY captureChanged)
    Q_PROPERTY(QVariantList captureAudioInputs READ captureAudioInputs NOTIFY captureChanged)
    Q_PROPERTY(int captureAudioChoice READ captureAudioChoice WRITE setCaptureAudioChoice NOTIFY captureChanged)
    Q_PROPERTY(int captureColorSpace READ captureColorSpace WRITE setCaptureColorSpace NOTIFY captureChanged)
    Q_PROPERTY(int captureColorRange READ captureColorRange WRITE setCaptureColorRange NOTIFY captureChanged)
    Q_PROPERTY(double captureRequestedFps READ captureRequestedFps WRITE setCaptureRequestedFps NOTIFY captureChanged)
    Q_PROPERTY(int captureAudioIngress READ captureAudioIngress WRITE setCaptureAudioIngress NOTIFY settingsChanged)
    Q_PROPERTY(int captureBufferMode READ captureBufferMode WRITE setCaptureBufferMode NOTIFY settingsChanged)
    Q_PROPERTY(bool captureQueryBusy READ captureQueryBusy NOTIFY captureChanged)
    Q_PROPERTY(QString captureStatus READ captureStatus NOTIFY captureChanged)
    // Magewell Pro Capture low-latency mode (MagewellCapture.h): whether the selected device is
    // one, and a one-line state (active with its latency, missing runtime, or why it fell back).
    Q_PROPERTY(bool captureMagewellDevice READ captureMagewellDevice NOTIFY captureChanged)
    Q_PROPERTY(QString captureMagewellStatus READ captureMagewellStatus NOTIFY snapshotChanged)

    // --- screen-capture dialog ---------------------------------------------
    Q_PROPERTY(QVariantList screenTargets READ screenTargets NOTIFY captureChanged)
    Q_PROPERTY(QString screenTargetId READ screenTargetId WRITE setScreenTargetId NOTIFY captureChanged)
    Q_PROPERTY(QString screenTargetLabel READ screenTargetLabel NOTIFY captureChanged)
    // Screen capture options (ScreenCaptureOptions): kind 0 window / 1 monitor,
    // method 0 WGC / 1 DXGI (monitor only), fps cap index into {follow,30,60,120,144,240},
    // pointer, crop margins in source pixels, and "fill the window".
    Q_PROPERTY(QVariantMap screenOptions READ screenOptions NOTIFY captureChanged)

    // Subtitles (P4-c): the engine's SubtitleLoader reads the embedded tracks and
    // the same-name external file; the old shell's SubtitleOverlay draws them as a
    // layered child of the native video window, so the picture's window region
    // also clips the subtitles away from QML popups. Style lives in preferences.
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitlePrimary READ subtitlePrimary WRITE setSubtitlePrimary NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitleSecondary READ subtitleSecondary WRITE setSubtitleSecondary NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitleOffsetMs READ subtitleOffsetMs WRITE setSubtitleOffsetMs NOTIFY subtitlesChanged)
    Q_PROPERTY(QString subtitleStatus READ subtitleStatus NOTIFY subtitlesChanged)
    Q_PROPERTY(bool subtitleAligning READ subtitleAligning NOTIFY subtitlesChanged)
    // The text the overlay is drawing now (joined lines), for tests and the a11y name.
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY subtitleTextChanged)
    // Extra space the shell keeps free at the bottom (fullscreen controls).
    Q_PROPERTY(int subtitleBottomInset READ subtitleBottomInset WRITE setSubtitleBottomInset NOTIFY subtitlesChanged)

    // --- ps5 dialog --------------------------------------------------------
    Q_PROPERTY(QString remotePlayHost READ remotePlayHost WRITE setRemotePlayHost NOTIFY settingsChanged)
    Q_PROPERTY(QString remotePlayPin READ remotePlayPin WRITE setRemotePlayPin NOTIFY settingsChanged)
    // P4-e PS5 Remote Play, ported from the 1.4.4 panel: saved pairings (shared
    // %LOCALAPPDATA%\Veyra\remoteplay, encrypted), discovery, pairing, PSN login,
    // wake, stream profile, decode/sampling, view-only, login PIN, controller
    // forwarding and gyro calibration. `ps5` carries the form state.
    Q_PROPERTY(QVariantMap ps5 READ ps5 NOTIFY ps5Changed)
    Q_PROPERTY(QVariantList ps5Profiles READ ps5Profiles NOTIFY ps5Changed)
    // PC streaming (Moonlight / Sunshine): hosts, pairing, apps and stream settings live in
    // `moonlight` (null when the build has no Moonlight); capture state and the stats overlay here.
    Q_PROPERTY(QObject* moonlight READ moonlightModel CONSTANT)
    // Xbox home streaming (unofficial): sign-in, consoles and stream state; null without Xbox support.
    Q_PROPERTY(QObject* xbox READ xboxModel CONSTANT)
    Q_PROPERTY(bool moonlightCaptured READ moonlightCaptured NOTIFY moonlightUiChanged)
    Q_PROPERTY(bool moonlightStatsVisible READ moonlightStatsVisible WRITE setMoonlightStatsVisible NOTIFY moonlightUiChanged)

    // Shell preferences that are not enhancement settings: screenshot folder,
    // subtitle look, audio output. Stored in <data>/qml-preferences.v1.json and
    // validated key by key in setPreference(); anything unknown is refused.
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    // The interface language in use (zh-CN / zh-TW / en / ja), after "auto" is resolved;
    // preferences.language holds the choice itself.
    Q_PROPERTY(QString uiLanguage READ uiLanguage NOTIFY preferencesChanged)
    Q_PROPERTY(QString screenshotDirectory READ screenshotDirectory NOTIFY preferencesChanged)
    Q_PROPERTY(QString lastScreenshot READ lastScreenshot NOTIFY preferencesChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    // Rebindable shortcuts: action -> key sequence text (defaults merged in).
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY preferencesChanged)
    // The interface scale chosen for the next start (0 = follow Windows).
    Q_PROPERTY(int uiScaleActive READ uiScaleActive CONSTANT)
    Q_PROPERTY(bool obsGameCaptureActive READ obsGameCaptureActive CONSTANT)
    // This run draws the interface in software because RivaTuner (RTSS) was running at start
    // (设置 → 监控软件兼容, main.cpp): its OSD then sits on the video only.
    Q_PROPERTY(bool overlayCompatActive READ overlayCompatActive CONSTANT)
    // Audio output (P4-d): the active WASAPI render endpoints by device id, the
    // stored choice (preferences.audioDevice, empty = system default) and what
    // the renderer actually opened, including a fallback to the default.
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    // The GPU the "GPU 占用" orb measures (设置 → 通用与外观): [{id, label}], the first
    // entry ("", 自动) picks the high-performance (discrete) GPU. Field report
    // 2026-10-02: the orb used to add the integrated and the discrete GPU together.
    Q_PROPERTY(QVariantList gpuMonitorChoices READ gpuMonitorChoices CONSTANT)
    Q_PROPERTY(QString gpuMonitorName READ gpuMonitorName NOTIFY preferencesChanged)
    Q_PROPERTY(QString audioOutputStatus READ audioOutputStatus NOTIFY audioDevicesChanged)
    Q_PROPERTY(bool audioOutputFallback READ audioOutputFallback NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

    // --- playback ----------------------------------------------------------
    Q_PROPERTY(QString statusText READ statusText NOTIFY snapshotChanged)
    Q_PROPERTY(bool running READ running NOTIFY snapshotChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY snapshotChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY snapshotChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY snapshotChanged)
    Q_PROPERTY(bool isCapture READ isCapture NOTIFY snapshotChanged)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY snapshotChanged)
    Q_PROPERTY(double position READ position NOTIFY snapshotChanged)
    Q_PROPERTY(double duration READ duration NOTIFY snapshotChanged)
    Q_PROPERTY(double progress READ progress NOTIFY snapshotChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY snapshotChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY snapshotChanged)

    // --- source ------------------------------------------------------------
    Q_PROPERTY(int thumbnailGeneration READ thumbnailGeneration NOTIFY thumbnailGenerationChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY snapshotChanged)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY snapshotChanged)
    Q_PROPERTY(double sourceFps READ sourceFps NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceRotation READ sourceRotation NOTIFY snapshotChanged)
    // The film's display aspect, so the window can snap to it in cinema mode
    // exactly as the prototype's fitAspect() does. 0 when nothing is open.
    Q_PROPERTY(double sourceAspect READ sourceAspect NOTIFY snapshotChanged)
    Q_PROPERTY(double previewAspect READ previewAspect NOTIFY compareChanged)

    // --- live performance --------------------------------------------------
    // Submit FPS only. There is deliberately no "display FPS": the user asked
    // for it to be left out (MSI Afterburner reads the real panel rate, and a
    // number we cannot measure must not be shown as if we measured it).
    Q_PROPERTY(double submitFps READ submitFps NOTIFY snapshotChanged)
    Q_PROPERTY(bool submitFpsKnown READ submitFpsKnown NOTIFY snapshotChanged)
    Q_PROPERTY(double lateMs READ lateMs NOTIFY snapshotChanged)
    Q_PROPERTY(double lateP95Ms READ lateP95Ms NOTIFY snapshotChanged)
    Q_PROPERTY(QString metricsSummary READ metricsSummary NOTIFY snapshotChanged)
    Q_PROPERTY(bool nrActive READ nrActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool srActive READ srActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool fgActive READ fgActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool captureRecovering READ captureRecovering NOTIFY snapshotChanged)
    Q_PROPERTY(double captureFps READ captureFps NOTIFY snapshotChanged)
    Q_PROPERTY(double captureDropped READ captureDropped NOTIFY snapshotChanged)
    Q_PROPERTY(QString backendWarning READ backendWarning NOTIFY snapshotChanged)

    // --- settings (what the UI shows) --------------------------------------
    Q_PROPERTY(bool nrEnabled READ nrEnabled WRITE setNrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int selectedNrLayer READ selectedNrLayer WRITE setSelectedNrLayer NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList nrLayers READ nrLayers NOTIFY chainChanged)
    // List mode's NR master switch: true while any NR layer is on.
    Q_PROPERTY(bool nrAnyEnabled READ nrAnyEnabled NOTIFY chainChanged)
    Q_PROPERTY(bool srEnabled READ srEnabled WRITE setSrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int fgMultiplier READ fgMultiplier WRITE setFgMultiplier NOTIFY settingsChanged)
    Q_PROPERTY(bool lowLatency READ lowLatency WRITE setLowLatency NOTIFY settingsChanged)
    Q_PROPERTY(bool nrTemporal READ nrTemporal WRITE setNrTemporal NOTIFY settingsChanged)
    Q_PROPERTY(double nrHoldStrength READ nrHoldStrength WRITE setNrHoldStrength NOTIFY settingsChanged)
    Q_PROPERTY(double nrHoldTolerance READ nrHoldTolerance WRITE setNrHoldTolerance NOTIFY settingsChanged)
    Q_PROPERTY(bool videoHdr READ videoHdr WRITE setVideoHdr NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap videoHdrParams READ videoHdrParams NOTIFY chainChanged)
    Q_PROPERTY(int nrStyle READ nrStyle WRITE setNrStyle NOTIFY settingsChanged)
    Q_PROPERTY(double nrIntensity READ nrIntensity WRITE setNrIntensity NOTIFY settingsChanged)
    Q_PROPERTY(double nrTone READ nrTone WRITE setNrTone NOTIFY settingsChanged)
    Q_PROPERTY(double nrStructure READ nrStructure WRITE setNrStructure NOTIFY settingsChanged)
    Q_PROPERTY(double nrSkin READ nrSkin WRITE setNrSkin NOTIFY settingsChanged)
    Q_PROPERTY(bool nrAutoMask READ nrAutoMask WRITE setNrAutoMask NOTIFY settingsChanged)
    Q_PROPERTY(bool nrUiCorrection READ nrUiCorrection WRITE setNrUiCorrection NOTIFY settingsChanged)
    Q_PROPERTY(int videoSrQuality READ videoSrQuality WRITE setVideoSrQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool protectionEnabled READ protectionEnabled WRITE setProtectionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap protectionState READ protectionState NOTIFY chainChanged)
    Q_PROPERTY(bool colorEnabled READ colorEnabled WRITE setColorEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int audioOffsetMs READ audioOffsetMs WRITE setAudioOffsetMs NOTIFY settingsChanged)
    // 1.4.4 声音补偿: 0 自动(软件估算) / 1 手动 / 2 关闭; live sources only.
    Q_PROPERTY(int audioSyncMode READ audioSyncMode WRITE setAudioSyncMode NOTIFY settingsChanged)
    Q_PROPERTY(bool audioSyncLive READ audioSyncLive NOTIFY snapshotChanged)
    Q_PROPERTY(double audioCompensationMs READ audioCompensationMs NOTIFY snapshotChanged)
    // Performance orbs and the status light (1.4.4 live status dashboard).
    Q_PROPERTY(double gpuUtilization READ gpuUtilization NOTIFY perfChanged)
    Q_PROPERTY(bool gpuUtilizationKnown READ gpuUtilizationKnown NOTIFY perfChanged)
    Q_PROPERTY(QString runStatus READ runStatus NOTIFY perfChanged)
    Q_PROPERTY(QString runStatusLevel READ runStatusLevel NOTIFY perfChanged)
    Q_PROPERTY(QString runStatusDetail READ runStatusDetail NOTIFY perfChanged)
    // Achieved output rate / target (source x multiplier); -1 while unknown.
    Q_PROPERTY(double outputRateRatio READ outputRateRatio NOTIFY perfChanged)
    // Frame interval history (ms, newest last) for the frame-time line.
    Q_PROPERTY(QVariantList frameTimes READ frameTimes NOTIFY perfChanged)
    // 补帧 / 光流 (design gap A1-A5): motion estimation quality 0 性能 1 平衡 2 质量,
    // AMD half-resolution flow, content cadence (engine::ContentRate order).
    Q_PROPERTY(int flowQuality READ flowQuality WRITE setFlowQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool amdFlowHalf READ amdFlowHalf WRITE setAmdFlowHalf NOTIFY settingsChanged)
    Q_PROPERTY(int contentRate READ contentRate WRITE setContentRate NOTIFY settingsChanged)
    Q_PROPERTY(int hdrOutputMode READ hdrOutputMode WRITE setHdrOutputMode NOTIFY settingsChanged)
    Q_PROPERTY(int fgMotionSource READ fgMotionSource WRITE setFgMotionSource NOTIFY settingsChanged)
    Q_PROPERTY(int srMotionSource READ srMotionSource WRITE setSrMotionSource NOTIFY settingsChanged)
    Q_PROPERTY(int nrMotionSource READ nrMotionSource WRITE setNrMotionSource NOTIFY settingsChanged)
    // Presentation: display sync 0 允许撕裂 1 垂直同步 2 自动; output cap 0 关闭
    // 1 跟随显示器 2 自定义 (+ fps). Owned by XeSS when presentationOwned.
    Q_PROPERTY(int displaySync READ displaySync WRITE setDisplaySync NOTIFY presentationChanged)
    Q_PROPERTY(int outputRateMode READ outputRateMode WRITE setOutputRateMode NOTIFY presentationChanged)
    Q_PROPERTY(double outputCustomFps READ outputCustomFps WRITE setOutputCustomFps NOTIFY presentationChanged)
    Q_PROPERTY(bool presentationOwned READ presentationOwned NOTIFY snapshotChanged)
    Q_PROPERTY(QString presentationStatus READ presentationStatus NOTIFY snapshotChanged)
    // 显示: compare 0 增强 1 原画 2 分屏; base false 输入原画 / true 增强前底图;
    // split 0..1 of the picture. Aspect 0 适应 1 原始 2 填充.
    Q_PROPERTY(int compareMode READ compareMode WRITE setCompareMode NOTIFY compareChanged)
    Q_PROPERTY(bool compareBase READ compareBase WRITE setCompareBase NOTIFY compareChanged)
    Q_PROPERTY(double compareSplit READ compareSplit NOTIFY compareChanged)
    Q_PROPERTY(int aspectMode READ aspectMode WRITE setAspectMode NOTIFY compareChanged)
    // Measured audio/video skew of a live source (ms); unknown otherwise.
    Q_PROPERTY(double audioSkewMs READ audioSkewMs NOTIFY snapshotChanged)
    Q_PROPERTY(bool audioSkewKnown READ audioSkewKnown NOTIFY snapshotChanged)
    // Export: an estimate of the output size ("" when there is nothing to base it on).
    Q_PROPERTY(QString exportSizeEstimate READ exportSizeEstimate NOTIFY exportChanged)
    // 继续上次: {kind: capture|ps5|screen, label, summary}; empty when none.
    Q_PROPERTY(QVariantMap lastSource READ lastSource NOTIFY preferencesChanged)
    // P5 (2026-09-29): what is playing, for the page headers.
    // sourceKind: "" | file | image | capture | ps5 | screen.
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY snapshotChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY snapshotChanged)
    // "1080p60 · YUY2 · SDR": reported values only.
    Q_PROPERTY(QString sourceFormatText READ sourceFormatText NOTIFY snapshotChanged)
    Q_PROPERTY(QString sourceRateText READ sourceRateText NOTIFY snapshotChanged)
    // 调色 undo / redo / copy / paste for the selected colour layer.
    Q_PROPERTY(bool colourCanUndo READ colourCanUndo NOTIFY settingsChanged)
    Q_PROPERTY(bool colourCanRedo READ colourCanRedo NOTIFY settingsChanged)
    Q_PROPERTY(bool colourCanPaste READ colourCanPaste NOTIFY settingsChanged)
    // Export queue rows: [{name, input, output, state, progress, note, current}].
    Q_PROPERTY(QVariantList exportItems READ exportItems NOTIFY exportChanged)
    Q_PROPERTY(QObject* exportQueueModel READ exportQueueModel CONSTANT)
    Q_PROPERTY(int exportContainer READ exportContainer WRITE setExportContainer NOTIFY exportChanged)
    Q_PROPERTY(int exportReadyCount READ exportReadyCount NOTIFY exportChanged)
    Q_PROPERTY(QVariantList exportTracks READ exportTracks NOTIFY exportChanged)
    Q_PROPERTY(int exportAudioPolicy READ exportAudioPolicy NOTIFY exportChanged)
    Q_PROPERTY(int exportSubtitlePolicy READ exportSubtitlePolicy NOTIFY exportChanged)
    Q_PROPERTY(bool exportSelectionEditable READ exportSelectionEditable NOTIFY exportChanged)
    Q_PROPERTY(bool exportCompletionSound READ exportCompletionSound WRITE setExportCompletionSound NOTIFY exportChanged)
    // Capture dialog preview: the session's poster still (the engine's own
    // first-frame poster), as a data URL; "" when the session has none. Never a
    // periodic readback: that would stall the live capture path.
    Q_PROPERTY(QString capturePreviewUrl READ capturePreviewUrl NOTIFY snapshotChanged)
    Q_PROPERTY(QString captureSignalText READ captureSignalText NOTIFY snapshotChanged)
    Q_PROPERTY(QString captureSignalLevel READ captureSignalLevel NOTIFY snapshotChanged)
    // A live source (PS5 / capture) that is still opening: what the picture area
    // says meanwhile ("" once it plays or when nothing is opening).
    Q_PROPERTY(QString liveOpeningText READ liveOpeningText NOTIFY snapshotChanged)
    // Protection regions as the engine holds them: [{left,top,right,bottom,ellipse}].
    Q_PROPERTY(QVariantList protectionRegions READ protectionRegions NOTIFY chainChanged)
    Q_PROPERTY(QString protectionDrawShape READ protectionDrawShape NOTIFY protectionDrawChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY snapshotChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY snapshotChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed NOTIFY snapshotChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY snapshotChanged)
    // True while the engine has not yet applied what the UI asked for. The UI
    // shows the pending value and marks it, rather than lying about the state.
    Q_PROPERTY(bool applying READ applying NOTIFY snapshotChanged)

    // --- effect chain ------------------------------------------------------
    // The chain as data. List mode renders it; node mode renders the same
    // objects with positions. Both edit one chain, so switching modes never
    // loses work.
    Q_PROPERTY(QVariantList chain READ chain NOTIFY chainChanged)
    Q_PROPERTY(QVariantList nodeConnections READ nodeConnections NOTIFY chainChanged)
    Q_PROPERTY(QVariantList effectCatalog READ effectCatalog NOTIFY chainChanged)
    Q_PROPERTY(QString chainError READ chainError NOTIFY chainChanged)
    Q_PROPERTY(bool chainValid READ chainValid NOTIFY chainChanged)
    Q_PROPERTY(int nodeMode READ nodeMode WRITE setNodeMode NOTIFY chainChanged)

    // --- dialogs the UI opens ----------------------------------------------
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // The name of the preset currently in effect, or "自定义" when the
    // settings no longer match any stored preset. Derived, never guessed.
    Q_PROPERTY(QString currentPresetName READ currentPresetName NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY snapshotChanged)
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY snapshotChanged)
    // The last capture session the user actually ran, for the home page's
    // "continue" row. Absent until one exists: nothing is invented.
    Q_PROPERTY(bool hasCaptureSession READ hasCaptureSession NOTIFY snapshotChanged)
    Q_PROPERTY(QString captureSessionSummary READ captureSessionSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString colorStatus READ colorStatus NOTIFY snapshotChanged)
    Q_PROPERTY(QString videoHdrStatus READ videoHdrStatus NOTIFY snapshotChanged)

    // --- export ------------------------------------------------------------
    // Backed by the real ExportJobManager: state, progress and encoded counts come
    // from its snapshot, not from a counter this file keeps.
    Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY exportChanged)
    Q_PROPERTY(bool exportPaused READ exportPaused NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportChanged)
    Q_PROPERTY(QString exportTarget READ exportTarget NOTIFY exportChanged)
    Q_PROPERTY(int exportEncoded READ exportEncoded NOTIFY exportChanged)
    Q_PROPERTY(int exportGenerated READ exportGenerated NOTIFY exportChanged)
    Q_PROPERTY(bool exportHevc READ exportHevc WRITE setExportHevc NOTIFY exportChanged)
    Q_PROPERTY(int exportBitrateMbps READ exportBitrateMbps WRITE setExportBitrateMbps NOTIFY exportChanged)
    Q_PROPERTY(int exportRateControl READ exportRateControl WRITE setExportRateControl NOTIFY exportChanged)
    Q_PROPERTY(double exportEtaSeconds READ exportEtaSeconds NOTIFY exportChanged)
    Q_PROPERTY(int exportQueueCount READ exportQueueCount NOTIFY exportChanged)
    // Queue items that could not start, and why the latest one failed.
    Q_PROPERTY(int exportQueueFailures READ exportQueueFailures NOTIFY exportChanged)
    Q_PROPERTY(QString exportQueueFailure READ exportQueueFailure NOTIFY exportChanged)
    // Image export (P4-b): each image goes through the real player chain (open,
    // first processed frame, the engine's own frame save), one after another.
    Q_PROPERTY(bool imageBatchActive READ imageBatchActive NOTIFY imageBatchChanged)
    Q_PROPERTY(int imageBatchDone READ imageBatchDone NOTIFY imageBatchChanged)
    Q_PROPERTY(int imageBatchTotal READ imageBatchTotal NOTIFY imageBatchChanged)
    Q_PROPERTY(int imageBatchFailures READ imageBatchFailures NOTIFY imageBatchChanged)
    Q_PROPERTY(QString imageBatchStatus READ imageBatchStatus NOTIFY imageBatchChanged)
    // The preset the export will use. The design requires export to select a
    // preset (list or node) rather than assembling effects separately.
    Q_PROPERTY(QString exportPresetName READ exportPresetName NOTIFY exportChanged)
    Q_PROPERTY(int exportSrTargetIndex READ exportSrTargetIndex WRITE setExportSrTargetIndex NOTIFY exportChanged)
    Q_PROPERTY(double exportTrimStart READ exportTrimStart WRITE setExportTrimStart NOTIFY exportChanged)
    Q_PROPERTY(double exportTrimEnd READ exportTrimEnd WRITE setExportTrimEnd NOTIFY exportChanged)
    // The super-resolution target, which is what actually decides the export size
    // in this engine (0 = source, then Qhd/Uhd4K/Uhd8K). A separate "export
    // resolution" field does not exist, so the UI exposes the real control instead
    // of a picker that would change nothing.
    Q_PROPERTY(int srTargetIndex READ srTargetIndex WRITE setSrTargetIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString srTargetLabel READ srTargetLabel NOTIFY settingsChanged)
    // Presets as a picker model: one preset concept, list and node together.
    Q_PROPERTY(QVariantList presetChoices READ presetChoices NOTIFY presetsChanged)
    // The design's save flow lists what the preset will contain before naming it:
    // each part, its current summary, and whether it is being saved.
    Q_PROPERTY(QVariantList presetSaveParts READ presetSaveParts NOTIFY chainChanged)
    // Which preset the app opens with, as the manage page's "set default" uses.
    Q_PROPERTY(int defaultPresetIndex READ defaultPresetIndex NOTIFY presetsChanged)

    // --- colour page -------------------------------------------------------
    // The engine's ColorSettings carries a large parameter set. Rather than one
    // property per field, the page reads and writes them by name through one pair
    // of calls, and colourParameters() reports the catalogue the UI builds its
    // controls from - so a control can only exist for a parameter that is real.
    Q_PROPERTY(QVariantList colourParameters READ colourParameters NOTIFY chainChanged)
    // The same catalogue grouped by section, so the page can nest its collapsible
    // sub-panels without re-grouping in QML.
    Q_PROPERTY(QVariantList colourGroups READ colourGroups NOTIFY chainChanged)
    Q_PROPERTY(int selectedColourLayer READ selectedColourLayer WRITE setSelectedColourLayer NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap colourState READ colourState NOTIFY settingsChanged)
    // Imported .cube files (ColorLutStore, the folder the engine resolves) and
    // the saved colour looks (ColorLookStore): a colour preset is one Color
    // node's settings, separate from the whole-chain presets.
    Q_PROPERTY(QVariantList lutLibrary READ lutLibrary NOTIFY colourLibraryChanged)
    Q_PROPERTY(QVariantList colourLooks READ colourLooks NOTIFY colourLibraryChanged)

public:
    bool amdNrGpu() const;
    QVariantMap effectCapabilities() const;
    QVariantList nrRuntimeChoices() const;
    QVariantList srBackendChoices() const;
    Q_INVOKABLE QVariantMap effectAvailability(const QString& id) const;
    // `engine` must outlive the bridge. `dataDirectory` holds ui-session.v1 and
    // the preset library; empty means the default user data directory.
    QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory = {},
                    QObject* parent = nullptr);
    ~QmlPlayerBridge() override;

    QString outputSummary() const;
    double displayFps() const;
    bool displayFpsKnown() const;
    double queuedFrames() const;
    bool queuedFramesKnown() const;
    int skippedFrames() const;
    QString flowBackend() const;
    int opticalFlowChoice() const;
    Q_INVOKABLE bool setOpticalFlowChoice(int backend);
    QVariantList stageTimings() const;
    QVariantMap nodeTimings() const;
    QVariantMap nodeAnchors() const;
    Q_INVOKABLE bool setNodeAnchor(const QString& key, double x, double y);
    Q_INVOKABLE QVariantMap colourStateAt(int index) const;
    double stageBudgetMs() const;
    double chainTotalMs() const;
    bool chainTotalMsKnown() const;
    double scheduleP95Ms() const;

    QString fgBackendName() const;
    QVariantList fgBackendChoices() const;
    QString fgProviderText() const;
    void setFgBackendName(const QString& value);
    int vfgQuality() const;
    void setVfgQuality(int value);
    int fgMaxMultiplier() const;
    bool fgEnabled() const;
    void setFgEnabled(bool enabled);
    QString submitFpsLabel() const;
    QVariantList fgMultiplierChoices() const;
    bool fgStrict() const;
    void setFgStrict(bool value);
    bool fgLowQueue() const;
    void setFgLowQueue(bool value);

    double colorExposure() const;
    void setColorExposure(double v);
    double colorContrast() const;
    void setColorContrast(double v);
    double colorSaturation() const;
    void setColorSaturation(double v);
    double colorTemperature() const;
    void setColorTemperature(double v);

    bool reducedMotion() const;
    void setReducedMotion(bool value);
    QString defaultPage() const;
    void setDefaultPage(const QString& value);
    QString defaultPageLabel() const;
    QString initialPage() const;
    void setInitialPage(const QString& value);
    QString currentPage() const;
    void setCurrentPage(const QString& value);
    QString remotePlayState() const;
    QVariantList componentList() const;
    QVariantList captureDevices() const;
    QString captureDeviceId() const;
    void setCaptureDeviceId(const QString& value);
    QString captureDeviceLabel() const;
    bool captureForceSdr() const;
    bool captureMagewellDevice() const;
    QString captureMagewellStatus() const;
    void setCaptureForceSdr(bool value);
    bool captureFlipVertical() const;
    void setCaptureFlipVertical(bool value);
    QVariantList captureFormats() const;
    QString captureFormatKey() const;
    void setCaptureFormatKey(const QString& key);
    QVariantList captureAudioInputs() const;
    int captureAudioChoice() const;
    void setCaptureAudioChoice(int choice);
    int captureColorSpace() const;
    void setCaptureColorSpace(int value);
    int captureColorRange() const;
    void setCaptureColorRange(int value);
    double captureRequestedFps() const;
    void setCaptureRequestedFps(double value);
    int captureAudioIngress() const;
    void setCaptureAudioIngress(int value);
    int captureBufferMode() const;
    void setCaptureBufferMode(int value);
    bool captureQueryBusy() const;
    QString captureStatus() const;
    QVariantMap screenOptions() const;
    // Declared here, in the public section: a Q_INVOKABLE above `public:` is
    // private to QML ("is not a function").
    Q_INVOKABLE void refreshCaptureTargets();
    Q_INVOKABLE void refreshCaptureDevices();
    Q_INVOKABLE bool startCaptureSession();
    Q_INVOKABLE bool setScreenOption(const QString& key, const QVariant& value);
    Q_INVOKABLE bool startScreenCapture();

    QVariantList screenTargets() const;
    QString screenTargetId() const;
    void setScreenTargetId(const QString& value);
    QString screenTargetLabel() const;

    QVariantList subtitleTracks() const;
    int subtitlePrimary() const;
    void setSubtitlePrimary(int index);
    int subtitleSecondary() const;
    void setSubtitleSecondary(int index);
    int subtitleOffsetMs() const;
    void setSubtitleOffsetMs(int ms);
    QString subtitleStatus() const;
    bool subtitleAligning() const;
    QString subtitleText() const;
    int subtitleBottomInset() const;
    void setSubtitleBottomInset(int value);
    Q_INVOKABLE void loadSubtitleDialog();
    Q_INVOKABLE bool loadSubtitleFile(const QString& path);
    Q_INVOKABLE void nudgeSubtitle(int deltaMs);
    Q_INVOKABLE void cycleSubtitle(bool secondary);
    Q_INVOKABLE void toggleSubtitles();
    Q_INVOKABLE void autoAlignSubtitle();

    QVariantMap ps5() const;
    QVariantList ps5Profiles() const;
    QObject* moonlightModel() const;
    bool moonlightCaptured() const;
    bool moonlightStatsVisible() const;
    void setMoonlightStatsVisible(bool visible);
    Q_INVOKABLE void openMoonlightDialog();
    Q_INVOKABLE void moonlightCapture(bool on);
    Q_INVOKABLE void moonlightDisconnect();
    QObject* xboxModel() const;
    Q_INVOKABLE void openXboxDialog();
    Q_INVOKABLE void xboxDisconnect();
    Q_INVOKABLE void ps5Load();
    Q_INVOKABLE bool ps5Set(const QString& key, const QVariant& value);
    Q_INVOKABLE void ps5SelectProfile(const QString& id);
    Q_INVOKABLE void ps5Scan();
    Q_INVOKABLE void ps5Pair(const QString& pin);
    Q_INVOKABLE bool ps5Connect();
    Q_INVOKABLE void ps5Wake();
    Q_INVOKABLE void ps5Forget();
    Q_INVOKABLE void ps5PsnLogin();
    Q_INVOKABLE void ps5PsnComplete();
    Q_INVOKABLE void ps5PsnForget();
    Q_INVOKABLE void ps5SendLoginPin(const QString& pin);
    Q_INVOKABLE void ps5Cancel();
    Q_INVOKABLE void ps5Calibrate();
    QString remotePlayHost() const;
    void setRemotePlayHost(const QString& value);
    QString remotePlayPin() const;
    void setRemotePlayPin(const QString& value);

    QVariantMap preferences() const;
    QString uiLanguage() const;
    // main.cpp hands over the QML engine so a language change retranslates the scene.
    void setQmlEngine(QQmlEngine* engine);
    Q_INVOKABLE void rememberWindowSize(int width, int height);
    Q_INVOKABLE bool setPreference(const QString& key, const QVariant& value);
    // Available area (taskbar excluded) of the screen holding a point, or of the screen the
    // pointer is on at start. QML's Screen.desktopAvailable* span the whole virtual desktop,
    // which centred the window across two of three monitors (field report 2026-10-01).
    Q_INVOKABLE QRect screenAvailableAt(int x, int y) const;
    Q_INVOKABLE QRect launchScreenAvailable() const;
    QString screenshotDirectory() const;
    QString lastScreenshot() const;
    QString dataDirectory() const;
    QString logFile() const;
    Q_INVOKABLE void chooseScreenshotDirectory();
    QVariantMap shortcuts() const;
    Q_INVOKABLE bool setShortcut(const QString& action, const QString& sequence);
    Q_INVOKABLE void resetShortcuts();
    int uiScaleActive() const;
    bool obsGameCaptureActive() const;
    bool overlayCompatActive() const;
    // RivaTuner Statistics Server is running now (gfx::rivaTunerRunning).
    Q_INVOKABLE bool rivaTunerRunning() const;
    Q_INVOKABLE void openFeedbackPage();
    Q_INVOKABLE void openReleasesPage();
    QVariantList audioDevices() const;
    QVariantList gpuMonitorChoices() const;
    QString gpuMonitorName() const;
    QString audioOutputStatus() const;
    bool audioOutputFallback() const;
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void openScreenshotDirectory();
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void openDataFolder();
    QString appName() const;
    QString version() const;

    // --- property accessors (Q_PROPERTY READ targets) -----------------------
    // Split into read-only and read-write so a reviewer can see at a glance
    // which values the UI can command and which it can only display.
    QString statusText() const;
    bool running() const;
    bool paused() const;
    bool failed() const;
    bool isImage() const;
    bool isCapture() const;
    bool hasSource() const;
    bool openingSource() const;
    double position() const;
    double duration() const;
    double progress() const;
    QString positionText() const;
    QString durationText() const;

    QString sourceName() const;
    int thumbnailGeneration() const;
    QString sourceSummary() const;
    int sourceWidth() const;
    int sourceHeight() const;
    double sourceFps() const;
    int sourceRotation() const;
    double sourceAspect() const;
    double previewAspect() const;

    double submitFps() const;
    bool submitFpsKnown() const;
    double lateMs() const;
    double lateP95Ms() const;
    QString metricsSummary() const;
    bool nrActive() const;
    bool srActive() const;
    bool fgActive() const;
    bool captureRecovering() const;
    double captureFps() const;
    double captureDropped() const;
    QString backendWarning() const;

    bool applying() const;
    double volume() const;
    void setVolume(double value);
    bool muted() const;
    void setMuted(bool value);
    double playbackSpeed() const;
    double playbackRate() const;
    void setPlaybackRate(double rate);

    bool nrEnabled() const;
    int selectedNrLayer() const;
    void setSelectedNrLayer(int index);
    void setNrEnabled(bool value);
    bool srEnabled() const;
    void setSrEnabled(bool value);
    int fgMultiplier() const;
    void setFgMultiplier(int value);
    bool lowLatency() const;
    void setLowLatency(bool value);
    bool nrTemporal() const;
    void setNrTemporal(bool value);
    // Stage-5 output stabiliser (anti-flicker). Global rather than per-node:
    // it runs once, after the whole NR stack and the residual composite.
    double nrHoldStrength() const;
    void setNrHoldStrength(double value);
    double nrHoldTolerance() const;
    void setNrHoldTolerance(double value);
    bool videoHdr() const;
    void setVideoHdr(bool value);
    QVariantMap videoHdrParams() const;
    Q_INVOKABLE bool setVideoHdrParameter(const QString& key, double value);
    int nrStyle() const;
    void setNrStyle(int value);
    double nrIntensity() const;
    void setNrIntensity(double value);
    double nrTone() const;
    void setNrTone(double value);
    double nrStructure() const;
    void setNrStructure(double value);
    double nrSkin() const;
    void setNrSkin(double value);
    bool nrAutoMask() const;
    void setNrAutoMask(bool value);
    bool nrUiCorrection() const;
    void setNrUiCorrection(bool value);
    int videoSrQuality() const;
    void setVideoSrQuality(int value);
    bool protectionEnabled() const;
    void setProtectionEnabled(bool value);
    QVariantMap protectionState() const;
    Q_INVOKABLE bool setProtectionRegion(double left, double top, double right, double bottom, double feather);
    bool colorEnabled() const;
    void setColorEnabled(bool value);
    int audioOffsetMs() const;
    void setAudioOffsetMs(int value);
    int audioSyncMode() const;
    void setAudioSyncMode(int value);
    bool audioSyncLive() const;
    double audioCompensationMs() const;
    double gpuUtilization() const;
    bool gpuUtilizationKnown() const;
    QString runStatus() const;
    QString runStatusLevel() const;
    QString runStatusDetail() const;
    double outputRateRatio() const;
    QVariantList frameTimes() const;
    int flowQuality() const;
    int hdrOutputMode() const;
    void setHdrOutputMode(int value);
    int fgMotionSource() const;
    void setFgMotionSource(int value);
    int srMotionSource() const;
    void setSrMotionSource(int value);
    int nrMotionSource() const;
    void setNrMotionSource(int value);
    void setFlowQuality(int value);
    bool amdFlowHalf() const;
    void setAmdFlowHalf(bool value);
    int contentRate() const;
    void setContentRate(int value);
    int displaySync() const;
    void setDisplaySync(int value);
    Q_INVOKABLE void setPresentationFullscreen(bool value);
    bool fullscreenMemorySafe() const;
    int outputRateMode() const;
    void setOutputRateMode(int value);
    double outputCustomFps() const;
    void setOutputCustomFps(double value);
    bool presentationOwned() const;
    QString presentationStatus() const;
    int compareMode() const;
    void setCompareMode(int value);
    bool compareBase() const;
    void setCompareBase(bool value);
    double compareSplit() const;
    int aspectMode() const;
    void setAspectMode(int value);
    double audioSkewMs() const;
    bool audioSkewKnown() const;
    QString exportSizeEstimate() const;
    QVariantMap lastSource() const;
    QString sourceKind() const;
    QString sourceTitle() const;
    QString sourceFormatText() const;
    QString sourceRateText() const;
    bool colourCanUndo() const;
    bool colourCanRedo() const;
    bool colourCanPaste() const;
    Q_INVOKABLE bool colourUndo();
    Q_INVOKABLE bool colourRedo();
    Q_INVOKABLE void colourCopy();
    Q_INVOKABLE bool colourPaste();
    QVariantList exportItems() const;
    QObject* exportQueueModel() const;
    int exportContainer() const;void setExportContainer(int value);
    int exportReadyCount() const;
    QVariantList exportTracks() const;
    int exportAudioPolicy() const;int exportSubtitlePolicy() const;
    bool exportSelectionEditable() const;
    bool exportCompletionSound() const;void setExportCompletionSound(bool enabled);
    Q_INVOKABLE void addExportFiles(const QStringList& paths);
    Q_INVOKABLE void addCurrentExportFile();
    Q_INVOKABLE void previewExportItem(qulonglong id);
    Q_INVOKABLE void setExportTrackPolicy(bool audio,int policy);
    Q_INVOKABLE void toggleExportTrack(bool audio,int index,bool keep);
    Q_INVOKABLE void clearFinishedExportItems();
    Q_INVOKABLE QString fileThumbnailId(const QString& path, double seconds) const;
    QString capturePreviewUrl() const;
    QString captureSignalText() const;
    QString captureSignalLevel() const;
    QString liveOpeningText() const;
    Q_INVOKABLE void importPresetDialog();
    Q_INVOKABLE bool importPreset(const QString& path);
    Q_INVOKABLE void exportPresetDialog(int index);
    Q_INVOKABLE bool exportPreset(int index, const QString& path);
    // Split position from a pointer on the picture host (fractions of the host).
    Q_INVOKABLE void setCompareSplitAt(double hostFractionX);
    Q_INVOKABLE void resumeLastSource();
    void autoResumeLastSource();
    // List mode: an NR layer's model and residual back to their defaults.
    Q_INVOKABLE bool resetNrLayer(int index);
    QVariantList protectionRegions() const;
    QString protectionDrawShape() const;
    // Drawing on the picture: arm with "rect"/"ellipse" ("" cancels), then
    // report the drag in picture-host pixels; commit adds the region.
    Q_INVOKABLE bool beginProtectionDraw(const QString& shape);
    Q_INVOKABLE void updateProtectionDraw(double x0, double y0, double x1, double y1);
    Q_INVOKABLE bool commitProtectionDraw(double x0, double y0, double x1, double y1);
    Q_INVOKABLE bool removeProtectionRegion(int index);
    Q_INVOKABLE bool clearProtectionRegions();
    Q_INVOKABLE bool setProtectionFeather(double pixels);

    QVariantList chain() const;
    QVariantList nodeConnections() const;
    QVariantList nrLayers() const;
    Q_INVOKABLE bool setNrLayerParameter(int index, const QString& key, double value);
    QVariantList effectCatalog() const;
    QString chainError() const;
    bool chainValid() const;
    int nodeMode() const;
    void setNodeMode(int mode);

    QVariantList recentFiles() const;
    QVariantList presets() const;
    QString currentPresetName() const;
    QVariantList audioTracks() const;
    int selectedAudioTrack() const;
    void setSelectedAudioTrack(int index);
    bool hasCaptureSession() const;
    QString captureSessionSummary() const;
    QString colorStatus() const;
    QString videoHdrStatus() const;

    bool exportRunning() const;
    bool exportPaused() const;
    double exportProgress() const;
    QString exportStatus() const;
    QString exportTarget() const;
    int exportEncoded() const;
    int exportGenerated() const;
    bool exportHevc() const;
    void setExportHevc(bool value);
    int exportBitrateMbps() const;
    void setExportBitrateMbps(int value);
    int exportRateControl() const;
    void setExportRateControl(int value);
    double exportEtaSeconds() const;
    int exportQueueCount() const;
    int exportQueueFailures() const;
    bool imageBatchActive() const;
    int imageBatchDone() const;
    int imageBatchTotal() const;
    int imageBatchFailures() const;
    QString imageBatchStatus() const;
    // The processed frame on screen, saved as PNG/JPEG (JXR for HDR output).
    Q_INVOKABLE void saveFrameDialog();
    Q_INVOKABLE void exportImagesDialog();
    Q_INVOKABLE bool startImageBatch(const QStringList& files, const QString& folder);
    Q_INVOKABLE void cancelImageBatch();
    QString exportQueueFailure() const;
    QString exportPresetName() const;
    int exportSrTargetIndex() const;
    void setExportSrTargetIndex(int index);
    double exportTrimStart() const;
    void setExportTrimStart(double seconds);
    double exportTrimEnd() const;
    void setExportTrimEnd(double seconds);
    int srTargetIndex() const;
    QString srTargetLabel() const;
    void setSrTargetIndex(int index);
    QVariantList presetChoices() const;
    QVariantList presetSaveParts() const;
    int defaultPresetIndex() const;
    QVariantList colourParameters() const;
    QVariantList colourGroups() const;
    int selectedColourLayer() const;
    void setSelectedColourLayer(int index);
    QVariantMap colourState() const;
    Q_INVOKABLE double colourParameter(const QString& name) const;
    Q_INVOKABLE bool setColourParameter(const QString& name, double value);
    Q_INVOKABLE bool resetColourParameter(const QString& name);
    Q_INVOKABLE bool setColourCurve(int channel, const QVariantList& points);
    Q_INVOKABLE bool setColourWheel(int zone, double hue, double saturation, double luminance);
    Q_INVOKABLE bool setColourOption(const QString& name, int value);
    Q_INVOKABLE bool resetColourGroup(int group);
    QVariantList lutLibrary() const;
    QVariantList colourLooks() const;
    Q_INVOKABLE void importLutDialog();
    Q_INVOKABLE bool importLut(const QString& path);
    // Empty name clears the LUT from the selected Color node.
    Q_INVOKABLE bool setColourLut(const QString& name);
    Q_INVOKABLE bool saveColourLook(const QString& name, bool replace);
    Q_INVOKABLE bool applyColourLook(int index);
    Q_INVOKABLE bool deleteColourLook(int index);
    Q_INVOKABLE void importColourLookDialog();
    Q_INVOKABLE bool importColourLook(const QString& path);
    Q_INVOKABLE void exportColourLookDialog(int index);
    Q_INVOKABLE bool exportColourLook(int index, const QString& path);

    // The native HWND the engine presents into. The UI creates it as a child
    // window of the QML window and hands it over; the bridge never draws QML
    // into it and Qt never touches its swapchain.
    void attachVideoWindow(qulonglong nativeHandle);

    // Run immediately before the engine opens a source. The presenter reads the
    // host window's client size once, when it initialises, and builds the
    // swapchain from it; a window that is still 0x0 at that moment yields a 1x1
    // surface that never recovers. The UI supplies this hook so the geometry is
    // settled at the one moment that matters, instead of racing a timer.
    void setPreOpenHook(std::function<void()> hook);

    // --- commands exposed to QML -------------------------------------------
    // Every one returns immediately; none touches the GPU or waits for the
    // engine.
    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openCaptureDialog();
    // Reopens the last capture session with the same device and format.
    Q_INVOKABLE void resumeCaptureSession();
    Q_INVOKABLE void openPs5Dialog();
    Q_INVOKABLE void openScreenCaptureDialog();
    Q_INVOKABLE void openAudioDialog();
    Q_INVOKABLE void openSubtitleDialog();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void seekBy(double seconds);
    // V held down: the engine shows the unprocessed frame until release
    // (AppShell holdOriginal -> engine.comparison(1, false)).
    Q_INVOKABLE void holdOriginal(bool held);
    // A dropped file (AppShell WM_DROPFILES): a local file URL goes to openPath.
    Q_INVOKABLE void openUrl(const QUrl& url);
    // UI events the shell logs (fullscreen, lock) under the given channel.
    Q_INVOKABLE void logUi(const QString& channel, const QString& text);
    // Limits a window's pointer input to a rectangle (QWindow::setMask); the
    // fullscreen bar window uses it so only its pill takes the pointer.
    Q_INVOKABLE void setWindowMask(QObject* window, const QRectF& rect);
    Q_INVOKABLE void stepFrame(int direction);
    Q_INVOKABLE void takeScreenshot();
    Q_INVOKABLE void chooseExportPath();
    // The export target without the dialog (the dialog's result, or --export-out in a test).
    void setExportPath(const QString& path);
    Q_INVOKABLE void startExport();
    Q_INVOKABLE void enqueueExportFile(const QString& input, const QString& output);
    Q_INVOKABLE void addExportFilesDialog();
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void pauseExport(bool paused);
    Q_INVOKABLE void quit();
    Q_INVOKABLE void restartApplication();
    Q_INVOKABLE void restartForOverlayCompatibility();
    // Returns this page's controls to the engine defaults (the design's
    // "重置本页"); it does not touch presets or other pages.
    Q_INVOKABLE void resetCurrentPage();

    // Chain editing. The validator decides whether an edit is allowed where it
    // was asked (frame generation is pinned last, RTX Video HDR immediately
    // before it) and reports why when it is not.
    Q_INVOKABLE int addEffect(const QString& type);
    Q_INVOKABLE int duplicateNrLayer(int index);
    Q_INVOKABLE int nodeIndexForId(uint id) const;
    Q_INVOKABLE int duplicateNode(uint id);
    Q_INVOKABLE bool resetNode(uint id);
    Q_INVOKABLE bool connectNodes(uint from, uint to);
    Q_INVOKABLE bool disconnectNode(uint from);
    Q_INVOKABLE bool insertNodeAfter(uint id, uint after);
    Q_INVOKABLE bool removeEffect(int index);
    Q_INVOKABLE bool moveEffect(int from, int to);
    Q_INVOKABLE bool setEffectEnabled(int index, bool enabled);
    // Every NR layer off at once; on again restores the layers that were on.
    Q_INVOKABLE bool setAllNrEnabled(bool enabled);
    bool nrAnyEnabled() const;
    // Node-mode positions are UI state and persist with the chain.
    Q_INVOKABLE bool setEffectPosition(int index, double x, double y);
    Q_INVOKABLE QVariantMap effectDescriptor(const QString& type) const;

    // Presets. A preset carries selected parts, so there is one preset concept,
    // not several.
    Q_INVOKABLE bool applyPresetIndex(int index);
    Q_INVOKABLE bool selectExportPreset(int index);
    Q_INVOKABLE bool savePresetAs(const QString& name, int contentsMask, bool nodeMode);
    Q_INVOKABLE bool deletePreset(int index);
    Q_INVOKABLE bool renamePreset(int index, const QString& name);
    Q_INVOKABLE bool duplicatePreset(int index);
    Q_INVOKABLE bool setDefaultPreset(int index);

    Q_INVOKABLE void refreshRecentFiles();
    Q_INVOKABLE void clearRecentFiles();
    Q_INVOKABLE QString formatTime(double seconds) const;

    // Diagnostics for the settings page: what the engine actually reports,
    // never a summary written by hand.
    Q_INVOKABLE QString diagnosticsReport() const;
    Q_INVOKABLE void openProjectPage();
    Q_INVOKABLE void copyDiagnostics();

signals:
    void nodeAnchorsChanged();
    void ps5Changed();
    void moonlightUiChanged();
    void imageBatchChanged();
    void audioDevicesChanged();
    void subtitlesChanged();
    void subtitleTextChanged();
    void preferencesChanged();
    void colourLibraryChanged();
    // Emitted only when the underlying snapshot actually changed, so QML
    // bindings do not re-evaluate 60 times a second for nothing.
    void snapshotChanged();
    void perfChanged();
    void protectionDrawChanged();
    void presentationChanged();
    void compareChanged();
    void thumbnailGenerationChanged();
    void thumbnailSourceChanged(const QString& path);
    void settingsChanged();
    void fgChoicesChanged();
    void chainChanged();
    void recentFilesChanged();
    void presetsChanged();
    void exportChanged();
    // Emitted when the capture device or screen-target lists are re-enumerated.
    void captureChanged();
    // User-facing notices the UI shows as a transient message: a rejected
    // chain edit, an export that finished, a failed open.
    void notice(const QString& text, bool isError);
    // RivaTuner injected into a run whose interface draws on the GPU: its OSD is kept out
    // of this process until a restart switches the interface to software drawing.
    void overlayRestartSuggested();
    // The UI moves to a page by name when a command implies it.
    void navigate(const QString& page);

private:
    // The settings the UI is showing: the facade's pending copy, which the
    // engine has been asked to apply. Every settings property reads this, so
    // they can never disagree with each other.
    engine::EnhancementSettings settings() const;
    void startGpuSampler();
    void requestApplicationRestart(bool overlayCompatibility);
    void updateRunStatus();
    void applyPresentation();
    void applyAspect(bool force);
    // Live sources started from the start page: switch to 极简 first, show the
    // connecting note, and start the engine once the page and window have settled.
    void openAfterCinema(const QString& pending, std::function<void()> open);
    void rememberSource(const QString& kind, const QString& label);
    // Edits the list chain's protection node; rolls back when refused.
    bool editProtection(const std::function<bool(engine::ChainNode&)>& edit);
    engine::ColorSettings selectedColourSettings() const;
    bool commitColour(const engine::ColorSettings& colour);
    // Reads the export job's snapshot and emits exportChanged.
    void pollExport();
    // Pushes a stored preference to whatever consumes it (subtitle overlay,
    // audio endpoint). Called after load and after every accepted change.
    void applyPreference(const QString& key);
    // Hold-to-compare key (press/release), handled for the whole application.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rememberPosition(bool flush);
    void refreshSubtitles(const std::wstring& media);
    void tickSubtitles();
    void tickImageBatch();
    void tickCapture();
    void tickPs5();
    void tickMoonlight();
    void tickXbox();
#ifdef VEYRA_ENABLE_XBOX
    void setupXbox();
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    void setupMoonlight();
#endif
    void queryCapture(int kind, std::wstring device);
    // Opens a device/stream URI (capture, screen) the way openPath opens a file.
    void openSourceUri(const std::wstring& uri, const std::wstring& label);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace veyra::ui
