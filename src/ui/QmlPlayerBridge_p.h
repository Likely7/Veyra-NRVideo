#pragma once
// Private bridge state shared by cohesive implementation modules.
// Public QML API, ownership, polling and settings contracts remain in QmlPlayerBridge.h.
#include "veyra/media/FFmpegDeinterlacer.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/NvidiaVrr.h"
#include "veyra/media/NetworkInput.h"
#include "veyra/ngx/DlssgLite.h"
#include "veyra/RuntimePaths.h"
#include "veyra/ui/GamePassthrough.h"
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <shellapi.h>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include "veyra/ui/EffectAvailability.h"
#include "veyra/engine/FrameGenerationSelection.h"
#include "veyra/ui/QmlExportQueueModel.h"
#include "veyra/ui/PlaylistModel.h"
#include "veyra/ui/UpdateChecker.h"
#include "veyra/ui/UiLanguage.h"
#include <QQmlEngine>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>

#include <QRegion>
#include <QCursor>
#include <QScreen>
#include <QWindow>
#include <QPlatformSurfaceEvent>
#include <QPointer>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QKeyEvent>
#include <QKeySequence>
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QBuffer>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/pipeline/VfgBackend.h"
#include "veyra/gfx/GpuSchedulingPriority.h"
#include "veyra/engine/ColorLookStore.h"
#include "veyra/engine/ColorLut.h"
#include "veyra/engine/Subtitles.h"
#include "veyra/engine/PlaybackRate.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/source/CaptureFormatSelection.h"
#include "veyra/source/CaptureCapabilityRange.h"
#include "veyra/source/CaptureFormatRank.h"
#include "veyra/source/CaptureColorOverride.h"
#include "veyra/source/CaptureFrameRate.h"
#include "veyra/source/MagewellCapture.h"
#include "CapturePreferenceStore.h"
#ifdef VEYRA_ENABLE_REMOTEPLAY
// SDL3-static's system libraries (see CMakeLists: kept off the link line).
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "dinput8.lib")
#include "veyra/remoteplay/ControllerInput.h"
#include "veyra/remoteplay/Discovery.h"
#include "veyra/remoteplay/ProfileStore.h"
#include "veyra/remoteplay/PsnAuth.h"
#include "veyra/source/RemotePlaySource.h"
#endif
#ifdef VEYRA_ENABLE_XBOX
#include "veyra/ui/XboxModel.h"
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
#include "veyra/ui/MoonlightInputCapture.h"
#include "veyra/ui/MoonlightModel.h"
#endif
#include <future>
#include <functional>
#include <map>
#include <deque>
#include "SubtitleOverlay.h"
// GDI+ is only started here (the overlay draws with it); its headers expect
// min/max, which NOMINMAX builds do not define as macros.
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
// 1.4.4's live status rules (正常 / 补帧调度降档 / 输出未达标 / 输入帧率不足).
#include "LiveStatusHistory.h"
// GPU utilization for the performance orb: the Windows "GPU Engine" counters,
// the same source Task Manager reads.
#include <pdh.h>
#include <pdhmsg.h>
#pragma comment(lib, "pdh.lib")
#include <atomic>
#include <set>
#include <mutex>
#include <thread>
#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/ui/PlaybackPowerGuard.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/PresentationHooks.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "veyra/ngx/NrArchitecturePolicy.h"
#include "veyra/ngx/FgCompatibilitySession.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/DeckLinkCapture.h"
#include "veyra/source/CaptureSignalProbe.h"
#include "veyra/source/ScreenCaptureSource.h"

namespace veyra::ui {
class VirtualCaptureAudioSetup;
namespace bridge_detail {
inline QString utf8Of(std::string_view s) { return QString::fromUtf8(s.data(), int(s.size())); }
inline QString utf8Of(const std::wstring& w) { return QString::fromWCharArray(w.c_str(), int(w.size())); }

// Where crash and device-loss hints go: next to the log, where the crash handler in
// apps/veyra-qml/main.cpp writes them too.
inline QString lastFailurePath() {
    return QFileInfo(qApp->property("veyraLogFile").toString()).absolutePath() + QStringLiteral("/veyra-last-failure.txt");
}

// What the user can switch off, for the injected components named in `modules`
// (RTSSHooks64.dll, see gfx::riskyInjections). Empty when none is known.
inline QString injectionAdvice(const QString& modules) {
    QStringList parts;
    if (modules.contains(QStringLiteral("RTSSHooks64.dll"), Qt::CaseInsensitive) && !qApp->property("veyraSoftwareUi").toBool())
        parts << QCoreApplication::translate("QmlPlayerBridge", "检测到 RivaTuner（小飞机 OSD）注入了 Veyra。请把 设置 → 监控软件兼容 设为“自动”并在小飞机运行时重启 Veyra：界面改用兼容绘制，OSD 只显示在视频上");
    return parts.join(QStringLiteral("；"));
}
// Messages and fixed table labels shown to the user, in the interface language
// (UiLanguage.h). Data that only passes through the UI - paths, device, preset and
// LUT names - keeps utf8Of(), and so do log lines.
inline QString uiText(const std::wstring& w) { return veyra::ui::i18n::text(w); }
inline QString uiText(std::string_view s) { return veyra::ui::i18n::text(QString::fromUtf8(s.data(), int(s.size()))); }
inline std::wstring wideOf(const QString& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()), size_t(s.size()));
}
inline QString mmss(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int total = int(seconds);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
inline void exportCompletionNotice(bool enabled,uint64_t event,const char* kind){
    const BOOL queued=enabled?MessageBeep(MB_OK):FALSE;
    veyra::log::info("export-notify",std::format("kind={} event={} soundEnabled={} systemSoundQueued={}",kind,event,enabled,queued!=FALSE));
}
inline bool exportFileClosed(const QString& path){
    HANDLE file=CreateFileW(wideOf(path).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    CloseHandle(file);return true;
}
} // namespace bridge_detail
using namespace bridge_detail;

QVariantMap currentEffectCapabilities(bool refresh=false);
QString unavailableEffects(const engine::EnhancementSettings& settings);
QString disableUnavailableEffects(engine::ChainConfiguration& configuration);
ui::EffectGpu effectGpu();

namespace bridge_detail {
// WM_DISPLAYCHANGE reaches every top-level window when a display mode, refresh
// rate or HDR state, or the set of screens, changes: the 可变刷新 state is then
// read once more (DisplayStateService) instead of on a timer.
struct DisplayChangeFilter final : QAbstractNativeEventFilter {
    bool* changed = nullptr;
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (changed && type == "windows_generic_MSG" && static_cast<const MSG*>(message)->message == WM_DISPLAYCHANGE) *changed = true;
        return false;
    }
};
} // namespace bridge_detail

struct QmlPlayerBridge::Impl {
    // Per-layer on/off before the NR master switch turned them all off.
    std::vector<bool> nrMasterRestore;
    std::vector<bool> beautyMasterRestore;   // the same for the beauty group switch
    QQmlEngine* qmlEngine = nullptr;
    engine::EngineController& engine;
    PlayerUiFacade facade;

    // Last published snapshot and the revision it came from. Polling compares
    // revisions, so an unchanged snapshot costs one comparison per tick.
    engine::PlayerSnapshot snapshot;
    uint64_t publishedRevision = 0;
    bool haveSnapshot = false;
    bool openingSource = false;
    uint64_t openingSessionId = 0;
    uint64_t vramNoticeSession = 0;   // the session the video-memory warning was shown for

    // The chain the UI edits, kept in step with the facade's pending settings.
    engine::EffectChain chain;
    engine::EffectChain acceptedChain;
    engine::NodeGraphLayout layout;
    engine::ChainGlobalSettings draftGlobals;
    const engine::EffectChain& activeRuntime() const {
        return chain.mode == engine::ChainMode::Node ? acceptedChain : chain;
    }
    void initializeDefaultNodePositions() {
        if (chain.mode != engine::ChainMode::Node) return;
        for (uint32_t i = 0; i < chain.nodeCount; ++i)
            if (chain.nodes[i].viewX == 0 && chain.nodes[i].viewY == 0) {
                chain.nodes[i].viewX = float(i % 4) * 260;
                chain.nodes[i].viewY = float(i / 4) * 300;
            }
    }
    void restoreConfiguration(const engine::ChainConfiguration& c) {
        draftGlobals = c.editor && c.editor->globals ? *c.editor->globals :
            static_cast<const engine::ChainGlobalSettings&>(c);
        acceptedChain = c.chain;
        chain = c.editor ? c.editor->nodes : c.chain;
        if (c.editor) layout = c.editor->layout;
        else if (chain.mode == engine::ChainMode::Node) layout.initialize(chain);
        selectedNr = c.selectedNr; selectedColour = c.selectedColour;
        // Resolve the old QML-only layout fallback once so duplication, wires
        // and saved coordinates use the same physical rectangles.
        if (!c.editor) initializeDefaultNodePositions();
    }
    engine::ChainValidation validation;
    engine::ChainSession session;
    engine::ChainSession savedSession;
    QTimer* sessionSaveTimer = nullptr;
    // 音量 is a shell preference (2026-11-08); a drag is written once it settles.
    QTimer* volumeSaveTimer = nullptr;
    int selectedNr=-1;
    int nrIndex() const {
        if(selectedNr>=0&&uint32_t(selectedNr)<chain.nodeCount&&
           chain.nodes[selectedNr].type==engine::EffectType::NrEnhance)return selectedNr;
        for(uint32_t i=0;i<chain.nodeCount;++i)if(chain.nodes[i].type==engine::EffectType::NrEnhance)return int(i);
        return -1;
    }
    engine::ChainNode* nrNode(){const int i=nrIndex();return i>=0?&chain.nodes[i]:nullptr;}
    int selectedColour=-1;
    int colourIndex() const {
        if(selectedColour>=0&&uint32_t(selectedColour)<chain.nodeCount&&
           chain.nodes[selectedColour].type==engine::EffectType::Color)return selectedColour;
        for(uint32_t i=0;i<chain.nodeCount;++i)if(chain.nodes[i].type==engine::EffectType::Color)return int(i);
        return -1;
    }

    // The window the engine presents into, and the last file the UI opened.
    HWND videoWindow = nullptr;
    std::wstring sourceLabel;
    int thumbnailGeneration = 0;
    // The live source kind and label ("" for a file), for the page headers.
    QString liveKind, liveLabel;
    // A live open waiting for the 极简 switch to settle (openAfterCinema); the
    // generation lets a newer open or a stop cancel it.
    int liveOpenGen = 0;
    QString pendingLiveText;
    // 调色 history: each commit pushes the layer's previous state; a slider
    // drag (edits under 600 ms apart on one layer) is one step.
    struct ColourStep { int index = -1; engine::ColorSettings colour{}; };
    std::vector<ColourStep> colourUndo, colourRedo;
    std::optional<engine::ColorSettings> colourClip;
    qint64 colourLastPushMs = 0;
    int colourLastIndex = -1;
    bool colourReplaying = false;
    // Poster data URL cache, per session.
    uint64_t posterSession = 0;
    QString posterUrl;

    // Keeps the display and system awake while a video plays (AppShell rule: running,
    // not failed, not a still image, transport Playing). Acquired and released on
    // this GUI thread, as SetThreadExecutionState requires.
    PlaybackPowerGuard power;
    std::function<void()> preOpen;

    // Export progress is mirror state: the engine reports an export through a
    // progress callback, and there is no export status in PlayerSnapshot yet.
    bool exportRunning = false;
    std::wstring exportOutput;
    QString exportStatus;
    engine::PlayerOptions options;

    // The export job manager is the real thing: state, progress and encoded
    // counts come from its snapshot.
    engine::ExportJobManager exportJob;
    QmlExportQueueModel exportQueue{exportJob};
    int exportContainer=0;
    uint64_t exportNotifiedBatch=0;
    QString pendingFrameExport;
    qint64 pendingFrameDeadline=0;
    uint64_t imageCompletionEvent=0;
    engine::ExportJobSnapshot exportSnapshot;
    bool exportHevc = false;
    // Default: VBR at 8 Mbps (field request 2026-10-02); CQ stays one tap away.
    uint32_t exportBitrateMbps = 8;
    sink::ExportRateControl exportRateControl = sink::ExportRateControl::Vbr;
    int exportResolutionIndex = -1;
    double exportTrimStartSeconds = 0.0;
    double exportTrimEndSeconds = 0.0;
    std::wstring exportPresetName;
    QString initialPageOverride;
    QString currentPage;
    std::wstring captureDevice;
    QString screenTarget;
    QString remotePlayHost, remotePlayPin;

    QTimer* timer = nullptr;

    // Canvas positions of the fixed input / optical-flow / output boxes. They are
    // editor furniture, not chain nodes, so they live beside the preferences.
    std::filesystem::path anchorsFile;
    QVariantMap anchors;
    // Where the user's own files live (the facade resolves the same default).
    std::filesystem::path dataDir;
    PlaylistModel* playlist = nullptr;
    QString playlistPath;
    uint64_t playlistSession = 0, playlistCompletedSession = 0;
    // --- subtitles (ported from the Win32 shell, same semantics) ---
    std::unique_ptr<engine::SubtitleLoader> subLoader;
    // Embedded-subtitle scan of a file on a server (#48): reads only while playback has its own
    // buffer. The gate follows the engine's network statistics (tickSubtitles); null for local files.
    std::shared_ptr<std::atomic<bool>> subScanGate;
    bool subScanOpen = false;
    std::unique_ptr<engine::SubtitleLoader> subFileLoader;
    uint64_t subFileGeneration = 0;
    std::wstring subFilePath;
    uint64_t subGeneration = 0;
    std::vector<engine::SubtitleTrack> subTracks;   // external files first, then the loader's
    size_t subExternal = 0;                          // user-loaded files at the front
    size_t subLoaded = 0;                            // loader tracks currently held
    int subPrimary = -1, subSecondary = -1;
    bool subPrimaryChosen = false, subSecondaryChosen = false;
    QString subStatus;
    QString subText;
    int subBottomInset = 0;
    HWND subOverlay = nullptr;
    bool subOverlayFailed = false;
    std::wstring subMedia;
    std::atomic<bool> subAligning{false}, subAlignReady{false};
    std::atomic<int> subAlignOffset{0};
    std::mutex subAlignMutex;
    std::wstring subAlignDetail;
    uint64_t subAlignGeneration = 0;
    int subAlignTrack = -1;
    // Declared after everything the worker touches, so it is joined first.
    std::jthread subAlignWorker;
    QVariantMap prefs;
    // Live recording state published by the engine (see LiveRecorder.h).
    QVariantMap recordingState;
    uint64_t recordingSaved = 0;   // LiveRecorder's finished-file counter already announced
  // Custom: the display whose HDR curve is currently applied (see syncHdrDisplayProfile).
  QString hdrProfileDisplay;
  // Custom: the title whose curve is currently applied (see syncHdrTitleProfile).
  QString hdrTitleApplied;
  HMONITOR hdrIdentityMonitor=nullptr;
  QString hdrIdentityKey;
  bool hdrRestoring=false,hdrCoverageObserved=false,hdrMemoryDirty=false;
  QTimer* hdrMemorySaveTimer=nullptr;
  // Custom: how many remembered titles survive; the oldest fall off first.
  int kHdrTitleProfileLimit = 60;
    QString lastScreenshot;
    QString pendingScreenshot;
    qint64 pendingScreenshotDeadline=0;
    bool holdKeyDown = false;
    // The last key the shortcut map was offered (ShortcutOverride), named in the
    // record trigger log: field report 2026-11-08 of recording that toggled on
    // and off with nothing in the log saying which key or button did it.
    struct ShortcutKeySeen {
        int key = 0, modifiers = 0;
        quint32 virtualKey = 0, scanCode = 0;
        bool autoRepeat = false;
        qint64 atMs = 0;
        QString window;
    } lastShortcutKey;
    int positionTicks = 0;
    // A reopened file resumes here once its session is running.
    uint64_t resumeSession = 0;
    double resumeAt = 0;
    std::vector<sink::RenderEndpoint> audioEndpoints;
    // --- capture connection (P4-e) ---
    struct CaptureQuery { int kind = 0; std::wstring device,requestedResolution; std::vector<source::CaptureDevice> video, audio; std::vector<source::CaptureFormat> formats;source::CaptureFormatQueryInfo info; };
    std::future<CaptureQuery> captureQuery;
    std::optional<std::pair<int,std::wstring>> captureNextQuery;
    bool captureBusy = false, captureListed = false, captureResumePending = false;
    bool captureDriverSettingsOpen = false;
    std::wstring captureDriverFormatKey;
    int captureQueryKind = 0;   // 0 device list, 1 formats, 2 formats then connect (continue)
    std::vector<source::CaptureDevice> captureVideo, captureAudio;
    std::vector<source::CaptureFormat> captureFormatList;
    // The capture dialog's lists are built once per input. QML re-reads a list property on
    // every element access (Qt 6 sequence references), so a getter that rebuilt the list
    // made `.find()` and VSelect quadratic: 15474 formats hung the interface (logs(18)).
    // Each cache is checked against what it was built from, not reset by captureChanged:
    // QML bindings receive a notify signal before any ordinary connection does.
    uint64_t captureFormatGeneration = 0;   // bumped whenever captureFormatList is replaced
    int uiDeviceGeneration = 0;   // interface scene graphs recreated after a lost device
    // One automatic capture-card reconnect after the GPU stopped completing work. Field
    // logs 56: after the hang and driver reset the session just ended; reconnecting by hand
    // ran smoothly again. A second GPU failure within ten minutes is left to the user.
    uint64_t gpuReconnectSession = 0;
    std::chrono::steady_clock::time_point gpuReconnectDue{}, gpuReconnectLast{};
    // Network sources (#48): damaged data is concealed or skipped without a word per packet;
    // the user hears about it once in a while, and about a successful reconnect.
    uint64_t netNoticeSession = 0, netLossReported = 0;
    uint32_t netReconnectsReported = 0;
    std::chrono::steady_clock::time_point netLossNoticeAt{};
    struct CaptureListCache { bool valid = false; uint64_t generation = 0; QString language; std::wstring a, b; QVariantList list; };
    CaptureListCache captureFormatsCache, captureResolutionCache, captureRateCache, capturePixelCache;
    // Settings → 组件与许可. Read once and on refreshComponentList(): the property used to
    // notify with every engine snapshot, so each 60 Hz tick re-read the manifest and
    // rebuilt the page's 49 rows (audit 2026-10-15 B5).
    QVariantList componentList; bool componentListRead = false;
    bool liveCompanionCap = false;   // updateLiveCompanionCompat
    int captureHiddenRates = 0;   // set with captureRateCache
    template <class Build> const QVariantList& captureList(CaptureListCache& cache, const std::wstring& a, const std::wstring& b, Build&& build) {
        const QString language = i18n::current();
        if (!cache.valid || cache.generation != captureFormatGeneration || cache.language != language || cache.a != a || cache.b != b) {
            cache.list = build();
            cache.valid = true; cache.generation = captureFormatGeneration; cache.language = language; cache.a = a; cache.b = b;
        }
        return cache.list;
    }
    std::wstring captureFormatKey;
    std::wstring captureResolutionChoice, captureRateChoice;
    std::wstring captureRequestedResolution;
    int captureAudioChoice = source::kCaptureAudioDisabled;
    bool captureVirtualAudioInstalled = false;
    VirtualCaptureAudioSetup* captureVirtualAudioSetup = nullptr;
    bool captureAudioMissing = false;
    unsigned captureColor = 0;
    double captureFps = 0;
    QString captureStatus;
    ui::CapturePreferences capturePrefs;
    // --- PS5 Remote Play ---
    QVariantMap ps5Form;
    QString ps5Status;
    std::wstring ps5Profile;                 // file name inside profileDirectory()
    QVariantList ps5ProfileList;
    uint64_t psnLoginStarted = 0;
    bool ps5Watching = false;
    struct Ps5Outcome { std::wstring message; std::vector<std::pair<std::string, std::string>> hosts; std::wstring savedPath; std::string account; bool refreshProfiles = false; };
    std::mutex ps5Mutex;
    Ps5Outcome ps5Outcome;
    std::atomic<bool> ps5Done{false};
    bool ps5Busy = false;
    QTimer* controllerTimer = nullptr;
    // PS5 手柄控制 (capture card dialog).
    QTimer* ps5ControlTimer = nullptr;
    QVariantMap ps5ControlState;
    std::wstring ps5ControlProfile;          // pairing (or test host) the session was started with
    QString ps5ControlNoticed;               // state the last notice was shown for
    int ps5ControlTicks = 0;
    bool ps5ControlNoPair = false;           // switched on without a pairing; cleared once pairings exist
#ifdef VEYRA_ENABLE_REMOTEPLAY
    remoteplay::ControllerInput controller;
    // One SDL pad reader shared by PS5 stream, PS5 手柄控制, PC and Xbox streaming.
    // Each user acquires it and only the last release stops it, so the timer of a
    // source that just ended never cuts off the pad the next source started using.
    enum PadUser : unsigned { PadPs5 = 1, PadPs5Control = 2, PadMoonlight = 4, PadXbox = 8 };
    unsigned padUsers = 0;
    bool acquirePad(unsigned user) {
        const bool ok = controller.start(); // no-op once initialized
        padUsers |= user;
        controller.setFollowLastPressed((padUsers & PadPs5Control) != 0);
        return ok;
    }
    void releasePad(unsigned user) {
        padUsers &= ~user;
        if (!padUsers) controller.stop();
        else controller.setFollowLastPressed((padUsers & PadPs5Control) != 0);
    }
#endif
    std::jthread ps5Worker;                  // last: joined before what it touches
    // --- screen capture ---
    std::vector<source::ScreenCaptureTarget> screenList;
    bool screenFillActive = false;
    QSize screenFitClient, screenFitImage;
    double screenFitAspect = -1;
    // --- image batch ---
    QStringList imageFiles;
    QString imageFolder, imageTarget, imageStatus;
    int imageIndex = -1, imageDone = 0, imageFailures = 0, imageWaits = 0;
    uint64_t imageSession = 0;
    enum class ImagePhase { Idle, Opening, Saving } imagePhase = ImagePhase::Idle;
    sink::ActiveRenderEndpoint audioActive;
    int audioPollTicks = 0;
    // --- protection drawing (#8) ---
    QString protDraw;                         // "" | rect | ellipse
    HWND protOverlay = nullptr;
    // --- performance orbs / status light (#13) ---
    ui::live_status::DashboardHistory history;
    int historyTicks = 0;
    QString runStatus, runLevel, runDetail;
    QStringList runOptions;
    double effectiveOutputFps=-1;
    // FG output hint for the 倍率 row; 可变刷新 level (gfx::vrrLevel) of the
    // presenting monitor, or of the main window's monitor while idle.
    QString fgDisplayHint;
    int vrrState=0;
    // The shown monitor's Windows HDR (-1 unknown) and refresh rate.
    int displayHdr=-1;double displayRefreshHz=0;
    // 刷新显示器状态: the display generation it asked for, until the shown monitor
    // answers (displayChecking also covers any query in flight).
    bool displayChecking=false,displayRecheckPending=false,displayChanged=false;
    uint64_t displayRecheckGeneration=0;
    std::chrono::steady_clock::time_point displayRecheckSince{};
    DisplayChangeFilter displayChangeFilter;
    std::atomic<double> gpuUtil{-1.0};
    // Hardware adapters in Windows' high-performance order (discrete first). The
    // sampler only counts engines of the adapter whose LUID is in gpuMonitorLuid
    // (0: every adapter, the old behaviour, used only when none was found).
    struct GpuAdapter { QString id, name; uint32_t luidHigh = 0, luidLow = 0; };
    std::vector<GpuAdapter> gpuAdapters;
    std::atomic<uint64_t> gpuMonitorLuid{0};
    QString gpuMonitorName;
    // LUIDs that have "GPU Engine" counter instances. DXGI can list one physical GPU
    // twice (a virtual display driver such as GameViewer's exposes a second adapter
    // with the GPU's own name); only the real one has engines to measure.
    static std::set<uint64_t> gpuEngineLuids() {
        std::set<uint64_t> found;
        DWORD counterBytes = 0, instanceBytes = 0;
        if (PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine", nullptr, &counterBytes, nullptr, &instanceBytes, PERF_DETAIL_WIZARD, 0) != PDH_MORE_DATA)
            return found;
        std::vector<wchar_t> counters(counterBytes + 1), instances(instanceBytes + 1);
        if (PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine", counters.data(), &counterBytes, instances.data(), &instanceBytes, PERF_DETAIL_WIZARD, 0) != ERROR_SUCCESS)
            return found;
        for (const wchar_t* name = instances.data(); *name; name += wcslen(name) + 1) {
            unsigned high = 0, low = 0;
            if (const wchar_t* at = wcsstr(name, L"luid_"); at && swscanf_s(at, L"luid_0x%x_0x%x", &high, &low) == 2)
                found.insert(uint64_t(high) << 32 | low);
        }
        return found;
    }
    void enumerateGpuAdapters() {
        const auto measured = gpuEngineLuids();
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))); ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (FAILED(adapter->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == 0x1414) continue;
            const uint64_t luid = uint64_t(uint32_t(d.AdapterLuid.HighPart)) << 32 | d.AdapterLuid.LowPart;
            if (!measured.empty() && !measured.count(luid)) {
                veyra::log::info("perf", std::format("gpu adapter skipped (no engine counters) luid={:08X}:{:08X} {}", uint32_t(luid >> 32), uint32_t(luid),
                    QString::fromWCharArray(d.Description).toStdString()));
                continue;
            }
            // LUIDs change at every boot, so the saved choice is vendor:device:subsystem,
            // with an ordinal for two identical cards.
            QString id = QStringLiteral("%1:%2:%3").arg(d.VendorId, 4, 16, QLatin1Char('0')).arg(d.DeviceId, 4, 16, QLatin1Char('0')).arg(d.SubSysId, 8, 16, QLatin1Char('0'));
            int same = 0;
            for (const auto& a : gpuAdapters) if (a.id.section('#', 0, 0) == id) ++same;
            if (same) id += QStringLiteral("#%1").arg(same);
            gpuAdapters.push_back({id, QString::fromWCharArray(d.Description).trimmed(), uint32_t(d.AdapterLuid.HighPart), d.AdapterLuid.LowPart});
            veyra::log::info("perf", std::format("gpu adapter {} id={} luid={:08X}:{:08X} dedicatedMiB={}", gpuAdapters.size() - 1,
                id.toStdString(), uint32_t(d.AdapterLuid.HighPart), d.AdapterLuid.LowPart, d.DedicatedVideoMemory >> 20));
        }
    }
    // 游戏模式: the native side, the capture it belongs to and what the UI reported.
    ui::GamePassthrough game;
    std::wstring gameModeUri;          // the screen URI startScreenCapture opened in game mode
    uint64_t gameModeSessionId = 0;    // its engine session once the open ran
    bool gameModeLive = false, gamePanelOpen = false, gameRefused = false;
    bool gameModeShown = false, passthroughShown = false, gameInputShown = false;
    QRectF gameVideoRect;
    QPointer<QWindow> gameWindow;
    HMONITOR gameMonitor = nullptr;
    // The monitored GPU also computes when 设置 -> "同时用这块显卡运算" is on
    // and a specific GPU (not 自动) is chosen; otherwise the default order.
    QString computeGpuName;
    uint64_t computeLuid = 0;
    void applyComputeGpu() {
        const QString wanted = prefString("monitorGpu");
        const GpuAdapter* chosen = nullptr;
        for (const auto& a : gpuAdapters) if (!wanted.isEmpty() && a.id == wanted) chosen = &a;
        const bool follow = prefBool("computeOnMonitorGpu", false) && chosen;
        computeLuid = follow ? (uint64_t(chosen->luidHigh) << 32 | chosen->luidLow) : 0;
        computeGpuName = follow ? chosen->name : QString();
        gfx::setPreferredAdapterLuid(computeLuid);
        veyra::log::info("perf", std::format("compute gpu follow={} wanted={} using={}", follow, wanted.isEmpty() ? "auto" : wanted.toStdString(),
            follow ? chosen->name.toStdString() : "default order (highest-performance adapter)"));
    }
    void resolveGpuMonitor() {
        const QString wanted = prefString("monitorGpu");
        const GpuAdapter* chosen = gpuAdapters.empty() ? nullptr : &gpuAdapters.front();
        for (const auto& a : gpuAdapters) if (!wanted.isEmpty() && a.id == wanted) chosen = &a;
        gpuMonitorLuid = chosen ? (uint64_t(chosen->luidHigh) << 32 | chosen->luidLow) : 0;
        gpuMonitorName = chosen ? chosen->name : QString();
        veyra::log::info("perf", std::format("gpu monitor wanted={} using={}", wanted.isEmpty() ? "auto" : wanted.toStdString(),
            chosen ? chosen->name.toStdString() : "all adapters"));
    }
    double gpuPublished = -2.0;
    double rateRatio = -1.0;
    std::deque<double> frameTimes;           // frame interval ms, newest last
    // --- design gap: presentation, compare, aspect ---
    engine::PresentationSettings presentation;
    int compareMode = 0;
    bool compareBase = false;
    double compareSplit = 0.5;
    int aspectMode = 0, aspectApplied = 0;
    uint64_t aspectSession = 0;
    std::jthread gpuSampler;                 // joined before gpuUtil goes away
    std::filesystem::path prefsFile() const { return dataDir / L"qml-preferences.v1.json"; }
    void loadPrefs() {
        QFile file(QString::fromStdWString(prefsFile().wstring()));
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly)) { veyra::log::warn("ui-prefs", "qml preferences unreadable; defaults used"); return; }
        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            veyra::log::warn("ui-prefs", "qml preferences damaged; defaults used, file kept");
            return;
        }
        prefs = doc.object().toVariantMap();
    }
    bool savePrefs() {
        const QString path = QString::fromStdWString(prefsFile().wstring());
        // A fresh data directory may not exist yet: the first preference saved
        // on a new profile used to fail and was silently kept only in memory.
        std::error_code ec;
        std::filesystem::create_directories(prefsFile().parent_path(), ec);
        if(ec)return false;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(prefs)).toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size()) return false;
        return file.commit();
    }
    int prefInt(const char* key, int fallback) const { return prefs.contains(key) ? prefs.value(key).toInt() : fallback; }
    bool prefBool(const char* key, bool fallback) const { return prefs.contains(key) ? prefs.value(key).toBool() : fallback; }
    QString prefString(const char* key) const { return prefs.value(key).toString(); }
    void restoreCaptureForm() {
        captureFps=capturePrefs.requestedFps;
        captureColor=capturePrefs.colorForDevice(captureDevice);
        captureAudioChoice=source::kCaptureAudioDisabled;
        captureAudioMissing=false;
        if(capturePrefs.audioMode==source::kCaptureAudioFromVideoDevice)captureAudioChoice=source::kCaptureAudioFromVideoDevice;
        else if(!capturePrefs.audioPath.empty()){
            captureAudioMissing=true;
            for(size_t k=0;k<captureAudio.size();++k)
                if(captureAudio[k].path==capturePrefs.audioPath&&captureAudio[k].audioMode()==capturePrefs.audioMode){
                    captureAudioChoice=int(k);captureAudioMissing=false;break;
                }
        }
    }
    bool persistCaptureSelection(bool explicitFormat = false) {
        if(captureDevice.empty())return true; // Disconnecting a card must retain it.
        const bool sameDevice=capturePrefs.videoPath==captureDevice;
        capturePrefs.videoPath=captureDevice;
        // An unavailable saved type has no active selection. Editing another
        // control must retain its key until the user explicitly picks a new type.
        if(explicitFormat||!captureFormatKey.empty()||!sameDevice)capturePrefs.formatKey=captureFormatKey;
        if(explicitFormat){
            // A partial form is a deliberate choice too. Keep it without
            // upgrading capture-preferences.v1 or inventing a concrete format.
            prefs["captureDraftDevice"]=utf8Of(captureDevice);
            prefs["captureDraftResolution"]=utf8Of(captureResolutionChoice);
            prefs["captureDraftRate"]=utf8Of(captureRateChoice);
            if(!savePrefs())return false;
        }
        capturePrefs.colorOverride=captureColor;capturePrefs.requestedFps=captureFps;
        if(capturePrefs.deviceColors.size()<128||capturePrefs.deviceColors.contains(captureDevice))capturePrefs.deviceColors[captureDevice]=captureColor;
        if(!captureAudioMissing){
            const auto* audio=captureAudioChoice>=0&&size_t(captureAudioChoice)<captureAudio.size()?&captureAudio[size_t(captureAudioChoice)]:nullptr;
            capturePrefs.audioPath=audio?audio->path:std::wstring{};
            capturePrefs.audioMode=audio?audio->audioMode():captureAudioChoice;
        }
        if(ui::CapturePreferenceStore(dataDir).save(capturePrefs))return true;
        veyra::log::error("capture-ui","capture settings could not be saved");return false;
    }
    Impl(engine::EngineController& e, std::filesystem::path dataDirectory)
        : engine(e), facade(e, dataDirectory),
          dataDir(dataDirectory.empty() ? runtime::localDataDirectory() : dataDirectory) {
        if (!dataDirectory.empty()) anchorsFile = dataDirectory / L"node-anchors.json";
        if (!anchorsFile.empty()) {
            QFile file(QString::fromStdWString(anchorsFile.wstring()));
            if (file.open(QIODevice::ReadOnly)) anchors = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        }
        // Same high-performance DXGI ordering as the engine. This only seeds a
        // fresh session; loading saved settings below preserves manual choices.
        gfx::ComPtr<IDXGIFactory6> factory;
        if(SUCCEEDED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)))) {
            gfx::ComPtr<IDXGIAdapter1> adapter;
            for(UINT i=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++i) {
                DXGI_ADAPTER_DESC1 desc{};
                if(FAILED(adapter->GetDesc1(&desc))||(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
                auto initial=facade.pendingSettings();
                initial.nrRuntime=desc.VendorId==0x1002?engine::NrRuntime::LmxxfAmd:engine::NrRuntime::Ampere;
                if(desc.VendorId==0x1002||desc.VendorId==0x8086){
                    initial.opticalFlowBackend=engine::OpticalFlowBackend::AmdFidelityFx;
                    initial.videoSrQuality=engine::kVideoSrFsr;
                }
                initial.frameGenerationBackend=engine::defaultFrameGenerationBackend(desc.VendorId);
                facade.setPending(initial);
                veyra::log::info("nr-default",std::format("adapter={} selected={}",utf8Of(std::wstring(desc.Description)).toStdString(),engine::nrRuntimeName(initial.nrRuntime)));
                break;
            }
        }
        chain = engine::toChain(facade.pendingSettings());
        acceptedChain = chain;
        draftGlobals = engine::ChainGlobalSettings::capture(facade.pendingSettings());
        session = engine::ChainSession::initial(facade.pendingSettings());
        savedSession = session;
        validation = engine::validateChain(chain);
        options = engine::PlayerOptions::from(facade.pendingSettings());
    }

    std::optional<engine::EnhancementSettings> exportSettings(QString& error) const {
        if (chain.mode == engine::ChainMode::Node) {
            error = QObject::tr("节点链尚未接入导出执行器，请切换列表模式后导出");
            return std::nullopt;
        }
        auto settings = facade.pendingSettings();
        engine::fromChain(chain, settings);
        if (!exportPresetName.empty()) {
            const auto& entries = facade.presets().entries();
            const auto preset = std::find_if(entries.begin(), entries.end(), [this](const auto& entry) {
                return entry.name == exportPresetName;
            });
            if (preset == entries.end()) {
                error = QObject::tr("导出预设已不存在，请重新选择");
                return std::nullopt;
            }
            if (preset->kind != engine::ChainMode::List) {
                error = QObject::tr("节点预设尚未接入导出执行器");
                return std::nullopt;
            }
            engine::PresetLibrary::apply(*preset, settings);
        }
        settings.exportBitrateMbps = exportBitrateMbps;
        if (exportResolutionIndex >= 0) {
            settings.sr = exportResolutionIndex != 0;
            if (settings.sr) {
                switch (exportResolutionIndex) {
                case 1: settings.srTarget = pipeline::SrTarget::Qhd; break;
                case 2: settings.srTarget = pipeline::SrTarget::Uhd4K; break;
                case 3: settings.srTarget = pipeline::SrTarget::Uhd8K; break;
                case 4: settings.srTarget = pipeline::SrTarget::Uhd5K; break;
                case 5: settings.srTarget = pipeline::SrTarget::Uhd6K; break;
                case 6: settings.srTarget = pipeline::SrTarget::Uhd7K; break;
                default: break;
                }
            }
        }
        if (exportRateControl != sink::ExportRateControl::Cq && !exportBitrateMbps) {
            error = QObject::tr("CBR/VBR 需要指定码率");
            return std::nullopt;
        }
        if (const auto why = settings.validate(); !why.empty()) {
            veyra::log::warn("export-settings", std::format("invalid: {} bitrateMbps={}", why, settings.exportBitrateMbps));
            error = QObject::tr("导出设置无效：%1").arg(utf8Of(why));
            return std::nullopt;
        }
        if (const auto why = unavailableEffects(settings); !why.isEmpty()) {
            error = QObject::tr("导出设置无效：%1").arg(why);
            return std::nullopt;
        }
        if(settings.multiplier>1&&engine::presentSinkFrameGeneration(settings.frameGenerationBackend)){
            error=QObject::tr("XeSS 尚未提供可编码的补帧纹理，请为导出选择 FSR、DLSS 或 VFG 补帧");
            return std::nullopt;
        }
        return settings;
    }

    engine::ChainSession currentSession() const {
        auto current = session;
        current.active = chain.mode;
        current.initialized[size_t(chain.mode)] = true;
        current.configurations[size_t(chain.mode)] = engine::ChainConfiguration::capture(
            activeRuntime(), facade.pendingSettings(), nrIndex(), colourIndex());
        // Custom: the static HDR output tuning does not travel through the runtime
        // configuration yet, so take it straight from the draft globals at the point
        // the session is built. Without this the values took effect and then came
        // back as defaults on the next start.
        current.configurations[size_t(chain.mode)].hdrCurve = draftGlobals.hdrCurve;
        current.configurations[size_t(chain.mode)].hdrMetadata = draftGlobals.hdrMetadata;
        if (chain.mode == engine::ChainMode::Node) {
            auto document = std::make_shared<engine::NodeEditorDocument>();
            document->nodes = chain; document->layout = layout; document->globals = draftGlobals;
            current.configurations[size_t(chain.mode)].editor = std::move(document);
        }
        return current;
    }

    bool persistSession() {
        const auto current = currentSession();
        if (current == savedSession) return true;
        if (!facade.saveChainSession(current)) return false;
        session = savedSession = current;
        return true;
    }

    bool poll() {
        const auto before=facade.pendingSettings();
        auto frame = facade.poll();
        const auto restored=facade.pendingSettings();
        const bool fgRollback=before.frameGenerationBackend!=restored.frameGenerationBackend||before.multiplier!=restored.multiplier||before.vfgQuality!=restored.vfgQuality;
        const bool nrRollback=before.nrRuntime!=restored.nrRuntime;
        if(nrRollback) {
            for(auto* c:{&chain,&acceptedChain})for(uint32_t i=0;i<c->nodeCount;++i)
                if(c->nodes[i].type==engine::EffectType::NrEnhance)c->nodes[i].nr.runtime=restored.nrRuntime;
        }
        if(fgRollback){
            const auto restoreFg=[&](engine::EffectChain& value){
                if(restored.multiplier>1)value.fgMultiplier=restored.multiplier;
                for(uint32_t i=0;i<value.nodeCount;++i)
                    if(value.nodes[i].type==engine::EffectType::FrameGeneration)value.nodes[i].enabled=restored.multiplier>1;
            };
            restoreFg(chain);restoreFg(acceptedChain);
            if(draftGlobals.fgBackend==before.frameGenerationBackend)draftGlobals.fgBackend=restored.frameGenerationBackend;
            if(draftGlobals.vfgQuality==before.vfgQuality)draftGlobals.vfgQuality=restored.vfgQuality;
            options=engine::PlayerOptions::from(restored,engine::runtimeOrder(acceptedChain));
        }
        if (!frame.changed && haveSnapshot) return fgRollback||nrRollback;
        snapshot = frame.snapshot;
        publishedRevision = frame.revision;
        haveSnapshot = true;
        if (openingSource && openingSessionId != 0 && snapshot.sessionId == openingSessionId &&
            (snapshot.running || snapshot.image || snapshot.failed ||
             snapshot.transport == engine::TransportState::Empty ||
             snapshot.transport == engine::TransportState::Stopping))
            openingSource = false;
        return fgRollback||nrRollback;
    }

    // The chain is the source of truth for the stage settings; every commit
    // writes it back over the struct so the two can never disagree.
    bool commit(engine::EnhancementSettings settings) {
        // Custom: the static HDR output tuning belongs to neither transaction, so
        // keep the draft globals in sync in BOTH modes. Only the node branch below
        // refreshed them, which is why a change made in list mode took effect and
        // then came back as a default after a restart (the session capture reads the
        // tuning from the draft globals).
        draftGlobals.hdrCurve = settings.hdrCurve;
        draftGlobals.hdrMetadata = settings.hdrMetadata;
        if (chain.mode == engine::ChainMode::Node) {
            const auto previous = facade.pendingSettings();
            const auto oldGlobals = draftGlobals;
            draftGlobals = engine::ChainGlobalSettings::capture(settings);
            // Non-node controls (e.g. audio) may still change while a wire is
            // incomplete. Keep runtime globals separate from that transaction.
            // Custom: HDR brightness is one of those controls - it is not part of a
            // node edit, so the restore below must not put the previous value back
            // (field report: a new target peak took effect and then reverted on the
            // next start, because this line overwrote it before the session was
            // captured).
            facade.setPending(settings);
            if (revalidate(true, &previous)) return true;
            draftGlobals = oldGlobals; facade.setPending(previous);
            revalidate();
            return false;
        }
        const auto& runtime = activeRuntime();
        engine::fromChain(runtime, settings);
        if (const auto reason = unavailableEffects(settings); !reason.isEmpty()) {
            veyra::log::warn("effect-availability", reason.toStdString());
            return false;
        }
        if(!facade.applySettings(settings,engine::runtimeOrder(runtime)))return false;
        options = engine::PlayerOptions::from(settings,engine::runtimeOrder(runtime));
        return true;
    }

    bool revalidate(bool apply=false, const engine::EnhancementSettings* prior=nullptr) {
        auto projected = std::make_unique<engine::EffectChain>(chain);
        bool incomplete = false;
        if (chain.mode == engine::ChainMode::Node) {
            validation = layout.validate(chain);
            if (!validation.accepted) return false;
            // Even detached controls must obey the persistent parameter contract.
            auto check = std::make_unique<engine::ChainConfiguration>(
                engine::ChainConfiguration::capture(acceptedChain, facade.pendingSettings()));
            auto document = std::make_shared<engine::NodeEditorDocument>();
            document->nodes = chain; document->layout = layout; document->globals = draftGlobals; check->editor = document;
            if (!check->valid()) { validation = {false, "节点参数无效，未提交"}; return false; }
            validation = layout.project(chain, *projected);
            if (!validation.accepted) {
                uint32_t end = layout.inputNext;
                while (end > engine::NodeGraphLayout::Output)
                    end = layout.next[size_t(layout.indexOf(chain, end))];
                // Incomplete wires are editable/savable. A complete but illegal
                // path is rejected atomically by the caller, never auto-sorted.
                if (end != engine::NodeGraphLayout::Input) return false;
                *projected = acceptedChain; incomplete = true;
            }
        } else validation = engine::validateChain(chain);
        if(!validation.accepted && !incomplete)return false;
        const auto previous = prior ? *prior : facade.pendingSettings();
        auto s = facade.pendingSettings();
        if (chain.mode == engine::ChainMode::Node && !incomplete) {
            draftGlobals.apply(s);
            // A detached singleton owns editable values, not live resources.
            if (!projected->firstOf(engine::EffectType::SuperResolution)) {
                s.srTarget = previous.srTarget; s.videoSrQuality = previous.videoSrQuality;
                s.dlssSrModel = previous.dlssSrModel;
                s.srPrescale = previous.srPrescale; s.srOutput = previous.srOutput;
                s.dualSrEnabled=previous.dualSrEnabled;s.dualSrTarget=previous.dualSrTarget;s.dualSrQuality=previous.dualSrQuality;
            }
            if (!projected->firstOf(engine::EffectType::FrameGeneration)){
                s.frameGenerationBackend = previous.frameGenerationBackend;
                s.vfgQuality = previous.vfgQuality;
            }
        }
        engine::fromChain(*projected, s);
        if(const auto error=s.validate();!error.empty()){
            veyra::log::error("qml-settings",error);
            validation={false,"效果参数无效，未提交；详见诊断日志"};return false;
        }
        const auto order = engine::runtimeOrder(*projected);
        const bool changed = s != previous || order != engine::runtimeOrder(acceptedChain);
        if (apply && changed) {
            if (const auto reason = unavailableEffects(s); !reason.isEmpty()) {
                validation = {false, "当前硬件或包不支持所选效果，请查看组件页"};
                veyra::log::warn("effect-availability", reason.toStdString());
                return false;
            }
        }
        if(apply && changed && !facade.applySettings(s,order)){
            facade.setPending(previous);
            validation={false,"引擎未接受设置，已保留原状态"};
            return false;
        }
        if(!apply || !changed)facade.setPending(s);
        acceptedChain = *projected;
        options = engine::PlayerOptions::from(s,order);
        return true;
    }
#ifdef VEYRA_ENABLE_MOONLIGHT
    // --- PC streaming (Moonlight). Last, so they are destroyed first: the model joins its workers.
    QTimer* moonlightTimer = nullptr;
    bool moonlightWantCapture = false, moonlightStatsVisible = false, moonlightWasActive = false;
    int moonlightStatsTicks = 0;
    std::unique_ptr<ui::MoonlightInputCapture> moonlightCapture;
    std::unique_ptr<ui::MoonlightModel> moonlight;
#endif
#ifdef VEYRA_ENABLE_XBOX
    QTimer* xboxTimer = nullptr;
    bool xboxWasActive = false;
    int xboxStatsTicks = 0;
    std::unique_ptr<ui::XboxModel> xbox;   // last: joins its worker first
#endif
};


uint32_t primaryGpuVendor();
bool fsr4Possible();
const DXGI_ADAPTER_DESC1* engineAdapterDesc();
bool vfgHardwarePossible();
unsigned normalizeFgMultiplier(unsigned value, engine::FrameGenerationBackend backend, unsigned cap);
QString fgEffectId(engine::FrameGenerationBackend backend);
int xessCeiling(const engine::PlayerSnapshot& snapshot);
void ensureGdiplus();

} // namespace veyra::ui
