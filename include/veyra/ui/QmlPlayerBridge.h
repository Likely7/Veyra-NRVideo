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

// 单次 B 站请求的操作种类。用枚举而不是字符串：拼错的枚举编译不过，
// 拼错的字符串会静默地永远匹配不上。
// The kind of one-shot Bilibili request. An enum rather than a string on purpose: a
// typo in an enum fails to compile, a typo in a string silently never matches.
enum class BiliOp : uint8_t {
    InteractionState, Favorite, FavFolders,
    Like, Coin, WatchLater,
    Comments, EnableDanmaku, ResumeAtCid,
    QrStart, QrPoll, Account,
};
// 操作名，供信号传给 QML 分派用。
// The action name, passed to QML so one handler can serve several actions.
constexpr const char* biliOpName(BiliOp op) {
    switch (op) {
    case BiliOp::InteractionState: return "interaction";
    case BiliOp::Favorite:         return "favorite";
    case BiliOp::FavFolders:       return "fav-folders";
    case BiliOp::Like:             return "like";
    case BiliOp::Coin:             return "coin";
    case BiliOp::WatchLater:       return "watchlater";
    case BiliOp::Comments:         return "comments";
    case BiliOp::EnableDanmaku:    return "enable-danmaku";
    case BiliOp::ResumeAtCid:      return "resume-at-cid";
    case BiliOp::QrStart:          return "qr-start";
    case BiliOp::QrPoll:           return "qr-poll";
    case BiliOp::Account:          return "account";
    }
    return "unknown";
}
// 一次异步任务的结果信封。worker 只填这里，GUI tick 取走后发信号。
// The result envelope of one async task: the worker only fills this, and the GUI tick
// reads it and emits the signal.
struct BiliTaskResult {
    BiliOp op = BiliOp::Account;
    bool ok = false;
    QString error;
    QVariantMap object;      // account / qr-start / interaction 这类单对象
    QVariantList list;       // comments / fav-folders 这类列表
    double number = -1.0;    // resume-at-cid 的秒数 / 弹幕条数
    QString text;            // qr-start 的地址、qr-poll 的状态
    // 三个方法需要回带自己的输入，才能把结果路由回正确的调用点。
    // Three methods carry their own inputs back so the reply reaches the right caller.
    QString bvid;
    int generation = 0;
    QString reason;          // fav-folders / favorite 区分调用点
    bool add = false;        // favorite 的方向
};

class QmlPlayerBridge : public QObject {
    Q_OBJECT
    // 私有实现。提前声明，好让下面的成员函数签名能用它的嵌套类型。
    // The private implementation, declared up front so member signatures below can
    // name its nested types.
    struct Impl;
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
    Q_PROPERTY(QVariantList fgBackendChoices READ fgBackendChoices NOTIFY effectCapabilitiesChanged)
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
    // Counts interface graphics devices recreated after a loss (a GPU reset, logs 56). VIcon
    // re-feeds its path when it changes: Qt 6.8.3's curve renderer leaves recreated nodes empty.
    Q_PROPERTY(int uiDeviceGeneration READ uiDeviceGeneration NOTIFY uiDeviceGenerationChanged)
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
    Q_PROPERTY(QVariantList componentList READ componentList NOTIFY componentListChanged)
    Q_PROPERTY(QObject* updates READ updates CONSTANT)

    // --- capture dialog ----------------------------------------------------
    // Device and format lists come from the source's own enumeration, so the UI
    // can only offer what the machine actually reports.
    Q_PROPERTY(QVariantList captureDevices READ captureDevices NOTIFY captureChanged)
    Q_PROPERTY(QString captureDeviceId READ captureDeviceId WRITE setCaptureDeviceId NOTIFY captureChanged)
    Q_PROPERTY(QString captureDeviceLabel READ captureDeviceLabel NOTIFY captureChanged)
    Q_PROPERTY(bool captureBlackmagicDevice READ captureBlackmagicDevice NOTIFY captureChanged)
    Q_PROPERTY(bool captureForceSdr READ captureForceSdr WRITE setCaptureForceSdr NOTIFY settingsChanged)
    Q_PROPERTY(bool amdNrGpu READ amdNrGpu CONSTANT)
    Q_PROPERTY(QVariantMap effectCapabilities READ effectCapabilities NOTIFY effectCapabilitiesChanged)
    Q_PROPERTY(QVariantList nrRuntimeChoices READ nrRuntimeChoices NOTIFY effectCapabilitiesChanged)
    Q_PROPERTY(QVariantList srBackendChoices READ srBackendChoices NOTIFY effectCapabilitiesChanged)
    Q_PROPERTY(bool captureFlipVertical READ captureFlipVertical WRITE setCaptureFlipVertical NOTIFY settingsChanged)
    // P4-e: the full capture connection, as the 1.4.4 panel: device details and
    // formats come from asynchronous DirectShow queries (never on the UI thread),
    // the choice is remembered in capture-preferences.v1 for "continue".
    Q_PROPERTY(QVariantList captureFormats READ captureFormats NOTIFY captureChanged)
    Q_PROPERTY(QString captureFormatKey READ captureFormatKey WRITE setCaptureFormatKey NOTIFY captureChanged)
    // The selected format's label ("" when none) and whether it gets the cost hint, so the
    // dialog never searches captureFormats itself (logs(18): 15474 formats hung it).
    Q_PROPERTY(QString captureFormatLabel READ captureFormatLabel NOTIFY captureChanged)
    Q_PROPERTY(bool captureFormatCostHint READ captureFormatCostHint NOTIFY captureChanged)
    // Resolution, frame rate and pixel format are three views of captureFormatKey.
    // Changing one retargets that key to a media type the device actually enumerated.
    Q_PROPERTY(QVariantList captureResolutionOptions READ captureResolutionOptions NOTIFY captureChanged)
    Q_PROPERTY(QString captureResolutionId READ captureResolutionId WRITE setCaptureResolutionId NOTIFY captureChanged)
    Q_PROPERTY(QVariantList captureFrameRateOptions READ captureFrameRateOptions NOTIFY captureChanged)
    // Rates the frame-rate menu leaves out unless preferences.captureShowAllFrameRates is on
    // (0: the card offers nothing beyond the common rates, so the switch is not shown).
    Q_PROPERTY(int captureHiddenFrameRates READ captureHiddenFrameRates NOTIFY captureChanged)
    Q_PROPERTY(QString captureFrameRateId READ captureFrameRateId WRITE setCaptureFrameRateId NOTIFY captureChanged)
    Q_PROPERTY(QVariantList capturePixelOptions READ capturePixelOptions NOTIFY captureChanged)
    Q_PROPERTY(QString capturePixelId READ capturePixelId WRITE setCapturePixelId NOTIFY captureChanged)
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
    // 游戏模式 (screen option "passthrough", ui/GamePassthrough.h): gameMode while the
    // current source is such a capture (opening or live); passthroughActive while the
    // fullscreen picture is click-through and the captured program has the input.
    Q_PROPERTY(bool gameMode READ gameMode NOTIFY gameModeChanged)
    Q_PROPERTY(bool passthroughActive READ passthroughActive NOTIFY gameModeChanged)
    Q_PROPERTY(bool gameInputActive READ gameInputActive NOTIFY gameModeChanged)
    Q_PROPERTY(bool gameScaledPreview READ gameScaledPreview NOTIFY gameModeChanged)
    Q_PROPERTY(QRectF gameVideoRect READ gameVideoRect NOTIFY gameModeChanged)

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
    // PS5 手柄控制 (采集卡对话框): a controller-only Remote Play session beside the
    // capture card, using the pairing selected in the PS5 dialog. {available,
    // enabled, background, state: off|starting|connected|pin|recovering|failed|
    // nopair, message, keepaliveFps (-1 = not yet), pad, gyro, touch}
    Q_PROPERTY(QVariantMap ps5Control READ ps5Control NOTIFY ps5ControlChanged)
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
    Q_PROPERTY(QString gpuPriorityStatus READ gpuPriorityStatus NOTIFY preferencesChanged)
    Q_PROPERTY(bool enhancementPrewarmAvailable READ enhancementPrewarmAvailable CONSTANT)
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
    // 设置 -> "同时用这块显卡运算" (preference computeOnMonitorGpu): the GPU picked
    // for monitoring also runs enhancement, frame generation and export.
    Q_PROPERTY(QString computeGpuHint READ computeGpuHint NOTIFY preferencesChanged)
    Q_PROPERTY(QString audioOutputStatus READ audioOutputStatus NOTIFY audioDevicesChanged)
    Q_PROPERTY(bool audioOutputFallback READ audioOutputFallback NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

    // --- playback ----------------------------------------------------------
    Q_PROPERTY(bool deinterlaceAvailable READ deinterlaceAvailable CONSTANT)
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
    Q_PROPERTY(bool nrAutoActive READ nrAutoActive NOTIFY snapshotChanged)
    Q_PROPERTY(int nrAutoPercent READ nrAutoPercent NOTIFY snapshotChanged)
    Q_PROPERTY(QString nrAutoStatus READ nrAutoStatus NOTIFY snapshotChanged)
    Q_PROPERTY(bool srActive READ srActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool fgActive READ fgActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool captureRecovering READ captureRecovering NOTIFY snapshotChanged)
    Q_PROPERTY(double captureFps READ captureFps NOTIFY snapshotChanged)
    Q_PROPERTY(double captureDropped READ captureDropped NOTIFY snapshotChanged)
    Q_PROPERTY(QString backendWarning READ backendWarning NOTIFY snapshotChanged)

    // --- settings (what the UI shows) --------------------------------------
    Q_PROPERTY(bool nrEnabled READ nrEnabled WRITE setNrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int selectedNrLayer READ selectedNrLayer WRITE setSelectedNrLayer NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList nrLayers READ nrLayers NOTIFY nrLayersChanged)
    // List mode's NR master switch: true while any NR layer is on.
    Q_PROPERTY(bool nrAnyEnabled READ nrAnyEnabled NOTIFY nrLayersChanged)
    Q_PROPERTY(bool srEnabled READ srEnabled WRITE setSrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int fgMultiplier READ fgMultiplier WRITE setFgMultiplier NOTIFY settingsChanged)
    // 自动选择倍率 开关：只对 DLSS 和 VFG 开放。开启后按源帧率 × 输出上限自动挑档，
    // 源帧率超过上限时关闭补帧。与倍率档位完全分离（2026-10-24 用户要求）。
    Q_PROPERTY(bool fgAutoMultiplierEnabled READ fgAutoMultiplier WRITE setFgAutoMultiplier NOTIFY settingsChanged)
    Q_PROPERTY(bool fgAutoMultiplierAvailable READ fgAutoMultiplierAvailable NOTIFY settingsChanged)
    Q_PROPERTY(bool lowLatency READ lowLatency WRITE setLowLatency NOTIFY settingsChanged)
    Q_PROPERTY(bool nrTemporal READ nrTemporal WRITE setNrTemporal NOTIFY settingsChanged)
    Q_PROPERTY(double nrHoldStrength READ nrHoldStrength WRITE setNrHoldStrength NOTIFY settingsChanged)
    Q_PROPERTY(double nrHoldTolerance READ nrHoldTolerance WRITE setNrHoldTolerance NOTIFY settingsChanged)
    Q_PROPERTY(bool videoHdr READ videoHdr WRITE setVideoHdr NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap videoHdrParams READ videoHdrParams NOTIFY videoHdrParamsChanged)
    // Spatial anti-aliasing (#53): the list card edits the first instance, created
    // on first use; node cards edit their own node by chain index.
    // Beauty nodes of the active chain in chain order (list mode: the group's layers).
    Q_PROPERTY(QVariantList beautyLayers READ beautyLayers NOTIFY beautyChanged)
    // The chain's shared face detection: quality, maxFaces, stability.
    Q_PROPERTY(QVariantMap beautyDetection READ beautyDetection NOTIFY beautyChanged)
    Q_PROPERTY(QString beautyStatus READ beautyStatus NOTIFY snapshotChanged)
    Q_PROPERTY(bool beautyAnyEnabled READ beautyAnyEnabled NOTIFY beautyChanged)
    Q_PROPERTY(bool antiAliasEnabled READ antiAliasEnabled WRITE setAntiAliasEnabled NOTIFY antiAliasChanged)
    Q_PROPERTY(QVariantMap antiAliasParams READ antiAliasParams NOTIFY antiAliasChanged)
    // Custom: static HDR output tuning (globals, every field live).
    Q_PROPERTY(bool hdrCurve READ hdrCurve WRITE setHdrCurve NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap hdrCurveParams READ hdrCurveParams NOTIFY settingsChanged)
    // Custom: follow the Dolby Vision RPU level 1 for the curve's highlight start.
    Q_PROPERTY(bool doviFollow READ doviFollow WRITE setDoviFollow NOTIFY settingsChanged)
    Q_PROPERTY(bool doviP7Compose READ doviP7Compose WRITE setDoviP7Compose NOTIFY settingsChanged)
    Q_PROPERTY(bool doviFollowAverage READ doviFollowAverage WRITE setDoviFollowAverage NOTIFY settingsChanged)
    // One-click tuning presets (see hdrTuningPresets); "custom" when the sliders
    // no longer match any of them.
    Q_PROPERTY(QString hdrTuningPreset READ hdrTuningPreset NOTIFY settingsChanged)
    // One dial for the whole curve (0..200%, 100 = the preset as written) and the
    // display's own peak brightness (0 = ask the system). Both live next to the
    // baseline, so moving them never leaves the selected preset.
    Q_PROPERTY(int hdrCurveStrength READ hdrCurveStrength WRITE setHdrCurveStrength NOTIFY settingsChanged)
    Q_PROPERTY(int hdrDisplayPeakNits READ hdrDisplayPeakNits WRITE setHdrDisplayPeakNits NOTIFY settingsChanged)
    // What the system reports about the display, for the peak field's reset button.
    Q_PROPERTY(QVariantMap hdrDisplayInfo READ hdrDisplayInfo NOTIFY snapshotChanged)
    // Custom: the source's own HDR numbers (container metadata), the one-click
    // recommendation derived from them, and the warning that compares the film's
    // peak with what the curve will actually cap.
    Q_PROPERTY(QVariantMap hdrSourceInfo READ hdrSourceInfo NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap hdrRecommendation READ hdrRecommendation NOTIFY snapshotChanged)
    // Custom: whether the running source is SDR. Used only to word the recommendation
    // row (an SDR source has no HDR metadata, so "no metadata" would be misleading).
    Q_PROPERTY(bool sourceIsSdr READ sourceIsSdr NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap hdrPeakWarning READ hdrPeakWarning NOTIFY snapshotChanged)
    // Custom: one remembered curve per display device, so moving the window to the
    // projector or the TV applies the settings tuned on that device.
    Q_PROPERTY(int hdrDisplayProfileCount READ hdrDisplayProfileCount NOTIFY preferencesChanged)
    // Custom: which preset system the panel shows. "legacy" is the original seven
    // presets, "axes" is content x environment. A UI choice kept in preferences:
    // both modes write the very same curve, so the saved session is unaffected.
    Q_PROPERTY(QString hdrPresetMode READ hdrPresetMode WRITE setHdrPresetMode NOTIFY preferencesChanged)

    // Which preset system established the curve in force. Kept apart from hdrPresetMode
    // on purpose: switching the panel view must not let the other system claim the curve,
    // so only an actual apply moves this.
    Q_PROPERTY(QString hdrAppliedFrom READ hdrAppliedFrom WRITE setHdrAppliedFrom NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap hdrAxes READ hdrAxes NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap hdrAxesMatch READ hdrAxesMatch NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap hdrDials READ hdrDials NOTIFY settingsChanged)
    // Custom: the curve sampled by the same function the shader runs, for the panel
    // graph (see hdrCurveLuminance).
    Q_PROPERTY(QVariantMap hdrCurveGraph READ hdrCurveGraph NOTIFY settingsChanged)
    // Custom: the live coverage measured by the presenter's probe (see the panel's
    // live meter). Invalid until a probe has completed.
    Q_PROPERTY(QVariantMap hdrCurveCoverage READ hdrCurveCoverage NOTIFY snapshotChanged)
    Q_PROPERTY(bool hdrTitleMemory READ hdrTitleMemory WRITE setHdrTitleMemory NOTIFY preferencesChanged)
    Q_PROPERTY(bool hdrAutoRecommend READ hdrAutoRecommend WRITE setHdrAutoRecommend NOTIFY preferencesChanged)
    Q_PROPERTY(int hdrTitleCount READ hdrTitleCount NOTIFY preferencesChanged)
    Q_PROPERTY(int nrStyle READ nrStyle WRITE setNrStyle NOTIFY settingsChanged)
    Q_PROPERTY(double nrIntensity READ nrIntensity WRITE setNrIntensity NOTIFY settingsChanged)
    Q_PROPERTY(double nrTone READ nrTone WRITE setNrTone NOTIFY settingsChanged)
    Q_PROPERTY(double nrStructure READ nrStructure WRITE setNrStructure NOTIFY settingsChanged)
    Q_PROPERTY(double nrSkin READ nrSkin WRITE setNrSkin NOTIFY settingsChanged)
    Q_PROPERTY(bool nrAutoMask READ nrAutoMask WRITE setNrAutoMask NOTIFY settingsChanged)
    Q_PROPERTY(bool nrUiCorrection READ nrUiCorrection WRITE setNrUiCorrection NOTIFY settingsChanged)
    Q_PROPERTY(int videoSrQuality READ videoSrQuality WRITE setVideoSrQuality NOTIFY settingsChanged)
    Q_PROPERTY(int dlssSrModel READ dlssSrModel WRITE setDlssSrModel NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList dlssSrModelChoices READ dlssSrModelChoices CONSTANT)
    Q_PROPERTY(bool protectionEnabled READ protectionEnabled WRITE setProtectionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap protectionState READ protectionState NOTIFY protectionChanged)
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
    // Live FG output hint ("当前帧率…超出屏幕刷新率，建议降至…"), empty when the
    // output fits; a suggestion only, the multiplier is never changed.
    Q_PROPERTY(QString fgDisplayHint READ fgDisplayHint NOTIFY perfChanged)
    Q_PROPERTY(QStringList runOptions READ runOptions NOTIFY perfChanged)
    Q_PROPERTY(double effectiveOutputFps READ effectiveOutputFps NOTIFY perfChanged)
    // Achieved output rate / effective policy target; -1 while unknown.
    Q_PROPERTY(double outputRateRatio READ outputRateRatio NOTIFY perfChanged)
    // Frame interval history (ms, newest last) for the frame-time line.
    Q_PROPERTY(QVariantList frameTimes READ frameTimes NOTIFY perfChanged)
    // 补帧 / 光流 (design gap A1-A5): motion estimation quality 0 性能 1 平衡 2 质量,
    // AMD half-resolution flow, content cadence (engine::ContentRate order).
    Q_PROPERTY(int flowQuality READ flowQuality WRITE setFlowQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool amdFlowHalf READ amdFlowHalf WRITE setAmdFlowHalf NOTIFY settingsChanged)
    // 边缘神盾: refined optical-flow motion for XeSS (EnhancementSettings::fgMotionRefine).
    Q_PROPERTY(bool edgeAegis READ edgeAegis WRITE setEdgeAegis NOTIFY settingsChanged)
    // DLSS 性能模式: NVIDIA's DLSS-G performance mode (EnhancementSettings::dlssgLite);
    // available when the package carries its runtime (runtime/experimental/dlssg-lite).
    Q_PROPERTY(bool dlssgLite READ dlssgLite WRITE setDlssgLite NOTIFY settingsChanged)
    Q_PROPERTY(bool dlssgLiteAvailable READ dlssgLiteAvailable CONSTANT)
    Q_PROPERTY(QString dlssgLiteHint READ dlssgLiteHint NOTIFY snapshotChanged)
    Q_PROPERTY(int contentRate READ contentRate WRITE setContentRate NOTIFY settingsChanged)
    Q_PROPERTY(int hdrOutputMode READ hdrOutputMode WRITE setHdrOutputMode NOTIFY settingsChanged)
    Q_PROPERTY(int fgMotionSource READ fgMotionSource WRITE setFgMotionSource NOTIFY settingsChanged)
    Q_PROPERTY(int srMotionSource READ srMotionSource WRITE setSrMotionSource NOTIFY settingsChanged)
    Q_PROPERTY(int nrMotionSource READ nrMotionSource WRITE setNrMotionSource NOTIFY settingsChanged)
    // Presentation: display sync 0 允许撕裂 1 垂直同步 2 自动; output cap 0 关闭
    // 1 跟随显示器 2 自定义 (+ fps). Owned by XeSS when presentationOwned.
    Q_PROPERTY(int displaySync READ displaySync WRITE setDisplaySync NOTIFY presentationChanged)
    // 可变刷新 switch (persisted with the presentation settings) and the
    // presenting monitor's VRR level: 0 unknown, 1 the monitor cannot do VRR
    // (switch greyed), 2 it can but the driver setting is off, 3 possible,
    // 4 active; vrrHint explains it beside the switch.
    Q_PROPERTY(bool variableRefresh READ variableRefresh WRITE setVariableRefresh NOTIFY presentationChanged)
    Q_PROPERTY(int vrrState READ vrrState NOTIFY perfChanged)
    Q_PROPERTY(QString vrrHint READ vrrHint NOTIFY perfChanged)
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
    // Network / server playback (#48): the signal-panel line ("缓冲 18.2 s · 下载 12.4 MB/s · 重连 0"),
    // whether playback is paused to buffer, and the reason text shown while it is.
    Q_PROPERTY(bool networkActive READ networkActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool networkHolding READ networkHolding NOTIFY snapshotChanged)
    Q_PROPERTY(QString networkSummary READ networkSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString networkNotice READ networkNotice NOTIFY snapshotChanged)
    // Protection regions as the engine holds them: [{left,top,right,bottom,ellipse}].
    Q_PROPERTY(QVariantList protectionRegions READ protectionRegions NOTIFY protectionChanged)
    Q_PROPERTY(QString protectionDrawShape READ protectionDrawShape NOTIFY protectionDrawChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY snapshotChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY snapshotChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed NOTIFY snapshotChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY snapshotChanged)
    Q_PROPERTY(double minimumPlaybackRate READ minimumPlaybackRate CONSTANT)
    Q_PROPERTY(double maximumPlaybackRate READ maximumPlaybackRate CONSTANT)
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
    Q_PROPERTY(QObject* playlist READ playlist CONSTANT)
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
    Q_PROPERTY(QVariantList colourParameters READ colourParameters NOTIFY colourParamsChanged)
    // The same catalogue grouped by section, so the page can nest its collapsible
    // sub-panels without re-grouping in QML.
    Q_PROPERTY(QVariantList colourGroups READ colourGroups NOTIFY colourParamsChanged)
    Q_PROPERTY(int selectedColourLayer READ selectedColourLayer WRITE setSelectedColourLayer NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap colourState READ colourState NOTIFY settingsChanged)
    // Imported .cube files (ColorLutStore, the folder the engine resolves) and
    // the saved colour looks (ColorLookStore): a colour preset is one Color
    // node's settings, separate from the whole-chain presets.
    Q_PROPERTY(QVariantList lutLibrary READ lutLibrary NOTIFY colourLibraryChanged)
    Q_PROPERTY(QVariantList colourLooks READ colourLooks NOTIFY colourLibraryChanged)

public:
    QObject* playlist() const;
    Q_INVOKABLE void showPlaylist();
    Q_INVOKABLE void addPlaylistFilesDialog();
    Q_INVOKABLE void addPlaylistFolderDialog();
    Q_INVOKABLE void openDroppedUrls(const QList<QUrl>& urls);

    // --- Bilibili sign-in ---------------------------------------------------
    // Member renditions (1080P+/4K) are only served to a signed-in session. The user
    // pastes the Cookie header from their own browser once; it is kept under
    // %LOCALAPPDATA%\Veyra and sent on every later resolve.
    Q_INVOKABLE void bilibiliSignOut();
    // QR sign-in is asynchronous: bilibiliRequestQrStart() fetches a code and
    // bilibiliRequestQrPoll() asks once per second until the phone confirms; the page
    // renders whatever bilibiliQrImage() returns and reacts to bilibiliQrStarted /
    // bilibiliQrPolled. The code stays in this process; no browser window is involved.
    // The current sign-in code as an image URL for QML's Image source. Empty when
    // there is no code. `size` is the rendered pixel size of one symbol edge.
    Q_INVOKABLE QString bilibiliQrImage(int size = 320);

    // --- Bilibili module ----------------------------------------------------
    // Search / feed / detail / lists. Each returns a list of cards
    // (bvid, title, cover, owner, duration, play). An empty list means the request
    // failed or had no results; bilibiliLastError() says which.
    // Asynchronous form of the list endpoints. The synchronous ones block the GUI thread
    // for a full HTTPS round trip, which is why a rail click used to freeze the window and
    // why a loading skeleton could never appear: nothing repaints until the call returns.
    // These start the fetch on a worker and publish the result through bilibiliListReady.
    // Returns a generation id; a later request supersedes an earlier one.
    Q_INVOKABLE int bilibiliLoadList(const QString& kind);
    // Collects the finished fetch; called from the GUI tick like tickCapture.
    void tickBiliList();
    // 启动一个列表 worker。发起路径与 tick 的队列派发共用。
    // Starts one list worker; shared by the direct path and the tick's queue dispatch.
    void startListWorker(const QString& kind, int generation);
    // Result of one asynchronous list fetch, built by the worker and consumed unchanged on
    // the GUI thread. Public so the worker lambda can name it.
    struct ListFetchResult {
        bool ok = false;
        std::wstring error;
        QVariantList items;
        // Set instead of items when the request was a single object ("detail:<bvid>"),
        // because the video detail page is one map, not a list of cards.
        QVariantMap object;
    };
    // mediaId is qlonglong: folder ids run past the 32-bit int range (3490600161
    // for one real folder), and truncating one made the API fall back to the
    // default folder, so every folder looked identical.
    // 异步版：主线程只发起，结果为通过 bilibiliFavFoldersReady 回到 QML。
    // Async form: starts the request on a worker; the list arrives through
    // bilibiliFavFoldersReady.
    Q_INVOKABLE void bilibiliRequestFavFolders(const QString& reason = QString());
    Q_INVOKABLE void bilibiliDisableDanmaku();
    // 下面九个异步版：发起即返回，结果通过对应的 Ready 信号回到 QML。
    // 每个操作有独立槽位，打开详情页时同时发起的多个请求互不覆盖。
    // The nine async forms below return at once; results arrive through the matching
    // Ready signal. Each op owns its own slot, so the several requests the detail page
    // starts together never evict one another.
    Q_INVOKABLE void bilibiliRequestLike(const QString& bvid, bool like);
    Q_INVOKABLE void bilibiliRequestCoin(const QString& bvid, int count);
    Q_INVOKABLE void bilibiliRequestWatchLater(const QString& bvid, bool add);
    Q_INVOKABLE void bilibiliRequestComments(const QString& bvid, int page);
    Q_INVOKABLE void bilibiliRequestEnableDanmaku(const QString& bvid, int pageIndex);
    Q_INVOKABLE void bilibiliRequestResumeAt(const QString& bvid, qlonglong cid);
    Q_INVOKABLE void bilibiliRequestQrStart();
    Q_INVOKABLE void bilibiliRequestQrPoll();
    Q_INVOKABLE void bilibiliRequestAccount();
    // folderId 0 targets the account's first folder; the page passes the one the user
    // picked in the folder chooser shown beside the interaction buttons.
    // 异步版：发起后立即返回，结果通过 bilibiliFavoriteReady 回来。
    // reason 区分两个调用点（"toggle" 取消收藏 / "picker" 从浮层选定文件夹），
    // 回调据此决定要不要关浮层、要不要刷新互动状态。
    // Async form: returns at once; the outcome arrives through
    // bilibiliFavoriteReady. `reason` tells the two callers apart ("toggle" for
    // removing a favourite, "picker" for choosing a folder) so the reply can decide
    // whether to close the picker and whether to re-read the interaction state.
    Q_INVOKABLE void bilibiliRequestFavorite(const QString& bvid, bool add,
                                             qlonglong folderId, const QString& reason);
    // 异步版：主线程只发起，网络请求在 worker 里跑，结果通过
    // bilibiliInteractionReady 回到 QML。
    // Async form: the GUI thread only starts the request; the round trip runs on a
    // worker and the result arrives through bilibiliInteractionReady.
    Q_INVOKABLE void bilibiliRequestInteractionState(const QString& bvid);
    // Plays one part of a video through the normal pipeline. `qn` 0 = best allowed.
    // fromStart skips the watch-history resume point: the detail page offers a separate
    // button for that, because a video already in the history would otherwise always
    // continue where it was left and could not be watched again from the beginning.
    Q_INVOKABLE bool bilibiliPlay(const QString& bvid, int pageIndex, int qn,
                                 bool fromStart = false);
    // Same, but the caller already knows the part's cid from the detail response, so the
    // page lookup inside bilibiliPlay can be skipped. That lookup is a full round trip and
    // it runs on the GUI thread between the click and first frame.
    Q_INVOKABLE bool bilibiliPlayCid(const QString& bvid, qlonglong cid, int qn,
                                     bool fromStart = false);
    // Shared tail of the two entry points above, after the part is known.
    bool bilibiliPlayResolved(const QString& bvid, qlonglong cid,
                              const std::wstring& pageTitle, int qn, bool fromStart);
    // Reports where the user stopped to the account's own watch history. Called when
    // another source is opened and once more on teardown; a no-op when nothing from
    // the Bilibili page is playing. Failures are logged and ignored.
    void flushBiliWatchProgress();
    Q_INVOKABLE QString bilibiliLastError() const;
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
    int uiDeviceGeneration() const;
    // main.cpp: a window's scene graph came back after its graphics device was lost.
    void noteUiDeviceRecreated(const QString& window);
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
    QVariantList readComponentList() const;
    QVariantList captureDevices() const;
    QString captureDeviceId() const;
    void setCaptureDeviceId(const QString& value);
    QString captureDeviceLabel() const;
    bool captureBlackmagicDevice() const;
    bool captureForceSdr() const;
    bool captureMagewellDevice() const;
    QString captureMagewellStatus() const;
    void setCaptureForceSdr(bool value);
    bool captureFlipVertical() const;
    void setCaptureFlipVertical(bool value);
    QVariantList captureFormats() const;
    QString captureFormatKey() const;
    QString captureFormatLabel() const;
    bool captureFormatCostHint() const;
    void setCaptureFormatKey(const QString& key);
    QVariantList captureResolutionOptions() const;
    QString captureResolutionId() const;
    void setCaptureResolutionId(const QString& id);
    QVariantList captureFrameRateOptions() const;
    int captureHiddenFrameRates() const;
    QString captureFrameRateId() const;
    void setCaptureFrameRateId(const QString& id);
    QVariantList capturePixelOptions() const;
    QString capturePixelId() const;
    void setCapturePixelId(const QString& id);
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
    Q_INVOKABLE bool openCaptureDriverSettings(bool inputSelector = false);
    Q_INVOKABLE bool startCaptureSession();
    Q_INVOKABLE bool setScreenOption(const QString& key, const QVariant& value);
    Q_INVOKABLE bool startScreenCapture();
    bool gameMode() const;
    bool passthroughActive() const;
    bool gameInputActive() const;
    bool gameScaledPreview() const;
    QRectF gameVideoRect() const;
    // The Home panel is open: the picture stays on top but takes the input back.
    Q_INVOKABLE void setGamePanelOpen(bool open);
    // Index into Qt.application.screens of the monitor 游戏模式 covers, or -1.
    Q_INVOKABLE int gameModeScreenIndex() const;
    // Test only (VEYRA_TEST_UI_BLOCK): blocks the GUI thread to exercise the hang release.
    Q_INVOKABLE void testBlockUi(int ms);

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
    QVariantMap ps5Control() const;
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
    // key: enabled | background (saved preferences ps5CaptureControl / ps5ControlBackground).
    Q_INVOKABLE void ps5ControlSet(const QString& key, const QVariant& value);
    Q_INVOKABLE void ps5ControlRestart();
    QString remotePlayHost() const;
    void setRemotePlayHost(const QString& value);
    QString remotePlayPin() const;
    void setRemotePlayPin(const QString& value);

    QVariantMap preferences() const;
    QString gpuPriorityStatus() const;
    bool enhancementPrewarmAvailable() const;
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
    // An overlay that 监控软件兼容 reacts to is running now: RTSS, NVIDIA's in-game overlay
    // or GamePP (gfx/PresentationHooks.h).
    Q_INVOKABLE bool presentationOverlayRunning() const;
    Q_INVOKABLE void openFeedbackPage();
    Q_INVOKABLE void openReleasesPage();
    // 设置 → 赞助作者: opens the author's page for `id` (bilibili, douyin, x, github,
    // youtube, kofi) in the browser. The addresses live here; any other id does nothing.
    Q_INVOKABLE bool openSponsorLink(const QString& id);
    QObject* updates() const;
    QVariantList audioDevices() const;
    QVariantList gpuMonitorChoices() const;
    QString computeGpuHint() const;
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
    bool deinterlaceAvailable() const;
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
    bool nrAutoActive() const;
    int nrAutoPercent() const;
    QString nrAutoStatus() const;
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
    double minimumPlaybackRate() const;
    double maximumPlaybackRate() const;
    void setPlaybackRate(double rate);

    bool nrEnabled() const;
    int selectedNrLayer() const;
    void setSelectedNrLayer(int index);
    void setNrEnabled(bool value);
    bool srEnabled() const;
    void setSrEnabled(bool value);
    int fgMultiplier() const;
    void setFgMultiplier(int value);
    bool fgAutoMultiplier() const;
    void setFgAutoMultiplier(bool value);
    bool fgAutoMultiplierAvailable() const;
    // 自动选择倍率的运行时决策：每个会话首次进入播放时算一次档位。
    void maybeApplyFgAutoMultiplier();
    // 打开片源前用元数据里的源帧率预先定档，避免播放中重建 GPU 管线。
    // 返回 false 表示拿不到帧率或无可用档，由 maybeApplyFgAutoMultiplier 兜底。
    bool planFgAutoMultiplierBeforeOpen(double sourceFps);
    // B 站播放的解析在后台线程跑完后的回收（每个 GUI tick 一次）。
    void tickBiliPlay();
    // 通用任务：发起一个，回收全部已完成的。
    // Generic tasks: start one; drain every finished one.
    bool startBiliTask(BiliOp op, std::function<BiliTaskResult()> work);
    void tickBiliTasks();
    // 把观看进度从 impl_ 状态里取出，网络上报交给后台 worker。
    bool takeBiliWatchProgress(std::wstring& bvid, long long& cid, int& played);
    // 设倍率的实际动作，不碰 fgAutoMultiplier 标记（自动决策与手动选择共用）。
    void applyFgMultiplier(int multiplier);
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
    bool hdrCurve() const;
    void setHdrCurve(bool enabled);
    QVariantMap hdrCurveParams() const;
    Q_INVOKABLE bool setHdrCurveParameter(const QString& key, double value);
    // Custom: Dolby Vision level 1 follow. Live; only meaningful with the curve on.
    bool doviFollow() const;
    void setDoviFollow(bool on);
    // Custom (P5): composite the profile 7 enhancement layer. Takes effect on the
    // next open (it decides whether the second decoder runs at all).
    bool doviP7Compose() const;
    // Custom: follow may also use the frame's average brightness for the knee.
    bool doviFollowAverage() const;
    void setDoviFollowAverage(bool on);
    void setDoviP7Compose(bool on);
    // Commit + re-assert the tuning in the places the session capture reads, so a
    // change takes effect and survives a restart (the same trap the earlier
    // per-scene brightness block hit: a plain commit left it out of the pending
    // settings the session is written from).
    bool commitHdrTuning(engine::EnhancementSettings settings);
    QString hdrTuningPreset() const;
    Q_INVOKABLE bool applyHdrTuningPreset(const QString& id);
    // Custom: apply the recommendation above in one click.
    Q_INVOKABLE bool applyRecommendedHdrTuning();
    // Custom: forget every remembered display profile (the current device is kept
    // and re-learned from the settings in use).
    Q_INVOKABLE void forgetHdrDisplayProfiles();
    Q_INVOKABLE bool applyHdrAxesPreset(const QString& content, const QString& environment);
    Q_INVOKABLE bool setHdrDial(const QString& key, int level);
    Q_INVOKABLE void forgetHdrTitles();
    // The strength dial and the display peak (stored next to the baseline).
    int hdrCurveStrength() const;
    void setHdrCurveStrength(int percent);
    int hdrDisplayPeakNits() const;
    void setHdrDisplayPeakNits(int nits);
    QVariantMap hdrDisplayInfo() const;
    // Custom: see the hdrSourceInfo/hdrRecommendation/hdrPeakWarning properties.
    QVariantMap hdrSourceInfo() const;
    QVariantMap hdrRecommendation() const;
    bool sourceIsSdr() const;
    unsigned effectiveHdrDisplayPeakNits() const;
    QVariantMap hdrPeakWarning() const;
    int hdrDisplayProfileCount() const;
    QString hdrPresetMode() const;
    QString hdrAppliedFrom() const;
    void setHdrAppliedFrom(const QString& from);
    void setHdrPresetMode(const QString& mode);
    QVariantMap hdrAxes() const;
    QVariantMap hdrAxesMatch() const;
    QVariantMap hdrDials() const;
    QVariantMap hdrCurveGraph() const;
    QVariantMap hdrCurveCoverage() const;
    Q_INVOKABLE void observeHdrCoverage(bool on);
    bool hdrTitleMemory() const;
    void setHdrTitleMemory(bool on);
    bool hdrAutoRecommend() const;
    void setHdrAutoRecommend(bool on);
    int hdrTitleCount() const;
    void syncHdrTitleProfile();
    void flushHdrMemory();
    bool loadHdrTitleProfile(const QString& title);
    bool storeHdrTitleProfile(const QString& title);
    bool loadHdrProfile(const QString& id);
    bool storeHdrProfile(const QString& id);
    QString hdrDisplayProfileId() const;
    void syncHdrDisplayProfile();
    QVariantMap videoHdrParams() const;
    Q_INVOKABLE bool setVideoHdrParameter(const QString& key, double value);
    bool antiAliasEnabled() const;
    void setAntiAliasEnabled(bool enabled);
    QVariantMap antiAliasParams() const;
    Q_INVOKABLE QVariantMap antiAliasParamsAt(int index) const;
    QVariantList beautyLayers() const;
    QVariantMap beautyDetection() const;
    QString beautyStatus() const;
    // `index` is the chain node index (node mode cards, list layers alike).
    Q_INVOKABLE QVariantMap beautyParamsAt(int index) const;
    Q_INVOKABLE bool setBeautyParameterAt(int index, const QString& key, double value);
    // List mode: a copy right behind the layer; a move within the beauty group.
    Q_INVOKABLE int duplicateBeautyLayer(int index);
    Q_INVOKABLE bool moveBeautyLayer(int index, int delta);
    // natural / light / restore / restoreNatural: the sub-switches and values they set.
    Q_INVOKABLE bool applyBeautyPreset(int index, const QString& preset);
    Q_INVOKABLE bool setBeautyDetection(const QString& key, double value);
    bool beautyAnyEnabled() const;
    // List group switch: every layer off, and on again the layers that were on.
    Q_INVOKABLE bool setAllBeautyEnabled(bool enabled);
    // Back to the defaults (both sub-functions off); the switch stays as it is.
    Q_INVOKABLE bool resetBeautyLayer(int index);
    Q_INVOKABLE bool setAntiAliasParameter(const QString& key, double value);
    Q_INVOKABLE bool setAntiAliasParameterAt(int index, const QString& key, double value);
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
    int dlssSrModel() const;
    void setDlssSrModel(int value);
    QVariantList dlssSrModelChoices() const;
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
    QStringList runOptions() const;
    double effectiveOutputFps() const;
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
    bool edgeAegis() const;
    void setEdgeAegis(bool value);
    bool dlssgLite() const;
    void setDlssgLite(bool value);
    bool dlssgLiteAvailable() const;
    QString dlssgLiteHint() const;
    int contentRate() const;
    void setContentRate(int value);
    int displaySync() const;
    void setDisplaySync(int value);
    bool variableRefresh() const;
    void setVariableRefresh(bool value);
    int vrrState() const;
    QString vrrHint() const;
    QString fgDisplayHint() const;
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
    bool networkActive() const;
    bool networkHolding() const;
    QString networkSummary() const;
    QString networkNotice() const;
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
    Q_INVOKABLE QString nrAutoSelectionReason(int index) const;
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
    // The QML main window: 游戏模式 changes its styles and owns its hotkeys.
    void attachMainWindow(qulonglong nativeHandle);

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
    Q_INVOKABLE void openNetworkDialog();
    Q_INVOKABLE bool openNetworkAddress(const QString& address);
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
    // Re-reads the runtime manifest (Settings → 组件与许可 calls it when shown).
    Q_INVOKABLE void refreshComponentList();

signals:
    void playlistRequested();
    void nodeAnchorsChanged();
    void ps5Changed();
    void ps5ControlChanged();
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
    void componentListChanged();
    void perfChanged();
    void protectionDrawChanged();
    void presentationChanged();
    void compareChanged();
    void thumbnailGenerationChanged();
    void thumbnailSourceChanged(const QString& path);
    void settingsChanged();
    void fgChoicesChanged();
    void effectCapabilitiesChanged();
    void chainChanged();
    // A slider edits one group's values without touching the chain's structure,
    // labels or positions; only that group notifies (audit 2026-10-15 B1).
    void nrLayersChanged();
    void videoHdrParamsChanged();
    void antiAliasChanged();
    void beautyChanged();
    void colourParamsChanged();
    void protectionChanged();
    void recentFilesChanged();
    void presetsChanged();
    void exportChanged();
    // Emitted when the capture device or screen-target lists are re-enumerated.
    void captureChanged();
    void uiDeviceGenerationChanged();
    // User-facing notices the UI shows as a transient message: a rejected
    // chain edit, an export that finished, a failed open.
    void notice(const QString& text, bool isError);
    // kind is the same token passed to bilibiliLoadList; items is the fetched list.
    // generation lets QML drop a result that a newer request has already superseded.
    void bilibiliListReady(const QString& kind, const QVariantList& items, int generation);
    // Same request lifetime, for the single-object endpoints ("detail:<bvid>").
    void bilibiliObjectReady(const QString& kind, const QVariantMap& object, int generation);
    // Async interaction state: emitted when the worker that answers
    // bilibiliRequestInteractionState finishes. bvid echoes the request so a late
    // reply for a video the user has already left can be dropped; generation does
    // the same job for repeated requests on one video.
    void bilibiliInteractionReady(const QString& bvid, const QVariantMap& state, int generation);
    // Async favourite-folder list. `reason` tells the two call sites apart: switching to
    // the 收藏 view fills the page-level list, while the folder picker beside the
    // interaction buttons needs the same data for its own list.
    void bilibiliFavFoldersReady(const QVariantList& folders, const QString& reason);
    // Async favourite outcome. `ok` false means the server refused and `error` carries
    // its message; QML keeps the old button state in that case and shows the text.
    void bilibiliFavoriteReady(bool ok, const QString& error, bool add, const QString& reason);
    // 通用异步任务的结果信号。op 是操作名（like / coin / watchlater），
    // 便于 QML 用一个处理器覆盖三个形状相同的动作。
    // Result signals for the generic async task. `op` names the action (like / coin /
    // watchlater) so QML can handle the three same-shaped writes in one function.
    void bilibiliActionDone(const QString& op, bool ok, const QString& error);
    void bilibiliCommentsReady(const QVariantList& comments, bool ok, const QString& error);
    void bilibiliDanmakuReady(bool ok, const QString& error, int lineCount);
    void bilibiliResumeReady(double seconds);
    void bilibiliQrStarted(const QString& qrUrl, bool ok, const QString& error);
    void bilibiliQrPolled(const QString& state);
    void bilibiliAccountReady(const QVariantMap& account, bool ok);
    // A presentation-hooking overlay appeared in a run whose interface draws on the GPU
    // ("rtss": its OSD is kept out of this process; "nvidia": NVIDIA's in-game overlay and
    // "gamepp": GamePP, which cannot be kept out); a restart draws the interface in software.
    void overlayRestartSuggested(const QString& kind);
    // The UI moves to a page by name when a command implies it.
    void navigate(const QString& page);
    void gameModeChanged();
    // Configurable game hotkeys: 0 = adjust panel, 1 = leave.
    void gameHotkey(int which);
    // The game-mode capture ended or failed, or the GUI hung, while the picture was in
    // front: the UI leaves fullscreen and shows the reason.
    void gameModeStopped(const QString& reason);

private:
    // Structural change: every chain-derived property notifies.
    void emitChainChanged();
    // Value edit of one group; a change of the chain's validity notifies everything.
    void emitParameterChanged(void (QmlPlayerBridge::*group)(), bool wasValid);
    void updateGameMode();
    void updateGameGeometry();
    // 抖音直播伴侣 running (gfx/PresentationHooks.h): Realtime GPU priority is capped at
    // High while it runs and restored when it exits; 监控软件兼容 "off" leaves it alone.
    void updateLiveCompanionCompat();
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
    void scheduleEnhancementPrewarm();
    // Hold-to-compare key (press/release), handled for the whole application.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rememberPosition(bool flush);
    void refreshSubtitles(const std::wstring& media);
    void tickSubtitles();
    // Per-frame danmaku update; called from tickSubtitles().
    void tickDanmaku();
    void tickImageBatch();
    void tickCapture();
    void tickPs5();
    void tickPs5Control(bool force = false);
    // restart: replace a running session (another pairing, or the user asked).
    void startPs5Control(bool restart);
    void stopPs5Control();
    bool captureCardLive() const;
    void tickMoonlight();
    void tickXbox();
    void tickPlaylist();
#ifdef VEYRA_ENABLE_XBOX
    void setupXbox();
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
    void setupMoonlight();
#endif
    void queryCapture(int kind, std::wstring device);
    // Opens a device/stream URI (capture, screen) the way openPath opens a file.
    void openSourceUri(const std::wstring& uri, const std::wstring& label, bool screenFill = false);

    std::unique_ptr<Impl> impl_;
    QObject* updates_ = nullptr;
};

} // namespace veyra::ui
