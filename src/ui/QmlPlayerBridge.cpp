#include "veyra/media/FFmpegDeinterlacer.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/gfx/NvidiaVrr.h"
#include "veyra/media/NetworkInput.h"
#include "veyra/ngx/DlssgLite.h"
#include "veyra/RuntimePaths.h"
#include "veyra/ui/GamePassthrough.h"
#include <QGuiApplication>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <shellapi.h>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include "veyra/ui/EffectAvailability.h"
#include "veyra/engine/FrameGenerationSelection.h"
#include "veyra/engine/DisplayFgBudget.h"
#include "veyra/ui/QmlExportQueueModel.h"
#include "veyra/ui/PlaylistModel.h"
#include "veyra/ui/UpdateChecker.h"
#include "veyra/ui/UiLanguage.h"
#include "veyra/media/BilibiliResolver.h"
#include "veyra/media/BilibiliApi.h"
#include "veyra/media/QrImage.h"
#include "veyra/media/DanmakuWindow.h"
// The sign-in QR is drawn from this encoder (MIT, Project Nayuki) rather than
// opening Bilibili's scan page in a browser.
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
namespace {
QString utf8Of(std::string_view s) { return QString::fromUtf8(s.data(), int(s.size())); }
QString utf8Of(const std::wstring& w) { return QString::fromWCharArray(w.c_str(), int(w.size())); }

// Where crash and device-loss hints go: next to the log, where the crash handler in
// apps/veyra-qml/main.cpp writes them too.
QString lastFailurePath() {
    return QFileInfo(qApp->property("veyraLogFile").toString()).absolutePath() + QStringLiteral("/veyra-last-failure.txt");
}

// What the user can switch off, for the injected components named in `modules`
// (RTSSHooks64.dll, see gfx::riskyInjections). Empty when none is known.
QString injectionAdvice(const QString& modules) {
    QStringList parts;
    if (modules.contains(QStringLiteral("RTSSHooks64.dll"), Qt::CaseInsensitive) && !qApp->property("veyraSoftwareUi").toBool())
        parts << QCoreApplication::translate("QmlPlayerBridge", "检测到 RivaTuner（小飞机 OSD）注入了 Veyra。请把 设置 → 监控软件兼容 设为“自动”并在小飞机运行时重启 Veyra：界面改用兼容绘制，OSD 只显示在视频上");
    return parts.join(QStringLiteral("；"));
}
// Messages and fixed table labels shown to the user, in the interface language
// (UiLanguage.h). Data that only passes through the UI - paths, device, preset and
// LUT names - keeps utf8Of(), and so do log lines.
QString uiText(const std::wstring& w) { return veyra::ui::i18n::text(w); }
QString uiText(std::string_view s) { return veyra::ui::i18n::text(QString::fromUtf8(s.data(), int(s.size()))); }
std::wstring wideOf(const QString& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()), size_t(s.size()));
}
QString mmss(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int total = int(seconds);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
void exportCompletionNotice(bool enabled,uint64_t event,const char* kind){
    const BOOL queued=enabled?MessageBeep(MB_OK):FALSE;
    veyra::log::info("export-notify",std::format("kind={} event={} soundEnabled={} systemSoundQueued={}",kind,event,enabled,queued!=FALSE));
}
bool exportFileClosed(const QString& path){
    HANDLE file=CreateFileW(wideOf(path).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    CloseHandle(file);return true;
}
} // namespace

static QVariantMap currentEffectCapabilities(bool refresh=false);
static QString unavailableEffects(const engine::EnhancementSettings& settings);
static QString disableUnavailableEffects(engine::ChainConfiguration& configuration);
static ui::EffectGpu effectGpu();

// B 站播放解析的后台结果。三个网络往返在 worker 线程跑完写这里，GUI tick 读走。
// 只含普通数据结构：worker 不能碰 Qt 对象或 impl_ 里的容器。
struct BiliPlayWorkerResult {
    media::BiliPlayUrls urls;
    bool reportOk = false;
    std::wstring reportBvid;
    long long reportCid = 0;
    int reportPlayed = 0;
    double resumeAt = -1.0;
    std::atomic<bool> done{false};
};

struct QmlPlayerBridge::Impl {
    // Per-layer on/off before the NR master switch turned them all off.
    std::vector<bool> nrMasterRestore;
    std::vector<bool> beautyMasterRestore;   // the same for the beauty group switch
    QQmlEngine* qmlEngine = nullptr;
    engine::EngineController& engine;
    PlayerUiFacade facade;

    // Set by the Bilibili resolver for the next open: the page title and the HTTP
    // headers the CDN requires. These live here rather than only in `options`
    // because PlayerOptions::from() rebuilds the whole struct on every settings
    // change and would wipe them; openPath() copies them back in before the open.
    std::wstring pendingTitle;
    std::wstring pendingHeaders;
    std::wstring pendingUserAgent;
    std::wstring pendingAudioPath;
    // True only between a Bilibili branch writing the pending* fields above and the
    // openPath call it makes, so openPath knows to keep them instead of clearing.
    bool bilibiliOpenPending = false;
    // True while bilibiliPlay is resolving. That call makes three blocking requests
    // (progress report, page lookup, playurl); a second click during them used to queue a
    // whole second set on the GUI thread, which is what made rapid switching feel stuck.
    bool biliPlayResolving = false;
    // QR sign-in key while a scan is pending.
    std::wstring bilibiliQrKey;
    // The URL the pending code encodes; the page renders it as a QR image.
    std::wstring bilibiliQrUrl;
    // Last Bilibili module failure, surfaced by bilibiliLastError().
    std::wstring bilibiliError;
    // 列表请求按「用途」分成四个独立槽位，互不顶替。
    // 打开详情页时会同时发起 qualities 和 related 两个请求，而它们只相隔一个计时器
    // 周期；共用一个槽位时后发的会覆盖先发的 future，先发的回复永远不会发出 ——
    // 表现就是画质菜单一直只有「自动」，尽管请求确实发出去并拿到了数据。
    // List requests get four independent slots grouped by purpose. Opening the detail
    // page starts qualities and related one timer tick apart; a single shared slot let
    // the later request overwrite the earlier one's future, so that reply never
    // arrived - which showed as a quality menu stuck on 自动 even though the request
    // ran and returned data.
    enum class ListSlot : size_t { Cards = 0, Qualities, Related, Detail, Count };
    // 一个排队中的请求：只记 kind 和它的代号，等前一个回收完再启动。
    // A queued request: just the kind and its generation, started after the previous
    // one in the same slot has been collected.
    struct PendingListRequest {
        QString kind;
        int generation = 0;
    };
    struct ListSlotState {
        std::future<QmlPlayerBridge::ListFetchResult> query;
        bool busy = false;
        QString kind;
        int generation = 0;
        std::vector<PendingListRequest> pending;
    };
    std::array<ListSlotState, size_t(ListSlot::Count)> biliListSlots{};
    int biliListGeneration = 0;
    // 把一个 kind 归到它该用的槽位。（QString::startsWith 不是 constexpr，故不加。）
    // Map a kind onto the slot it belongs to. (Not constexpr: startsWith is not.)
    static ListSlot listSlotOf(const QString& kind) {
        if (kind.startsWith(QLatin1String("qualities:"))) return ListSlot::Qualities;
        if (kind.startsWith(QLatin1String("related:"))) return ListSlot::Related;
        if (kind.startsWith(QLatin1String("detail:"))) return ListSlot::Detail;
        return ListSlot::Cards;
    }
    // 互动状态请求的代号：每发起一次递增，用来丢弃过期回复。
    // Generation counter for interaction-state requests, used to drop late replies.
    int biliInteractionGeneration = 0;
    // 通用异步任务的槽位：每个操作一个，互不覆盖。打开详情页时会同时发起
    // 互动状态、续播位置、评论等请求，共用一个槽位会让后发起的顶掉先发起的，
    // 那个请求就永远收不到结果。
    // One slot per Bilibili operation so requests never evict each other: opening the
    // detail page starts interaction state, resume point and comments together, and a
    // shared slot would make the last one cancel the others.
    struct BiliSlot {
        std::future<BiliTaskResult> query;
        bool busy = false;
    };
    // 12 个操作各一个槽位，索引就是枚举值，因此同时发起的请求互不覆盖。
    // One slot per operation, indexed by the enum value, so requests started together
    // never evict one another.
    // 大小取自枚举：以后再加操作不会漏掉一个槽位（原先写死 12，这次加
    // ReportProgress 才需要改）。越界会静默踩到相邻字段，所以用枚举算。
    // Sized from the enum so a new operation cannot silently fall off the end; the old
    // literal 12 had to be remembered by hand every time.
    std::array<BiliSlot, static_cast<size_t>(BiliOp::Count)> biliTaskSlots{};
    // 丹�� overlay: created lazily, only while comments are enabled.
    std::unique_ptr<media::DanmakuWindow> danmakuWindow;
    bool danmakuEnabled = false;
    bool danmakuFailed = false;
    // Last position fed to the engine, to notice a seek.
    double danmakuLastSeconds = -1.0;
    // Backup CDN retry: the resolved mirrors of the current video, which one is in
    // use, and how many attempts have already failed. Bilibili lists several mirrors
    // of the same rendition; a node that cannot be opened is simply the usual
    // reason a play fails, so the next one is tried instead of giving up.
    std::vector<std::wstring> biliMirrors;
    size_t biliMirrorIndex = 0;
    int biliRetriesLeft = 0;
    bool biliRetryPending = false;
    std::wstring biliVideoUrl;   // the URL currently open, for context on retry
    // Bumped by every openPath. A delayed mirror retry checks it and gives up when the
    // user opened something else in the meantime: without this the retry reopened a CDN
    // address over the user's new source, and because openPath had already cleared the
    // pending headers the retried open went out without a Referer (403).
    int biliOpenGen = 0;
    // What was last played from the Bilibili page, so the watch progress can be reported
    // to the account's history when the user moves on or closes the window. Reporting is
    // done at those two points only, not on a timer: every Bilibili request runs on the
    // GUI thread, and a 15 s heartbeat would freeze the interface a little each time.
    std::wstring biliWatchBvid;
    long long biliWatchCid = 0;
    // The last position seen while this video was actually playing, refreshed by the
    // 16 ms tick. Reported from here rather than read from the engine snapshot at flush
    // time: by then the engine has already torn the old session down and reports a
    // transport state and position that no longer describe what the user watched.
    double biliWatchLastPos = -1.0;
    double biliWatchDuration = 0.0;
    // Where the account left this video, read from the site's history before the open.
    // Negative means "no resume point": the tick below turns it into a seek once the
    // engine has a running session to seek.
    double biliResumeAt = -1.0;
    uint64_t biliResumeSession = 0;
    // 发起这次播放时引擎正在用的会话号。续播 seek 必须等到会话号变成别的值才发：
    // 点击后引擎还没开始新会话，impl_->snapshot 里仍是上一个视频的 position/duration，
    // 此时发出的 seek 打在正要被拆掉的旧会话上，永远不会被引擎取走（实测 5 次
    // 连续全丢），而 biliResumeAt 已被清空，于是新会话从头播。
    // The session id the engine was using when this play was requested. The resume seek
    // must wait until the session id changes: right after the click the engine has not
    // started the new session yet, so impl_->snapshot still holds the previous video's
    // position/duration. A seek issued then targets the session being torn down and is
    // never drained by the engine (measured: five in a row lost), while biliResumeAt has
    // already been cleared - so the new session plays from the beginning.
    uint64_t biliResumeAfterSession = 0;
    // 当前正在播的是 B 站视频。B 站给的是带时效签名的 CDN 地址，播完再拿它重开
    // 必然 403，所以结束后的「播放」不能重开 sourceLabel —— 那里记的就是那条过期
    // 地址。打开任何非 B 站来源时清掉。
    // The source currently playing is a Bilibili video. Bilibili hands out CDN URLs
    // with an expiry signature, so reopening sourceLabel after the video ends is
    // guaranteed to 403 - that label holds exactly that stale address. Cleared whenever
    // a non-Bilibili source is opened.
    bool biliSourceActive = false;
    // 重播身份：视频播完后点「播放」要重新解析地址，所以必须记住「要播什么」，
    // 而不只是「上次从哪播」—— 后者是带时效签名的地址，过期就 403。
    // Replay identity: after a video ends, Play has to resolve a fresh address, so the
    // bridge remembers *what* to play rather than *where it played from* - the latter is
    // a signed URL that expires and then only 403s.
    QString biliReplayBvid;
    long long biliReplayCid = 0;
    int biliReplayQuality = 0;

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
    bool holdKeyDown = false;
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
    // 自动选择倍率：只在每个会话刚开始播放时决定一次，播放中不再改动。
    // 换档必须重建 GPU 管线（缓冲区随倍率变），播放中做会丢补帧历史并卡顿，
    // 所以这里只在 session 首次进入 running 时计算（用户 2026-10-24 选定）。
    uint64_t fgAutoSession = 0;       // 已为哪个会话决定过档位
    // 打开片源前已经按元数据定过档：首帧后的决策看到它就跳过，避免二次重建管线。
    bool fgAutoPlanned = false;
    // B 站播放的解析工作放到后台线程。三个同步网络往返（上报进度 / 解析地址 /
    // 读续播位置）原本全在 GUI 线程，WinHTTP 每个 15 秒超时 —— 网络一慢主线程就
    // 静止十几秒（现场 2026-10-24：22.9 秒）。worker 只做请求，结果回到 GUI 线程
    // 才推进播放状态，所以状态仍归主线程所有。
    struct BiliPlayJob {
        std::future<void> query;
        std::shared_ptr<BiliPlayWorkerResult> result;   // worker 写，tick 读
        uint64_t generation = 0;      // 当前请求的代号，新请求会顶掉旧的
        bool busy = false;
        bool resolving = false;       // 已有一个在解析（保留原有的防重入语义）
        QString bvid;
        long long cid = 0;
        std::wstring pageTitle;
        int qn = 0;
        bool fromStart = false;
        bool plannedResume = false;
    };
    BiliPlayJob biliPlayJob;
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
    gfx::VrrState idleVrr{};
    std::chrono::steady_clock::time_point idleVrrQuery{};
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
                if(captureAudio[k].path==capturePrefs.audioPath&&captureAudio[k].wasapi==(capturePrefs.audioMode==source::kCaptureAudioWasapi)){
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
            capturePrefs.audioMode=audio?(audio->wasapi?source::kCaptureAudioWasapi:0):captureAudioChoice;
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

namespace {
bool prepareStartupPreset(const engine::PresetEntry& entry, engine::ChainSession& session,
                          engine::EnhancementSettings& settings, QString& error) {
    if (!session.select(entry.kind)) {
        error = QObject::tr("默认预设的编辑模式无效");
        return false;
    }
    auto& configuration = session.configurations[size_t(entry.kind)];
    configuration.apply(settings);
    if (entry.kind == engine::ChainMode::List) {
        auto chain = configuration.chain;
        const auto result = engine::PresetLibrary::applyToChain(entry, chain, settings);
        if (!result.accepted) { error = uiText(result.message); return false; }
        configuration = engine::ChainConfiguration::capture(chain, settings);
    } else {
        engine::NodeEditorDocument document;
        if (configuration.editor) document = *configuration.editor;
        else {
            document.nodes = configuration.chain;
            const auto result = document.layout.initialize(document.nodes);
            if (!result.accepted) { error = uiText(result.message); return false; }
        }
        if (!document.globals)
            document.globals = static_cast<const engine::ChainGlobalSettings&>(configuration);
        auto draftSettings = settings;
        const auto result = engine::PresetLibrary::applyToEditor(entry, document, draftSettings);
        if (!result.accepted) { error = uiText(result.message); return false; }
        auto runtime = configuration.chain;
        const auto projected = document.layout.project(document.nodes, runtime);
        if (!projected.accepted) {
            uint32_t end = document.layout.inputNext;
            while (end > engine::NodeGraphLayout::Output)
                end = document.layout.next[size_t(document.layout.indexOf(document.nodes, end))];
            if (end != engine::NodeGraphLayout::Input) { error = uiText(projected.message); return false; }
            runtime = configuration.chain;
        } else {
            document.globals->apply(settings);
            engine::fromChain(runtime, settings);
        }
        settings.audioSync = draftSettings.audioSync;
        settings.audioOffsetMs = draftSettings.audioOffsetMs;
        settings.content = draftSettings.content;
        configuration = engine::ChainConfiguration::capture(runtime, settings);
        configuration.editor = std::make_shared<engine::NodeEditorDocument>(std::move(document));
    }
    if (!session.valid() || !settings.validate().empty()) {
        error = QObject::tr("默认预设参数无效，已保留恢复会话");
        return false;
    }
    return true;
}
} // namespace

// The first hardware adapter, the one the engine opens (gfx adapter[0]): 0x10DE NVIDIA,
// 0x1002 AMD, 0x8086 Intel, 0 unknown.
static uint32_t primaryGpuVendor() {
    return effectGpu().vendor;
}
// FSR 4 ML frame generation needs an AMD RX 9000; on other GPUs the provider crashed the
// whole program (field report 2026-10-01). Known non-AMD adapters are refused up front.
static bool fsr4Possible() { return currentEffectCapabilities().value("fsr4").toMap().value("available").toBool(); }
// The adapter the engine opens: the user's compute GPU when one is chosen
// (gfx::preferredAdapterLuid) and usable, else the first D3D12 hardware
// adapter in DXGI's high-performance order, as D3D12DeviceContext picks it.
// UI thread only; cached per preference.
static const DXGI_ADAPTER_DESC1* engineAdapterDesc(){
    static uint64_t cachedFor=~0ull;static DXGI_ADAPTER_DESC1 cached{};static bool found=false;
    const uint64_t preferred=gfx::preferredAdapterLuid();
    if(cachedFor==preferred)return found?&cached:nullptr;
    cachedFor=preferred;found=false;
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return nullptr;
    for(int pass=preferred?0:1;pass<2&&!found;++pass)
        for(UINT i=0;;++i){Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if(FAILED(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)))||!adapter)break;
            DXGI_ADAPTER_DESC1 d{};
            if(FAILED(adapter->GetDesc1(&d))||(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            const uint64_t luid=uint64_t(uint32_t(d.AdapterLuid.HighPart))<<32|d.AdapterLuid.LowPart;
            if(pass==0&&luid!=preferred)continue;
            if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),nullptr)))continue;
            cached=d;found=true;break;
        }
    return found?&cached:nullptr;
}
// The menu checks the same adapter as the engine. The native backend then
// verifies CUDA compute capability and the exact adapter LUID.
static bool vfgHardwarePossible(){
    const auto* d=engineAdapterDesc();
    if(!d)return false;
    const auto name=QString::fromWCharArray(d->Description);
    return d->VendorId==0x10DE&&(name.contains(QStringLiteral("GeForce RTX 40"),Qt::CaseInsensitive)||
        name.contains(QStringLiteral("GeForce RTX 50"),Qt::CaseInsensitive)||name.contains(QStringLiteral("Ada"),Qt::CaseInsensitive)||name.contains(QStringLiteral("Blackwell"),Qt::CaseInsensitive));
}
static unsigned normalizeFgMultiplier(unsigned value,engine::FrameGenerationBackend backend,unsigned cap){
    value=std::clamp(value,2u,cap);
    return backend==engine::FrameGenerationBackend::Dlss&&value==5?4u:value;
}
bool QmlPlayerBridge::amdNrGpu() const { return primaryGpuVendor()==0x1002; }

static ui::EffectGpu effectGpu() {
    ui::EffectGpu result;
    const auto* d = engineAdapterDesc();
    if (!d) return result;
    const auto name = QString::fromWCharArray(d->Description);
    result.vendor = d->VendorId;
    result.deviceId = d->DeviceId;
    result.rtx = name.contains("RTX", Qt::CaseInsensitive);
    result.blackwell = name.contains("RTX 50", Qt::CaseInsensitive) || name.contains("Blackwell", Qt::CaseInsensitive);
    result.ada = name.contains("RTX 40", Qt::CaseInsensitive) || name.contains("Ada", Qt::CaseInsensitive);
    result.rx9000 = name.contains("RX 90", Qt::CaseInsensitive);
    return result;
}

static QVariantMap scanEffectCapabilities() {
    using H = ui::HardwareEffect;
    const auto gpu = effectGpu();
    const auto nr = runtime::localRuntimeDirectory();
    const auto data = runtime::localDataDirectory();
    auto exists = [](const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); };
    const auto fsr = data / "amd/fidelityfx";
    const bool fsrLoader = exists(fsr / "amd_fidelityfx_loader_dx12.dll");
    QVariantMap out;
    auto add = [&](const QString& id, H hardware, bool files, const QString& requirement) {
        const bool supported = ui::hardwareSupports(hardware, gpu);
        out[id] = QVariantMap{{"available", supported && files}, {"reason", !supported ? requirement : !files ? QObject::tr("当前包缺少此功能的运行组件") : QString{}}};
    };
    add("nr2", H::NvidiaNrSf, exists(nr / "nr-ampere/nvngx_dlssnr.dll"), QObject::tr("需要 NVIDIA RTX 显卡"));
    out["nr0"]=out["nr2"]; // Retired preset ID; no retired runtime is loaded.
    out["nrAuto"]=QVariantMap{{"available",gpu.vendor==0x10DE&&gpu.deviceId==0x2f04&&out.value("nr0").toMap().value("available").toBool()},
        {"reason",gpu.deviceId!=0x2f04?QObject::tr("自动 NR 当前仅验证 RTX 5070"):!out.value("nr0").toMap().value("available").toBool()?QObject::tr("当前包缺少此功能的运行组件"):QString{}}};
    add("nr3", H::NvidiaNr50, exists(nr / "nr-original/nvngx_dlssnr.dll"), QObject::tr("需要 NVIDIA RTX 50 / Blackwell"));
    add("nr4", H::AmdNr, exists(nr / "../amd-nr/LmxxfNrRuntime.dll"), QObject::tr("需要 AMD RX 9000"));
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) && !exists(std::filesystem::path(systemDir) / "amdhip64_7.dll") && !GetModuleHandleW(L"amdhip64_7.dll")) {
        auto cap = out.value("nr4").toMap();
        if (cap.value("available").toBool()) { cap["available"] = false; cap["reason"] = QObject::tr("需要 AMD HIP 7 驱动运行组件"); out["nr4"] = cap; }
    }
    add("sr0", H::Nvidia, exists(nr / "nvngx_dlss.dll"), QObject::tr("需要 NVIDIA RTX 显卡"));
    for (int i = 1; i <= 4; ++i) add("sr" + QString::number(i), H::Nvidia, exists(nr / "nvngx_vsr.dll"), QObject::tr("需要 NVIDIA RTX 显卡"));
    add("sr5", H::CrossVendor, fsrLoader && exists(fsr / "amd_fidelityfx_upscaler_dx12.dll"), QObject::tr("需要支持 DirectX 12 的硬件显卡"));
    add("dlss", H::Nvidia, exists(nr / "nvngx_dlssg.dll"), QObject::tr("需要 NVIDIA RTX 显卡"));
    add("vfg", H::Vfg, pipeline::VfgBackend::runtimeAvailable(nr.wstring()), QObject::tr("需要 NVIDIA RTX 40 / 50（Ada / Blackwell）"));
    add("xess", H::CrossVendor, exists(data / "intel/experimental/libxess_fg.dll") && exists(data / "intel/experimental/libxell.dll"), QObject::tr("需要支持 DirectX 12 的硬件显卡"));
    add("fsr3", H::CrossVendor, fsrLoader && exists(fsr / "amd_fidelityfx_framegeneration_dx12.dll"), QObject::tr("需要支持 DirectX 12 的硬件显卡"));
    add("fsr4", H::Fsr4, fsrLoader && exists(fsr / "amd_fidelityfx_framegeneration_dx12.dll"), QObject::tr("需要 AMD RX 9000"));
    add("hdr", H::Nvidia, exists(nr / "nvngx_truehdr.dll"), QObject::tr("需要 NVIDIA RTX 显卡"));
    add("flow0", H::Nvidia, true, QObject::tr("需要 NVIDIA RTX 显卡及 NVOF 驱动"));
    add("flow1", H::CrossVendor, true, QObject::tr("需要支持 DirectX 12 的硬件显卡"));
    return out;
}
// All callers are on the bridge/UI thread. Snapshot reads must not perform
// filesystem probes or rebuild capability maps. Refresh on actions, language
// changes and a slow timer so user-replaced runtime files remain discoverable.
static QVariantMap currentEffectCapabilities(bool refresh) {
    static QVariantMap cached;
    static QString language;
    const auto currentLanguage=i18n::current();
    if(refresh||cached.isEmpty()||language!=currentLanguage){
        cached=scanEffectCapabilities();language=currentLanguage;
    }
    return cached;
}
static QString fgEffectId(engine::FrameGenerationBackend backend) {
    switch (backend) {
    case engine::FrameGenerationBackend::Dlss: return "dlss";
    case engine::FrameGenerationBackend::XeSS: return "xess";
    case engine::FrameGenerationBackend::Fsr: return "fsr3";
    case engine::FrameGenerationBackend::Fsr4: return "fsr4";
    case engine::FrameGenerationBackend::Vfg: return "vfg";
    }
    return {};
}
static QString unavailableEffects(const engine::EnhancementSettings& settings) {
    const auto caps = currentEffectCapabilities(true);
    QStringList requested;
    if (settings.nr) requested << "nr" + QString::number(int(engine::currentNrRuntime(settings.nrRuntime)));
    if (settings.sr) requested << "sr" + QString::number(settings.videoSrQuality);
    if (settings.multiplier > 1) requested << fgEffectId(settings.frameGenerationBackend);
    if (settings.videoHdr.enabled) requested << "hdr";
    for (const auto& id : requested) {
        const auto cap = caps.value(id).toMap();
        if (!cap.value("available").toBool()) return id + ": " + cap.value("reason").toString();
    }
    return {};
}
static QString disableUnavailableEffects(engine::ChainConfiguration& configuration) {
    const auto caps = currentEffectCapabilities();
    const auto available = [&](const char* id) { return caps.value(id).toMap().value("available").toBool(); };
    const engine::FrameGenerationAvailability fgAvailable{
        available("dlss"),available("xess"),available("fsr3"),available("fsr4"),available("vfg")};
    QStringList reasons;
    const auto sanitize = [&](engine::EffectChain& chain, engine::ChainGlobalSettings& globals) {
        // Also migrate disabled stages: keeping an unavailable DLSS selection
        // left the AMD dropdown at DLSS even though a fresh profile chose FSR.
        const auto savedBackend = globals.fgBackend;
        globals.fgBackend = engine::restoredFrameGenerationBackend(savedBackend,fgAvailable);
        if (globals.fgBackend != savedBackend) {
            if (engine::fsrFrameGeneration(globals.fgBackend)) chain.fgMultiplier=2;
            else if (globals.fgBackend==engine::FrameGenerationBackend::XeSS)
                chain.fgMultiplier=normalizeFgMultiplier(chain.fgMultiplier,globals.fgBackend,2);
            else chain.fgMultiplier=normalizeFgMultiplier(chain.fgMultiplier,globals.fgBackend,6);
            veyra::log::warn("fg-default",std::format("unavailable saved backend={} selected={} rememberedMultiplier={} enabled flags preserved",
                engine::frameGenerationBackendName(savedBackend),engine::frameGenerationBackendName(globals.fgBackend),chain.fgMultiplier));
        }
        for (uint32_t i = 0; i < chain.nodeCount; ++i) {
            auto& node = chain.nodes[i];
            QString id;
            switch (node.type) {
            case engine::EffectType::NrEnhance: id = "nr" + QString::number(int(engine::currentNrRuntime(node.nr.runtime))); break;
            case engine::EffectType::SuperResolution: id = "sr" + QString::number(globals.videoSrQuality); break;
            case engine::EffectType::FrameGeneration: id = fgEffectId(globals.fgBackend); break;
            case engine::EffectType::VideoHdr: id = "hdr"; break;
            default: break;
            }
            if (id.isEmpty() || !node.enabled) continue;
            const auto cap = caps.value(id).toMap();
            if (!cap.value("available").toBool()) {
                node.enabled = false;
                reasons << id + ": " + cap.value("reason").toString();
            }
        }
        if (globals.opticalFlowBackend == engine::OpticalFlowBackend::Nvidia &&
            !caps.value("flow0").toMap().value("available").toBool())
            globals.opticalFlowBackend = engine::OpticalFlowBackend::AmdFidelityFx;
    };
    sanitize(configuration.chain, configuration);
    if (configuration.editor) {
        auto editor = std::make_shared<engine::NodeEditorDocument>(*configuration.editor);
        auto globals = editor->globals.value_or(static_cast<const engine::ChainGlobalSettings&>(configuration));
        sanitize(editor->nodes, globals);
        editor->globals = globals;
        configuration.editor = std::move(editor);
    }
    reasons.removeDuplicates();
    return reasons.join("; ");
}
QVariantMap QmlPlayerBridge::effectCapabilities() const { return currentEffectCapabilities(); }
QVariantMap QmlPlayerBridge::effectAvailability(const QString& id) const {
    const auto& s = settings();
    const QString key = id == "nr" ? "nr" + QString::number(int(engine::currentNrRuntime(s.nrRuntime))) :
        id == "sr" ? "sr" + QString::number(s.videoSrQuality) : id == "video-hdr" ? QString("hdr") :
        id == "frame-generation" ? fgEffectId(s.frameGenerationBackend) :
        id == "dlss-fg" ? QString("dlss") : id == "xess-fg" ? QString("xess") :
        id == "fsr3-fg" ? QString("fsr3") : id == "fsr4-fg" ? QString("fsr4") : id == "vfg-fg" ? QString("vfg") : id;
    const auto caps = effectCapabilities();
    return caps.contains(key) ? caps.value(key).toMap() : QVariantMap{{"available", true}, {"reason", QString{}}};
}
QVariantList QmlPlayerBridge::nrRuntimeChoices() const {
    QVariantList choices;
    const auto caps = effectCapabilities();
    for (const auto& entry : std::initializer_list<std::pair<int, QString>>{{2, "RTX 20–50 · SF 310.9.16"}, {3, tr("RTX 50 · NVIDIA 原版")}, {4, tr("RX 9000 · lmxxf（实验）")}}) {
        const auto cap = caps.value("nr" + QString::number(entry.first)).toMap();
        choices << QVariantMap{{"id", QString::number(entry.first)}, {"label", entry.second}, {"disabled", !cap.value("available").toBool()}, {"note", cap.value("reason")}};
    }
    return choices;
}
QVariantList QmlPlayerBridge::srBackendChoices() const {
    QVariantList choices;
    const auto caps = effectCapabilities();
    for (const auto& entry : std::initializer_list<std::pair<int, QString>>{{0, "DLSS SR"}, {4, tr("RTX 视频超分")}, {5, effectGpu().vendor == 0x1002 ? tr("FSR 3.1 / 4 · 自动兼容") : QString("FSR 3.1")}}) {
        const auto cap = caps.value("sr" + QString::number(entry.first)).toMap();
        choices << QVariantMap{{"id", QString::number(entry.first)}, {"label", entry.second}, {"disabled", !cap.value("available").toBool()}, {"note", cap.value("reason")}};
    }
    return choices;
}

QmlPlayerBridge::QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory,
                                QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(engine, std::move(dataDirectory))) {
    auto* checker = new UpdateChecker(version(), this);
    updates_ = checker;
    connect(checker, &UpdateChecker::notice, this, &QmlPlayerBridge::notice);
    impl_->playlist = new PlaylistModel(utf8Of((impl_->dataDir / L"playlist.v1.json").wstring()), this);
    connect(impl_->playlist, &PlaylistModel::playRequested, this, [this](const QString& path) {
        openPath(path);
        // Explicit playlist selection starts at the beginning; opening from
        // recent files retains the existing resume-position preference.
        impl_->resumeSession = 0;
    });
    connect(impl_->playlist, &PlaylistModel::error, this, [this](const QString& text) {
        veyra::log::warn("playlist", text.toStdString()); emit notice(text, true);
    });
    // The compute GPU decides which effects exist, so it is settled before
    // anything below samples the capabilities or restores the saved chain.
    impl_->loadPrefs();
    const auto gameKeys=shortcuts();
    impl_->game.setHotkeys(ui::gameKey(QKeySequence(gameKeys.value("gamePanel").toString(),QKeySequence::PortableText)),
                          ui::gameKey(QKeySequence(gameKeys.value("gameExit").toString(),QKeySequence::PortableText)));
    impl_->enumerateGpuAdapters();
    impl_->resolveGpuMonitor();
    impl_->applyComputeGpu();
    connect(this,&QmlPlayerBridge::snapshotChanged,this,
        [this,ceiling=fgMaxMultiplier(),text=fgProviderText()]() mutable {
            const auto nextCeiling=fgMaxMultiplier();const auto nextText=fgProviderText();
            if(nextCeiling==ceiling&&nextText==text)return;
            ceiling=nextCeiling;text=nextText;emit fgChoicesChanged();
        });
    connect(this,&QmlPlayerBridge::settingsChanged,this,&QmlPlayerBridge::fgChoicesChanged);
    auto* capabilityTimer=new QTimer(this);
    connect(capabilityTimer,&QTimer::timeout,this,[this,reported=currentEffectCapabilities(),priorityRevision=uint64_t(-1)]() mutable {
        if(gfx::gpuPriorityStatus().state==gfx::GpuPriorityState::Pending)gfx::applyRequestedGpuPriority();
        const auto priority=gfx::gpuPriorityStatus();
        if(priority.revision!=priorityRevision){priorityRevision=priority.revision;emit preferencesChanged();}
        const auto next=currentEffectCapabilities(true);
        // An action may have refreshed the cache before this tick. Compare with
        // the last reported map so that QML still learns about that change.
        if(next==reported)return;
        reported=next;emit effectCapabilitiesChanged();emit fgChoicesChanged();
    });
    capabilityTimer->start(2000);
    applyPreference(QStringLiteral("gpuPriority"));
    updateLiveCompanionCompat();
    applyPreference(QStringLiteral("prewarmEnhancement"));
    impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));
    connect(&impl_->exportQueue,&QmlExportQueueModel::changed,this,&QmlPlayerBridge::exportChanged);
    if (qApp) qApp->installEventFilter(this);
    // 游戏模式. The native filter also keeps windows shown during a monitor capture
    // out of that capture, so it is installed for every run.
    if (qApp) qApp->installNativeEventFilter(&impl_->game);
    impl_->game.onEvent = [this](ui::GamePassthrough::Event event) {
        using E = ui::GamePassthrough::Event;
        switch (event) {
        case E::PanelHotkey: emit gameHotkey(0); break;
        case E::ExitHotkey:
        case E::ExitUnavailable:
            // Restore input before QML processes its fullscreen notification.
            impl_->gameRefused = true;
            impl_->gameInputShown = impl_->passthroughShown = false;
            emit gameModeChanged();
            impl_->game.apply(ui::GamePassthrough::Mode::Off);
            if (event == E::ExitHotkey) emit gameHotkey(1);
            else emit notice(tr("游戏模式键盘监听不可用，已退出鼠标穿透；请退出全屏后重试"), true);
            break;
        case E::HangReleased:
            // Publish Off before the foreground goes back to Veyra; stay out of
            // click-through until fullscreen is left.
            impl_->gameRefused = true;
            impl_->gameInputShown = impl_->passthroughShown = false;
            emit gameModeChanged();
            impl_->game.apply(ui::GamePassthrough::Mode::Off);
            emit gameModeStopped(tr("界面卡住超过 3 秒，已退出游戏模式")); break;
        }
    };
    auto* gameBeat = new QTimer(this);
    connect(gameBeat, &QTimer::timeout, this, [this] {
        impl_->game.heartbeat();
        updateGameGeometry();
    });
    gameBeat->start(250);
#ifdef VEYRA_ENABLE_MOONLIGHT
    setupMoonlight();
#endif
#ifdef VEYRA_ENABLE_XBOX
    setupXbox();
#endif
    // Before anything opens: the renderer reads the output choice at start().
    applyPreference(QStringLiteral("language"));
    applyPreference(QStringLiteral("audioDevice"));
    applyPreference(QStringLiteral("audioForceStereo"));
    applyPreference(QStringLiteral("magewellLowLatency"));
    const bool presetsLoaded = impl_->facade.importLegacyStores();
    const auto presetNotice = uiText(impl_->facade.error());
    if (!presetNotice.isEmpty())
        QTimer::singleShot(0, this, [this, presetNotice, presetsLoaded] { emit notice(presetNotice, !presetsLoaded); });
    // Recent files, the last capture session and the shell preferences are saved
    // on every change; without this load they were written but never read back,
    // so every restart came up with an empty history. A damaged file is logged
    // and the defaults stand.
    if (!impl_->facade.loadPreferences())
        veyra::log::error("ui", "preferences not loaded: " + utf8Of(impl_->facade.error()).toStdString());
    auto restored = impl_->session;
    QString restoreError;
    if (!impl_->facade.loadChainSession(restored)) {
        restoreError = uiText(impl_->facade.error());
    } else {
        const auto migrationNotice = uiText(impl_->facade.error());
        if (!migrationNotice.isEmpty())
            QTimer::singleShot(0, this, [this, migrationNotice] { emit notice(migrationNotice, false); });
        const auto savedConfiguration = restored;
        QStringList disabled;
        for (auto& configuration : restored.configurations) {
            const auto reason = disableUnavailableEffects(configuration);
            if (!reason.isEmpty()) disabled << reason;
        }
        if (!disabled.isEmpty()) {
            const auto message = tr("已关闭当前硬件或包不支持的效果，参数仍保留：") + disabled.join("; ");
            veyra::log::warn("effect-availability", message.toStdString());
            QTimer::singleShot(0, this, [this, message] { emit notice(message, false); });
        }
        auto settings = impl_->facade.pendingSettings();
        const auto& configuration = restored.configurations[size_t(restored.active)];
        configuration.apply(settings);
        const auto previous = impl_->facade.pendingSettings();
        if (settings.validate().empty() && impl_->facade.applySettings(settings,engine::runtimeOrder(configuration.chain))) {
            impl_->session = restored;
            // Persist a repaired backend as well as the active mode. Otherwise
            // every restart reloads the unavailable DLSS value again.
            impl_->savedSession = savedConfiguration;
            impl_->restoreConfiguration(configuration);
            impl_->options = engine::PlayerOptions::from(settings,engine::runtimeOrder(impl_->activeRuntime()),&impl_->options);
            impl_->revalidate();
        } else {
            impl_->facade.setPending(previous);
            restoreError = tr("模式会话未被引擎接受，保留初始状态");
        }
    }
    // Custom: the Dolby Vision follow is a viewing preference rather than part of
    // the grade, so it is stored in the shell preferences and re-applied here -
    // after the restored session has reached the engine, so it wins over a value
    // the session round trip never carried.
    if (impl_->prefBool("doviFollow", false) && !settings().doviFollow) {
        auto followed = settings();
        followed.doviFollow = true;
        commitHdrTuning(std::move(followed));
    }
    // The P7 composite is its own preference. It used to be restored inside the
    // doviFollow branch above, so a profile without that flag could never turn it
    // on no matter what the preferences said.
    if (impl_->prefBool("doviP7Compose", false) && !settings().doviP7Compose) {
        auto composed = settings();
        composed.doviP7Compose = true;
        commitHdrTuning(std::move(composed));
    }
    bool defaultFlowApplied = false;
    if (!restoreError.isEmpty()) {
        veyra::log::error("ui-session", restoreError.toStdString());
        QTimer::singleShot(0, this, [this, restoreError] { emit notice(restoreError, true); });
    } else if (presetsLoaded) {
        const auto defaultIndex = impl_->facade.presets().defaultIndex();
        if (defaultIndex) {
            const auto before = impl_->currentSession();
            auto candidate = before;
            auto settings = impl_->facade.pendingSettings();
            QString error;
            const auto& entry = impl_->facade.presets().entries()[*defaultIndex];
            if (!prepareStartupPreset(entry, candidate, settings, error)) {
                veyra::log::error("ui-preset", error.toStdString());
            } else {
                auto& configuration = candidate.configurations[size_t(candidate.active)];
                const auto disabled = disableUnavailableEffects(configuration);
                configuration.apply(settings);
                if (!disabled.isEmpty()) veyra::log::warn("effect-availability", "default preset disabled: " + disabled.toStdString());
            if (!impl_->facade.applySettings(settings,
                                                    engine::runtimeOrder(candidate.configurations[size_t(candidate.active)].chain))) {
                error = tr("引擎未接受默认预设，已保留恢复会话");
            } else {
                impl_->session = candidate;
                impl_->restoreConfiguration(candidate.configurations[size_t(candidate.active)]);
                impl_->revalidate();
                impl_->options = engine::PlayerOptions::from(settings, engine::runtimeOrder(impl_->activeRuntime()),&impl_->options);
                defaultFlowApplied = entry.flow.has_value() &&
                    (entry.contents & engine::presetContentMask(engine::PresetContent::Flow));
                if (defaultFlowApplied) {
                    impl_->prefs[QStringLiteral("contentRate")] = int(settings.content);
                    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "default preset content cadence not saved");
                }
                if (candidate != before && !impl_->facade.saveChainSession(candidate))
                    error = tr("默认预设已应用，但会话保存失败：") + uiText(impl_->facade.error());
                else impl_->savedSession = candidate;
            }
            }
            if (!error.isEmpty())
                QTimer::singleShot(0, this, [this, error] { emit notice(error, true); });
        }
    }
    // These controls are global, outside either effect chain. Restore them
    // from shell preferences after the session; old profiles keep defaults.
    {
        auto s = impl_->facade.pendingSettings();
        const auto& p = impl_->prefs;
        bool changed = false;
        if (p.contains(QStringLiteral("audioSync"))) {
            const int m = p.value(QStringLiteral("audioSync")).toInt();
            if (m >= 0 && m <= 2 && int(s.audioSync) != m) { s.audioSync = static_cast<engine::AudioSyncMode>(m); changed = true; }
        }
        if (p.contains(QStringLiteral("audioOffsetMs"))) {
            const int ms = p.value(QStringLiteral("audioOffsetMs")).toInt();
            if (ms >= -250 && ms <= 250 && s.audioOffsetMs != ms) { s.audioOffsetMs = ms; changed = true; }
        }
        if (p.contains(QStringLiteral("edgeAegis"))) {
            const bool on = p.value(QStringLiteral("edgeAegis")).toBool();
            if (s.fgMotionRefine != on) { s.fgMotionRefine = on; changed = true; }
        }
        if (p.contains(QStringLiteral("dlssgPerformance"))) {
            const bool on = p.value(QStringLiteral("dlssgPerformance")).toBool();
            if (s.dlssgLite != on) { s.dlssgLite = on; changed = true; }
        }
        if (!defaultFlowApplied && p.contains(QStringLiteral("contentRate"))) {
            const int rate = p.value(QStringLiteral("contentRate")).toInt();
            if (rate >= 0 && rate <= int(engine::ContentRate::Capture60To30) && int(s.content) != rate) { s.content = static_cast<engine::ContentRate>(rate); changed = true; }
        }
        auto restoreHold = [&](const char* key, float& target) {
            if (!p.contains(QLatin1String(key))) return;
            bool ok = false;
            const double value = p.value(QLatin1String(key)).toDouble(&ok);
            if (ok && std::isfinite(value) && value >= 0 && value <= 1 && target != float(value)) {
                target = float(value); changed = true;
            }
        };
        restoreHold("nrHoldStrength", s.nrHoldStrength);
        restoreHold("nrHoldTolerance", s.nrHoldTolerance);
        for(const auto& pair : {std::pair{"captureFlipVertical",&s.captureFlipVertical},std::pair{"captureForceSdr",&s.forceSdrPreview}})
            if(p.contains(QLatin1String(pair.first))){const bool value=p.value(QLatin1String(pair.first)).toBool();changed|=*pair.second!=value;*pair.second=value;}
        if(p.contains("captureAudioIngress")){const int value=p.value("captureAudioIngress").toInt();if(value>=0&&value<=2){changed|=int(s.captureAudio)!=value;s.captureAudio=static_cast<engine::CaptureAudioIngress>(value);}}
        if(p.contains("captureBufferMode")){const int value=p.value("captureBufferMode").toInt();if(value>=0&&value<=2){changed|=int(s.captureBuffer)!=value;s.captureBuffer=static_cast<source::CaptureBufferMode>(value);}}
        if (changed && !impl_->commit(std::move(s))) veyra::log::warn("ui-prefs", "saved global controls not accepted");
    }
    // Presentation (低延迟队列 / 显示同步 / 输出上限) and the picture aspect.
    {
        const auto saved = impl_->prefs.value(QStringLiteral("presentation")).toMap();
        auto& pr = impl_->presentation;
        pr.hdrFgCompatibility = impl_->prefBool("hdrFgCompatibility", false);
        if (!saved.isEmpty()) {
            pr.enabled = saved.value(QStringLiteral("lowQueue")).toBool();
            pr.display = static_cast<engine::DisplaySync>(std::clamp(saved.value(QStringLiteral("display")).toInt(), 0, 2));
            pr.outputRate = static_cast<engine::OutputRateMode>(std::clamp(saved.value(QStringLiteral("outputRate")).toInt(), 0, 2));
            pr.customFps = std::clamp(saved.value(QStringLiteral("customFps"), 60.0).toDouble(), 1.0, 1000.0);
            pr.variableRefresh = saved.value(QStringLiteral("vrr")).toBool();
        }
        if (pr.valid()) impl_->engine.requestPresentation(pr);
        impl_->aspectMode = std::clamp(impl_->prefs.value(QStringLiteral("aspect")).toInt(), 0, 6);
    }
    impl_->sessionSaveTimer = new QTimer(this);
    impl_->sessionSaveTimer->setSingleShot(true);
    impl_->sessionSaveTimer->setInterval(300);
    const auto scheduleSave = [this] { impl_->sessionSaveTimer->start(); };
    connect(this, &QmlPlayerBridge::chainChanged, this, scheduleSave);
    connect(this, &QmlPlayerBridge::settingsChanged, this, scheduleSave);
    connect(impl_->sessionSaveTimer, &QTimer::timeout, this, [this] {
        if (!impl_->persistSession()) {
            veyra::log::error("ui-session", utf8Of(impl_->facade.error()).toStdString());
            emit notice(tr("当前修改尚未保存：") + uiText(impl_->facade.error()), true);
        }
    });
    // 60 Hz ceiling, and it does nothing at all when the snapshot is unchanged.
    impl_->hdrMemorySaveTimer=new QTimer(this);
    impl_->hdrMemorySaveTimer->setSingleShot(true);impl_->hdrMemorySaveTimer->setInterval(400);
    connect(impl_->hdrMemorySaveTimer,&QTimer::timeout,this,[this]{flushHdrMemory();});
    impl_->timer = new QTimer(this);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        const bool had = impl_->haveSnapshot;
        const uint64_t before = impl_->publishedRevision;
        const bool wasRunning = impl_->snapshot.running;
        const bool wasImage = impl_->snapshot.image;
        const bool wasFailed = impl_->snapshot.failed;
        const bool wasEngineHung = impl_->snapshot.status == engine::kEngineHangStatus;
        const bool wasFullscreenUnsafe = impl_->snapshot.vramFullscreenUnsafe;
        const auto wasTransport = impl_->snapshot.transport;
        const uint64_t wasSession = impl_->snapshot.sessionId;
        if(impl_->poll()){emit settingsChanged();emitChainChanged();}
  // Custom: a display change re-applies that device's HDR curve. The id check is
  // a few Win32 calls, so this is safe on the polling path.
  syncHdrDisplayProfile();
  syncHdrTitleProfile();
        if (impl_->snapshot.vramRunawayMiB && (impl_->vramNoticeSession != impl_->snapshot.sessionId ||
            (!wasFullscreenUnsafe && impl_->snapshot.vramFullscreenUnsafe))) {
            impl_->vramNoticeSession = impl_->snapshot.sessionId;
            if (impl_->snapshot.vramFullscreenUnsafe)
                emit notice(tr("全屏期间显存持续增长了 %1 GB，已退出全屏保护播放；当前会话使用窗口播放。增强设置保留。"
                               "原因尚未确认，可开启设置中的 OBS 游戏采集兼容、重启后测试全屏。")
                                .arg(impl_->snapshot.vramRunawayMiB / 1024.0, 0, 'f', 1), true);
            else emit notice(tr("显存在设置未变时增加了 %1 GB，正在尝试回收；回收结果见诊断日志。原因尚未确认。")
                                 .arg(impl_->snapshot.vramRunawayMiB / 1024.0, 0, 'f', 1), true);
        }
        tickSubtitles();
        tickImageBatch();
        tickBiliList();
        tickBiliTasks();
        tickBiliPlay();
        tickCapture();
        tickPs5();
        tickMoonlight();
        tickXbox();
        // Geometry uses the source DAR and the same transform as mouse/compare.
        if (impl_->snapshot.running && impl_->videoWindow) {
            const bool fresh = impl_->aspectSession != impl_->snapshot.sessionId;
            impl_->aspectSession = impl_->snapshot.sessionId;
            applyAspect(fresh);
        }
        {
            const auto& s = impl_->snapshot;
            if (impl_->resumeSession && s.sessionId == impl_->resumeSession && s.running && s.duration > 0) {
                const double at = impl_->resumeAt;
                impl_->resumeSession = 0;
                if (at >= 0.5 && at < s.duration - 0.5) {
                    seekTo(at);
                    veyra::log::info("ui-resume", std::format("resumed at {:.1f}s", at));
                    emit notice(tr("已从上次位置 %1 继续").arg(formatTime(at)), false);
                }
            }
            // Bilibili resume: the position came from the site's own history, so it applies
            // to the session this open produced. seekTo needs a running session with a known
            // duration, which this condition is exactly.
            // 还必须等到「会话号已经换成新的」：点击续播的那一刻引擎还没开始建新会话，
            // impl_->snapshot 里仍是上一个视频的 position/duration，此时发出的 seek 打在
            // 要被拆掉的旧会话上，引擎永远取不到（实测连续 5 次全丢），而 biliResumeAt
            // 已经被清空 —— 结果就是新会话从头播。
            // It must also wait until the session id has changed: at the moment of the
            // click the engine has not built the new session yet, so impl_->snapshot still
            // holds the previous video's position/duration. A seek issued then targets the
            // session being torn down and is never drained (measured: five in a row lost),
            // while biliResumeAt has already been cleared - so the new session starts from
            // the beginning.
            const bool biliSessionIsNew = s.sessionId != 0
                && s.sessionId != impl_->biliResumeAfterSession;
            if (impl_->biliResumeAt >= 0.0 && s.running && s.duration > 0 && biliSessionIsNew) {
                if (!impl_->biliResumeSession) impl_->biliResumeSession = s.sessionId;
                if (s.sessionId == impl_->biliResumeSession) {
                    const double at = impl_->biliResumeAt;
                    impl_->biliResumeAt = -1.0;
                    if (at >= 0.5 && at < s.duration - 0.5) {
                        seekTo(at);
                        veyra::log::info("ui-resume", std::format(
                            "bilibili history resumed at {:.1f}s (session={} dur={:.1f})",
                            at, static_cast<unsigned long long>(s.sessionId), s.duration));
                        emit notice(tr("已从上次位置 %1 继续").arg(formatTime(at)), false);
                    }
                }
            }
            if (++impl_->positionTicks >= 120) {
                impl_->positionTicks = 0; rememberPosition(false);
                if(s.running&&!s.capture&&!s.image&&s.sourceWidth>=64&&s.sourceHeight>=64&&s.sourceWidth<=3840&&s.sourceHeight<=2160){
                    const QVariantMap extent{{"width",s.sourceWidth},{"height",s.sourceHeight}};
                    if(impl_->prefs.value(QStringLiteral("lastSourceExtent")).toMap()!=extent){
                        impl_->prefs[QStringLiteral("lastSourceExtent")]=extent;
                        if(!impl_->savePrefs())veyra::log::warn("ui-prefs","last source extent not saved");
                    }
                }
            }
        }
        // Status light: 1.4.4 sampled its dashboard history every 250 ms.
        if (++impl_->historyTicks >= 16) {
            impl_->historyTicks = 0;
            updateRunStatus();
        }
        // The renderer reopens on its own thread (device change, fallback); pick
        // up what it opened about twice a second.
        if (++impl_->audioPollTicks >= 30) {
            impl_->audioPollTicks = 0;
            const auto active = sink::activeRenderEndpoint();
            if (active.valid != impl_->audioActive.valid || active.id != impl_->audioActive.id ||
                active.fallback != impl_->audioActive.fallback || active.channels != impl_->audioActive.channels ||
                active.downmix != impl_->audioActive.downmix) {
                const bool fellBack = active.fallback && !impl_->audioActive.fallback;
                impl_->audioActive = active;
                emit audioDevicesChanged();
                if (fellBack) emit notice(tr("所选音频设备不可用，已切到系统默认设备"), true);
            }
        }
        if (!had || impl_->publishedRevision != before) {
            const auto& s = impl_->snapshot;
            // 画面起来之后**不再**清掉重试次数：一个节点可能在开播时正常、播到一半才
            // 开始失败或掉速，这时换到下一个节点仍然有意义。次数由 biliRetriesLeft 封顶
            // （等于备用节点数），所以不会无限切换。
            // The retry budget is **no longer** dropped once the picture is up: a node can
            // open fine and only start failing or crawling later, and moving to the next one
            // still helps. biliRetriesLeft caps the count (one per spare mirror), so this
            // cannot loop.
            if (!had || wasSession != s.sessionId || wasTransport != s.transport ||
                wasRunning != s.running || wasImage != s.image || wasFailed != s.failed)
                veyra::log::info("qml-window", std::format(
                    "source-state session={} transport={} running={} image={} failed={} opening={} revision={}",
                    s.sessionId, int(s.transport), s.running, s.image, s.failed,
                    impl_->openingSource, impl_->publishedRevision));
            impl_->power.update(s.running && !s.failed && !s.image && s.transport == engine::TransportState::Playing);
            // A Bilibili stream that failed to open gets one attempt per remaining
            // mirror before the failure is reported. The mirrors are the same
            // rendition on different nodes, so a single unreachable node is the usual
            // cause; the next one normally works.
            if (had && !wasFailed && s.failed && !impl_->biliMirrors.empty() &&
                impl_->biliRetriesLeft > 0 && !impl_->biliRetryPending) {
                impl_->biliRetryPending = true;
                --impl_->biliRetriesLeft;
                // 播放中失败：把当前位置交给续播机制，新节点打开后由 tick 里的
                // 「等新会话再 seek」逻辑跳回来。打开阶段的失败没有位置，保持 -1。
                // A failure during playback hands the position to the resume mechanism,
                // which the tick's "wait for the new session, then seek" logic picks up.
                // A failure while opening has no position, so it stays -1.
                if (impl_->biliSourceActive && impl_->snapshot.position > 1.0) {
                    impl_->biliResumeAt = impl_->snapshot.position;
                    impl_->biliResumeAfterSession = impl_->snapshot.sessionId;
                    impl_->biliResumeSession = 0;
                    veyra::log::info("bilibili", std::format(
                        "mirror switch during playback; resuming at {:.1f}s",
                        impl_->snapshot.position));
                }
                // Next mirror, wrapping past the one that just failed.
                impl_->biliMirrorIndex = (impl_->biliMirrorIndex + 1) % impl_->biliMirrors.size();
                const std::wstring next = impl_->biliMirrors[impl_->biliMirrorIndex];
                veyra::log::warn("bilibili", std::format("open failed; retrying mirror {}/{}",
                    impl_->biliMirrorIndex + 1, impl_->biliMirrors.size()));
                emit notice(tr("当前节点打不开，正在换用备用节点…"), false);
                // Give the engine a moment to settle its failed session first;
                // reopening inside the same poll tick left the old source half torn down.
                const int retryGen = impl_->biliOpenGen;
                QTimer::singleShot(150, this, [this, next, retryGen] {
                    impl_->biliRetryPending = false;
                    if (retryGen != impl_->biliOpenGen) {
                        // Someone opened another source in these 150 ms; that source wins
                        // and this mirror address is stale.
                        veyra::log::info("bilibili", std::format("mirror retry superseded gen={} now={}",
                                                                retryGen, impl_->biliOpenGen));
                        return;
                    }
                    impl_->bilibiliOpenPending = true;   // the mirror is the same rendition
                    openPath(QString::fromStdWString(next));
                });
            }
            if (had && !wasFailed && s.failed && !s.capture && !s.remotePlay &&
                impl_->prefString("decode") == QLatin1String("hardware"))
                emit notice(tr("已设为“强制硬解”，这个文件无法用硬件解码打开；可在 设置 → 播放 改回“自动”"), true);
            // A live source that fails says why, and the user is taken back to the
            // start page: the reason used to land only in the (closed) PS5 panel,
            // leaving a black cinema strip that looked like a minimised window.
            if (had && !wasFailed && s.failed && (s.capture || s.remotePlay)) {
                const QString why = veyra::ui::i18n::text(s.status);
                veyra::log::warn("ui-live-failed", std::format("remote={} status={}", s.remotePlay, utf8Of(s.status).toStdString()));
                QString text = why.isEmpty() ? (s.remotePlay ? tr("PS5 串流没有连上") : tr("采集没有开始")) : why;
                // The interface itself may be gone with the device (Qt loses it too), so
                // the hint is also left for the next start.
                const QString injected = utf8Of(gfx::riskyInjections());
                if (const QString advice = injectionAdvice(injected); !advice.isEmpty()) {
                    text += QStringLiteral("；") + advice;
                    QFile marker(lastFailurePath());
                    if (marker.open(QIODevice::WriteOnly)) marker.write((QStringLiteral("device\n") + injected + QStringLiteral("\n")).toUtf8());
                }
                const auto now = std::chrono::steady_clock::now();
                const bool reconnect = s.gpuFailed && !s.remotePlay && impl_->liveKind == QLatin1String("capture") &&
                    (impl_->gpuReconnectLast == std::chrono::steady_clock::time_point{} || now - impl_->gpuReconnectLast > std::chrono::minutes(10));
                if (s.gpuFailed) veyra::log::warn("gpu-reconnect", std::format("session={} kind={} automatic={}", s.sessionId, impl_->liveKind.toStdString(), reconnect));
                if (reconnect) { impl_->gpuReconnectSession = s.sessionId; impl_->gpuReconnectDue = {}; text += QStringLiteral("；") + tr("将自动重新连接采集卡一次"); }
                emit notice(text, true);
                if (!reconnect && impl_->currentPage == QLatin1String("min")) emit navigate(QStringLiteral("home"));
            }
            // Engine watchdog (field logs(15)): a frozen picture says why, once.
            if (had && !wasEngineHung && s.status == engine::kEngineHangStatus) emit notice(uiText(s.status), true);
            // Network sources (#48): a recovered connection and now-and-then loss summaries.
            if (s.sessionId != impl_->netNoticeSession) {
                impl_->netNoticeSession = s.sessionId;
                impl_->netLossReported = 0;
                impl_->netReconnectsReported = 0;
                impl_->netLossNoticeAt = {};
            }
            if (s.network.active) {
                const auto now = std::chrono::steady_clock::now();
                if (s.network.reconnects > impl_->netReconnectsReported) {
                    impl_->netReconnectsReported = s.network.reconnects;
                    emit notice(tr("网络已重新连接，继续播放"), false);
                }
                const uint64_t lost = std::max(s.network.corruptPackets, s.network.corruptFrames);
                if (lost > impl_->netLossReported &&
                    (impl_->netLossNoticeAt == std::chrono::steady_clock::time_point{} ||
                     now - impl_->netLossNoticeAt >= std::chrono::seconds(20))) {
                    emit notice(tr("检测到网络丢包，已自动跳过 %1 处").arg(qulonglong(lost - impl_->netLossReported)), false);
                    impl_->netLossReported = lost;
                    impl_->netLossNoticeAt = now;
                }
            }
            // 「自动」倍率：每个会话进入播放后决定一次（见 applyFgAutoMultiplier）。
            maybeApplyFgAutoMultiplier();
            emit snapshotChanged();
        }
        // The reconnect waits for the failed session's teardown (the GPU reset can hold it for
        // seconds) and is dropped if anything else was opened meanwhile.
        if (impl_->gpuReconnectSession) {
            const auto& s = impl_->snapshot;
            const auto now = std::chrono::steady_clock::now();
            if (s.sessionId != impl_->gpuReconnectSession || impl_->openingSource) {
                veyra::log::info("gpu-reconnect", "cancelled: another source was opened");
                impl_->gpuReconnectSession = 0;
            } else if (!s.running) {
                if (impl_->gpuReconnectDue == std::chrono::steady_clock::time_point{}) impl_->gpuReconnectDue = now + std::chrono::seconds(3);
                else if (now >= impl_->gpuReconnectDue) {
                    impl_->gpuReconnectSession = 0; impl_->gpuReconnectLast = now;
                    veyra::log::warn("gpu-reconnect", "reconnecting the capture card after the GPU failure");
                    emit notice(tr("GPU 已重置，正在自动重新连接采集卡"), false);
                    resumeCaptureSession();
                }
            }
        }
        // An export in flight needs its own poll; it is a separate job and its
        // snapshot is not part of the player snapshot.
        if (impl_->exportQueue.queue().needsTick()) pollExport();
        tickPlaylist();
        updateGameMode();
    });
    impl_->timer->start(16);
    startGpuSampler();
    // Overlays that hook presentation get injected into this process when a D3D device
    // appears (Qt's at start, the engine's at open); see gfx/PresentationHooks.h for what
    // they broke in the field. Log each once (every 2 s for two minutes, then every 10 s).
    auto* hookTimer = new QTimer(this);
    connect(hookTimer, &QTimer::timeout, this, [this, hookTimer, started = GetTickCount64()] {
        struct Seen { const wchar_t* module; const char* product; };
        static const Seen others[] = {
            {L"graphics-hook64.dll", "OBS game capture"}, {L"DiscordHook64.dll", "Discord overlay"},
            {L"gameoverlayrenderer64.dll", "Steam overlay"}, {L"nvspcap64.dll", "NVIDIA overlay / instant replay"},
            {L"ow-graphics-hook64.dll", "Overwolf overlay"},
        };
        static bool loggedOthers[std::size(others)]{};
        for (size_t k = 0; k < std::size(others); ++k)
            if (!loggedOthers[k] && GetModuleHandleW(others[k].module)) {
                loggedOthers[k] = true;
                veyra::log::info("overlay-hooks", std::format("{} is injected into this process", others[k].product));
            }
        // RivaTuner / GamePP are only logged. The XeSS guard that switched frame generation
        // away from XeSS while they were injected was removed on request (2026-10-02): the
        // interface now draws with Direct3D 12 like 1.4.4's process, which is what they
        // coexisted with.
        static bool handled = false;
        if (const auto* hook = gfx::injectedPresentationHook(); hook && !handled) {
            handled = true;
            veyra::log::warn("overlay-hooks", std::format("{} is injected into this process ({})",
                hook->product, QString::fromWCharArray(hook->module).toStdString()));
            if (GetModuleHandleW(L"RTSSHooks64.dll")) {
                wchar_t path[32768]{};
                GetModuleFileNameW(GetModuleHandleW(L"RTSSHooks64.dll"), path, DWORD(std::size(path)));
                veyra::log::info("overlay-hooks", std::format("RTSS hook module: activeServer={} path={}",
                    gfx::rivaTunerRunning(), QString::fromWCharArray(path).toStdString()));
            }
        }
        // RivaTuner arrived after a start with the interface on the GPU (it was not running
        // then, see main.cpp). Its OSD stays out of this process (RTSSHooksProfileOverride),
        // so nothing breaks; a restart moves the interface to software drawing and puts the
        // OSD on the video.
        static bool rivaTunerLate = false;
        if (!rivaTunerLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && GetModuleHandleW(L"RTSSHooks64.dll") && gfx::rivaTunerRunning()) {
            rivaTunerLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("RivaTuner injected into a run with a GPU interface; its OSD is off in this process ({})",
                automatic ? "active OSD server: compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("rtss"));
        }
        // NVIDIA's in-game overlay switched on after a start with the interface on the GPU
        // (main.cpp checks at start). Unlike RTSS it cannot be kept out of this process,
        // and with both swapchains hooked the video and interface threads deadlocked in
        // the field (gfx/PresentationHooks.h); a restart draws the interface in software.
        static bool nvidiaOverlayLate = false;
        if (!nvidiaOverlayLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_NVIDIA_OVERLAY", nullptr, 0) && gfx::nvidiaOverlayRunning()) {
            nvidiaOverlayLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("NVIDIA overlay is on in a run with a GPU interface ({})",
                automatic ? "compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("nvidia"));
        }
        // GamePP (游戏加加) started after Veyra or injected into it: same as NVIDIA's overlay.
        static bool gamePPLate = false;
        if (!gamePPLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_GAMEPP", nullptr, 0) && (gfx::gamePPInjected() || gfx::gamePPRunning())) {
            gamePPLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("GamePP is running or injected in a run with a GPU interface ({})",
                automatic ? "compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("gamepp"));
        }
        updateLiveCompanionCompat();
        // RivaTuner's default hooking keeps OBS game capture from hooking any Direct3D 12
        // program (2026-10-03, RTSS 7.3.5.28314 + OBS 32.1.2, a bare D3D12 window included;
        // RTSS version corrected against the executable on 2026-10-04); its
        // "Use Microsoft Detours API hooking" option lets both work. Said once, ever.
        static bool obsWithRivaTuner = false;
        if (!obsWithRivaTuner && GetModuleHandleW(L"graphics-hook64.dll") && GetModuleHandleW(L"RTSSHooks64.dll")) {
            obsWithRivaTuner = true;
            veyra::log::warn("overlay-hooks", "OBS game capture and RivaTuner are both injected: OBS may not get the picture unless RTSS uses Detours hooking for Veyra");
            if (!impl_->prefs.value(QStringLiteral("obsRivaTunerHintShown")).toBool()) {
                impl_->prefs[QStringLiteral("obsRivaTunerHintShown")] = true;
                (void)impl_->savePrefs();
                emit notice(tr("检测到 OBS 游戏采集和小飞机（RTSS）同时注入 Veyra。两者默认的挂钩方式冲突，OBS 游戏采集可能抓不到画面（任何 D3D12 程序都一样）。"
                               "在 RTSS 里添加 veyra_qml_ui.exe，并在它的设置中勾选“Use Microsoft Detours API hooking”；或在 OBS 改用窗口采集（Windows 10 1903+）"), false);
            }
        }
        // Overlays inject when they decide the window is a game, e.g. on entering fullscreen
        // long after start; after the first two minutes look every 10 s instead of every 2 s.
        if (GetTickCount64() - started > 120000 && hookTimer->interval() < 10000) hookTimer->setInterval(10000);
    });
    hookTimer->start(2000);
    // The previous run crashed or lost the GPU with a known-risky component injected:
    // say so once the window is up (the crash handler cannot show anything itself).
    QTimer::singleShot(2500, this, [this] {
        QFile marker(lastFailurePath());
        if (!marker.exists()) return;
        QByteArray raw;
        if (marker.open(QIODevice::ReadOnly)) raw = marker.readAll();
        marker.close();
        marker.remove();
        // The crash handler writes UTF-16 (no allocation in the filter); the bridge UTF-8.
        const QString text = raw.size() > 1 && raw.at(1) == 0
            ? QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), raw.size() / 2) : QString::fromUtf8(raw);
        const QStringList lines = text.split(QLatin1Char('\n'));
        const QString kind = lines.value(0), modules = lines.value(1);
        QString advice = injectionAdvice(kind + QLatin1Char(',') + modules);
        veyra::log::info("overlay-hooks", std::format("previous run: {} injected={}", kind.toStdString(), modules.toStdString()));
        if (advice.isEmpty()) return;
        const QString head = kind.startsWith(QStringLiteral("crash"))
            ? tr("上次 Veyra 异常退出（崩溃在 %1）").arg(kind.mid(6).trimmed())
            : tr("上次采集时显卡设备丢失");
        emit notice(head + QStringLiteral("；") + advice, true);
    });
    // Unsupported saved FG choices were repaired together with both modes
    // above, using actual provider availability rather than a brand alone.
}

void QmlPlayerBridge::flushBiliWatchProgress() {
    if (impl_->biliWatchBvid.empty() || impl_->biliWatchCid <= 0) return;
    // Nothing was ever observed playing: do not write a bogus position into the history.
    if (impl_->biliWatchLastPos < 0.0) return;
    int played = int(impl_->biliWatchLastPos);
    // The site stores -1 for "watched to the end", which is what makes a finished video
    // show as finished instead of resuming a few seconds before the credits.
    if (impl_->biliWatchDuration > 0 && impl_->biliWatchLastPos >= impl_->biliWatchDuration - 1.0)
        played = -1;
    if (played < 0 && played != -1) played = 0;
    // Cleared before reporting: the video is done, whatever this call returns, so the same
    // position is not sent again from the destructor.
    std::wstring bvid = std::move(impl_->biliWatchBvid);
    const long long cid = impl_->biliWatchCid;
    impl_->biliWatchBvid.clear();
    impl_->biliWatchCid = 0;
    impl_->biliWatchLastPos = -1.0;
    impl_->biliWatchDuration = 0.0;
    const std::string tag = utf8Of(bvid).toStdString();
    if (media::biliReportWatchProgress(bvid, cid, played))
        log::info("bilibili", std::format("watch progress reported bvid={} played_time={}", tag, played));
    else
        log::warn("bilibili", std::format("watch progress not reported bvid={} played_time={}", tag, played));
}

// 播放自然结束时的上报：取值规则和 flushBiliWatchProgress 一样，但网络请求丢给 worker
// —— 它内部是同步 HTTP POST，直接放在 GUI tick 里会卡住界面几百毫秒。
// 这样历史在结束那一刻就写入了，用户马上点「历史记录」就能看到这个视频。以前要等下一次
// 打开别的来源才补报，表现就是「必须再播一个视频它才出现」。
// Same position rules as flushBiliWatchProgress, but the request runs on a worker:
// biliReportWatchProgress posts synchronously, and doing that from the GUI tick would
// freeze the window. Reporting at the end is what makes the video appear in 历史记录
// immediately; the write used to wait for the next open, which looked like "it only shows
// up after playing another video".
// 开播那一刻写一次历史。played 必须是**实际起播位置**：续播传续播点，从头播放传 0。
// 一律传 0 会把 B 站记下来的进度清掉，用户下次就没法从原处继续。
// 不影响 biliWatch* 记录：调用方传的是副本，后续 tick 继续追踪真实位置，播完照旧按 -1
// 上报。
// Writes the history the moment playback starts. played must be the position playback
// really begins at: the resume point when resuming, 0 when starting from the top. Always
// passing 0 would wipe the progress the site stored, so the next resume would start over.
// The biliWatch* fields are left alone - the caller passes copies - so the tick keeps
// tracking the real position and the end still reports -1.
bool QmlPlayerBridge::reportBiliWatchStartedAsync(const std::wstring& bvid, long long cid,
                                                  int played) {
    auto& i = *impl_;
    if (bvid.empty() || cid <= 0) return false;
    if (played < 0) played = 0;
    // 槽位忙时跳过：覆盖一个还在跑的任务会析构旧 future 并阻塞 GUI 线程。
    // Skip while the slot is busy: overwriting a running task destroys the old future
    // and blocks the GUI thread.
    if (i.biliTaskSlots[static_cast<size_t>(BiliOp::ReportProgress)].busy) return false;
    const std::string tag = utf8Of(bvid).toStdString();
    return startBiliTask(BiliOp::ReportProgress, [bvid, cid, played, tag] {
        BiliTaskResult r;
        r.ok = media::biliReportWatchProgress(bvid, cid, played);
        log::info("bilibili", std::format(
            "watch progress reported at start bvid={} played_time={} ok={}",
            tag, played, r.ok ? 1 : 0));
        return r;
    });
}

bool QmlPlayerBridge::reportBiliWatchProgressAsync() {
    auto& i = *impl_;
    if (i.biliWatchBvid.empty() || i.biliWatchCid <= 0) return false;
    if (i.biliWatchLastPos < 0.0) return false;
    // 上一次上报还没回来：这一次先不碰，数据留着让下一帧重试。直接覆盖槽位会让旧
    // future 析构并阻塞 GUI，网络慢的时候正好卡住一帧甚至更久。
    // An earlier report is still in flight: leave the data alone and retry next frame.
    // Overwriting the slot would destroy the old future, which blocks the GUI thread.
    if (i.biliTaskSlots[static_cast<size_t>(BiliOp::ReportProgress)].busy) return false;
    int played = int(i.biliWatchLastPos);
    if (i.biliWatchDuration > 0 && i.biliWatchLastPos >= i.biliWatchDuration - 1.0)
        played = -1;
    if (played < 0 && played != -1) played = 0;
    // 立刻清空：同一个位置只报一次，之后析构或 openPath 的 flush 不会再发。
    // Cleared at once so the same position is not sent again by the flush in the
    // destructor or in openPath.
    const std::wstring bvid = std::move(i.biliWatchBvid);
    const long long cid = i.biliWatchCid;
    i.biliWatchBvid.clear();
    i.biliWatchCid = 0;
    i.biliWatchLastPos = -1.0;
    i.biliWatchDuration = 0.0;
    const std::string tag = utf8Of(bvid).toStdString();
    return startBiliTask(BiliOp::ReportProgress, [bvid, cid, played, tag] {
        BiliTaskResult r;
        r.ok = media::biliReportWatchProgress(bvid, cid, played);
        log::info("bilibili", std::format(
            "watch progress reported at end bvid={} played_time={} ok={}",
            tag, played, r.ok ? 1 : 0));
        return r;
    });
}
QmlPlayerBridge::~QmlPlayerBridge() {
    // Before anything is torn down: this reads the playback position.
    flushBiliWatchProgress();
    // The overlay is a child window: it has to go before the video window does.
    if (impl_->danmakuWindow) impl_->danmakuWindow->destroy();
    flushHdrMemory();
    if (qApp) qApp->removeEventFilter(this);
    rememberPosition(true);
    if (impl_->sessionSaveTimer) impl_->sessionSaveTimer->stop();
    if (!impl_->persistSession())
        veyra::log::error("ui-session", utf8Of(impl_->facade.error()).toStdString());
}


// --- professional-page readouts ---------------------------------------------
// These read fields the engine genuinely publishes. Where it publishes nothing,
// the getter says so and the UI shows an explicit "unmeasured" rather than a
// zero or a stand-in.
QString QmlPlayerBridge::outputSummary() const {
    const auto& s = impl_->snapshot;
    const auto& plan = s.metrics.resolution;
    if (!s.running || !plan.output.valid() || plan.settingsRevision != s.applied.revision) return {};
    return QStringLiteral("%1x%2").arg(plan.output.width).arg(plan.output.height);
}

// A display rate is only real when the system's display event can be read. No
// counter is wired in this build, so it reports unknown and the UI shows "未测" -
// which is exactly what the design requires instead of passing the submit rate off
// as a measurement that was never taken.
double QmlPlayerBridge::displayFps() const { return 0.0; }
bool QmlPlayerBridge::displayFpsKnown() const { return false; }

double QmlPlayerBridge::queuedFrames() const {
    // Valid output opportunities held by presenter jobs, not cumulative skips
    // and not the OS/display queue. Zero is meaningful only with a live window.
    return queuedFramesKnown() ? double(impl_->snapshot.metrics.flow.pendingOutputFrames) : 0.0;
}
bool QmlPlayerBridge::queuedFramesKnown() const {
    const auto& s = impl_->snapshot;
    const auto& flow = s.metrics.flow;
    return s.running && !s.image && s.sessionId != 0 &&
        flow.latest.sessionId == s.sessionId && flow.lastReady100ns > 0 &&
        flow.latest.frame.settingsRevision == s.applied.revision;
}

int QmlPlayerBridge::skippedFrames() const { return int(impl_->snapshot.previewSkipped); }

QString QmlPlayerBridge::flowBackend() const {
    // The snapshot carries a performance figure and a content rate, not a backend
    // name; report the fact we do have and let it read as unmeasured otherwise.
    const auto& s = impl_->snapshot;
    if (s.flowPerf == 0) return {};
    return QStringLiteral("GPU 光流");
}

int QmlPlayerBridge::opticalFlowChoice() const { return int(settings().opticalFlowBackend); }
bool QmlPlayerBridge::setOpticalFlowChoice(int backend) {
    if(backend<0||backend>2)return false;
    const auto cap = effectCapabilities().value("flow" + QString::number(backend)).toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return false; }
    const auto before=settings();auto next=before;
    next.opticalFlowBackend=engine::OpticalFlowBackend(backend);
    if(next.opticalFlowBackend==before.opticalFlowBackend)return true;
    if(!impl_->commit(next)){
        emit notice(tr("引擎未接受光流配置，原设置保留"),true);return false;
    }
    emit settingsChanged();return true;
}

QVariantList QmlPlayerBridge::stageTimings() const {
    // Consume the existing GPU timestamp aggregates. Repeated NR layers share
    // a timestamp slot: this is stage sampling, NOT per-node attribution.
    // XeSS-internal FG is not measured by the graph's DLSS FG batch query.
    const auto& s = impl_->snapshot;
    QVariantList out;
    using Stage = diagnostics::GpuStage;
    struct Row { const char* label; Stage stage; const char* color; };
    const Row rows[] = {
        {"输入", Stage::Color, "#A8A8B8"},
        {"抗锯齿", Stage::AaNode0, "#7FD4FF"},
        {"抗锯齿 2", Stage::AaNode1, "#A9E2FF"},
        {"人脸识别", Stage::BeautyDetect, "#FFB3D1"},
        {"美颜", Stage::BeautyNode0, "#FF8FBF"},
        {"美颜 2", Stage::BeautyNode1, "#FF9DC7"},
        {"美颜 3", Stage::BeautyNode2, "#FFABCF"},
        {"美颜 4", Stage::BeautyNode3, "#FFB9D7"},
        {"美颜 5", Stage::BeautyNode4, "#FFC7DF"},
        {"美颜 6", Stage::BeautyNode5, "#FFD5E7"},
        {"超分", Stage::Sr, "#6EA8FF"},
        {"光流", Stage::Flow, "#9A85FF"},
        {"NR采样", Stage::Nr, "#FF8A3D"},
        {"残差", Stage::Residual, "#E7B868"},
        {"FG", Stage::FgBatch, "#3DDC84"},
        {"HDR", Stage::VideoHdr, "#E58BD9"},
        {"呈现", Stage::Blit, "#58CAD4"},
    };
    const double budget = stageBudgetMs();
    // Several NR layers: every layer marks the shared NR slot, so it only ever held the last
    // layer's time (field report 2026-10-01). Each layer also has its own slot; with two or
    // more layers measured, show those instead of the shared row.
    std::vector<Row> shown;
    unsigned measuredLayers = 0;
    for (unsigned i = 0; i < diagnostics::kTimedNrLayers; ++i)
        if (s.metrics.flow.gpuTiming[size_t(diagnostics::nrLayerStage(i))].samples > 0) ++measuredLayers;
    static const char* const layerLabels[] = {"NR 第1层", "NR 第2层", "NR 第3层", "NR 第4层"};
    static const char* const layerColors[] = {"#FF8A3D", "#FFA866", "#FFC28F", "#FFD9B8"};
    for (const auto& r : rows) {
        // Anti-aliasing and beauty rows appear only while those nodes run.
        const bool optional = r.stage == Stage::AaNode0 || r.stage == Stage::AaNode1 ||
            (r.stage >= Stage::BeautyNode0 && r.stage <= Stage::BeautyDetect);
        if (optional && s.metrics.flow.gpuTiming[size_t(r.stage)].samples == 0) continue;
        if (r.stage == Stage::Nr && measuredLayers >= 2) {
            for (unsigned i = 0; i < diagnostics::kTimedNrLayers; ++i)
                if (s.metrics.flow.gpuTiming[size_t(diagnostics::nrLayerStage(i))].samples > 0)
                    shown.push_back({layerLabels[i], diagnostics::nrLayerStage(i), layerColors[i]});
        } else shown.push_back(r);
    }
    for (const auto& r : shown) {
        const auto& sample = s.metrics.flow.gpuTiming[size_t(r.stage)];
        // The mean of the last second, as 1.4.4's dashboard showed; P95 rides along for the
        // tooltip. Showing P95 alone read as "slower than 1.4.4" for the same work.
        const bool measured = queuedFramesKnown() && sample.samples > 0 &&
            sample.mean && std::isfinite(*sample.mean) && *sample.mean >= 0.0;
        const double ms = measured ? *sample.mean : 0.0;
        QVariantMap item;
        item["label"] = uiText(std::string_view(r.label));
        item["ms"] = ms;
        item["p95"] = measured && sample.p95 ? *sample.p95 : 0.0;
        item["measured"] = measured;
        item["samples"] = measured ? QVariant::fromValue<qulonglong>(sample.samples) : QVariant::fromValue<qulonglong>(0);
        item["domain"] = QStringLiteral("GPU timestamp P95");
        item["color"] = QString::fromUtf8(r.color);
        item["fraction"] = (measured && budget > 0.01) ? ms / budget : 0.0;
        out << item;
    }
    return out;
}

QVariantMap QmlPlayerBridge::nodeTimings() const {
    // Per-node GPU timestamp P95, keyed by the card id chain() publishes. An
    // editor node borrows a number only when the wired path it sits on is the
    // chain actually running; drafts, detached and disabled nodes never do.
    using Stage = diagnostics::GpuStage;
    const auto& c = impl_->chain;
    const auto& flow = impl_->snapshot.metrics.flow;
    const bool node = c.mode == engine::ChainMode::Node;
    std::vector<int> path;
    bool draft = false;
    if (!node) {
        for (uint32_t i = 0; i < c.nodeCount; ++i) path.push_back(int(i));
    } else {
        uint32_t id = impl_->layout.inputNext;
        while (id > engine::NodeGraphLayout::Output && path.size() <= c.nodeCount) {
            const int index = impl_->layout.indexOf(c, id);
            if (index < 0) break;
            path.push_back(index);
            id = impl_->layout.next[size_t(index)];
        }
        engine::EffectChain projected;
        draft = id != engine::NodeGraphLayout::Output || !impl_->layout.project(c, projected).accepted ||
            engine::runtimeOrder(projected) != engine::runtimeOrder(impl_->acceptedChain);
        if (draft) path.clear();
    }
    const bool live = queuedFramesKnown();
    const auto& snapshot = impl_->snapshot;
    // A failed optical-flow/NR backend can leave the requested editor layer
    // enabled while the engine has deliberately resumed playback without NR.
    // Show that confirmed recovery, not an endless timing warmup or stale ms.
    const bool nrRejected = live && !snapshot.applying && snapshot.rejectedRevision != 0 &&
        snapshot.desired.revision == snapshot.applied.revision &&
        !snapshot.desired.nr && !snapshot.applied.nr && !snapshot.backendWarning.empty();
    const bool xess = fgBackendName() == QLatin1String("xess");
    QVariantMap out;
    auto publish = [&](uint32_t i, const QString& state, std::optional<Stage> stage) {
        QVariantMap item{{"state", state}, {"ms", 0.0}, {"samples", QVariant::fromValue<qulonglong>(0)}};
        if (state == QLatin1String("failed")) item["reason"] = uiText(snapshot.backendWarning);
        if (stage) {
            const auto& sample = flow.gpuTiming[size_t(*stage)];
            if (live && sample.samples > 0 && sample.mean && std::isfinite(*sample.mean) && *sample.mean >= 0.0) {
                item["state"] = QStringLiteral("measured");
                item["ms"] = *sample.mean;
                item["samples"] = QVariant::fromValue<qulonglong>(sample.samples);
            }
        }
        const QVariant key = node ? QVariant(impl_->layout.ids[i]) : QVariant(i + 2);
        out[key.toString()] = item;
    };
    std::vector<bool> onPath(c.nodeCount, false);
    for (const int i : path) onPath[size_t(i)] = true;
    for (uint32_t i = 0; i < c.nodeCount; ++i)
        if (!onPath[i]) publish(i, draft ? QStringLiteral("draft") : QStringLiteral("idle"), std::nullopt);
    unsigned enabledNr = 0, colour = 0, antiAlias = 0, beauty = 0;
    for (const int index : path) {
        const auto i = uint32_t(index);
        const auto& n = c.nodes[i];
        std::optional<Stage> stage;
        QString waiting = live ? QStringLiteral("pending") : QStringLiteral("stopped");
        switch (n.type) {
        case engine::EffectType::NrEnhance:
            if (nrRejected) waiting = QStringLiteral("failed");
            else if (n.enabled && enabledNr < diagnostics::kTimedNrLayers) stage = diagnostics::nrLayerStage(enabledNr);
            if (n.enabled) ++enabledNr;
            break;
        case engine::EffectType::Color:
            // Parameter 0 before any SR/NR is fused into the input conversion
            // shader: it has no separable GPU span of its own.
            if (colour < diagnostics::kTimedColorNodes) stage = diagnostics::colorNodeStage(colour);
            if (colour == 0) waiting = live ? QStringLiteral("fused") : waiting;
            ++colour;
            break;
        case engine::EffectType::AntiAliasing:
            // Indexed by parameter ordinal, like Color: every anti-aliasing node on the path counts.
            if (antiAlias < diagnostics::kTimedAaNodes) stage = diagnostics::aaNodeStage(antiAlias);
            ++antiAlias;
            break;
        case engine::EffectType::Beauty:
            if (beauty < diagnostics::kTimedBeautyNodes) stage = diagnostics::beautyNodeStage(beauty);
            ++beauty;
            break;
        case engine::EffectType::SuperResolution: stage = Stage::Sr; break;
        case engine::EffectType::VideoHdr:
            stage = Stage::VideoHdr;
            // SDR display path: the node is wired but TrueHDR is not built.
            if (live && !impl_->snapshot.videoHdrActive) waiting = QStringLiteral("hdr-sdr");
            break;
        case engine::EffectType::FrameGeneration:
            if (xess) waiting = QStringLiteral("sdk"); else stage = Stage::FgBatch;
            // Realtime admission declined the pairs inside this window.
            if (!xess && live && flow.counters.fgSkippedBeforeEval > 0 && flow.counters.fgEvaluated < flow.counters.fgCandidate)
                waiting = QStringLiteral("fg-skipped");
            break;
        default: waiting = QStringLiteral("untimed"); break;
        }
        if (!n.enabled) { publish(i, QStringLiteral("disabled"), std::nullopt); continue; }
        publish(i, waiting, stage);
    }
    return out;
}

double QmlPlayerBridge::stageBudgetMs() const {
    // Budget per content frame entering enhancement. Intentional dedup and
    // playback speed change that interval; a presentation cap alone does not.
    // Reuse the bounded status sample, without sampling or repainting faster.
    const auto& s=impl_->snapshot;const auto& h=impl_->history;
    const double fps=h.session==s.sessionId&&h.revision==s.applied.revision&&h.output.contentFps>0
        ?h.output.contentFps:s.nominalSourceFps*(s.captureHalfRate?.5:1.0);
    return fps > 0.01 ? 1000.0 / fps : 0.0;
}

double QmlPlayerBridge::scheduleP95Ms() const { return impl_->snapshot.schedulingWaitP95Ms; }

double QmlPlayerBridge::chainTotalMs() const {
    // Engine merges same-frame measured intervals before aggregation. Never
    // sum unrelated percentiles. This excludes uninstrumented stages/nodes.
    // Mean of the last second, the same statistic as the stage rows (see stageTimings).
    return chainTotalMsKnown() ? *impl_->snapshot.metrics.flow.enhancementProcessing.mean : 0.0;
}
bool QmlPlayerBridge::chainTotalMsKnown() const {
    const auto& sample = impl_->snapshot.metrics.flow.enhancementProcessing;
    return queuedFramesKnown() && sample.samples > 0 && sample.mean &&
        std::isfinite(*sample.mean) && *sample.mean >= 0.0;
}


// --- frame generation --------------------------------------------------------
QString QmlPlayerBridge::fgBackendName() const {
    switch(settings().frameGenerationBackend){
    case engine::FrameGenerationBackend::Dlss:return QStringLiteral("dlss");
    case engine::FrameGenerationBackend::XeSS:return QStringLiteral("xess");
    case engine::FrameGenerationBackend::Fsr:return QStringLiteral("fsr3");
    case engine::FrameGenerationBackend::Fsr4:return QStringLiteral("fsr4");
    case engine::FrameGenerationBackend::Vfg:return QStringLiteral("vfg");
    }
    return {};
}
QVariantList QmlPlayerBridge::fgBackendChoices() const {
    QVariantList choices = {
        QVariantMap{{"id","dlss"},{"label",tr("DLSS 帧生成")}},
        QVariantMap{{"id","xess"},{"label",tr("Intel XeSS · 实验")}},
        QVariantMap{{"id","fsr3"},{"label",tr("FSR 3.1 · 2X")}},
        QVariantMap{{"id","fsr4"},{"label",tr("FSR 4 ML · RX 9000 · 实验")}},
        QVariantMap{{"id","vfg"},{"label",tr("NVIDIA VFG · RTX 40 / 50")}}
    };
    const auto caps = effectCapabilities();
    for (auto& choice : choices) {
        auto row = choice.toMap();
        const auto cap = caps.value(row.value("id").toString()).toMap();
        row["disabled"] = !cap.value("available").toBool();
        row["note"] = cap.value("reason");
        choice = row;
    }
    return choices;
}
QString QmlPlayerBridge::fgProviderText() const {
    const auto& s=impl_->snapshot;
    if(fgBackendName()==QLatin1String("vfg")){
        if(!vfgHardwarePossible())return tr("VFG 需要 NVIDIA Ada / Blackwell 显卡（RTX 40 / 50）");
        if(!effectCapabilities().value("vfg").toMap().value("available").toBool())return tr("未找到 VFG 运行组件，请在组件页核对路径");
        return tr("VFG 支持 2X～8X 和低 / 中 / 高质量；6X～8X 为实验档，高倍高质量会增加处理时间");
    }
    if(engine::fsrFrameGeneration(s.applied.frameGenerationBackend)&&s.applied.multiplier>1&&!s.fsrProviderVersion.empty())
        return tr("实际运行：FSR %1 · 2X").arg(QString::fromStdString(s.fsrProviderVersion));
    if(fgBackendName()==QLatin1String("fsr4"))return tr("FSR 4 ML 需要支持的 RX 9000 和驱动；实卡待验");
    if(fgBackendName()==QLatin1String("fsr3"))return tr("FSR 3.1 跨厂商补帧 · 最高 2X");
    if(fgBackendName()==QLatin1String("xess"))
        return fgMaxMultiplier()<4?tr("当前 XeSS 模块未匹配 3X / 4X 解锁版本；2X 可用，请在组件页核对版本"):
            tr("2X 为原生路径，3X / 4X 为实验解锁；失败原因见诊断");
    return tr("NVIDIA RTX 补帧；倍率以运行库实际能力为准");
}
// XeSS ceiling (SettingsWindow): 4X with the audited unlock provider, else 2X,
// or a running session's reported interpolation count above 2X.
static int xessCeiling(const engine::PlayerSnapshot& s) {
    static const bool audited = gfx::XessMfgUnlock::providerIsAudited(
        (runtime::localDataDirectory() / L"intel" / L"experimental" / L"libxess_fg.dll").wstring());
    int cap = audited ? 4 : 2;
    // A running context reports the count it was CREATED with (3X -> 2), not
    // what the provider allows: it may raise the ceiling, never lower it.
    // Lowering it made 4X vanish after picking 3X until the backend was reset.
    // The highest ceiling any context has reported this run stays offered:
    // a later context created at 2X reports 1 and must not hide 3X/4X again.
    static int seen = 0;
    if (s.xessMaxInterpolatedFrames > 1) seen = std::max(seen, std::clamp(s.xessMaxInterpolatedFrames + 1, 2, 4));
    return std::max(cap, seen);
}
void QmlPlayerBridge::setFgBackendName(const QString& value) {
    if(value!=QLatin1String("dlss")&&value!=QLatin1String("xess")&&value!=QLatin1String("fsr3")&&value!=QLatin1String("fsr4")&&value!=QLatin1String("vfg")){emit notice(tr("未知补帧后端"),true);return;}
    const auto availability = effectCapabilities().value(value).toMap();
    if (!availability.value("available").toBool()) { emit notice(availability.value("reason").toString(), true); emit settingsChanged(); return; }
    auto s = settings();
    const auto want = value == QLatin1String("xess") ? engine::FrameGenerationBackend::XeSS
                    : value == QLatin1String("fsr3") ? engine::FrameGenerationBackend::Fsr
                    : value == QLatin1String("fsr4") ? engine::FrameGenerationBackend::Fsr4
                    : value == QLatin1String("vfg") ? engine::FrameGenerationBackend::Vfg
                    : engine::FrameGenerationBackend::Dlss;
    if (s.frameGenerationBackend == want) return;
    if(want==engine::FrameGenerationBackend::Vfg&&(!vfgHardwarePossible()||!pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring()))){
        emit notice(!vfgHardwarePossible()?tr("VFG 需要 NVIDIA Ada / Blackwell 显卡（RTX 40 / 50）"):tr("未找到 VFG 运行组件，请在组件页核对路径"),true);emit settingsChanged();return;
    }
    if (want == engine::FrameGenerationBackend::Fsr4 && !fsr4Possible()) {
        // Keep the previous choice; the menu snaps back to it.
        emit notice(tr("FSR 4 ML 补帧只支持 AMD RX 9000 显卡，当前显卡不能使用，已保持原来的补帧方式"), true);
        emit settingsChanged();
        return;
    }
    s.frameGenerationBackend = want;
    // A DLSS multiplier above XeSS's ceiling used to make the switch fail
    // silently in effect (the backend stayed DLSS). Clamp to the ceiling instead.
    auto& chain = impl_->chain;
    const auto beforeMultiplier = chain.fgMultiplier;
    const int cap = want == engine::FrameGenerationBackend::XeSS ? xessCeiling(impl_->snapshot)
                  : engine::fsrFrameGeneration(want) ? 2 : want==engine::FrameGenerationBackend::Vfg?8:6;
    chain.fgMultiplier=normalizeFgMultiplier(chain.fgMultiplier,want,uint32_t(cap));
    const bool clamped = chain.fgMultiplier != beforeMultiplier;
    if(!impl_->commit(s)){
        chain.fgMultiplier = beforeMultiplier;
        emit notice(tr("补帧后端切换未被接受，原设置保留"),true);return;
    }
    if (clamped) emit notice(tr("所选补帧后端的倍率已调整为 %1X").arg(chain.fgMultiplier), false);
    emit settingsChanged();
    emitChainChanged();
}

int QmlPlayerBridge::fgMaxMultiplier() const {
    // AppShell/SettingsWindow rules. Capabilities report GENERATED frames, so the
    // multiplier ceiling is that count + 1 (DLSS MultiFrameCountMax 5 = 6X on
    // Blackwell, 1 = 2X on Ada). Unknown DLSS capability offers the full list; a
    // new context validates it. XeSS: 4X with the audited unlock provider, else
    // 2X, or what a running session already reported above 2X.
    const auto& s = impl_->snapshot;
    if(settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg)return 8;
    if(engine::fsrFrameGeneration(settings().frameGenerationBackend))return 2;
    if (settings().frameGenerationBackend == engine::FrameGenerationBackend::XeSS)
        return xessCeiling(s);
    if (s.fgCapabilityKnown && s.fgMultiFrameMax > 0) return std::clamp(s.fgMultiFrameMax + 1, 2, 6);
    return 6;
}

QVariantList QmlPlayerBridge::fgMultiplierChoices() const {
    // Only multipliers the chosen backend supports are offered: DLSS reaches 6X on
    // capable hardware, XeSS stops at 4X. Offering a value the provider would
    // refuse is the same lie as a dead control.
    const auto backend = settings().frameGenerationBackend;
    const bool xess = backend == engine::FrameGenerationBackend::XeSS;
    const int ceiling = xess ? std::min(fgMaxMultiplier(), 4) : fgMaxMultiplier();
    QVariantList out;
    // 档位与作者原版一致：只有数字档，不再有「自动」。自动改由单独的
    // 自动选择倍率 开关控制（2026-10-24 用户要求）。
    const auto choices=backend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    for (const auto choice : choices) {
        const int m = int(choice);
        if (m < 2 || m > ceiling) continue;
        QVariantMap item;
        item["id"] = QString::number(m);
        item["label"] = QString::number(m) + QStringLiteral("X");
        out << item;
    }
    return out;
}

// 自动选择倍率只对 DLSS 和 VFG 开放：这两家的倍率选择足够宽，值得按上限去凑。
// FSR 只有 2X、XeSS 最多 4X 且由提供方自报能力，自动在它们身上没有可调的余地。
bool QmlPlayerBridge::fgAutoMultiplierAvailable() const {
    const auto backend = settings().frameGenerationBackend;
    return backend == engine::FrameGenerationBackend::Dlss ||
           backend == engine::FrameGenerationBackend::Vfg;
}

bool QmlPlayerBridge::fgAutoMultiplier() const { return impl_->chain.fgAutoMultiplier; }

void QmlPlayerBridge::setFgAutoMultiplier(bool value) {
    if (!fgAutoMultiplierAvailable()) {
        if (value) emit notice(tr("自动倍率只支持 DLSS 帧生成和 NVIDIA VFG"), true);
        return;
    }
    if (impl_->chain.fgAutoMultiplier == value) return;
    const auto before = impl_->chain.fgAutoMultiplier;
    impl_->chain.fgAutoMultiplier = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgAutoMultiplier = before; impl_->revalidate();
        emit notice(uiText(error), true); return;
    }
    // 打开自动选择时，若补帧正被关着就抬到 2X，让下一次播放有档可用。
    // 注意：不覆盖用户已选的具体档位（关闭自动后仍按那个档跑）。
    if (value && impl_->chain.fgMultiplier < 2) impl_->chain.fgMultiplier = 2;
    // 重新打开必须让它再决策一次：fgAutoSession 记的是"本会话已经算过档位"，
    // 用户中途关掉再打开时，不清这个标记就不会重新生效（2026-10-24 实测）。
    if (value) impl_->fgAutoSession = 0;
    veyra::log::info("ui-fg-auto", std::format("auto={} backend={} multiplier={}{}",
        value, engine::frameGenerationBackendName(settings().frameGenerationBackend), impl_->chain.fgMultiplier,
        value ? " (session decision reset)" : " (author-default multiplier logic)"));
    emit settingsChanged(); emitChainChanged();
}

// 目标帧率：输出上限 = 自定义/跟随显示器 时用它的值，关闭时用显示器实际刷新率。
// 打开片源之前也要能算（那时 snapshot 还没有本会话的数据），所以两个入口共用它：
// 一个读引擎已解析的 snapshot，一个读待下发的 presentation + 最近一次上报的刷新率。
static double fgAutoTargetRate(const engine::PresentationSettings& effective,
                               const engine::PresentationSettings& requested,
                               double reportedRefreshHz) {
    // 引擎解析过的有效值优先（跟随显示器时那里已经是真实 Hz）。
    if (effective.outputRate == engine::OutputRateMode::Custom ||
        effective.outputRate == engine::OutputRateMode::FollowDisplay)
        return effective.customFps;
    // 打开片源之前引擎还没解析过，用待下发的请求值。
    if (requested.outputRate == engine::OutputRateMode::Custom)
        return requested.customFps;
    if (requested.outputRate == engine::OutputRateMode::FollowDisplay)
        return requested.customFps > 0.01 ? requested.customFps : reportedRefreshHz;
    // 两处都说「关闭」：按显示器实际刷新率算。
    return reportedRefreshHz;
}

// 打开片源之前先把倍率定下来，避开播放中重建管线导致的 ~2 秒卡顿。
// sourceFps 来自容器/DASH 元数据；拿不到就返回 false，由首帧后的决策兜底。
bool QmlPlayerBridge::planFgAutoMultiplierBeforeOpen(double sourceFps) {
    if (!(sourceFps > 0.01)) {
        veyra::log::info("fg-auto", std::format("pre-open skipped: no source rate ({:.3f})", sourceFps));
        return false;
    }
    if (!impl_->chain.fgAutoMultiplier) {
        veyra::log::info("fg-auto", "pre-open skipped: automatic selection is off");
        return false;
    }
    if (!fgAutoMultiplierAvailable()) return false;
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) return false;

    const double targetFps = fgAutoTargetRate(impl_->snapshot.presentationEffective,
                                              impl_->presentation,
                                              impl_->snapshot.displayFgRefreshHz);
    if (!(targetFps > 0.01)) {
        veyra::log::info("fg-auto", std::format("pre-open skipped: no target rate (eff={} req={} refresh={:.3f})",
            int(impl_->snapshot.presentationEffective.outputRate), int(impl_->presentation.outputRate),
            impl_->snapshot.displayFgRefreshHz));
        return false;
    }

    const auto backend = settings().frameGenerationBackend;
    const int before = fgMultiplier();
    // 源帧率已达上限：关闭补帧（与首帧后决策同一条规则）。
    int target = 1;
    if (sourceFps < targetFps / engine::kDisplayFgFitTolerance) {
        const unsigned ceiling = unsigned(std::clamp(fgMaxMultiplier(), 2, 8));
        const unsigned wanted = engine::displayFgAutoMultiplier(backend, sourceFps, targetFps, ceiling);
        if (wanted == 0) return false;      // 无可用档：交给首帧后决策
        target = int(wanted);
    }
    if (before == target && node->enabled == (target > 1)) {
        // 已经是这个档：什么都不用改。
        veyra::log::info("fg-auto", std::format("pre-open: already {}X source={:.3f} target={:.3f}",
            target, sourceFps, targetFps));
        return true;
    }
    // 先把 chain 改成目标档，openPath 之后构建管线即用它，不产生二次重建。
    applyFgMultiplier(target);
    // 标记"本会话已按预决策定档"，否则首帧后的决策会再走一遍、把刚建好的管线又拆掉
    // （实测 2026-10-24：pre-open 关掉补帧后，首帧后又关一次，卡 614ms）。
    impl_->fgAutoPlanned = true;
    veyra::log::info("fg-auto", std::format("pre-open: backend={} source={:.3f} target={:.3f} chosen={} previous={} (decided before the graph is built)",
        engine::frameGenerationBackendName(backend), sourceFps, targetFps, target, before));
    return true;
}

// 自动选择倍率 的运行时决策（用户 2026-10-24 定稿）。
// 开关关闭：完全按作者原版逻辑，倍率就是用户选的那个数字档，这里什么都不做。
// 开关开启：
//   · 源帧率低于输出上限 → 按公式挑档（ceil(上限 ÷ (源帧率×1.02))，档位向上取）
//   · 源帧率高于/等于输出上限 → 关闭补帧（节点 enabled=false，开关也跟着关）
//   · 输出上限为「关闭」→ 用显示器实际刷新率当目标，照样自动选
// 只在每个会话首次进入播放时算一次：改倍率要重建 GPU 管线，播放中改会卡顿。
void QmlPlayerBridge::maybeApplyFgAutoMultiplier() {
    const auto& s = impl_->snapshot;
    if (!s.running || s.sessionId == 0) return;
    // 本会话已经决定过：不再动。会话号变化才重新算。
    if (impl_->fgAutoSession == s.sessionId) return;
    // 打开前已按元数据定档：本会话不必再算，否则会把刚建好的管线拆了重建。
    if (impl_->fgAutoPlanned) {
        impl_->fgAutoSession = s.sessionId;
        impl_->fgAutoPlanned = false;
        return;
    }
    if (!impl_->chain.fgAutoMultiplier) { impl_->fgAutoSession = s.sessionId; return; }
    const auto backend = settings().frameGenerationBackend;
    if (!fgAutoMultiplierAvailable()) { impl_->fgAutoSession = s.sessionId; return; }
    // 补帧节点不存在才放弃；节点存在但被关着必须继续决策 —— 上一个片源可能按规则
    // 把补帧关掉了（源帧率超过上限），下一个低帧率片源要能重新打开它。
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { impl_->fgAutoSession = s.sessionId; return; }

    // 目标帧率：输出上限为自定义/跟随显示器时用它的值；关闭时用显示器实际刷新率，
    // 自动选倍数照常工作（用户 2026-10-24 要求）。
    const double targetFps = fgAutoTargetRate(s.presentationEffective, s.presentationEffective,
                                              s.displayFgRefreshHz);
    if (!(targetFps > 0.01)) {
        // 显示器刷新率也没拿到：下一帧再试（不擅自改设置）。
        veyra::log::info("fg-auto", std::format("waiting for a target rate cap={} refresh={:.3f}",
            int(s.presentationEffective.outputRate), s.displayFgRefreshHz));
        return;
    }
    const double sourceFps = s.nominalSourceFps;
    if (!(sourceFps > 0.01)) {
        // 源帧率还没就绪：下一帧再试。
        veyra::log::info("fg-auto", std::format("waiting for source rate target={:.3f}", targetFps));
        return;
    }

    impl_->fgAutoSession = s.sessionId;
    const int before = fgMultiplier();
    const uint64_t session = s.sessionId;

    // 源帧率高于/等于输出上限：补了也显示不出来，关闭补帧。
    if (sourceFps >= targetFps / engine::kDisplayFgFitTolerance) {
        veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} chosen=off previous={} (source at/above the cap: frame generation off)",
            session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, before));
        QTimer::singleShot(0, this, [this, session] {
            if (impl_->snapshot.sessionId != session) return;
            if (!impl_->chain.fgAutoMultiplier) return;
            veyra::log::info("fg-auto", std::format("turning frame generation off session={}", session));
            applyFgMultiplier(1);
        });
        return;
    }

    // 源帧率低于上限：按公式挑档，让它刚好打满上限。
    const unsigned ceiling = unsigned(std::clamp(fgMaxMultiplier(), 2, 8));
    const unsigned wanted = engine::displayFgAutoMultiplier(backend, sourceFps, targetFps, ceiling);
    if (wanted == 0) {
        veyra::log::info("fg-auto", std::format("no usable step backend={} source={:.3f} target={:.3f} ceiling={} (multiplier kept)",
            engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling));
        return;
    }
    const int target = int(wanted);
    veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} ceiling={} chosen={} previous={}",
        session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling, target, before));
    if (before == target && node->enabled) return;
    QTimer::singleShot(0, this, [this, session, target] {
        if (impl_->snapshot.sessionId != session) return;
        if (!impl_->chain.fgAutoMultiplier) return;
        veyra::log::info("fg-auto", std::format("applying {}X session={}", target, session));
        applyFgMultiplier(target);
    });
}

int QmlPlayerBridge::vfgQuality()const{return int(settings().vfgQuality);}void QmlPlayerBridge::setVfgQuality(int value){
    if(value<0||uint32_t(value)>=engine::kVfgQualityCount)return;auto s=settings();if(s.vfgQuality==uint32_t(value))return;
    s.vfgQuality=uint32_t(value);
    if(!impl_->commit(s)){emit notice(tr("VFG 质量设置未被接受，原设置保留"),true);return;}
    emit settingsChanged();emitChainChanged();
}

bool QmlPlayerBridge::fgStrict() const { return impl_->chain.fgStrictAdmission; }
void QmlPlayerBridge::setFgStrict(bool value) {
    if (impl_->chain.fgStrictAdmission == value) return;
    impl_->chain.fgStrictAdmission = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgStrictAdmission = !value; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged();
    emitChainChanged();
}

// 低延迟队列 is the presentation queue (1.4.4 PresentationSettings.enabled),
// not EnhancementSettings::lowLatency, which means "NR before SR".
bool QmlPlayerBridge::fgLowQueue() const { return impl_->presentation.enabled; }
void QmlPlayerBridge::setFgLowQueue(bool value) {
    if (impl_->presentation.enabled == value) return;
    impl_->presentation.enabled = value;
    applyPresentation();
}

// --- colour ------------------------------------------------------------------
double QmlPlayerBridge::colorExposure() const { return selectedColourSettings().exposure; }
void QmlPlayerBridge::setColorExposure(double v) {
    setColourParameter(QStringLiteral("exposure"),v);
}
double QmlPlayerBridge::colorContrast() const { return selectedColourSettings().contrast; }
void QmlPlayerBridge::setColorContrast(double v) {
    setColourParameter(QStringLiteral("contrast"),v);
}
double QmlPlayerBridge::colorSaturation() const { return selectedColourSettings().saturation; }
void QmlPlayerBridge::setColorSaturation(double v) {
    setColourParameter(QStringLiteral("saturation"),v);
}
double QmlPlayerBridge::colorTemperature() const { return selectedColourSettings().temperature; }
void QmlPlayerBridge::setColorTemperature(double v) {
    setColourParameter(QStringLiteral("temperature"),v);
}

void QmlPlayerBridge::resetCurrentPage() {
    // Reset only the enhancement settings this page edits. Presets, recent files,
    // the capture session and the export selection are deliberately untouched.
    auto s = settings();
    s.nr = false; s.sr = false; s.multiplier = 1;
    s.lowLatency = false; s.nrTemporal = false;
    s.nrLayers = {}; s.nrLayerCount = 0;
    s.model = engine::NrSettings{};
    s.residual = engine::ResidualSettings{};
    s.protection = engine::ProtectionSettings{};
    s.color = engine::ColorSettings{};
    s.additionalColors = {}; s.additionalColorCount = 0;
    s.antiAlias = engine::AntiAliasSettings{}; s.additionalAntiAlias = {}; s.additionalAntiAliasCount = 0;
    s.beauty = engine::BeautySettings{}; s.additionalBeauty = {}; s.additionalBeautyCount = 0; s.beautyDetection = {};
    s.videoHdr = engine::VideoHdrSettings{};
    const auto before=impl_->chain; const auto selected=impl_->selectedNr;
    const auto oldLayout = impl_->layout;
    const auto selectedColour=impl_->selectedColour;
    impl_->chain=engine::toChain(s); impl_->selectedNr=-1; impl_->selectedColour=-1;
    impl_->chain.mode = before.mode;
    if (before.mode == engine::ChainMode::Node) {
        engine::removeLegacyNodeProtection(impl_->chain);
        const auto initialized = impl_->layout.initialize(impl_->chain);
        if (!initialized.accepted) {
            impl_->chain = before; impl_->layout = oldLayout;
            impl_->selectedNr = selected; impl_->selectedColour = selectedColour;
            emit notice(uiText(initialized.message), true); return;
        }
        impl_->initializeDefaultNodePositions();
    }
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;
        impl_->chain=before;impl_->layout=oldLayout;impl_->selectedNr=selected;impl_->selectedColour=selectedColour;impl_->revalidate();
        emit notice(uiText(error),true);
    }
    emit settingsChanged();
    emitChainChanged();
}


// --- shell preferences -------------------------------------------------------
bool QmlPlayerBridge::reducedMotion() const { return impl_->facade.reducedMotion(); }
int QmlPlayerBridge::uiDeviceGeneration() const { return impl_->uiDeviceGeneration; }
void QmlPlayerBridge::noteUiDeviceRecreated(const QString& window) {
    ++impl_->uiDeviceGeneration;
    veyra::log::warn("ui-device", std::format("{} scene graph recreated after a lost graphics device; icons rebuilt generation={}",
                                              window.toStdString(), impl_->uiDeviceGeneration));
    emit uiDeviceGenerationChanged();
}
void QmlPlayerBridge::setReducedMotion(bool value) {
    if (impl_->facade.reducedMotion() == value) return;
    impl_->facade.setReducedMotion(value);
    emit settingsChanged();
}
QString QmlPlayerBridge::defaultPage() const { return utf8Of(impl_->facade.defaultPage()); }
void QmlPlayerBridge::setDefaultPage(const QString& value) {
    if (utf8Of(impl_->facade.defaultPage()) == value) return;
    impl_->facade.setDefaultPage(wideOf(value));
    emit settingsChanged();
}
QString QmlPlayerBridge::defaultPageLabel() const {
    const QString page = defaultPage();
    if (page == QLatin1String("min")) return tr("极简模式");
    if (page == QLatin1String("pro")) return tr("专业模式");
    if (page == QLatin1String("last")) return tr("上次");
    return tr("首页");
}

QString QmlPlayerBridge::initialPage() const {
    // The saved preference unless a test override was set; "last" reopens the
    // page the previous session ended on.
    if (!impl_->initialPageOverride.isEmpty()) return impl_->initialPageOverride;
    if (defaultPage() == QLatin1String("last")) {
        const QString last = impl_->prefString("lastPage");
        return last.isEmpty() ? QStringLiteral("home") : last;
    }
    return defaultPage();
}
void QmlPlayerBridge::setInitialPage(const QString& value) {
    impl_->initialPageOverride = value;
    emit settingsChanged();
}

QString QmlPlayerBridge::currentPage() const { return impl_->currentPage; }
void QmlPlayerBridge::setCurrentPage(const QString& value) {
    impl_->currentPage = value;
    static const QStringList remembered{"home", "min", "pro", "node", "set", "exp"};
    if (remembered.contains(value) && impl_->prefString("lastPage") != value) {
        impl_->prefs["lastPage"] = value == QLatin1String("node") ? QStringLiteral("pro") : value;
        impl_->savePrefs();
    }
}

QString QmlPlayerBridge::remotePlayState() const {
    // The engine's own remote-play state word, or empty when there is none. No
    // invented "connected" wording.
    return uiText(impl_->snapshot.remoteRecoveryMessage);
}

QVariantList QmlPlayerBridge::componentList() const {
    if (!impl_->componentListRead) { impl_->componentList = readComponentList(); impl_->componentListRead = true; }
    return impl_->componentList;
}
void QmlPlayerBridge::refreshComponentList() {
    auto list = readComponentList();
    impl_->componentListRead = true;
    if (list == impl_->componentList) return;
    impl_->componentList = std::move(list);
    emit componentListChanged();
}
QVariantList QmlPlayerBridge::readComponentList() const {
    // Read from the runtime manifest the build produces rather than a list written
    // by hand here, so the page cannot claim a component the package lacks.
    QVariantList out;
    const auto vfgDirectory=pipeline::VfgBackend::runtimeDirectory(runtime::localRuntimeDirectory().wstring());
    const bool vfgAvailable=pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring());
    out<<QVariantMap{{"name",QStringLiteral("NVIDIA VFG")},{"detail",QString::fromStdWString(vfgDirectory)},
        {"loaded",vfgAvailable},{"experimental",true}};
    const auto manifest = runtime::localRuntimeDirectory() / L"release-runtime-manifest.json";
    QFile file(QString::fromWCharArray(manifest.c_str()));
    if (!file.open(QIODevice::ReadOnly)) {
        // No manifest is a reportable fact, not a reason to invent entries.
        return out;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto entries = document.isArray() ? document.array()
                                            : document.object().value(QStringLiteral("files")).toArray();
    for (const auto& value : entries) {
        const auto object = value.toObject();
        QVariantMap item;
        item["name"] = object.value(QStringLiteral("name")).toString();
        // Release manifests write fileVersion/authenticode; keep the short keys too.
        auto field = [&](const char* primary, const char* fallback) {
            const QString text = object.value(QLatin1String(primary)).toString();
            return text.isEmpty() ? object.value(QLatin1String(fallback)).toString() : text;
        };
        const QString version = field("version", "fileVersion");
        const QString signature = field("signature", "authenticode");
        const bool experimental = object.value(QStringLiteral("experimental")).toBool();
        // Several runtimes share a file name (the NR slots); the folder tells them apart.
        const QString path = object.value(QStringLiteral("path")).toString();
        const QString folder = path.section(QLatin1Char('/'), 0, -2);
        QStringList detail;
        for (const QString& part : {version, signature, folder})
            if (!part.isEmpty()) detail << part;
        item["detail"] = detail.join(QStringLiteral(" · "));
        item["loaded"] = object.value(QStringLiteral("loaded")).toBool(true);
        item["experimental"] = experimental;
        if (!item["name"].toString().isEmpty()) out << item;
    }
    return out;
}

void QmlPlayerBridge::openProjectPage() {
    // 关于 → 交流群: the group QR codes are kept current in the project README.
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo#readme"))))
        emit notice(tr("无法打开浏览器"), true);
}

void QmlPlayerBridge::copyDiagnostics() {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnosticsReport());
        emit notice(tr("诊断信息已复制"), false);
    }
}


// --- capture dialog ----------------------------------------------------------
static QString captureDeviceUiLabel(const source::CaptureDevice& device) {
    return source::decklink::isPath(device.path)
        ? uiText(device.name).replace(QStringLiteral("跟随驱动输入"), uiText(L"跟随驱动输入")) : utf8Of(device.name);
}
QVariantList QmlPlayerBridge::captureDevices() const {
    // The source's own enumeration (asynchronous, see refreshCaptureDevices). An
    // empty list means the machine reports no capture devices.
    QVariantList out;
    for (const auto& d : impl_->captureVideo) {
        const bool native=source::decklink::isPath(d.path);
        const bool enabled=impl_->prefBool("blackmagicNativeCapture",false);
        if(native&&!enabled)continue;
        if(!native&&enabled&&source::isBlackmagicCaptureDevice(d.name,d.path))continue;
        out << QVariantMap{{"id", utf8Of(d.path)}, {"label", captureDeviceUiLabel(d)}, {"embeddedAudio", d.hasEmbeddedAudio}};
    }
    return out;
}
QString QmlPlayerBridge::captureDeviceId() const { return utf8Of(impl_->captureDevice); }
void QmlPlayerBridge::setCaptureDeviceId(const QString& value) {
    if (impl_->captureDriverSettingsOpen) return;
    if (impl_->captureDevice == wideOf(value)) return;
    impl_->captureDevice = wideOf(value);
    impl_->captureFormatList.clear();++impl_->captureFormatGeneration;
    impl_->captureResolutionChoice.clear();impl_->captureRateChoice.clear();
    impl_->captureRequestedResolution.clear();
    impl_->captureFormatKey=impl_->captureDevice==impl_->capturePrefs.videoPath?impl_->capturePrefs.formatKey:std::wstring{};
    impl_->captureDriverFormatKey.clear();
    // Colour follows the device, as in 1.4.4; audio returns to "none" unless it
    // is the remembered device.
    impl_->captureColor = impl_->capturePrefs.colorForDevice(impl_->captureDevice);
    if(impl_->captureDevice==impl_->capturePrefs.videoPath)impl_->restoreCaptureForm();
    else {impl_->captureAudioChoice=source::kCaptureAudioDisabled;impl_->captureAudioMissing=false;}
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    if (!impl_->captureDevice.empty()) queryCapture(1, impl_->captureDevice);
    emit captureChanged();
}
QString QmlPlayerBridge::captureDeviceLabel() const {
    for (const auto& d : impl_->captureVideo) if (d.path == impl_->captureDevice) return captureDeviceUiLabel(d);
    return impl_->captureDevice.empty() ? tr("未选择设备") : tr("设备未连接");
}
static QString captureFormatUiLabel(const source::CaptureFormat& f, const std::wstring& device) {
    auto label=uiText(f.label);
    if(source::decklink::isPath(device))label=label.replace(QStringLiteral("逐行"),QmlPlayerBridge::tr("逐行")).replace(QStringLiteral("隔行"),QmlPlayerBridge::tr("隔行"));
    if(source::captureMjpegFormat(f))label+=uiText(L"（不建议使用）");
    return label;
}
QVariantList QmlPlayerBridge::captureFormats() const {
    return impl_->captureList(impl_->captureFormatsCache, impl_->captureDevice, {}, [this] {
        QVariantList out;
        out.reserve(qsizetype(impl_->captureFormatList.size()));
        for (const auto& f : impl_->captureFormatList) {
            out << QVariantMap{{"id", utf8Of(f.key)}, {"label", captureFormatUiLabel(f, impl_->captureDevice)}, {"tier", f.tier},
                               {"costHint", source::captureFormatNeedsCostHint(static_cast<source::CaptureFormatTier>(f.tier))}, {"driverCurrent", f.driverCurrent},
                               {"width", int(f.width)}, {"height", int(f.height)}, {"fps", f.fps}, {"pixel", uiText(f.pixel)},
                               {"scan", uiText(f.scan)}, {"interlaced", f.interlaced}, {"disabled", !f.supported}, {"rangeCandidate", f.rangeCandidate}, {"danger",source::captureMjpegFormat(f)}};
        }
        return out;
    });
}
QString QmlPlayerBridge::captureFormatKey() const { return utf8Of(impl_->captureFormatKey); }
QString QmlPlayerBridge::captureFormatLabel() const {
    if (impl_->captureFormatKey.empty()) return {};
    for (const auto& f : impl_->captureFormatList)
        if (f.key == impl_->captureFormatKey) return captureFormatUiLabel(f, impl_->captureDevice);
    return {};
}
bool QmlPlayerBridge::captureFormatCostHint() const {
    if (impl_->captureFormatKey.empty()) return false;
    for (const auto& f : impl_->captureFormatList)
        if (f.key == impl_->captureFormatKey) return source::captureFormatNeedsCostHint(static_cast<source::CaptureFormatTier>(f.tier));
    return false;
}
void QmlPlayerBridge::setCaptureFormatKey(const QString& key) {
    const auto* format=source::selectCaptureFormat(impl_->captureFormatList,-1,wideOf(key));
    if(!format){
        // Retain the attempted identity as invalid, so a subsequent Connect
        // cannot silently open the previous selection instead.
        impl_->captureFormatKey=wideOf(key);
        impl_->captureStatus=tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。");
        emit notice(impl_->captureStatus,true);emit captureChanged();return;
    }
    if (impl_->captureFormatKey == format->key && impl_->captureResolutionChoice == source::captureResolutionId(*format) && impl_->captureRateChoice == source::captureFrameRateGroupId(*format)) return;
    impl_->captureResolutionChoice=source::captureResolutionId(*format);
    impl_->captureRateChoice=source::captureFrameRateGroupId(*format);
    impl_->captureFormatKey = format->key;
    impl_->captureStatus.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
namespace {
QVariantList captureChoiceList(const std::vector<source::CaptureFormatOption>& options) {
    QVariantList out;
    for (const auto& option : options)
        out << QVariantMap{{"id", utf8Of(option.id)}, {"label", uiText(option.label)+(option.discouraged?uiText(L"（不建议使用）"):QString{})}, {"disabled",option.disabled},{"danger",option.discouraged}};
    return out;
}
const source::CaptureFormat* selectedCaptureFormat(const std::vector<source::CaptureFormat>& formats, const std::wstring& key) {
    return source::selectCaptureFormat(formats, -1, key);
}
}
QVariantList QmlPlayerBridge::captureResolutionOptions() const {
    return impl_->captureList(impl_->captureResolutionCache, {}, {}, [this] { return captureChoiceList(source::captureResolutionOptions(impl_->captureFormatList)); });
}
QString QmlPlayerBridge::captureResolutionId() const {
    return utf8Of(impl_->captureResolutionChoice);
}
void QmlPlayerBridge::setCaptureResolutionId(const QString& id) {
    QString normalized=id.trimmed().toLower();normalized.remove(QLatin1Char(' '));
    normalized.replace(QChar(0x00D7),QLatin1Char('x'));
    const auto wide = wideOf(normalized);
    if(impl_->captureResolutionChoice==wide)return;
    const auto* sample=source::captureResolutionSample(impl_->captureFormatList,wide);
    if(!sample){
        const auto hint=source::captureResolutionHint(wide);
        if(hint.width&&hint.height&&!impl_->captureDevice.empty()&&!source::decklink::isPath(impl_->captureDevice)){
            impl_->captureRequestedResolution=wide;queryCapture(4,impl_->captureDevice);return;
        }
        emit notice(tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return;
    }
    impl_->captureResolutionChoice=wide;
    if(!source::captureRateSample(impl_->captureFormatList,wide,impl_->captureRateChoice))
        if(const auto* rate=source::captureDefaultFrameRate(impl_->captureFormatList,wide))impl_->captureRateChoice=source::captureFrameRateGroupId(*rate);
    impl_->captureFormatKey.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
QVariantList QmlPlayerBridge::captureFrameRateOptions() const {
    // Common rates only unless 显示全部可用帧率 is on (captureVisibleFrameRates).
    const bool all = impl_->prefBool("captureShowAllFrameRates", false);
    return impl_->captureList(impl_->captureRateCache, impl_->captureResolutionChoice, impl_->captureRateChoice + (all ? L"|all" : L""), [this, all] {
        size_t hidden = 0;
        const auto rates = source::captureVisibleFrameRates(source::captureFrameRateOptions(impl_->captureFormatList,
            source::captureResolutionSample(impl_->captureFormatList, impl_->captureResolutionChoice)), impl_->captureRateChoice, all, &hidden);
        impl_->captureHiddenRates = int(hidden);
        return captureChoiceList(rates);
    });
}
int QmlPlayerBridge::captureHiddenFrameRates() const {
    (void)captureFrameRateOptions();   // the count is built with the list
    return impl_->captureHiddenRates;
}
QString QmlPlayerBridge::captureFrameRateId() const {
    return utf8Of(impl_->captureRateChoice);
}
void QmlPlayerBridge::setCaptureFrameRateId(const QString& id) {
    const auto wide = wideOf(id);
    if(impl_->captureResolutionChoice.empty()){emit notice(tr("请先选择分辨率"),true);return;}
    if(impl_->captureRateChoice==wide)return;
    if(!source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,wide)){
        emit notice(tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return;
    }
    const auto* sample=source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,wide);
    impl_->captureRateChoice=source::captureFrameRateGroupId(*sample);impl_->captureFormatKey.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
QVariantList QmlPlayerBridge::capturePixelOptions() const {
    return impl_->captureList(impl_->capturePixelCache, impl_->captureResolutionChoice, impl_->captureRateChoice, [this] {
        return captureChoiceList(source::capturePixelOptions(impl_->captureFormatList, source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice)));
    });
}
QString QmlPlayerBridge::capturePixelId() const {
    const auto* format = source::selectCapturePixelFormat(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice,impl_->captureFormatKey);
    return format ? utf8Of(format->key) : QString();
}
void QmlPlayerBridge::setCapturePixelId(const QString& id) {
    const auto key = wideOf(id);
    if(impl_->captureResolutionChoice.empty()){emit notice(tr("请先选择分辨率"),true);return;}
    if(!source::selectCapturePixelFormat(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice,key)){
        impl_->captureFormatKey=key;
        impl_->captureStatus=tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。");
        emit notice(impl_->captureStatus,true);emit captureChanged();return;
    }
    setCaptureFormatKey(id);
}
QVariantList QmlPlayerBridge::captureAudioInputs() const {
    // Only what the user picks explicitly is ever opened (1.4.4 rule).
    QVariantList out{QVariantMap{{"id", source::kCaptureAudioDisabled}, {"label", tr("不监听音频")}}};
    if(impl_->captureAudioMissing)out<<QVariantMap{{"id",-4},{"label",tr("上次的音频输入未连接")},{"disabled",true}};
    for (const auto& d : impl_->captureVideo)
        if (d.path == impl_->captureDevice)
            out << QVariantMap{{"id", source::kCaptureAudioFromVideoDevice},
                               {"label", d.hasEmbeddedAudio ? tr("视频设备内置音频（已检测）") : tr("尝试视频设备内置音频")}};
    for (size_t i = 0; i < impl_->captureAudio.size(); ++i)
        out << QVariantMap{{"id", int(i)}, {"label", QStringLiteral("[%1] %2").arg(impl_->captureAudio[i].wasapi ? QStringLiteral("WASAPI") : QStringLiteral("DirectShow"), utf8Of(impl_->captureAudio[i].name))}};
    return out;
}
int QmlPlayerBridge::captureAudioChoice() const { return impl_->captureAudioMissing?-4:impl_->captureAudioChoice; }
void QmlPlayerBridge::setCaptureAudioChoice(int choice) {
    if (choice < source::kCaptureAudioFromVideoDevice || choice >= int(impl_->captureAudio.size())) return;
    impl_->captureAudioChoice = choice;
    impl_->captureAudioMissing=false;
    // Remember the audio choice for this device as soon as it is picked, not only on
    // 连接并开始 (field report 2026-10-01: picked, closed, and it was gone next time).
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
int QmlPlayerBridge::captureColorSpace() const { return int(source::captureColorSpace(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorSpace(int value) {
    if (value < 0 || value > 3) return;
    impl_->captureColor = source::captureColorOverride(unsigned(value), source::captureColorRange(impl_->captureColor));
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
int QmlPlayerBridge::captureColorRange() const { return int(source::captureColorRange(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorRange(int value) {
    if (value < 0 || value > 2) return;
    impl_->captureColor = source::captureColorOverride(source::captureColorSpace(impl_->captureColor), unsigned(value));
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
double QmlPlayerBridge::captureRequestedFps() const { return impl_->captureFps; }
void QmlPlayerBridge::setCaptureRequestedFps(double value) {
    if (!source::validCaptureFrameRate(value)) { emit notice(tr("采集帧率请输入 1–1000 的数字（可带小数），或 0 沿用设备默认"), true); return; }
    if (impl_->captureFps == value) return;
    impl_->captureFps = value;
    // Saved as soon as it is typed, as the audio choice is (field report 2026-10-01: the
    // new rate was lost unless another option was changed before closing).
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    veyra::log::info("capture-ui", std::format("requested fps={}", value));
    emit captureChanged();
}
int QmlPlayerBridge::captureAudioIngress() const { return int(settings().captureAudio); }
void QmlPlayerBridge::setCaptureAudioIngress(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureAudio = static_cast<engine::CaptureAudioIngress>(value);
    if(impl_->commit(std::move(s))){impl_->prefs["captureAudioIngress"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
int QmlPlayerBridge::captureBufferMode() const { return int(settings().captureBuffer); }
void QmlPlayerBridge::setCaptureBufferMode(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureBuffer = static_cast<source::CaptureBufferMode>(value);
    if(impl_->commit(std::move(s))){impl_->prefs["captureBufferMode"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
bool QmlPlayerBridge::captureQueryBusy() const { return impl_->captureBusy || impl_->captureDriverSettingsOpen; }
QString QmlPlayerBridge::captureStatus() const { return impl_->captureStatus; }
bool QmlPlayerBridge::captureBlackmagicDevice() const {
    for (const auto& d : impl_->captureVideo) if (d.path == impl_->captureDevice) return source::decklink::isPath(d.path)||source::isBlackmagicCaptureDevice(d.name, d.path);
    return false;
}
bool QmlPlayerBridge::openCaptureDriverSettings(bool inputSelector) {
    auto& i = *impl_;
    if (captureQueryBusy() || i.captureDevice.empty()) { emit notice(tr("请先选择设备并等待查询完成"), true); return false; }
    if (i.openingSource || (i.snapshot.capture && !i.engine.idle())) { emit notice(tr("请等待连接结束并断开采集，再打开驱动设置"), true); return false; }
    HWND owner = i.videoWindow ? GetAncestor(i.videoWindow, GA_ROOT) : nullptr;
    if (!owner) if (auto* window = QGuiApplication::focusWindow()) owner = reinterpret_cast<HWND>(window->winId());
    i.captureDriverSettingsOpen = true; emit captureChanged();
    std::wstring error;
    const bool ok = source::CaptureCardSource::showDeviceProperties(i.captureDevice, reinterpret_cast<uintptr_t>(owner), inputSelector, error, &i.captureDriverFormatKey);
    i.captureDriverSettingsOpen = false;
    if (!ok) { i.captureStatus = uiText(error); emit notice(i.captureStatus, true); emit captureChanged(); return false; }
    // Some property pages change only this filter instance's IAMStreamConfig.
    // Carry its exact identity to the new graph instead of discarding it.
    queryCapture(3, i.captureDevice);
    return true;
}
bool QmlPlayerBridge::captureMagewellDevice() const { return source::magewell::isProCaptureDevicePath(impl_->captureDevice); }
QString QmlPlayerBridge::captureMagewellStatus() const {
    std::wstring runtime;
    if (!source::magewell::runtimeAvailable(&runtime))
        return tr("找不到美乐威运行库 LibMWCapture.dll（应在软件的 runtime\\magewell 文件夹里，或装美乐威驱动/SDK）");
    return veyra::ui::i18n::text(source::magewell::statusText());
}
void QmlPlayerBridge::refreshCaptureDevices() {
    if (impl_->captureDriverSettingsOpen) return;
    impl_->capturePrefs = ui::CapturePreferenceStore(impl_->dataDir).load();
    queryCapture(0, {});
}
void QmlPlayerBridge::queryCapture(int kind, std::wstring device) {
    auto& i = *impl_;
    if (captureQueryBusy()) { i.captureNextQuery=std::pair{kind,std::move(device)};return; }
    i.captureBusy = true; i.captureQueryKind = kind;
    i.captureStatus = kind == 0 ? tr("正在查询采集设备…当前播放继续") : tr("正在读取设备支持的格式…");
    const auto preferred=kind==2?i.capturePrefs.formatKey:
        kind==3&&!i.captureDriverFormatKey.empty()?i.captureDriverFormatKey:i.captureFormatKey;
    const auto requested=kind==4?i.captureRequestedResolution:
        preferred.empty()&&wideOf(i.prefs.value("captureDraftDevice").toString())==device?
            wideOf(i.prefs.value("captureDraftResolution").toString()):std::wstring{};
    i.captureQuery = std::async(std::launch::async, [kind, device, preferred, requested] {
        Impl::CaptureQuery q; q.kind = kind; q.device = device;q.requestedResolution=requested;
        const auto initialized=CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if(FAILED(initialized)){q.info.code=initialized;q.info.stage=L"COM initialization";return q;}
        try {
            if (kind == 0) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = source::CaptureCardSource::deviceDetails(true); }
            else {
                if (kind == 2) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = source::CaptureCardSource::deviceDetails(true); }
                q.formats = source::CaptureCardSource::formatsByPath(device,&q.info,preferred,requested);
            }
        } catch (...) {q.info.code=E_FAIL;q.info.stage=L"Capture capability exception";}
        CoUninitialize();
        return q;
    });
    emit captureChanged();
}
void QmlPlayerBridge::tickBiliList() {
    auto& i = *impl_;
    // 每个槽位单独回收：qualities 和 related 同时在路上时各自发自己的信号，
    // 一次遍历处理所有已完成的，不会互相顶替。
    // Collect each slot on its own: qualities and related can be in flight together and
    // each emits its own signal, so one sweep drains everything that finished.
    for (size_t n = 0; n < i.biliListSlots.size(); ++n) {
        auto& slot = i.biliListSlots[n];
        if (!slot.busy) {
            // 槽位空着但有排队的请求：启动下一个。
            // The slot is free and something is queued: start the next one.
            if (!slot.pending.empty()) {
                auto next = std::move(slot.pending.front());
                slot.pending.erase(slot.pending.begin());
                if (!next.kind.isEmpty()) {
                    startListWorker(next.kind, next.generation);
                }
            }
            continue;
        }
        if (slot.query.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;

        auto r = slot.query.get();
        slot.busy = false;
        // A cancelled or superseded request drops its result: the user has already asked
        // for something else and must not see this one land afterwards.
        if (slot.kind.isEmpty()) continue;
        const QString kind = slot.kind;
        const int generation = slot.generation;
        slot.kind.clear();

        const bool objectRequest = kind.startsWith(QLatin1String("detail:"));
        if (!r.ok) {
            i.bilibiliError = r.error;
            if (objectRequest) emit bilibiliObjectReady(kind, QVariantMap{}, generation);
            else emit bilibiliListReady(kind, QVariantList{}, generation);
            continue;
        }
        i.bilibiliError.clear();
        if (objectRequest) emit bilibiliObjectReady(kind, r.object, generation);
        else emit bilibiliListReady(kind, r.items, generation);
    }
}

void QmlPlayerBridge::tickBiliPlay() {
    auto& i = *impl_;
    auto& job = i.biliPlayJob;
    if (!job.busy) return;
    // 只在 worker 真正完成时才取结果；done 由 worker 最后一步置位。
    if (!job.result || !job.result->done.load()) return;
    if (job.query.valid()) job.query.get();   // 回收线程；异常在 worker 里不会抛到这里
    auto result = std::move(job.result);
    job.busy = job.resolving = false;
    // 期间用户又点了别的（新请求把 generation 顶掉）：这次结果作废。
    if (job.generation == 0 || !result) return;
    if (result->reportCid > 0)
        veyra::log::info("bilibili", std::format("watch progress reported bvid={} played_time={} ok={}",
            utf8Of(result->reportBvid).toStdString(), result->reportPlayed,
            result->reportOk ? 1 : 0));
    const auto& urls = result->urls;
    if (!urls.ok) {
        emit notice(QString::fromStdWString(urls.error), true);
        return;
    }
    const std::wstring pageTitle = job.pageTitle;
    i.pendingTitle = pageTitle;
    // Backup CDN: remember the full mirror list so a failed open can try the next.
    i.biliMirrors = i.prefBool("biliAutoCdn", true) ? urls.videoMirrors
                                                    : std::vector<std::wstring>{};
    i.biliMirrorIndex = 0;
    i.biliRetriesLeft = int(i.biliMirrors.size()) > 1 ? int(i.biliMirrors.size()) - 1 : 0;
    i.biliRetryPending = false;
    i.biliVideoUrl = urls.videoUrl;
    // Remember what is playing so the position can be reported when this play ends.
    i.biliWatchBvid = job.bvid.toStdWString();
    i.biliWatchCid = job.cid;
    i.biliWatchLastPos = -1.0;
    i.biliWatchDuration = 0.0;
    // 开播即写一次历史：这样点开就算「看过」，不必等播完 —— B 站网页播放器也是
    // 一开始播放就出现在历史记录里。**不碰**上面刚设好的记录字段，所以 tick 仍会
    // 继续追踪真实位置，播完时照旧按 -1 上报。
    // （真正的调用在 resumeAt 算完之后：续播必须报续播位置，不能报 0，否则会把 B 站
    //   记录下来的进度清掉，用户下次就没法从原处继续了。）
    // Write the history as soon as playback starts, so opening a video counts as watched
    // without waiting for it to finish - the site's own player behaves the same way. The
    // fields above are left untouched, so the tick keeps tracking the real position and the
    // end still reports -1.
    // (The call itself sits after resumeAt is resolved: a resume must report the resumed
    // position, not 0, or it would wipe the progress the site stored and the next resume
    // would start over.)
    // Where the account left this part, so playback resumes there.
    bool startedFromTop = false;
    if (job.plannedResume) {
        i.biliResumeAt = result->resumeAt;
        if (i.biliResumeAt < 0.0) startedFromTop = true;
    } else {
        i.biliResumeAt = -1.0;
        startedFromTop = job.fromStart;
    }
    // 报「实际起播位置」：续播就是那个续播点，从头播放（或本来就没有进度）才是 0。
    // Report where playback actually starts: the resume point when resuming, 0 only when
    // starting from the top (or when there was no saved position at all).
    reportBiliWatchStartedAsync(i.biliWatchBvid, i.biliWatchCid,
                                i.biliResumeAt > 0.0 ? int(i.biliResumeAt) : 0);
    i.biliResumeSession = 0;
    // 这一路打开的是 B 站来源，记下来：播完后「播放」不能再拿过期地址重开。
    // This open is a Bilibili source, so remember it: after the video ends, Play must
    // not reopen the stale address.
    i.biliSourceActive = true;
    // Kept on impl_ as well: openPath() re-applies them after rebuilding options.
    i.pendingHeaders = urls.headers;
    i.pendingUserAgent = urls.userAgent;
    i.pendingAudioPath = urls.audioUrl;
    i.bilibiliOpenPending = true;
    i.options.network.requestHeaders = urls.headers;
    i.options.network.userAgent = urls.userAgent;
    i.options.audioPath = urls.audioUrl;
    // 自动选择倍率：用 DASH 的 frame_rate 在打开前定档，让管线一次就按对的倍率构建。
    planFgAutoMultiplierBeforeOpen(urls.frameRate);
    QString info = QStringLiteral("%1 × %2  %3")
        .arg(urls.width).arg(urls.height).arg(QString::fromStdWString(urls.codec));
    if (urls.audioUrl.empty()) info += QStringLiteral("  ") + tr("无音频");
    const QString fromTop = startedFromTop ? tr("（从头开始）") : QString();
    if (!pageTitle.empty())
        emit notice(tr("正在播放：") + QString::fromStdWString(pageTitle) + QStringLiteral("  ")
                    + info + fromTop, false);
    else
        emit notice(tr("正在播放：") + info + fromTop, false);
    openPath(QString::fromStdWString(urls.videoUrl));
}

void QmlPlayerBridge::tickCapture() {
    auto& i = *impl_;
    if (!i.captureBusy || i.captureQuery.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto q = i.captureQuery.get();
    i.captureBusy = false;
    if(i.captureNextQuery){auto next=std::move(*i.captureNextQuery);i.captureNextQuery.reset();queryCapture(next.first,std::move(next.second));return;}
    if(FAILED(q.info.code)&&q.formats.empty()){
        if(q.kind!=0&&q.device!=i.captureDevice){emit captureChanged();return;}
        if(q.kind!=0&&q.kind!=4){i.captureFormatList.clear();++i.captureFormatGeneration;i.captureResolutionChoice.clear();i.captureRateChoice.clear();}
        i.captureStatus=tr("读取采集格式失败：%1（0x%2）。请检查设备占用与驱动后重试。")
            .arg(utf8Of(q.info.stage),QString::number(quint32(q.info.code),16).rightJustified(8,QLatin1Char('0')));
        emit notice(i.captureStatus,true);emit captureChanged();return;
    }
    if (q.kind == 0 || q.kind == 2) { i.captureVideo = std::move(q.video); i.captureAudio = std::move(q.audio); i.captureListed = true; }
    // The remembered rate and audio input, matched against the inputs listed now. Both the
    // panel (kind 0) and 继续上次 (kind 2) need it: the resume path skipped it and connected
    // without sound until the panel was opened once (field report 2026-10-02).
    const auto restoreRememberedAudio = [&i] {
        i.restoreCaptureForm();
    };
    if (q.kind == 2 && q.device == i.capturePrefs.videoPath) restoreRememberedAudio();
    if (q.kind == 0) {
        veyra::log::info("capture-ui", std::format("devices video={} audio={}", i.captureVideo.size(), i.captureAudio.size()));
        // Restore the remembered device; otherwise the first one. Never guess audio.
        std::wstring pick;
        const auto choices=captureDevices();
        auto allowed=[&](std::wstring_view path){for(const auto& value:choices)if(wideOf(value.toMap().value("id").toString())==path)return true;return false;};
        if(allowed(i.capturePrefs.videoPath))pick=i.capturePrefs.videoPath;
        // The two APIs are separate identities. Restore an explicitly used
        // endpoint, or select a unique native endpoint; never map by ordinal.
        const bool native=i.prefBool("blackmagicNativeCapture",false);
        const auto remembered=wideOf(i.prefs.value(native?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice").toString());
        if(pick.empty()&&allowed(remembered))pick=remembered;
        if(pick.empty()&&native){std::vector<std::wstring> candidates;for(const auto& d:i.captureVideo)if(source::decklink::isPath(d.path))candidates.push_back(d.path);if(candidates.size()==1)pick=candidates.front();}
        if (pick.empty() && !choices.empty() && i.capturePrefs.videoPath.empty()&&!native) pick=wideOf(choices.front().toMap().value("id").toString());
        i.captureStatus = i.captureVideo.empty() ? tr("未找到采集设备。连接后点击刷新。")
                        : pick.empty() ? tr("上次采集设备未连接。插回后刷新，或手动选择其他设备。") : QString();
        if (!pick.empty()) {
            i.captureDevice.clear();
            setCaptureDeviceId(utf8Of(pick));
            if (pick == i.capturePrefs.videoPath) restoreRememberedAudio();
        }else{setCaptureDeviceId({});if(native)i.captureStatus=tr("请选择 BMD 原生设备；若列表中没有，请安装 Desktop Video 驱动。多个设备不会自动猜选。");}
        emit captureChanged();
        return;
    }
    if (q.device != i.captureDevice) { emit captureChanged(); return; }   // stale answer
    if(q.kind==4&&!source::captureResolutionSample(q.formats,q.requestedResolution)){
        i.captureStatus=tr("驱动未提供该分辨率，或尺寸不符合步进要求；原选择已保留。");
        emit notice(i.captureStatus,true);emit captureChanged();return;
    }
    i.captureFormatList = std::move(q.formats);
    ++i.captureFormatGeneration;
    const bool remembered = i.captureDevice == i.capturePrefs.videoPath;
    const std::wstring driverSelection = i.captureDriverFormatKey.empty() ? i.captureFormatKey : i.captureDriverFormatKey;
    if (q.kind == 3) i.captureDriverFormatKey.clear();
    i.captureFormatKey.clear();
    if (q.kind == 3 && !driverSelection.empty()) {
        if (const auto* f = source::selectCaptureFormat(i.captureFormatList, -1, driverSelection)) i.captureFormatKey = f->key;
    } else if (remembered && !i.capturePrefs.formatKey.empty()) {
        if (const auto* f = source::selectCaptureFormat(i.captureFormatList, -1, i.capturePrefs.formatKey)) i.captureFormatKey = f->key;
    }
    i.captureResolutionChoice.clear();i.captureRateChoice.clear();
    if(const auto* f=selectedCaptureFormat(i.captureFormatList,i.captureFormatKey)){
        i.captureResolutionChoice=source::captureResolutionId(*f);i.captureRateChoice=source::captureFrameRateGroupId(*f);
    }else if(remembered&&i.capturePrefs.formatKey.empty()&&wideOf(i.prefs.value("captureDraftDevice").toString())==i.captureDevice){
        const auto resolution=wideOf(i.prefs.value("captureDraftResolution").toString());
        const auto rate=wideOf(i.prefs.value("captureDraftRate").toString());
        if(source::captureResolutionSample(i.captureFormatList,resolution)){
            i.captureResolutionChoice=resolution;
            if(const auto* sample=source::captureRateSample(i.captureFormatList,resolution,rate))i.captureRateChoice=source::captureFrameRateGroupId(*sample);
        }
    }
    if(q.kind==4){
        i.captureResolutionChoice=q.requestedResolution;i.captureFormatKey.clear();
        if(const auto* rate=source::captureDefaultFrameRate(i.captureFormatList,i.captureResolutionChoice))i.captureRateChoice=source::captureFrameRateGroupId(*rate);
        if(!i.persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    }
    if(!i.captureFormatKey.empty()&&!i.persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    i.captureStatus = i.captureFormatList.empty() ? tr("驱动未提供可解析的视频格式，请检查设备与驱动后刷新。")
                    : i.captureFormatKey.empty() ? (!i.capturePrefs.formatKey.empty()?tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"):tr("先选择分辨率，再选择该分辨率支持的帧率和像素格式"))
                    : q.kind == 3 ? tr("驱动设置已关闭。请核对所选格式与设备帧率，再连接。")
                    : source::decklink::isPath(i.captureDevice) ? tr("原生采集先确认有效信号；请在设备列表选择对应的 HDMI/SDI 输入，支持自动检测的卡建议选择自动。Driver 保留驱动当前输入。")
                    : captureBlackmagicDevice() ? tr("请选择与输入信号一致的格式。驱动默认格式不代表检测到的信号。")
                    : tr("连接后使用当前增强设置。格式与音频变更需要重新连接。");
    if(q.info.partial)i.captureStatus=tr("部分采集能力读取失败（%1，0x%2）；当前列表可能不完整，请刷新重试。")
        .arg(utf8Of(q.info.stage),QString::number(quint32(q.info.code),16).rightJustified(8,QLatin1Char('0')));
    veyra::log::info("capture-ui", std::format("formats={} selected={} raw={} rejected={} partial={} fallback={}", i.captureFormatList.size(), !i.captureFormatKey.empty(),q.info.advertised,q.info.rejected,q.info.partial,q.info.fallback));
    emit captureChanged();
    if (q.kind == 2) {
        if (i.captureFormatKey.empty()) emit notice(tr("上次的采集格式不可用，请在采集卡面板重新选择"), true);
        else startCaptureSession();
    }
}
bool QmlPlayerBridge::startCaptureSession() {
    auto& i = *impl_;
    if (captureQueryBusy()) { emit notice(tr("设备查询或驱动设置尚未完成"), true); return false; }
    int device = -1;
    for (size_t k = 0; k < i.captureVideo.size(); ++k) if (i.captureVideo[k].path == i.captureDevice) device = int(k);
    const auto* format = source::selectCapturePixelFormat(i.captureFormatList,i.captureResolutionChoice,i.captureRateChoice,i.captureFormatKey);
    if(source::decklink::isPath(i.captureDevice)!=i.prefBool("blackmagicNativeCapture",false)&&captureBlackmagicDevice()){
        emit notice(tr("采集接口已切换，请刷新并重新选择设备和格式"),true);return false;
    }
    if(device<0){emit notice(tr("请先选择采集设备和格式"),true);return false;}
    if(!format){emit notice(i.captureFormatKey.empty()&&i.capturePrefs.formatKey.empty()?tr("请先选择分辨率和像素格式"):tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return false;}
    if(!format->supported){emit notice(tr("设备提供此格式，但当前播放器不支持其尺寸或像素布局，请选择其他格式"),true);return false;}
    if(i.captureAudioMissing){emit notice(tr("上次的音频输入未连接，请插回设备或重新选择音频监听"),true);return false;}
    const int audio = i.captureAudioChoice;
    const source::CaptureDevice* audioDevice = audio >= 0 && size_t(audio) < i.captureAudio.size() ? &i.captureAudio[size_t(audio)] : nullptr;
    const auto path = source::CaptureCardSource::makeCapturePath(unsigned(device), i.captureVideo[size_t(device)], format->index, audio,
                                                                 audioDevice, i.captureColor, i.captureFps, format->key);
    if (path.empty()) { emit notice(tr("无法生成采集连接参数"), true); return false; }
    if(!i.persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    i.facade.noteCaptureSession({i.captureDevice, format->key, i.facade.presets().entries().empty() ? std::wstring{} : wideOf(currentPresetName()),
                                 i.captureFps > 0 ? i.captureFps : format->fps, true});
    veyra::log::info("capture-ui", std::format("connect format={} audio={} color={} fps={}", QString::fromStdWString(format->label).toStdString(), audio, i.captureColor, i.captureFps));
    i.prefs["captureDeviceName"] = utf8Of(i.captureVideo[size_t(device)].name);
    if(captureBlackmagicDevice())i.prefs[source::decklink::isPath(i.captureDevice)?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice"]=utf8Of(i.captureDevice);
    rememberSource(QStringLiteral("capture"), utf8Of(i.captureVideo[size_t(device)].name));
    i.prefs["captureFormatLabel"] = utf8Of(format->label);
    i.savePrefs();
    // Started beside the capture open, not after it. A running PS5 stream still
    // holds the console's slot until the capture replaces it; the tick starts
    // PS5 手柄控制 once the capture card is live instead.
    if (i.prefBool("ps5CaptureControl", false) && !i.snapshot.remotePlay) startPs5Control(false);
    openSourceUri(path, i.captureVideo[size_t(device)].name);
    emit snapshotChanged();
    return true;
}
void QmlPlayerBridge::openSourceUri(const std::wstring& uri, const std::wstring& label, bool screenFill) {
    // A capture or stream open supersedes a waiting Bilibili mirror retry just as a
    // file open does; this path does not go through openPath, so it bumps the
    // generation itself and drops the retry state that belonged to the old source.
    ++impl_->biliOpenGen;
    impl_->biliMirrors.clear();
    impl_->biliMirrorIndex = 0;
    impl_->biliRetriesLeft = 0;
    impl_->biliRetryPending = false;
    rememberPosition(true);
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    impl_->sourceLabel = label;
    impl_->resumeSession = 0;
    impl_->screenFillActive = screenFill;
    // Honor the dialog's initial viewport before input can be taken over.
    // Later explicit aspect choices still replace this session preset.
    if (!impl_->gameModeUri.empty() && uri == impl_->gameModeUri) impl_->aspectMode = 0;
    const bool screen = uri.find(L"screen") != std::wstring::npos;
    veyra::log::info("capture-ui", "open uri " + QString::fromStdWString(uri).toStdString());
    openAfterCinema(screen ? tr("正在开始屏幕捕获…") : tr("正在打开采集设备 · %1").arg(utf8Of(label)), [this, uri] {
        if (impl_->preOpen) impl_->preOpen();
        impl_->engine.previewView({});
        impl_->engine.open(impl_->videoWindow, uri, impl_->options);
        impl_->openingSessionId = impl_->engine.snapshot().sessionId;
        // Only the exact capture startScreenCapture opened in 游戏模式 is one.
        impl_->gameModeSessionId = !impl_->gameModeUri.empty() && uri == impl_->gameModeUri ? impl_->openingSessionId : 0;
        impl_->gameModeLive = false;
        impl_->gameModeUri.clear();
        updateGameMode();
        refreshSubtitles({});
    });
}

void QmlPlayerBridge::openAfterCinema(const QString& pending, std::function<void()> open) {
    const int gen = ++impl_->liveOpenGen;
    const bool fromHome = impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home");
    if (!fromHome || !impl_->gameModeUri.empty()) {
        // Game mode does not wait for the cinema animation or feature startup.
        if (fromHome) emit navigate(QStringLiteral("min"));
        impl_->pendingLiveText.clear(); open(); return;
    }
    // User report 2026-09-29: the window blacked out and jumped into 极简 when
    // the engine opened in the same instant as the page switch and the window's
    // height spring. Switch first, say that it is connecting, then open once
    // the page (.22s out + 150 ms + rise) and the .7s height spring have settled.
    emit navigate(QStringLiteral("min"));
    impl_->pendingLiveText = pending;
    emit snapshotChanged();
    const int delay = reducedMotion() ? 0 : 820;
    veyra::log::info("ui-live-open", std::format("deferred ms={} gen={}", delay, gen));
    QTimer::singleShot(delay, this, [this, gen, open = std::move(open)] {
        if (gen != impl_->liveOpenGen) { veyra::log::info("ui-live-open", std::format("superseded gen={}", gen)); return; }
        impl_->pendingLiveText.clear();
        veyra::log::info("ui-live-open", std::format("open gen={}", gen));
        open();
        emit snapshotChanged();
    });
}

// Force-SDR and vertical flip are real settings the engine already carries, so
// these are genuine controls rather than placeholders.
bool QmlPlayerBridge::captureForceSdr() const { return settings().forceSdrPreview; }
void QmlPlayerBridge::setCaptureForceSdr(bool value) {
    auto s = settings();
    if (s.forceSdrPreview == value) return;
    s.forceSdrPreview = value;
    if(impl_->commit(std::move(s))){impl_->prefs["captureForceSdr"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
bool QmlPlayerBridge::captureFlipVertical() const { return settings().captureFlipVertical; }
void QmlPlayerBridge::setCaptureFlipVertical(bool value) {
    auto s = settings();
    if (s.captureFlipVertical == value) return;
    s.captureFlipVertical = value;
    if(impl_->commit(std::move(s))){impl_->prefs["captureFlipVertical"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}

// --- screen capture ----------------------------------------------------------
QVariantList QmlPlayerBridge::screenTargets() const {
    // Enumerated from the source for the chosen kind; cached until refreshed so a
    // binding re-evaluation does not walk every window again.
    QVariantList out;
    for (const auto& target : impl_->screenList) {
        QVariantMap item;
        item["id"] = QString::number(target.handle);
        QString label = QString::fromWCharArray(target.name.c_str());
        if (target.width > 0)
            label += QStringLiteral(" · %1x%2").arg(target.width).arg(target.height);
        if (target.refresh > 0) label += QStringLiteral(" · %1Hz").arg(target.refresh);
        item["label"] = label;
        item["width"] = int(target.width);
        item["height"] = int(target.height);
        out << item;
    }
    return out;
}
QVariantMap QmlPlayerBridge::screenOptions() const {
    QVariantMap d{{"kind", 0}, {"method", 0}, {"fps", 0}, {"cursor", true}, {"fill", false},
                  {"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}, {"passthrough", false}};
    const auto saved = impl_->prefs.value("screen").toMap();
    for (auto it = saved.begin(); it != saved.end(); ++it) if (d.contains(it.key())) d[it.key()] = it.value();
    if (d.value("kind").toInt() == 0) d["method"] = 0;           // DXGI is monitors only
    if (d.value("method").toInt() == 1) d["cursor"] = false;     // and has no pointer
    return d;
}
bool QmlPlayerBridge::setScreenOption(const QString& key, const QVariant& value) {
    static const QHash<QString, std::pair<int, int>> ranges{{"kind", {0, 1}}, {"method", {0, 1}}, {"fps", {0, 5}},
        {"left", {0, 16384}}, {"top", {0, 16384}}, {"right", {0, 16384}}, {"bottom", {0, 16384}}};
    auto saved = impl_->prefs.value("screen").toMap();
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok);
        const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) { emit notice(tr("数值超出范围"), true); return false; }
        saved[key] = n;
    } else if (key == QLatin1String("cursor") || key == QLatin1String("fill") || key == QLatin1String("passthrough")) {
        saved[key] = value.toBool();
    } else return false;
    impl_->prefs["screen"] = saved;
    impl_->savePrefs();
    if (key == QLatin1String("kind")) { impl_->screenTarget.clear(); refreshCaptureTargets(); }
    emit captureChanged();
    return true;
}
bool QmlPlayerBridge::startScreenCapture() {
    const auto o = screenOptions();
    source::ScreenCaptureOptions value;
    bool found = false;
    for (const auto& t : impl_->screenList)
        if (QString::number(t.handle) == impl_->screenTarget) { value.kind = t.kind; value.target = t.handle; found = true; }
    if (!found) { emit notice(tr("先选择一个捕获目标"), true); return false; }
    constexpr unsigned rates[] = {0, 30, 60, 120, 144, 240};
    value.method = source::ScreenCaptureMethod(o.value("method").toInt());
    value.fps = rates[std::clamp(o.value("fps").toInt(), 0, 5)];
    value.cursor = o.value("cursor").toBool();
    value.left = o.value("left").toUInt(); value.top = o.value("top").toUInt();
    value.right = o.value("right").toUInt(); value.bottom = o.value("bottom").toUInt();
    // 游戏模式: the real pointer is drawn on top, so the picture carries none (a
    // second, late pointer otherwise), the picture must match the screen 1:1 for
    // clicks to land where they are seen (no crop), and the yellow border goes.
    const bool game = o.value("passthrough").toBool();
    if (game) {
        value.cursor = false; value.borderless = true;
        value.left = value.top = value.right = value.bottom = 0;
        const HWND window = value.kind == source::ScreenTargetKind::Window ? reinterpret_cast<HWND>(value.target) : nullptr;
        impl_->gameMonitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : reinterpret_cast<HMONITOR>(value.target);
        impl_->game.setTarget(window, impl_->gameMonitor, !window);
        impl_->gameModeUri = value.uri();
        // Exclusive fullscreen still cannot be covered by another process.
        QUERY_USER_NOTIFICATION_STATE state{};
        if (SUCCEEDED(SHQueryUserNotificationState(&state)) && state == QUNS_RUNNING_D3D_FULL_SCREEN)
            emit notice(tr("检测到独占全屏的游戏：Windows 不允许其它窗口盖在它上面，请把游戏改成无边框窗口或窗口化"), true);
    } else impl_->gameModeUri.clear();
    veyra::log::info("screen-ui", std::format("start kind={} method={} fps={} cursor={} crop={},{},{},{} fill={} gameMode={}", int(value.kind), int(value.method),
        value.fps, value.cursor, value.left, value.top, value.right, value.bottom, o.value("fill").toBool(), game));
    openSourceUri(value.uri(), wideOf(screenTargetLabel()), o.value("fill").toBool());
    impl_->screenFitClient = impl_->screenFitImage = QSize();
    if (gameMode()) { updateGameGeometry(); updateGameMode(); emit gameModeChanged(); }
    emit compareChanged();
    rememberSource(QStringLiteral("screen"), screenTargetLabel());
    return true;
}
bool QmlPlayerBridge::gameMode() const { return impl_->gameModeSessionId != 0; }
bool QmlPlayerBridge::passthroughActive() const { return impl_->passthroughShown; }
bool QmlPlayerBridge::gameInputActive() const { return impl_->gameInputShown; }
bool QmlPlayerBridge::gameScaledPreview() const {
    return gameMode() && ui::gameViewportScaled(impl_->screenFillActive, impl_->aspectMode);
}
QRectF QmlPlayerBridge::gameVideoRect() const { return impl_->gameVideoRect; }
void QmlPlayerBridge::attachMainWindow(qulonglong nativeHandle) {
    impl_->game.attach(reinterpret_cast<HWND>(nativeHandle));
    for (auto* window:QGuiApplication::allWindows())
        if (qulonglong(window->winId())==nativeHandle) { impl_->gameWindow=window; break; }
}
void QmlPlayerBridge::updateGameGeometry() {
    if (!gameMode() || !impl_->presentation.fullscreen || !impl_->gameWindow) return;
    RECT r=impl_->game.pictureBounds();
    if (gameScaledPreview()) {
        const HWND main = reinterpret_cast<HWND>(impl_->gameWindow->winId());
        POINT origin{};
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(impl_->gameMonitor, &info) || !ClientToScreen(main, &origin)) return;
        r=info.rcMonitor; OffsetRect(&r,-origin.x,-origin.y);
    }
    const qreal scale=impl_->gameWindow->devicePixelRatio();
    if (scale<=0) return;
    const QRectF rect(r.left/scale,r.top/scale,(r.right-r.left)/scale,(r.bottom-r.top)/scale);
    if (!rect.isEmpty() && rect!=impl_->gameVideoRect) {
        impl_->gameVideoRect=rect; emit gameModeChanged();
    }
}
void QmlPlayerBridge::setGamePanelOpen(bool open) {
    if (impl_->gamePanelOpen == open) return;
    impl_->gamePanelOpen = open;
    updateGameMode();
}
int QmlPlayerBridge::gameModeScreenIndex() const {
    if (!impl_->gameMonitor) return -1;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const auto* native = screens[i]->nativeInterface<QNativeInterface::QWindowsScreen>();
        if (native && native->handle() == impl_->gameMonitor) return i;
    }
    return -1;
}
void QmlPlayerBridge::testBlockUi(int ms) {
    if (GetEnvironmentVariableW(L"VEYRA_TEST_UI_BLOCK", nullptr, 0) == 0) return;
    veyra::log::warn("game-mode", std::format("test: GUI thread blocked for {} ms", ms));
    Sleep(DWORD(std::clamp(ms, 0, 20000)));
}
// 游戏模式 state: click-through while the game-mode capture is live and the picture
// is fullscreen, the panel while the Home panel is open, otherwise a normal window.
void QmlPlayerBridge::updateGameMode() {
    using Mode = ui::GamePassthrough::Mode;
    auto& i = *impl_;
    const auto& s = i.snapshot;
    if (i.gameModeSessionId) {
        const bool same = s.sessionId == i.gameModeSessionId;
        if (same && s.running && !s.failed) i.gameModeLive = true;
        const bool ended = (same && (s.failed || s.transport == engine::TransportState::Stopping ||
            s.transport == engine::TransportState::Empty || (i.gameModeLive && !s.running))) || s.sessionId > i.gameModeSessionId;
        if (ended) {
            const bool shown = i.game.mode() != Mode::Off;
            veyra::log::info("game-mode", std::format("capture session {} ended failed={} pictureInFront={}", i.gameModeSessionId, s.failed, shown));
            i.gameModeSessionId = 0; i.gameModeLive = false;
            if (shown) {
                i.gameInputShown = i.passthroughShown = false;
                emit gameModeChanged();
                i.game.apply(Mode::Off);
                const QString why = s.failed ? uiText(s.status) : QString();
                emit gameModeStopped(why.isEmpty() ? tr("捕获已结束，已退出游戏模式") : why);
            }
        }
    }
    if (!i.presentation.fullscreen) i.gameRefused = false;
    Mode want = ui::gameInputMode(i.gameModeSessionId != 0, i.presentation.fullscreen,
                                  i.gameRefused, i.gamePanelOpen, gameScaledPreview());
    if (want!=Mode::Off && i.gameVideoRect.isEmpty()) updateGameGeometry();
    // A listener failure holds until fullscreen is left.
    if (want != Mode::Off && i.game.mode() == Mode::Off && !i.game.arm()) { i.gameRefused = true; want = Mode::Off; }
    if (want != i.game.mode()) {
        // QML binds the window's click-through flags to passthroughActive: publish
        // first, so Qt has changed the window before the foreground moves.
        const bool through = want == Mode::Through;
        const bool input = through || want == Mode::Scaled;
        if (through != i.passthroughShown || input != i.gameInputShown) {
            i.passthroughShown = through; i.gameInputShown = input; emit gameModeChanged();
        }
        if (want == Mode::Scaled && i.game.mode() != Mode::Panel)
            emit notice(tr("全屏缩放已启用：键盘和手柄控制源窗口，鼠标点击已拦截以免错位；需要鼠标穿透时切回贴合原窗口"), false);
        i.game.apply(want);
    }
    const bool mode = i.gameModeSessionId != 0;
    if ((!mode || !i.presentation.fullscreen) && !i.gameVideoRect.isEmpty()) {
        i.gameVideoRect={}; emit gameModeChanged();
    }
    if (mode != i.gameModeShown) { i.gameModeShown = mode; emit gameModeChanged(); }
}
QString QmlPlayerBridge::screenTargetId() const { return impl_->screenTarget; }
void QmlPlayerBridge::setScreenTargetId(const QString& value) {
    impl_->screenTarget = value;
    emit captureChanged();
}
QString QmlPlayerBridge::screenTargetLabel() const {
    const QString id = screenTargetId();
    if (id.isEmpty()) return tr("未选择目标");
    for (const auto& item : screenTargets()) {
        if (item.toMap().value(QStringLiteral("id")).toString() == id)
            return item.toMap().value(QStringLiteral("label")).toString();
    }
    return tr("目标已失效");
}
void QmlPlayerBridge::refreshCaptureTargets() {
    const int kind = screenOptions().value("kind").toInt();
    impl_->screenList = source::ScreenCaptureSource::targets(kind == 1 ? source::ScreenTargetKind::Monitor : source::ScreenTargetKind::Window);
    bool stillThere = false;
    for (const auto& t : impl_->screenList) stillThere = stillThere || QString::number(t.handle) == impl_->screenTarget;
    if (!stillThere) impl_->screenTarget = impl_->screenList.empty() ? QString() : QString::number(impl_->screenList.front().handle);
    emit captureChanged();
}

// --- PS5 Remote Play (P4-e) ---------------------------------------------------
namespace {
constexpr uint32_t kPs5Bitrates[] = {5000, 10000, 15000, 20000, 30000, 50000, 80000, 100000};
#ifdef VEYRA_ENABLE_REMOTEPLAY
std::filesystem::path ps5Settings() { return remoteplay::profileDirectory() / L"settings.ini"; }
#endif
}
QVariantMap QmlPlayerBridge::ps5() const {
    QVariantMap out = impl_->ps5Form;
    out["status"] = impl_->ps5Status;
    out["busy"] = impl_->ps5Busy;
    out["profile"] = QString::fromStdWString(impl_->ps5Profile);
#ifdef VEYRA_ENABLE_REMOTEPLAY
    out["available"] = true;
    out["psnReady"] = remoteplay::loadPsnAuthorization().has_value();
    const auto& s = impl_->snapshot;
    out["active"] = s.remotePlay && (s.running || s.transport == engine::TransportState::Opening || s.transport == engine::TransportState::Stopping);
    const auto c = impl_->controller.capabilities();
    out["controller"] = c.connected;
    out["gyro"] = c.gyro && c.accel;
    out["calibrating"] = c.calibrating;
#else
    out["available"] = false;
#endif
    return out;
}
QVariantList QmlPlayerBridge::ps5Profiles() const { return impl_->ps5ProfileList; }

// --- PC streaming (Moonlight / Sunshine) -----------------------------------------------------------
QObject* QmlPlayerBridge::moonlightModel() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlight.get();
#else
    return nullptr;
#endif
}
bool QmlPlayerBridge::moonlightCaptured() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlightCapture && impl_->moonlightCapture->captured();
#else
    return false;
#endif
}
bool QmlPlayerBridge::moonlightStatsVisible() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlightStatsVisible;
#else
    return false;
#endif
}
void QmlPlayerBridge::setMoonlightStatsVisible(bool visible) {
#ifdef VEYRA_ENABLE_MOONLIGHT
    if (impl_->moonlightStatsVisible == visible) return;
    impl_->moonlightStatsVisible = visible;
    emit moonlightUiChanged();
#else
    (void)visible;
#endif
}
void QmlPlayerBridge::openMoonlightDialog() { emit navigate(QStringLiteral("moonlight")); }

// --- Xbox home streaming (unofficial) ------------------------------------------------------------------
QObject* QmlPlayerBridge::xboxModel() const {
#ifdef VEYRA_ENABLE_XBOX
    return impl_->xbox.get();
#else
    return nullptr;
#endif
}
void QmlPlayerBridge::openXboxDialog() { emit navigate(QStringLiteral("xbox")); }
void QmlPlayerBridge::xboxDisconnect() {
#ifdef VEYRA_ENABLE_XBOX
    if (impl_->snapshot.xboxActive) stopPlayback();
#endif
}

#ifdef VEYRA_ENABLE_XBOX
void QmlPlayerBridge::setupXbox() {
    auto& i = *impl_;
    i.xbox = std::make_unique<ui::XboxModel>();
    connect(i.xbox.get(), &ui::XboxModel::notice, this, &QmlPlayerBridge::notice);
    i.xbox->setLaunchHandler([this](ui::XboxModel::Launch launch) -> bool {
        auto& j = *impl_;
        if (!j.videoWindow) return false;
        rememberPosition(true);
        j.openingSource = true;
        j.openingSessionId = 0;
        j.sourceLabel = L"Xbox · " + launch.label.toStdWString();
        j.resumeSession = 0;
        j.screenFillActive = false;
        const bool pad = launch.desc.gamepad;
        const QString label = launch.label;
        auto request = std::make_shared<source::XboxConnectDesc>(std::move(launch.desc));
        openAfterCinema(tr("正在连接 Xbox · %1").arg(label), [this, request, pad] {
            auto& k = *impl_;
            if (k.preOpen) k.preOpen();
            k.engine.previewView({});
            k.engine.openXbox(k.videoWindow, std::move(*request), k.options);
            k.openingSessionId = k.engine.snapshot().sessionId;
            refreshSubtitles({});
#ifdef VEYRA_ENABLE_REMOTEPLAY
            // The pad is polled on the UI thread at 250 Hz; the session sends changes and a 33 ms heartbeat.
            if (pad) {
                if (!k.acquirePad(Impl::PadXbox)) veyra::log::warn("xbox-input", "SDL gamepad initialization failed");
                if (!k.xboxTimer) {
                    k.xboxTimer = new QTimer(this);
                    k.xboxTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.xboxTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.xboxActive) { m.releasePad(Impl::PadXbox); m.xboxTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.xboxController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.xboxFeedback(), focused);
                    });
                }
                k.xboxTimer->start(4);
            } else {
                if (k.xboxTimer) k.xboxTimer->stop();
                k.releasePad(Impl::PadXbox);
            }
#else
            (void)pad;
#endif
        });
        rememberSource(QStringLiteral("xbox"), label);
        return true;
    });
}

void QmlPlayerBridge::tickXbox() {
    auto& i = *impl_;
    if (!i.xbox) return;
    const auto& s = i.snapshot;
    const bool active = s.xboxActive;
    if (active != i.xboxWasActive) {
        i.xboxWasActive = active;
        i.xbox->updateStream(active, s.xbox);
    } else if (active && ++i.xboxStatsTicks >= 15) {
        i.xboxStatsTicks = 0;
        i.xbox->updateStream(true, s.xbox);
    }
}
#else
void QmlPlayerBridge::tickXbox() {}
#endif

void QmlPlayerBridge::moonlightCapture(bool on) {
#ifdef VEYRA_ENABLE_MOONLIGHT
    auto& i = *impl_;
    if (!i.moonlightCapture) return;
    if (!on) {
        i.moonlightWantCapture = false;
        i.moonlightCapture->capture(false);
        return;
    }
    if (!i.snapshot.moonlightActive || !i.videoWindow) return;
    i.moonlightCapture->setWindow(GetAncestor(i.videoWindow, GA_ROOT));
    // The tick keeps trying until the window is in the foreground (right after connecting it may not be yet).
    i.moonlightWantCapture = !i.moonlightCapture->capture(true);
#else
    (void)on;
#endif
}

void QmlPlayerBridge::moonlightDisconnect() {
#ifdef VEYRA_ENABLE_MOONLIGHT
    moonlightCapture(false);
    if (impl_->snapshot.moonlightActive) stopPlayback();
#endif
}

#ifdef VEYRA_ENABLE_MOONLIGHT
void QmlPlayerBridge::setupMoonlight() {
    auto& i = *impl_;
    i.moonlight = std::make_unique<ui::MoonlightModel>();
    i.moonlightCapture = std::make_unique<ui::MoonlightInputCapture>(i.engine);
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::capturedChanged, this, [this](bool) { emit moonlightUiChanged(); });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::releaseRequested, this, [this] {
        impl_->moonlightWantCapture = false;
        emit notice(tr("已释放键盘和鼠标（点击画面重新捕获）"), false);
    });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::quitRequested, this, [this] { moonlightDisconnect(); });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::statsRequested, this, [this] { setMoonlightStatsVisible(!moonlightStatsVisible()); });
    connect(i.moonlight.get(), &ui::MoonlightModel::notice, this, &QmlPlayerBridge::notice);
    i.moonlight->setLaunchHandler([this](ui::MoonlightModel::Launch launch) -> bool {
        auto& j = *impl_;
        if (!j.videoWindow) return false;
        rememberPosition(true);
        j.openingSource = true;
        j.openingSessionId = 0;
        j.sourceLabel = L"PC · " + launch.label.toStdWString();
        j.resumeSession = 0;
        j.screenFillActive = false;
        const bool pad = (launch.desc.options.gamepadMask & 1) != 0;
        j.moonlightWantCapture = launch.captureInput;
        const QString label = launch.label;
        auto request = std::make_shared<source::MoonlightConnectDesc>(std::move(launch.desc));
        openAfterCinema(tr("正在连接 %1").arg(label), [this, request, pad] {
            auto& k = *impl_;
            if (k.preOpen) k.preOpen();
            k.engine.previewView({});
            k.engine.openMoonlight(k.videoWindow, std::move(*request), k.options);
            k.openingSessionId = k.engine.snapshot().sessionId;
            refreshSubtitles({});
#ifdef VEYRA_ENABLE_REMOTEPLAY
            // The pad is polled on the UI thread at 250 Hz and sent only when it changes.
            if (pad) {
                if (!k.acquirePad(Impl::PadMoonlight)) veyra::log::warn("moonlight-input", "SDL gamepad initialization failed");
                if (!k.moonlightTimer) {
                    k.moonlightTimer = new QTimer(this);
                    k.moonlightTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.moonlightTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.moonlightActive) { m.releasePad(Impl::PadMoonlight); m.moonlightTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.moonlightController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.moonlightFeedback(), focused);
                    });
                }
                k.moonlightTimer->start(4);
            } else {
                if (k.moonlightTimer) k.moonlightTimer->stop();
                k.releasePad(Impl::PadMoonlight);
            }
#else
            (void)pad;
#endif
        });
        rememberSource(QStringLiteral("moonlight"), label);
        return true;
    });
}

void QmlPlayerBridge::tickMoonlight() {
    auto& i = *impl_;
    if (!i.moonlight) return;
    const auto& s = i.snapshot;
    const bool active = s.moonlightActive;
    if (active != i.moonlightWasActive) {
        i.moonlightWasActive = active;
        if (!active) {
            i.moonlightWantCapture = false;
            if (i.moonlightCapture) i.moonlightCapture->capture(false);
        }
        i.moonlight->updateStream(active, s.moonlight);
    } else if (active && ++i.moonlightStatsTicks >= 15) {
        i.moonlightStatsTicks = 0;
        i.moonlight->updateStream(true, s.moonlight);
    }
    // Take the keyboard and mouse as soon as the picture is up (and again when the window comes to the front).
    if (active && i.moonlightWantCapture && s.moonlight.state == source::MoonlightStats::State::Streaming) moonlightCapture(true);
}
#else
void QmlPlayerBridge::tickMoonlight() {}
#endif
void QmlPlayerBridge::ps5Load() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto& i = *impl_;
    try {
        const auto directory = remoteplay::profileDirectory();
        if (i.ps5Profile.empty()) {
            wchar_t last[80]{};
            GetPrivateProfileStringW(L"RemotePlay", L"LastProfile", L"", last, 80, ps5Settings().c_str());
            const std::wstring name = last;
            if (!name.empty() && name.find_first_of(L"/\\:") == std::wstring::npos) i.ps5Profile = name;
        }
        i.ps5ProfileList.clear();
        unsigned failures = 0;
        auto add = [&](const std::filesystem::path& path) {
            remoteplay::ProfileLoadError error{};
            auto saved = remoteplay::loadProfile(path, &error);
            if (saved) i.ps5ProfileList << QVariantMap{{"id", QString::fromStdWString(path.filename().wstring())},
                                                      {"label", QString::fromStdString(saved->host)}};
            else if (error != remoteplay::ProfileLoadError::Missing) ++failures;
        };
        add(directory / "remoteplay-profile.dat");
        std::error_code ec; size_t scanned = 0;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end && i.ps5ProfileList.size() < 64 && scanned < 256; it.increment(ec), ++scanned) {
            const auto id = it->path().stem().wstring();
            if (it->path().extension() == ".dat" && id.size() == 32 &&
                std::all_of(id.begin(), id.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); }))
                add(it->path());
        }
        // A pairing made or found in the PS5 dialog lets a waiting PS5 手柄控制 start.
        if (!i.ps5ProfileList.isEmpty()) i.ps5ControlNoPair = false;
        bool known = false;
        for (const auto& p : i.ps5ProfileList) known = known || p.toMap().value("id").toString() == QString::fromStdWString(i.ps5Profile);
        if (!known) i.ps5Profile = i.ps5ProfileList.isEmpty() ? std::wstring{} : i.ps5ProfileList.front().toMap().value("id").toString().toStdWString();
        // Form defaults, then the selected pairing's own values.
        i.ps5Form = QVariantMap{{"host", QString()}, {"account", QString()}, {"quality", 3}, {"codec", 0}, {"bitrate", 2}, {"viewOnly", false},
            {"decode", int(std::min(2u, GetPrivateProfileIntW(L"RemotePlay", L"DecodeMode", 0, ps5Settings().c_str())))},
            {"sampling", int(std::min(1u, GetPrivateProfileIntW(L"RemotePlay", L"FineSampling", 1, ps5Settings().c_str())))}};
        if (!i.ps5Profile.empty())
            if (auto saved = remoteplay::loadProfile(directory / i.ps5Profile)) {
                i.ps5Form["host"] = QString::fromStdString(saved->host);
                i.ps5Form["account"] = QString::fromStdString(remoteplay::accountIdToBase64(saved->credentials.accountId));
                i.ps5Form["quality"] = (saved->video.height == 1080 ? 2 : 0) + (saved->video.fps == 60 ? 1 : 0);
                i.ps5Form["codec"] = int(saved->video.codec);
                const auto found = std::find(std::begin(kPs5Bitrates), std::end(kPs5Bitrates), saved->video.bitrateKbps);
                i.ps5Form["bitrate"] = found == std::end(kPs5Bitrates) ? 2 : int(found - std::begin(kPs5Bitrates));
                i.ps5Form["viewOnly"] = saved->viewOnly;
            }
        if (i.ps5Status.isEmpty())
            i.ps5Status = failures ? tr("有配对存档无法读取或解密，详情见日志。请使用原 Windows 账户；不要删除原件。")
                        : !i.ps5Profile.empty() ? tr("已加载保存的主机，直接点“连接”，无需重新填写 8 位配对码。")
                        : tr("首次使用：PS5 设置 → 系统 → 远程游玩 → 关联设备，填写账号 ID 与 8 位配对码后配对。");
        veyra::log::info("remoteplay-ui", std::format("profiles={} readFailures={}", i.ps5ProfileList.size(), failures));
    } catch (...) {
        impl_->ps5Status = tr("无法读取远程游玩目录；原有配对文件不会被改动。");
    }
#else
    impl_->ps5Status = tr("此版本未包含 PS5 远程游玩组件。");
#endif
    emit ps5Changed();
}
bool QmlPlayerBridge::ps5Set(const QString& key, const QVariant& value) {
    static const QHash<QString, std::pair<int, int>> ranges{{"quality", {0, 3}}, {"codec", {0, 2}}, {"bitrate", {0, 7}}, {"decode", {0, 2}}, {"sampling", {0, 1}}};
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok); const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) return false;
        impl_->ps5Form[key] = n;
    } else if (key == QLatin1String("viewOnly")) impl_->ps5Form[key] = value.toBool();
    else if (key == QLatin1String("host") || key == QLatin1String("account")) impl_->ps5Form[key] = value.toString().trimmed();
    else return false;
    emit ps5Changed();
    return true;
}
void QmlPlayerBridge::ps5SelectProfile(const QString& id) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (impl_->ps5Busy) return;
    impl_->ps5Profile = id.toStdWString();
    WritePrivateProfileStringW(L"RemotePlay", L"LastProfile", impl_->ps5Profile.c_str(), ps5Settings().c_str());
    impl_->ps5Status.clear();
    ps5Load();
    if (impl_->engine.remoteControlRequested()) startPs5Control(true);
#else
    Q_UNUSED(id);
#endif
}
#ifdef VEYRA_ENABLE_REMOTEPLAY
namespace {
remoteplay::VideoProfile ps5Video(const QVariantMap& f) {
    remoteplay::VideoProfile p; const int q = f.value("quality").toInt();
    p.width = q < 2 ? 1280 : 1920; p.height = q < 2 ? 720 : 1080; p.fps = (q == 0 || q == 2) ? 30 : 60;
    p.codec = static_cast<remoteplay::Codec>(std::clamp(f.value("codec").toInt(), 0, 2));
    p.bitrateKbps = kPs5Bitrates[std::clamp(f.value("bitrate").toInt(), 0, 7)];
    return p;
}
std::string asciiOnly(const QString& s) {
    std::string out;
    for (const QChar c : s) { if (c.unicode() > 127) return {}; out.push_back(char(c.unicode())); }
    return out;
}
}
#endif
// One worker at a time, as the 1.4.4 panel; results are picked up in tickPs5().
#define VEYRA_PS5_LAUNCH(body) \
    do { if (impl_->ps5Busy) return; if (impl_->ps5Worker.joinable()) impl_->ps5Worker.join(); \
         impl_->ps5Busy = true; impl_->ps5Done = false; impl_->ps5Status = tr("正在处理…可随时取消"); emit ps5Changed(); \
         impl_->ps5Worker = std::jthread([this, fn = std::move(body)](std::stop_token stop) mutable { \
             Impl::Ps5Outcome result; \
             try { result = fn(stop); } catch (...) { result.message = L"操作失败；请检查网络、输入和可用磁盘空间。"; } \
             { std::lock_guard lock(impl_->ps5Mutex); impl_->ps5Outcome = std::move(result); } impl_->ps5Done = true; }); } while (0)
void QmlPlayerBridge::ps5Scan() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto work = [](std::stop_token stop) {
        Impl::Ps5Outcome out; auto report = remoteplay::discoverLocalPs5(stop);
        for (const auto& line : report.diagnostics) veyra::log::info("remoteplay-discovery", line);
        for (auto& h : report.hosts) {
            std::string id = h.consoleId;
            std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            out.hosts.push_back({h.host, id});
        }
        out.message = stop.stop_requested() ? L"已取消查找" : out.hosts.empty()
            ? (report.error ? std::format(L"主机搜索发生错误（{}），详情见日志；可手填 IP。", report.error) : L"搜索完成，未发现 PS5；确认同一局域网，或手动填写 IP。")
            : std::format(L"发现 {} 台 PS5。", out.hosts.size());
        return out;
    };
    VEYRA_PS5_LAUNCH(work);
#endif
}
void QmlPlayerBridge::ps5Pair(const QString& pinText) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto host = asciiOnly(impl_->ps5Form.value("host").toString()), id = asciiOnly(impl_->ps5Form.value("account").toString()), pin = asciiOnly(pinText);
    auto account = remoteplay::accountIdFromBase64(id); if (!account) account = remoteplay::accountIdFromDecimal(id);
    if (!remoteplay::validHost(host) || !account || !remoteplay::parsePairingPin(pin)) {
        impl_->ps5Status = tr("请填写有效主机地址、Account ID 和 8 位配对码。"); emit ps5Changed(); return;
    }
    // Same host + account reuses its file; otherwise a fresh random name.
    std::filesystem::path target;
    for (const auto& p : impl_->ps5ProfileList) {
        const auto path = remoteplay::profileDirectory() / p.toMap().value("id").toString().toStdWString();
        if (auto saved = remoteplay::loadProfile(path); saved && saved->host == host && saved->credentials.accountId == *account) target = path;
    }
    if (target.empty()) {
        GUID guid{}; if (FAILED(CoCreateGuid(&guid))) { impl_->ps5Status = tr("无法分配配对存档，请重试。"); emit ps5Changed(); return; }
        std::string name; const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
        for (size_t k = 0; k < sizeof(guid); ++k) name += std::format("{:02x}", bytes[k]);
        target = remoteplay::profileDirectory() / (name + ".dat");
    }
    const auto format = ps5Video(impl_->ps5Form);
    auto work = [host = std::move(host), account = *account, pin = std::move(pin), format, target](std::stop_token stop) mutable {
        auto result = remoteplay::pairLocalPs5(host, account, pin, stop); SecureZeroMemory(pin.data(), pin.size());
        Impl::Ps5Outcome out;
        if (result.result.ok && !stop.stop_requested()) {
            remoteplay::NativeConnectRequest request; request.host = host; request.video = format; request.credentials = std::move(result.credentials);
            for (auto byte : result.mac) request.consoleId += std::format("{:02x}", byte);
            if (request.consoleId == "000000000000") request.consoleId.clear();
            if (remoteplay::saveProfile(target, request)) { out.savedPath = target.filename().wstring(); out.message = L"配对成功并已加密保存；现在可以连接。"; }
            else out.message = L"配对成功，但保存失败；请检查用户目录权限。";
        } else out.message = result.canceled || stop.stop_requested() ? L"已取消配对" : std::format(L"配对失败（{}），检查 PS5 配对码、Account ID 与网络。", result.result.code);
        return out;
    };
    VEYRA_PS5_LAUNCH(work);
#else
    Q_UNUSED(pinText);
#endif
}
bool QmlPlayerBridge::ps5Connect() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto& i = *impl_;
    if (i.ps5Busy) return false;
    const auto path = i.ps5Profile.empty() ? std::filesystem::path{} : remoteplay::profileDirectory() / i.ps5Profile;
    auto saved = path.empty() ? std::nullopt : remoteplay::loadProfile(path);
    const auto host = asciiOnly(i.ps5Form.value("host").toString());
    if (!saved || !remoteplay::validHost(host)) { i.ps5Status = tr("请先完成配对，并填写有效主机地址。"); emit ps5Changed(); return false; }
    saved->host = host; saved->video = ps5Video(i.ps5Form); saved->viewOnly = i.ps5Form.value("viewOnly").toBool();
    if (!remoteplay::saveProfile(path, *saved)) { i.ps5Status = tr("保存连接设置失败，请检查目录权限。"); emit ps5Changed(); return false; }
    source::RemotePlayConnectDesc desc; desc.request = std::move(*saved);
    const int decode = std::clamp(i.ps5Form.value("decode").toInt(), 0, 2);
    desc.decodeMode = static_cast<source::RemotePlayConnectDesc::DecodeMode>(decode);
    desc.highQualitySampling = i.ps5Form.value("sampling").toInt() == 1;
    WritePrivateProfileStringW(L"RemotePlay", L"DecodeMode", std::to_wstring(decode).c_str(), ps5Settings().c_str());
    WritePrivateProfileStringW(L"RemotePlay", L"FineSampling", desc.highQualitySampling ? L"1" : L"0", ps5Settings().c_str());
    const bool viewOnly = desc.request.viewOnly;
    // The console has one Remote Play slot; the engine ends PS5 手柄控制 first.
    if (i.engine.remoteControlRequested()) emit notice(tr("已结束 PS5 手柄控制：PS5 同一时间只允许一个远程游玩连接"), false);
    rememberPosition(true);
    // The session id is cleared with the flag: the previous session's id left over here
    // ended "opening" on the next snapshot while the old source still ran.
    i.openingSource = true; i.openingSessionId = 0; i.sourceLabel = L"PS5 · " + std::wstring(host.begin(), host.end()); i.resumeSession = 0; i.screenFillActive = false;
    auto request = std::make_shared<source::RemotePlayConnectDesc>(std::move(desc));
    openAfterCinema(tr("正在连接 PS5 · %1").arg(QString::fromStdString(host)), [this, request, viewOnly] {
        auto& i = *impl_;
        if (i.preOpen) i.preOpen();
        i.engine.previewView({});
        i.engine.openRemotePlay(i.videoWindow, std::move(*request), i.options);
        i.openingSessionId = i.engine.snapshot().sessionId;
        refreshSubtitles({});
        // Controller forwarding at 8 ms, as 1.4.4; view-only leaves the pad to the PS5.
        // Started with the session: the poll stops the pad when no session runs.
        if (!viewOnly) {
            if (!i.acquirePad(Impl::PadPs5)) veyra::log::warn("remoteplay-input", "SDL gamepad initialization failed");
            if (!i.controllerTimer) {
                i.controllerTimer = new QTimer(this);
                connect(i.controllerTimer, &QTimer::timeout, this, [this] {
                    auto& j = *impl_;
                    const auto s = j.engine.snapshot();
                    if (s.remotePlay && (s.running || s.transport == engine::TransportState::Opening)) {
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        j.engine.remotePlayController(j.controller.poll(focused));
                        j.controller.feedback(j.engine.remotePlayFeedback(), focused);
                    } else {
                        j.engine.remotePlayController({}); j.releasePad(Impl::PadPs5); j.controllerTimer->stop();
                    }
                });
            }
            i.controllerTimer->start(8);
        } else {
            if (i.controllerTimer) i.controllerTimer->stop();
            i.releasePad(Impl::PadPs5);
        }
    });
    i.ps5Watching = true;
    rememberSource(QStringLiteral("ps5"), QString::fromStdString(host));
    i.ps5Status = tr("连接已开始。需要登录 PIN 时在下方提交。关闭面板不停止串流。");
    veyra::log::info("remoteplay-ui", std::format("connect quality={} codec={} bitrate={} decode={} viewOnly={}",
        i.ps5Form.value("quality").toInt(), i.ps5Form.value("codec").toInt(), i.ps5Form.value("bitrate").toInt(), decode, viewOnly));
    emit ps5Changed();
    return true;
#else
    return false;
#endif
}
void QmlPlayerBridge::ps5Wake() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto saved = impl_->ps5Profile.empty() ? std::nullopt : remoteplay::loadProfile(remoteplay::profileDirectory() / impl_->ps5Profile);
    if (!saved) { impl_->ps5Status = tr("需要先配对才能唤醒。"); emit ps5Changed(); return; }
    saved->host = asciiOnly(impl_->ps5Form.value("host").toString());
    auto work = [request = std::move(*saved)](std::stop_token stop) {
        Impl::Ps5Outcome out; if (stop.stop_requested()) { out.message = L"已取消唤醒"; return out; }
        auto r = remoteplay::wakeLocalPs5(request);
        out.message = r.ok ? L"已发送唤醒请求，等待 PS5 开机后再连接。" : std::format(L"唤醒失败（{}）；确认 PS5 处于待机而不是关机。", r.code);
        return out;
    };
    VEYRA_PS5_LAUNCH(work);
#endif
}
void QmlPlayerBridge::ps5Forget() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (impl_->ps5Busy || impl_->ps5Profile.empty()) return;
    std::error_code ec;
    const bool removed = std::filesystem::remove(remoteplay::profileDirectory() / impl_->ps5Profile, ec);
    impl_->ps5Status = removed ? tr("配对已删除。现有串流会保留；下次连接需选择其他主机或重新配对。") : tr("删除失败，请检查目录权限。");
    if (removed) impl_->ps5Profile.clear();
    ps5Load();
#endif
}
void QmlPlayerBridge::ps5PsnLogin() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (QDesktopServices::openUrl(QUrl(QString::fromStdWString(remoteplay::psnLoginUrl())))) {
        impl_->psnLoginStarted = GetTickCount64();
        impl_->ps5Status = tr("在 Sony 网页完成登录，复制最终回调地址，再点“提交登录结果”。");
    } else impl_->ps5Status = tr("无法打开浏览器，请检查默认浏览器设置。");
    emit ps5Changed();
#endif
}
void QmlPlayerBridge::ps5PsnComplete() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (impl_->ps5Busy) return;
    if (!impl_->psnLoginStarted || GetTickCount64() - impl_->psnLoginStarted > 600000) {
        impl_->ps5Status = tr("请先点登录 PSN，完成网页授权后再提交。"); emit ps5Changed(); return;
    }
    std::wstring callback = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text().left(8192).toStdWString() : std::wstring{};
    if (!remoteplay::validPsnCallback(callback)) {
        SecureZeroMemory(callback.data(), callback.size() * sizeof(wchar_t));
        impl_->ps5Status = tr("剪贴板不是有效的 Sony 授权回调地址。请复制登录完成后的完整 URL。"); emit ps5Changed(); return;
    }
    impl_->psnLoginStarted = 0;
    auto work = [callback = std::move(callback)](std::stop_token stop) mutable {
        auto result = remoteplay::authorizePsn(std::move(callback), stop); Impl::Ps5Outcome out;
        if (result.account) out.account = remoteplay::accountIdToBase64(*result.account);
        out.message = result.ok ? L"PSN 登录已保存，Account ID 已填入。首次仍需配对；已配对主机直接连接。"
                                : std::format(L"PSN 授权失败（{}）；原有配对不受影响，可重新登录。", result.error);
        return out;
    };
    VEYRA_PS5_LAUNCH(work);
#endif
}
void QmlPlayerBridge::ps5PsnForget() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (impl_->ps5Busy) return;
    impl_->ps5Status = remoteplay::forgetPsnAuthorization() ? tr("PSN 授权已删除，局域网配对保留。") : tr("删除授权失败，请检查目录权限。");
    emit ps5Changed();
#endif
}
void QmlPlayerBridge::ps5SendLoginPin(const QString& pin) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    const auto text = pin.trimmed();
    if (text.isEmpty() || !std::all_of(text.begin(), text.end(), [](QChar c) { return c.isDigit(); })) {
        impl_->ps5Status = tr("登录 PIN 必须为数字。"); emit ps5Changed(); return;
    }
    impl_->engine.remotePlayLoginPin(text.toStdString());
#else
    (void)pin; // RemotePlay disabled: keep the QML-callable symbol but no backend.
#endif
}
void QmlPlayerBridge::ps5Cancel() {
    if (impl_->ps5Busy && impl_->ps5Worker.joinable()) { impl_->ps5Worker.request_stop(); return; }
    if (impl_->snapshot.remotePlay) stopPlayback();
}
void QmlPlayerBridge::ps5Calibrate() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    impl_->ps5Status = impl_->controller.calibrate() ? tr("将手柄平放并保持静止：采集 120 个稳定样本完成校准；10 秒内不稳定则保留原校准。")
                                                     : tr("请先连接串流及带陀螺仪的电脑手柄。仅观看模式不使用电脑手柄。");
    emit ps5Changed();
#endif
}
void QmlPlayerBridge::tickPs5() {
    auto& i = *impl_;
    bool changed = false;
    if (i.ps5Busy && i.ps5Done.load()) {
        if (i.ps5Worker.joinable()) i.ps5Worker.join();
        i.ps5Busy = false;
        Impl::Ps5Outcome r; { std::lock_guard lock(i.ps5Mutex); r = std::move(i.ps5Outcome); }
        i.ps5Status = veyra::ui::i18n::text(r.message);
        if (!r.account.empty()) i.ps5Form["account"] = QString::fromStdString(r.account);
#ifdef VEYRA_ENABLE_REMOTEPLAY
        if (!r.savedPath.empty()) {
            i.ps5Profile = r.savedPath;
            WritePrivateProfileStringW(L"RemotePlay", L"LastProfile", i.ps5Profile.c_str(), ps5Settings().c_str());
            const auto status = i.ps5Status; ps5Load(); i.ps5Status = status;
        }
        if (!r.hosts.empty()) {
            // A found console updates the selected pairing's address only when its
            // id matches (or the legacy profile had no id and the same host).
            const auto path = i.ps5Profile.empty() ? std::filesystem::path{} : remoteplay::profileDirectory() / i.ps5Profile;
            auto selected = path.empty() ? std::nullopt : remoteplay::loadProfile(path);
            bool matched = false;
            if (selected) for (const auto& [host, id] : r.hosts) {
                if ((!selected->consoleId.empty() && selected->consoleId == id) || (selected->consoleId.empty() && selected->host == host)) {
                    selected->host = host; if (id.size() == 12) selected->consoleId = id;
                    if (remoteplay::saveProfile(path, *selected)) { i.ps5Form["host"] = QString::fromStdString(host); matched = true; }
                    break;
                }
            }
            if (!selected && r.hosts.size() == 1) i.ps5Form["host"] = QString::fromStdString(r.hosts.front().first);
            else if (selected && !matched) i.ps5Status = tr("发现主机，但无法确认是已配对的这一台。保留原地址，可核对后手动修改。");
            else if (!selected && r.hosts.size() > 1) i.ps5Status = tr("发现多台 PS5，请填写要配对的主机 IP，避免选错主机。");
        }
#endif
        changed = true;
    }
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (i.ps5Watching && !i.ps5Busy) {
        const auto& s = i.snapshot;
        const bool active = s.remotePlay && (s.running || s.transport == engine::TransportState::Opening || s.transport == engine::TransportState::Stopping);
        QString message = veyra::ui::i18n::text(s.failed ? s.status : s.remoteRecovering ? s.remoteRecoveryMessage
                          : s.remotePlay && s.running ? std::wstring(L"PS5 画面已进入播放；关闭此面板不停止串流。") : active ? s.status : std::wstring(L"PS5 串流已停止，可以重新连接。"));
        if (s.remotePlay && s.running && !s.failed && !s.remoteRecovering) {
            const auto& r = s.remoteStream; const auto& p = r.requestedProfile;
            message = veyra::ui::i18n::text(std::format(L"生效：{}p / {} fps / {} / 请求 {} Mbps · 实收视频 {:.1f} Mbps", p.height, p.fps,
                p.codec == remoteplay::Codec::H264 ? L"H.264" : p.codec == remoteplay::Codec::H265Hdr ? L"H.265 HDR" : L"H.265", p.bitrateKbps / 1000, r.videoMbps));
            const auto c = i.controller.capabilities();
            if (i.ps5Form.value("viewOnly").toBool()) message += tr("\n仅观看：电脑输入已关闭，手柄由 PS5 处理。");
            else if (!c.connected) message += tr("\n未检测到电脑手柄。");
            else message += veyra::ui::i18n::text(std::format(L"\n陀螺仪 {} · 触摸板 {} · 扳机 {} · 触觉 {}{}", c.gyro && c.accel ? L"已启用" : L"不可用",
                c.touch ? L"可用" : L"不可用", c.triggers ? L"已接入" : L"不可用", c.haptics ? L"端点已打开" : L"未打开", c.calibrating ? L" · 校准中" : L""));
        }
        if (message != i.ps5Status) { i.ps5Status = message; changed = true; }
        if (!active && !s.remotePlay) i.ps5Watching = false;
    }
#endif
    if (changed) emit ps5Changed();
    tickPs5Control();
}

// --- PS5 手柄控制 (capture card dialog) ----------------------------------------
QVariantMap QmlPlayerBridge::ps5Control() const {
    QVariantMap out = impl_->ps5ControlState;
    if (!out.contains(QStringLiteral("state"))) out[QStringLiteral("state")] = QStringLiteral("off");
#ifdef VEYRA_ENABLE_REMOTEPLAY
    out[QStringLiteral("available")] = true;
#else
    out[QStringLiteral("available")] = false;
#endif
    out[QStringLiteral("enabled")] = impl_->prefBool("ps5CaptureControl", false);
    out[QStringLiteral("background")] = impl_->prefBool("ps5ControlBackground", false);
    return out;
}
// liveKind is "ps5", "screen", "moonlight" or "xbox" for those sources; a file
// leaves it alone but does not set snapshot.capture.
bool QmlPlayerBridge::captureCardLive() const {
    const auto& s = impl_->snapshot;
    // Not while Stopping: after 断开 the run keeps `running` until it has torn
    // down, and the tick would start PS5 手柄控制 again in that window.
    const bool playing = s.running && !s.image && !s.remotePlay &&
                         (s.transport == engine::TransportState::Playing || s.transport == engine::TransportState::Paused);
    // Tests only: any playing source stands in for the capture card (no card needed).
    if (playing && qEnvironmentVariableIsSet("VEYRA_TEST_PS5_CONTROL_ANY_SOURCE")) return true;
    return playing && s.capture && impl_->liveKind == QLatin1String("capture");
}
void QmlPlayerBridge::ps5ControlSet(const QString& key, const QVariant& value) {
    static const QHash<QString, QString> keys{{QStringLiteral("enabled"), QStringLiteral("ps5CaptureControl")},
                                              {QStringLiteral("background"), QStringLiteral("ps5ControlBackground")}};
    if (!keys.contains(key)) return;
    const bool on = value.toBool();
    const auto previous = impl_->prefs;
    impl_->prefs[keys.value(key)] = on;
    if (!impl_->savePrefs()) { impl_->prefs = previous; emit notice(tr("PS5 手柄控制设置保存失败"), true); emit ps5ControlChanged(); return; }
    veyra::log::info("remoteplay-control", std::format("ui {}={}", key.toStdString(), on));
    if (key == QLatin1String("enabled")) {
        impl_->ps5ControlNoPair = false;
        if (!on) stopPs5Control();
        else if (captureCardLive()) startPs5Control(false);
        tickPs5Control(true);
    }
    emit ps5ControlChanged();
}
void QmlPlayerBridge::ps5ControlRestart() {
    if (impl_->prefBool("ps5CaptureControl", false) && captureCardLive()) startPs5Control(true);
}
void QmlPlayerBridge::startPs5Control(bool restart) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto& i = *impl_;
    source::RemotePlayConnectDesc desc;
    std::wstring profile;
    // Tests only: a dummy pairing to this host (an unroutable test address); the
    // saved pairings are not read, so no real console is ever contacted.
    const QString testHost = qEnvironmentVariable("VEYRA_TEST_PS5_CONTROL_HOST");
    if (!testHost.isEmpty()) {
        desc.request.host = testHost.toStdString();
        profile = L"test:" + testHost.toStdWString();
    } else {
        if (i.ps5Profile.empty()) ps5Load();
        auto saved = i.ps5Profile.empty() ? std::nullopt : remoteplay::loadProfile(remoteplay::profileDirectory() / i.ps5Profile);
        if (!saved) {
            i.engine.stopRemoteControl();
            if (!i.ps5ControlNoPair) emit notice(tr("PS5 手柄控制：还没有配对的 PS5，先在「PS5 串流」里配对一次"), true);
            i.ps5ControlNoPair = true;
            tickPs5Control(true);
            return;
        }
        desc.request = std::move(*saved);
        profile = i.ps5Profile;
    }
    const auto running = i.engine.remoteControlStatus();
    if (!restart && running.requested && (running.alive || running.starting) && profile == i.ps5ControlProfile) {
        if (!(i.padUsers & Impl::PadPs5Control)) i.acquirePad(Impl::PadPs5Control);
        if (i.ps5ControlTimer && !i.ps5ControlTimer->isActive()) i.ps5ControlTimer->start(8);
        return;
    }
    // Keepalive profile, the lightest the console accepts. Never written back to the pairing.
    remoteplay::VideoProfile keepalive;
    keepalive.width = 1280; keepalive.height = 720; keepalive.fps = 30; keepalive.bitrateKbps = 5000; keepalive.codec = remoteplay::Codec::H264;
    desc.request.video = keepalive;
    veyra::log::info("remoteplay-control", std::format("start host={} restart={} background={}", desc.request.host, restart, i.prefBool("ps5ControlBackground", false)));
    i.ps5ControlProfile = profile;
    i.ps5ControlNoticed.clear();
    i.ps5ControlNoPair = false;
    i.engine.startRemoteControl(std::move(desc));
    // The pad is read on the UI thread every 8 ms, as for the PS5 stream (the
    // session sends one state per 4 ms and queues the rest).
    if (!i.acquirePad(Impl::PadPs5Control)) veyra::log::warn("remoteplay-input", "SDL gamepad initialization failed");
    if (!i.ps5ControlTimer) {
        i.ps5ControlTimer = new QTimer(this);
        connect(i.ps5ControlTimer, &QTimer::timeout, this, [this] {
            auto& j = *impl_;
            // A PS5 stream polls the pad with its own timer and takes the slot.
            if (j.snapshot.remotePlay || !j.engine.remoteControlRequested()) {
                j.ps5ControlTimer->stop(); j.releasePad(Impl::PadPs5Control); return;
            }
            const bool focused = QGuiApplication::focusWindow() != nullptr || j.prefBool("ps5ControlBackground", false);
            j.engine.remotePlayController(j.controller.poll(focused));
            j.controller.feedback(j.engine.remotePlayFeedback(), focused);
        });
    }
    i.ps5ControlTimer->start(8);
    tickPs5Control(true);
#else
    (void)restart;
#endif
}
void QmlPlayerBridge::stopPs5Control() {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto& i = *impl_;
    if (!i.engine.remoteControlRequested()) return;
    veyra::log::info("remoteplay-control", "stop");
    i.engine.stopRemoteControl();
    i.ps5ControlProfile.clear();
    tickPs5Control(true);
#endif
}
void QmlPlayerBridge::tickPs5Control(bool force) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
    auto& i = *impl_;
    if (!force && ++i.ps5ControlTicks % 15) return; // ~4 Hz from the 60 Hz poll
    // Switched on while the capture card was still opening, or the capture came
    // back by itself: start once it is live. A failed session stays requested
    // until the user restarts it, so this never loops.
    if (!force && !i.ps5ControlNoPair && i.prefBool("ps5CaptureControl", false) && captureCardLive() && !i.engine.remoteControlRequested())
        startPs5Control(false);
    const auto st = i.engine.remoteControlStatus();
    QString state = QStringLiteral("off");
    if (i.ps5ControlNoPair && !st.requested) state = QStringLiteral("nopair");
    else if (st.requested) {
        state = st.pinRequired ? QStringLiteral("pin")
              : st.recovering ? QStringLiteral("recovering")
              : st.alive && st.connected ? QStringLiteral("connected")
              : !st.alive && !st.starting ? QStringLiteral("failed")
              : QStringLiteral("starting");
    }
    // Tests only: show a state no console is available to produce here.
    if (const QString forced = qEnvironmentVariable("VEYRA_TEST_PS5_CONTROL_STATE"); !forced.isEmpty() && st.requested) state = forced;
    QVariantMap next;
    next[QStringLiteral("state")] = state;
    next[QStringLiteral("message")] = state == QLatin1String("nopair") ? tr("还没有配对的 PS5，先在「PS5 串流」里配对一次") : uiText(st.message);
    next[QStringLiteral("keepaliveFps")] = st.keepaliveReady ? std::round(st.keepaliveFps) : -1.0;
    const auto c = i.controller.capabilities();
    next[QStringLiteral("pad")] = (i.padUsers & Impl::PadPs5Control) && c.connected ? QString::fromStdString(c.name) : QString();
    next[QStringLiteral("gyro")] = c.gyro && c.accel;
    next[QStringLiteral("touch")] = c.touch;
    if (next == i.ps5ControlState) return;
    i.ps5ControlState = next;
    if (state != i.ps5ControlNoticed) {
        if (state == QLatin1String("connected")) emit notice(tr("PS5 手柄控制已连接"), false);
        else if (state == QLatin1String("pin")) emit notice(tr("PS5 要求输入登录 PIN：在采集卡面板「PS5 手柄控制」里填写"), false);
        else if (state == QLatin1String("failed")) {
            const QString reason = next.value(QStringLiteral("message")).toString();
            emit notice(reason.isEmpty() ? tr("PS5 手柄控制已停止") : reason, true);
        }
        if (state == QLatin1String("connected") || state == QLatin1String("pin") || state == QLatin1String("failed")) i.ps5ControlNoticed = state;
    }
    emit ps5ControlChanged();
#else
    (void)force;
#endif
}

// --- ps5 dialog --------------------------------------------------------------
QString QmlPlayerBridge::remotePlayHost() const { return impl_->remotePlayHost; }
void QmlPlayerBridge::setRemotePlayHost(const QString& value) {
    impl_->remotePlayHost = value;
    emit settingsChanged();
}
QString QmlPlayerBridge::remotePlayPin() const { return impl_->remotePlayPin; }
void QmlPlayerBridge::setRemotePlayPin(const QString& value) {
    // Held only until the connect command runs; it is never written to the session
    // file, and the engine keeps PSN credentials encrypted in the user data
    // directory rather than here.
    impl_->remotePlayPin = value;
    emit settingsChanged();
}


// --- colour page -------------------------------------------------------------
// One table of the parameters the engine actually carries, so a control can only
// exist for a real field, and the UI can build itself from this catalogue.
namespace {
struct ColourParam {
    const char* name;
    const char* label;
    const char* group;
    float minimum, maximum, step;
};
// Names match ColorSettings' own members; the mixer bands are indexed.
const ColourParam kColourParams[] = {
    {"exposure",      "曝光（EV）",    "亮",       -5,     5,   0.05f},
    {"contrast",      "对比度",        "亮",      -100,   100,   1},
    {"highlights",    "高光",          "亮",      -100,   100,   1},
    {"shadows",       "阴影",          "亮",      -100,   100,   1},
    {"whites",        "白色",          "亮",      -100,   100,   1},
    {"blacks",        "黑色",          "亮",      -100,   100,   1},
    {"texture",       "纹理",          "效果",    -100,   100,   1},
    {"clarity",       "清晰度",        "效果",    -100,   100,   1},
    {"dehaze",        "去朦胧",        "效果",    -100,   100,   1},
    {"temperature",   "色温（相对）",  "颜色",    -100,   100,   1},
    {"tint",          "色调",          "颜色",    -100,   100,   1},
    {"vibrance",      "自然饱和度",    "颜色",    -100,   100,   1},
    {"saturation",    "饱和度",        "颜色",    -100,   100,   1},
    {"paramHighlights","参数化高光",   "曲线",    -100,   100,   1},
    {"paramLights",   "参数化亮调",    "曲线",    -100,   100,   1},
    {"paramDarks",    "参数化暗调",    "曲线",    -100,   100,   1},
    {"paramShadows",  "参数化阴影",    "曲线",    -100,   100,   1},
    {"splitHighlights","分离高光",     "曲线", -100,  100,   1},
    {"splitMidtones", "分离中间调",    "曲线", -100,  100,   1},
    {"splitShadows",  "分离阴影",      "曲线", -100,  100,   1},
    {"gradingBlending","混合",         "颜色分级",   0,   100,   1},
    {"gradingBalance","平衡",          "颜色分级", -100,   100,   1},
    {"calibrationShadowTint","阴影色调","校准",    -100,   100,   1},
    {"lutStrength",   "LUT 强度",      "LUT",        0,   100,   1},
};

float* colourScalar(engine::ColorSettings& c, const char* name) {
    const std::string_view n{name};
    if (n == "exposure") return &c.exposure;
    if (n == "contrast") return &c.contrast;
    if (n == "highlights") return &c.highlights;
    if (n == "shadows") return &c.shadows;
    if (n == "whites") return &c.whites;
    if (n == "blacks") return &c.blacks;
    if (n == "texture") return &c.texture;
    if (n == "clarity") return &c.clarity;
    if (n == "dehaze") return &c.dehaze;
    if (n == "temperature") return &c.temperature;
    if (n == "tint") return &c.tint;
    if (n == "vibrance") return &c.vibrance;
    if (n == "saturation") return &c.saturation;
    if (n == "paramHighlights") return &c.paramHighlights;
    if (n == "paramLights") return &c.paramLights;
    if (n == "paramDarks") return &c.paramDarks;
    if (n == "paramShadows") return &c.paramShadows;
    if (n == "splitHighlights") return &c.splitHighlights;
    if (n == "splitMidtones") return &c.splitMidtones;
    if (n == "splitShadows") return &c.splitShadows;
    if (n == "gradingBlending") return &c.gradingBlending;
    if (n == "gradingBalance") return &c.gradingBalance;
    if (n == "calibrationShadowTint") return &c.calibrationShadowTint;
    if (n == "lutStrength") return &c.lutStrength;
    // Array parameters retain the existing engine units; no UI-only mirrors.
    const auto parts=QString::fromUtf8(name).split('.');
    bool ok=false;
    const int index=parts.size()>=2?parts[1].toInt(&ok):-1;
    if(!ok||index<0)return nullptr;
    if(parts.size()==2){
        if(index<engine::kColorMixerBands){
            if(parts[0]=="mixerHue")return &c.mixerHue[index];
            if(parts[0]=="mixerSaturation")return &c.mixerSaturation[index];
            if(parts[0]=="mixerLuminance")return &c.mixerLuminance[index];
            if(parts[0]=="blackWhiteMix")return &c.blackWhiteMix[index];
        }
        if(index<3){
            if(parts[0]=="calibrationHue")return &c.calibrationHue[index];
            if(parts[0]=="calibrationSaturation")return &c.calibrationSaturation[index];
        }
    }
    if(parts.size()==3&&parts[0]=="grading"&&index<engine::kColorGradingZones){
        if(parts[2]=="hue")return &c.grading[index].hue;
        if(parts[2]=="saturation")return &c.grading[index].saturation;
        if(parts[2]=="luminance")return &c.grading[index].luminance;
    }
    return nullptr;
}
} // namespace

QVariantList QmlPlayerBridge::colourParameters() const {
    QVariantList out;
    engine::ColorSettings defaults;
    for (const auto& p : kColourParams) {
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = uiText(std::string_view(p.label));
        item["group"] = uiText(std::string_view(p.group));
        item["min"] = double(p.minimum);
        item["max"] = double(p.maximum);
        item["step"] = double(p.step);
        if (const auto* value = colourScalar(defaults, p.name)) item["defaultValue"] = double(*value);
        // Centred controls read their fill from the middle, as the design's sliders
        // do for anything that can go either way.
        item["center"] = p.minimum < 0 && p.maximum > 0;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::colourGroups() const {
    // Grouped in catalogue order, so the sections appear in the design's order
    // rather than in whatever order a hash would produce.
    QVariantList groups;
    QStringList order;
    QHash<QString, QVariantList> buckets;
    for (const auto& p : kColourParams) {
        const QString group = uiText(std::string_view(p.group));
        if (!buckets.contains(group)) order << group;
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = uiText(std::string_view(p.label));
        item["min"] = double(p.minimum);
        item["max"] = double(p.maximum);
        item["center"] = p.minimum < 0 && p.maximum > 0;
        buckets[group] << item;
    }
    for (const auto& group : order) {
        QVariantMap entry;
        entry["group"] = group;
        entry["items"] = buckets.value(group);
        groups << entry;
    }
    return groups;
}

double QmlPlayerBridge::colourParameter(const QString& name) const {
    auto c = selectedColourSettings();
    const auto utf8 = name.toUtf8();
    if (float* field = colourScalar(c, utf8.constData())) return double(*field);
    return 0.0;
}

bool QmlPlayerBridge::setColourParameter(const QString& name, double value) {
    if(!std::isfinite(value)){emit notice(tr("调色值必须是有限数值"),true);return false;}
    auto c = selectedColourSettings();
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(c, utf8.constData());
    if (field == nullptr) return false;
    const float next = float(value);
    if (*field == next) return true;
    *field = next;
    return commitColour(c);
}

bool QmlPlayerBridge::resetColourParameter(const QString& name) {
    engine::ColorSettings defaults;
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(defaults, utf8.constData());
    if (field == nullptr) return false;
    return setColourParameter(name,*field);
}

int QmlPlayerBridge::selectedColourLayer() const {return impl_->colourIndex();}
void QmlPlayerBridge::setSelectedColourLayer(int index){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::Color||impl_->selectedColour==index)return;
    impl_->selectedColour=index;emit settingsChanged();
}
engine::ColorSettings QmlPlayerBridge::selectedColourSettings() const {
    const int index=impl_->colourIndex();
    if(index<0)return {};
    auto colour=impl_->chain.nodes[index].color;
    colour.enabled=impl_->chain.nodes[index].enabled;
    return colour;
}
bool QmlPlayerBridge::commitColour(const engine::ColorSettings& colour){
    const int index=impl_->colourIndex();
    if(index<0){emit notice(tr("处理链中没有调色节点，未更改设置"),true);return false;}
    if(const auto error=colour.validate();!error.empty()){
        emit notice(tr("调色参数被拒绝：%1").arg(uiText(error)),true);return false;
    }
    auto& node=impl_->chain.nodes[index];
    if(node.color==colour&&node.enabled==colour.enabled)return true;
    const auto previous=node;const bool wasValid=chainValid();
    node.color=colour;node.enabled=colour.enabled;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;node=previous;impl_->revalidate();
        emit notice(uiText(error),true);emit settingsChanged();return false;
    }
    if(!impl_->colourReplaying){
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        const bool sameDrag=index==impl_->colourLastIndex&&now-impl_->colourLastPushMs<600&&!impl_->colourUndo.empty();
        if(!sameDrag){
            auto before=previous.color;before.enabled=previous.enabled;
            impl_->colourUndo.push_back({index,before});
            if(impl_->colourUndo.size()>64)impl_->colourUndo.erase(impl_->colourUndo.begin());
        }
        impl_->colourLastPushMs=now;impl_->colourLastIndex=index;impl_->colourRedo.clear();
    }
    emit settingsChanged();
    if(previous.enabled!=node.enabled)emitChainChanged();else emitParameterChanged(&QmlPlayerBridge::colourParamsChanged,wasValid);
    return true;
}
QVariantMap QmlPlayerBridge::colourState() const { return colourStateAt(impl_->colourIndex()); }
// Any colour node's full state, so several node cards can show their own
// values at once; edits still go through the selected-layer setters.
QVariantMap QmlPlayerBridge::colourStateAt(int index) const {
    engine::ColorSettings c{};
    if(index>=0&&uint32_t(index)<impl_->chain.nodeCount&&impl_->chain.nodes[index].type==engine::EffectType::Color){
        c=impl_->chain.nodes[index].color;c.enabled=impl_->chain.nodes[index].enabled;
    }else index=-1;
    QVariantMap out;
    out["index"]=index;out["enabled"]=c.enabled;
    out["blackWhite"]=c.blackWhite;out["groupBypassMask"]=int(c.groupBypassMask);
    for(const auto& p:kColourParams)out[QString::fromUtf8(p.name)]=double(*colourScalar(c,p.name));
    const auto list=[](const auto& values){QVariantList out;for(const auto v:values)out<<double(v);return out;};
    out["mixerHue"]=list(c.mixerHue);out["mixerSaturation"]=list(c.mixerSaturation);
    out["mixerLuminance"]=list(c.mixerLuminance);out["blackWhiteMix"]=list(c.blackWhiteMix);
    out["calibrationHue"]=list(c.calibrationHue);out["calibrationSaturation"]=list(c.calibrationSaturation);
    QVariantList curves,wheels,samples;
    for(const auto& curve:c.curves){
        QVariantList points;
        for(int i=0;i<curve.count;++i)points<<QVariantMap{{"x",double(curve.points[i].x)},{"y",double(curve.points[i].y)}};
        curves<<QVariant(points);
        QVariantList values;
        for(int i=0;i<=128;++i)values<<double(pipeline::ColorGradeTables::curveValue(curve,float(i)/128.0f));
        samples<<QVariant(values);
    }
    for(const auto& wheel:c.grading)wheels<<QVariantMap{{"hue",double(wheel.hue)},
        {"saturation",double(wheel.saturation)},{"luminance",double(wheel.luminance)}};
    out["curves"]=curves;out["curveSamples"]=samples;out["grading"]=wheels;
    out["lutName"]=QString::fromStdWString(c.lutNameString());out["lutInputSpace"]=c.lutInputSpace;
    return out;
}
bool QmlPlayerBridge::setColourCurve(int channel,const QVariantList& points){
    if(channel<0||channel>=4||points.size()<2||points.size()>engine::kColorCurvePoints)return false;
    auto c=selectedColourSettings();auto& curve=c.curves[channel];curve.reset();curve.count=int(points.size());
    for(int i=0;i<curve.count;++i){
        const auto point=points[i].toMap();bool xok=false,yok=false;
        const double x=point.value("x").toDouble(&xok),y=point.value("y").toDouble(&yok);
        if(!xok||!yok||!std::isfinite(x)||!std::isfinite(y))return false;
        curve.points[i]={float(x),float(y)};
    }
    return commitColour(c);
}
bool QmlPlayerBridge::setColourOption(const QString& name,int value){
    auto c=selectedColourSettings();
    if(name=="blackWhite"&&(value==0||value==1))c.blackWhite=value!=0;
    else if(name=="groupBypassMask"&&value>=0&&value<128)c.groupBypassMask=uint32_t(value);
    else if(name=="lutInputSpace"&&value>=0&&value<=2)c.lutInputSpace=value;
    else return false;
    return commitColour(c);
}
bool QmlPlayerBridge::setColourWheel(int zone,double hue,double saturation,double luminance){
    if(zone<0||zone>=engine::kColorGradingZones||!std::isfinite(hue)||
       !std::isfinite(saturation)||!std::isfinite(luminance))return false;
    auto c=selectedColourSettings();c.grading[zone]={float(hue),float(saturation),float(luminance)};
    return commitColour(c);
}
bool QmlPlayerBridge::resetColourGroup(int group){
    auto c=selectedColourSettings();const engine::ColorSettings d;
    switch(group){
    case -1:{const bool enabled=c.enabled;c=d;c.enabled=enabled;break;}
    case 0:c.exposure=c.contrast=c.highlights=c.shadows=c.whites=c.blacks=0;break;
    case 1:c.temperature=c.tint=c.vibrance=c.saturation=0;break;
    case 2:c.paramHighlights=c.paramLights=c.paramDarks=c.paramShadows=0;c.curves=d.curves;
        c.splitHighlights=c.splitMidtones=c.splitShadows=0;break;
    case 3:c.mixerHue=d.mixerHue;c.mixerSaturation=d.mixerSaturation;c.mixerLuminance=d.mixerLuminance;
        c.blackWhite=d.blackWhite;c.blackWhiteMix=d.blackWhiteMix;break;
    case 4:c.grading=d.grading;c.gradingBlending=d.gradingBlending;c.gradingBalance=0;break;
    case 5:c.calibrationShadowTint=0;c.calibrationHue=d.calibrationHue;c.calibrationSaturation=d.calibrationSaturation;break;
    case 6:c.clearLut();c.lutStrength=d.lutStrength;c.lutInputSpace=d.lutInputSpace;break;
    case 7:c.texture=c.clarity=c.dehaze=0;break;
    default:return false;
    }
    if(group>=0&&group<7)c.groupBypassMask&=~(1u<<group);
    return commitColour(c);
}

// --- LUT library and colour looks (P4-g) ------------------------------------
// LUTs go to the folder the engine and the export worker resolve by name
// (runtime_local/luts); looks are user data beside the presets.
QVariantList QmlPlayerBridge::lutLibrary() const {
    QVariantList out;
    const engine::ColorLutStore store(runtime::localDataDirectory());
    for (const auto& name : store.list()) out << QString::fromStdWString(name);
    return out;
}
QVariantList QmlPlayerBridge::colourLooks() const {
    engine::ColorLookStore store(impl_->dataDir);
    QVariantList out;
    if (!store.load()) return out;
    for (size_t i = 0; i < store.entries().size(); ++i) {
        const auto& look = store.entries()[i];
        out << QVariantMap{{"index", int(i)}, {"name", QString::fromStdWString(look.name)},
                           {"lut", QString::fromStdWString(look.color.lutNameString())}};
    }
    return out;
}
void QmlPlayerBridge::importLutDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入 .cube LUT"), QString(),
                                                      tr("Cube LUT (*.cube);;所有文件 (*.*)"));
    if (!path.isEmpty()) importLut(path);
}
bool QmlPlayerBridge::importLut(const QString& path) {
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点，无法使用 LUT"), true); return false; }
    const engine::ColorLutStore store(runtime::localDataDirectory());
    std::wstring name; std::string error;
    if (!store.importFile(std::filesystem::path(path.toStdWString()), name, error) || name.empty()) {
        veyra::log::warn("ui-colour", std::format("lut import rejected path={} error={}", path.toStdString(), error));
        emit notice(tr("LUT 导入失败：%1").arg(QString::fromStdString(error)), true);
        return false;
    }
    veyra::log::info("ui-colour", std::format("lut imported name={}", QString::fromStdWString(name).toStdString()));
    emit colourLibraryChanged();
    const bool applied = setColourLut(QString::fromStdWString(name));
    if (applied) emit notice(tr("已导入并使用 LUT：%1").arg(QString::fromStdWString(name)), false);
    return applied;
}
bool QmlPlayerBridge::setColourLut(const QString& name) {
    auto c = selectedColourSettings();
    if (name.isEmpty()) { c.clearLut(); return commitColour(c); }
    const engine::ColorLutStore store(runtime::localDataDirectory());
    engine::ColorLutData data;
    if (!store.validName(name.toStdWString()) || !store.resolve(name.toStdWString(), data) || !data.valid()) {
        emit notice(tr("LUT 不可用：%1").arg(name), true);
        return false;
    }
    if (!c.setLutName(name.toStdWString())) { emit notice(tr("LUT 名称过长"), true); return false; }
    if (c.lutStrength <= 0.0f) c.lutStrength = 100.0f;
    c.enabled = true;
    return commitColour(c);
}
bool QmlPlayerBridge::saveColourLook(const QString& name, bool replace) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 48) { emit notice(tr("颜色预设名称需为 1–48 个字符"), true); return false; }
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点"), true); return false; }
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load()) { emit notice(tr("颜色预设库无法读取：%1").arg(veyra::ui::i18n::text(store.error())), true); return false; }
    const bool exists = std::any_of(store.entries().begin(), store.entries().end(),
                                    [&](const auto& e) { return e.name == trimmed.toStdWString(); });
    if (exists && !replace) { emit notice(tr("已存在同名颜色预设"), true); return false; }
    auto colour = selectedColourSettings(); colour.enabled = true;
    if (!store.put(trimmed.toStdWString(), colour, replace) || !store.save()) {
        emit notice(tr("颜色预设保存失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    veyra::log::info("ui-colour", std::format("look saved name={} layer={}", trimmed.toStdString(), impl_->colourIndex()));
    emit colourLibraryChanged();
    emit notice(tr("已保存颜色预设：%1").arg(trimmed), false);
    return true;
}
bool QmlPlayerBridge::applyColourLook(int index) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || size_t(index) >= store.entries().size()) {
        emit notice(tr("颜色预设不存在"), true); return false;
    }
    auto colour = store.entries()[size_t(index)].color; colour.enabled = true;
    if (colour.hasLut()) {
        const engine::ColorLutStore luts(runtime::localDataDirectory());
        engine::ColorLutData data;
        if (!luts.resolve(colour.lutNameString(), data)) {
            emit notice(tr("预设引用的 LUT 不存在：%1，未应用").arg(QString::fromStdWString(colour.lutNameString())), true);
            return false;
        }
    }
    const bool ok = commitColour(colour);
    veyra::log::info("ui-colour", std::format("look applied index={} layer={} ok={}", index, impl_->colourIndex(), ok));
    return ok;
}
bool QmlPlayerBridge::deleteColourLook(int index) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || !store.erase(size_t(index)) || !store.save()) {
        emit notice(tr("删除颜色预设失败"), true); return false;
    }
    emit colourLibraryChanged();
    return true;
}
void QmlPlayerBridge::importColourLookDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入颜色预设"), QString(),
                                                      tr("Veyra 颜色预设 (*.vpcolor);;所有文件 (*.*)"));
    if (!path.isEmpty()) importColourLook(path);
}
bool QmlPlayerBridge::importColourLook(const QString& path) {
    engine::ColorLookStore store(impl_->dataDir);
    std::wstring name;
    if (!store.load() || !store.importFile(std::filesystem::path(path.toStdWString()), name)) {
        emit notice(tr("导入颜色预设失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    emit colourLibraryChanged();
    emit notice(tr("已导入颜色预设：%1").arg(QString::fromStdWString(name)), false);
    return true;
}
void QmlPlayerBridge::exportColourLookDialog(int index) {
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出颜色预设"), QString(),
                                                      tr("Veyra 颜色预设 (*.vpcolor)"));
    if (!path.isEmpty()) exportColourLook(index, path);
}
bool QmlPlayerBridge::exportColourLook(int index, const QString& path) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || !store.exportFile(size_t(index), std::filesystem::path(path.toStdWString()))) {
        emit notice(tr("导出颜色预设失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    emit notice(tr("已导出颜色预设"), false);
    return true;
}

// --- image export (P4-b) -----------------------------------------------------
bool QmlPlayerBridge::imageBatchActive() const { return impl_->imagePhase != Impl::ImagePhase::Idle; }
int QmlPlayerBridge::imageBatchDone() const { return impl_->imageDone; }
int QmlPlayerBridge::imageBatchTotal() const { return int(impl_->imageFiles.size()); }
int QmlPlayerBridge::imageBatchFailures() const { return impl_->imageFailures; }
QString QmlPlayerBridge::imageBatchStatus() const { return impl_->imageStatus; }
void QmlPlayerBridge::saveFrameDialog() {
    if(!impl_->pendingFrameExport.isEmpty()||imageBatchActive()){emit notice(tr("请等待当前图片导出完成"),true);return;}
    if (!hasSource()) { emit notice(tr("没有可保存的画面"), true); return; }
    if (impl_->chain.mode == engine::ChainMode::Node) {
        emit notice(tr("图片导出只接受列表模式；请切回列表模式再导出"), true); return;
    }
    const QString suggested = QDir(screenshotDirectory()).filePath(
        QFileInfo(QString::fromStdWString(impl_->sourceLabel)).completeBaseName() + QStringLiteral("-veyra.png"));
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出当前画面"), suggested,
                                                      tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path)) { emit notice(tr("目标文件已存在，请换一个文件名"), true); return; }
    impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(path)));
    impl_->pendingFrameExport=path;impl_->pendingFrameDeadline=QDateTime::currentMSecsSinceEpoch()+30000;
    emit notice(tr("正在保存：%1").arg(QDir::toNativeSeparators(path)), false);
}
void QmlPlayerBridge::exportImagesDialog() {
    const auto files = QFileDialog::getOpenFileNames(nullptr, tr("选择要处理的图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg)"));
    if (files.isEmpty()) return;
    const QString folder = QFileDialog::getExistingDirectory(nullptr, tr("保存到"), QFileInfo(files.front()).absolutePath());
    if (!folder.isEmpty()) startImageBatch(files, folder);
}
bool QmlPlayerBridge::startImageBatch(const QStringList& files, const QString& folder) {
    auto& i = *impl_;
    if (i.imagePhase != Impl::ImagePhase::Idle) { emit notice(tr("已有一批图片在处理"), true); return false; }
    if (i.chain.mode == engine::ChainMode::Node) {
        emit notice(tr("图片导出只接受列表模式；请切回列表模式再导出"), true); return false;
    }
    if (files.isEmpty() || !QFileInfo(folder).isDir()) { emit notice(tr("请选择图片和已存在的输出目录"), true); return false; }
    i.imageFiles = files; i.imageFolder = folder;
    i.imageIndex = -1; i.imageDone = 0; i.imageFailures = 0;
    i.imagePhase = Impl::ImagePhase::Opening; i.imageWaits = 0;
    veyra::log::info("image-batch", std::format("start files={} folder={}", files.size(), folder.toStdString()));
    emit imageBatchChanged();
    return true;
}
void QmlPlayerBridge::cancelImageBatch() {
    if (impl_->imagePhase == Impl::ImagePhase::Idle) return;
    impl_->imagePhase = Impl::ImagePhase::Idle;
    impl_->imageStatus = tr("已取消：完成 %1 / %2").arg(impl_->imageDone).arg(impl_->imageFiles.size());
    veyra::log::info("image-batch", "cancelled");
    emit imageBatchChanged();
}
void QmlPlayerBridge::tickImageBatch() {
    auto& i = *impl_;
    if(!i.pendingFrameExport.isEmpty()){
        QString path=i.pendingFrameExport;
        if(!QFileInfo(path).isFile())path=QFileInfo(path).path()+QLatin1Char('/')+QFileInfo(path).completeBaseName()+QStringLiteral(".jxr");
        const bool reported=i.snapshot.status.find(L"已保存")!=std::wstring::npos;
        if(reported&&QFileInfo(path).size()>0&&exportFileClosed(path)){
            const bool valid=path.endsWith(QStringLiteral(".jxr"),Qt::CaseInsensitive)||!QImageReader(path).read().isNull();
            if(valid){i.pendingFrameExport.clear();exportCompletionNotice(exportCompletionSound(),++i.imageCompletionEvent,"image");emit notice(tr("图片导出完成：%1").arg(path),false);}
        }
        if(!i.pendingFrameExport.isEmpty()&&QDateTime::currentMSecsSinceEpoch()>i.pendingFrameDeadline){i.pendingFrameExport.clear();emit notice(tr("图片保存未确认完成，请查看诊断"),true);}
    }
    if (i.imagePhase == Impl::ImagePhase::Idle) return;
    auto fail = [&](const QString& why) {
        ++i.imageFailures;
        veyra::log::warn("image-batch", std::format("item failed index={} reason={}", i.imageIndex, why.toStdString()));
        emit notice(tr("图片未导出：%1（%2）").arg(QFileInfo(i.imageFiles[i.imageIndex]).fileName(), why), true);
        i.imagePhase = Impl::ImagePhase::Opening; i.imageSession = 0;
    };
    const auto& s = i.snapshot;
    if (i.imagePhase == Impl::ImagePhase::Opening && i.imageSession == 0) {
        // Next item.
        if (++i.imageIndex >= i.imageFiles.size()) {
            i.imagePhase = Impl::ImagePhase::Idle;
            i.imageStatus = tr("完成 %1 张，失败 %2 张").arg(i.imageDone).arg(i.imageFailures);
            veyra::log::info("image-batch", std::format("finished done={} failed={}", i.imageDone, i.imageFailures));
            emit imageBatchChanged();
            emit notice(i.imageStatus, i.imageFailures > 0);
            if(i.imageDone>0&&i.imageFailures==0)exportCompletionNotice(exportCompletionSound(),++i.imageCompletionEvent,"image-batch");
            return;
        }
        const QString input = i.imageFiles[i.imageIndex];
        i.imageTarget = QDir(i.imageFolder).filePath(QFileInfo(input).completeBaseName() + QStringLiteral("-veyra.png"));
        i.imageStatus = tr("正在处理 %1 / %2：%3").arg(i.imageIndex + 1).arg(i.imageFiles.size()).arg(QFileInfo(input).fileName());
        emit imageBatchChanged();
        if (!QFileInfo(input).isFile()) { fail(tr("文件不存在")); return; }
        if (QFileInfo::exists(i.imageTarget)) { fail(tr("输出已存在，不覆盖")); return; }
        openPath(input);
        i.imageSession = i.openingSessionId ? i.openingSessionId : UINT64_MAX;
        i.imageWaits = 0;
        return;
    }
    ++i.imageWaits;
    if (i.imagePhase == Impl::ImagePhase::Opening) {
        if (s.sessionId == i.imageSession && s.failed) { fail(tr("无法打开")); return; }
        if (s.sessionId == i.imageSession && s.image && s.frames > 0 && !applying()) {
            impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(i.imageTarget)));
            i.imagePhase = Impl::ImagePhase::Saving; i.imageWaits = 0;
        } else if (i.imageWaits > 60 * 20) { fail(tr("打开超时")); }
        return;
    }
    // Saving: the engine writes on its own thread; HDR output becomes .jxr.
    QString written = i.imageTarget;
    if (!QFileInfo(written).isFile()) written = QFileInfo(i.imageTarget).path() + QLatin1Char('/') + QFileInfo(i.imageTarget).completeBaseName() + QStringLiteral(".jxr");
    if (QFileInfo(written).isFile() && QFileInfo(written).size() > 0 && s.status.find(L"已保存")!=std::wstring::npos && exportFileClosed(written)) {
        ++i.imageDone;
        veyra::log::info("image-batch", std::format("saved index={} path={}", i.imageIndex, written.toStdString()));
        i.imagePhase = Impl::ImagePhase::Opening; i.imageSession = 0;
        emit imageBatchChanged();
    } else if (i.imageWaits > 60 * 15) {
        fail(tr("保存超时"));
    }
}

// --- subtitles (P4-c) --------------------------------------------------------
namespace {
const wchar_t* const kSubtitleFonts[] = {L"Microsoft YaHei UI", L"SimHei", L"SimSun", L"DengXian", L"Arial", L"Segoe UI"};
bool subtitleIsChinese(const engine::SubtitleTrack& t) {
    const auto has = [](const std::wstring& s, const wchar_t* k) { return s.find(k) != std::wstring::npos; };
    return has(t.language, L"chi") || has(t.language, L"zho") || has(t.language, L"zh") || has(t.name, L"中") ||
           has(t.name, L"简") || has(t.name, L"繁");
}
void ensureGdiplus() {
    static ULONG_PTR token = 0;
    if (token) return;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
}
}
void QmlPlayerBridge::refreshSubtitles(const std::wstring& media) {
    auto& i = *impl_;
    i.subTracks.clear(); i.subExternal = i.subLoaded = 0;
    i.subPrimary = i.subSecondary = -1; i.subPrimaryChosen = i.subSecondaryChosen = false;
    i.subStatus=QFileInfo(QString::fromStdWString(media)).isFile()?tr("正在读取字幕"):QString{}; i.subMedia = media;
    if(i.subFileLoader)i.subFileGeneration=i.subFileLoader->requestFile(L"");
    i.subFilePath.clear();
    if (i.subAlignWorker.joinable()) { i.subAlignWorker.request_stop(); i.subAlignWorker = {}; }
    i.subAligning = false; i.subAlignReady = false;
    if (!i.subLoader) i.subLoader = std::make_unique<engine::SubtitleLoader>();
    // A file on a server is scanned only while playback has its buffer (#48): a second reader
    // that takes the same share's bandwidth would starve the picture it is meant to subtitle.
    const bool server = !media.empty() && engine::subtitleServerFile(media);
    i.subScanGate = server ? std::make_shared<std::atomic<bool>>(false) : nullptr;
    i.subScanOpen = false;
    i.subLoader->setReadGate(i.subScanGate);
    if (server) i.subStatus = tr("字幕将在视频缓冲好后读取");
    i.subGeneration = i.subLoader->request(media);
    emit subtitlesChanged();
}
void QmlPlayerBridge::tickSubtitles() {
    auto& i = *impl_;
    // The only place that sees playback while it happens: remember where the Bilibili
    // video got to, so the report at flush time describes what was actually watched.
    if (!i.biliWatchBvid.empty() && i.biliWatchCid > 0) {
        const auto& cur = i.snapshot;
        using Tr = engine::TransportState;
        const bool playing = cur.running && !cur.failed &&
                             (cur.transport == Tr::Playing || cur.transport == Tr::Paused);
        if (playing && cur.position > 0.0) {
            i.biliWatchLastPos = cur.position;
            if (cur.duration > 0.0) i.biliWatchDuration = cur.duration;
        } else if (cur.transport == Tr::Ended && !cur.failed) {
            // 播完立刻上报。以前只有「打开另一个来源」或程序退出时才写回历史，于是刚看完
            // 的视频要等下一次播放才出现在历史记录里。
            // Report at the end. The write used to happen only when another source was
            // opened or the app exited, so a video that had just finished did not appear
            // in the history until the next playback.
            reportBiliWatchProgressAsync();
        }
    }
    bool changed = false;
    if (i.subScanGate) {
        // Opens once playback holds a few seconds ahead (or has read the whole file), closes when
        // it runs short, and never opens for a server too slow for the stream itself.
        const auto& n = i.snapshot.network;
        const bool ended = n.active && n.state == media::NetworkStats::State::Ended;
        const bool healthy = n.active && !n.bandwidthLow && n.state == media::NetworkStats::State::Playing;
        const bool open = i.subScanOpen ? (ended || (healthy && n.bufferedSeconds >= 1.0))
                                        : (ended || (healthy && n.bufferedSeconds >= 3.0));
        if (open != i.subScanOpen) {
            i.subScanOpen = open;
            i.subScanGate->store(open);
            veyra::log::info("subtitle", std::format("server scan gate {} (buffer {:.1f}s)", open ? "open" : "closed", n.bufferedSeconds));
            if (open && i.subStatus == tr("字幕将在视频缓冲好后读取")) { i.subStatus = tr("正在读取字幕"); changed = true; }
        }
    }
    if (i.subLoader) {
        if (auto result = i.subLoader->poll(); result && result->generation == i.subGeneration) {
            // Replace the loader's tracks, keep user-loaded files and per-track offsets.
            const size_t count = result->tracks.size();
            for (size_t k = 0; k < std::min(i.subLoaded, count); ++k)
                result->tracks[k].offsetMs = i.subTracks[i.subExternal + k].offsetMs;
            i.subTracks.erase(i.subTracks.begin() + std::ptrdiff_t(i.subExternal),
                              i.subTracks.begin() + std::ptrdiff_t(i.subExternal + i.subLoaded));
            i.subTracks.insert(i.subTracks.begin() + std::ptrdiff_t(i.subExternal),
                               std::make_move_iterator(result->tracks.begin()), std::make_move_iterator(result->tracks.end()));
            i.subLoaded = count;
            if(result->complete&&(i.subStatus==tr("正在读取字幕")||i.subStatus==tr("字幕将在视频缓冲好后读取")))i.subStatus.clear();
            int firstUsable = -1, firstChinese = -1;
            for (size_t k = 0; k < i.subTracks.size(); ++k) {
                if (!i.subTracks[k].usable()) continue;
                if (firstUsable < 0) firstUsable = int(k);
                if (firstChinese < 0 && subtitleIsChinese(i.subTracks[k])) firstChinese = int(k);
            }
            if (!i.subPrimaryChosen) i.subPrimary = firstChinese >= 0 ? firstChinese : firstUsable;
            if (!i.subSecondaryChosen && i.prefBool("subtitleSecondLanguage", false)) {
                int candidate = -1;
                for (size_t k = 0; k < i.subTracks.size(); ++k)
                    if (i.subTracks[k].usable() && int(k) != i.subPrimary) { candidate = int(k); break; }
                i.subSecondary = candidate;
            }
            if (result->complete)
                for (const auto& t : i.subTracks)
                    veyra::log::info("subtitle", std::format("track name={} codec={} embedded={} cues={} usable={}",
                        QString::fromStdWString(t.name).toStdString(), QString::fromStdWString(t.codec).toStdString(),
                        t.embedded ? 1 : 0, t.cues.size(), t.usable() ? 1 : 0));
            veyra::log::info("subtitle", std::format("tracks={} primary={} secondary={}", i.subTracks.size(), i.subPrimary, i.subSecondary));
            changed = true;
        }
    }
    if(i.subFileLoader){
        if(auto result=i.subFileLoader->poll();result&&result->generation==i.subFileGeneration&&result->complete&&!i.subFilePath.empty()){
            const auto file=QFileInfo(QString::fromStdWString(i.subFilePath)).fileName();
            i.subFilePath.clear();
            const bool usable=std::any_of(result->tracks.begin(),result->tracks.end(),[](const auto& t){return t.usable();});
            if(!usable){
                const auto detail=result->tracks.empty()?tr("文件格式不支持、数据损坏或没有字幕内容"):uiText(result->tracks.front().note);
                i.subStatus=tr("外部字幕加载失败：%1 · %2").arg(file,detail);
                emit notice(i.subStatus,true);
            }else{
                const int added=int(result->tracks.size());
                int first=0;
                while(!result->tracks[size_t(first)].usable())++first;
                i.subTracks.insert(i.subTracks.begin(),std::make_move_iterator(result->tracks.begin()),std::make_move_iterator(result->tracks.end()));
                i.subExternal+=size_t(added);
                if(i.subSecondary>=0)i.subSecondary+=added;
                if(i.subAlignTrack>=0)i.subAlignTrack+=added;
                i.subPrimary=first;i.subPrimaryChosen=true;
                i.subStatus=tr("已加载外部字幕：%1").arg(file);
                veyra::log::info("subtitle",std::format("external loaded tracks={} file={}",added,file.toStdString()));
            }
            changed=true;
        }
    }
    if (i.subAlignReady.exchange(false) && i.subAlignGeneration == i.subGeneration &&
        i.subAlignTrack >= 0 && size_t(i.subAlignTrack) < i.subTracks.size()) {
        i.subTracks[size_t(i.subAlignTrack)].offsetMs = i.subAlignOffset.load();
        std::lock_guard lock(i.subAlignMutex);
        i.subStatus = tr("自动对齐：%1（Z/X 可微调）").arg(QString::fromStdWString(i.subAlignDetail));
        veyra::log::info("subtitle", std::format("auto align applied offsetMs={}", i.subAlignOffset.load()));
        changed = true;
    }
    if (i.subAligning.load() != (i.subStatus.startsWith(tr("正在自动对齐")))) changed = true;
    if (changed) emit subtitlesChanged();

    // Draw. The overlay is a layered child of the video window: it moves with the
    // picture and is clipped by the same window region as the video itself.
    if (!i.videoWindow || !IsWindow(i.videoWindow)) return;
    if (!i.subOverlay) {
        // A refused layered child (missing compatibility manifest, old OS) is
        // reported once; retrying every tick would only flood the log.
        if (i.subOverlayFailed) return;
        ensureGdiplus();
        i.subOverlay = createSubtitleOverlay(i.videoWindow);
        const DWORD error = i.subOverlay ? 0 : GetLastError();
        veyra::log::info("subtitle", std::format("overlay created ok={} err={}", i.subOverlay != nullptr, error));
        if (!i.subOverlay) { i.subOverlayFailed = true; return; }
    }
    RECT client{}; GetClientRect(i.videoWindow, &client);
    RECT current{}; GetClientRect(i.subOverlay, &current);
    if (current.right != client.right || current.bottom != client.bottom)
        SetWindowPos(i.subOverlay, HWND_TOP, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
    const auto& s = i.snapshot;
    std::vector<SubtitleLine> draw;
    const bool show = i.prefBool("subtitleEnabled", true) && (s.running && !s.image && !s.capture) && !i.subTracks.empty();
    if (show) {
        auto append = [&](int index, bool secondary) {
            if (index < 0 || size_t(index) >= i.subTracks.size()) return;
            const auto& track = i.subTracks[size_t(index)];
            if (!track.usable()) return;
            const double at = s.position + double(track.offsetMs) / 1000.0;
#ifdef VEYRA_HAS_LIBASS
            if (track.ass) {
                // ASS/SSA: libass draws the whole script (effects, karaoke,
                // container fonts). The parsed text only feeds diagnostics.
                SubtitleLine line;
                line.ass = track.ass; line.assTimeMs = at * 1000.0; line.secondary = secondary;
                line.text = engine::textAt(track, at);
                draw.push_back(std::move(line));
                return;
            }
#endif
            for (const auto* cue : engine::cuesAt(track, at, 3)) {
                SubtitleLine line;
                line.text = cue->text; line.bitmap = cue->bitmap;
                line.style = track.styles.empty() ? engine::SubtitleStyle{}
                           : track.styles[size_t(std::clamp(cue->style, 0, int(track.styles.size()) - 1))];
                if (cue->alignOverride) line.style.alignment = cue->alignOverride;
                line.alignOverride = cue->alignOverride;
                line.posX = cue->posX >= 0 ? cue->posX / std::max(1.0, track.scriptWidth) : -1.0;
                line.posY = cue->posY >= 0 ? cue->posY / std::max(1.0, track.scriptHeight) : -1.0;
                line.secondary = secondary;
                // Outline level 0..3: none / thin / normal / thick, over the track's own width.
                const int outline = i.prefInt("subtitleOutline", 2);
                if (outline == 0) line.style.outlineWidth = 0;
                else if (line.style.outlineWidth <= 0) line.style.outlineWidth = outline == 1 ? 1.0 : outline == 2 ? 2.0 : 3.5;
                else line.style.outlineWidth *= outline == 1 ? 0.6 : outline == 3 ? 1.6 : 1.0;
                draw.push_back(std::move(line));
            }
        };
        append(i.subSecondary, true);   // the painter stacks bottom-up
        append(i.subPrimary, false);
    }
    SubtitleView view;
    view.scale = double(i.prefInt("subtitleSize", 22)) / 22.0;
    const int font = i.prefInt("subtitleFont", 0);
    view.fontOverride = font > 0 ? std::wstring(kSubtitleFonts[std::clamp(font, 0, 5)]) : std::wstring{};
    view.outline = i.prefInt("subtitleOutline", 2) > 0;
    view.background = i.prefBool("subtitleBackground", false);
    view.bottomMargin = i.prefInt("subtitleMargin", 0) + i.subBottomInset;
    view.targetLines = i.prefInt("subtitleLines", 2);
    view.fitToLines = i.prefBool("subtitleFit", false);
    view.preview = i.engine.previewView();
    view.videoWidth = s.metrics.resolution.output.width;
    view.videoHeight = s.metrics.resolution.output.height;
    updateSubtitleOverlay(i.subOverlay, draw, view);
    QString joined;
    for (const auto& line : draw) { if (line.text.empty()) continue; if (!joined.isEmpty()) joined += QStringLiteral(" | "); joined += QString::fromStdWString(line.text); }
    if (joined != i.subText) {
        i.subText = joined;
        if (!joined.isEmpty()) veyra::log::info("subtitle-overlay", "text=" + joined.toStdString());
        emit subtitleTextChanged();
    }
    tickDanmaku();
}

// Danmaku rides the same per-frame tick as the subtitle overlay: it reads the
// engine's own position, so pause, seek and speed changes need no extra wiring.
void QmlPlayerBridge::tickDanmaku() {
    auto& i = *impl_;
    if (!i.danmakuEnabled || !i.danmakuWindow) return;
    const auto& s = i.snapshot;
    // Only over a moving picture: a still image or a capture has no video timeline.
    if (!s.running || s.image || s.capture) { i.danmakuWindow->hide(); return; }
    if (s.duration <= 0) { i.danmakuWindow->hide(); return; }

    // A jump means a seek: the engine rebuilds its tracks rather than letting old
    // comments fly in from stale positions.
    if (i.danmakuLastSeconds >= 0 &&
        (s.position < i.danmakuLastSeconds - 0.5 || s.position > i.danmakuLastSeconds + 2.0))
        i.danmakuWindow->noteSeek();
    i.danmakuLastSeconds = s.position;

    i.danmakuWindow->follow(impl_->videoWindow);

    media::DanmakuView view;
    view.scale = double(i.prefInt("danmakuScale", 100)) / 100.0;
    view.opacity = double(i.prefInt("danmakuOpacity", 80)) / 100.0;
    view.speedFactor = double(i.prefInt("danmakuSpeed", 100)) / 100.0;
    view.area = double(i.prefInt("danmakuArea", 50)) / 100.0;
    view.videoWidth = int(s.metrics.resolution.output.width);
    view.videoHeight = int(s.metrics.resolution.output.height);
    i.danmakuWindow->render(s.position, view);
}
QVariantList QmlPlayerBridge::subtitleTracks() const {
    QVariantList out;
    for (size_t k = 0; k < impl_->subTracks.size(); ++k) {
        const auto& t = impl_->subTracks[k];
        QString label = QString::fromStdWString(t.name);
        if (label.isEmpty()) label = QString::fromStdWString(t.language);
        if (label.isEmpty()) label = tr("字幕 %1").arg(k + 1);
        QString note = t.embedded ? tr("内嵌") : tr("外挂");
        if (!t.codec.empty()) note += QStringLiteral(" · ") + QString::fromStdWString(t.codec).toUpper();
        if (!t.note.empty()) note += QStringLiteral(" · ") + uiText(t.note);
        else if(!t.usable()) note += QStringLiteral(" · ") + tr("未解码出可显示的字幕");
        out << QVariantMap{{"index", int(k)}, {"label", label}, {"note", note}, {"usable", t.usable()},
                           {"embedded", t.embedded}, {"cues", int(t.cues.size())}, {"offsetMs", t.offsetMs}};
    }
    return out;
}
int QmlPlayerBridge::subtitlePrimary() const { return impl_->subPrimary; }
void QmlPlayerBridge::setSubtitlePrimary(int index) {
    if (index < -1 || index >= int(impl_->subTracks.size())) return;
    if (index >= 0 && !impl_->subTracks[size_t(index)].usable()) { emit notice(tr("该字幕轨没有可显示的内容"), true); return; }
    impl_->subPrimaryChosen = true;
    if (impl_->subPrimary == index) return;
    impl_->subPrimary = index;
    if (impl_->subSecondary == index) impl_->subSecondary = -1;
    veyra::log::info("subtitle", std::format("primary={} secondary={}", impl_->subPrimary, impl_->subSecondary));
    emit subtitlesChanged();
}
int QmlPlayerBridge::subtitleSecondary() const { return impl_->subSecondary; }
void QmlPlayerBridge::setSubtitleSecondary(int index) {
    if (index < -1 || index >= int(impl_->subTracks.size())) return;
    if (index >= 0 && (!impl_->subTracks[size_t(index)].usable() || index == impl_->subPrimary)) return;
    impl_->subSecondaryChosen = true;
    if (impl_->subSecondary == index) return;
    impl_->subSecondary = index;
    veyra::log::info("subtitle", std::format("primary={} secondary={}", impl_->subPrimary, impl_->subSecondary));
    emit subtitlesChanged();
}
int QmlPlayerBridge::subtitleOffsetMs() const {
    const int p = impl_->subPrimary;
    return p >= 0 && size_t(p) < impl_->subTracks.size() ? impl_->subTracks[size_t(p)].offsetMs : 0;
}
void QmlPlayerBridge::setSubtitleOffsetMs(int ms) {
    const int p = impl_->subPrimary;
    if (p < 0 || size_t(p) >= impl_->subTracks.size()) return;
    auto& track = impl_->subTracks[size_t(p)];
    ms = std::clamp(ms, -30000, 30000);
    if (track.offsetMs == ms) return;
    track.offsetMs = ms;
    impl_->subStatus = tr("字幕延时 %1 ms").arg(ms);
    veyra::log::info("subtitle", std::format("offset track={} offsetMs={}", p, ms));
    emit subtitlesChanged();
}
QString QmlPlayerBridge::subtitleStatus() const { return impl_->subStatus; }
bool QmlPlayerBridge::subtitleAligning() const { return impl_->subAligning.load(); }
QString QmlPlayerBridge::subtitleText() const { return impl_->subText; }
int QmlPlayerBridge::subtitleBottomInset() const { return impl_->subBottomInset; }
void QmlPlayerBridge::setSubtitleBottomInset(int value) {
    value = std::clamp(value, 0, 400);
    if (impl_->subBottomInset == value) return;
    impl_->subBottomInset = value;
    emit subtitlesChanged();
}
void QmlPlayerBridge::loadSubtitleDialog() {
    const QString start = impl_->subMedia.empty() ? QString() : QFileInfo(QString::fromStdWString(impl_->subMedia)).absolutePath();
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("加载外部字幕"), start,
        tr("字幕文件 (*.srt *.ass *.ssa *.vtt *.sup *.pgs *.idx *.sub *.smi *.sami *.sbv *.mpl *.stl *.txt);;所有文件 (*.*)"));
    if (!path.isEmpty()) loadSubtitleFile(path);
}
bool QmlPlayerBridge::loadSubtitleFile(const QString& path) {
    if(!QFileInfo(path).isFile()){
        emit notice(tr("字幕文件不存在或无法读取"),true);
        return false;
    }
    if(!impl_->subFileLoader)impl_->subFileLoader=std::make_unique<engine::SubtitleLoader>();
    impl_->subFilePath=wideOf(path);
    impl_->subFileGeneration=impl_->subFileLoader->requestFile(impl_->subFilePath);
    impl_->subStatus=tr("正在加载外部字幕：%1").arg(QFileInfo(path).fileName());
    emit subtitlesChanged();
    return true;
}
void QmlPlayerBridge::nudgeSubtitle(int deltaMs) {
    if (impl_->subPrimary < 0) { emit notice(tr("没有正在显示的字幕"), true); return; }
    setSubtitleOffsetMs(subtitleOffsetMs() + deltaMs);
    emit notice(impl_->subStatus, false);
}
void QmlPlayerBridge::cycleSubtitle(bool secondary) {
    auto& i = *impl_;
    if (i.subTracks.empty()) { emit notice(tr("当前片源没有字幕"), true); return; }
    int slot = secondary ? i.subSecondary : i.subPrimary;
    for (size_t n = 0; n <= i.subTracks.size(); ++n) {   // -1 -> 0 -> ... -> -1, skipping unusable
        slot = slot + 1 >= int(i.subTracks.size()) ? -1 : slot + 1;
        if (slot < 0 || (i.subTracks[size_t(slot)].usable() && !(secondary && slot == i.subPrimary))) break;
    }
    if (secondary) setSubtitleSecondary(slot); else setSubtitlePrimary(slot);
    const int now = secondary ? i.subSecondary : i.subPrimary;
    emit notice(now < 0 ? (secondary ? tr("副字幕已关闭") : tr("主字幕已关闭"))
                        : (secondary ? tr("副字幕：%1") : tr("主字幕：%1")).arg(subtitleTracks()[now].toMap().value("label").toString()), false);
}
void QmlPlayerBridge::toggleSubtitles() {
    const bool on = !impl_->prefBool("subtitleEnabled", true);
    setPreference(QStringLiteral("subtitleEnabled"), on);
    emit notice(on ? tr("字幕：开") : tr("字幕：关"), false);
}
void QmlPlayerBridge::autoAlignSubtitle() {
    auto& i = *impl_;
    if (i.subAligning.load()) { emit notice(tr("已有一个自动对齐在进行"), true); return; }
    if (i.subMedia.empty() || i.subPrimary < 0 || !i.subTracks[size_t(i.subPrimary)].usable()) {
        emit notice(tr("没有可用的主字幕，无法自动对齐"), true); return;
    }
    const std::wstring media = i.subMedia;
    const auto track = i.subTracks[size_t(i.subPrimary)];
    i.subAlignGeneration = i.subGeneration; i.subAlignTrack = i.subPrimary;
    i.subAligning = true; i.subAlignReady = false;
    i.subStatus = tr("正在自动对齐：分析音轨（最多前 30 分钟）…");
    emit subtitlesChanged();
    i.subAlignWorker = std::jthread([&i, media, track](std::stop_token stop) {
        const auto result = engine::alignSubtitleToAudio(media, track, 30, stop);
        if (stop.stop_requested()) { i.subAligning = false; return; }
        { std::lock_guard lock(i.subAlignMutex); i.subAlignDetail = result.detail; }
        i.subAlignOffset = result.ok ? result.offsetMs : 0;
        i.subAlignReady = result.ok;
        i.subAligning = false;
    });
}

void QmlPlayerBridge::attachVideoWindow(qulonglong nativeHandle) {
    impl_->videoWindow = reinterpret_cast<HWND>(nativeHandle);
}

// --- embedded browser -------------------------------------------------------


void QmlPlayerBridge::setPreOpenHook(std::function<void()> hook) {
    impl_->preOpen = std::move(hook);
}

QString QmlPlayerBridge::appName() const { return QStringLiteral("Veyra"); }
QString QmlPlayerBridge::version() const { return QStringLiteral(VEYRA_DISPLAY_VERSION); }

// --- playback ---------------------------------------------------------------
QString QmlPlayerBridge::statusText() const { return uiText(impl_->snapshot.status); }
bool QmlPlayerBridge::running() const { return impl_->snapshot.running; }
// 注意：必须保持原来的含义（只有 Paused），不要顺手把 Ended 也算进来 ——
// 这个属性被作者原有的 7 处界面共用（极简/专业/节点/导出/音轨标记），把 Ended
// 算成暂停会改掉本地视频播完后的按钮图标，那是动作者的原功能。
// Keep this exactly as it was (Paused only). Seven of the author's own screens read
// it (极简 / 专业 / 节点 / 导出 / audio-track marks), and counting Ended as paused
// would change the button glyph for a finished local video - the author's behaviour.
bool QmlPlayerBridge::paused() const { return impl_->snapshot.transport == engine::TransportState::Paused; }
bool QmlPlayerBridge::failed() const { return impl_->snapshot.failed; }
bool QmlPlayerBridge::isImage() const { return impl_->snapshot.image; }
bool QmlPlayerBridge::isCapture() const { return impl_->snapshot.capture; }
bool QmlPlayerBridge::hasSource() const { return impl_->snapshot.running || impl_->snapshot.image; }
bool QmlPlayerBridge::openingSource() const { return impl_->openingSource; }
double QmlPlayerBridge::position() const { return impl_->snapshot.position; }
double QmlPlayerBridge::duration() const { return impl_->snapshot.duration; }
double QmlPlayerBridge::progress() const {
    const double d = impl_->snapshot.duration;
    return d > 0.01 ? std::clamp(impl_->snapshot.position / d, 0.0, 1.0) : 0.0;
}
QString QmlPlayerBridge::positionText() const { return mmss(impl_->snapshot.position); }
QString QmlPlayerBridge::durationText() const { return mmss(impl_->snapshot.duration); }

// --- source -----------------------------------------------------------------
QString QmlPlayerBridge::sourceName() const {
    if (impl_->sourceLabel.empty()) return QString();
    if(impl_->liveKind==QLatin1String("url"))return tr("网络视频 · %1").arg(impl_->liveLabel);
    return QFileInfo(utf8Of(impl_->sourceLabel)).fileName();
}
QString QmlPlayerBridge::sourceSummary() const {
    const auto& s = impl_->snapshot;
    if (!s.sourceWidth) return QString();
    // Reported values only: no upscaling claim, no invented bit depth.
    return QStringLiteral("%1x%2").arg(s.sourceWidth).arg(s.sourceHeight);
}
int QmlPlayerBridge::sourceWidth() const { return int(impl_->snapshot.sourceWidth); }
int QmlPlayerBridge::sourceHeight() const { return int(impl_->snapshot.sourceHeight); }
double QmlPlayerBridge::sourceFps() const { return impl_->snapshot.nominalSourceFps; }
int QmlPlayerBridge::sourceRotation() const { return impl_->snapshot.sourceRotationDegrees; }
double QmlPlayerBridge::sourceAspect() const {
    const auto& s = impl_->snapshot;
    // A known DAR already includes SAR/rotation. With no DAR, rotate the coded
    // extent as the presenter does; phone files can omit SAR yet carry a matrix.
    engine::PreviewView view;
    view.displayAspect=s.sourceDisplayAspect;
    view.rotationDegrees=s.sourceRotationDegrees;
    return view.aspect(float(s.sourceWidth),float(s.sourceHeight));
}

// --- live performance -------------------------------------------------------
// XeSS generates inside the provider: the engine only submits source frames,
// so its own rate would read 60 while the SDK presents 120. Show the provider's
// reported submissions for XeSS (AppShell: "SDK提交（非屏幕实测）").
static bool xessPresenting(const engine::PlayerSnapshot& s) {
    return s.fgActive && s.applied.frameGenerationBackend == engine::FrameGenerationBackend::XeSS &&
        s.metrics.flow.xessSdkSubmitFps > 0;
}
double QmlPlayerBridge::submitFps() const {
    const auto& s = impl_->snapshot;
    return xessPresenting(s) ? s.metrics.flow.xessSdkSubmitFps : s.submissionFps.value_or(0.0);
}
bool QmlPlayerBridge::submitFpsKnown() const { return xessPresenting(impl_->snapshot) || impl_->snapshot.submissionFps.has_value(); }
QString QmlPlayerBridge::submitFpsLabel() const { return xessPresenting(impl_->snapshot) ? tr("SDK 提交") : tr("提交"); }
double QmlPlayerBridge::lateMs() const { return impl_->snapshot.lateMs; }
double QmlPlayerBridge::lateP95Ms() const { return impl_->snapshot.lateP95Ms; }
QString QmlPlayerBridge::metricsSummary() const {
    const auto& s = impl_->snapshot;
    // Submit FPS, named as such: it is what we handed to the presenter, not what
    // the panel scanned out. There is no "display FPS" because we cannot measure
    // one, and a number we did not measure must not be shown as if we had.
    if (!s.submissionFps) return tr("未测量");
    return tr("提交 %1").arg(*s.submissionFps, 0, 'f', 1);
}
bool QmlPlayerBridge::nrActive() const { return impl_->snapshot.nrActive; }
bool QmlPlayerBridge::nrAutoActive() const {return impl_->snapshot.nrAutoActive;}
int QmlPlayerBridge::nrAutoPercent() const {return int(impl_->snapshot.nrAutoPercent);}
QString QmlPlayerBridge::nrAutoStatus() const {
    const auto& s=impl_->snapshot;
    if(s.nrAutoActive)return tr("NR 自动 %1% · %2×%3").arg(s.nrAutoPercent).arg(s.nrAutoWidth).arg(s.nrAutoHeight);
    if(s.nrAutoStatus==L"自动NR实例池不可用，保留1080p NR")return tr("自动 NR 实例池不可用，保留固定 1080p 档");
    if(!s.nrAutoStatus.empty())return tr("当前配置未启用自动 NR，保留固定 1080p 档");
    return {};
}
bool QmlPlayerBridge::srActive() const { return impl_->snapshot.srActive; }
bool QmlPlayerBridge::fgActive() const { return impl_->snapshot.fgActive; }
bool QmlPlayerBridge::captureRecovering() const { return impl_->snapshot.captureRecovering; }
double QmlPlayerBridge::captureFps() const { return impl_->snapshot.captureFps; }
double QmlPlayerBridge::captureDropped() const { return double(impl_->snapshot.captureDropped); }
QString QmlPlayerBridge::backendWarning() const { return uiText(impl_->snapshot.backendWarning); }

// --- settings ---------------------------------------------------------------

#define VEYRA_BOOL_PROP(getter, setter, member)                              \
    bool QmlPlayerBridge::getter() const { return settings().member; }       \
    void QmlPlayerBridge::setter(bool value) {                               \
        auto s = settings();                                                 \
        if (s.member == value) return;                                       \
        s.member = value;                                                    \
        impl_->commit(std::move(s));                                         \
        emit settingsChanged();                                              \
    }
#define VEYRA_NUM_PROP(getter, setter, member, type)                         \
    type QmlPlayerBridge::getter() const { return type(settings().member); } \
    void QmlPlayerBridge::setter(type value) {                               \
        auto s = settings();                                                 \
        if (type(s.member) == value) return;                                 \
        s.member = value;                                                    \
        impl_->commit(std::move(s));                                         \
        emit settingsChanged();                                              \
    }

int QmlPlayerBridge::videoSrQuality() const { return int(settings().videoSrQuality); }
int QmlPlayerBridge::dlssSrModel() const { return int(settings().dlssSrModel); }
QVariantList QmlPlayerBridge::dlssSrModelChoices() const {
    return {QVariantMap{{"id", "0"}, {"label", "DLSS 4 · K"}},
            QVariantMap{{"id", "1"}, {"label", "DLSS 4.5 · M"}},
            QVariantMap{{"id", "2"}, {"label", "DLSS 4.5 · L"}}};
}
void QmlPlayerBridge::setDlssSrModel(int value) {
    if (value < 0 || unsigned(value) >= engine::kDlssSrModelCount) return;
    const auto cap = effectCapabilities().value("sr0").toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return; }
    auto s = settings();
    if (s.dlssSrModel == unsigned(value)) return;
    s.dlssSrModel = unsigned(value);
    if (impl_->commit(s)) emit settingsChanged();
}
void QmlPlayerBridge::setVideoSrQuality(int value) {
    if (value < 0 || value > 5) return;
    const auto cap = effectCapabilities().value("sr" + QString::number(value)).toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return; }
    auto s = settings();
    if (int(s.videoSrQuality) == value) return;
    s.videoSrQuality = unsigned(value);
    if (impl_->commit(s)) emit settingsChanged();
}
int QmlPlayerBridge::audioOffsetMs() const { return settings().audioOffsetMs; }
void QmlPlayerBridge::setAudioOffsetMs(int value) {
    auto s = settings();
    if (s.audioOffsetMs == value) return;
    s.audioOffsetMs = value;
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("audioOffsetMs")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "audio offset not saved");
    emit settingsChanged();
}
#undef VEYRA_BOOL_PROP
#undef VEYRA_NUM_PROP

// Stage-owned properties must change the authoritative chain first. commit()
// projects that chain back to settings, so setting only a flat mirror is a no-op.
bool QmlPlayerBridge::srEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::SuperResolution);
    return node && node->enabled;
}
void QmlPlayerBridge::setSrEnabled(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::SuperResolution) {
            setEffectEnabled(int(i), enabled); return;
        }
    if (enabled) emit notice(tr("请先添加超分节点"), true);
}
bool QmlPlayerBridge::videoHdr() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::VideoHdr);
    return node && node->enabled;
}
// Custom: static HDR output tuning. Globals, not chain nodes, and every field is
// live (the curve is read by the present blit, the metadata sent to the display),
// so a change never rebuilds the graph - but it does have to reach the session
// file, which has no other trigger for a global change.
int QmlPlayerBridge::hdrCurveStrength()const{return int(settings().hdrCurve.strengthPercent);}
void QmlPlayerBridge::setHdrCurveStrength(int percent){
    auto s=settings();
    const unsigned clamped=unsigned(std::clamp(percent,0,200));
    if(s.hdrCurve.strengthPercent==clamped)return;
    s.hdrCurve.strengthPercent=clamped;
    commitHdrTuning(std::move(s));
}
int QmlPlayerBridge::hdrDisplayPeakNits()const{return int(settings().hdrCurve.displayPeakNits);}
void QmlPlayerBridge::setHdrDisplayPeakNits(int nits){
    auto s=settings();
    // 0 = ask the system; anything else has to be a plausible panel peak.
    const int clamped=nits<=0?0:std::clamp(nits,100,10000);
    if(s.hdrCurve.displayPeakNits==unsigned(clamped))return;
    s.hdrCurve.displayPeakNits=unsigned(clamped);
    commitHdrTuning(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrDisplayInfo()const{
    // This is read from QML bindings (the peak row's hint text), so it must not do
    // real work per call: building a DXGI factory and enumerating every adapter and
    // output is far too heavy for something the snapshot signal can re-evaluate
    // every frame. Cache it for a few seconds instead.
    static QElapsedTimer cacheTimer;
    static gfx::PresentSink::DisplayPeakInfo cached;
    static HMONITOR cachedMonitor=nullptr;
    HWND window=impl_->videoWindow;
    if(!IsWindow(window)){
        if(auto* focused=QGuiApplication::focusWindow())window=reinterpret_cast<HWND>(focused->winId());
        else window=nullptr;
    }
    const auto monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    if(!cacheTimer.isValid()||cacheTimer.elapsed()>5000||cachedMonitor!=monitor){
        cached=gfx::PresentSink::queryDisplayPeak(window);cachedMonitor=monitor;
        cacheTimer.restart();
    }
    return {{"valid",cached.valid},{"plausible",cached.plausible},{"peakNits",int(cached.peakNits)},
            {"fullFrameNits",int(cached.fullFrameNits)},{"minNits",cached.minNits}};
}
bool QmlPlayerBridge::hdrCurve()const{return settings().hdrCurve.enabled;}
void QmlPlayerBridge::setHdrCurve(bool enabled){
    auto s=settings();if(s.hdrCurve.enabled==enabled)return;s.hdrCurve.enabled=enabled;
    commitHdrTuning(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrCurveParams()const{
    const auto& c=settings().hdrCurve;
    return {{"shadowLiftEv100",int(c.shadowLiftEv100)},{"shadowRangeNits",int(c.shadowRangeNits)},
            {"midGrayNits",int(c.midGrayNits)},{"highlightStartNits",int(c.highlightStartNits)},
            {"rollOffPercent",int(c.rollOffPercent)},{"peakCapNits",int(c.peakCapNits)}};
}
bool QmlPlayerBridge::setHdrCurveParameter(const QString& key,double value){
    if(!std::isfinite(value)||value<-10001||value>10001)return false;
    auto s=settings();const auto v=int(std::lround(value));
    if(key=="shadowLiftEv100"){if(v<-200||v>200)return false;s.hdrCurve.shadowLiftEv100=v;}
    else if(key=="shadowRangeNits"){if(v<1||v>100)return false;s.hdrCurve.shadowRangeNits=unsigned(v);}
    else if(key=="midGrayNits"){if(v<100||v>500)return false;s.hdrCurve.midGrayNits=unsigned(v);}
    else if(key=="highlightStartNits"){if(v<0||v>2000)return false;s.hdrCurve.highlightStartNits=unsigned(v);}
    else if(key=="rollOffPercent"){if(v<10||v>200)return false;s.hdrCurve.rollOffPercent=unsigned(v);}
    else if(key=="peakCapNits"){if(v<0||v>4000)return false;s.hdrCurve.peakCapNits=unsigned(v);}
    else return false;
    if(!s.hdrCurve.valid())return false;
    return commitHdrTuning(std::move(s));
}
bool QmlPlayerBridge::commitHdrTuning(engine::EnhancementSettings settings){
    const bool wasValid=chainValid();
    const auto curve=settings.hdrCurve;
    const auto metadata=settings.hdrMetadata;
    const bool doviFollow=settings.doviFollow;
    if(!impl_->commit(std::move(settings)))return false;
    // Re-project first, then re-assert both copies the session capture reads:
    // revalidate() rewrites the pending settings and the draft globals from the
    // runtime configuration, which does not carry this block yet.
    impl_->revalidate(true);
    auto after=impl_->facade.pendingSettings();
    after.hdrCurve=curve;after.hdrMetadata=metadata;after.doviFollow=doviFollow;
    impl_->facade.setPending(after);
    impl_->draftGlobals.hdrCurve=curve;impl_->draftGlobals.hdrMetadata=metadata;
    impl_->persistSession();
    if(!impl_->hdrRestoring&&impl_->hdrMemorySaveTimer){impl_->hdrMemoryDirty=true;impl_->hdrMemorySaveTimer->start();}
    emit settingsChanged();
    if(chainValid()!=wasValid)emitChainChanged();
    return true;
}
QString QmlPlayerBridge::hdrTuningPreset() const {
    const auto& s=settings();
    // hdrPresetMatches deliberately ignores the strength dial and the display peak:
    // they only scale the baseline, so turning them keeps you on the same preset.
    for(const auto& p:engine::kHdrTuningPresets)
        if(engine::hdrPresetMatches(s.hdrCurve,s.hdrMetadata,p))return QString::fromLatin1(p.id);
    return QStringLiteral("custom");
}
QString QmlPlayerBridge::hdrAppliedFrom() const {
  return impl_->prefString("hdrAppliedFrom")==QLatin1String("axes")?QStringLiteral("axes")
      :QStringLiteral("legacy");
}

void QmlPlayerBridge::setHdrAppliedFrom(const QString& from){
  const QString wanted=from==QLatin1String("axes")?QStringLiteral("axes"):QStringLiteral("legacy");
  if(impl_->prefString("hdrAppliedFrom")==wanted)return;
  impl_->prefs[QStringLiteral("hdrAppliedFrom")]=wanted;
  impl_->savePrefs();
  emit preferencesChanged();
}

QString QmlPlayerBridge::hdrPresetMode() const {
  return impl_->prefString("hdrPresetMode")==QLatin1String("axes")?QStringLiteral("axes")
                                                                :QStringLiteral("legacy");
}

void QmlPlayerBridge::setHdrPresetMode(const QString& mode){
  const QString wanted=mode==QLatin1String("axes")?QStringLiteral("axes"):QStringLiteral("legacy");
  if(impl_->prefString("hdrPresetMode")==wanted)return;
  impl_->prefs[QStringLiteral("hdrPresetMode")]=wanted;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr preset mode not saved");
  emit preferencesChanged();
}

QVariantMap QmlPlayerBridge::hdrAxes() const {
  auto environment=impl_->prefString("hdrAxesEnvironment");
  if(environment==QLatin1String("bright"))environment=QStringLiteral("compensate");
  else if(environment==QLatin1String("dark"))environment=QStringLiteral("plain");
  return {{"content",impl_->prefString("hdrAxesContent")},
          {"environment",environment}};
}

QVariantMap QmlPlayerBridge::hdrAxesMatch() const {
  // Which combination the curve currently equals. Strength and the display peak are
  // ignored, exactly like hdrPresetMatches: they only scale the baseline.
  const auto& c=settings().hdrCurve;
  for(const auto& content:engine::kHdrContentPresets)
    for(const auto* environment:engine::kHdrEnvironmentIds){
      const auto candidate=engine::hdrAxesCurve(content.id,environment);
      if(c.enabled==candidate.enabled&&c.shadowLiftEv100==candidate.shadowLiftEv100&&
         c.shadowRangeNits==candidate.shadowRangeNits&&c.midGrayNits==candidate.midGrayNits&&
         c.highlightStartNits==candidate.highlightStartNits&&c.rollOffPercent==candidate.rollOffPercent&&
         c.peakCapNits==candidate.peakCapNits)
        return {{"content",QString::fromLatin1(content.id)},{"environment",QString::fromLatin1(environment)}};
    }
  return {{"content",QString()},{"environment",QString()}};
}

QVariantMap QmlPlayerBridge::hdrCurveCoverage() const {
  const auto& snap=impl_->snapshot;
  return {{"valid",impl_->hdrCoverageObserved&&hdrCurve()&&snap.hdrCoverageValid},
          {"aboveCeiling",snap.hdrAboveCeilingPercent},
          {"rollOff",snap.hdrInRollOffPercent},
          {"lift",snap.hdrInLiftPercent}};
}

void QmlPlayerBridge::observeHdrCoverage(bool on){if(impl_->hdrCoverageObserved==on)return;impl_->hdrCoverageObserved=on;impl_->engine.observeHdrCoverage(on);emit snapshotChanged();}
QVariantMap QmlPlayerBridge::hdrCurveGraph() const {
  auto base=settings().hdrCurve;if(!base.displayPeakNits)base.displayPeakNits=effectiveHdrDisplayPeakNits();
  engine::HdrCurveSettings c;engine::HdrMetadataSettings metadata;
  engine::resolveHdrTuning(base,settings().hdrMetadata,c,metadata);
  QVariantList inputs,outputs;
  for(unsigned i=0;i<engine::kHdrCurveGraphPoints;++i){
    const double x=engine::hdrCurveGraphInputNits(i);
    inputs.append(x);
    outputs.append(c.enabled?engine::hdrCurveLuminance(c,x):x);
  }
  return {{"inputs",inputs},{"outputs",outputs},
          {"minNits",engine::kHdrCurveGraphMinNits},{"maxNits",engine::kHdrCurveGraphMaxNits},
          {"enabled",c.enabled}};
}

QVariantMap QmlPlayerBridge::hdrDials() const {
  const auto& c=settings().hdrCurve;
  return {{"shadowLift",int(engine::hdrShadowLiftLevel(c))},
          {"brightness",int(engine::hdrBrightnessLevel(c))},
          {"highlight",int(engine::hdrHighlightLevel(c))}};
}

bool QmlPlayerBridge::setHdrDial(const QString& key,int level){
  if(level<0)return false;
  auto s=settings();
  const unsigned value=unsigned(level);
  if(key==QLatin1String("shadowLift"))engine::setHdrShadowLiftLevel(s.hdrCurve,value);
  else if(key==QLatin1String("brightness"))engine::setHdrBrightnessLevel(s.hdrCurve,value);
  else if(key==QLatin1String("highlight"))engine::setHdrHighlightLevel(s.hdrCurve,value);
  else return false;
  return commitHdrTuning(std::move(s));
}

bool QmlPlayerBridge::applyHdrAxesPreset(const QString& content,const QString& environment){
  const QByteArray contentId=content.toUtf8();
  const QString normalizedEnvironment=environment==QLatin1String("bright")?QStringLiteral("compensate")
      :environment==QLatin1String("dark")?QStringLiteral("plain"):environment;
  const QByteArray environmentId=normalizedEnvironment.toUtf8();
  if(!engine::findHdrContentPreset(contentId.constData())||
      (normalizedEnvironment!=QLatin1String("compensate")&&normalizedEnvironment!=QLatin1String("plain")))return false;
  const auto curve=engine::hdrAxesCurve(contentId.constData(),environmentId.constData());
  auto s=settings();
  // The strength dial and the display peak belong to the user, not to the preset.
  const auto strength=s.hdrCurve.strengthPercent;
  const auto displayPeak=s.hdrCurve.displayPeakNits;
  s.hdrCurve=curve;
  s.hdrCurve.strengthPercent=strength;
  s.hdrCurve.displayPeakNits=displayPeak;
  if(!commitHdrTuning(std::move(s)))return false;
  setHdrAppliedFrom(QStringLiteral("axes"));
  impl_->prefs[QStringLiteral("hdrAxesContent")]=content;
  impl_->prefs[QStringLiteral("hdrAxesEnvironment")]=normalizedEnvironment;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr axes choice not saved");
  emit preferencesChanged();
  return true;
}

bool QmlPlayerBridge::hdrTitleMemory() const { return impl_->prefBool("hdrTitleMemory",true); }
bool QmlPlayerBridge::hdrAutoRecommend() const { return impl_->prefBool("hdrAutoRecommend",false); }
int QmlPlayerBridge::hdrTitleCount() const {
  return impl_->prefs.value(QStringLiteral("hdrTitles")).toList().size();
}

void QmlPlayerBridge::setHdrTitleMemory(bool on){
  if(impl_->prefBool("hdrTitleMemory",true)==on)return;
  impl_->prefs[QStringLiteral("hdrTitleMemory")]=on;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr title memory flag not saved");
  emit preferencesChanged();
}

void QmlPlayerBridge::setHdrAutoRecommend(bool on){
  if(impl_->prefBool("hdrAutoRecommend",false)==on)return;
  impl_->prefs[QStringLiteral("hdrAutoRecommend")]=on;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr auto recommend flag not saved");
  emit preferencesChanged();
}

void QmlPlayerBridge::forgetHdrTitles(){
  flushHdrMemory();
  impl_->prefs[QStringLiteral("hdrTitles")]=QVariantList{};
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr title profiles not cleared");
  emit preferencesChanged();
  emit notice(tr("已清除影片记忆"),false);
}

void QmlPlayerBridge::flushHdrMemory(){
  if(impl_->hdrMemorySaveTimer)impl_->hdrMemorySaveTimer->stop();
  if(impl_->hdrRestoring||!impl_->hdrMemoryDirty)return;
  impl_->hdrMemoryDirty=false;
  if(!impl_->hdrProfileDisplay.isEmpty())storeHdrProfile(impl_->hdrProfileDisplay);
  if(hdrTitleMemory()&&!impl_->hdrTitleApplied.isEmpty())storeHdrTitleProfile(impl_->hdrTitleApplied);
}
void QmlPlayerBridge::syncHdrTitleProfile(){
  auto& i=*impl_;
  if(!i.snapshot.running||i.openingSource)return;
  const QFileInfo file(QString::fromStdWString(i.sourceLabel));
  if(!file.isFile())return; // Capture/stream labels are not film identities.
  const QString title=file.canonicalFilePath();
  if(std::getenv("VEYRA_DOVI_TRACE")!=nullptr){
      static unsigned titleLogged=0;
      if(titleLogged<2){++titleLogged;
          veyra::log::info("hdr-title", std::format("canonical={}", title.toStdString()));}
  }
  if(title.isEmpty()||title==i.hdrTitleApplied)return;
  flushHdrMemory();i.hdrTitleApplied=title;
  if(hdrTitleMemory()&&loadHdrTitleProfile(title)){
    emit notice(tr("已套用这部影片上次的曲线设置"),false);return;
  }
  if(hdrAutoRecommend()&&settings().hdrCurve.enabled){QScopedValueRollback<bool> restoring(i.hdrRestoring,true);applyRecommendedHdrTuning();}
}

bool QmlPlayerBridge::loadHdrTitleProfile(const QString& title){
  const auto list=impl_->prefs.value(QStringLiteral("hdrTitles")).toList();
  // Path separators must not decide whether a memory matches: canonicalFilePath()
  // hands back '/', while entries written by older builds stored '\'.
  const auto normalized=[](QString v){v.replace(QLatin1Char('\\'),QLatin1Char('/'));return v;};
  const QString wanted=normalized(title);
  for(const auto& item:list){
    const auto stored=item.toMap();
    if(normalized(stored.value(QStringLiteral("key")).toString())!=wanted)continue;
    auto s=settings();
    const auto number=[&](const char* key,int fallback){
      return stored.contains(QLatin1String(key))?stored.value(QLatin1String(key)).toInt():fallback;};
    s.hdrCurve.enabled=stored.value(QStringLiteral("enabled"),s.hdrCurve.enabled).toBool();
    s.hdrCurve.shadowLiftEv100=number("shadowLiftEv100",int(s.hdrCurve.shadowLiftEv100));
    s.hdrCurve.shadowRangeNits=unsigned(std::clamp(number("shadowRangeNits",int(s.hdrCurve.shadowRangeNits)),1,100));
    s.hdrCurve.midGrayNits=unsigned(std::clamp(number("midGrayNits",int(s.hdrCurve.midGrayNits)),100,500));
    s.hdrCurve.highlightStartNits=unsigned(std::clamp(number("highlightStartNits",int(s.hdrCurve.highlightStartNits)),0,2000));
    s.hdrCurve.rollOffPercent=unsigned(std::clamp(number("rollOffPercent",int(s.hdrCurve.rollOffPercent)),10,200));
    s.hdrCurve.peakCapNits=unsigned(std::clamp(number("peakCapNits",int(s.hdrCurve.peakCapNits)),0,4000));
    s.hdrCurve.strengthPercent=unsigned(std::clamp(number("strengthPercent",int(s.hdrCurve.strengthPercent)),0,200));
    // Custom: the Dolby Vision follow belongs to the same per-film look, so it is
    // remembered with it. Entries written before it existed keep the current value.
    s.doviFollow=stored.value(QStringLiteral("doviFollow"),s.doviFollow).toBool();
        s.doviP7Compose=stored.value(QStringLiteral("doviP7Compose"),s.doviP7Compose).toBool();
    QScopedValueRollback<bool> restoring(impl_->hdrRestoring,true);
    return commitHdrTuning(std::move(s));
  }
  return false;
}

bool QmlPlayerBridge::storeHdrTitleProfile(const QString& title){
  if(title.isEmpty()||!hdrTitleMemory())return false;
  auto& i=*impl_;
  const auto& c=settings().hdrCurve;
  QVariantMap entry;
  entry[QStringLiteral("key")]=title;
  entry[QStringLiteral("enabled")]=c.enabled;
  entry[QStringLiteral("shadowLiftEv100")]=int(c.shadowLiftEv100);
  entry[QStringLiteral("shadowRangeNits")]=int(c.shadowRangeNits);
  entry[QStringLiteral("midGrayNits")]=int(c.midGrayNits);
  entry[QStringLiteral("highlightStartNits")]=int(c.highlightStartNits);
  entry[QStringLiteral("rollOffPercent")]=int(c.rollOffPercent);
  entry[QStringLiteral("peakCapNits")]=int(c.peakCapNits);
  entry[QStringLiteral("strengthPercent")]=int(c.strengthPercent);
  entry[QStringLiteral("doviFollow")]=doviFollow();
        entry[QStringLiteral("doviP7Compose")]=doviP7Compose();
  auto list=i.prefs.value(QStringLiteral("hdrTitles")).toList();
  const auto normalized=[](QString v){v.replace(QLatin1Char('\\'),QLatin1Char('/'));return v;};
  const QString wanted=normalized(title);
  for(const auto& item:list)if(normalized(item.toMap().value(QStringLiteral("key")).toString())==wanted&&item.toMap()==entry)return false;
  for(int n=list.size()-1;n>=0;--n)
    if(normalized(list[n].toMap().value(QStringLiteral("key")).toString())==wanted)list.removeAt(n);
  list.append(entry);
  // Bounded list: the oldest titles fall off the front.
  while(list.size()>i.kHdrTitleProfileLimit)list.removeFirst();
  i.prefs[QStringLiteral("hdrTitles")]=list;
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr title profile not saved");
  emit preferencesChanged();return true;
}

int QmlPlayerBridge::hdrDisplayProfileCount() const {
  return impl_->prefs.value(QStringLiteral("hdrProfiles")).toMap().size();
}

QString QmlPlayerBridge::hdrDisplayProfileId() const {
  // The EDID-based device id survives cable and port changes; the GDI device name
  // is the fallback for monitors that report none. Both are cheap Win32 calls -
  // no DXGI enumeration, so this stays safe on the snapshot cadence.
  HWND window=impl_->videoWindow;
  if(!IsWindow(window)){
    if(auto* focused=QGuiApplication::focusWindow())window=reinterpret_cast<HWND>(focused->winId());
    else window=nullptr;
  }
  const HMONITOR monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)
                               :MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
  if(!monitor)return QString();
  if(monitor==impl_->hdrIdentityMonitor&&!impl_->hdrIdentityKey.isEmpty())return impl_->hdrIdentityKey;
  impl_->hdrIdentityMonitor=monitor;
  MONITORINFOEXW info{};info.cbSize=sizeof(info);
  if(!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info)))return QString();
  DISPLAY_DEVICEW device{};device.cb=sizeof(device);
  if(EnumDisplayDevicesW(info.szDevice,0,&device,0)&&device.DeviceID[0])
    impl_->hdrIdentityKey=QString::fromWCharArray(device.DeviceID);
  else impl_->hdrIdentityKey=QString::fromWCharArray(info.szDevice);
  return impl_->hdrIdentityKey;
}

void QmlPlayerBridge::forgetHdrDisplayProfiles(){
  flushHdrMemory();
  auto& i=*impl_;
  i.prefs[QStringLiteral("hdrProfiles")]=QVariantMap{};
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr display profiles not cleared");
  emit preferencesChanged();
  emit notice(tr("已清除显示设备记忆"),false);
}

void QmlPlayerBridge::syncHdrDisplayProfile(){
  auto& i=*impl_;
  const QString id=hdrDisplayProfileId();
  if(id.isEmpty()||id==i.hdrProfileDisplay)return;   // the common path: nothing changed
  // Leaving a display: remember what the curve looks like on it.
  flushHdrMemory();
  i.hdrProfileDisplay=id;
  emit settingsChanged();
  if(loadHdrProfile(id)){
    emit preferencesChanged();
    emit notice(tr("已套用该显示设备的 HDR 曲线配置"),false);
  }
}

bool QmlPlayerBridge::loadHdrProfile(const QString& id){
  auto& i=*impl_;
  const auto profiles=i.prefs.value(QStringLiteral("hdrProfiles")).toMap();
  const auto stored=profiles.value(id).toMap();
  if(stored.isEmpty())return false;
  auto s=settings();
  const auto number=[&](const char* key,int fallback){
    return stored.contains(QLatin1String(key))?stored.value(QLatin1String(key)).toInt():fallback;};
  s.hdrCurve.enabled=stored.value(QStringLiteral("enabled"),s.hdrCurve.enabled).toBool();
  s.hdrCurve.shadowLiftEv100=number("shadowLiftEv100",int(s.hdrCurve.shadowLiftEv100));
  s.hdrCurve.shadowRangeNits=unsigned(std::clamp(number("shadowRangeNits",int(s.hdrCurve.shadowRangeNits)),1,100));
  s.hdrCurve.midGrayNits=unsigned(std::clamp(number("midGrayNits",int(s.hdrCurve.midGrayNits)),100,500));
  s.hdrCurve.highlightStartNits=unsigned(std::clamp(number("highlightStartNits",int(s.hdrCurve.highlightStartNits)),0,2000));
  s.hdrCurve.rollOffPercent=unsigned(std::clamp(number("rollOffPercent",int(s.hdrCurve.rollOffPercent)),10,200));
  s.hdrCurve.peakCapNits=unsigned(std::clamp(number("peakCapNits",int(s.hdrCurve.peakCapNits)),0,4000));
  s.hdrCurve.strengthPercent=unsigned(std::clamp(number("strengthPercent",int(s.hdrCurve.strengthPercent)),0,200));
  s.hdrCurve.displayPeakNits=unsigned(std::clamp(number("displayPeakNits",int(s.hdrCurve.displayPeakNits)),0,10000));
  s.doviFollow=stored.value(QStringLiteral("doviFollow"),s.doviFollow).toBool();
        s.doviP7Compose=stored.value(QStringLiteral("doviP7Compose"),s.doviP7Compose).toBool();
  QScopedValueRollback<bool> restoring(i.hdrRestoring,true);
  return commitHdrTuning(std::move(s));
}

bool QmlPlayerBridge::storeHdrProfile(const QString& id){
  auto& i=*impl_;
  const auto& c=settings().hdrCurve;
  QVariantMap entry;
  entry[QStringLiteral("enabled")]=c.enabled;
  entry[QStringLiteral("shadowLiftEv100")]=int(c.shadowLiftEv100);
  entry[QStringLiteral("shadowRangeNits")]=int(c.shadowRangeNits);
  entry[QStringLiteral("midGrayNits")]=int(c.midGrayNits);
  entry[QStringLiteral("highlightStartNits")]=int(c.highlightStartNits);
  entry[QStringLiteral("rollOffPercent")]=int(c.rollOffPercent);
  entry[QStringLiteral("peakCapNits")]=int(c.peakCapNits);
  entry[QStringLiteral("strengthPercent")]=int(c.strengthPercent);
  entry[QStringLiteral("displayPeakNits")]=int(c.displayPeakNits);
  entry[QStringLiteral("doviFollow")]=doviFollow();
        entry[QStringLiteral("doviP7Compose")]=doviP7Compose();
  auto profiles=i.prefs.value(QStringLiteral("hdrProfiles")).toMap();
  if(profiles.value(id).toMap()==entry)return false;   // nothing new to remember
  profiles[id]=entry;
  while(profiles.size()>32)profiles.erase(profiles.begin());
  i.prefs[QStringLiteral("hdrProfiles")]=profiles;
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr display profile not saved");
  emit preferencesChanged();return true;
}

QVariantMap QmlPlayerBridge::hdrSourceInfo() const {
  const auto& snap=impl_->snapshot;
  const unsigned mastering=snap.sourceMasteringPeakNits;
  const unsigned cll=snap.sourceMaxCllNits,fall=snap.sourceMaxFallNits;
  const bool valid=snap.sourceHdr&&(mastering||cll||fall);
  return {{"valid",valid},{"masteringPeakNits",int(mastering)},
          {"maxCllNits",int(cll)},{"maxFallNits",int(fall)},
            {"doviProfile",int(snap.sourceDoviProfile)},
            {"doviRpu",snap.sourceDoviRpu},
            {"doviMaxNits",int(snap.sourceDoviMaxNits)},
          {"doviEnhancement",snap.sourceDoviEnhancement}};
}

bool QmlPlayerBridge::sourceIsSdr() const {
  const auto& snap=impl_->snapshot;
  return snap.running&&!snap.sourceHdr;
}

unsigned QmlPlayerBridge::effectiveHdrDisplayPeakNits() const {
  // The ceiling the panel is actually held to: the user's own value when set, else the
  // system report - but only when that report is plausible (a projector answering
  // 10000 nit with a 0.000 black is its EDID ceiling, not a measurement; the presenter
  // applies the same rule). 0 means unknown and the recommendation skips that branch.
  const unsigned setting=unsigned(std::max(0,hdrDisplayPeakNits()));
  if(setting)return setting;
  const auto info=hdrDisplayInfo();
  if(!info.value("valid").toBool()||!info.value("plausible").toBool())return 0;
  return unsigned(std::max(0,info.value("peakNits").toInt()));
}

QVariantMap QmlPlayerBridge::hdrRecommendation() const {
  const auto& snap=impl_->snapshot;
  const auto rec=engine::recommendHdrTuning(snap.sourceMasteringPeakNits,snap.sourceMaxCllNits,
      snap.sourceMaxFallNits,effectiveHdrDisplayPeakNits());
  return {{"available",rec.available},{"presetId",QString::fromLatin1(rec.presetId)},
          {"axesContent",QString::fromLatin1(engine::hdrAxesContentForPreset(rec.presetId))},
          {"reason",QString::fromLatin1(rec.reasonKey)},{"strengthPercent",int(rec.strengthPercent)},
          {"cllNits",int(rec.cllNits)},{"fallNits",int(rec.fallNits)}};
}

QVariantMap QmlPlayerBridge::hdrPeakWarning() const {
  const auto& snap=impl_->snapshot;
  const auto& s=settings();
  // The arithmetic (including the divide-by-zero guard) lives in the header so the
  // unit tests cover it; this only marshals it to QML.
  const unsigned sourcePeak=snap.sourceMaxCllNits?snap.sourceMaxCllNits:snap.sourceMasteringPeakNits;
  auto baseline=s.hdrCurve;if(!baseline.displayPeakNits)baseline.displayPeakNits=effectiveHdrDisplayPeakNits();
  const auto w=engine::hdrPeakWarning(sourcePeak,snap.sourceHdr,baseline.enabled,baseline,s.hdrMetadata);
  return {{"valid",w.valid},{"sourcePeakNits",int(w.sourcePeakNits)},{"capNits",int(w.capNits)},
          {"keptPercent",int(w.keptPercent)},{"clipped",w.clipped}};
}

bool QmlPlayerBridge::applyRecommendedHdrTuning(){
  const auto& snap=impl_->snapshot;
  const auto rec=engine::recommendHdrTuning(snap.sourceMasteringPeakNits,snap.sourceMaxCllNits,
      snap.sourceMaxFallNits,effectiveHdrDisplayPeakNits());
  if(!rec.available)return false;
  // The finding is the same in both modes; only the thing it names differs. In the
  // axes mode the content comes from the source and the environment stays the user's
  // choice, exactly like a manual pick there.
  if(hdrPresetMode()==QLatin1String("axes")){
    QString environment=impl_->prefString("hdrAxesEnvironment");
    if(environment.isEmpty())environment=QStringLiteral("compensate");
    const QString content=QString::fromLatin1(engine::hdrAxesContentForPreset(rec.presetId));
    if(!applyHdrAxesPreset(content,environment))return false;
    emit notice(tr("已按片源自动选档：%1").arg(content),false);
    return true;
  }
  const auto* preset=engine::findHdrTuningPreset(rec.presetId);
  if(!preset)return false;
  auto s=settings();
  // The strength dial and the display peak belong to the user: the axes pick
  // preserves them, so a recommendation must not reset them either.
  const auto strength=s.hdrCurve.strengthPercent;
  const auto displayPeak=s.hdrCurve.displayPeakNits;
  s.hdrCurve=preset->curve;s.hdrMetadata=preset->metadata;
  s.hdrCurve.strengthPercent=strength;
  s.hdrCurve.displayPeakNits=displayPeak;
  if(!commitHdrTuning(std::move(s)))return false;
  setHdrAppliedFrom(QStringLiteral("legacy"));
  // One line per application, for diagnostics: it records what the recommendation decided

  // and on which numbers, so an odd suggestion can be explained afterwards. Per-user

  // calibration was considered and dropped - thresholds tuned on one person film library

  // would be shipped to everyone else.
  veyra::log::info("hdr-recommend",std::format("applied preset={} reason={} cll={} fall={} mastering={} displayPeak={} mode={}",
      rec.presetId,rec.reasonKey,rec.cllNits,rec.fallNits,snap.sourceMasteringPeakNits,
      effectiveHdrDisplayPeakNits(),hdrPresetMode().toStdString()));
  emit notice(tr("已按片源自动选档：%1").arg(QString::fromLatin1(rec.presetId)),false);
  return true;
}

bool QmlPlayerBridge::applyHdrTuningPreset(const QString& id){
    for(const auto& p:engine::kHdrTuningPresets){
        if(id!=QLatin1String(p.id))continue;
        auto s=settings();
        // Picking a preset replaces the baseline, not the two dials: how hard the
        // user turned it (and which panel they told us about) must survive.
        const auto strength=s.hdrCurve.strengthPercent;
        const auto displayPeak=s.hdrCurve.displayPeakNits;
        s.hdrCurve=p.curve;s.hdrMetadata=p.metadata;
        s.hdrCurve.strengthPercent=strength;s.hdrCurve.displayPeakNits=displayPeak;
        if(!s.hdrCurve.valid()||!s.hdrMetadata.valid())return false;
        if(!commitHdrTuning(std::move(s)))return false;
        setHdrAppliedFrom(QStringLiteral("legacy"));
        return true;
    }
    return false;
}
bool QmlPlayerBridge::doviP7Compose()const{return settings().doviP7Compose;}
void QmlPlayerBridge::setDoviP7Compose(bool on){
    auto s=settings();if(s.doviP7Compose==on)return;s.doviP7Compose=on;
    // Lives with the shell preference like the Level 1 follow flag, so it survives
    // a restart and applies before the next file is opened.
    auto& i=*impl_;
    i.prefs[QStringLiteral("doviP7Compose")]=on;
    if(!i.savePrefs())veyra::log::warn("ui-prefs","dolby vision p7 compose flag not saved");
    commitHdrTuning(std::move(s));
}
bool QmlPlayerBridge::doviFollow()const{return settings().doviFollow;}
bool QmlPlayerBridge::doviFollowAverage()const{return settings().doviFollowAverage;}
void QmlPlayerBridge::setDoviFollowAverage(bool on){
    auto s=settings();if(s.doviFollowAverage==on)return;s.doviFollowAverage=on;
    commitHdrTuning(std::move(s));}
void QmlPlayerBridge::setDoviFollow(bool on){
    auto s=settings();if(s.doviFollow==on)return;s.doviFollow=on;
    // The shell preference is the global fallback: it re-applies at the next start
    // even for a source that has no stored per-film profile yet.
    auto& i=*impl_;
    i.prefs[QStringLiteral("doviFollow")]=on;
    if(!i.savePrefs())veyra::log::warn("ui-prefs","dolby vision follow flag not saved");
    commitHdrTuning(std::move(s));
}
void QmlPlayerBridge::setVideoHdr(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            setEffectEnabled(int(i), enabled); return;
        }
    if (enabled) emit notice(tr("请先添加 RTX Video HDR 节点"), true);
}
// RTX Video HDR tuning lives on the chain's HDR node, like NR parameters on
// NR nodes: the same values AppShell's settings sliders edited.
namespace {
// Spatial anti-aliasing (#53): one node's values for the editors, and the edits they send.
QString antiAliasSummary(const engine::AntiAliasSettings& a) {
    using engine::AntiAliasMethod;
    if (a.method == AntiAliasMethod::Mlaa)
        return QStringLiteral("MLAA · ") + QmlPlayerBridge::tr("阈值 %1").arg(double(a.mlaaThreshold), 0, 'f', 2);
    const QStringList fxaa{QmlPlayerBridge::tr("中"), QmlPlayerBridge::tr("高"), QmlPlayerBridge::tr("极高")};
    const QStringList smaa{QmlPlayerBridge::tr("低"), QmlPlayerBridge::tr("中"), QmlPlayerBridge::tr("高"), QmlPlayerBridge::tr("极高")};
    QString text = a.method == AntiAliasMethod::Fxaa ? QStringLiteral("FXAA · ") + fxaa.value(int(a.quality))
                                                     : QStringLiteral("SMAA · ") + smaa.value(int(a.quality));
    if (a.usesTemporal())
        text += a.temporal == engine::AntiAliasTemporal::T2x ? QmlPlayerBridge::tr(" · 时域 T2x（实验）") : QmlPlayerBridge::tr(" · 时域 4x（实验）");
    return text;
}
QVariantMap antiAliasMap(const engine::ChainNode* node, int index) {
    const engine::AntiAliasSettings a = node ? node->aa : engine::AntiAliasSettings{};
    return {{"index", index}, {"present", node != nullptr}, {"enabled", node && node->enabled},
            {"method", int(a.method)}, {"quality", int(a.quality)},
            {"mlaaThreshold", double(a.mlaaThreshold)}, {"mlaaStrength", double(a.mlaaStrength)},
            {"temporal", int(a.temporal)}, {"historyWeight", double(a.historyWeight)},
            {"historyRejection", double(a.historyRejection)}, {"summary", antiAliasSummary(a)}};
}
// A method change starts from its high tier; the experimental modes keep Magpie's
// history defaults (T2x 0.5, 4x 0.75) unless the weight was tuned.
uint8_t defaultAntiAliasQuality(engine::AntiAliasMethod method) {
    return method == engine::AntiAliasMethod::Fxaa ? 1 : method == engine::AntiAliasMethod::Smaa ? 2 : 0;
}
float defaultHistoryWeight(engine::AntiAliasTemporal temporal) {
    return temporal == engine::AntiAliasTemporal::X4 ? 0.75f : 0.5f;
}
bool applyAntiAliasKey(engine::AntiAliasSettings& a, bool& enabled, const QString& key, double value) {
    using engine::AntiAliasMethod; using engine::AntiAliasTemporal;
    if (!std::isfinite(value)) return false;
    const int whole = int(std::lround(value));
    if (key == "enabled") { enabled = value != 0; return true; }
    if (key == "method") {
        if (whole < 0 || whole > 2) return false;
        const auto method = AntiAliasMethod(whole);
        if (method != a.method) { a.method = method; a.quality = defaultAntiAliasQuality(method); }
        if (method != AntiAliasMethod::Smaa) a.temporal = AntiAliasTemporal::Off;
        return true;
    }
    if (key == "quality") {
        if (whole < 0 || whole >= int(engine::antiAliasQualityCount(a.method))) return false;
        a.quality = uint8_t(whole); return true;
    }
    if (key == "temporal") {
        if (whole < 0 || whole > 2 || (whole && a.method != AntiAliasMethod::Smaa)) return false;
        const auto next = AntiAliasTemporal(whole);
        if (next != a.temporal && a.historyWeight == defaultHistoryWeight(a.temporal)) a.historyWeight = defaultHistoryWeight(next);
        a.temporal = next; return true;
    }
    if (key == "mlaaThreshold") { a.mlaaThreshold = float(value); return true; }
    if (key == "mlaaStrength") { a.mlaaStrength = float(value); return true; }
    if (key == "historyWeight") { a.historyWeight = float(value); return true; }
    if (key == "historyRejection") { a.historyRejection = float(value); return true; }
    return false;
}
}
bool QmlPlayerBridge::antiAliasEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::AntiAliasing);
    return node && node->enabled;
}
void QmlPlayerBridge::setAntiAliasEnabled(bool enabled) {
    if (enabled != antiAliasEnabled()) (void)setAntiAliasParameter(QStringLiteral("enabled"), enabled ? 1 : 0);
}
QVariantMap QmlPlayerBridge::antiAliasParams() const {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::AntiAliasing) return antiAliasMap(&impl_->chain.nodes[i], int(i));
    return antiAliasMap(nullptr, -1);
}
QVariantMap QmlPlayerBridge::antiAliasParamsAt(int index) const {
    if (index < 0 || uint32_t(index) >= impl_->chain.nodeCount ||
        impl_->chain.nodes[index].type != engine::EffectType::AntiAliasing) return antiAliasMap(nullptr, -1);
    return antiAliasMap(&impl_->chain.nodes[index], index);
}
bool QmlPlayerBridge::setAntiAliasParameter(const QString& key, double value) {
    auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i)
        if (c.nodes[i].type == engine::EffectType::AntiAliasing) return setAntiAliasParameterAt(int(i), key, value);
    if (c.mode != engine::ChainMode::List) { emit notice(tr("请先添加抗锯齿节点"), true); return false; }
    // The list card's instance exists once it is used: after the grades, in
    // front of SR, where toChain() puts it.
    engine::ChainNode node; node.type = engine::EffectType::AntiAliasing;
    bool enabled = false;
    if (!applyAntiAliasKey(node.aa, enabled, key, value)) return false;
    node.enabled = node.aa.enabled = enabled;
    if (!node.aa.validate().empty()) return false;
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return false; }
    uint32_t insertAt = 0;
    while (insertAt < c.nodeCount && c.nodes[insertAt].type == engine::EffectType::Color) ++insertAt;
    const auto before = std::make_unique<engine::EffectChain>(c);
    const int selected = impl_->selectedNr, colourSelected = impl_->selectedColour;
    for (uint32_t i = c.nodeCount; i > insertAt; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[insertAt] = node; ++c.nodeCount;
    if (impl_->selectedNr >= int(insertAt)) ++impl_->selectedNr;
    if (impl_->selectedColour >= int(insertAt)) ++impl_->selectedColour;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        c = *before; impl_->selectedNr = selected; impl_->selectedColour = colourSelected; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::setAntiAliasParameterAt(int index, const QString& key, double value) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::AntiAliasing) return false;
    auto& node = c.nodes[index];
    const auto before = node; const bool wasValid = chainValid();
    bool enabled = node.enabled;
    if (!applyAntiAliasKey(node.aa, enabled, key, value)) return false;
    node.enabled = node.aa.enabled = enabled;
    if (!node.aa.validate().empty()) { node = before; return false; }
    if (node == before) return true;
    const bool accepted = impl_->revalidate(true);
    if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
    emit settingsChanged();
    // The switch also strikes the chip through; parameters notify only their group.
    if (before.enabled != node.enabled) emitChainChanged();
    else emitParameterChanged(&QmlPlayerBridge::antiAliasChanged, wasValid);
    return accepted;
}
namespace {
// Beauty nodes: one node's values for the editors, the edits they send and the presets.
QString beautySummary(const engine::BeautySettings& b, bool enabled) {
    if (!enabled) return QmlPlayerBridge::tr("已关闭");
    QStringList parts;
    if (b.restore) parts << QmlPlayerBridge::tr("原画保护 %1%").arg(std::lround(b.restoreStrength * 100.0f));
    if (b.smooth) parts << QmlPlayerBridge::tr("磨皮 %1%").arg(std::lround(b.smoothAmount * 100.0f));
    return parts.isEmpty() ? QmlPlayerBridge::tr("未启用子功能") : parts.join(QStringLiteral(" · "));
}
QVariantMap beautyMap(const engine::ChainNode* node, int index, int layer) {
    const engine::BeautySettings b = node ? node->beauty : engine::BeautySettings{};
    return {{"index", index}, {"layer", layer}, {"present", node != nullptr}, {"enabled", node && node->enabled},
            {"restore", b.restore}, {"restoreStrength", double(b.restoreStrength)}, {"restoreScope", int(b.restoreScope)},
            {"regionExpand", double(b.regionExpand)}, {"feather", double(b.feather)},
            {"smooth", b.smooth}, {"strength", double(b.strength)}, {"smoothAmount", double(b.smoothAmount)},
            {"radius", double(b.radius)}, {"detail", double(b.detail)}, {"texture", double(b.texture)},
            {"brighten", double(b.brighten)}, {"scope", int(b.scope)},
            {"hueCenter", double(b.hueCenter)}, {"hueRange", double(b.hueRange)}, {"satMin", double(b.satMin)},
            {"satMax", double(b.satMax)}, {"lumaMin", double(b.lumaMin)}, {"lumaMax", double(b.lumaMax)},
            {"softness", double(b.softness)}, {"minConfidence", double(b.minConfidence)}, {"showMask", b.showMask},
            {"summary", beautySummary(b, node && node->enabled)}};
}
bool applyBeautyKey(engine::BeautySettings& b, bool& enabled, const QString& key, double value) {
    if (!std::isfinite(value)) return false;
    const int whole = int(std::lround(value));
    if (key == "enabled") { enabled = value != 0; return true; }
    if (key == "restore") { b.restore = value != 0; return true; }
    if (key == "smooth") { b.smooth = value != 0; return true; }
    if (key == "showMask") { b.showMask = value != 0; return true; }
    if (key == "restoreScope") { if (whole < 0 || whole > 1) return false; b.restoreScope = engine::BeautyRestoreScope(whole); return true; }
    if (key == "scope") { if (whole < 0 || whole > 1) return false; b.scope = engine::BeautyScope(whole); return true; }
    const std::pair<const char*, float engine::BeautySettings::*> floats[] = {
        {"restoreStrength", &engine::BeautySettings::restoreStrength}, {"regionExpand", &engine::BeautySettings::regionExpand},
        {"feather", &engine::BeautySettings::feather}, {"strength", &engine::BeautySettings::strength},
        {"smoothAmount", &engine::BeautySettings::smoothAmount}, {"radius", &engine::BeautySettings::radius},
        {"detail", &engine::BeautySettings::detail}, {"texture", &engine::BeautySettings::texture},
        {"brighten", &engine::BeautySettings::brighten}, {"hueCenter", &engine::BeautySettings::hueCenter},
        {"hueRange", &engine::BeautySettings::hueRange}, {"satMin", &engine::BeautySettings::satMin},
        {"satMax", &engine::BeautySettings::satMax}, {"lumaMin", &engine::BeautySettings::lumaMin},
        {"lumaMax", &engine::BeautySettings::lumaMax}, {"softness", &engine::BeautySettings::softness},
        {"minConfidence", &engine::BeautySettings::minConfidence}};
    for (const auto& [name, field] : floats)
        if (key == QLatin1String(name)) { b.*field = float(value); return true; }
    return false;
}
// Starting points, tuned on lit photos and dark game footage; each names the
// sub-switches it changes and leaves the skin range and the detection alone.
bool applyBeautyPresetValues(engine::BeautySettings& b, const QString& preset) {
    const auto natural = [&] { b.smooth = true; b.strength = 1.0f; b.smoothAmount = 0.6f; b.radius = 5.0f; b.detail = 0.6f; b.texture = 0.3f; };
    if (preset == "natural") { natural(); b.restore = false; return true; }
    if (preset == "light") {
        b.smooth = true; b.restore = false; b.strength = 1.0f; b.smoothAmount = 0.35f; b.radius = 3.0f; b.detail = 0.75f; b.texture = 0.4f;
        return true;
    }
    if (preset == "restore") { b.restore = true; b.restoreStrength = 1.0f; b.smooth = false; return true; }
    if (preset == "restoreNatural") { b.restore = true; b.restoreStrength = 1.0f; natural(); return true; }
    return false;
}
}
QVariantList QmlPlayerBridge::beautyLayers() const {
    QVariantList out;
    int layer = 0;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::Beauty) out << beautyMap(&impl_->chain.nodes[i], int(i), layer++);
    return out;
}
QVariantMap QmlPlayerBridge::beautyDetection() const {
    const auto& d = impl_->chain.beautyDetection;
    return {{"quality", int(d.quality)}, {"maxFaces", int(d.maxFaces)}, {"stability", double(d.stability)}};
}
QString QmlPlayerBridge::beautyStatus() const {
    const auto& s = impl_->snapshot;
    if (!s.beautyActive) return {};
    if (!s.beautyNotice.empty()) return uiText(s.beautyNotice);
    return s.beautyFaces ? tr("检测到 %1 张人脸").arg(s.beautyFaces) : tr("未检测到人脸");
}
QVariantMap QmlPlayerBridge::beautyParamsAt(int index) const {
    const auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return beautyMap(nullptr, -1, -1);
    int layer = 0;
    for (int i = 0; i < index; ++i) layer += c.nodes[i].type == engine::EffectType::Beauty;
    return beautyMap(&c.nodes[index], index, layer);
}
bool QmlPlayerBridge::setBeautyParameterAt(int index, const QString& key, double value) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node; const bool wasValid = chainValid();
    bool enabled = node.enabled;
    if (!applyBeautyKey(node.beauty, enabled, key, value)) return false;
    node.enabled = node.beauty.enabled = enabled;
    if (!node.beauty.validate().empty()) { node = before; return false; }
    if (node == before) return true;
    const bool accepted = impl_->revalidate(true);
    if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
    emit settingsChanged();
    // The switch also strikes the chip through; parameters notify only their group.
    if (before.enabled != node.enabled) emitChainChanged();
    else emitParameterChanged(&QmlPlayerBridge::beautyChanged, wasValid);
    return accepted;
}
int QmlPlayerBridge::duplicateBeautyLayer(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return -1;
    if (c.mode != engine::ChainMode::List) { emit notice(tr("节点模式请用节点的复制"), true); return -1; }
    if (c.countOf(engine::EffectType::Beauty) >= engine::kMaxBeautyInstances) {
        emit notice(tr("%1 最多 %2 个").arg(tr("美颜")).arg(engine::kMaxBeautyInstances), true); return -1;
    }
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return -1; }
    const auto before = std::make_unique<engine::EffectChain>(c);
    const int selected = impl_->selectedNr, colourSelected = impl_->selectedColour;
    const uint32_t at = uint32_t(index) + 1;
    for (uint32_t i = c.nodeCount; i > at; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[at] = c.nodes[index]; ++c.nodeCount;
    if (impl_->selectedNr >= int(at)) ++impl_->selectedNr;
    if (impl_->selectedColour >= int(at)) ++impl_->selectedColour;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        c = *before; impl_->selectedNr = selected; impl_->selectedColour = colourSelected; impl_->revalidate();
        emit notice(uiText(error), true); return -1;
    }
    emit settingsChanged(); emitChainChanged();
    return int(at);
}
bool QmlPlayerBridge::moveBeautyLayer(int index, int delta) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty || !delta) return false;
    int other = index;
    do other += delta < 0 ? -1 : 1;
    while (other >= 0 && uint32_t(other) < c.nodeCount && c.nodes[other].type != engine::EffectType::Beauty);
    if (other < 0 || uint32_t(other) >= c.nodeCount) return false;
    const auto before = std::make_unique<engine::EffectChain>(c);
    const auto viewA = std::pair{c.nodes[index].viewX, c.nodes[index].viewY};
    const auto viewB = std::pair{c.nodes[other].viewX, c.nodes[other].viewY};
    std::swap(c.nodes[index], c.nodes[other]);
    // Node mode keeps the canvas positions with the slot; only the parameters trade places.
    c.nodes[index].viewX = viewA.first; c.nodes[index].viewY = viewA.second;
    c.nodes[other].viewX = viewB.first; c.nodes[other].viewY = viewB.second;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c = *before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::applyBeautyPreset(int index, const QString& preset) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node;
    if (!applyBeautyPresetValues(node.beauty, preset)) return false;
    node.enabled = node.beauty.enabled = true;
    if (!node.beauty.validate().empty()) { node = before; return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::setBeautyDetection(const QString& key, double value) {
    auto& c = impl_->chain;
    if (!std::isfinite(value)) return false;
    auto next = c.beautyDetection;
    const int whole = int(std::lround(value));
    if (key == "quality") next.quality = uint8_t(std::clamp(whole, 0, 2));
    else if (key == "maxFaces") next.maxFaces = uint8_t(std::clamp(whole, 1, 8));
    else if (key == "stability") next.stability = float(value);
    else return false;
    if (!next.validate().empty()) return false;
    if (next == c.beautyDetection) return true;
    const auto before = c.beautyDetection; const bool wasValid = chainValid();
    c.beautyDetection = next;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c.beautyDetection = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged();
    emitParameterChanged(&QmlPlayerBridge::beautyChanged, wasValid);
    return true;
}
bool QmlPlayerBridge::beautyAnyEnabled() const {
    const auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i) if (c.nodes[i].type == engine::EffectType::Beauty && c.nodes[i].enabled) return true;
    return false;
}
bool QmlPlayerBridge::setAllBeautyEnabled(bool enabled) {
    auto& c = impl_->chain;
    const auto before = std::make_unique<engine::EffectChain>(c);
    std::vector<uint32_t> layers;
    for (uint32_t i = 0; i < c.nodeCount; ++i) if (c.nodes[i].type == engine::EffectType::Beauty) layers.push_back(i);
    if (layers.empty()) return false;
    auto& restore = impl_->beautyMasterRestore;
    if (!enabled) {
        restore.clear();
        for (const auto i : layers) { restore.push_back(c.nodes[i].enabled); c.nodes[i].enabled = c.nodes[i].beauty.enabled = false; }
    } else {
        // As the NR switch: layers added or removed meanwhile bring every layer back.
        const bool faithful = restore.size() == layers.size() && std::find(restore.begin(), restore.end(), true) != restore.end();
        for (size_t k = 0; k < layers.size(); ++k) c.nodes[layers[k]].enabled = c.nodes[layers[k]].beauty.enabled = faithful ? bool(restore[k]) : true;
    }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c = *before; impl_->revalidate();
        emit notice(uiText(error), true); emitChainChanged(); return false;
    }
    veyra::log::info("ui-beauty", std::format("group switch {} layers={}", enabled ? "on" : "off", layers.size()));
    emitChainChanged(); emit settingsChanged();
    return true;
}
bool QmlPlayerBridge::resetBeautyLayer(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node;
    node.beauty = engine::BeautySettings{};
    node.beauty.enabled = node.enabled;
    if (node == before) return true;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
QVariantMap QmlPlayerBridge::videoHdrParams() const {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            const auto& v = impl_->chain.nodes[i].videoHdr;
            return {{"contrast", int(v.contrast)}, {"saturation", int(v.saturation)},
                    {"middleGray", int(v.middleGray)}, {"peakNits", int(v.peakNits)}};
        }
    return {};
}
bool QmlPlayerBridge::setVideoHdrParameter(const QString& key, double value) {
    if (!std::isfinite(value)) return false;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i) {
        auto& node = impl_->chain.nodes[i];
        if (node.type != engine::EffectType::VideoHdr) continue;
        const auto before = node; const bool wasValid = chainValid();
        if (value < 0) return false;
        const auto v = unsigned(std::lround(value));
        if (key == "contrast") node.videoHdr.contrast = v;
        else if (key == "saturation") node.videoHdr.saturation = v;
        else if (key == "middleGray") node.videoHdr.middleGray = v;
        else if (key == "peakNits") node.videoHdr.peakNits = v;
        else return false;
        if (!node.videoHdr.valid()) { node = before; return false; }
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged(); emitParameterChanged(&QmlPlayerBridge::videoHdrParamsChanged, wasValid); return accepted;
    }
    emit notice(tr("请先添加 RTX Video HDR 节点"), true);
    return false;
}
bool QmlPlayerBridge::fgEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    return node && node->enabled;
}
void QmlPlayerBridge::setFgEnabled(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::FrameGeneration) {
            if (impl_->chain.nodes[i].enabled == enabled) return;
            // Turning on keeps the last chosen multiplier (at least 2X).
            if (enabled && impl_->chain.fgMultiplier < 2) impl_->chain.fgMultiplier = 2;
            setEffectEnabled(int(i), enabled); emit settingsChanged(); return;
        }
    if (enabled) emit notice(tr("请先添加补帧节点"), true);
}
int QmlPlayerBridge::fgMultiplier() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    // 档位只有数字：自动选倍数由独立开关控制，不再占用倍率档位（2026-10-24）。
    return node && node->enabled ? int(impl_->chain.fgMultiplier) : 1;
}
void QmlPlayerBridge::setFgMultiplier(int multiplier) {
    // 倍率档只有数字（1 = 关闭，>=2 = 补帧倍率）。自动选择是独立开关，
    // 不再占用 0 这个值；收到 0 说明调用方过旧，忽略即可。
    if (multiplier <= 0) return;
    applyFgMultiplier(multiplier);
}

// 设倍率的实际动作，不碰 fgAutoMultiplier 标记。自动决策与手动选择共用。
void QmlPlayerBridge::applyFgMultiplier(int multiplier) {
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    const bool legal = std::find(choices.begin(),choices.end(),uint32_t(std::max(multiplier,0)))!=choices.end();
    if (!legal || multiplier > fgMaxMultiplier()) {
        emit notice(tr("补帧倍率超出当前后端范围"), true); return;
    }
    auto& chain = impl_->chain;
    auto* node = chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { if (multiplier > 1) emit notice(tr("请先添加补帧节点"), true); return; }
    const bool enabled = multiplier > 1;
    if (node->enabled == enabled && (!enabled || chain.fgMultiplier == uint32_t(multiplier))) return;
    const auto beforeMultiplier = chain.fgMultiplier; const auto beforeEnabled = node->enabled;
    node->enabled = enabled; if (enabled) chain.fgMultiplier = uint32_t(multiplier);
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        node->enabled = beforeEnabled; chain.fgMultiplier = beforeMultiplier; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged(); emitChainChanged();
}

bool QmlPlayerBridge::colorEnabled() const {return selectedColourSettings().enabled;}
void QmlPlayerBridge::setColorEnabled(bool enabled){
    auto c=selectedColourSettings();c.enabled=enabled;commitColour(c);
}

bool QmlPlayerBridge::protectionEnabled() const { return settings().protection.enabled; }
void QmlPlayerBridge::setProtectionEnabled(bool enabled) {
    if (impl_->chain.mode != engine::ChainMode::List) {
        emit notice(tr("NR 保护区域仅在列表模式可用"), true); return;
    }
    for (uint32_t i=0; i<impl_->chain.nodeCount; ++i) {
        if (impl_->chain.nodes[i].type==engine::EffectType::Protection) {
            setEffectEnabled(int(i), enabled);
            return;
        }
    }
    emit notice(tr("处理链中没有 NR 保护节点，未更改设置"), true);
}

QVariantMap QmlPlayerBridge::protectionState() const {
    const auto p = settings().protection;
    const auto r = p.regions[0];
    return {{"left",double(r.left)},{"top",double(r.top)},
            {"right",double(r.right)},{"bottom",double(r.bottom)},
            {"feather",double(p.featherPixels)}};
}

bool QmlPlayerBridge::setProtectionRegion(double left,double top,double right,double bottom,double feather) {
    if (impl_->chain.mode != engine::ChainMode::List) return false;
    for (double v : {left,top,right,bottom,feather}) if (!std::isfinite(v)) return false;
    if (left<0 || top<0 || right>1 || bottom>1 || left>right || top>bottom || feather<0 || feather>64) return false;
    for (uint32_t i=0;i<impl_->chain.nodeCount;++i) {
        auto& node=impl_->chain.nodes[i];
        if (node.type!=engine::EffectType::Protection) continue;
        const auto before=node;const bool wasValid=chainValid();
        // Explicit edits replace legacy multi-rectangle settings with the one global region.
        node.protection.regions={};
        node.protection.regions[0]={float(left),float(top),float(right),float(bottom)};
        node.protection.featherPixels=float(feather);
        if(node==before)return true;
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emitParameterChanged(&QmlPlayerBridge::protectionChanged,wasValid);return accepted;
    }
    return false;
}

// The inspector edits the selected list node, not the first flat NR mirror.
#define VEYRA_NR_PROP(getter,setter,member,type)                              \
    type QmlPlayerBridge::getter() const {                                  \
        const auto* n=impl_->nrNode(); const engine::ChainNode fallback{};    \
        return type((n?*n:fallback).member);                                 \
    }                                                                      \
    void QmlPlayerBridge::setter(type value){                               \
        auto* n=impl_->nrNode(); if(!n||type(n->member)==value)return;        \
        const auto before=*n; const bool wasValid=chainValid();             \
        n->member=decltype(n->member)(value);                                \
        if(!impl_->revalidate(true)){                                       \
            const auto error=impl_->validation.message; *n=before;          \
            impl_->revalidate();emit notice(uiText(error),true);             \
        }                                                                  \
        emit settingsChanged();                                              \
        if(before.enabled!=n->enabled)emitChainChanged();                    \
        else emitParameterChanged(&QmlPlayerBridge::nrLayersChanged,wasValid); \
    }
VEYRA_NR_PROP(nrEnabled,setNrEnabled,enabled,bool)
VEYRA_NR_PROP(nrTemporal,setNrTemporal,nr.temporal,bool)
VEYRA_NR_PROP(nrIntensity,setNrIntensity,nr.model.intensity,double)
VEYRA_NR_PROP(nrTone,setNrTone,nr.model.tone,double)
VEYRA_NR_PROP(nrStructure,setNrStructure,nr.model.structure,double)
VEYRA_NR_PROP(nrSkin,setNrSkin,nr.model.skin,double)
VEYRA_NR_PROP(nrAutoMask,setNrAutoMask,nr.model.autoMask,bool)
VEYRA_NR_PROP(nrUiCorrection,setNrUiCorrection,nr.model.uiCorrection,bool)
VEYRA_NR_PROP(nrStyle,setNrStyle,nr.model.style,int)
#undef VEYRA_NR_PROP
int QmlPlayerBridge::selectedNrLayer() const{return impl_->nrIndex();}
void QmlPlayerBridge::setSelectedNrLayer(int index){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::NrEnhance||index==impl_->selectedNr)return;
    impl_->selectedNr=index;emit settingsChanged();
}
bool QmlPlayerBridge::lowLatency() const{return settings().lowLatency;}
void QmlPlayerBridge::setLowLatency(bool value){
    const auto before=impl_->chain;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i)if(impl_->chain.nodes[i].type==engine::EffectType::NrEnhance)
        impl_->chain.nodes[i].nr.lowLatencyPairing=value;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;impl_->chain=before;
        impl_->revalidate();emit notice(uiText(error),true);
    }
    emit settingsChanged();emitChainChanged();
}

// Stage-5 output stabiliser (anti-flicker). Global, not per-node: it runs once,
// after the whole NR stack and the residual composite. Strength 0 is the
// default and the graph then skips the dispatch entirely, so committing it
// never costs anything until the user turns it on.
double QmlPlayerBridge::nrHoldStrength() const { return double(settings().nrHoldStrength); }
void QmlPlayerBridge::setNrHoldStrength(double value) {
    auto s = settings();
    const float next = float(value);
    if (s.nrHoldStrength == next) return;
    s.nrHoldStrength = next;
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("nrHoldStrength")] = double(next);
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "output stabiliser strength not saved");
    emit settingsChanged();
}
double QmlPlayerBridge::nrHoldTolerance() const { return double(settings().nrHoldTolerance); }
void QmlPlayerBridge::setNrHoldTolerance(double value) {
    auto s = settings();
    const float next = float(value);
    if (s.nrHoldTolerance == next) return;
    s.nrHoldTolerance = next;
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("nrHoldTolerance")] = double(next);
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "output stabiliser tolerance not saved");
    emit settingsChanged();
}

bool QmlPlayerBridge::applying() const { return impl_->facade.applying(); }

// Volume and mute are engine state (the audio clock owns them), so they go
// straight to the engine and come back through the snapshot: the UI shows what
// the engine did, not what it asked for.
double QmlPlayerBridge::volume() const { return double(impl_->snapshot.volume); }
void QmlPlayerBridge::setVolume(double value) {
    impl_->engine.setVolume(float(std::clamp(value, 0.0, 1.0)), impl_->snapshot.muted);
}
bool QmlPlayerBridge::muted() const { return impl_->snapshot.muted; }
void QmlPlayerBridge::setMuted(bool value) {
    impl_->engine.setVolume(impl_->snapshot.volume, value);
}
double QmlPlayerBridge::playbackSpeed() const { return impl_->snapshot.playbackSpeed; }
double QmlPlayerBridge::playbackRate() const { return impl_->snapshot.playbackRate; }
double QmlPlayerBridge::minimumPlaybackRate() const { return engine::kMinPlaybackRate; }
double QmlPlayerBridge::maximumPlaybackRate() const { return engine::kMaxPlaybackRate; }
void QmlPlayerBridge::setPlaybackRate(double rate) {
    if(!impl_->engine.setPlaybackRate(rate))return;
    impl_->snapshot.playbackRate=rate;emit snapshotChanged();
}

// --- chain ------------------------------------------------------------------
// The settings the UI is showing: the facade's pending copy, which the engine
// has been asked to apply. One accessor so every property reads the same value.
engine::EnhancementSettings QmlPlayerBridge::settings() const {
    auto result = impl_->facade.pendingSettings();
    if (impl_->chain.mode == engine::ChainMode::Node) impl_->draftGlobals.apply(result);
    return result;
}

QVariantList QmlPlayerBridge::chain() const {
    QVariantList out;
    int nrOrdinal=0;
    const auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i) {
        const auto& node = c.nodes[i];
        const auto& info = engine::effectInfo(node.type);
        QVariantMap item;
        item["index"] = int(i);
        item["id"] = c.mode == engine::ChainMode::Node ? impl_->layout.ids[i] : i + 2;
        item["type"] = utf8Of(info.id);
        item["label"] = uiText(info.label);
        if(node.type==engine::EffectType::FrameGeneration)
            for(const auto& choice:fgBackendChoices()){
                const auto option=choice.toMap();
                if(option.value("id").toString()==fgBackendName())item["label"]=option.value("label");
            }
        if(node.type==engine::EffectType::NrEnhance)item["label"]=tr("NR 画面增强 · 第 %1 层").arg(++nrOrdinal);
        item["enabled"] = node.enabled;
        item["x"] = double(node.viewX);
        item["y"] = double(node.viewY);
        item["mustBeLast"] = info.mustBeLast;
        item["justBeforeLast"] = info.justBeforeLast;
        item["experimental"] = info.experimental;
        item["changesResolution"] = info.changesResolution;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::nrLayers() const {
    QVariantList out;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i){const auto& node=impl_->chain.nodes[i];
        if(node.type!=engine::EffectType::NrEnhance)continue;
        const auto& n=node.nr;
        out<<QVariantMap{{"index",int(i)},{"enabled",node.enabled},{"sizePolicy",int(n.sizePolicy)},{"runtime",int(engine::currentNrRuntime(n.runtime))},
            {"intensity",n.model.intensity},{"tone",n.model.tone},{"structure",n.model.structure},
            {"skin",n.model.skin},{"style",n.model.style},{"autoMask",bool(n.model.autoMask)},
            {"uiCorrection",bool(n.model.uiCorrection)},{"performance",n.model.performance},{"total",n.residual.total},
            {"darken",n.residual.darken},{"brighten",n.residual.brighten},
            {"color",n.residual.color},{"luminance",n.residual.luminance},{"temporal",n.temporal},
            {"antiFlicker",int(n.antiFlicker)},
            {"correctionEnabled",n.residual.correction.enabled},{"correctionAuto",n.residual.correction.automatic},
            {"hueProtection",n.residual.correction.hue},{"chromaProtection",n.residual.correction.chroma},
            {"highlightProtection",n.residual.correction.highlight},{"localCompression",n.residual.correction.compression},
            {"temporalStability",n.residual.correction.stability},
            {"neutralProtection",n.residual.correction.neutral},{"colorRetention",n.residual.correction.colorKeep},
            {"luminanceRetention",n.residual.correction.lumaKeep},{"shadowProtection",n.residual.correction.shadow},
            {"correctionAmount",n.residual.correction.autoAmount}};
    }
    return out;
}

QString QmlPlayerBridge::nrAutoSelectionReason(int index) const {
    const auto cap=effectCapabilities().value("nrAuto").toMap();
    if(!cap.value("available").toBool())return cap.value("reason").toString();
    const auto& s=settings();const auto& snapshot=impl_->snapshot;
    int first=-1;unsigned count=0;bool temporal=false,otherAuto=false;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i){const auto& node=impl_->chain.nodes[i];
        if(node.type!=engine::EffectType::NrEnhance)continue;
        if(first<0)first=int(i);
        ++count;temporal|=node.enabled&&(node.nr.residual.correction.enabled?
            node.nr.residual.correction.usesHistory():node.nr.temporal);
        otherAuto|=int(i)!=index&&node.enabled&&node.nr.sizePolicy==pipeline::NrSizePolicy::Auto;
    }
    if(first!=index||count>2||otherAuto)return tr("自动 NR 仅支持一至两层列表的第一层");
    if(impl_->chain.mode!=engine::ChainMode::List||engine::currentNrRuntime(s.nrRuntime)!=engine::NrRuntime::Ampere||s.sr||s.multiplier>1||temporal||
       s.nrHoldStrength>0||s.color.enabled||s.additionalColorCount||s.antiAlias.enabled||s.additionalAntiAliasCount||s.beautyEnabled()||s.videoHdr.enabled||
       s.opticalFlowBackend!=engine::OpticalFlowBackend::Nvidia||s.nrMotion!=engine::MotionSource::OpticalFlow||snapshot.capture||snapshot.image||snapshot.sourceHdr||
       (snapshot.sourceWidth&& (snapshot.sourceWidth!=1920||snapshot.sourceHeight!=1080)))
        return tr("需 1080p SDR 文件、SF 和 NVIDIA 光流；关闭超分、补帧、抗闪烁、调色、美颜及 HDR");
    return {};
}

bool QmlPlayerBridge::setNrLayerParameter(int index,const QString& key,double value){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::NrEnhance||!std::isfinite(value))return false;
    if(key=="runtime") {
        if(value!=0&&value!=1&&value!=2&&value!=3&&value!=4)return false;
        value=int(engine::currentNrRuntime(static_cast<engine::NrRuntime>(int(value))));
        const auto cap = effectCapabilities().value("nr" + QString::number(int(value))).toMap();
        if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return false; }
        const auto before=impl_->chain;
        for(uint32_t i=0;i<impl_->chain.nodeCount;++i)
            if(impl_->chain.nodes[i].type==engine::EffectType::NrEnhance)
                impl_->chain.nodes[i].nr.runtime=static_cast<engine::NrRuntime>(int(value));
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;impl_->chain=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emitChainChanged();return accepted;
    }
    auto& node=impl_->chain.nodes[index];const auto before=node;auto& n=node.nr;const bool wasValid=chainValid();
    if(key=="intensity")n.model.intensity=float(value);
    else if(key=="tone")n.model.tone=float(value);
    else if(key=="structure")n.model.structure=float(value);
    else if(key=="skin")n.model.skin=float(value);
    else if(key=="total")n.residual.total=float(value);
    else if(key=="darken")n.residual.darken=float(value);
    else if(key=="brighten")n.residual.brighten=float(value);
    else if(key=="color")n.residual.color=float(value);
    else if(key=="luminance")n.residual.luminance=float(value);
    else if(key=="hueProtection")n.residual.correction.hue=float(value);
    else if(key=="chromaProtection")n.residual.correction.chroma=float(value);
    else if(key=="highlightProtection")n.residual.correction.highlight=float(value);
    else if(key=="localCompression")n.residual.correction.compression=float(value);
    else if(key=="temporalStability")n.residual.correction.stability=float(value);
    else if(key=="neutralProtection")n.residual.correction.neutral=float(value);
    else if(key=="colorRetention")n.residual.correction.colorKeep=float(value);
    else if(key=="luminanceRetention")n.residual.correction.lumaKeep=float(value);
    else if(key=="shadowProtection")n.residual.correction.shadow=float(value);
    else if(key=="correctionAmount")n.residual.correction.autoAmount=float(value);
    else {
        if(value!=std::floor(value)||value<0||value>6)return false;
        if(key=="sizePolicy"){
            if(value==6){const auto reason=nrAutoSelectionReason(index);if(!reason.isEmpty()){emit notice(reason,true);return false;}}
            n.sizePolicy=static_cast<pipeline::NrSizePolicy>(int(value));
        }
        else if(key=="style"&&value<=2)n.model.style=int(value);
        else if(key=="autoMask"&&value<=1)n.model.autoMask=int(value);
        else if(key=="uiCorrection"&&value<=1)n.model.uiCorrection=int(value);
        else if(key=="performance"&&value<=1&&engine::nrUsesPerformanceModel(n.runtime))n.model.performance=value!=0;
        else if(key=="temporal"&&value<=1)n.temporal=value!=0;
        else if(key=="correctionEnabled"&&value<=1)n.residual.correction.enabled=value!=0;
        else if(key=="correctionAuto"&&value<=1)n.residual.correction.automatic=value!=0;
        else if(key=="correctionUseAuto"&&value==1)n.residual.correction.useAutomaticValues(n.model.style);
        else if(key=="antiFlicker"&&validNrAntiFlicker(engine::NrAntiFlicker(int(value))))n.antiFlicker=static_cast<engine::NrAntiFlicker>(int(value));
        else return false;
    }
    if(node==before)return true;
    const bool accepted=impl_->revalidate(true);
    if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
    emit settingsChanged();emitParameterChanged(&QmlPlayerBridge::nrLayersChanged,wasValid);return accepted;
}

QVariantList QmlPlayerBridge::effectCatalog() const {
    QVariantList out;
    for (const auto& info : engine::effectCatalog()) {
        if (info.hidden) continue;
        if (impl_->chain.mode == engine::ChainMode::Node && info.type == engine::EffectType::Protection) continue;
        if (impl_->chain.mode == engine::ChainMode::Node && info.type == engine::EffectType::FrameGeneration) {
            for(const auto& choice : fgBackendChoices()) {
                const auto option=choice.toMap();const auto backend=option.value("id").toString();
                QVariantMap fg;
                fg["id"]=backend+QStringLiteral("-fg");
                fg["label"]=option.value("label");
                fg["maxInstances"]=1;fg["repeatable"]=false;fg["mustBeLast"]=true;
                fg["justBeforeLast"]=false;fg["experimental"]=backend!=QLatin1String("dlss");
                fg["disabled"]=option.value("disabled");fg["note"]=option.value("note");
                out<<fg;
            }
            continue;
        }
        QVariantMap item;
        item["id"] = utf8Of(info.id);
        item["label"] = uiText(info.label);
        item["maxInstances"] = int(info.maxInstances);
        item["repeatable"] = info.repeatable;
        item["mustBeLast"] = info.mustBeLast;
        item["justBeforeLast"] = info.justBeforeLast;
        item["experimental"] = info.experimental;
        auto cap = effectAvailability(utf8Of(info.id));
        if(info.type==engine::EffectType::FrameGeneration)
            for(const auto& choice:fgBackendChoices())if(!choice.toMap().value("disabled").toBool()){
                cap=QVariantMap{{"available",true},{"reason",QString{}}};break;
            }
        item["disabled"] = !cap.value("available").toBool();
        item["note"] = cap.value("reason");
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::chainError() const { return uiText(impl_->validation.message); }
void QmlPlayerBridge::emitChainChanged() {
    emit chainChanged(); emit nrLayersChanged(); emit videoHdrParamsChanged();
    emit antiAliasChanged(); emit beautyChanged(); emit colourParamsChanged(); emit protectionChanged();
}
// Every chainChanged re-evaluated all chain-bound bindings on every page (the pro
// and node pages stay loaded) and repainted the node wires: 65-80 ms of interface
// thread per 40 ms slider step with six effects (audit 2026-10-15).
void QmlPlayerBridge::emitParameterChanged(void (QmlPlayerBridge::*group)(), bool wasValid) {
    if (chainValid() != wasValid) { emitChainChanged(); return; }
    (this->*group)();
}
bool QmlPlayerBridge::chainValid() const { return impl_->validation.accepted; }
int QmlPlayerBridge::nodeMode() const { return impl_->chain.mode == engine::ChainMode::Node ? 1 : 0; }
void QmlPlayerBridge::setNodeMode(int mode) {
    if (mode < 0 || mode > 1) { emit notice(tr("无效编辑模式"), true); return; }
    const auto want = mode ? engine::ChainMode::Node : engine::ChainMode::List;
    if (impl_->chain.mode == want) return;
    const auto previousSession = impl_->currentSession();
    auto candidate = previousSession;
    const bool freshNode = want == engine::ChainMode::Node && !candidate.initialized[size_t(want)];
    if (!candidate.select(want)) { emit notice(tr("模式配置无效，未切换"), true); return; }
    if (freshNode) {
        // The first visit to the node editor starts from an empty graph (input
        // straight to output), not a copy of the list's effects.
        auto& fresh = candidate.configurations[size_t(want)];
        auto empty = std::make_unique<engine::EffectChain>();
        empty->mode = engine::ChainMode::Node;
        empty->fgMultiplier = fresh.chain.fgMultiplier;
        fresh.chain = *empty;
        fresh.editor.reset();
        fresh.selectedNr = fresh.selectedColour = -1;
    }
    const auto previousSettings = impl_->facade.pendingSettings();
    auto next = previousSettings;
    auto& configuration = candidate.configurations[size_t(want)];
    disableUnavailableEffects(configuration);
    configuration.apply(next);
    if (!next.validate().empty()) { emit notice(tr("模式参数无效，未切换"), true); return; }
    // Save before changing the live mode; an unwritable/corrupt file cannot
    // silently discard the configuration the user is about to leave.
    if (!impl_->facade.saveChainSession(candidate)) { emit notice(uiText(impl_->facade.error()), true); return; }
    if (!impl_->facade.applySettings(next,engine::runtimeOrder(configuration.chain))) {
        impl_->facade.setPending(previousSettings);
        if (!impl_->facade.saveChainSession(previousSession))
            emit notice(tr("引擎拒绝切换，存档回退失败：") + uiText(impl_->facade.error()), true);
        emit notice(tr("引擎未接受模式设置，当前配置不变"), true); return;
    }
    impl_->session = impl_->savedSession = candidate;
    impl_->restoreConfiguration(configuration);
    impl_->revalidate();
    impl_->options = engine::PlayerOptions::from(next,engine::runtimeOrder(impl_->activeRuntime()),&impl_->options);
    emitChainChanged();
    emit settingsChanged();
}

QVariantList QmlPlayerBridge::recentFiles() const {
    QVariantList out;
    for (const auto& entry : impl_->facade.recentFiles()) {
        QVariantMap item;
        item["path"] = utf8Of(entry.path);
        item["label"] = utf8Of(entry.label);
        item["exists"] = entry.exists;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::presets() const {
    QVariantList out;
    const auto& entries = impl_->facade.presets().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        QVariantMap item;
        item["index"] = int(i);
        item["name"] = utf8Of(e.name);
        item["note"] = uiText(e.note);
        item["builtin"] = e.builtin;
        item["nodeMode"] = e.kind == engine::ChainMode::Node;
        item["contents"] = int(e.contents);
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::currentPresetName() const {
    // Matching is by name of the stored entry whose chain equals what the UI is
    // showing; anything else is honestly "自定义" rather than the nearest guess.
    const auto& entries = impl_->facade.presets().entries();
    for (const auto& e : entries) {
        if (e.chain == impl_->chain) return utf8Of(e.name);
    }
    return tr("自定义");
}

QVariantList QmlPlayerBridge::audioTracks() const {
    QVariantList out;
    for (const auto& track : impl_->snapshot.audioTracks) {
        // The track struct carries raw fields; the UI shows the ones that are
        // actually present rather than a synthesized name.
        QStringList parts;
        if (!track.language.empty()) parts << utf8Of(track.language);
        if (!track.title.empty()) parts << utf8Of(track.title);
        if (!track.codec.empty()) parts << utf8Of(track.codec);
        QVariantMap item;
        item["index"] = track.streamIndex;
        item["label"] = parts.isEmpty() ? tr("音轨 %1").arg(track.streamIndex) : parts.join(QStringLiteral(" · "));
        item["channels"] = int(track.channels);
        out << item;
    }
    return out;
}
int QmlPlayerBridge::selectedAudioTrack() const { return impl_->snapshot.selectedAudioTrack; }
void QmlPlayerBridge::setSelectedAudioTrack(int index) {
    impl_->engine.selectAudioTrack(impl_->snapshot.sessionId, index);
}
bool QmlPlayerBridge::hasCaptureSession() const { return impl_->facade.hasCaptureSession(); }

QString QmlPlayerBridge::captureSessionSummary() const {
    // Built from the recorded session only: the device, the format and the preset
    // that were actually used. A summary assembled from defaults would read as a
    // history that never happened.
    const auto& session = impl_->facade.lastCaptureSession();
    QStringList parts;
    // The device's friendly name (stored when the session started); the moniker
    // path itself is not something a person can read.
    const QString name = impl_->prefString("captureDeviceName");
    if (!name.isEmpty()) parts << name;
    else if (!session.devicePath.empty()) parts << QFileInfo(utf8Of(session.devicePath)).fileName();
    const QString formatLabel = impl_->prefString("captureFormatLabel");
    if (!formatLabel.isEmpty()) parts << formatLabel.section(QStringLiteral(" · "), 0, 1);
    else if (!session.formatKey.empty()) parts << utf8Of(session.formatKey);
    if (formatLabel.isEmpty() && session.fps > 0.01) parts << QStringLiteral("%1 fps").arg(session.fps, 0, 'f', 2);
    if (!session.presetName.empty()) parts << tr("预设「%1」").arg(utf8Of(session.presetName));
    return parts.join(QStringLiteral(" · "));
}

void QmlPlayerBridge::resumeCaptureSession() {
    // "Continue": the remembered device, format, audio and colour, re-checked
    // against what the machine reports now; nothing else is picked for the user.
    impl_->capturePrefs = ui::CapturePreferenceStore(impl_->dataDir).load();
    if (impl_->capturePrefs.videoPath.empty()) { emit notice(tr("还没有采集记录"), true); return; }
    impl_->captureDevice = impl_->capturePrefs.videoPath;
    impl_->captureColor = impl_->capturePrefs.colorForDevice(impl_->captureDevice);
    impl_->captureResumePending = true;
    queryCapture(2, impl_->captureDevice);
}

QString QmlPlayerBridge::colorStatus() const { return uiText(impl_->snapshot.colorStatus); }
QString QmlPlayerBridge::videoHdrStatus() const { return uiText(impl_->snapshot.videoHdrStatus); }

// --- export -----------------------------------------------------------------
// Poll the export job once per UI tick. Its snapshot is the source of the
// numbers below; nothing here keeps a parallel counter that could drift.
void QmlPlayerBridge::pollExport() {
    const auto& queue=impl_->exportQueue.queue();
    const bool changed=impl_->exportQueue.tick();
    impl_->exportSnapshot=queue.snapshot();
    const auto event=queue.successEvent();
    if(event&&event!=impl_->exportNotifiedBatch){
        impl_->exportNotifiedBatch=event;
        exportCompletionNotice(exportCompletionSound(),event,"video-batch");
        emit notice(uiText(queue.status()),false);
    }
    if(changed)emit exportChanged();
}

bool QmlPlayerBridge::exportRunning() const { return impl_->exportQueue.queue().busy(); }
bool QmlPlayerBridge::exportPaused() const {
    return impl_->exportSnapshot.state == engine::ExportState::Paused;
}
double QmlPlayerBridge::exportProgress() const { return impl_->exportSnapshot.progress; }
QString QmlPlayerBridge::exportStatus() const {
    if(impl_->exportSnapshot.active())return uiText(impl_->exportSnapshot.message);
    return uiText(impl_->exportQueue.queue().status());
}
QString QmlPlayerBridge::exportTarget() const { return utf8Of(impl_->exportOutput); }
int QmlPlayerBridge::exportEncoded() const { return int(impl_->exportSnapshot.encoded); }
int QmlPlayerBridge::exportGenerated() const { return int(impl_->exportSnapshot.generated); }
double QmlPlayerBridge::exportEtaSeconds() const { return impl_->exportSnapshot.etaSeconds; }
int QmlPlayerBridge::exportQueueCount() const { return int(std::count_if(impl_->exportQueue.queue().items().begin(),impl_->exportQueue.queue().items().end(),[](const auto& i){return i.state==engine::ExportItemState::Queued;})); }
int QmlPlayerBridge::exportQueueFailures() const { return int(impl_->exportQueue.queue().failures()); }
QString QmlPlayerBridge::exportQueueFailure() const { return uiText(impl_->exportSnapshot.lastQueueFailure); }

// HEVC or H.264: the engine's export entry takes this as a flag, so it is a real
// choice with a real effect rather than a label.
bool QmlPlayerBridge::exportHevc() const { return impl_->exportHevc; }
void QmlPlayerBridge::setExportHevc(bool value) {
    if (impl_->exportHevc == value) return;
    impl_->exportHevc = value;
    emit exportChanged();
}

// Export bitrate: 0 means the encoder's constant-quality default, which is what
// the engine's exportBitrateMbps field documents.
int QmlPlayerBridge::exportBitrateMbps() const { return int(impl_->exportBitrateMbps); }
void QmlPlayerBridge::setExportBitrateMbps(int value) {
    const uint32_t want = uint32_t(std::clamp(value, 0, 300));
    if (impl_->exportBitrateMbps == want) return;
    impl_->exportBitrateMbps = want;
    emit exportChanged();
}

int QmlPlayerBridge::exportRateControl() const { return int(impl_->exportRateControl); }
void QmlPlayerBridge::setExportRateControl(int value) {
    if (value < 0 || value > 2) return;
    const auto want = static_cast<sink::ExportRateControl>(value);
    if (impl_->exportRateControl == want) return;
    impl_->exportRateControl = want;
    emit exportChanged();
}

QString QmlPlayerBridge::exportPresetName() const {
    return impl_->exportPresetName.empty() ? tr("当前播放设置") : utf8Of(impl_->exportPresetName);
}

int QmlPlayerBridge::exportSrTargetIndex() const {
    return impl_->exportResolutionIndex;
}
void QmlPlayerBridge::setExportSrTargetIndex(int index) {
    if (index < -1 || index > 6 || impl_->exportResolutionIndex == index) return;
    impl_->exportResolutionIndex = index;
    emit exportChanged();
}

double QmlPlayerBridge::exportTrimStart() const { return impl_->exportTrimStartSeconds; }
void QmlPlayerBridge::setExportTrimStart(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double duration = impl_->snapshot.duration;
    const double end = impl_->exportTrimEndSeconds > 0.0 ? impl_->exportTrimEndSeconds : duration;
    const double maxStart = end > 0.05 ? end - 0.05 : 0.0;
    const double value = std::clamp(seconds, 0.0, std::max(0.0, maxStart));
    if (std::abs(impl_->exportTrimStartSeconds - value) < 0.0005) return;
    const auto selected=impl_->exportQueue.selectedId();
    if(selected&&!impl_->exportQueue.queue().setTrim(selected,value,impl_->exportTrimEndSeconds))return;
    impl_->exportTrimStartSeconds = value;
    emit exportChanged();
}

double QmlPlayerBridge::exportTrimEnd() const { return impl_->exportTrimEndSeconds; }
void QmlPlayerBridge::setExportTrimEnd(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double duration = impl_->snapshot.duration;
    const double sourceEnd = duration > 0.0 ? duration : seconds;
    const double value = std::clamp(seconds, 0.0, std::max(0.0, sourceEnd));
    const double start = impl_->exportTrimStartSeconds;
    if (value > 0.0 && value <= start + 0.05) return;
    if (std::abs(impl_->exportTrimEndSeconds - value) < 0.0005) return;
    const auto selected=impl_->exportQueue.selectedId();
    if(selected&&!impl_->exportQueue.queue().setTrim(selected,impl_->exportTrimStartSeconds,value))return;
    impl_->exportTrimEndSeconds = value;
    emit exportChanged();
}

QString QmlPlayerBridge::srTargetLabel() const {
    // The label follows the SR target, because that is what sets the output size.
    switch (srTargetIndex()) {
    case 1: return QStringLiteral("2K");
    case 2: return QStringLiteral("4K");
    case 3: return QStringLiteral("8K");
    case 4: return QStringLiteral("5K");
    case 5: return QStringLiteral("6K");
    case 6: return QStringLiteral("7K");
    default: return tr("源尺寸");
    }
}

int QmlPlayerBridge::srTargetIndex() const {
    // UI IDs 1/2/3 remain 2K/4K/8K; append 4/5/6 for 5K/6K/7K.
    switch (settings().srTarget) {
    case pipeline::SrTarget::Qhd: return 1;
    case pipeline::SrTarget::Uhd4K: return 2;
    case pipeline::SrTarget::Uhd8K: return 3;
    case pipeline::SrTarget::Uhd5K: return 4;
    case pipeline::SrTarget::Uhd6K: return 5;
    case pipeline::SrTarget::Uhd7K: return 6;
    }
    return 0;
}
void QmlPlayerBridge::setSrTargetIndex(int index) {
    if(index<1||index>6)return;
    auto s = settings();
    pipeline::SrTarget want = pipeline::SrTarget::Uhd4K;
    switch (index) {
    case 1: want = pipeline::SrTarget::Qhd; break;
    case 2: want = pipeline::SrTarget::Uhd4K; break;
    case 3: want = pipeline::SrTarget::Uhd8K; break;
    case 4: want = pipeline::SrTarget::Uhd5K; break;
    case 5: want = pipeline::SrTarget::Uhd6K; break;
    case 6: want = pipeline::SrTarget::Uhd7K; break;
    default: break;
    }
    if (s.srTarget == want) return;
    s.srTarget = want;
    impl_->commit(std::move(s));
    emit settingsChanged();
}

QVariantList QmlPlayerBridge::presetChoices() const {
    // One list for both kinds; the label carries the distinction so the export page
    // can offer "any preset" without a second control.
    QVariantList out;
    out << QVariantMap{{"id", QStringLiteral("-1")}, {"label", tr("当前播放设置")}};
    const auto& entries = impl_->facade.presets().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        QVariantMap item;
        item["id"] = QString::number(i);
        item["label"] = utf8Of(entries[i].name)
                        + (entries[i].kind == engine::ChainMode::Node ? tr(" · 节点") : QString());
        item["disabled"] = entries[i].kind == engine::ChainMode::Node;
        out << item;
    }
    return out;
}

void QmlPlayerBridge::pauseExport(bool paused) {
    impl_->exportJob.pause(paused);
    pollExport();
}

QString QmlPlayerBridge::formatTime(double seconds) const { return mmss(seconds); }

QString QmlPlayerBridge::diagnosticsReport() const {
    // Engine-reported values only. A diagnostic that guesses is worse than none.
    const auto& s = impl_->snapshot;
    QStringList lines;
    lines << tr("状态: %1").arg(uiText(s.status));
    lines << tr("源: %1x%2 旋转 %3° 标称 %4 fps")
                 .arg(s.sourceWidth).arg(s.sourceHeight).arg(s.sourceRotationDegrees)
                 .arg(s.nominalSourceFps, 0, 'f', 2);
    lines << tr("提交 FPS: %1").arg(s.submissionFps ? QString::number(*s.submissionFps, 'f', 2) : tr("未测量"));
    lines << tr("晚点: 当前 %1 ms / P95 %2 ms").arg(s.lateMs, 0, 'f', 2).arg(s.lateP95Ms, 0, 'f', 2);
    lines << tr("调度等待 P95: %1 ms").arg(s.schedulingWaitP95Ms, 0, 'f', 2);
    lines << tr("处理 CPU P95: %1 ms").arg(s.processCpuP95Ms, 0, 'f', 2);
    lines << tr("呈现 CPU P95: %1 ms").arg(s.presentCpuP95Ms, 0, 'f', 2);
    lines << tr("帧: 已处理 %1 生成 %2").arg(s.frames).arg(s.generated);
    lines << tr("NR 求值 %1 / NVOF %2").arg(s.nrEvaluated).arg(s.nvofExecuted);
    if (s.capture) {
        lines << tr("采集: 接收 %1 丢弃 %2 速率 %3 fps")
                     .arg(s.captureReceived).arg(s.captureDropped).arg(s.captureFps, 0, 'f', 2);
    }
    if (!s.backendWarning.empty()) lines << tr("后端告警: %1").arg(uiText(s.backendWarning));
    if (!s.colorStatus.empty()) lines << tr("色彩: %1").arg(uiText(s.colorStatus));
    if (!s.videoHdrStatus.empty()) lines << tr("Video HDR: %1").arg(uiText(s.videoHdrStatus));
    return lines.join(QLatin1Char('\n'));
}

// --- commands ---------------------------------------------------------------
void QmlPlayerBridge::openFileDialog() {
    veyra::log::info("qml-file", "openFileDialog entered");
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("打开视频或图片"), QString(),
        tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts *.jpg *.jpeg *.png *.bmp *.webp);;所有文件 (*)"));
    veyra::log::info("qml-file", path.isEmpty() ? "openFileDialog cancelled" : "openFileDialog selected file");
    if (!path.isEmpty()) openPath(path);
}
int QmlPlayerBridge::thumbnailGeneration() const { return impl_->thumbnailGeneration; }

QObject* QmlPlayerBridge::playlist() const { return impl_->playlist; }
void QmlPlayerBridge::showPlaylist() { emit playlistRequested(); }
void QmlPlayerBridge::addPlaylistFilesDialog() {
    const auto paths = QFileDialog::getOpenFileNames(nullptr, tr("添加文件到播放列表"), {},
        tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts *.mts *.m4v *.wmv *.mpg *.mpeg *.vob *.flv *.ogv *.asf *.3gp *.jpg *.jpeg *.png *.bmp *.webp);;所有文件 (*)"));
    if (!paths.isEmpty()) { impl_->playlist->addPaths(paths); emit playlistRequested(); }
}
void QmlPlayerBridge::addPlaylistFolderDialog() {
    const auto path = QFileDialog::getExistingDirectory(nullptr, tr("导入文件夹到播放列表"));
    if (!path.isEmpty()) { impl_->playlist->addPaths({path}); emit playlistRequested(); }
}
void QmlPlayerBridge::openDroppedUrls(const QList<QUrl>& urls) {
    // A single file on the video retains drag-to-open. Folder/multi-file drops
    // append to the playlist; dropping inside PlaylistWindow always appends.
    if (urls.size() == 1) {
        const QUrl url = urls.front();
        if(!url.isLocalFile()){openUrl(url);return;}
        if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isDir()) { openUrl(url); return; }
    }
    impl_->playlist->addUrls(urls);
    emit playlistRequested();
}
void QmlPlayerBridge::tickPlaylist() {
    const auto& s = impl_->snapshot;
    using T = engine::TransportState;
    if (!impl_->playlistSession) return;
    if (!impl_->liveKind.isEmpty() || utf8Of(impl_->sourceLabel) != impl_->playlistPath) {
        impl_->playlistSession = 0; impl_->playlist->setPlayback({}, {}); return;
    }
    if (s.sessionId != impl_->playlistSession) return; // Ignore old session snapshots after open.
    if (s.capture || s.remotePlay) { impl_->playlistSession = 0; impl_->playlist->setPlayback({}, {}); return; }
    QString state = QStringLiteral("stopped");
    switch (s.transport) {
    case T::Opening: state = QStringLiteral("opening"); break;
    case T::Playing: state = s.image ? QStringLiteral("image") : QStringLiteral("playing"); break;
    case T::Paused: state = QStringLiteral("paused"); break;
    case T::Ended: state = QStringLiteral("ended"); break;
    case T::Failed: state = QStringLiteral("failed"); break;
    default: break;
    }
    impl_->playlist->setPlayback(impl_->playlistPath, state);
    if (s.transport != T::Ended || s.failed || s.image || impl_->openingSource ||
        impl_->playlistCompletedSession == s.sessionId) return;
    impl_->playlistCompletedSession = s.sessionId;
    // Wait for this GUI tick to finish, then recheck the engine session. A stop,
    // new file, or live source selected in between must cancel auto-advance.
    QTimer::singleShot(0, this, [this, session = s.sessionId] {
        const auto latest = impl_->engine.snapshot();
        if (impl_->playlistSession == session && latest.sessionId == session &&
            latest.transport == T::Ended && !latest.failed && !latest.image && impl_->liveKind.isEmpty())
            impl_->playlist->advanceAfterEnd();
    });
}

void QmlPlayerBridge::openPath(const QString& path) {
    // 观看进度由 tickBiliPlay 的 worker 负责上报（B 站那次打开会先把状态取走）。
    // 只有非 B 站打开才在这里上报，因为那条路径没有 worker 替它做。
    // 保留这个入口的理由：openPath 是所有打开方式的汇合点。
    flushBiliWatchProgress();
    // Consumed up front: an empty path must not leave the flag set for the next open.
    const bool keepBilibiliStream = impl_->bilibiliOpenPending;
    impl_->bilibiliOpenPending = false;
    // Any open supersedes a mirror retry that is still waiting on its timer.
    ++impl_->biliOpenGen;
    if (path.isEmpty()) return;
    flushHdrMemory();impl_->hdrTitleApplied.clear();
    // A local file or an unrelated URL must not inherit the last video's DASH audio
    // address or the site's Referer/UA: separateAudio reads audioPath alone, so a
    // stale value makes the engine open the previous Bilibili track as this source's
    // audio. The Bilibili branches write these immediately before calling openPath
    // and raise bilibiliOpenPending, so the open they asked for keeps them.
    if (!keepBilibiliStream) {
        // 不是 B 站那次打开：清掉上一次预决策留下的标记，否则这一部片源会被当成
        // "已经定过档"而跳过首帧决策。
        // 同时说明当前来源不再是 B 站，结束后的「播放」可以按本地路径重开。
        // Not the Bilibili open: clear the previous pre-decision marker so this source
        // is not treated as "already profiled", and note that the current source is no
        // longer Bilibili so Play after the end may reopen by path again.
        impl_->biliSourceActive = false;
        // 重播身份也一起清：换了本地文件或别的地址之后，「播放」不该再回到那个
        // B 站视频。
        // Clear the replay identity too: once a local file or another address is open,
        // Play must not jump back to that Bilibili video.
        impl_->biliReplayBvid.clear();
        impl_->biliReplayCid = 0;
        impl_->fgAutoPlanned = false;
        impl_->pendingHeaders.clear();
        impl_->pendingUserAgent.clear();
        impl_->pendingAudioPath.clear();
        // The mirror retry belongs to the source that was being opened, not to this one.
        impl_->biliMirrors.clear();
        impl_->biliMirrorIndex = 0;
        impl_->biliRetriesLeft = 0;
        impl_->biliRetryPending = false;
        // Danmaku belongs to the Bilibili track it was loaded for: a local file, a plain
        // URL, a capture or a stream must not keep drawing the previous video's comments.
        if (impl_->danmakuEnabled) bilibiliDisableDanmaku();
        // A history position belongs to the Bilibili video it was read for. Opening
        // anything else must not seek that other source to it.
        impl_->biliResumeAt = -1.0;
        impl_->biliResumeSession = 0;
    }
    const bool network=media::networkMediaInput(wideOf(path));
    if (QFileInfo(path).isDir()) {
        impl_->playlist->addPaths({path}); emit playlistRequested(); return;
    }
    if (!network&&PlaylistModel::supported(path) && !impl_->playlist->containsPath(path)) impl_->playlist->addPaths({path});
    impl_->playlistPath = path;
    impl_->playlist->setPlayback(path, QStringLiteral("opening"));
    rememberPosition(true);
    impl_->exportQueue.setSelectedId(0);
    ++impl_->liveOpenGen; impl_->pendingLiveText.clear();   // a waiting live open is dropped
    impl_->exportTrimStartSeconds = 0.0;
    impl_->exportTrimEndSeconds = 0.0;
    emit exportChanged();
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    const std::wstring wide = wideOf(path);
    impl_->sourceLabel = wide;
    impl_->liveKind.clear();
    impl_->liveLabel.clear();
    if(network){impl_->liveKind=QStringLiteral("url");impl_->liveLabel=QUrl(path).host();}
    ++impl_->thumbnailGeneration;
    emit thumbnailSourceChanged(network?QString():path);
    emit thumbnailGenerationChanged();
    if(!network)impl_->facade.noteRecentFile(wide);
    emit recentFilesChanged();
    // Switch to a page with a video area, then settle the native window's
    // geometry, and only then open. The presenter samples the window's client
    // size once at initialisation; opening before that point is what produced a
    // 1x1 swapchain.
    // Leave the current page when the user is choosing a source from home, and also
    // from the Bilibili page: that page is a browser, so letting the picture appear in
    // its lower half covers the list and makes the page unusable. Playing a video from
    // the professional page still keeps the user there, which is what the design's
    // source menu implies.
    const bool fromBrowsePage = impl_->currentPage == QLatin1String("bili");
    // 从首页或 B 站页开始播放时切到专业模式：播放之后通常要调补帧/NR/画质，
    // 首屏就给出这些控件比停在极简模式更顺手（用户 2026-10-25 要求）。
    // 已经在专业模式里开始播放则原地不动。
    if (impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home")
        || fromBrowsePage)
        emit navigate(QStringLiteral("pro"));
    if(!network)rememberSource(QStringLiteral("file"), path);
    if (impl_->preOpen) impl_->preOpen();
    auto options = impl_->options;
    options.softwareDecode = impl_->prefString("decode") == QLatin1String("software");
    options.hardwareDecodeOnly = impl_->prefString("decode") == QLatin1String("hardware");
    options.deinterlace=deinterlaceAvailable()&&impl_->prefBool("deinterlace",true);
    // Network / server playback (#48): read at open, like the decode choice above.
    options.network.level=media::networkBufferLevelFromId(impl_->prefString("networkBuffer").toStdString());
    options.network.startupPrebufferSeconds=impl_->prefs.value(QStringLiteral("networkPrebuffer"),-1).toInt();
    options.network.autoReconnect=impl_->prefBool("networkAutoReconnect",true);
    options.network.corrupt=media::corruptPolicyFromId(impl_->prefString("networkCorrupt").toStdString());
    // Per-open values a settings rebuild would otherwise drop: PlayerOptions::from()
    // replaces the whole struct, so the resolver's headers and the DASH audio address
    // are re-applied here, right before the engine reads them.
    options.network.requestHeaders = impl_->pendingHeaders;
    options.network.userAgent = impl_->pendingUserAgent;
    options.audioPath = impl_->pendingAudioPath;
    veyra::log::info("open",std::format("headers={} userAgent={} audioPath={}",
        options.network.requestHeaders.size(),options.network.userAgent.size(),
        options.audioPath.size()));
    impl_->engine.open(impl_->videoWindow, wide, options);
    impl_->openingSessionId = impl_->engine.snapshot().sessionId;
    impl_->playlistSession = network?0:impl_->openingSessionId;
    impl_->resumeSession = 0;
    if ((impl_->prefBool("rememberPosition", true)||impl_->prefBool("autoResume",false))) {
        const auto saved = impl_->prefs.value("positions").toMap().value(path).toMap();
        if (!saved.isEmpty()) { impl_->resumeSession = impl_->openingSessionId; impl_->resumeAt = saved.value("at").toDouble(); }
    }
    refreshSubtitles(network?std::wstring():wide);
    if (fromBrowsePage)
        veyra::log::info("open", "opened from the Bilibili page; the window has switched to 极简");
}

bool QmlPlayerBridge::deinterlaceAvailable() const { return media::FFmpegDeinterlacer::available(); }
void QmlPlayerBridge::openNetworkDialog(){emit navigate(QStringLiteral("network"));}
bool QmlPlayerBridge::openNetworkAddress(const QString& address){
    const QString trimmed = address.trimmed();
    // A Bilibili page URL is not a media URL: resolve it to the CDN stream first.
    // This is what makes "paste the page address and play the video" work without
    // embedding a browser at all.
    if (media::isBilibiliPageUrl(trimmed.toStdWString())) {
        // Send the stored session so member renditions are available.
        const std::wstring cookie = media::loadBilibiliCookie();
        const auto stream = media::resolveBilibiliStream(trimmed.toStdWString(), cookie);
        if (!stream.ok) {
            emit notice(QString::fromStdWString(stream.error), true);
            log::warn("bilibili", QString::fromStdWString(stream.error).toStdString());
            return false;
        }
        if (!stream.title.empty()) impl_->pendingTitle = stream.title;
        // The CDN needs the site's Referer/User-Agent; carry them on the open
        // options so the demuxer sends them (see networkMediaHeaders).
        impl_->pendingHeaders = stream.headers;
        impl_->pendingUserAgent = stream.userAgent;
        impl_->pendingAudioPath = stream.audioUrl;
        impl_->bilibiliOpenPending = true;
        impl_->options.network.requestHeaders = stream.headers;
        impl_->options.network.userAgent = stream.userAgent;
        // DASH keeps audio in its own stream; the video address carries none.
        impl_->options.audioPath = stream.audioUrl;
        // Report what was actually selected: without a session the API silently
        // returns a low rendition, and the picture quality is the only clue.
        QString info = QStringLiteral("%1 × %2  %3")
            .arg(stream.width).arg(stream.height)
            .arg(QString::fromStdWString(stream.codec));
        if (stream.audioUrl.empty()) info += QStringLiteral("  ") + tr("无音频");
        if (!media::bilibiliCookieLooksSignedIn(cookie)) info += QStringLiteral("  ") + tr("（未登录，画质受限）");
        emit notice(tr("正在播放：") + QString::fromStdWString(stream.title) + QStringLiteral("  ") + info, false);
        openPath(QString::fromStdWString(stream.url));
        return true;
    }
    const QUrl url(trimmed,QUrl::StrictMode);
    if(!url.isValid()||url.host().isEmpty()||!media::networkMediaInput(wideOf(url.scheme()+QStringLiteral(":")))){
        emit notice(tr("请输入有效的 HTTP、HTTPS 或 RTSP 视频地址"),true);return false;
    }
    openPath(url.adjusted(QUrl::RemoveFragment).toString(QUrl::FullyEncoded));return true;
}

void QmlPlayerBridge::bilibiliSignOut(){
    media::saveBilibiliCookie({});
    log::info("bilibili", "cookie cleared");
    emit notice(tr("B 站登录已清除"), false);
}
// --- Bilibili module --------------------------------------------------------

namespace {
// Cards travel to QML as plain maps; the page decides how to draw them.
QVariantList cardsToVariant(const std::vector<media::BiliVideoCard>& cards) {
    QVariantList out;
    out.reserve(int(cards.size()));
    for (const auto& c : cards) {
        QVariantMap m;
        m.insert(QStringLiteral("bvid"), QString::fromStdWString(c.bvid));
        m.insert(QStringLiteral("title"), QString::fromStdWString(c.title));
        m.insert(QStringLiteral("cover"), QString::fromStdWString(c.cover));
        m.insert(QStringLiteral("owner"), QString::fromStdWString(c.owner));
        m.insert(QStringLiteral("duration"), QString::fromStdWString(c.duration));
        // Numbers travel as int: QML only needs them for display, and qlonglong
        // needs a registered metatype to cross the boundary safely.
        m.insert(QStringLiteral("play"), QVariant(int(std::min<long long>(c.play, 2147483647LL))));
        m.insert(QStringLiteral("danmaku"), QVariant(int(std::min<long long>(c.danmaku, 2147483647LL))));
        out.push_back(m);
    }
    return out;
}
} // namespace
// 异步版：worker 拉收藏夹列表，tickBiliTasks 取回后发信号。
// The async form: a worker fetches the folder list; tickBiliTasks() collects it
// and emits the signal.
void QmlPlayerBridge::bilibiliRequestFavFolders(const QString& reason){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::FavFolders, [reason] {
        BiliTaskResult r;
        r.reason = reason;
        std::vector<media::BiliFavFolder> folders;
        std::wstring error;
        if (!media::biliFavFolders(folders, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        for (const auto& f : folders) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), QVariant::fromValue<qlonglong>(f.id));
            m.insert(QStringLiteral("title"), QString::fromStdWString(f.title));
            m.insert(QStringLiteral("count"), QVariant::fromValue<qlonglong>(f.count));
            r.list.push_back(m);
        }
        r.ok = true;
        return r;
    });
}
void QmlPlayerBridge::bilibiliDisableDanmaku(){
    impl_->danmakuEnabled = false;
    if (impl_->danmakuWindow) {
        impl_->danmakuWindow->clearItems();
        impl_->danmakuWindow->hide();
    }
    log::info("danmaku", "disabled");
}
// 异步版：worker 发收藏请求，tickBiliTasks 取回后发信号。
// 提示语（已收藏/已取消收藏）和错误提示都在 GUI 线程发，worker 只带回结果。
// The async form: a worker performs the request, tickBiliTasks() collects the
// outcome. The user-facing notice stays on the GUI thread; the worker only carries
// the result back.
void QmlPlayerBridge::bilibiliRequestFavorite(const QString& bvid, bool add,
                                              qlonglong folderId, const QString& reason){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Favorite, [wbvid, add, folderId, reason] {
        BiliTaskResult r;
        r.add = add;
        r.reason = reason;
        std::wstring error;
        r.ok = media::biliFavorite(wbvid, add, folderId, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

// ---------------------------------------------------------------------------
// 通用异步 B 站任务：9 个方法共用一套发起 / 回收。每个 op 有自己的槽位，
// 所以同时发起的请求互不覆盖。
// Generic async Bilibili task: nine methods share one start/collect path, and each op
// owns its slot, so requests started together never evict one another.
// ---------------------------------------------------------------------------

// 内部：发起一个任务。worker 闭包只捕获值，绝不捕获 this 指针或 Qt 容器。
// Internal: start one task. The worker closure captures values only - never `this` and
// never a Qt container.
bool QmlPlayerBridge::startBiliTask(BiliOp op, std::function<BiliTaskResult()> work) {
    auto& i = *impl_;
    auto& slot = i.biliTaskSlots[static_cast<size_t>(op)];
    slot.busy = true;
    slot.query = std::async(std::launch::async, [op, work = std::move(work)] {
        BiliTaskResult r = work();
        r.op = op;
        return r;
    });
    return true;
}

// GUI tick：把所有已完成的任务取走，按 op 发对应信号。一次遍历处理多个槽位，
// 所以同时完成的请求会在同一帧送达，不会各占一帧。
// GUI tick: drain every finished slot and emit the matching signal. Iterating all
// slots means several completions in one frame arrive together.
void QmlPlayerBridge::tickBiliTasks() {
    auto& i = *impl_;
    for (size_t n = 0; n < i.biliTaskSlots.size(); ++n) {
        auto& slot = i.biliTaskSlots[n];
        if (!slot.busy) continue;
        if (slot.query.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        BiliTaskResult r = slot.query.get();
        slot.busy = false;
        if (!r.ok && !r.error.isEmpty()) {
            i.bilibiliError = r.error.toStdWString();
            emit notice(r.error, true);
        } else if (r.ok) {
            i.bilibiliError.clear();
        }
        switch (r.op) {
        case BiliOp::Like:
        case BiliOp::Coin:
        case BiliOp::WatchLater:
        // 发评论和删评论也走这一组：它们同样只需要把结果送回界面。
        // 之前漏了 Reply 这个 case，结果被 switch 丢掉，界面就一直停在「发送中」。
        // Posting and deleting a reply belong here too: they only need their result
        // delivered. Reply was missing, so the switch discarded it and the button stayed at
        // "sending" forever.
        case BiliOp::Reply:
        case BiliOp::DeleteReply:
            // 成功提示要留在 GUI 线程发：worker 不能碰翻译表和信号。
            // 三个动作各有自己的文案，所以按 op 分派而不是用一句通用的话。
            // The success notice belongs on the GUI thread: the worker cannot touch the
            // translation table or emit signals. Each action has its own wording, so
            // dispatch on the op rather than using one generic sentence.
            if (r.ok) {
                switch (r.op) {
                case BiliOp::Like:       emit notice(r.add ? tr("已点赞") : tr("已取消点赞"), false); break;
                case BiliOp::Coin:       emit notice(tr("已投币"), false); break;
                case BiliOp::WatchLater: emit notice(r.add ? tr("已加入稍后再看") : tr("已从稍后再看移除"), false); break;
                case BiliOp::Reply:       emit notice(tr("评论已发送"), false); break;
                case BiliOp::DeleteReply: emit notice(tr("评论已删除"), false); break;
                default: break;
                }
            }
            emit bilibiliActionDone(biliOpName(r.op), r.ok, r.error);
            break;
        case BiliOp::Comments:
            emit bilibiliCommentsReady(r.list, qlonglong(r.number), r.ok, r.error);
            break;
        case BiliOp::AuthorVideos:
            // 复用卡片列表信号，kind 带 "author:" 前缀，界面按前缀分流。
            // Reuses the card-list signal; the kind carries an "author:" prefix and the UI
            // dispatches on it.
            emit bilibiliListReady(r.text, r.list, 0);
            break;
        case BiliOp::EmotePanel:
            // 这里只发信号，不发 notice：面板是点按钮才拉的，失败时在面板位置
            // 显示原因更贴切，弹一条全局提示反而突兀。
            // Signal only, no notice: the panel is fetched on a button press, so showing the
            // reason where the panel would be is clearer than a global toast.
            emit bilibiliEmotePanelReady(r.text.split(QLatin1Char('\n')), r.ok, r.error);
            break;
        case BiliOp::EnableDanmaku: {
            // 创建叠加层、清洗、交给渲染器 —— 三步都在 GUI 线程。
            // Create the overlay, filter, hand it to the renderer - all on the GUI thread.
            int shown = 0;
            if (r.ok) {
                if (!i.danmakuWindow) {
                    i.danmakuWindow = std::make_unique<media::DanmakuWindow>();
                    if (!i.danmakuWindow->create(i.videoWindow)) {
                        i.danmakuWindow.reset();
                        r.ok = false;
                        r.error = tr("无法创建弹幕叠加层");
                    }
                }
                if (r.ok) {
                    std::vector<media::DanmakuItem> items;
                    items.reserve(r.list.size());
                    for (const auto& v : r.list) {
                        const QVariantMap m = v.toMap();
                        media::DanmakuItem item;
                        item.time = m.value(QStringLiteral("time")).toDouble();
                        item.mode = m.value(QStringLiteral("mode")).toInt();
                        item.color = m.value(QStringLiteral("color")).toULongLong();
                        item.text = m.value(QStringLiteral("text")).toString().toStdWString();
                        items.push_back(std::move(item));
                    }
                    log::info("danmaku", std::format("stage: fetched items={}", items.size()));
                    // 渲染器接不住的字符串先剔掉：空的或过长的无害，孤立代理项会让
                    // GDI+ 的 AddString 崩溃。
                    // Drop strings the renderer cannot handle before they reach GDI+: an
                    // empty or over-long one is harmless, a lone surrogate crashes
                    // AddString.
                    size_t dropped = 0;
                    items.erase(std::remove_if(items.begin(), items.end(), [&](const media::DanmakuItem& it) {
                        if (it.text.empty() || it.text.size() > 200) { ++dropped; return true; }
                        for (size_t k = 0; k < it.text.size(); ++k) {
                            const char32_t c = it.text[k];
                            if (c >= 0xD800 && c <= 0xDBFF) {
                                if (k + 1 >= it.text.size() ||
                                    it.text[k + 1] < 0xDC00 || it.text[k + 1] > 0xDFFF) {
                                    ++dropped; return true;
                                }
                                ++k;
                            } else if (c >= 0xDC00 && c <= 0xDFFF) { ++dropped; return true; }
                        }
                        return false;
                    }), items.end());
                    if (dropped) log::warn("danmaku", std::format("dropped {} unusable strings", dropped));
                    log::info("danmaku", std::format("stage: cleaned items={}", items.size()));
                    shown = int(items.size());
                    i.danmakuWindow->setItems(std::move(items));
                    log::info("danmaku", "stage: setItems done");
                    i.danmakuEnabled = true;
                    i.danmakuLastSeconds = -1.0;
                    log::info("danmaku", std::format("enabled lines={}", i.danmakuWindow->itemCount()));
                }
            }
            if (!r.ok && !r.error.isEmpty()) {
                i.bilibiliError = r.error.toStdWString();
                emit notice(r.error, true);
            }
            emit bilibiliDanmakuReady(r.ok, r.error, shown);
            break;
        }
        case BiliOp::ResumeAtCid:
            emit bilibiliResumeReady(r.number);
            break;
        case BiliOp::QrStart:
            // qrKey / qrUrl 必须落进 impl_：bilibiliQrImage() 用 qrUrl 画二维码，
            // 轮询用 qrKey 去问状态。worker 不能写 impl_（那是 GUI 线程的状态），
            // 所以在这里写。漏了这一步的后果是二维码显示成白框、点完立刻退回原页面。
            // qrKey and qrUrl have to land in impl_: bilibiliQrImage() draws the code
            // from qrUrl and the poll asks with qrKey. The worker cannot write impl_ -
            // that state belongs to the GUI thread - so it happens here. Missing this
            // step left the code as a white square and sent the page straight back.
            if (r.ok) {
                i.bilibiliQrKey = r.object.value(QStringLiteral("qrKey")).toString().toStdWString();
                i.bilibiliQrUrl = r.text.toStdWString();
            }
            emit bilibiliQrStarted(r.text, r.ok, r.error);
            break;
        case BiliOp::QrPoll:
            // confirmed 时 worker 把 cookie 带回来，落盘并清掉一次性 key。
            // 完整性校验和成功/失败提示都在 GUI 线程做，与原来的同步版一致。
            // On confirmation the worker carries the cookie back: save it and drop the
            // one-shot key. The integrity check and the notices stay on the GUI thread,
            // matching the synchronous version.
            if (r.text == QLatin1String("confirmed")) {
                const std::wstring cookie =
                    r.object.value(QStringLiteral("cookie")).toString().toStdWString();
                i.bilibiliQrKey.clear();
                i.bilibiliQrUrl.clear();
                if (cookie.empty() || !media::bilibiliCookieLooksSignedIn(cookie)) {
                    emit notice(tr("登录返回的 Cookie 不完整"), true);
                    r.text = QStringLiteral("failed");
                } else if (!media::saveBilibiliCookie(cookie)) {
                    emit notice(tr("无法保存登录信息"), true);
                    r.text = QStringLiteral("failed");
                } else {
                    log::info("bilibili", "qr sign-in stored");
                    emit notice(tr("B 站登录成功，可以播放会员画质"), false);
                }
            } else if (r.text == QLatin1String("expired") || r.text == QLatin1String("none")) {
                i.bilibiliQrKey.clear();
                i.bilibiliQrUrl.clear();
            }
            emit bilibiliQrPolled(r.text);
            break;
        case BiliOp::Account:
            emit bilibiliAccountReady(r.object, r.ok);
            break;
        // 观看进度上报：结果不通知界面。播放结束时弹一句「上报失败」只会打断观看，
        // 而这个请求本来就是尽力而为（作者原注释：best effort）。
        // The progress report tells the UI nothing: a "report failed" toast at the end of
        // a video would only interrupt playback, and the request is best-effort by design.
        case BiliOp::ReportProgress:
            break;
        case BiliOp::Count:
            break;
        // 三个早先完成的异步方法，现在也走这套槽位，回收逻辑只有一份。
        // The three earlier async methods now share this path too, so collection
        // logic exists once.
        case BiliOp::InteractionState:
            emit bilibiliInteractionReady(r.bvid, r.object, r.generation);
            break;
        case BiliOp::Favorite:
            if (r.ok) emit notice(r.add ? tr("已收藏") : tr("已取消收藏"), false);
            emit bilibiliFavoriteReady(r.ok, r.error, r.add, r.reason);
            break;
        case BiliOp::FavFolders:
            emit bilibiliFavFoldersReady(r.list, r.reason);
            break;
        }
    }
}

// --- 动作型：点赞 / 投币 / 稍后再看 -----------------------------------------
// 三者形状一致：发一个写请求，成功则刷新互动状态。提示语在 GUI 线程发。
// All three have the same shape: one write request, then re-read the interaction
// state. The user-facing notice stays on the GUI thread.
void QmlPlayerBridge::bilibiliRequestEmotePanel(){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::EmotePanel, [] {
        BiliTaskResult r;
        std::wstring error;
        std::vector<media::BiliEmote> emotes;
        if (!media::biliEmotePanel(emotes, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 每项 "text<TAB>url"：文本里不会出现制表符，所以这个分隔是安全的。
        // One "text<TAB>url" each; a tab never appears in either field.
        QStringList flat;
        for (const auto& e : emotes)
            flat.push_back(QString::fromStdWString(e.text) + QLatin1Char('\t')
                           + QString::fromStdWString(e.url) + QLatin1Char('\t')
                           + QString::fromStdWString(e.package));
        r.text = flat.join(QLatin1Char('\n'));
        r.ok = true;
        return r;
    });
}

void QmlPlayerBridge::bilibiliDeleteReply(const QString& bvid, qlonglong rpid){
    if (bvid.isEmpty() || rpid <= 0) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    const long long wrpid = rpid;
    startBiliTask(BiliOp::DeleteReply, [wbvid, wrpid] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliDeleteReply(wbvid, wrpid, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliPostReply(const QString& bvid, const QString& message){
    if (bvid.isEmpty() || message.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    const std::wstring wmessage = message.toStdWString();
    startBiliTask(BiliOp::Reply, [wbvid, wmessage] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliPostReply(wbvid, wmessage, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestLike(const QString& bvid, bool like){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Like, [wbvid, like] {
        BiliTaskResult r;
        r.add = like;   // 点赞方向：true=点赞 false=取消
        std::wstring error;
        r.ok = media::biliLike(wbvid, like, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestCoin(const QString& bvid, int count){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Coin, [wbvid, count] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliCoin(wbvid, count, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestWatchLater(const QString& bvid, bool add){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::WatchLater, [wbvid, add] {
        BiliTaskResult r;
        r.add = add;    // 方向：true=加入 false=移除
        std::wstring error;
        r.ok = media::biliAddWatchLater(wbvid, add, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

// --- 取数型：评论 -----------------------------------------------------------
void QmlPlayerBridge::bilibiliRequestComments(const QString& bvid, int page, int sort){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Comments, [wbvid, page, sort] {
        BiliTaskResult r;
        std::wstring error;
        std::vector<media::BiliComment> comments;
        long long total = 0;
        if (!media::biliComments(wbvid, page, sort, comments, total, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 评论总数走 number：结构体里现成的数值字段，不必为此加成员。
        // The total rides on `number`, an existing numeric field, instead of adding a member.
        r.number = double(total);
        for (const auto& c : comments) {
            QVariantMap m;
            m.insert(QStringLiteral("author"), QString::fromStdWString(c.author));
            m.insert(QStringLiteral("face"), QString::fromStdWString(c.face));
            m.insert(QStringLiteral("message"), QString::fromStdWString(c.message));
            // 富文本版（表情已换成 <img>），供评论正文用 RichText 渲染。
            // The rich version with emotes turned into <img>, rendered as RichText.
            m.insert(QStringLiteral("messageRich"), QString::fromStdWString(c.messageRich));
            m.insert(QStringLiteral("like"), QVariant::fromValue<qlonglong>(c.like));
            m.insert(QStringLiteral("ctime"), c.ctime);
            m.insert(QStringLiteral("replies"), c.replies);
            // 删除按钮只给自己的评论，所以把 rpid 和作者 mid 一起送过去；
            // 「是不是我的」由 QML 拿它与 root.account.mid 比较 —— 账号数据本来就在那边，
            // 不必在这里再存一份。
            // The delete button only appears on the account's own comments, so rpid and the
            // author mid go along; QML compares that against root.account.mid, which it
            // already holds, instead of this layer keeping a second copy.
            m.insert(QStringLiteral("rpid"), QVariant::fromValue<qlonglong>(c.rpid));
            m.insert(QStringLiteral("mid"), QVariant::fromValue<qlonglong>(c.mid));
            r.list.push_back(m);
        }
        r.ok = true;
        return r;
    });
}

// --- 取数型：开弹幕 ---------------------------------------------------------
// worker 只负责取数据（两次网络往返）；创建叠加层、清洗字符串、交给渲染器全部在
// GUI 线程的 tick 里做 —— 那三步都碰 impl_ 和窗口句柄，不能离开 UI 线程。
// 早先这版只把「条数」带回来，也没 setItems，所以开关看起来动了但画面没弹幕。
// The worker only fetches (two round trips). Creating the overlay, filtering the
// strings and handing them to the renderer all happen in the GUI tick - those three
// touch impl_ and a window handle and cannot leave the UI thread. An earlier revision
// carried back only a count and never called setItems, so the switch moved but no
// danmaku appeared.
void QmlPlayerBridge::bilibiliRequestEnableDanmaku(const QString& bvid, int pageIndex){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::EnableDanmaku, [wbvid, pageIndex] {
        BiliTaskResult r;
        // 分P 校验也在 worker 里做：biliVideoPage 本身是一次网络往返。
        // The part lookup runs on the worker too: biliVideoPage is a network round trip.
        const auto page = media::biliVideoPage(wbvid);
        if (!page.ok) { r.error = QString::fromStdWString(page.error); return r; }
        if (pageIndex < 0 || pageIndex >= int(page.pages.size())) {
            r.error = QStringLiteral("分P序号无效");
            return r;
        }
        std::vector<media::BiliDanmaku> lines;
        std::wstring error;
        if (!media::biliDanmaku(page.pages[size_t(pageIndex)].first, lines, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 原始弹幕按 QML 能接的形态带回去：时间、模式、颜色、文本。
        // 清洗和渲染留给 GUI 线程。
        // Carry the raw lines back in a form QML can hold: time, mode, colour, text.
        // Filtering and rendering stay on the GUI thread.
        for (const auto& line : lines) {
            QVariantMap m;
            m.insert(QStringLiteral("time"), line.time);
            m.insert(QStringLiteral("mode"), line.mode);
            m.insert(QStringLiteral("color"), QVariant::fromValue<qulonglong>(line.color));
            m.insert(QStringLiteral("text"), QString::fromStdWString(line.text));
            r.list.push_back(m);
        }
        r.number = double(lines.size());
        r.ok = true;
        return r;
    });
}

// --- 取数型：续播位置 -------------------------------------------------------
void QmlPlayerBridge::bilibiliRequestResumeAt(const QString& bvid, qlonglong cid){
    if (bvid.isEmpty() || cid <= 0) {
        emit bilibiliResumeReady(-1.0);
        return;
    }
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::ResumeAtCid, [wbvid, cid] {
        BiliTaskResult r;
        r.number = media::biliResumePosition(wbvid, cid);
        r.ok = true;
        return r;
    });
}

// --- 扫码登录：取二维码 -----------------------------------------------------
void QmlPlayerBridge::bilibiliRequestQrStart(){
    startBiliTask(BiliOp::QrStart, [] {
        BiliTaskResult r;
        const auto login = media::startBilibiliQrLogin();
        if (!login.ok) {
            r.error = QString::fromStdWString(login.error);
            return r;
        }
        // qrKey / qrUrl 要留到 poll 用，所以带回去由 GUI 线程写进 impl_。
        // qrKey and qrUrl are kept for the poll, so they travel back to the GUI thread.
        r.text = QString::fromStdWString(login.qrUrl);
        r.object.insert(QStringLiteral("qrKey"), QString::fromStdWString(login.qrKey));
        r.ok = true;
        return r;
    });
}

// --- 扫码登录：轮询 ---------------------------------------------------------
void QmlPlayerBridge::bilibiliRequestQrPoll(){
    auto& i = *impl_;
    if (i.bilibiliQrKey.empty()) {
        emit bilibiliQrPolled(QStringLiteral("none"));
        return;
    }
    const std::wstring key = i.bilibiliQrKey;
    startBiliTask(BiliOp::QrPoll, [key] {
        BiliTaskResult r;
        const auto poll = media::pollBilibiliQrLogin(key);
        using State = media::BilibiliQrPoll::State;
        switch (poll.state) {
        case State::Waiting:   r.text = QStringLiteral("waiting"); break;
        case State::Scanned:   r.text = QStringLiteral("scanned"); break;
        case State::Expired:   r.text = QStringLiteral("expired"); break;
        case State::Confirmed:
            // cookie 落盘必须在 GUI 线程：它会改 impl_ 并触发界面刷新。
            // Saving the cookie has to happen on the GUI thread: it writes impl_ and
            // refreshes the UI.
            r.text = QStringLiteral("confirmed");
            r.object.insert(QStringLiteral("cookie"), QString::fromStdWString(poll.cookie));
            break;
        default:               r.text = QStringLiteral("none"); break;
        }
        r.ok = true;
        return r;
    });
}

// --- 取数型：账号信息 -------------------------------------------------------
void QmlPlayerBridge::bilibiliRequestAccount(){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::Account, [] {
        BiliTaskResult r;
        const auto a = media::biliAccount();
        r.object.insert(QStringLiteral("loggedIn"), a.loggedIn);
        r.object.insert(QStringLiteral("name"), QString::fromStdWString(a.name));
        r.object.insert(QStringLiteral("face"), QString::fromStdWString(a.face));
        r.object.insert(QStringLiteral("level"), a.level);
        r.object.insert(QStringLiteral("vip"), a.vip);
        // mid 用来判断哪条评论是自己发的。
        // mid identifies which comments belong to this account.
        r.object.insert(QStringLiteral("mid"), QVariant::fromValue<qlonglong>(a.mid));
        r.ok = true;
        return r;
    });
}

int QmlPlayerBridge::bilibiliLoadAuthorVideos(qlonglong mid, int page){
    // 走和卡片列表同一条通道，只是 kind 用 "author:<mid>:<page>" ——
    // 排队、槽位、结果分发都是现成的，不必再造一套。
    // Uses the same channel as the card lists with an "author:<mid>:<page>" kind: queueing,
    // slots and result delivery are all already there.
    return bilibiliLoadList(QStringLiteral("author:") + QString::number(mid)
                            + QLatin1Char(':') + QString::number(page));
}

int QmlPlayerBridge::bilibiliLoadList(const QString& kind){
    // Follows the shape the capture-device query already uses: start the work with
    // std::async, collect it from the GUI tick with a zero-timeout wait. That keeps the
    // result hand-off on the GUI thread and needs no locking for the returned list.
    //
    // Cookie first, here on the GUI thread: the media layer keeps it in one global, and
    // letting the worker write it would race with every other call reading it.
    media::setBiliCookie(media::loadBilibiliCookie());

    const int generation = ++impl_->biliListGeneration;
    // 按 kind 选槽位：qualities / related / detail / 卡片列表各有各的位置，
    // 所以详情页里同时发起的几个请求不会互相覆盖。
    // Pick the slot by kind: qualities, related, detail and card lists each have their
    // own, so the requests the detail page starts together cannot overwrite each other.
    auto& slot = impl_->biliListSlots[size_t(Impl::listSlotOf(kind))];
    // 同一槽位里已经有一个在途请求时，把新请求排进队列，而不是覆盖它。
    // 直接给 slot.query 赋新值会销毁旧 future，那个请求取到的数据就永远不会送达
    // QML —— 表现是数据明明回来了，界面却是空列表。队列里派发的请求在 tick 回收
    // 完当前那个之后再启动，所以既不丢结果，也不会让 GUI 线程等待网络。
    // Queue a new request instead of overwriting the one in flight in this slot.
    // Assigning over slot.query destroys the old future, so whatever that request
    // fetched never reaches QML - which shows as an empty list even though the data
    // came back. A queued request starts only after the current one has been collected,
    // so no result is lost and the GUI thread never waits on the network.
    if (slot.busy) {
        slot.pending.push_back(Impl::PendingListRequest{kind, generation});
        return generation;
    }
    startListWorker(kind, generation);
    return generation;
}

// 启动一个列表 worker。发起路径和 tick 的队列派发共用这一处。
// Start one list worker; the direct path and the tick's queue both use this.
void QmlPlayerBridge::startListWorker(const QString& kind, int generation) {
    auto& slot = impl_->biliListSlots[size_t(Impl::listSlotOf(kind))];
    slot.kind = kind;
    slot.generation = generation;
    slot.busy = true;

    // kind carries everything the worker needs: "home"/"popular"/... , "fav:<id>" or
    // "search:<words>".
    slot.query = std::async(std::launch::async, [kind]() {
        std::vector<media::BiliVideoCard> cards;
        std::wstring error;
        bool ok = false;
        // A kind may carry a page ("popular:2", "search:<words>:2", "fav:<id>:2"). The
        // endpoints with no page parameter (推荐/历史/稍后再看) are simply called again —
        // the site has no page for them, and that is what endless scrolling uses.
        int page = 1;
        QString base = kind;
        const int lastColon = kind.lastIndexOf(QLatin1Char(':'));
        if (lastColon > 0) {
            bool okPage = false;
            const int parsed = kind.mid(lastColon + 1).toInt(&okPage);
            if (okPage && parsed > 0) {
                const QString head = kind.left(lastColon);
                // detail:/qualities:/related: carry a bvid or a cid, not a page.
                // fav: 的两种写法只差一件事：head（最后一个冒号左边）是不是就是
                // "fav" 本身。
                //   "fav:203714961"    → head=="fav"          → 截掉的是文件夹 id
                //   "fav:203714961:2"  → head=="fav:203714961" → 截掉的是页码
                // 不要再去数 head 里的冒号：带页码时 head 必然含一个冒号，用那个做判据
                // 会把页码也当成 id（第一版就是这样，两处都错）。
                // The two fav: forms differ in one thing only: whether head (everything
                // left of the last colon) is the bare token "fav".
                //   "fav:203714961"   -> head=="fav"           -> the cut piece is the id
                //   "fav:203714961:2" -> head=="fav:203714961" -> the cut piece is a page
                // Do not count colons in head: with a page, head always holds one, and
                // using that as the test treats the page as an id too.
                const bool favIdWithoutPage = head == QLatin1String("fav");
                // "author:<mid>" 的最后一个冒号切掉的是页码，但 head 是 "author:<mid>"，
                // 所以和 fav 不同：这里需要页码，判据是 head 是否以 "author:" 开头。
                // For "author:<mid>" the last colon separates the page, and head is
                // "author:<mid>" - unlike fav, the page IS wanted here, so the test is simply
                // whether head starts with "author:".
                const bool authorPage = head.startsWith(QLatin1String("author:"));
                if (!head.startsWith(QLatin1String("detail:"))
                    && !head.startsWith(QLatin1String("qualities:"))
                    && !head.startsWith(QLatin1String("related:"))
                    && !favIdWithoutPage) {
                    page = parsed;
                    base = head;
                }
            }
        }
        if (base == QLatin1String("home")) ok = media::biliRecommended(cards, error);
        else if (base == QLatin1String("popular")) ok = media::biliPopular(cards, error, page);
        else if (base == QLatin1String("follow")) ok = media::biliFollowed(cards, error);
        else if (base == QLatin1String("history")) ok = media::biliHistory(cards, error);
        else if (base == QLatin1String("watchlater")) ok = media::biliWatchLater(cards, error);
        else if (base.startsWith(QLatin1String("author:"))) {
            // "author:<mid>"，页码已经在上面解析到 page。
            // "author:<mid>"; the page has already been parsed above.
            bool okMid = false;
            const qlonglong mid = base.mid(7).toLongLong(&okMid);
            ok = okMid && media::biliAuthorVideos(mid, page, cards, error);
        }
        else if (base.startsWith(QLatin1String("fav:")))
            ok = media::biliFavorites(cards, error, base.mid(4).toLongLong(), page);
        else if (base.startsWith(QLatin1String("search:")))
            ok = media::biliSearch(base.mid(7).toStdWString(), page, cards, error);
        else if (kind.startsWith(QLatin1String("qualities:"))) {
            // "qualities:<bvid>:<cid>" — the cid comes from the detail response, so this
            // does not ask the site for the page a second time (the synchronous
            // bilibiliQualities does, which was a whole extra round trip).
            const QStringList parts = kind.mid(10).split(QLatin1Char(':'));
            if (parts.size() == 2) {
                const QString bvid = parts[0];
                const qlonglong cid = parts[1].toLongLong();
                if (cid > 0) {
                    QVariantList out;
                    for (const auto& q : media::biliQualities(bvid.toStdWString(), cid)) {
                        QVariantMap m;
                        m.insert(QStringLiteral("id"), q.id);
                        m.insert(QStringLiteral("label"), QString::fromStdWString(q.label));
                        m.insert(QStringLiteral("vip"), q.needsVip);
                        out.push_back(m);
                    }
                    ListFetchResult r;
                    r.ok = !out.isEmpty();
                    if (!r.ok) r.error = L"无法获取画质列表";
                    r.items = out;
                    return r;
                }
            }
        }
        else if (kind.startsWith(QLatin1String("related:"))) {
            std::vector<media::BiliVideoCard> related;
            std::wstring relError;
            const bool relOk = media::biliRelated(kind.mid(8).toStdWString(), related, relError);
            ListFetchResult r;
            r.ok = relOk;
            r.error = relError;
            if (relOk) r.items = cardsToVariant(related);
            return r;
        }
        else if (kind.startsWith(QLatin1String("detail:"))) {
            // One object instead of a card list. Built here rather than through the
            // synchronous bilibiliVideoDetail so the open no longer blocks a frame.
            const auto page = media::biliVideoPage(kind.mid(7).toStdWString());
            ListFetchResult r;
            r.ok = page.ok;
            r.error = page.error;
            if (page.ok) {
                QVariantMap m;
                m.insert(QStringLiteral("ok"), true);
                m.insert(QStringLiteral("error"), QString());
                m.insert(QStringLiteral("bvid"), QString::fromStdWString(page.bvid));
                m.insert(QStringLiteral("title"), QString::fromStdWString(page.title));
                m.insert(QStringLiteral("cover"), QString::fromStdWString(page.cover));
                m.insert(QStringLiteral("desc"), QString::fromStdWString(page.desc));
                m.insert(QStringLiteral("owner"), QString::fromStdWString(page.owner));
                // 作者 mid：界面要用它拉「作者视频」。
                // The uploader's mid, which the UI needs for the author's video list.
                m.insert(QStringLiteral("ownerMid"), QVariant::fromValue<qlonglong>(page.ownerMid));
                m.insert(QStringLiteral("play"), QVariant(int(std::min<long long>(page.play, 2147483647LL))));
                m.insert(QStringLiteral("like"), QVariant(int(std::min<long long>(page.like, 2147483647LL))));
                m.insert(QStringLiteral("duration"), page.duration);
                QVariantList parts;
                for (const auto& [cid, title] : page.pages) {
                    QVariantMap p;
                    p.insert(QStringLiteral("cid"), QVariant::fromValue<qlonglong>(cid));
                    p.insert(QStringLiteral("title"), QString::fromStdWString(title));
                    parts.push_back(p);
                }
                m.insert(QStringLiteral("parts"), parts);
                r.object = m;
            }
            return r;
        }
        ListFetchResult r;
        r.ok = ok;
        r.error = error;
        if (ok) r.items = cardsToVariant(cards);
        return r;
    });
}
// 异步版：本函数只启动 worker 就返回，网络请求不再占用 GUI 线程。
// 结果由 tickBiliTasks() 取走并通过 bilibiliInteractionReady 交给 QML。
// 查询期间按钮的旧状态继续显示，等信号到了再更新 —— 比"点一下界面卡半秒"好。
// The async form starts a worker and returns; the round trip no longer occupies the
// GUI thread. tickBiliTasks() collects the result and emits
// bilibiliInteractionReady. The old button state stays on screen until then, which
// beats freezing for half a second on every click.
void QmlPlayerBridge::bilibiliRequestInteractionState(const QString& bvid){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    // 新的请求顶掉还没回来的旧请求：generation 让 QML 丢掉过期结果。
    // A newer request supersedes one still in flight; the generation lets QML drop
    // the stale reply.
    const int generation = ++impl_->biliInteractionGeneration;
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::InteractionState, [wbvid, bvid, generation] {
        BiliTaskResult r;
        r.bvid = bvid;
        r.generation = generation;
        const auto state = media::biliInteractionState(wbvid);
        r.object.insert(QStringLiteral("liked"), state.liked);
        r.object.insert(QStringLiteral("coined"), state.coined);
        r.object.insert(QStringLiteral("favorited"), state.favorited);
        r.object.insert(QStringLiteral("coins"), state.coins);
        r.ok = true;
        return r;
    });
}
bool QmlPlayerBridge::bilibiliPlay(const QString& bvid, int pageIndex, int qn, bool fromStart){
    media::setBiliCookie(media::loadBilibiliCookie());
    // This entry point has to look the page up itself: only the cid is not known here.
    const auto page = media::biliVideoPage(bvid.toStdWString());
    if (!page.ok) {
        emit notice(QString::fromStdWString(page.error), true);
        return false;
    }
    if (pageIndex < 0 || pageIndex >= int(page.pages.size())) {
        emit notice(tr("分P序号无效"), true);
        return false;
    }
    return bilibiliPlayResolved(bvid, page.pages[size_t(pageIndex)].first, page.title,
                                qn, fromStart);
}

bool QmlPlayerBridge::bilibiliPlayCid(const QString& bvid, qlonglong cid, int qn, bool fromStart){
    // No page lookup: the detail response already carried the cid, so this skips one
    // whole round trip between the click and the first frame. The title is not needed for
    // the resolve, and the notice still shows the video's own title from the caller.
    if (cid <= 0) {
        emit notice(tr("分P序号无效"), true);
        return false;
    }
    return bilibiliPlayResolved(bvid, cid, std::wstring(), qn, fromStart);
}

// Shared tail of both play entry points: everything after the part is known. Kept in
// one place so the cid-carrying variant cannot drift from the page-lookup one.
bool QmlPlayerBridge::bilibiliPlayResolved(const QString& bvid, qlonglong cid,
                                           const std::wstring& pageTitle, int qn,
                                           bool fromStart){
    // B 站打开要发三个网络往返（上报进度 / 解析播放地址 / 读续播位置）。它们原先全在
    // GUI 线程同步执行，而 WinHTTP 每个请求 15 秒超时 —— 网络一慢主线程就静止十几秒
    // （现场 2026-10-24 15:03：从列表切一个 4K 视频后静止 22.9 秒）。
    // 现在改成：主线程只取快照和发起请求，请求在 worker 里跑，结果由 tickBiliPlay 回收
    // 后再推进播放状态。状态仍然只在 GUI 线程改。
    if (impl_->biliPlayJob.resolving) {
        veyra::log::info("bilibili", "play request ignored: the previous one is still resolving");
        return false;
    }
    // 上一部的观看进度先取下来（这一部马上要覆盖 biliWatch 状态），网络部分交给 worker。
    int pendingPlayed = 0;
    std::wstring pendingReportBvid;
    long long pendingReportCid = 0;
    if (takeBiliWatchProgress(pendingReportBvid, pendingReportCid, pendingPlayed))
        veyra::log::info("bilibili", std::format("watch progress reporting queued bvid={} played_time={}",
            utf8Of(pendingReportBvid).toStdString(), pendingPlayed));
    media::setBiliCookie(media::loadBilibiliCookie());
    // 记下这次要播什么，供播完后「播放」重播用：地址会过期，bvid/cid/画质不会。
    // Remember what this play is, so Play after the end can resolve again: the address
    // expires, the bvid/cid/quality do not.
    impl_->biliReplayBvid = bvid;
    impl_->biliReplayCid = cid;
    impl_->biliReplayQuality = qn;
    // Every play is a new track (a different part, or the same part replayed): the
    // comments already on screen belong to the previous one. Done before the resolve so
    // a failure below leaves nothing stale on screen either.
    if (impl_->danmakuEnabled) bilibiliDisableDanmaku();
    // The three Bilibili settings that change what gets requested. qn 0 means the
    // quality preference, which itself may be 0 (automatic).
    const int qualityPref = impl_->prefInt("biliQuality", 0);
    const int effectiveQn = qn > 0 ? qn : qualityPref;
    QString codecPref = impl_->prefString("biliCodec");
    if (codecPref.isEmpty()) codecPref = QStringLiteral("auto");
    const bool backupCdn = impl_->prefBool("biliAutoCdn", true);
    QString audioPref = impl_->prefString("biliAudio");
    if (audioPref.isEmpty()) audioPref = QStringLiteral("auto");
    const bool wantResume = !fromStart &&
        (impl_->prefBool("rememberPosition", true) || impl_->prefBool("autoResume", false));

    auto& job = impl_->biliPlayJob;
    const uint64_t generation = ++job.generation;
    job.busy = job.resolving = true;
    job.bvid = bvid;
    job.cid = cid;
    job.pageTitle = pageTitle;
    job.qn = effectiveQn;
    job.fromStart = fromStart;
    job.plannedResume = wantResume;
    // 记下「点击时」引擎用的会话号。续播 seek 要等它变成别的值才发 —— 见 Impl 里
    // biliResumeAfterSession 的说明。这里取引擎的真实快照而不是缓存副本，缓存可能
    // 已经滞后。
    // Record the session the engine was using at click time. The resume seek waits
    // until it changes - see biliResumeAfterSession in Impl. The engine's own snapshot
    // is used rather than the cached copy, which may already be stale.
    impl_->biliResumeAfterSession = impl_->engine.snapshot().sessionId;
    // worker 只碰自己的副本：Qt 对象（impl_ 里的 QString/容器）全部在发起前拷好。
    const std::wstring wbvid = bvid.toStdWString();
    const std::string codec = codecPref.toStdString();
    const std::string audio = audioPref.toStdString();
    // 结果放在 shared_ptr 里：worker 写它，GUI tick 读它。worker 不碰任何 Qt 对象。
    job.result = std::make_shared<BiliPlayWorkerResult>();
    auto result = job.result;
    job.query = std::async(std::launch::async,
        [wbvid, cid, effectiveQn, codec, audio, backupCdn, wantResume,
         pendingReportBvid, pendingReportCid, pendingPlayed, result]() mutable {
            // 顺序与原来一致：先解析播放地址，再上报上一部的进度，最后读续播位置。
            result->urls = media::biliResolvePlay(wbvid, cid, effectiveQn, codec, backupCdn, audio);
            if (pendingReportCid > 0 && !pendingReportBvid.empty())
                result->reportOk = media::biliReportWatchProgress(pendingReportBvid, pendingReportCid, pendingPlayed);
            result->reportBvid = pendingReportBvid;
            result->reportCid = pendingReportCid;
            result->reportPlayed = pendingPlayed;
            if (wantResume) result->resumeAt = media::biliResumePosition(wbvid, cid);
            result->done.store(true);
        });
    return true;
}

// 把观看进度从 impl_ 状态里取出来，网络上报交给调用方。
bool QmlPlayerBridge::takeBiliWatchProgress(std::wstring& bvid, long long& cid, int& played) {    if (impl_->biliWatchBvid.empty() || impl_->biliWatchCid <= 0) return false;
    // Nothing was ever observed playing: do not write a bogus position into the history.
    if (impl_->biliWatchLastPos < 0.0) return false;
    played = int(impl_->biliWatchLastPos);
    // The site stores -1 for "watched to the end", which is what makes a finished video
    // show as finished instead of resuming a few seconds before the credits.
    if (impl_->biliWatchDuration > 0 && impl_->biliWatchLastPos >= impl_->biliWatchDuration - 1.0)
        played = -1;
    if (played < 0 && played != -1) played = 0;
    bvid = std::move(impl_->biliWatchBvid);
    cid = impl_->biliWatchCid;
    impl_->biliWatchBvid.clear();
    impl_->biliWatchCid = 0;
    impl_->biliWatchLastPos = -1.0;
    impl_->biliWatchDuration = 0.0;
    return true;
}

QString QmlPlayerBridge::bilibiliLastError() const {
    return QString::fromStdWString(impl_->bilibiliError);
}
QString QmlPlayerBridge::bilibiliQrImage(int size){
    if (impl_->bilibiliQrUrl.empty()) return {};
    // The encoder lives in its own translation unit (see QrImage.h): its headers
    // must not meet Qt's, or the process aborts at startup.
    const std::string png = media::qrPngBase64(
        QString::fromStdWString(impl_->bilibiliQrUrl).toStdString(), size);
    if (png.empty()) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromStdString(png);
}
void QmlPlayerBridge::togglePlayPause() {
    // AppShell's Play button: a paused session is still "running" (the worker
    // stays alive), so the decision must come from the transport state.
    const auto& s = impl_->snapshot;
    using T = engine::TransportState;
    if (s.image || s.transport == T::Opening || s.transport == T::Stopping) return;
    // B 站视频的播完/恢复必须最先处理：它的来源也是网络地址（liveKind=="url"），
    // 下面的网络地址重开会拿 sourceLabel 重新连接 —— 而那是带时效签名的 CDN 地址，
    // 早已过期，只会 403。这里按记住的 bvid/cid/画质重新解析一次，拿到全新地址。
    // 从头播：播完时位置就在结尾，沿用续播位置会立刻再次结束。
    // A finished Bilibili video must be handled before everything else: its source is
    // also a network address (liveKind=="url"), and the branch below reconnects from
    // sourceLabel - a CDN URL with an expiry signature that only 403s by then. Re-resolve
    // from the remembered bvid/cid/quality instead. From the start: the position sits at
    // the end when the video finished, so a resume point would end it immediately again.
    if (impl_->biliSourceActive && !impl_->biliReplayBvid.isEmpty() && impl_->biliReplayCid > 0
        && (s.transport == T::Ended || !s.running)) {
        veyra::log::info("ui-transport", std::format(
            "ended Bilibili video: re-resolving bvid={} cid={} qn={} from the start",
            impl_->biliReplayBvid.toStdString(), impl_->biliReplayCid,
            impl_->biliReplayQuality));
        bilibiliPlayCid(impl_->biliReplayBvid, impl_->biliReplayCid,
                        impl_->biliReplayQuality, /*fromStart=*/true);
        return;
    }
    if(impl_->liveKind==QLatin1String("url")&&(s.transport==T::Ended||(s.transport==T::Paused&&s.duration<=0))){
        openNetworkAddress(QString::fromStdWString(impl_->sourceLabel));return;
    }
    // Capture cards and streams pause too (field request 2026-10-02): the engine stops
    // reading and enhancing, keeps the last picture and resets the source's queue, so
    // play resumes with the newest frame rather than a backlog. The connection stays up.
    if (s.capture && s.running) {
        const bool pause = s.transport == T::Playing;
        impl_->engine.pause(pause);
        veyra::log::info("ui-transport", pause ? "live pause" : "live resume");
        emit notice(pause ? tr("已暂停：画面停在当前帧，不再处理新画面；点播放继续") : tr("已继续"), false);
        return;
    }
    if (!s.running || s.transport == T::Ended) {
        // B 站的情况已在函数开头处理（它的来源也是网络地址），这里只管本地文件：
        // 只有真实存在的文件才重开，设备/流标签不是路径。
        // Bilibili is handled at the top of this function (its source is a network
        // address too); this branch only covers local files - only a file that still
        // exists is reopened, device/stream labels are not paths.
        const QString file = QString::fromStdWString(impl_->sourceLabel);
        if (!file.isEmpty() && QFileInfo(file).isFile()) openPath(file);
        return;
    }
    const bool pause = s.transport == T::Playing;
    impl_->engine.pause(pause);
    veyra::log::info("ui-transport", pause ? "pause" : "resume");
}
void QmlPlayerBridge::stopPlayback() {
    flushHdrMemory();
    impl_->playlistSession = 0;
    impl_->playlist->setPlayback(impl_->playlistPath, QStringLiteral("stopped"));
    rememberPosition(true);
    ++impl_->liveOpenGen; impl_->pendingLiveText.clear();
    impl_->openingSource = false;
    impl_->openingSessionId = 0;
    impl_->engine.stop();
    stopPs5Control();
}
void QmlPlayerBridge::seekTo(double seconds) { impl_->engine.seek(seconds); }
void QmlPlayerBridge::seekBy(double seconds) {
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + seconds));
}
void QmlPlayerBridge::openUrl(const QUrl& url) {
    if(!url.isLocalFile()){openNetworkAddress(url.toString(QUrl::FullyEncoded));return;}
    if (!url.isLocalFile()) {
        emit notice(tr("只能打开本地文件"), true);
        return;
    }
    // Opened inside the drop, as AppShell's WM_DROPFILES does.
    veyra::log::info("ui-drop", url.toLocalFile().toStdString());
    openPath(QDir::toNativeSeparators(url.toLocalFile()));
}
void QmlPlayerBridge::logUi(const QString& channel, const QString& text) {
    const std::string c = channel.toStdString();
    veyra::log::info(c.c_str(), text.toStdString());
}
void QmlPlayerBridge::setWindowMask(QObject* window, const QRectF& rect) {
    auto* w = qobject_cast<QWindow*>(window);
    if (!w) return;
    w->setMask(QRegion(rect.toAlignedRect()));
}
void QmlPlayerBridge::holdOriginal(bool held) {
    // Releasing the key returns to whatever the 显示 page chose (off or split).
    if (held) impl_->engine.comparison(1, false);
    else impl_->engine.comparison(impl_->compareMode, impl_->compareBase, float(impl_->compareSplit));
    veyra::log::info("ui-compare", held ? "hold original on" : "hold original off");
}
void QmlPlayerBridge::stepFrame(int direction) {
    // A frame step is a small seek. The engine owns the real frame duration, so
    // this is honest about being approximate rather than pretending to be exact.
    if (!direction) return;
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + (direction > 0 ? 1.0 / 60.0 : -1.0 / 60.0)));
}

void QmlPlayerBridge::takeScreenshot() {
    if (!hasSource()) { emit notice(tr("没有正在播放的画面，无法截图"), true); return; }
    const QString folder = screenshotDirectory();
    if (!QDir().mkpath(folder)) { emit notice(tr("截图目录不可用：%1").arg(folder), true); return; }
    const QString name = QStringLiteral("veyra-%1.png")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const QString path = QDir(folder).filePath(name);
    impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(path)));
    impl_->lastScreenshot = path;
    veyra::log::info("ui-screenshot", "requested path=" + path.toStdString());
    emit preferencesChanged();
    emit notice(tr("截图保存到：%1").arg(QDir::toNativeSeparators(path)), false);
}

// --- shell preferences (P4-f / P4-c / P4-d) --------------------------------
QVariantMap QmlPlayerBridge::preferences() const { return impl_->prefs; }
QString QmlPlayerBridge::gpuPriorityStatus() const {
    const auto status=gfx::gpuPriorityStatus();
    const auto name=[](int priority){return priority==5?tr("实时"):priority==4?tr("高"):tr("普通");};
    if(status.state==gfx::GpuPriorityState::Pending)return tr("创建显卡会话后生效");
    if(status.state==gfx::GpuPriorityState::Unavailable)return tr("系统不支持调整 GPU 优先级");
    if(status.state==gfx::GpuPriorityState::Rejected)return tr("系统未接受设置，当前：%1").arg(status.actual<0?tr("未知"):name(status.actual));
    if(status.capped)return tr("已生效：%1（抖音直播伴侣运行中，暂不使用实时）").arg(name(status.actual));
    return tr("已生效：%1").arg(name(status.actual));
}
bool QmlPlayerBridge::enhancementPrewarmAvailable() const {
    // PreviewGpuSession::prepare refuses RTX 30/40 (patched FG sessions keep the
    // full NGX lifetime), so the switch must not look usable there either.
    const auto gpu=effectGpu();
    return gpu.vendor==0x10DE&&!ngx::FgCompatibilitySession::requested(gpu.vendor,gpu.deviceId);
}
void QmlPlayerBridge::scheduleEnhancementPrewarm(){
    QTimer::singleShot(2000,this,[this]{
        if(!impl_->prefBool("prewarmEnhancement",false)||!enhancementPrewarmAvailable()||impl_->openingSource||impl_->snapshot.running||!impl_->engine.idle())return;
        if(engine::runtimeOrder(impl_->activeRuntime()))return;
        const auto saved=impl_->prefs.value(QStringLiteral("lastSourceExtent")).toMap();
        const auto width=saved.value("width",1920).toUInt(),height=saved.value("height",1080).toUInt();
        const bool valid=width>=64&&height>=64&&width<=3840&&height<=2160;
        impl_->engine.prewarmEnhancement(impl_->facade.pendingSettings(),valid?width:1920,valid?height:1080);
    });
}
void QmlPlayerBridge::rememberWindowSize(int width, int height) {
    // Only kept when the user chose "记住上次"; the key is internal.
    if (width < 400 || height < 200 || width > 16384 || height > 16384) return;
    const QString v = QStringLiteral("%1x%2").arg(width).arg(height);
    if (impl_->prefs.value(QStringLiteral("lastWindow")).toString() == v) return;
    impl_->prefs[QStringLiteral("lastWindow")] = v;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "last window size not saved");
}
QRect QmlPlayerBridge::screenAvailableAt(int x, int y) const {
    QScreen* screen = QGuiApplication::screenAt(QPoint(x, y));
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1040);
}
QRect QmlPlayerBridge::launchScreenAvailable() const {
    const QPoint at = QCursor::pos();
    return screenAvailableAt(at.x(), at.y());
}

bool QmlPlayerBridge::setPreference(const QString& key, const QVariant& value) {
    if(key==QLatin1String("blackmagicNativeCapture")){
        if(captureQueryBusy()){emit notice(tr("请等待采集设备查询完成后再切换接口"),true);return false;}
        if(captureBlackmagicDevice())impl_->prefs[source::decklink::isPath(impl_->captureDevice)?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice"]=utf8Of(impl_->captureDevice);
    }
    static const QHash<QString, std::pair<int, int>> ranges{
        {"subtitleSize", {16, 56}}, {"subtitleFont", {0, 5}}, {"subtitleOutline", {0, 3}},
        {"subtitleMargin", {0, 240}}, {"subtitleLines", {0, 8}},
        // 列表模式 parameter panel width (ProPage splitter); QML clamps it to the window.
        {"listInspectorWidth", {376, 4000}},
        // Network sources (#48): seconds to collect before the first picture; -1 follows the buffer level.
        {"networkPrebuffer", {-1, 60}}};
    static const QStringList flags{"subtitleEnabled", "subtitleBackground", "subtitleFit",
                                   "subtitleSecondLanguage", "audioForceStereo", "holdCompare",
                                   "magewellLowLatency", "blackmagicNativeCapture", "captureShowAllFrameRates", "cinePillHidden", "exportStopsPlayback", "obsGameCapture", "autoResume", "fullscreenMemoryProtection", "prewarmEnhancement", "hdrFgCompatibility", "computeOnMonitorGpu", "deinterlace", "networkAutoReconnect", "biliAutoCdn",
                                   // Bilibili module: whether a freshly opened video starts with danmaku on.
                                   "biliDanmaku",
                                   // Bilibili 模块：播放完毕后是否回到视频详情。默认关，播完停在
                                   // 播放画面，点播放按钮从头重播。
                                   // Bilibili module: return to the detail page once playback ends;
                                   // off by default, so it finishes on the player and Play replays.
                                   "biliReturnAfterEnd"};
    QVariant stored;
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok);
        const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) return false;
        stored = n;
    } else if (flags.contains(key)) {
        stored = value.toBool();
    } else if (key == QLatin1String("biliQuality")) {
        // Bilibili module: the quality a video opens with. 0 = automatic (the best the
        // account may use, which is HDR on a video that has an HDR master); otherwise a
        // Bilibili quality id (127/125/120/116/112/80/74/64/32/16).
        bool ok = false;
        const int n = value.toInt(&ok);
        static const QList<int> allowed{0, 127, 126, 125, 120, 116, 112, 80, 74, 64, 32, 16};
        if (!ok || !allowed.contains(n)) return false;
        stored = n;
    } else if (key == QLatin1String("biliCodec")) {
        // Bilibili module: which codec to prefer when several exist at the same quality.
        // "auto" takes the HDR-capable one first (HEVC), "avc" forces H.264, "hevc"
        // forces HEVC. "auto" is what makes an HDR video play as HDR.
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("avc") && v != QLatin1String("hevc")) return false;
        stored = v;
    } else if (key == QLatin1String("danmakuScale")) {
        // Danmaku appearance. Ranges are the ones newBV exposes: size 50-250%,
        // opacity 10-100%, speed 50-200%, area 10-100% of the picture height.
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 50 || n > 250) return false;
        stored = n;
    } else if (key == QLatin1String("danmakuOpacity")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 10 || n > 100) return false;
        stored = n;
    } else if (key == QLatin1String("danmakuSpeed")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 50 || n > 200) return false;
        stored = n;
    } else if (key == QLatin1String("danmakuArea")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 10 || n > 100) return false;
        stored = n;
    } else if (key == QLatin1String("biliAudio")) {
        // Bilibili module: which audio track to prefer. "auto" takes Hi-Res, then
        // Dolby, then the highest-bitrate AAC; a preference the video cannot satisfy
        // falls back to "auto" rather than going silent.
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("flac") && v != QLatin1String("dolby")
            && v != QLatin1String("aac192") && v != QLatin1String("aac132")
            && v != QLatin1String("aac64")) return false;
        stored = v;
    } else if (key == QLatin1String("biliAutoCdn")) {
        // Bilibili module: try the backup CDN addresses when the primary one fails.
        stored = value.toBool();
    } else if (key == QLatin1String("screenshotDir")) {
        const QString dir = value.toString();
        if (!dir.isEmpty() && !QFileInfo(dir).isDir()) { emit notice(tr("截图目录不存在：%1").arg(dir), true); return false; }
        stored = dir;
    } else if (key == QLatin1String("audioDevice")) {
        stored = value.toString();
    } else if (key == QLatin1String("sliderKeyStep")) {
        // Arrow-key step of a focused slider (设置 → 通用): 1, 0.1 or 0.01.
        bool ok = false; const double v = value.toDouble(&ok);
        if (!ok || !(qFuzzyCompare(v, 1.0) || qFuzzyCompare(v, 0.1) || qFuzzyCompare(v, 0.01))) return false;
        stored = v;
    } else if (key == QLatin1String("accent")) {
        const QString v = value.toString();
        if (v != QLatin1String("orange") && v != QLatin1String("white")) return false;
        stored = v;
    } else if (key == QLatin1String("backdrop")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 0 || n > 2) return false;
        stored = n;
    } else if (key == QLatin1String("uiScale")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || (n != 0 && n != 100 && n != 125 && n != 150)) return false;
        stored = n;
    } else if (key == QLatin1String("overlayCompat")) {
        // 设置 → 监控软件兼容; read by main.cpp before Qt starts.
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("off")) return false;
        stored = v;
    } else if (key == QLatin1String("gpuPriority")) {
        const QString v=value.toString();
        if(v!=QLatin1String("normal")&&v!=QLatin1String("high")&&v!=QLatin1String("realtime"))return false;
        stored=v;
    } else if (key == QLatin1String("decode")) {
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("software") && v != QLatin1String("hardware")) return false;
        stored = v;
    } else if (key == QLatin1String("networkBuffer")) {
        // Network buffer level (#48): the ids of media::NetworkBufferLevel.
        const QString v = value.toString();
        static const QStringList levels{"auto", "off", "small", "medium", "large"};
        if (!levels.contains(v)) return false;
        stored = v;
    } else if (key == QLatin1String("networkCorrupt")) {
        // What to do with pictures damaged by packet loss (network sources only).
        const QString v = value.toString();
        if (v != QLatin1String("conceal") && v != QLatin1String("keyframe")) return false;
        stored = v;
    } else if (key == QLatin1String("rememberPosition") || key == QLatin1String("dockPinned")) {
        stored = value.toBool();
    } else if (key == QLatin1String("language")) {
        const QString v = value.toString();
        static const QStringList languages{"auto", "zh-CN", "zh-TW", "en", "ja"};
        if (!languages.contains(v)) return false;
        stored = v;
    } else if (key == QLatin1String("monitorGpu")) {
        const QString v = value.toString();
        if (!v.isEmpty() && std::none_of(impl_->gpuAdapters.begin(), impl_->gpuAdapters.end(), [&](const auto& a) { return a.id == v; })) return false;
        stored = v;
    } else if (key == QLatin1String("windowSize")) {
        const QString v = value.toString();
        static const QStringList sizes{"1280x800", "1600x1000", "1920x1200", "last"};
        if (!sizes.contains(v)) return false;
        stored = v;
    } else {
        return false;
    }
    if (impl_->prefs.value(key) == stored) return true;
    const auto previous = impl_->prefs;
    impl_->prefs[key] = stored;
    if (!impl_->savePrefs()) {
        impl_->prefs = previous;
        emit notice(tr("偏好保存失败，已保留原设置"), true);
        return false;
    }
    veyra::log::info("ui-prefs", std::format("{}={}", key.toStdString(), stored.toString().toStdString()));
    applyPreference(key);
    emit preferencesChanged();
    return true;
}
void QmlPlayerBridge::applyPreference(const QString& key) {
    if(key==QLatin1String("hdrFgCompatibility")){
        impl_->presentation.hdrFgCompatibility=impl_->prefBool("hdrFgCompatibility",false);
        impl_->engine.requestPresentation(impl_->presentation);
        emit presentationChanged();
    }
    if(key==QLatin1String("prewarmEnhancement")){
        impl_->engine.setEnhancementPrewarmEnabled(impl_->prefBool("prewarmEnhancement",false));
        if(impl_->prefBool("prewarmEnhancement",false))scheduleEnhancementPrewarm();
    }
    if(key==QLatin1String("gpuPriority")){
        const auto value=impl_->prefString("gpuPriority");
        // Unset means the default, 实时 (field request 2026-10-05).
        gfx::requestGpuPriority(value==QLatin1String("normal")?gfx::GpuPriority::Normal:
                                value==QLatin1String("high")?gfx::GpuPriority::High:gfx::GpuPriority::Realtime);
    }
    if(key==QLatin1String("overlayCompat"))updateLiveCompanionCompat();
    if(key==QLatin1String("fullscreenMemoryProtection")){impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));emit snapshotChanged();}
    if(key==QLatin1String("captureShowAllFrameRates"))emit captureChanged();
    if (key == QLatin1String("audioDevice")) sink::setPreferredRenderEndpoint(impl_->prefString("audioDevice").toStdWString());
    else if (key == QLatin1String("audioForceStereo")) sink::setForceStereoDownmix(impl_->prefBool("audioForceStereo", false));
    else if (key == QLatin1String("magewellLowLatency")) source::magewell::setLowLatencyPreference(impl_->prefBool("magewellLowLatency", false));
    else if (key == QLatin1String("blackmagicNativeCapture")) { if(impl_->captureListed)refreshCaptureDevices();emit captureChanged(); }
    else if (key == QLatin1String("language")) {
        const QString code = i18n::resolve(impl_->prefString("language"));
        if (!i18n::apply(code)) emit notice(tr("这个语言的翻译没有找到，界面保持简体中文"), true);
        if (impl_->qmlEngine) {
            impl_->qmlEngine->setUiLanguage(i18n::current());
            impl_->qmlEngine->retranslate();
            // Strings the bridge builds itself (tr() in getters) follow once their
            // properties notify: every argument-free notify signal fires once.
            const QMetaObject* meta = metaObject();
            QSet<int> fired;
            for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
                const QMetaMethod notify = meta->property(i).notifySignal();
                if (notify.isValid() && notify.parameterCount() == 0 && !fired.contains(notify.methodIndex())) {
                    fired.insert(notify.methodIndex());
                    notify.invoke(this, Qt::DirectConnection);
                }
            }
            impl_->exportQueue.refresh();
        }
    }
    else if (key == QLatin1String("monitorGpu") || key == QLatin1String("computeOnMonitorGpu")) {
        if (key == QLatin1String("monitorGpu")) { impl_->resolveGpuMonitor(); impl_->gpuUtil = -1.0; emit perfChanged(); }
        const auto before = impl_->computeLuid;
        impl_->applyComputeGpu();
        if (impl_->computeLuid != before) {
            // Effect availability follows the compute GPU (NVIDIA-only effects on another card).
            (void)currentEffectCapabilities(true);
            // The chain is checked against the new card the way a restart checks it:
            // DLSS moves to an available backend, NVOF to FidelityFX flow, and stages
            // the card cannot run are switched off with their parameters kept.
            auto candidate = impl_->currentSession();
            const auto previous = candidate;
            // Switching back gives the list mode its frame generation again (DLSS 4X
            // moved to FSR 2X by the last switch returns as DLSS 4X) once the card runs it.
            const auto caps = currentEffectCapabilities();
            const auto runs = [&](const QString& id) { return caps.value(id).toMap().value("available").toBool(); };
            auto& active = candidate.configurations[size_t(candidate.active)];
            const bool listMode = !active.editor;
            const auto memory = impl_->prefs.value(QStringLiteral("computeFgMemory")).toMap();
            bool restored = false;
            if (listMode && !memory.isEmpty()) {
                const auto backend = engine::FrameGenerationBackend(memory.value(QStringLiteral("backend")).toInt());
                const auto flow = engine::OpticalFlowBackend(memory.value(QStringLiteral("flow")).toInt());
                if (runs(fgEffectId(backend))) {
                    active.fgBackend = backend;
                    active.chain.fgMultiplier = memory.value(QStringLiteral("multiplier")).toUInt();
                    if (flow != engine::OpticalFlowBackend::Nvidia || runs(QStringLiteral("flow0"))) active.opticalFlowBackend = flow;
                    impl_->prefs.remove(QStringLiteral("computeFgMemory"));
                    restored = true;
                    veyra::log::info("effect-availability", std::format("compute gpu switch restored backend={} multiplier={}",
                        engine::frameGenerationBackendName(backend), active.chain.fgMultiplier));
                }
            }
            QStringList disabled;
            for (auto& configuration : candidate.configurations) {
                const auto reason = disableUnavailableEffects(configuration);
                if (!reason.isEmpty()) disabled << reason;
            }
            const auto& before = previous.configurations[size_t(previous.active)];
            if (listMode && !restored && (before.fgBackend != active.fgBackend || before.opticalFlowBackend != active.opticalFlowBackend))
                impl_->prefs[QStringLiteral("computeFgMemory")] = QVariantMap{{QStringLiteral("backend"), int(before.fgBackend)},
                    {QStringLiteral("multiplier"), int(before.chain.fgMultiplier)}, {QStringLiteral("flow"), int(before.opticalFlowBackend)}};
            if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "compute gpu frame generation memory not saved");
            if (candidate != previous) {
                auto next = impl_->facade.pendingSettings();
                const auto& configuration = candidate.configurations[size_t(candidate.active)];
                configuration.apply(next);
                if (next.validate().empty() && impl_->facade.applySettings(next, engine::runtimeOrder(configuration.chain))) {
                    impl_->session = candidate;
                    impl_->restoreConfiguration(configuration);
                    impl_->revalidate();
                    impl_->options = engine::PlayerOptions::from(next, engine::runtimeOrder(impl_->activeRuntime()),&impl_->options);
                    if (!impl_->persistSession()) veyra::log::warn("effect-availability", "compute gpu refit not saved");
                    emitChainChanged(); emit settingsChanged();
                } else veyra::log::warn("effect-availability", "compute gpu refit not accepted; chain unchanged");
                if (!disabled.isEmpty()) {
                    const auto message = tr("已关闭当前硬件或包不支持的效果，参数仍保留：") + disabled.join("; ");
                    veyra::log::warn("effect-availability", message.toStdString());
                    emit notice(message, false);
                }
            }
            emit effectCapabilitiesChanged(); emit fgChoicesChanged();
            if (impl_->snapshot.running)
                emit notice(impl_->computeLuid ? tr("运算显卡已切换为「%1」，重新打开视频后生效").arg(impl_->computeGpuName)
                                               : tr("运算显卡已恢复默认（性能最强的显卡），重新打开视频后生效"), false);
        }
    }
}
QVariantList QmlPlayerBridge::gpuMonitorChoices() const {
    QVariantList out;
    out << QVariantMap{{"id", QString()}, {"label", tr("自动（独立显卡）")}};
    for (const auto& a : impl_->gpuAdapters) out << QVariantMap{{"id", a.id}, {"label", a.name}};
    return out;
}
QString QmlPlayerBridge::gpuMonitorName() const { return impl_->gpuMonitorName; }
QString QmlPlayerBridge::computeGpuHint() const {
    if (!impl_->prefBool("computeOnMonitorGpu", false)) return tr("关闭时增强、补帧和导出用性能最强的显卡");
    if (impl_->computeLuid) return tr("增强、补帧和导出都在「%1」上运行；切换后重新打开视频生效").arg(impl_->computeGpuName);
    return tr("上面选“自动”时仍用性能最强的显卡；选一块显卡后它也负责运算");
}
QString QmlPlayerBridge::uiLanguage() const { return i18n::current(); }
void QmlPlayerBridge::setQmlEngine(QQmlEngine* engine) {
    impl_->qmlEngine = engine;
    if (engine) engine->setUiLanguage(i18n::current());
}
QVariantList QmlPlayerBridge::audioDevices() const {
    QVariantList out;
    out << QVariantMap{{"id", QString()}, {"label", tr("跟随系统默认")}, {"isDefault", false}};
    const QString chosen = impl_->prefString("audioDevice");
    bool listed = chosen.isEmpty();
    for (const auto& e : impl_->audioEndpoints) {
        const QString id = QString::fromStdWString(e.id);
        listed = listed || id == chosen;
        out << QVariantMap{{"id", id}, {"label", QString::fromStdWString(e.name) + (e.isDefault ? tr(" · 当前默认") : QString())},
                           {"isDefault", e.isDefault}};
    }
    // A saved device that is unplugged stays visible, marked, instead of vanishing.
    if (!listed) out << QVariantMap{{"id", chosen}, {"label", tr("已保存的设备（未连接）")}, {"isDefault", false}, {"missing", true}};
    return out;
}
QString QmlPlayerBridge::audioOutputStatus() const {
    const auto& a = impl_->audioActive;
    if (!a.valid) return tr("尚未打开音频输出");
    QString text = QString::fromStdWString(a.name.empty() ? L"(未命名设备)" : a.name) + tr(" · %1 声道").arg(a.channels);
    if (a.downmix) text += tr(" · 下混");
    if (a.fallback) text = tr("所选设备不可用，已回落到系统默认：") + text;
    return text;
}
bool QmlPlayerBridge::audioOutputFallback() const { return impl_->audioActive.valid && impl_->audioActive.fallback; }
void QmlPlayerBridge::refreshAudioDevices() {
    impl_->audioEndpoints = sink::enumerateRenderEndpoints();
    impl_->audioActive = sink::activeRenderEndpoint();
    veyra::log::info("ui-audio", std::format("render endpoints={}", impl_->audioEndpoints.size()));
    emit audioDevicesChanged();
}
// --- shortcuts (P4-f) --------------------------------------------------------
namespace {
const QVariantMap& shortcutDefaults() {
    static const QVariantMap d{{"playPause", "Space"}, {"fullscreen", "F"}, {"lock", "Ctrl+L"},
                               {"hold", "V"}, {"screenshot", "Ctrl+S"}, {"toggleMode", "Tab"},
                               {"gamePanel", "Home"}, {"gameExit", "Ctrl+Alt+Home"}};
    return d;
}
}
QVariantMap QmlPlayerBridge::shortcuts() const {
    QVariantMap out = shortcutDefaults();
    const auto custom = impl_->prefs.value("shortcuts").toMap();
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (out.contains(it.key())) {
            if ((it.key()=="gamePanel"||it.key()=="gameExit") && !ui::gameKey(QKeySequence(it.value().toString(),QKeySequence::PortableText)).vk) continue;
            out[it.key()] = it.value();
        }
    return out;
}
bool QmlPlayerBridge::setShortcut(const QString& action, const QString& sequence) {
    if (!shortcutDefaults().contains(action)) return false;
    const QKeySequence seq(sequence, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1) { emit notice(tr("无效的按键"), true); return false; }
    const QString text = seq.toString(QKeySequence::PortableText);
    if ((action=="gamePanel"||action=="gameExit") && !ui::gameKey(seq).vk) {
        emit notice(tr("无效的按键"),true); return false;
    }
    if ((action=="gamePanel"||action=="gameExit") &&
        (text=="Ctrl+Alt+Del"||text=="Alt+Tab"||text=="Alt+F4"||text=="Ctrl+Shift+Esc"||text=="Alt+Enter")) {
        emit notice(tr("%1 已被固定功能占用").arg(text),true); return false;
    }
    // Keys the shell already owns cannot be taken over.
    static const QStringList reserved{"Esc", "F11", "Left", "Right", "Up", "Down", "Ctrl+O", "Ctrl+E",
                                      "B", "Z", "X", "T", "Y", "Shift+Z", "Shift+X", "Alt+Return"};
    if (reserved.contains(text)) { emit notice(tr("%1 已被固定功能占用").arg(text), true); return false; }
    const auto current = shortcuts();
    for (auto it = current.begin(); it != current.end(); ++it)
        if (it.key() != action && it.value().toString() == text) {
            emit notice(tr("%1 已用于其他操作").arg(text), true); return false;
        }
    auto custom = impl_->prefs.value("shortcuts").toMap();
    custom[action] = text;
    const auto previous = impl_->prefs;
    impl_->prefs["shortcuts"] = custom;
    if (!impl_->savePrefs()) { impl_->prefs = previous; emit notice(tr("快捷键保存失败"), true); return false; }
    veyra::log::info("ui-prefs", std::format("shortcut {}={}", action.toStdString(), text.toStdString()));
    const auto keys=shortcuts();
    impl_->game.setHotkeys(ui::gameKey(QKeySequence(keys.value("gamePanel").toString(),QKeySequence::PortableText)),
                          ui::gameKey(QKeySequence(keys.value("gameExit").toString(),QKeySequence::PortableText)));
    emit preferencesChanged();
    return true;
}
void QmlPlayerBridge::resetShortcuts() {
    const auto previous = impl_->prefs;
    impl_->prefs.remove("shortcuts");
    if (!impl_->savePrefs()) { impl_->prefs = previous; return; }
    impl_->game.setHotkeys(ui::gameKey(QKeySequence("Home")),ui::gameKey(QKeySequence("Ctrl+Alt+Home")));
    emit preferencesChanged();
}
bool QmlPlayerBridge::obsGameCaptureActive() const { return qApp && qApp->property("veyraObsGameCapture").toBool(); }
bool QmlPlayerBridge::overlayCompatActive() const { return qApp && qApp->property("veyraOverlayCompat").toBool(); }
bool QmlPlayerBridge::rivaTunerRunning() const { return gfx::rivaTunerRunning(); }
bool QmlPlayerBridge::presentationOverlayRunning() const {
    return gfx::rivaTunerRunning() || gfx::nvidiaOverlayRunning() || gfx::gamePPRunning() || gfx::douyinLiveCompanionRunning();
}
// The field hangs with 抖音直播伴侣 (2026-10-15) came with this process at Realtime GPU
// priority. OBS itself never asks for Realtime when hardware-accelerated GPU scheduling
// is on (High instead, libobs-d3d11 set_priority); the same class is used here while
// the companion runs, so neither program can hold the GPU off from the other.
void QmlPlayerBridge::updateLiveCompanionCompat() {
    const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
    const bool running = automatic && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_DOUYIN", nullptr, 0)
                         && gfx::douyinLiveCompanionRunning();
    if (running == impl_->liveCompanionCap) return;
    impl_->liveCompanionCap = running;
    gfx::setGpuPriorityCap(running ? gfx::GpuPriority::High : gfx::GpuPriority::Realtime);
    const auto status = gfx::gpuPriorityStatus();
    veyra::log::warn("overlay-hooks", std::format("Douyin live companion {}: GPU priority cap={} requested={} actual={}",
        running ? "running" : "gone", running ? "high" : "none", int(status.requested), status.actual));
    if (running && status.capped)
        emit notice(tr("检测到抖音直播伴侣：GPU 优先级暂时从“实时”降为“高”，避免两个程序争用显卡时卡死；直播伴侣退出后自动恢复"), false);
    emit preferencesChanged();
}

int QmlPlayerBridge::uiScaleActive() const { return qApp ? qApp->property("veyraUiScale").toInt() : 0; }
void QmlPlayerBridge::openFeedbackPage() {
    copyDiagnostics();
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/issues")));
    emit notice(tr("诊断信息已复制，可直接粘贴到反馈里"), false);
}
void QmlPlayerBridge::openReleasesPage() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/releases")));
}
bool QmlPlayerBridge::openSponsorLink(const QString& id) {
    // The author's pages as the author gave them (2026-10-19); the page shows buttons,
    // never these addresses.
    static constexpr std::pair<const char*, const char*> links[] = {
        {"bilibili", "https://space.bilibili.com/19500532?spm_id_from=333.1007.0.0"},
        {"douyin", "https://v.douyin.com/9s06Bf2Inqc/"},
        {"x", "https://x.com/likely2002"},
        {"github", "https://github.com/Likely7"},
        {"youtube", "https://www.youtube.com/@Likely7Ai"},
        {"kofi", "https://ko-fi.com/likely7"},
    };
    for (const auto& [name, url] : links) {
        if (id != QLatin1StringView(name)) continue;
        veyra::log::info("ui-sponsor", std::format("open {}", name));
        // Acceptance runs click every card: they read the address from the log
        // instead of opening a browser on the test machine.
        if (qEnvironmentVariableIsSet("VEYRA_TEST_SPONSOR_NO_BROWSER")) {
            veyra::log::info("ui-sponsor", std::format("test url {}", url));
            return true;
        }
        if (QDesktopServices::openUrl(QUrl(QString::fromLatin1(url)))) return true;
        emit notice(tr("无法打开浏览器"), true);
        return false;
    }
    return false;
}
QObject* QmlPlayerBridge::updates() const { return updates_; }
bool QmlPlayerBridge::eventFilter(QObject* watched, QEvent* event) {
    const auto type = event->type();
    if (watched == impl_->gameWindow && (type == QEvent::WinIdChange ||
        (type == QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                                          QPlatformSurfaceEvent::SurfaceCreated))) {
        // Rebind after Qt's platform-window transition, never during teardown.
        QMetaObject::invokeMethod(this, [this] {
            if (!impl_->gameWindow || !impl_->gameWindow->handle()) return;
            impl_->game.attach(reinterpret_cast<HWND>(impl_->gameWindow->winId()));
            updateGameGeometry(); updateGameMode();
        }, Qt::QueuedConnection);
    }
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        auto* key = static_cast<QKeyEvent*>(event);
        // 这里**不再**尝试捕获搜索框的回车。实测 2026-10-26：QML 的键盘事件不经过
        // 应用级 eventFilter（Qt Quick 直接调用 QQuickItem::keyPressEvent，不走 sendEvent），
        // 所以这个过滤器一次都看不到回车。回到 QML，用按键的释放事件触发 —— 那是唯一
        // 每次都到得了的通道。
        // The search box's Enter is **not** captured here any more. Measured 2026-10-26: QML
        // keyboard events do not pass through an application-level event filter (Qt Quick
        // calls QQuickItem::keyPressEvent directly rather than sendEvent), so this filter
        // never saw Return even once. The trigger is back in QML, on the key's release - the
        // one channel that arrives every time.
        if (!key->isAutoRepeat() && qobject_cast<QWindow*>(watched)) {
            const QKeySequence hold(shortcuts().value("hold").toString(), QKeySequence::PortableText);
            const int pressed = key->key() | int(key->modifiers() & ~Qt::KeypadModifier);
            // Typing into a text field never compares.
            const QObject* focus = QGuiApplication::focusObject();
            const bool typing = focus && (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit"));
            if (!hold.isEmpty() && hold[0].toCombined() == pressed && !typing && impl_->prefBool("holdCompare", true)) {
                const bool down = type == QEvent::KeyPress;
                if (down != impl_->holdKeyDown) { impl_->holdKeyDown = down; holdOriginal(down); }
                return true;
            }
            if (type == QEvent::KeyRelease && impl_->holdKeyDown && key->key() == QKeySequence(hold)[0].key()) {
                impl_->holdKeyDown = false; holdOriginal(false);
            }
        }
    } else if (type == QEvent::ApplicationDeactivate && impl_->holdKeyDown) {
        impl_->holdKeyDown = false; holdOriginal(false);
    }
    return QObject::eventFilter(watched, event);
}
// Remembered playback positions: files only, the last 40, saved on switch/stop/exit.
void QmlPlayerBridge::rememberPosition(bool flush) {
    const auto& s = impl_->snapshot;
    const QString file = QString::fromStdWString(impl_->sourceLabel);
    if (s.running && !s.image && !s.capture && s.duration > 0 && !file.isEmpty() && QFileInfo(file).isFile()) {
        auto positions = impl_->prefs.value("positions").toMap();
        const double at = s.position;
        if (at >= 0.5 && at < s.duration - 0.5) positions[file] = QVariantMap{{"at", at}, {"t", QDateTime::currentSecsSinceEpoch()}};
        else positions.remove(file);   // finished or barely started: next time from the top
        while (positions.size() > 40) {
            auto oldest = positions.begin();
            for (auto it = positions.begin(); it != positions.end(); ++it)
                if (it.value().toMap().value("t").toLongLong() < oldest.value().toMap().value("t").toLongLong()) oldest = it;
            positions.erase(oldest);
        }
        impl_->prefs["positions"] = positions;
    }
    if (flush) impl_->savePrefs();
}

QString QmlPlayerBridge::screenshotDirectory() const {
    const QString chosen = impl_->prefString("screenshotDir");
    if (!chosen.isEmpty()) return chosen;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("Veyra"));
}
QString QmlPlayerBridge::lastScreenshot() const { return impl_->lastScreenshot; }
QString QmlPlayerBridge::dataDirectory() const { return QString::fromStdWString(impl_->dataDir.wstring()); }
// main() records the file it actually opened (it falls back to a per-process name).
QString QmlPlayerBridge::logFile() const { return qApp ? qApp->property("veyraLogFile").toString() : QString(); }
void QmlPlayerBridge::chooseScreenshotDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择截图目录"), screenshotDirectory());
    if (!dir.isEmpty()) setPreference(QStringLiteral("screenshotDir"), dir);
}
void QmlPlayerBridge::openScreenshotDirectory() {
    const QString dir = screenshotDirectory();
    QDir().mkpath(dir);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("无法打开目录：%1").arg(dir), true);
}
void QmlPlayerBridge::openLogFolder() {
    const QString file = logFile();
    const QString dir = file.isEmpty() ? QString() : QFileInfo(file).absolutePath();
    if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("日志目录不可用"), true);
}
void QmlPlayerBridge::openDataFolder() {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dataDirectory()))) emit notice(tr("无法打开数据目录"), true);
}

void QmlPlayerBridge::chooseExportPath() {
    if (impl_->chain.mode == engine::ChainMode::Node) {
        emit notice(tr("节点链尚未接入导出执行器，请切换列表模式后导出"), true);
        return;
    }
    const QString path = QFileDialog::getExistingDirectory(nullptr,tr("选择导出目录"),exportTarget());
    setExportPath(path);
}

void QmlPlayerBridge::setExportPath(const QString& path) {
    if (path.isEmpty()) return;
    impl_->exportOutput = wideOf(path);
    impl_->exportStatus = QFileInfo(path).fileName();
    emit exportChanged();
}

void QmlPlayerBridge::startExport() {
    QString error;auto settings=impl_->exportSettings(error);
    if(!settings){emit notice(error,true);return;}
    if(impl_->exportQueue.count()==0){
        if(!QFileInfo(utf8Of(impl_->sourceLabel)).isFile()){emit notice(tr("请先添加要导出的文件"),true);return;}
        const QString target=exportTarget();
        const bool fileTarget=!target.isEmpty()&&!QFileInfo(target).isDir();
        const auto id=impl_->exportQueue.addFile(utf8Of(impl_->sourceLabel),fileTarget?target:QString());
        impl_->exportQueue.queue().setTrim(id,impl_->exportTrimStartSeconds,impl_->exportTrimEndSeconds);
        impl_->exportQueue.setSelectedId(id);
    }
    if(impl_->exportOutput.empty())chooseExportPath();
    if(impl_->exportOutput.empty())return;
    const QFileInfo target(exportTarget());
    const QString folder=target.isDir()?target.absoluteFilePath():target.absolutePath();
    std::wstring reason;
    if(!impl_->exportQueue.queue().start(wideOf(folder),*settings,impl_->exportHevc,impl_->exportRateControl,reason)){
        emit notice(uiText(reason),true);emit exportChanged();return;
    }
    veyra::log::info("qml-export",std::format("export frozen bitrateMbps={} rateControl={} hevc={} srTarget={}",
        settings->exportBitrateMbps,int(impl_->exportRateControl),impl_->exportHevc,int(settings->srTarget)));
    impl_->exportQueue.refresh();pollExport();emit navigate(QStringLiteral("exp"));
    // Leave the GPU to the export (field request 2026-10-02): whatever plays is closed,
    // unless 导出页 → 导出时关闭正在播放的内容 is off.
    if(impl_->prefBool("exportStopsPlayback",true)&&(hasSource()||impl_->openingSource)){
        veyra::log::info("qml-export","export started: closing the playing source");
        stopPlayback();emit notice(tr("已关闭正在播放的内容，显卡全部留给导出"),false);
    }
}
void QmlPlayerBridge::cancelExport() {
    impl_->exportQueue.queue().cancel();impl_->exportQueue.refresh();pollExport();emit exportChanged();
}

void QmlPlayerBridge::restartApplication() {
    requestApplicationRestart(false);
}
void QmlPlayerBridge::restartForOverlayCompatibility() {
    requestApplicationRestart(true);
}
void QmlPlayerBridge::requestApplicationRestart(bool overlayCompatibility) {
    if (exportRunning()) {
        emit notice(tr("设置已保存，请等待导出结束后重启软件"), true);
        return;
    }
    rememberPosition(true);
    qApp->setProperty("veyraOverlayCompatRestart", overlayCompatibility);
    // main launches the replacement only after UI/engine owners unwind.
    QCoreApplication::exit(42);
}

void QmlPlayerBridge::quit() {
    rememberPosition(true);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) app->quit();
}

void QmlPlayerBridge::openCaptureDialog() { emit navigate(QStringLiteral("capture")); }
void QmlPlayerBridge::openPs5Dialog() { emit navigate(QStringLiteral("ps5")); }
void QmlPlayerBridge::openScreenCaptureDialog() { emit navigate(QStringLiteral("screen")); }
void QmlPlayerBridge::openAudioDialog() { emit navigate(QStringLiteral("audio")); }
void QmlPlayerBridge::openSubtitleDialog() { emit navigate(QStringLiteral("subtitle")); }

// --- chain editing ----------------------------------------------------------
// The validator is the authority: an edit that would put frame generation or
// RTX Video HDR where the pipeline cannot honour them is refused with a reason,
// not silently dropped.
int QmlPlayerBridge::addEffect(const QString& type) {
    // The generic list entry means "add an available FG stage". A saved,
    // disabled NVIDIA choice must not make FSR/XeSS impossible to add on AMD.
    if(type==QLatin1String("frame-generation")&&
       !effectAvailability(type).value("available").toBool()){
        for(const auto& choice:fgBackendChoices()){
            const auto row=choice.toMap();if(row.value("disabled").toBool())continue;
            return addEffect(row.value("id").toString()+QStringLiteral("-fg"));
        }
    }
    const auto cap = effectAvailability(type);
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return -1; }
    const bool fgType=type==QLatin1String("dlss-fg")||type==QLatin1String("xess-fg")||
        type==QLatin1String("fsr3-fg")||type==QLatin1String("fsr4-fg")||type==QLatin1String("vfg-fg");
    const auto fgBackend=type==QLatin1String("vfg-fg")?engine::FrameGenerationBackend::Vfg:type==QLatin1String("fsr4-fg")?engine::FrameGenerationBackend::Fsr4:
        type==QLatin1String("fsr3-fg")?engine::FrameGenerationBackend::Fsr:
        type==QLatin1String("xess-fg")?engine::FrameGenerationBackend::XeSS:engine::FrameGenerationBackend::Dlss;
    const auto fgCeiling=engine::fsrFrameGeneration(fgBackend)?2u:
        fgBackend==engine::FrameGenerationBackend::XeSS?uint32_t(xessCeiling(impl_->snapshot)):fgBackend==engine::FrameGenerationBackend::Vfg?8u:6u;
    if(fgType&&fgBackend==engine::FrameGenerationBackend::Vfg&&(!vfgHardwarePossible()||!pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring()))){
        emit notice(!vfgHardwarePossible()?tr("VFG 需要 NVIDIA Ada / Blackwell 显卡（RTX 40 / 50）"):tr("未找到 VFG 运行组件，请在组件页核对路径"),true);return -1;
    }
    if (impl_->chain.mode == engine::ChainMode::Node) {
        const bool fg = fgType;
        const engine::EffectInfo* info = nullptr;
        for (const auto& item : engine::effectCatalog())
            if ((fg && item.type == engine::EffectType::FrameGeneration) || utf8Of(item.id) == type) info = &item;
        if (!info || info->hidden || info->type == engine::EffectType::Protection) {
            emit notice(tr("节点模式不支持该效果类型"), true); return -1;
        }
        auto& c = impl_->chain;
        auto before = std::make_unique<engine::EffectChain>(c);
        const auto oldLayout = impl_->layout; const auto previous = impl_->facade.pendingSettings();
        const auto oldGlobals = impl_->draftGlobals;
        auto pending = settings();
        if (fg){pending.frameGenerationBackend=fgBackend;c.fgMultiplier=normalizeFgMultiplier(c.fgMultiplier,fgBackend,fgCeiling);}
        int index = -1;
        if (fg) for (uint32_t i = 0; i < c.nodeCount; ++i)
            if (c.nodes[i].type == engine::EffectType::FrameGeneration) index = int(i);
        if (index < 0) {
            if (c.nodeCount >= engine::kMaxChainNodes || c.countOf(info->type) >= info->maxInstances ||
                impl_->layout.nextId == std::numeric_limits<uint32_t>::max()) {
                emit notice(tr("该类型节点已达上限（含禁用和未连接节点）"), true); return -1;
            }
            index = int(c.nodeCount++);
            auto& node = c.nodes[index]; node = engine::ChainNode{}; node.type = info->type;
            node.enabled = info->type != engine::EffectType::Beauty;   // beauty starts switched off
            node.viewX = float(index % 4) * 260; node.viewY = float(index / 4) * 300;
            if (info->type == engine::EffectType::NrEnhance) {
                node.nr.runtime = previous.nrRuntime; node.nr.lowLatencyPairing = previous.lowLatency;
            }
            const uint32_t id = impl_->layout.nextId++; impl_->layout.ids[index] = id; impl_->layout.next[index] = 0;
            auto projected = std::make_unique<engine::EffectChain>();
            if (oldLayout.project(*before, *projected).accepted) {
                uint32_t predecessor = 0, next = impl_->layout.inputNext;
                while (next > 1) {
                    const int at = impl_->layout.indexOf(c, next);
                    const auto t = c.nodes[at].type;
                    if (t == engine::EffectType::FrameGeneration ||
                        (info->type != engine::EffectType::FrameGeneration && t == engine::EffectType::VideoHdr)) break;
                    predecessor = next; next = impl_->layout.next[at];
                }
                if (predecessor == 0) impl_->layout.inputNext = id;
                else impl_->layout.next[size_t(impl_->layout.indexOf(c, predecessor))] = id;
                impl_->layout.next[index] = next;
            }
        } else c.nodes[index].enabled = true;
        impl_->draftGlobals = engine::ChainGlobalSettings::capture(pending);
        if (!impl_->revalidate(true, &previous)) {
            const auto error = impl_->validation.message; c = *before; impl_->layout = oldLayout;
            impl_->draftGlobals = oldGlobals;
            impl_->facade.setPending(previous); impl_->revalidate(); emit notice(uiText(error), true); return -1;
        }
        if (info->type == engine::EffectType::NrEnhance) impl_->selectedNr = index;
        if (info->type == engine::EffectType::Color) impl_->selectedColour = index;
        emitChainChanged(); emit settingsChanged(); return index;
    }
    if(fgType) {
        const auto before=impl_->chain;const auto previous=settings();
        auto& c=impl_->chain;
        int slot=-1;
        for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::FrameGeneration)slot=int(i);
        if(slot<0) {
            if(c.nodeCount>=engine::kMaxChainNodes){emit notice(tr("效果链已满"),true);return -1;}
            slot=int(c.nodeCount++);c.nodes[slot]={};c.nodes[slot].type=engine::EffectType::FrameGeneration;
            c.nodes[slot].viewX=float(slot%4)*260;c.nodes[slot].viewY=float(slot/4)*300;
        }
        c.nodes[slot].enabled=true;
        c.fgMultiplier=normalizeFgMultiplier(c.fgMultiplier,fgBackend,fgCeiling);
        auto next=previous;engine::fromChain(c,next);
        next.frameGenerationBackend=fgBackend;
        impl_->validation=engine::validateChain(c);
        if(!impl_->validation.accepted||!next.validate().empty()||
           !unavailableEffects(next).isEmpty()||
           !impl_->facade.applySettings(next,engine::runtimeOrder(c))) {
            c=before;impl_->facade.setPending(previous);impl_->validation=engine::validateChain(c);
            emit notice(tr("补帧选择未被接受，保留原设置；请检查倍率和后端能力"),true);return -1;
        }
        impl_->options=engine::PlayerOptions::from(next,engine::runtimeOrder(c),&impl_->options);
        emit settingsChanged();emitChainChanged();return slot;
    }
    engine::EffectType wanted = engine::EffectType::NrEnhance;
    bool found = false;
    for (const auto& info : engine::effectCatalog()) {
        if (!info.hidden && utf8Of(info.id) == type) {
            wanted = info.type;
            found = true;
            break;
        }
    }
    if (!found) { emit notice(tr("未知的效果: %1").arg(type), true); return -1; }
    if (impl_->chain.mode == engine::ChainMode::Node && wanted == engine::EffectType::Protection) {
        emit notice(tr("节点模式已取消 NR 保护区域；请在列表模式使用全局保护"), true); return -1;
    }

    auto& c = impl_->chain;
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return -1; }
    const auto& info = engine::effectInfo(wanted);
    if (c.countOf(wanted) >= info.maxInstances) {
        emit notice(tr("%1 最多 %2 个").arg(uiText(info.label)).arg(info.maxInstances), true);
        return -1;
    }

    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    // Insert before a pinned tail so the validator never has to undo the edit:
    // frame generation stays last and RTX Video HDR stays in front of it.
    uint32_t insertAt = c.nodeCount;
    if (wanted != engine::EffectType::FrameGeneration) {
        for (uint32_t i = 0; i < c.nodeCount; ++i) {
            const auto& nodeInfo = engine::effectInfo(c.nodes[i].type);
            if (nodeInfo.mustBeLast||((wanted==engine::EffectType::Color||wanted==engine::EffectType::Beauty)&&c.nodes[i].type==engine::EffectType::VideoHdr)||
                (wanted==engine::EffectType::NrEnhance&&
                (c.nodes[i].type==engine::EffectType::Protection||c.nodes[i].type==engine::EffectType::VideoHdr))) { insertAt = i; break; }
        }
    }
    for (uint32_t i = c.nodeCount; i > insertAt; --i) c.nodes[i] = c.nodes[i - 1];
    if(impl_->selectedNr>=int(insertAt))++impl_->selectedNr;
    if(impl_->selectedColour>=int(insertAt))++impl_->selectedColour;
    c.nodes[insertAt] = engine::ChainNode{};
    c.nodes[insertAt].type = wanted;
    // A new list-mode NR layer arrives switched off: adding a layer must not
    // change the picture until the user turns it on.
    c.nodes[insertAt].enabled = !(wanted == engine::EffectType::NrEnhance && c.mode == engine::ChainMode::List);
    // A beauty layer arrives with its switch and both sub-functions off: adding one
    // never changes the picture (its presets turn it on).
    if (wanted == engine::EffectType::Beauty) c.nodes[insertAt].enabled = false;
    if(wanted==engine::EffectType::Color)impl_->selectedColour=int(insertAt);
    if(wanted==engine::EffectType::NrEnhance){
        c.nodes[insertAt].nr.runtime=settings().nrRuntime;
        c.nodes[insertAt].nr.lowLatencyPairing=settings().lowLatency;
        impl_->selectedNr=int(insertAt);
    }
    // A new node lands below the others in node mode, in a stable column.
    c.nodes[insertAt].viewX = 40.0f + float(insertAt % 4) * 260.0f;
    c.nodes[insertAt].viewY = 40.0f + float(insertAt / 4) * 200.0f;
    ++c.nodeCount;

    const bool live=c.mode==engine::ChainMode::Node||wanted==engine::EffectType::NrEnhance||wanted==engine::EffectType::Color;
    if(!impl_->revalidate(live)){
        const auto error=impl_->validation.message;c=before;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return -1;
    }
    emitChainChanged();
    if (!impl_->validation.accepted) {
        emit notice(uiText(impl_->validation.message), true);
    } else {
        emit settingsChanged();
    }
    return int(insertAt);
}

QVariantList QmlPlayerBridge::nodeConnections() const {
    QVariantList out;
    if (impl_->chain.mode != engine::ChainMode::Node) return out;
    const auto add = [&](uint32_t from, uint32_t to) {
        if (to != engine::NodeGraphLayout::Input)
            out << QVariantMap{{"from", from}, {"to", to}};
    };
    add(0, impl_->layout.inputNext);
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        add(impl_->layout.ids[i], impl_->layout.next[i]);
    return out;
}

int QmlPlayerBridge::nodeIndexForId(uint id) const {
    return impl_->chain.mode == engine::ChainMode::Node ? impl_->layout.indexOf(impl_->chain, id) : -1;
}

int QmlPlayerBridge::duplicateNode(uint id) {
    if (impl_->chain.mode != engine::ChainMode::Node) return -1;
    auto before = std::make_unique<engine::EffectChain>(impl_->chain);
    const auto layout = impl_->layout;
    uint32_t created = 0;
    const auto result = impl_->layout.duplicate(impl_->chain, id, created);
    if (!result.accepted) { emit notice(uiText(result.message), true); return -1; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain = *before; impl_->layout = layout; impl_->revalidate();
        emit notice(uiText(error), true); return -1;
    }
    const int index = nodeIndexForId(created);
    if (impl_->chain.nodes[index].type == engine::EffectType::NrEnhance) impl_->selectedNr = index;
    if (impl_->chain.nodes[index].type == engine::EffectType::Color) impl_->selectedColour = index;
    emitChainChanged(); emit settingsChanged(); return index;
}

bool QmlPlayerBridge::resetNode(uint id) {
    const int index = nodeIndexForId(id);
    if (index < 0) return false;
    auto& node = impl_->chain.nodes[index];
    const auto before = node; const auto multiplier = impl_->chain.fgMultiplier;
    const auto strict = impl_->chain.fgStrictAdmission;
    const auto previous = impl_->facade.pendingSettings();
    const auto oldGlobals = impl_->draftGlobals;
    node = engine::ChainNode{};
    node.type = before.type; node.enabled = before.enabled;
    node.viewX = before.viewX; node.viewY = before.viewY;
    if (node.type == engine::EffectType::FrameGeneration) {
        impl_->chain.fgMultiplier = 2; impl_->chain.fgStrictAdmission = false;
    }
    if (node.type == engine::EffectType::SuperResolution) {
        const engine::EnhancementSettings defaults;
        impl_->draftGlobals.srTarget = defaults.srTarget;
        impl_->draftGlobals.videoSrQuality = defaults.videoSrQuality;
        impl_->draftGlobals.dlssSrModel = defaults.dlssSrModel;
    }
    if (!impl_->revalidate(true, &previous)) {
        const auto error = impl_->validation.message; node = before;
        impl_->draftGlobals = oldGlobals;
        impl_->facade.setPending(previous);
        impl_->chain.fgMultiplier = multiplier; impl_->chain.fgStrictAdmission = strict; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::connectNodes(uint from, uint to) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.connect(impl_->chain, from, to);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::disconnectNode(uint from) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.disconnect(impl_->chain, from);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::insertNodeAfter(uint id, uint after) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.insertAfter(impl_->chain, id, after);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

int QmlPlayerBridge::duplicateNrLayer(int index){
    if (impl_->chain.mode == engine::ChainMode::Node)
        return index >= 0 && uint32_t(index) < impl_->chain.nodeCount ? duplicateNode(impl_->layout.ids[index]) : -1;
    auto& c=impl_->chain;
    if(index<0||uint32_t(index)>=c.nodeCount||c.nodes[index].type!=engine::EffectType::NrEnhance||
       c.countOf(engine::EffectType::NrEnhance)>=engine::kMaxNrInstances||c.nodeCount>=engine::kMaxChainNodes)return -1;
    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    const auto original=c.nodes[index];
    for(uint32_t i=c.nodeCount;i>uint32_t(index+1);--i)c.nodes[i]=c.nodes[i-1];
    c.nodes[index+1]=original;++c.nodeCount;impl_->selectedNr=index+1;
    if(impl_->selectedColour>index)++impl_->selectedColour;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c=before;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return -1;
    }
    emitChainChanged();emit settingsChanged();return index+1;
}

bool QmlPlayerBridge::removeEffect(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    const auto& info = engine::effectInfo(c.nodes[index].type);
    if (info.mustBeLast) { emit notice(tr("补帧固定为最后一步，不能删除"), true); return false; }
    const bool live=c.mode==engine::ChainMode::Node||c.nodes[index].type==engine::EffectType::NrEnhance||c.nodes[index].type==engine::EffectType::Color;
    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    const auto previousLayout = impl_->layout;
    if (c.mode == engine::ChainMode::Node) {
        const auto result = impl_->layout.remove(c, impl_->layout.ids[index]);
        if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    } else {
        for (uint32_t i = uint32_t(index); i + 1 < c.nodeCount; ++i) c.nodes[i] = c.nodes[i + 1];
        --c.nodeCount;
    }
    if(impl_->selectedNr==index)impl_->selectedNr=-1;
    else if(impl_->selectedNr>index)--impl_->selectedNr;
    if(impl_->selectedColour==index)impl_->selectedColour=-1;
    else if(impl_->selectedColour>index)--impl_->selectedColour;
    if(!impl_->revalidate(live)){
        const auto error=impl_->validation.message;c=before;impl_->layout=previousLayout;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return false;
    }
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::moveEffect(int from, int to) {
    auto& c = impl_->chain;
    if (from < 0 || to < 0 || uint32_t(from) >= c.nodeCount || uint32_t(to) >= c.nodeCount) return false;
    if (c.mode == engine::ChainMode::Node) {
        if (from == to) return true;
        return insertNodeAfter(impl_->layout.ids[from], impl_->layout.ids[to]);
    }
    // The two pinned stages cannot be dragged; the UI greys them, and the
    // engine refuses them too so a script cannot route around the UI.
    if (engine::effectInfo(c.nodes[from].type).mustBeLast ||
        engine::effectInfo(c.nodes[to].type).mustBeLast) {
        emit notice(tr("补帧固定为最后一步，不能拖动"), true);
        return false;
    }
    const auto before = c;
    const auto moving = c.nodes[from];
    if (from < to) for (int i = from; i < to; ++i) c.nodes[i] = c.nodes[i + 1];
    else for (int i = from; i > to; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[to] = moving;

    impl_->revalidate(true);
    if (!impl_->validation.accepted) {
        const auto error=impl_->validation.message;
        impl_->chain = before;
        impl_->revalidate();
        emit notice(uiText(error), true);
        return false;
    }
    const auto remap=[&](int i){
        if(i==from)return to;
        if(from<to&&i>from&&i<=to)return i-1;
        if(from>to&&i>=to&&i<from)return i+1;
        return i;
    };
    impl_->selectedNr=remap(impl_->selectedNr);
    impl_->selectedColour=remap(impl_->selectedColour);
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::setEffectEnabled(int index, bool enabled) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    if (c.nodes[index].enabled == enabled) return true;
    c.nodes[index].enabled = enabled;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c.nodes[index].enabled=!enabled;
        impl_->revalidate();emit notice(uiText(error),true);return false;
    }
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::nrAnyEnabled() const {
    const auto& c=impl_->chain;
    for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::NrEnhance&&c.nodes[i].enabled)return true;
    return false;
}
bool QmlPlayerBridge::setAllNrEnabled(bool enabled) {
    auto& c=impl_->chain;const auto before=c;
    std::vector<uint32_t> layers;
    for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::NrEnhance)layers.push_back(i);
    if(layers.empty())return false;
    auto& restore=impl_->nrMasterRestore;
    if(!enabled){
        restore.clear();
        for(const auto i:layers){restore.push_back(c.nodes[i].enabled);c.nodes[i].enabled=false;}
    }else{
        // Layers added or removed since the switch went off: no faithful restore,
        // so every layer comes back on. Same when nothing was on to begin with.
        const bool faithful=restore.size()==layers.size()&&std::find(restore.begin(),restore.end(),true)!=restore.end();
        for(size_t k=0;k<layers.size();++k)c.nodes[layers[k]].enabled=faithful?bool(restore[k]):true;
    }
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c=before;impl_->revalidate();
        emit notice(uiText(error),true);emitChainChanged();return false;
    }
    veyra::log::info("ui-nr",std::format("master switch {} layers={}",enabled?"on":"off",layers.size()));
    emitChainChanged();emit settingsChanged();
    return true;
}

QVariantMap QmlPlayerBridge::nodeAnchors() const { return impl_->anchors; }
bool QmlPlayerBridge::setNodeAnchor(const QString& key, double x, double y) {
    if ((key != QLatin1String("input") && key != QLatin1String("flow") && key != QLatin1String("output")) ||
        !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000) return false;
    impl_->anchors[key] = QVariantMap{{"x", x}, {"y", y}};
    if (!impl_->anchorsFile.empty()) {
        QFile file(QString::fromStdWString(impl_->anchorsFile.wstring()));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(QJsonObject::fromVariantMap(impl_->anchors)).toJson(QJsonDocument::Compact));
        else veyra::log::warn("ui-node", "node anchor positions not saved");
    }
    emit nodeAnchorsChanged();
    return true;
}
bool QmlPlayerBridge::setEffectPosition(int index, double x, double y) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || !std::isfinite(x) || !std::isfinite(y) ||
        std::abs(x) > 1000000 || std::abs(y) > 1000000) return false;
    c.nodes[index].viewX = float(x);
    c.nodes[index].viewY = float(y);
    // Positions never affect the picture, so this deliberately does not
    // revalidate or touch the engine: dragging a node must not cost a rebuild.
    emitChainChanged();
    return true;
}

QVariantMap QmlPlayerBridge::effectDescriptor(const QString& type) const {
    for (const auto& info : engine::effectCatalog()) {
        if (utf8Of(info.id) != type) continue;
        QVariantMap out;
        out["id"] = utf8Of(info.id);
        out["label"] = uiText(info.label);
        out["maxInstances"] = int(info.maxInstances);
        out["repeatable"] = info.repeatable;
        out["mustBeLast"] = info.mustBeLast;
        out["justBeforeLast"] = info.justBeforeLast;
        out["changesResolution"] = info.changesResolution;
        out["experimental"] = info.experimental;
        return out;
    }
    return {};
}

// --- presets ----------------------------------------------------------------
bool QmlPlayerBridge::applyPresetIndex(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const auto mode = impl_->chain.mode;
    const auto& entry = impl_->facade.presets().entries()[size_t(index)];
    if (entry.kind != mode) { emit notice(tr("请先切换到预设对应的编辑模式"), true); return false; }
    const auto previous = impl_->facade.pendingSettings();
    const auto oldGlobals = impl_->draftGlobals;
    auto nextGlobals = oldGlobals;
    auto settings = previous;
    auto candidate = impl_->chain;
    const auto oldLayout = impl_->layout;
    auto nextLayout = oldLayout;
    engine::ChainValidation validation;
    if (mode == engine::ChainMode::Node) {
        auto document = std::make_unique<engine::NodeEditorDocument>();
        document->nodes = candidate; document->layout = nextLayout; document->globals = oldGlobals;
        validation = engine::PresetLibrary::applyToEditor(entry, *document, settings);
        candidate = document->nodes; nextLayout = document->layout;
        nextGlobals = document->globals.value_or(engine::ChainGlobalSettings::capture(settings));
        // Keep the facade on accepted values until revalidate projects the new
        // document. Audio/non-chain preset fields are retained in this copy.
        engine::fromChain(impl_->activeRuntime(), settings);
        // Custom: the static HDR output tuning rides in the globals block this call
        // re-derives, so preserve the values this commit is carrying.
        const auto keepCurve = settings.hdrCurve;
        const auto keepMetadata = settings.hdrMetadata;
        const bool keepDoviFollow = settings.doviFollow;
        engine::ChainGlobalSettings::capture(previous).apply(settings);
        settings.hdrCurve = keepCurve; settings.hdrMetadata = keepMetadata;
        settings.doviFollow = keepDoviFollow;
    } else validation = engine::PresetLibrary::applyToChain(entry, candidate, settings);
    if (!validation.accepted) { emit notice(uiText(validation.message), true); return false; }
    if (const auto error = settings.validate(); !error.empty()) {
        veyra::log::error("qml-preset", error);
        emit notice(tr("预设参数无效，已保留原状态"), true); return false;
    }
    // Partial presets do not change the current editing target. Colour-only
    // insertion/removal can shift NR indices, so retain the instance ordinal.
    const auto remapSelection = [&](int selected, engine::EffectType type) {
        if (entry.contents & engine::presetContentMask(engine::PresetContent::Chain)) return -1;
        if (selected < 0) return -1;
        uint32_t ordinal = 0;
        for (int i = 0; i < selected; ++i) if (impl_->chain.nodes[i].type == type) ++ordinal;
        for (uint32_t i = 0; i < candidate.nodeCount; ++i) {
            if (candidate.nodes[i].type != type) continue;
            if (ordinal == 0) return int(i);
            --ordinal;
        }
        return -1;
    };
    const int selectedNr = remapSelection(impl_->nrIndex(), engine::EffectType::NrEnhance);
    const int selectedColour = remapSelection(impl_->colourIndex(), engine::EffectType::Color);
    auto oldChain = std::make_unique<engine::EffectChain>(impl_->chain);
    impl_->chain = candidate;
    impl_->layout = nextLayout;
    if (mode == engine::ChainMode::Node) {
        impl_->draftGlobals = nextGlobals;
        engine::ChainGlobalSettings::capture(previous).apply(settings);
    }
    impl_->facade.setPending(settings);
    if (!impl_->revalidate(true, &previous)) {
        const auto error = impl_->validation.message; impl_->chain = *oldChain; impl_->layout = oldLayout;
        impl_->draftGlobals = oldGlobals;
        impl_->facade.setPending(previous); impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    impl_->selectedNr = selectedNr;
    impl_->selectedColour = selectedColour;
    if (entry.flow && (entry.contents & engine::presetContentMask(engine::PresetContent::Flow))) {
        impl_->prefs[QStringLiteral("contentRate")] = int(this->settings().content);
        if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "preset content cadence not saved");
    }
    emit settingsChanged();
    emitChainChanged();
    return true;
}

void QmlPlayerBridge::enqueueExportFile(const QString& input,const QString& output) {
    if(!QFileInfo(input).isFile()){emit notice(tr("队列输入文件不存在"),true);return;}
    impl_->exportQueue.addFile(input,output);emit exportChanged();
}
void QmlPlayerBridge::addExportFiles(const QStringList& paths) {
    for(const auto& path:paths){const auto url=QUrl(path);const QString file=url.isLocalFile()?url.toLocalFile():path;
        if(QFileInfo(file).isFile())impl_->exportQueue.addFile(file);
        else emit notice(tr("无法添加文件：%1").arg(file),true);
    }
    emit exportChanged();
}
void QmlPlayerBridge::addExportFilesDialog() {
    addExportFiles(QFileDialog::getOpenFileNames(nullptr,tr("加入导出队列"),QString(),
        tr("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts);;所有文件 (*)")));
}
bool QmlPlayerBridge::selectExportPreset(int index) {
    if (index == -1) {
        impl_->exportPresetName.clear();
    } else {
        const auto& entries = impl_->facade.presets().entries();
        if (index < 0 || size_t(index) >= entries.size() || entries[size_t(index)].kind == engine::ChainMode::Node)
            return false;
        impl_->exportPresetName = entries[size_t(index)].name;
    }
    emit exportChanged();
    return true;
}

bool QmlPlayerBridge::savePresetAs(const QString& name, int contentsMask, bool nodeMode) {
    if (name.isEmpty()) { emit notice(tr("预设需要一个名字"), true); return false; }
    if (nodeMode != (impl_->chain.mode == engine::ChainMode::Node)) {
        emit notice(tr("请先切换到要保存的编辑模式"), true); return false;
    }
    engine::PresetEntry entry;
    entry.name = wideOf(name);
    entry.kind = nodeMode ? engine::ChainMode::Node : engine::ChainMode::List;
    entry.contents = uint32_t(contentsMask);
    entry.chain = impl_->chain;
    entry.globals=engine::ChainGlobalSettings::capture(impl_->facade.pendingSettings());
    entry.color = impl_->facade.pendingSettings().color;
    entry.fg.multiplier = impl_->facade.pendingSettings().multiplier;
    entry.fg.backend = impl_->facade.pendingSettings().frameGenerationBackend;
    entry.fg.vfgQuality = impl_->facade.pendingSettings().vfgQuality;
    entry.audioSync = impl_->facade.pendingSettings().audioSync;
    entry.audioOffsetMs = impl_->facade.pendingSettings().audioOffsetMs;
    if (nodeMode) {
        entry.nodeConfiguration = impl_->currentSession().configurations[1];
        entry.chain = entry.nodeConfiguration->chain;
        if (const auto* color = impl_->chain.firstOf(engine::EffectType::Color)) {
            entry.color = color->color; entry.color.enabled = color->enabled;
        }
        entry.fg.multiplier = uint32_t(fgMultiplier());
        entry.fg.backend = impl_->draftGlobals.fgBackend;
        entry.fg.vfgQuality = impl_->draftGlobals.vfgQuality;
        entry.globals=impl_->draftGlobals;
    }
    entry.flow = engine::PresetFlowSettings::capture(settings());
    const bool ok = impl_->facade.savePreset(entry, false);
    if (ok) emit presetsChanged();
    else emit notice(tr("保存预设失败: %1").arg(uiText(impl_->facade.error())), true);
    return ok;
}

bool QmlPlayerBridge::deletePreset(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const auto& entry = impl_->facade.presets().entries()[size_t(index)];
    if (entry.builtin) { emit notice(tr("内置预设不能删除，可以另存一份"), true); return false; }
    const bool selected = impl_->exportPresetName == entry.name;
    const bool ok = impl_->facade.presets().erase(size_t(index));
    if (ok) {
        if (selected) { impl_->exportPresetName.clear(); emit exportChanged(); }
        emit presetsChanged();
    }
    return ok;
}

bool QmlPlayerBridge::renamePreset(int index, const QString& name) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool selected = impl_->exportPresetName == impl_->facade.presets().entries()[size_t(index)].name;
    const bool ok = impl_->facade.presets().rename(size_t(index), wideOf(name));
    if (ok) {
        if (selected) { impl_->exportPresetName = wideOf(name); emit exportChanged(); }
        emit presetsChanged();
    }
    return ok;
}

QVariantList QmlPlayerBridge::presetSaveParts() const {
    // What a preset would contain right now, part by part, so the save dialog can
    // list it before the user names anything. `meaningful` marks the parts that are
    // not at defaults, which is what makes "will be saved" honest.
    const auto s = settings();
    QVariantList out;
    auto add = [&](const char* id, const QString& label, const QString& summary, bool meaningful) {
        QVariantMap item;
        item["id"] = QString::fromUtf8(id);
        item["label"] = label;
        item["summary"] = summary;
        item["meaningful"] = meaningful;
        out << item;
    };
    QStringList chainParts;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        chainParts << uiText(engine::effectInfo(impl_->chain.nodes[i].type).label);
    add("chain", tr("效果链"),
        chainParts.isEmpty() ? tr("空") : chainParts.join(QStringLiteral(" → ")),
        impl_->chain.nodeCount > 0);
    add("color", tr("色彩"), s.color.enabled ? tr("已启用") : tr("未启用"), s.color.enabled);
    add("fg", tr("补帧"),
        s.multiplier > 1 ? QStringLiteral("%1X").arg(s.multiplier) : tr("关闭", "off"),
        s.multiplier > 1);
    add("audio", tr("声音"),
        s.audioOffsetMs != 0 ? tr("偏移 %1 ms").arg(s.audioOffsetMs) : tr("默认"),
        s.audioOffsetMs != 0);
    const QStringList flowNames{QStringLiteral("NVIDIA NVOF"), tr("AMD 光流"), QStringLiteral("GPU DIS")};
    const QStringList qualityNames{tr("性能"), tr("平衡"), tr("质量")};
    const QStringList cadenceNames{tr("采用源时间戳"), tr("自动识别内容节奏"), tr("识别 30fps 内容"),
        tr("识别 50fps 内容"), tr("识别 60fps 内容"), tr("采集 60→30（PS5 30 帧）")};
    QString flowSummary = flowNames.value(int(s.opticalFlowBackend)) + QStringLiteral(" · ") +
        qualityNames.value(int(s.flow)) + QStringLiteral(" · ") + cadenceNames.value(int(s.content));
    if (s.opticalFlowBackend == engine::OpticalFlowBackend::AmdFidelityFx && s.amdFlowHalfResolution)
        flowSummary += QStringLiteral(" · ") + tr("半分辨率");
    add("flow", tr("光流与内容节奏"), flowSummary,
        engine::PresetFlowSettings::capture(s) != engine::PresetFlowSettings{});
    return out;
}

int QmlPlayerBridge::defaultPresetIndex() const {
    const auto index = impl_->facade.presets().defaultIndex();
    return index.has_value() ? int(*index) : -1;
}

bool QmlPlayerBridge::setDefaultPreset(int index) {
    if (index < -1 || (index >= 0 && size_t(index) >= impl_->facade.presets().entries().size())) return false;
    const bool ok = index == -1 ? impl_->facade.presets().clearDefault()
                                : impl_->facade.presets().setDefault(size_t(index));
    if (ok) emit presetsChanged();
    else emit notice(tr("默认预设保存失败：") + uiText(impl_->facade.presets().error()), true);
    return ok;
}

bool QmlPlayerBridge::duplicatePreset(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool ok = impl_->facade.presets().duplicate(size_t(index));
    if (ok) emit presetsChanged();
    return ok;
}

void QmlPlayerBridge::refreshRecentFiles() {
    impl_->facade.refreshRecentFiles();
    emit recentFilesChanged();
}
void QmlPlayerBridge::clearRecentFiles() {
    // The facade owns the list; clearing it means dropping the entries it holds
    // and re-persisting, which refreshRecentFiles already does after the fact.
    impl_->facade.clearRecentFiles();
    emit recentFilesChanged();
}


// --- audio sync (1.4.4 声音补偿, #12) ------------------------------------------
int QmlPlayerBridge::audioSyncMode() const { return int(settings().audioSync); }
void QmlPlayerBridge::setAudioSyncMode(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings();
    if (int(s.audioSync) == value) return;
    s.audioSync = static_cast<engine::AudioSyncMode>(value);
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("audioSync")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "sound sync mode not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::audioSyncLive() const {
    const auto& s = impl_->snapshot;
    return s.running && (s.capture || s.remotePlay);
}
double QmlPlayerBridge::audioCompensationMs() const {
    return audioSyncLive() ? impl_->snapshot.captureAudio.compensationMs : 0.0;
}

// --- performance orbs and status light (#13) --------------------------------
double QmlPlayerBridge::gpuUtilization() const { return std::max(0.0, impl_->gpuUtil.load()); }
bool QmlPlayerBridge::gpuUtilizationKnown() const { return impl_->gpuUtil.load() >= 0.0; }
QString QmlPlayerBridge::runStatus() const { return impl_->runStatus; }
QString QmlPlayerBridge::runStatusLevel() const { return impl_->runLevel; }
QString QmlPlayerBridge::runStatusDetail() const { return impl_->runDetail; }
QStringList QmlPlayerBridge::runOptions() const { return impl_->runOptions; }
double QmlPlayerBridge::effectiveOutputFps() const { return impl_->effectiveOutputFps; }
double QmlPlayerBridge::outputRateRatio() const { return impl_->rateRatio; }

void QmlPlayerBridge::startGpuSampler() {
    // Task Manager's source: the "GPU Engine" utilization counters, summed per
    // engine type across processes, busiest type wins. Sampled once a second on
    // its own thread; -1 means the counters are unavailable.
    auto* impl = impl_.get();
    impl->gpuSampler = std::jthread([impl](std::stop_token stop) {
        PDH_HQUERY query = nullptr; PDH_HCOUNTER counter = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query); veyra::log::warn("perf", "GPU engine counters unavailable"); return;
        }
        PdhCollectQueryData(query);
        std::vector<unsigned char> buffer;
        while (!stop.stop_requested()) {
            for (int i = 0; i < 10 && !stop.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop.stop_requested()) break;
            if (PdhCollectQueryData(query) != ERROR_SUCCESS) continue;
            DWORD bytes = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA) continue;
            buffer.resize(bytes);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) continue;
            std::map<std::wstring, double> byType;
            // Instance names carry the adapter: pid_<n>_luid_0x<high>_0x<low>_phys_..._engtype_<type>.
            const uint64_t luid = impl->gpuMonitorLuid.load();
            const std::wstring luidKey = luid ? std::format(L"luid_0x{:08x}_0x{:08x}_", uint32_t(luid >> 32), uint32_t(luid)) : std::wstring{};
            for (DWORD i = 0; i < count; ++i) {
                if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
                std::wstring name = items[i].szName ? items[i].szName : L"";
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) { return wchar_t(towlower(ch)); });
                if (!luidKey.empty() && name.find(luidKey) == std::wstring::npos) continue;
                const auto at = name.find(L"engtype_");
                byType[at == std::wstring::npos ? std::wstring{} : name.substr(at + 8)] += items[i].FmtValue.doubleValue;
            }
            double busiest = 0;
            for (const auto& [type, value] : byType) busiest = std::max(busiest, value);
            impl->gpuUtil = std::clamp(busiest, 0.0, 100.0);
        }
        PdhCloseQuery(query);
    });
}

void QmlPlayerBridge::updateRunStatus() {
    auto& i = *impl_;
    const auto& s = i.snapshot;
    // QScreen's cached rate for the actual video host, not an arbitrary popup
    // or another monitor. This uses the existing bounded status cadence.
    double refresh=0;
    const HWND owner=i.videoWindow?GetAncestor(i.videoWindow,GA_ROOT):nullptr;
    for(auto* window:QGuiApplication::topLevelWindows())
        if(window->isVisible()&&window->screen()&&reinterpret_cast<HWND>(window->winId())==owner){refresh=window->screen()->refreshRate();break;}
    i.history.sample(s,{refresh,i.compareMode!=0});
    const auto& rate=i.history.output;
    const auto& f = s.metrics.flow;
    const bool active = s.running && !s.image && s.transport == engine::TransportState::Playing;
    QString status = tr("待机"), level = QStringLiteral("idle");
    if (s.failed) { status = tr("错误"); level = QStringLiteral("err"); }
    else if (s.remoteRecovering) { status = tr("恢复中"); level = QStringLiteral("warn"); }
    // Paused to buffer a network source (#48): no pictures are due, so the output rate is not judged.
    else if (s.running && s.network.active && (s.network.holding() || s.network.state == media::NetworkStats::State::Reconnecting)) {
        status = tr("缓冲中"); level = QStringLiteral("warn");
    }
    else if (active && !s.applying && f.rateWindowReady) {
        // Judged on the engine's own words, shown in the interface language.
        const std::wstring rate = i.history.rateStatus(s);
        status = uiText(rate);
        level = rate == L"正常" ? QStringLiteral("ok") : QStringLiteral("warn");
    }
    else if (s.applying && s.running) { status = tr("调整中"); level = QStringLiteral("warn"); }
    else if (active) status = tr("采样中");
    else if (s.transport == engine::TransportState::Paused) status = tr("已暂停");
    QStringList options;
    const auto chosen=settings();
    if(chosen.content!=engine::ContentRate::Transport){
        QString cadence;
        switch(chosen.content){
        case engine::ContentRate::Auto:cadence=tr("自动识别内容节奏");break;
        case engine::ContentRate::Fps30:cadence=tr("识别 %1 fps 内容").arg(30);break;
        case engine::ContentRate::Fps50:cadence=tr("识别 %1 fps 内容").arg(50);break;
        case engine::ContentRate::Fps60:cadence=tr("识别 %1 fps 内容").arg(60);break;
        case engine::ContentRate::Capture60To30:cadence=tr("采集 60→30");break;
        default:break;
        }
        if(s.running&&rate.halfRate)options<<tr("%1 · 当前约 %2 fps").arg(cadence).arg(rate.contentFps,0,'f',0);
        else if(s.running&&rate.deduplicating)options<<tr("%1 · 去重后约 %2 fps").arg(cadence).arg(rate.contentFps,0,'f',0);
        else if(s.running&&!s.capture)options<<tr("%1 · 当前来源未去重").arg(cadence);
        else if(s.running)options<<tr("%1 · 尚未观察到去重").arg(cadence);
        else options<<cadence;
    }
    if(i.presentation.display!=engine::DisplaySync::Tearing){
        const auto sync=i.presentation.display==engine::DisplaySync::Vsync?tr("垂直同步"):tr("自动显示同步");
        options<<((refresh>0)?tr("%1 · 屏幕 %2 Hz").arg(sync).arg(refresh,0,'f',1):sync);
    }
    // Variable refresh (NVIDIA G-SYNC / G-SYNC Compatible): the presenting
    // monitor while playing, otherwise the main window's monitor (NVAPI,
    // at most every 2 s). The driver setting is the user's; the 可变刷新
    // switch decides how Veyra presents when it is possible.
    int vrrLevel=int(s.displayVrr);
    if(!s.running){
        const auto now=std::chrono::steady_clock::now();
        if(i.idleVrrQuery==std::chrono::steady_clock::time_point{}||now-i.idleVrrQuery>=std::chrono::seconds(2)){
            HWND host=owner;
            if(!host)if(auto* window=QGuiApplication::focusWindow())host=reinterpret_cast<HWND>(window->winId());
            if(host){i.idleVrr=gfx::queryVrr(host);i.idleVrrQuery=now;}
        }
        vrrLevel=int(gfx::vrrLevel(i.idleVrr));
    }
    if(i.presentation.variableRefresh){
        if(vrrLevel>=4)options<<tr("可变刷新 · 生效中");
        else if(vrrLevel==3)options<<tr("可变刷新 · 等待驱动确认");
        else options<<tr("可变刷新 · 当前屏幕不可用");
    }else if(s.running&&vrrLevel>=4)options<<tr("屏幕可变刷新生效中");
    if(i.presentation.outputRate!=engine::OutputRateMode::Off){
        if(s.running&&s.presentationEffective.outputRate==engine::OutputRateMode::Off)
            options<<tr("输出限帧 · 当前未生效");
        else if(i.presentation.outputRate==engine::OutputRateMode::FollowDisplay)
            options<<tr("输出限帧 · 跟随屏幕");
        else options<<tr("输出限帧 · %1 fps").arg(i.presentation.customFps,0,'f',1);
    }
    if(i.presentation.enabled)options<<(s.running&&!s.presentationEffective.enabled?tr("低延迟队列 · 当前未生效"):tr("低延迟队列"));
    if(!s.capture&&std::abs(s.playbackRate-1.0)>.001)options<<tr("播放速度 %1×").arg(s.playbackRate,0,'f',2);
    if(i.compareMode!=0)options<<tr("画面对比 · 补帧暂停");
    QStringList detail;
    // Live output hint: also shown under the 倍率 control (fgDisplayHint).
    QString fgHint;
    if (s.running && !s.image && s.applied.multiplier > 1 && i.compareMode == 0 && s.displayFgSuggestion > 0 &&
        s.displayFgContentFps > 0 && s.displayFgRefreshHz > 0) {
        const double output = s.displayFgContentFps * s.applied.multiplier;
        fgHint = s.displayFgSuggestion == 1
            ? tr("当前帧率 %1 fps 超出屏幕刷新率 %2 Hz，建议关闭补帧来获得更好的延迟体验")
                  .arg(output, 0, 'f', 0).arg(s.displayFgRefreshHz, 0, 'f', 0)
            : tr("当前帧率 %1 fps 超出屏幕刷新率 %2 Hz，建议降至 %3X 来获得更好的延迟体验")
                  .arg(output, 0, 'f', 0).arg(s.displayFgRefreshHz, 0, 'f', 0).arg(s.displayFgSuggestion);
    }
    if (s.running && !s.image) {
        if (s.applied.multiplier > 1) {
            QString backend;
            switch (s.applied.frameGenerationBackend) {
            case engine::FrameGenerationBackend::Dlss: backend = tr("DLSS"); break;
            case engine::FrameGenerationBackend::XeSS: backend = tr("XeSS"); break;
            case engine::FrameGenerationBackend::Fsr: backend = tr("FSR 3.1"); break;
            case engine::FrameGenerationBackend::Fsr4: backend = tr("FSR 4 ML"); break;
            case engine::FrameGenerationBackend::Vfg: backend = QStringLiteral("NVIDIA VFG"); break;
            }
            // 与作者原版一致：只报实际倍率和后端。自动选倍率是设置项，不需要在状态栏
            // 反复标注 —— 「补帧 4X」已经把倍率说清楚了。
            // Same as the author's original: report the multiplier and the backend only.
            // Automatic selection is a setting, not something to repeat here - "补帧 4X"
            // already says what the multiplier is.
            detail << tr("补帧 %1X · %2").arg(s.applied.multiplier)
                          .arg(backend);
        }
        else detail << tr("未补帧");
        if (s.applied.multiplier > 1 && impl_->compareMode != 0) detail << tr("补帧暂停");
        // Live output hint (DisplayFgBudget.h): a suggestion only, the
        // multiplier stays the user's choice.
        else if (!fgHint.isEmpty()) detail << fgHint;
        else if (s.applied.multiplier > 1 && (s.fgBudgetLimited || s.xessGenerationSuppressed) &&
                 (!rate.policyLimited()||i.history.overloaded)) detail << tr("调度降档");
        // Why VFG cannot hold the requested multiplier. One VFG interpolation
        // costs about the same at any resolution, so what fits follows from its
        // measured cost, the rest of the chain and the source interval; the
        // display refresh caps what can be shown. A hint only: no setting changes.
        if (s.applied.multiplier > 1 && s.applied.frameGenerationBackend == engine::FrameGenerationBackend::Vfg && i.compareMode == 0) {
            const double source = rate.contentFps;
            const auto perRun = f.gpuTiming[size_t(diagnostics::GpuStage::Fg1)].p95;
            const auto batch = f.gpuTiming[size_t(diagnostics::GpuStage::FgBatch)].p95;
            const auto whole = f.enhancementProcessing.p95;
            // A mixed tier runs its first model once per pair (the middle
            // subframe) and the second model for the others; Fg1 is then a
            // second-model subframe whenever the group has more than one.
            const bool mixed = engine::vfgMixedQuality(s.applied.vfgQuality) && s.applied.multiplier > 2;
            const auto middleStage = diagnostics::GpuStage(unsigned(diagnostics::GpuStage::Fg1) + engine::vfgPrimarySubframe(s.applied.multiplier) - 1);
            const auto middle = mixed ? f.gpuTiming[size_t(middleStage)].p95 : perRun;
            if (s.fgBudgetLimited && (!rate.policyLimited()||i.history.overloaded) && source > 1 && perRun && *perRun > 0 && middle && *middle > 0 && batch && whole) {
                const double base = std::max(0.0, *whole - *batch), budget = 1000.0 / source - base;
                // generated = 1 middle + k others; fits = generated + the real frame.
                const int generated = budget < *middle ? 0 : 1 + int(std::floor((budget - *middle) / *perRun));
                const int fits = std::clamp(1 + generated, 1, int(s.applied.multiplier));
                if (fits < int(s.applied.multiplier)) {
                    if (mixed) detail << tr("VFG 中间帧约 %1 ms、其余每张约 %2 ms，当前负载约可稳定 %3X").arg(*middle, 0, 'f', 1).arg(*perRun, 0, 'f', 1).arg(fits);
                    else detail << tr("VFG 每张约 %1 ms，当前负载约可稳定 %2X").arg(*perRun, 0, 'f', 1).arg(fits);
                }
            }
            if (fgHint.isEmpty() && refresh > 1 && source > 1 && source * s.applied.multiplier > refresh * 1.02)
                detail << tr("屏幕 %1 Hz 最多显示约 %2X").arg(refresh, 0, 'f', 0).arg(std::max(1, int(refresh / source)));
        }
        if (s.nominalSourceFps > 0.01) detail << tr("源 %1 fps").arg(s.nominalSourceFps, 0, 'f', 2);
        if(active&&f.rateWindowReady&&rate.targetFps>0)detail<<tr("有效目标 %1 fps").arg(rate.targetFps,0,'f',1);
        const auto autoState=nrAutoStatus();if(!autoState.isEmpty())detail<<autoState;
    }
    double ratio = -1.0;
    if (active && f.rateWindowReady && rate.targetFps>0)ratio=rate.actualFps/rate.targetFps;
    const bool ratioChanged = std::abs(ratio - i.rateRatio) > 0.005;
    i.rateRatio = ratio;
    // Frame-time line: the submit interval while playing (a real display rate is
    // not available here), 48 samples = 12 s at the 250 ms cadence.
    if (active && submitFpsKnown() && submitFps() > 0.5) {
        i.frameTimes.push_back(1000.0 / submitFps());
        while (i.frameTimes.size() > 48) i.frameTimes.pop_front();
    } else if (!s.running && !i.frameTimes.empty()) i.frameTimes.clear();
    const QString text = detail.join(QStringLiteral(" · "));
    const double gpu = i.gpuUtil.load();
    const double target=active&&f.rateWindowReady&&rate.targetFps>0?rate.targetFps:-1;
    const bool changed = ratioChanged || active || status != i.runStatus || level != i.runLevel || text != i.runDetail || options!=i.runOptions || std::abs(target-i.effectiveOutputFps)>.05 || std::abs(gpu - i.gpuPublished) > 0.05 ||
                         fgHint != i.fgDisplayHint || vrrLevel != i.vrrState;
    i.runStatus = status; i.runLevel = level; i.runDetail = text; i.gpuPublished = gpu;
    i.runOptions=options;i.effectiveOutputFps=target;i.fgDisplayHint=fgHint;i.vrrState=vrrLevel;
    if (changed) emit perfChanged();
}

// --- NR protection regions drawn on the picture (#8) ------------------------
namespace {
// Outline of the region being drawn, on a layered child of the video window
// (the swapchain is never read or touched), as 1.4.4's ProtectionOverlay.
void drawProtectionOutline(HWND overlay, HWND video, RECT box, bool ellipse) {
    RECT r{}; GetClientRect(video, &r);
    if (r.right < 1 || r.bottom < 1) return;
    SetWindowPos(overlay, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = r.right;
    info.bmiHeader.biHeight = -r.bottom; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen); void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen); return; }
    auto previous = SelectObject(memory, bitmap);
    memset(bits, 0, size_t(r.right) * size_t(r.bottom) * 4);
    {
        Gdiplus::Bitmap canvas(r.right, r.bottom, r.right * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 138, 61), 2.0f);
        Gdiplus::SolidBrush fill(Gdiplus::Color(40, 255, 138, 61));
        const Gdiplus::Rect rect(box.left, box.top, std::max(1L, box.right - box.left), std::max(1L, box.bottom - box.top));
        if (ellipse) { g.FillEllipse(&fill, rect); g.DrawEllipse(&pen, rect); }
        else { g.FillRectangle(&fill, rect); g.DrawRectangle(&pen, rect); }
    }
    SIZE size{r.right, r.bottom}; POINT origin{}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (UpdateLayeredWindow(overlay, screen, nullptr, &size, memory, &origin, 0, &blend, ULW_ALPHA))
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
}
}

QVariantList QmlPlayerBridge::protectionRegions() const {
    QVariantList out;
    const auto& p = settings().protection;
    for (int i = 0; i < int(p.regions.size()); ++i) {
        const auto& q = p.regions[size_t(i)];
        if (q.empty()) continue;
        out << QVariantMap{{"index", i}, {"left", double(q.left)}, {"top", double(q.top)}, {"right", double(q.right)},
                           {"bottom", double(q.bottom)}, {"ellipse", q.ellipse}};
    }
    return out;
}
QString QmlPlayerBridge::protectionDrawShape() const { return impl_->protDraw; }

bool QmlPlayerBridge::editProtection(const std::function<bool(engine::ChainNode&)>& edit) {
    if (impl_->chain.mode != engine::ChainMode::List) { emit notice(tr("NR 保护区域仅在列表模式可用"), true); return false; }
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i) {
        auto& node = impl_->chain.nodes[i];
        if (node.type != engine::EffectType::Protection) continue;
        const auto before = node; const bool wasValid = chainValid();
        if (!edit(node)) return false;
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged();
        if (before.enabled != node.enabled) emitChainChanged(); else emitParameterChanged(&QmlPlayerBridge::protectionChanged, wasValid);
        return accepted;
    }
    emit notice(tr("处理链中没有 NR 保护节点，未更改设置"), true);
    return false;
}

bool QmlPlayerBridge::beginProtectionDraw(const QString& shape) {
    auto& i = *impl_;
    if (shape.isEmpty()) {
        if (i.protOverlay) ShowWindow(i.protOverlay, SW_HIDE);
        if (!i.protDraw.isEmpty()) { i.protDraw.clear(); emit protectionDrawChanged(); }
        return true;
    }
    if (shape != QLatin1String("rect") && shape != QLatin1String("ellipse")) return false;
    if (i.chain.mode != engine::ChainMode::List) { emit notice(tr("NR 保护区域仅在列表模式可用"), true); return false; }
    if (!i.snapshot.running || !i.videoWindow) { emit notice(tr("请先打开视频，再在画面上绘制保护区域"), true); return false; }
    bool space = false;
    for (const auto& q : settings().protection.regions) space |= q.empty();
    if (!space) { emit notice(tr("最多 4 个保护区域，请先删除一个"), true); return false; }
    i.protDraw = shape;
    emit protectionDrawChanged();
    return true;
}

void QmlPlayerBridge::updateProtectionDraw(double x0, double y0, double x1, double y1) {
    auto& i = *impl_;
    if (i.protDraw.isEmpty() || !i.videoWindow) return;
    if (!i.protOverlay) { ensureGdiplus(); i.protOverlay = createSubtitleOverlay(i.videoWindow); }
    if (!i.protOverlay) return;
    RECT client{}; GetClientRect(i.videoWindow, &client);
    const auto px = [&](double f, LONG extent) { return LONG(std::lround(std::clamp(f, 0.0, 1.0) * extent)); };
    const RECT box{px(std::min(x0, x1), client.right), px(std::min(y0, y1), client.bottom),
                   px(std::max(x0, x1), client.right), px(std::max(y0, y1), client.bottom)};
    drawProtectionOutline(i.protOverlay, i.videoWindow, box, i.protDraw == QLatin1String("ellipse"));
}

bool QmlPlayerBridge::commitProtectionDraw(double x0, double y0, double x1, double y1) {
    auto& i = *impl_;
    const bool ellipse = i.protDraw == QLatin1String("ellipse");
    const bool armed = !i.protDraw.isEmpty();
    beginProtectionDraw(QString());
    if (!armed || !i.videoWindow) return false;
    RECT client{}; GetClientRect(i.videoWindow, &client);
    if (client.right < 1 || client.bottom < 1) return false;
    const double w = client.right, h = client.bottom;
    // Too small to be intended (1.4.4: at least 3 px each way).
    if (std::abs(x1 - x0) * w < 3 || std::abs(y1 - y0) * h < 3) return false;
    // Host pixels -> source-normalized, through the current zoom/pan (1.4.4).
    const auto e = i.snapshot.metrics.resolution.output;
    const auto view = i.engine.previewView();
    const auto a = view.sourcePoint(float(x0 * w), float(y0 * h), float(w), float(h), float(e.width), float(e.height));
    const auto b = view.sourcePoint(float(x1 * w), float(y1 * h), float(w), float(h), float(e.width), float(e.height));
    engine::ProtectionRect q{std::clamp(std::min(a.first, b.first), 0.0f, 1.0f), std::clamp(std::min(a.second, b.second), 0.0f, 1.0f),
                             std::clamp(std::max(a.first, b.first), 0.0f, 1.0f), std::clamp(std::max(a.second, b.second), 0.0f, 1.0f), ellipse};
    if (q.empty()) return false;
    const bool ok = editProtection([&](engine::ChainNode& node) {
        for (auto& slot : node.protection.regions)
            if (slot.empty()) { slot = q; node.enabled = true; node.protection.enabled = true; return true; }
        emit notice(tr("最多 4 个保护区域，请先删除一个"), true);
        return false;
    });
    veyra::log::info("ui-protection", std::format("{} accepted={} bounds={:.3f},{:.3f},{:.3f},{:.3f} source-normalized NR-only",
                                                  ellipse ? "ellipse" : "rectangle", ok, q.left, q.top, q.right, q.bottom));
    return ok;
}

bool QmlPlayerBridge::removeProtectionRegion(int index) {
    if (index < 0 || index >= int(engine::ProtectionSettings{}.regions.size())) return false;
    return editProtection([&](engine::ChainNode& node) {
        // Keep the remaining regions packed in order.
        auto& regions = node.protection.regions;
        for (int k = index; k + 1 < int(regions.size()); ++k) regions[size_t(k)] = regions[size_t(k + 1)];
        regions.back() = {};
        return true;
    });
}
bool QmlPlayerBridge::clearProtectionRegions() {
    return editProtection([](engine::ChainNode& node) { node.protection.regions = {}; return true; });
}
bool QmlPlayerBridge::setProtectionFeather(double pixels) {
    if (!std::isfinite(pixels) || pixels < 0 || pixels > 64) return false;
    return editProtection([&](engine::ChainNode& node) { node.protection.featherPixels = float(pixels); return true; });
}


// --- design gap A1-A5: flow, cadence, presentation ----------------------------
int QmlPlayerBridge::hdrOutputMode()const{return int(settings().hdrOutputMode);}
void QmlPlayerBridge::setHdrOutputMode(int value){if(value<0||value>1)return;auto s=settings();if(int(s.hdrOutputMode)==value)return;s.hdrOutputMode=engine::HdrOutputMode(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::fgMotionSource()const{return engine::motionUsesFlow(settings().fgMotion,settings().frameGenerationBackend)?1:0;}
void QmlPlayerBridge::setFgMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.fgMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::srMotionSource()const{return engine::motionUsesFlow(settings().srMotion)?1:0;}
void QmlPlayerBridge::setSrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.srMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::nrMotionSource()const{return engine::motionUsesFlow(settings().nrMotion)?1:0;}
void QmlPlayerBridge::setNrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.nrMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::flowQuality() const { return int(settings().flow); }
void QmlPlayerBridge::setFlowQuality(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings();
    if (int(s.flow) == value) return;
    s.flow = static_cast<engine::FlowQuality>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受运动估算质量，原设置保留"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::amdFlowHalf() const { return settings().amdFlowHalfResolution; }
void QmlPlayerBridge::setAmdFlowHalf(bool value) {
    auto s = settings();
    if (s.amdFlowHalfResolution == value) return;
    s.amdFlowHalfResolution = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受 AMD 性能档，原设置保留"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::edgeAegis() const { return settings().fgMotionRefine; }
void QmlPlayerBridge::setEdgeAegis(bool value) {
    auto s = settings();
    if (s.fgMotionRefine == value) return;
    s.fgMotionRefine = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受边缘神盾，原设置保留"), true); return; }
    impl_->prefs[QStringLiteral("edgeAegis")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "edge aegis not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLite() const { return settings().dlssgLite; }
void QmlPlayerBridge::setDlssgLite(bool value) {
    auto s = settings();
    if (s.dlssgLite == value) return;
    s.dlssgLite = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受 DLSS 性能模式，原设置保留"), true); return; }
    impl_->prefs[QStringLiteral("dlssgPerformance")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "DLSS performance mode not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLiteAvailable() const {
    static const bool present = ngx::dlssgLiteRuntimePresent(runtime::localRuntimeDirectory());
    return present;
}
QString QmlPlayerBridge::dlssgLiteHint() const {
    if (!dlssgLiteAvailable()) return tr("此软件包没有附带性能模式所需的 DLSS 帧生成 310.9.16 运行库");
    const auto& s = impl_->snapshot;
    if (s.running && s.dlssgLiteState == 3) return tr("目前只支持 RTX 50 系列：40/30 系的多帧解锁只适用于原版运行库");
    if (s.running && s.dlssgLiteState == 1) return tr("生效中：NVIDIA DLSS 帧生成性能模式（测试版）");
    return tr("NVIDIA 新增的性能模式（测试版）：补帧更省 GPU，高倍率更稳，画质可能略降；导出仍用标准模式");
}
int QmlPlayerBridge::contentRate() const { return int(settings().content); }
void QmlPlayerBridge::setContentRate(int value) {
    if (value < 0 || value > int(engine::ContentRate::Capture60To30)) return;
    auto s = settings();
    if (int(s.content) == value) return;
    s.content = static_cast<engine::ContentRate>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受内容节奏，原设置保留"), true); return; }
    // Shared cadence stays in shell preferences and can be included in presets.
    impl_->prefs[QStringLiteral("contentRate")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "content cadence not saved");
    emit settingsChanged();
}

int QmlPlayerBridge::displaySync() const { return int(impl_->presentation.display); }
bool QmlPlayerBridge::fullscreenMemorySafe() const { return !impl_->prefBool("fullscreenMemoryProtection",false)||!impl_->snapshot.vramFullscreenUnsafe; }
void QmlPlayerBridge::setPresentationFullscreen(bool value) {
    if(impl_->presentation.fullscreen==value)return;
    impl_->presentation.fullscreen=value;
    impl_->engine.requestPresentation(impl_->presentation);
    emit presentationChanged();
    // QML's visibility notification runs inside Qt's fullscreen transition.
    // Let Qt finish its activation request before applying no-focus/through
    // flags and handing the foreground to the game. One queued transition,
    // without changing polling or presentation cadence.
    if(value&&gameMode()) QMetaObject::invokeMethod(this,[this]{
        if(impl_->presentation.fullscreen)updateGameMode();
    },Qt::QueuedConnection);
    else updateGameMode();
}
double QmlPlayerBridge::previewAspect() const {
    const int mode=impl_->screenFillActive?2:impl_->aspectMode;
    if(mode==4)return 16.0/9;if(mode==5)return 4.0/3;if(mode==6)return 21.0/9;
    const auto e=impl_->snapshot.metrics.resolution.output;
    if(mode==1&&e.height){
        engine::PreviewView view;view.mode=1;view.rotationDegrees=impl_->snapshot.sourceRotationDegrees;
        return view.aspect(float(e.width),float(e.height));
    }
    return sourceAspect();
}
void QmlPlayerBridge::setDisplaySync(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.display) == value) return;
    impl_->presentation.display = static_cast<engine::DisplaySync>(value);
    applyPresentation();
}
bool QmlPlayerBridge::variableRefresh() const { return impl_->presentation.variableRefresh; }
void QmlPlayerBridge::setVariableRefresh(bool value) {
    if (impl_->presentation.variableRefresh == value) return;
    impl_->presentation.variableRefresh = value;
    applyPresentation();
}
int QmlPlayerBridge::vrrState() const { return impl_->vrrState; }
QString QmlPlayerBridge::vrrHint() const {
    // The 可变刷新 switch is available (not grey) from level 2: the monitor
    // can do VRR even if the driver setting is still off.
    const bool on = impl_->presentation.variableRefresh, running = impl_->snapshot.running;
    switch (impl_->vrrState) {
    case 0: return tr("无法检测屏幕是否支持（目前只支持 NVIDIA 显卡）");
    case 1: return tr("当前屏幕不支持可变刷新");
    case 2: return tr("屏幕支持可变刷新，但当前显示模式不可用；请检查连接、刷新率及驱动设置");
    case 3:
        if (!on) return tr("屏幕跟着画面刷新：不撕裂，也几乎不用等刷新");
        return running ? tr("已请求可变刷新，驱动尚未确认生效；可检查窗口/全屏 G-SYNC 设置，当前状态不代表驱动已关闭")
                       : tr("已开启，将在播放时请求可变刷新；是否生效以驱动报告为准");
    default: return on ? tr("生效中：屏幕跟着画面刷新") : tr("屏幕正处于可变刷新（驱动已开启）");
    }
}
QString QmlPlayerBridge::fgDisplayHint() const { return impl_->fgDisplayHint; }
int QmlPlayerBridge::outputRateMode() const { return int(impl_->presentation.outputRate); }
void QmlPlayerBridge::setOutputRateMode(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.outputRate) == value) return;
    impl_->presentation.outputRate = static_cast<engine::OutputRateMode>(value);
    applyPresentation();
}
double QmlPlayerBridge::outputCustomFps() const { return impl_->presentation.customFps; }
void QmlPlayerBridge::setOutputCustomFps(double value) {
    if (!std::isfinite(value) || value < 1.0 || value > 1000.0) { emit notice(tr("输出上限需在 1–1000 fps 之间"), true); return; }
    if (std::abs(impl_->presentation.customFps - value) < 1e-6) return;
    impl_->presentation.customFps = value;
    applyPresentation();
}
bool QmlPlayerBridge::presentationOwned() const { return impl_->snapshot.presentationProviderOwned; }
QString QmlPlayerBridge::presentationStatus() const { return uiText(impl_->snapshot.presentationStatus); }
void QmlPlayerBridge::applyPresentation() {
    auto& p = impl_->presentation;
    if (!p.valid()) return;
    impl_->engine.requestPresentation(p);
    impl_->prefs[QStringLiteral("presentation")] = QVariantMap{
        {"lowQueue", p.enabled}, {"display", int(p.display)}, {"outputRate", int(p.outputRate)}, {"customFps", p.customFps}, {"vrr", p.variableRefresh}};
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "presentation settings not saved");
    veyra::log::info("ui-presentation", std::format("lowQueue={} display={} outputRate={} customFps={:.3f} variableRefresh={}",
        p.enabled, int(p.display), int(p.outputRate), p.customFps, p.variableRefresh));
    emit presentationChanged();
    emit settingsChanged();
    emit perfChanged();
}

// --- design gap B8-B11: compare and aspect ------------------------------------
int QmlPlayerBridge::compareMode() const { return impl_->compareMode; }
void QmlPlayerBridge::setCompareMode(int value) {
    if (value < 0 || value > 2 || impl_->compareMode == value) return;
    impl_->compareMode = value;
    if (!impl_->holdKeyDown) impl_->engine.comparison(value, impl_->compareBase, float(impl_->compareSplit));
    veyra::log::info("ui-compare", std::format("mode={} base={} split={:.3f}", value, impl_->compareBase, impl_->compareSplit));
    // Comparison shows real frames only (a generated frame has no original to set beside
    // it), so frame generation pauses while it is on. A field user left split compare on
    // for 24 minutes and reported "补帧无效" (logs9, 2026-10-01): say so.
    if (value != 0 && impl_->snapshot.running && impl_->snapshot.applied.multiplier > 1)
        emit notice(tr("对比模式下补帧暂停（对比只能用真实帧），关闭对比后自动恢复"), false);
    emit compareChanged();
}
bool QmlPlayerBridge::compareBase() const { return impl_->compareBase; }
void QmlPlayerBridge::setCompareBase(bool value) {
    if (impl_->compareBase == value) return;
    impl_->compareBase = value;
    if (!impl_->holdKeyDown) impl_->engine.comparison(impl_->compareMode, value, float(impl_->compareSplit));
    emit compareChanged();
}
double QmlPlayerBridge::compareSplit() const { return impl_->compareSplit; }
void QmlPlayerBridge::setCompareSplitAt(double fx) {
    // 1.4.4 AppShell: the split is a fraction of the displayed picture, which is
    // letterboxed and may be zoomed/panned inside the host.
    auto& i = *impl_;
    if (!i.videoWindow || !std::isfinite(fx)) return;
    RECT r{}; GetClientRect(i.videoWindow, &r);
    if (r.right < 1 || r.bottom < 1) return;
    const auto e = i.snapshot.metrics.resolution.output;
    const auto view = i.engine.previewView();
    float contentWidth = float(r.right);
    if (e.width && e.height) contentWidth = view.renderedSize(float(r.right),float(r.bottom),float(e.width),float(e.height)).first;
    const float left = r.right * .5f - contentWidth * view.centerX;
    i.compareSplit = std::clamp((float(fx) * r.right - left) / std::max(1.0f, contentWidth), 0.0f, 1.0f);
    if (!i.holdKeyDown) i.engine.comparison(i.compareMode, i.compareBase, float(i.compareSplit));
    emit compareChanged();
}
int QmlPlayerBridge::aspectMode() const { return impl_->screenFillActive ? 2 : impl_->aspectMode; }
void QmlPlayerBridge::setAspectMode(int value) {
    if (value < 0 || value > 6 || (impl_->aspectMode == value && !impl_->screenFillActive)) return;
    impl_->aspectMode = value;
    impl_->screenFillActive = false;
    impl_->prefs[QStringLiteral("aspect")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "aspect mode not saved");
    applyAspect(true);
    if (gameMode()) { updateGameGeometry(); updateGameMode(); emit gameModeChanged(); }
    emit compareChanged();
}
void QmlPlayerBridge::applyAspect(bool force) {
    auto& i = *impl_;
    const bool aligned=gameMode()&&i.presentation.fullscreen&&!gameScaledPreview();
    const int mode = i.screenFillActive ? 2 : i.aspectMode;
    if (!i.videoWindow) return;
    RECT r{}; GetClientRect(i.videoWindow, &r);
    const auto e = i.snapshot.metrics.resolution.output;
    const QSize client(r.right, r.bottom), image(int(e.width), int(e.height));
    const double dar=sourceAspect();
    if (!force && mode == i.aspectApplied && client == i.screenFitClient && image == i.screenFitImage && dar == i.screenFitAspect) return;
    i.screenFitClient = client; i.screenFitImage = image; i.aspectApplied = mode; i.screenFitAspect = dar;
    auto view=force||aligned?engine::PreviewView{}:i.engine.previewView();
    view.mode=mode;
    view.displayAspect=dar>0&&image.height()>0&&std::abs(dar-double(image.width())/image.height())>.001?dar:0;
    i.engine.previewView(view);
    veyra::log::info("ui-aspect", std::format("mode={} zoom={:.3f} dar={:.4f} client={}x{} image={}x{}",mode,view.zoom,dar,
        client.width(),client.height(),image.width(),image.height()));
}

// --- design gap D18 / C15 / E20 / F21 / F24 ------------------------------------
double QmlPlayerBridge::audioSkewMs() const {
    const auto& skew = impl_->snapshot.captureAudio.skewMs;
    return audioSkewKnown() ? *skew : 0.0;
}
bool QmlPlayerBridge::audioSkewKnown() const {
    const auto& skew = impl_->snapshot.captureAudio.skewMs;
    return audioSyncLive() && skew.has_value() && std::isfinite(*skew) && std::abs(*skew) < 5000.0;
}

QString QmlPlayerBridge::exportSizeEstimate() const {
    const auto gb = [](double bytes) {
        return bytes >= 1024.0 * 1024 * 1024 ? QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2)
                                            : QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 0);
    };
    if (exportRunning()) {
        const double progress = exportProgress();
        const QFileInfo partial(exportTarget() + QStringLiteral(".partial"));
        if (progress > 0.02 && partial.exists() && partial.size() > 0)
            return tr("预计约 %1（按已写入 %2 推算）").arg(gb(partial.size() / progress)).arg(gb(double(partial.size())));
        return tr("开始写入后按已写入量推算");
    }
    const double end = impl_->exportTrimEndSeconds > 0 ? impl_->exportTrimEndSeconds : impl_->snapshot.duration;
    const double seconds = std::max(0.0, end - impl_->exportTrimStartSeconds);
    if (seconds <= 0) return QString();
    if (impl_->exportBitrateMbps > 0)
        return tr("约 %1（视频 %2 Mbps × %3，音轨另计）").arg(gb(impl_->exportBitrateMbps * 1e6 / 8.0 * seconds))
            .arg(impl_->exportBitrateMbps).arg(formatTime(seconds));
    return tr("恒定质量：大小取决于画面，开始后按已写入量推算");
}

QVariantMap QmlPlayerBridge::lastSource() const {
    auto saved = impl_->prefs.value(QStringLiteral("lastSource")).toMap();
    const QString kind = saved.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("file")) {
        saved[QStringLiteral("summary")]=QFileInfo(saved.value(QStringLiteral("label")).toString()).fileName();
    } else if (kind == QLatin1String("capture")) {
        if (!hasCaptureSession()) return {};
        saved[QStringLiteral("summary")] = captureSessionSummary();
    } else if (kind == QLatin1String("ps5")) {
        saved[QStringLiteral("summary")] = tr("PS5 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("moonlight")) {
        saved[QStringLiteral("summary")] = tr("PC 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("xbox")) {
        saved[QStringLiteral("summary")] = tr("Xbox 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("screen")) {
        saved[QStringLiteral("summary")] = tr("屏幕捕获 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (hasCaptureSession()) {
        // Before this was recorded, only capture sessions were remembered.
        return QVariantMap{{"kind", "capture"}, {"summary", captureSessionSummary()}};
    } else return {};
    return saved;
}
void QmlPlayerBridge::rememberSource(const QString& kind, const QString& label) {
    if(kind!=QLatin1String("file")){impl_->liveKind = kind;impl_->liveLabel = label;}
    QVariantMap value{{"kind", kind}, {"label", label}};
    if (kind == QLatin1String("screen")) value[QStringLiteral("screenKind")] = screenOptions().value(QStringLiteral("kind")).toInt();
    if (impl_->prefs.value(QStringLiteral("lastSource")).toMap() == value) return;
    impl_->prefs[QStringLiteral("lastSource")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "last source not saved");
    emit preferencesChanged();
}
void QmlPlayerBridge::autoResumeLastSource() {
    if(!impl_->prefBool("autoResume",false)||impl_->openingSource||impl_->snapshot.running)return;
    if(lastSource().isEmpty())return;
    veyra::log::info("ui-startup","automatically resuming the last source");
    resumeLastSource();
}
void QmlPlayerBridge::resumeLastSource() {
    const auto last = lastSource();
    const QString kind = last.value(QStringLiteral("kind")).toString();
    veyra::log::info("ui-resume-source", "kind=" + kind.toStdString());
    if(kind==QLatin1String("file")){
        const QString path=last.value(QStringLiteral("label")).toString();
        if(!QFileInfo(path).isFile()){emit notice(tr("上次的视频文件不存在：%1").arg(path),true);return;}
        openPath(path);return;
    }
    if (kind == QLatin1String("capture")) { resumeCaptureSession(); return; }
    if (kind == QLatin1String("ps5")) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
        ps5Load();
        if (!ps5Connect()) emit notice(tr("没能继续上次的 PS5 串流：%1").arg(impl_->ps5Status), true);
#else
        emit notice(tr("此版本不含 PS5 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("xbox")) {
#ifdef VEYRA_ENABLE_XBOX
        if (!impl_->xbox || !impl_->xbox->resumeLast()) {
            emit notice(tr("没能继续上次的 Xbox 串流，请在 Xbox 串流窗口里重新选择"), true);
            emit navigate(QStringLiteral("xbox"));
        }
#else
        emit notice(tr("此版本不含 Xbox 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("moonlight")) {
#ifdef VEYRA_ENABLE_MOONLIGHT
        if (!impl_->moonlight || !impl_->moonlight->resumeLast()) {
            emit notice(tr("没能继续上次的 PC 串流，请在 PC 串流窗口里重新选择"), true);
            emit navigate(QStringLiteral("moonlight"));
        }
#else
        emit notice(tr("此版本不含 PC 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("screen")) {
        const int screenKind = last.value(QStringLiteral("screenKind")).toInt();
        if (screenOptions().value(QStringLiteral("kind")).toInt() != screenKind) setScreenOption(QStringLiteral("kind"), screenKind);
        refreshCaptureTargets();
        const QString label = last.value(QStringLiteral("label")).toString();
        for (const auto& item : screenTargets()) {
            const auto map = item.toMap();
            if (map.value(QStringLiteral("label")).toString() == label) {
                setScreenTargetId(map.value(QStringLiteral("id")).toString());
                startScreenCapture();
                return;
            }
        }
        emit notice(tr("上次的捕获目标「%1」已不在，请重新选择").arg(label), true);
        emit navigate(QStringLiteral("screen"));
        return;
    }
    emit notice(tr("还没有可以继续的片源"), true);
}

bool QmlPlayerBridge::resetNrLayer(int index) {
    auto& c = impl_->chain;
    if (c.mode != engine::ChainMode::List || index < 0 || index >= int(c.nodeCount) ||
        c.nodes[size_t(index)].type != engine::EffectType::NrEnhance) return false;
    auto& node = c.nodes[size_t(index)];
    const auto before = node;
    const engine::NrLayerSettings defaults;
    // The model and residual parameters, the size and the temporal option go
    // back to their defaults; the shared runtime and the on/off state stay.
    node.nr.model = defaults.model; node.nr.residual = defaults.residual;
    node.nr.temporal = defaults.temporal; node.nr.sizePolicy = defaults.sizePolicy;
    if (node == before) return true;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}

QVariantList QmlPlayerBridge::frameTimes() const {
    QVariantList out;
    for (const double v : impl_->frameTimes) out << v;
    return out;
}

// --- P5 (2026-09-29): headers, colour history, export rows, presets -------
QString QmlPlayerBridge::sourceKind() const {
    const auto& s = impl_->snapshot;
    if (!hasSource()) return {};
    if (s.image) return QStringLiteral("image");
    if (s.remotePlay) return QStringLiteral("ps5");
    if (!impl_->liveKind.isEmpty()) return impl_->liveKind;
    if (s.capture) return QStringLiteral("capture");
    return QStringLiteral("file");
}
QString QmlPlayerBridge::sourceTitle() const {
    const QString kind = sourceKind();
    if (kind.isEmpty()) return {};
    if(kind==QLatin1String("url"))return tr("网络视频 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("file") || kind == QLatin1String("image")) return sourceName();
    if (kind == QLatin1String("capture")) {
        const QString name = impl_->prefString("captureDeviceName");
        return tr("采集卡 · %1").arg(name.isEmpty() ? impl_->liveLabel : name);
    }
    if (kind == QLatin1String("ps5")) return tr("PS5 串流 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("moonlight")) return tr("PC 串流 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("xbox")) return tr("Xbox 串流 · %1").arg(impl_->liveLabel);
    return tr("屏幕捕获 · %1").arg(impl_->liveLabel);
}
// "1080p60" / "1080p59.94": the reported height and nominal rate only.
QString QmlPlayerBridge::sourceRateText() const {
    const auto& s = impl_->snapshot;
    if (!s.sourceHeight) return {};
    if (s.image) return QStringLiteral("%1×%2").arg(s.sourceWidth).arg(s.sourceHeight);
    const double fps = s.nominalSourceFps;
    if (fps <= 0.0) return QStringLiteral("%1p").arg(s.sourceHeight);
    const bool whole = std::abs(fps - std::round(fps)) < 0.01;
    return QStringLiteral("%1p%2").arg(s.sourceHeight)
        .arg(whole ? QString::number(int(std::round(fps))) : QString::number(fps, 'f', 2));
}
QString QmlPlayerBridge::sourceFormatText() const {
    QStringList parts;
    const QString rate = sourceRateText();
    if (rate.isEmpty()) return {};
    parts << rate;
    if (sourceKind() == QLatin1String("capture")) {
        for (const auto& f : impl_->captureFormatList)
            if (f.key == impl_->captureFormatKey) {
                const QString label = uiText(f.label);
                const int dot = label.lastIndexOf(QString::fromUtf8("·"));
                if (dot >= 0) parts << label.mid(dot + 1).trimmed();
                break;
            }
    }
    const QString colour = uiText(impl_->snapshot.colorStatus);
    if (!colour.isEmpty()) parts << colour.section(QString::fromUtf8(" → "), 0, 0).trimmed();
    return parts.join(QString::fromUtf8(" · "));
}

bool QmlPlayerBridge::colourCanUndo() const { return !impl_->colourUndo.empty(); }
bool QmlPlayerBridge::colourCanRedo() const { return !impl_->colourRedo.empty(); }
bool QmlPlayerBridge::colourCanPaste() const { return impl_->colourClip.has_value(); }
namespace {
bool isColourNode(const engine::EffectChain& chain, int index) {
    return index >= 0 && uint32_t(index) < chain.nodeCount && chain.nodes[index].type == engine::EffectType::Color;
}
}
// Undo and redo re-commit a stored state through the normal path (validation,
// chain rebuild), marked as a replay so it does not push history of its own.
bool QmlPlayerBridge::colourUndo() {
    while (!impl_->colourUndo.empty()) {
        const auto step = impl_->colourUndo.back();
        impl_->colourUndo.pop_back();
        if (!isColourNode(impl_->chain, step.index)) continue;
        impl_->selectedColour = step.index;
        const auto current = selectedColourSettings();
        impl_->colourReplaying = true;
        const bool ok = commitColour(step.colour);
        impl_->colourReplaying = false;
        if (ok) impl_->colourRedo.push_back({step.index, current});
        impl_->colourLastIndex = -1;
        veyra::log::info("ui-colour", std::format("undo layer={} ok={} left={}", step.index, ok, impl_->colourUndo.size()));
        emit settingsChanged();
        return ok;
    }
    emit settingsChanged();
    return false;
}
bool QmlPlayerBridge::colourRedo() {
    while (!impl_->colourRedo.empty()) {
        const auto step = impl_->colourRedo.back();
        impl_->colourRedo.pop_back();
        if (!isColourNode(impl_->chain, step.index)) continue;
        impl_->selectedColour = step.index;
        const auto current = selectedColourSettings();
        impl_->colourReplaying = true;
        const bool ok = commitColour(step.colour);
        impl_->colourReplaying = false;
        if (ok) impl_->colourUndo.push_back({step.index, current});
        impl_->colourLastIndex = -1;
        veyra::log::info("ui-colour", std::format("redo layer={} ok={}", step.index, ok));
        emit settingsChanged();
        return ok;
    }
    emit settingsChanged();
    return false;
}
void QmlPlayerBridge::colourCopy() {
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点"), true); return; }
    impl_->colourClip = selectedColourSettings();
    veyra::log::info("ui-colour", "copy");
    emit settingsChanged();
    emit notice(tr("已复制调色参数"), false);
}
bool QmlPlayerBridge::colourPaste() {
    if (!impl_->colourClip) return false;
    auto next = *impl_->colourClip;
    next.enabled = selectedColourSettings().enabled;   // paste the grade, not the switch
    impl_->colourLastIndex = -1;                        // a paste is its own step
    const bool ok = commitColour(next);
    veyra::log::info("ui-colour", std::format("paste ok={}", ok));
    return ok;
}

QVariantList QmlPlayerBridge::exportItems() const {
    QVariantList result;for(int i=0;i<impl_->exportQueue.count();++i)result<<impl_->exportQueue.get(i);return result;
}
void QmlPlayerBridge::addCurrentExportFile(){
    if(!QFileInfo(utf8Of(impl_->sourceLabel)).isFile())return;
    const auto id=impl_->exportQueue.addFile(utf8Of(impl_->sourceLabel));
    impl_->exportQueue.queue().setTrim(id,impl_->exportTrimStartSeconds,impl_->exportTrimEndSeconds);
    impl_->exportQueue.setSelectedId(id);emit exportChanged();
}
QObject* QmlPlayerBridge::exportQueueModel() const{return &impl_->exportQueue;}
int QmlPlayerBridge::exportContainer()const{return impl_->exportContainer;}
void QmlPlayerBridge::setExportContainer(int value){
    if(value<0||value>1||value==impl_->exportContainer)return;
    impl_->exportContainer=value;impl_->exportQueue.queue().setContainer(static_cast<engine::ExportContainer>(value));
    impl_->exportQueue.refresh();
}
int QmlPlayerBridge::exportReadyCount()const{return int(impl_->exportQueue.queue().readyCount());}
bool QmlPlayerBridge::exportSelectionEditable()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());
    return item&&item->state!=engine::ExportItemState::Running&&item->state!=engine::ExportItemState::Queued;
}
int QmlPlayerBridge::exportAudioPolicy()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());return item?int(item->media.audio.policy):1;
}
int QmlPlayerBridge::exportSubtitlePolicy()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());return item?int(item->media.subtitles.policy):1;
}
QVariantList QmlPlayerBridge::exportTracks()const{
    QVariantList result;const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());if(!item)return result;
    for(const auto& track:item->info.tracks){const auto& selection=track.audio?item->media.audio:item->media.subtitles;
        const bool selected=selection.policy==engine::ExportTrackPolicy::All||selection.contains(track.index);
        result<<QVariantMap{{"index",track.index},{"audio",track.audio},{"selected",selected},{"compatible",track.compatible},
            {"label",QStringLiteral("%1 · %2 · %3%4").arg(track.index).arg(utf8Of(track.codec)).arg(utf8Of(track.language)).arg(track.channels?QStringLiteral(" · %1 声道").arg(track.channels):QString())},
            {"title",utf8Of(track.title)},{"action",utf8Of(track.action)}};
    }
    return result;
}
void QmlPlayerBridge::setExportTrackPolicy(bool audio,int policy){
    if(policy<1||policy>3)return;
    auto& queue=impl_->exportQueue.queue();const auto id=impl_->exportQueue.selectedId();const auto* item=queue.find(id);if(!item)return;
    engine::ExportTrackSelection selection;selection.policy=static_cast<engine::ExportTrackPolicy>(policy);
    if(selection.policy==engine::ExportTrackPolicy::Selected){
        for(const auto& track:item->info.tracks)if(track.audio==audio){selection.indices[0]=track.index;selection.count=1;break;}
        if(!selection.count){emit notice(tr("该文件没有可选轨道"),true);return;}
    }
    if(queue.setTracks(id,audio,selection))impl_->exportQueue.refresh();
}
void QmlPlayerBridge::toggleExportTrack(bool audio,int index,bool keep){
    auto& queue=impl_->exportQueue.queue();const auto id=impl_->exportQueue.selectedId();const auto* item=queue.find(id);if(!item)return;
    const auto& previous=audio?item->media.audio:item->media.subtitles;
    engine::ExportTrackSelection selection;selection.policy=engine::ExportTrackPolicy::Selected;
    for(const auto& track:item->info.tracks)if(track.audio==audio){
        // Leaving "all" keeps what "all" actually carried: tracks it skipped for
        // this container stay out instead of turning into a hard failure.
        const bool selected=track.index==index?keep:previous.policy==engine::ExportTrackPolicy::All?track.compatible:previous.contains(track.index);
        if(selected){if(selection.count>=selection.indices.size()){emit notice(tr("手动选择最多 64 条；保留全部不限制轨道数量"),true);return;}selection.indices[selection.count++]=track.index;}
    }
    if(!selection.count)selection.policy=engine::ExportTrackPolicy::None;
    if(queue.setTracks(id,audio,selection))impl_->exportQueue.refresh();
}
void QmlPlayerBridge::previewExportItem(qulonglong id){
    const auto* item=impl_->exportQueue.queue().find(id);if(!item)return;
    const QString input=utf8Of(item->input);const double start=item->start,end=item->end;
    if(input!=utf8Of(impl_->sourceLabel))openPath(input);
    impl_->exportQueue.setSelectedId(id);impl_->exportTrimStartSeconds=start;impl_->exportTrimEndSeconds=end;
    emit exportChanged();
}
bool QmlPlayerBridge::exportCompletionSound()const{return impl_->prefs.value(QStringLiteral("exportCompletionSound"),true).toBool();}
void QmlPlayerBridge::setExportCompletionSound(bool enabled){
    const auto previous=impl_->prefs;impl_->prefs[QStringLiteral("exportCompletionSound")]=enabled;
    if(!impl_->savePrefs()){impl_->prefs=previous;emit notice(tr("提示音设置保存失败"),true);}
    emit exportChanged();
}
void QmlPlayerBridge::clearFinishedExportItems() {
    for(int row=impl_->exportQueue.count()-1;row>=0;--row){const auto& item=impl_->exportQueue.queue().items()[row];
        if(item.state==engine::ExportItemState::Done||item.state==engine::ExportItemState::Failed||item.state==engine::ExportItemState::Cancelled)impl_->exportQueue.removeItem(item.id);
    }
    emit exportChanged();
}
// A thumbnail of any file (export queue rows), through the same provider.
QString QmlPlayerBridge::fileThumbnailId(const QString& path, double seconds) const {
    if (path.isEmpty()) return {};
    return QStringLiteral("image://veyra-thumb/file/%1/%2/%3")
        .arg(impl_->thumbnailGeneration)
        .arg(QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(qint64(std::max(0.0, seconds) * 1000.0));
}

QString QmlPlayerBridge::capturePreviewUrl() const {
    const auto& s = impl_->snapshot;
    if (!s.running || s.posterRgba.empty() || !s.posterWidth || !s.posterHeight) return {};
    if (impl_->posterSession != s.sessionId || impl_->posterUrl.isEmpty()) {
        const QImage image(s.posterRgba.data(), int(s.posterWidth), int(s.posterHeight), int(s.posterWidth * 4),
                           QImage::Format_RGBA8888);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        impl_->posterSession = s.sessionId;
        impl_->posterUrl = png.isEmpty() ? QString()
                                         : QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    }
    return impl_->posterUrl;
}
QString QmlPlayerBridge::captureSignalLevel() const {
    const auto& s = impl_->snapshot;
    if (!s.running || !s.capture) return QStringLiteral("idle");
    if (s.captureRecovering) return QStringLiteral("warn");
    if (s.captureSampledBlack) return QStringLiteral("warn");
    return s.captureFps > 0.5 ? QStringLiteral("ok") : QStringLiteral("warn");
}
QString QmlPlayerBridge::captureSignalText() const {
    const auto& s = impl_->snapshot;
    if (captureSignalLevel() == QLatin1String("idle")) return tr("未连接");
    if (s.captureRecovering) return tr("正在恢复");
    if (s.captureFps > 0.5 && s.captureSampledBlack) return tr("收到黑帧 · 检查输入制式");
    return s.captureFps > 0.5 ? tr("正在收帧") : tr("等待信号");
}

void QmlPlayerBridge::importPresetDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入预设"), QString(),
                                                      tr("Veyra 预设 (*.vpreset);;所有文件 (*)"));
    if (!path.isEmpty()) importPreset(path);
}
bool QmlPlayerBridge::importPreset(const QString& path) {
    std::wstring name;
    auto& library = impl_->facade.presets();
    const bool ok = library.importFile(std::filesystem::path(wideOf(path)), name);
    veyra::log::info("ui-preset", std::format("import ok={} name={}", ok, utf8Of(name).toStdString()));
    if (!ok) { emit notice(tr("导入失败：%1").arg(uiText(library.error())), true); return false; }
    emit presetsChanged();
    emit notice(tr("已导入预设：%1").arg(utf8Of(name)), false);
    return true;
}
void QmlPlayerBridge::exportPresetDialog(int index) {
    const auto& entries = impl_->facade.presets().entries();
    if (index < 0 || size_t(index) >= entries.size()) return;
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出预设"),
                                                      utf8Of(entries[size_t(index)].name) + QStringLiteral(".vpreset"),
                                                      tr("Veyra 预设 (*.vpreset)"));
    if (!path.isEmpty()) exportPreset(index, path);
}
bool QmlPlayerBridge::exportPreset(int index, const QString& path) {
    auto& library = impl_->facade.presets();
    if (index < 0 || size_t(index) >= library.entries().size()) return false;
    const bool ok = library.exportEntry(size_t(index), std::filesystem::path(wideOf(path)));
    veyra::log::info("ui-preset", std::format("export index={} ok={}", index, ok));
    if (!ok) { emit notice(tr("导出失败：%1").arg(uiText(library.error())), true); return false; }
    emit notice(tr("预设已导出到：%1").arg(QDir::toNativeSeparators(path)), false);
    return true;
}

QString QmlPlayerBridge::liveOpeningText() const {
    if (!impl_->pendingLiveText.isEmpty()) return impl_->pendingLiveText;
    const auto& s = impl_->snapshot;
    if (s.running || s.failed || s.transport != engine::TransportState::Opening) return {};
    const QString status = veyra::ui::i18n::text(s.status);
    if (s.remotePlay)
        return status.contains(QStringLiteral("PIN")) ? status : tr("正在连接 PS5 · %1").arg(impl_->liveLabel);
    if (impl_->liveKind == QLatin1String("screen")) return tr("正在开始屏幕捕获…");
    if (s.capture) return tr("正在打开采集设备…");
    return {};
}

// ---- network / server playback (#48) ------------------------------------------------
bool QmlPlayerBridge::networkActive() const { return impl_->snapshot.network.active; }
bool QmlPlayerBridge::networkHolding() const {
    const auto& n = impl_->snapshot.network;
    return n.active && (n.holding() || n.state == media::NetworkStats::State::Reconnecting);
}
QString QmlPlayerBridge::networkSummary() const {
    const auto& n = impl_->snapshot.network;
    if (!n.active) return {};
    QStringList parts;
    parts << tr("缓冲 %1 s").arg(n.bufferedSeconds, 0, 'f', 1);
    parts << (n.downloadBytesPerSecond > 0 ? tr("下载 %1 MB/s").arg(n.downloadBytesPerSecond / 1048576.0, 0, 'f', 1)
                                           : tr("下载 —"));
    parts << tr("重连 %1").arg(n.reconnects);
    if (const uint64_t lost = std::max(n.corruptPackets, n.corruptFrames); lost > 0) parts << tr("丢包 %1").arg(qulonglong(lost));
    return parts.join(QStringLiteral(" · "));
}
QString QmlPlayerBridge::networkNotice() const {
    const auto& n = impl_->snapshot.network;
    return n.active ? uiText(n.message) : QString();
}

} // namespace veyra::ui
