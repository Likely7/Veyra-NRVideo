#include "veyra/ui/QmlPlayerBridge.h"
#include "veyra/ui/QmlExportQueueModel.h"
#include "veyra/ui/UiLanguage.h"
#include <QQmlEngine>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>

#include <QRegion>
#include <QCursor>
#include <QScreen>
#include <QWindow>

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
#include <QTimer>
#include <QBuffer>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/ColorLookStore.h"
#include "veyra/engine/ColorLut.h"
#include "veyra/engine/Subtitles.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/source/CaptureFormatSelection.h"
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
#include "veyra/source/CaptureCardSource.h"
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
// ("nvppex.dll,RTSSHooks64.dll", see gfx::riskyInjections). Empty when none is known.
QString injectionAdvice(const QString& modules) {
    QStringList parts;
    if (modules.contains(QStringLiteral("nvppex.dll"), Qt::CaseInsensitive) || modules.contains(QStringLiteral("NvPresent64.dll"), Qt::CaseInsensitive))
        parts << QCoreApplication::translate("QmlPlayerBridge", "检测到 NVIDIA App 的画面插件（RTX HDR / 智能平滑运动 / RTX 动态鲜艳度）注入了 Veyra，它已知会在采集开始时让显卡设备丢失或让程序崩溃。请在 NVIDIA App → 图形 → 程序设置里为 Veyra 关闭这几项后再试");
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

struct QmlPlayerBridge::Impl {
    // Per-layer on/off before the NR master switch turned them all off.
    std::vector<bool> nrMasterRestore;
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
    // --- subtitles (ported from the Win32 shell, same semantics) ---
    std::unique_ptr<engine::SubtitleLoader> subLoader;
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
    QString lastScreenshot;
    bool holdKeyDown = false;
    int positionTicks = 0;
    // A reopened file resumes here once its session is running.
    uint64_t resumeSession = 0;
    double resumeAt = 0;
    std::vector<sink::RenderEndpoint> audioEndpoints;
    // --- capture connection (P4-e) ---
    struct CaptureQuery { int kind = 0; std::wstring device; std::vector<source::CaptureDevice> video, audio; std::vector<source::CaptureFormat> formats; };
    std::future<CaptureQuery> captureQuery;
    bool captureBusy = false, captureListed = false, captureResumePending = false;
    int captureQueryKind = 0;   // 0 device list, 1 formats, 2 formats then connect (continue)
    std::vector<source::CaptureDevice> captureVideo, captureAudio;
    std::vector<source::CaptureFormat> captureFormatList;
    std::wstring captureFormatKey;
    int captureAudioChoice = source::kCaptureAudioDisabled;
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
#ifdef VEYRA_ENABLE_REMOTEPLAY
    remoteplay::ControllerInput controller;
#endif
    std::jthread ps5Worker;                  // last: joined before what it touches
    // --- screen capture ---
    std::vector<source::ScreenCaptureTarget> screenList;
    bool screenFillActive = false;
    QSize screenFitClient, screenFitImage;
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
        QFile file(path + QStringLiteral(".tmp"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(prefs)).toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size()) { file.close(); file.remove(); return false; }
        file.close();
        QFile::remove(path);
        return QFile::rename(path + QStringLiteral(".tmp"), path);
    }
    int prefInt(const char* key, int fallback) const { return prefs.contains(key) ? prefs.value(key).toInt() : fallback; }
    bool prefBool(const char* key, bool fallback) const { return prefs.contains(key) ? prefs.value(key).toBool() : fallback; }
    QString prefString(const char* key) const { return prefs.value(key).toString(); }
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
                initial.nrRuntime=ngx::preferSfNr(desc.VendorId,desc.Description)?engine::NrRuntime::Ampere:engine::NrRuntime::Original;
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
        if (!settings.validate().empty()) {
            error = QObject::tr("导出设置无效");
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
        const bool fgRollback=before.frameGenerationBackend!=restored.frameGenerationBackend||before.multiplier!=restored.multiplier;
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
            const auto brightness=settings.hdrBrightness;
            engine::ChainGlobalSettings::capture(previous).apply(settings);
            settings.hdrBrightness=brightness;
            facade.setPending(settings);
            if (revalidate(true, &previous)) return true;
            draftGlobals = oldGlobals; facade.setPending(previous);
            revalidate();
            return false;
        }
        const auto& runtime = activeRuntime();
        engine::fromChain(runtime, settings);
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
            }
            if (!projected->firstOf(engine::EffectType::FrameGeneration))
                s.frameGenerationBackend = previous.frameGenerationBackend;
        }
        engine::fromChain(*projected, s);
        if(const auto error=s.validate();!error.empty()){
            veyra::log::error("qml-settings",error);
            validation={false,"效果参数无效，未提交；详见诊断日志"};return false;
        }
        const auto order = engine::runtimeOrder(*projected);
        const bool changed = s != previous || order != engine::runtimeOrder(acceptedChain);
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
    static const uint32_t vendor = [] {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0u;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            return unsigned(desc.VendorId);
        }
        return 0u;
    }();
    return vendor;
}
// FSR 4 ML frame generation needs an AMD RX 9000; on other GPUs the provider crashed the
// whole program (field report 2026-10-01). Known non-AMD adapters are refused up front.
static bool fsr4Possible() { const auto v = primaryGpuVendor(); return v == 0 || v == 0x1002; }

QmlPlayerBridge::QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory,
                                QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(engine, std::move(dataDirectory))) {
    connect(this,&QmlPlayerBridge::snapshotChanged,this,&QmlPlayerBridge::fgChoicesChanged);
    connect(this,&QmlPlayerBridge::settingsChanged,this,&QmlPlayerBridge::fgChoicesChanged);
    impl_->loadPrefs();
    impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));
    impl_->enumerateGpuAdapters();
    impl_->resolveGpuMonitor();
    connect(&impl_->exportQueue,&QmlExportQueueModel::changed,this,&QmlPlayerBridge::exportChanged);
    if (qApp) qApp->installEventFilter(this);
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
        auto settings = impl_->facade.pendingSettings();
        const auto& configuration = restored.configurations[size_t(restored.active)];
        configuration.apply(settings);
        const auto previous = impl_->facade.pendingSettings();
        if (settings.validate().empty() && impl_->facade.applySettings(settings,engine::runtimeOrder(configuration.chain))) {
            impl_->session = impl_->savedSession = restored;
            impl_->restoreConfiguration(configuration);
            impl_->options = engine::PlayerOptions::from(settings,engine::runtimeOrder(impl_->activeRuntime()));
            impl_->revalidate();
        } else {
            impl_->facade.setPending(previous);
            restoreError = tr("模式会话未被引擎接受，保留初始状态");
        }
    }
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
            } else if (!impl_->facade.applySettings(settings,
                                                    engine::runtimeOrder(candidate.configurations[size_t(candidate.active)].chain))) {
                error = tr("引擎未接受默认预设，已保留恢复会话");
            } else {
                impl_->session = candidate;
                impl_->restoreConfiguration(candidate.configurations[size_t(candidate.active)]);
                impl_->revalidate();
                impl_->options = engine::PlayerOptions::from(settings, engine::runtimeOrder(impl_->activeRuntime()));
                if (candidate != before && !impl_->facade.saveChainSession(candidate))
                    error = tr("默认预设已应用，但会话保存失败：") + uiText(impl_->facade.error());
                else impl_->savedSession = candidate;
            }
            if (!error.isEmpty())
                QTimer::singleShot(0, this, [this, error] { emit notice(error, true); });
        }
    }
    // Sound sync is a player setting, not part of either chain: it is kept in
    // the shell preferences and restored here, after the session.
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
        if (p.contains(QStringLiteral("contentRate"))) {
            const int rate = p.value(QStringLiteral("contentRate")).toInt();
            if (rate >= 0 && rate <= int(engine::ContentRate::Capture60To30) && int(s.content) != rate) { s.content = static_cast<engine::ContentRate>(rate); changed = true; }
        }
        if (changed && !impl_->commit(std::move(s))) veyra::log::warn("ui-prefs", "saved sound sync not accepted");
    }
    // Presentation (低延迟队列 / 显示同步 / 输出上限) and the picture aspect.
    {
        const auto saved = impl_->prefs.value(QStringLiteral("presentation")).toMap();
        auto& pr = impl_->presentation;
        if (!saved.isEmpty()) {
            pr.enabled = saved.value(QStringLiteral("lowQueue")).toBool();
            pr.display = static_cast<engine::DisplaySync>(std::clamp(saved.value(QStringLiteral("display")).toInt(), 0, 2));
            pr.outputRate = static_cast<engine::OutputRateMode>(std::clamp(saved.value(QStringLiteral("outputRate")).toInt(), 0, 2));
            pr.customFps = std::clamp(saved.value(QStringLiteral("customFps"), 60.0).toDouble(), 1.0, 1000.0);
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
    impl_->timer = new QTimer(this);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        const bool had = impl_->haveSnapshot;
        const uint64_t before = impl_->publishedRevision;
        const bool wasRunning = impl_->snapshot.running;
        const bool wasImage = impl_->snapshot.image;
        const bool wasFailed = impl_->snapshot.failed;
        const bool wasFullscreenUnsafe = impl_->snapshot.vramFullscreenUnsafe;
        const auto wasTransport = impl_->snapshot.transport;
        const uint64_t wasSession = impl_->snapshot.sessionId;
        if(impl_->poll()){emit settingsChanged();emit chainChanged();}
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
            if (++impl_->positionTicks >= 120) { impl_->positionTicks = 0; rememberPosition(false); }
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
            if (!had || wasSession != s.sessionId || wasTransport != s.transport ||
                wasRunning != s.running || wasImage != s.image || wasFailed != s.failed)
                veyra::log::info("qml-window", std::format(
                    "source-state session={} transport={} running={} image={} failed={} opening={} revision={}",
                    s.sessionId, int(s.transport), s.running, s.image, s.failed,
                    impl_->openingSource, impl_->publishedRevision));
            impl_->power.update(s.running && !s.failed && !s.image && s.transport == engine::TransportState::Playing);
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
                emit notice(text, true);
                if (impl_->currentPage == QLatin1String("min")) emit navigate(QStringLiteral("home"));
            }
            emit snapshotChanged();
        }
        // An export in flight needs its own poll; it is a separate job and its
        // snapshot is not part of the player snapshot.
        if (impl_->exportQueue.queue().needsTick()) pollExport();
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
            // The driver's presentation layer; it carries NVIDIA Smooth Motion, which only
            // engages for fullscreen windows (suspected in the fullscreen-only VRAM growth).
            {L"NvPresent64.dll", "NVIDIA present layer (Smooth Motion)"},
            // NVIDIA App's picture plug-in (RTX HDR / Smooth Motion / RTX Dynamic Vibrance): in
            // every fullscreen VRAM-growth log and in two capture crashes (2026-10-02).
            {L"nvppex.dll", "NVIDIA App picture plug-in (RTX HDR / Smooth Motion / Dynamic Vibrance)"},
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
        }
        // RivaTuner arrived after a start with the interface on the GPU (it was not running
        // then, see main.cpp). Its OSD stays out of this process (RTSSHooksProfileOverride),
        // so nothing breaks; a restart moves the interface to software drawing and puts the
        // OSD on the video.
        static bool rivaTunerLate = false;
        if (!rivaTunerLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && GetModuleHandleW(L"RTSSHooks64.dll")) {
            rivaTunerLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("RivaTuner injected into a run with a GPU interface; its OSD is off in this process ({})",
                automatic ? "started after Veyra: restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested();
        }
        // RivaTuner's default hooking keeps OBS game capture from hooking any Direct3D 12
        // program (2026-10-03, RTSS 7.3.7 + OBS 32.1.2, a bare D3D12 window included); its
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
    // A saved FSR 4 ML choice on a non-AMD GPU crashed at the first open; move it on.
    if (settings().frameGenerationBackend == engine::FrameGenerationBackend::Fsr4 && !fsr4Possible()) {
        auto s = settings();
        s.frameGenerationBackend = primaryGpuVendor() == 0x10DE ? engine::FrameGenerationBackend::Dlss : engine::FrameGenerationBackend::Fsr;
        if (impl_->commit(s)) veyra::log::warn("fg-backend", "saved FSR 4 ML frame generation needs an AMD RX 9000; switched for this GPU");
    }
}

QmlPlayerBridge::~QmlPlayerBridge() {
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
        {"超分", Stage::Sr, "#6EA8FF"},
        {"光流", Stage::Flow, "#9A85FF"},
        {"NR采样", Stage::Nr, "#FF8A3D"},
        {"残差", Stage::Residual, "#E7B868"},
        {"DLSS FG", Stage::FgBatch, "#3DDC84"},
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
    const bool xess = fgBackendName() == QLatin1String("xess");
    QVariantMap out;
    auto publish = [&](uint32_t i, const QString& state, std::optional<Stage> stage) {
        QVariantMap item{{"state", state}, {"ms", 0.0}, {"samples", QVariant::fromValue<qulonglong>(0)}};
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
    unsigned enabledNr = 0, colour = 0;
    for (const int index : path) {
        const auto i = uint32_t(index);
        const auto& n = c.nodes[i];
        std::optional<Stage> stage;
        QString waiting = live ? QStringLiteral("pending") : QStringLiteral("stopped");
        switch (n.type) {
        case engine::EffectType::NrEnhance:
            if (n.enabled && enabledNr < diagnostics::kTimedNrLayers) stage = diagnostics::nrLayerStage(enabledNr);
            if (n.enabled) ++enabledNr;
            break;
        case engine::EffectType::Color:
            // Parameter 0 before any SR/NR is fused into the input conversion
            // shader: it has no separable GPU span of its own.
            if (colour < diagnostics::kTimedColorNodes) stage = diagnostics::colorNodeStage(colour);
            if (colour == 0) waiting = live ? QStringLiteral("fused") : waiting;
            ++colour;
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
    // One source period: the budget a stage has to stay inside. Zero when the
    // source rate is unknown, which the UI shows as unmeasured.
    // With 60->30 capture half rate only every other frame is enhanced, so each has two
    // source periods; dividing by one read 19 ms of 8K work as 115-130 % while it kept up
    // (logs8, 2026-10-01).
    const double fps = impl_->snapshot.nominalSourceFps * (impl_->snapshot.captureHalfRate ? 0.5 : 1.0);
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
    }
    return {};
}
QVariantList QmlPlayerBridge::fgBackendChoices() const {
    return {
        QVariantMap{{"id","dlss"},{"label",tr("DLSS 帧生成")}},
        QVariantMap{{"id","xess"},{"label",tr("Intel XeSS · 实验")}},
        QVariantMap{{"id","fsr3"},{"label",tr("FSR 3.1 · 2X")}},
        QVariantMap{{"id","fsr4"},{"label",tr("FSR 4 ML · RX 9000 · 实验")}}
    };
}
QString QmlPlayerBridge::fgProviderText() const {
    const auto& s=impl_->snapshot;
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
    if(value!=QLatin1String("dlss")&&value!=QLatin1String("xess")&&value!=QLatin1String("fsr3")&&value!=QLatin1String("fsr4")){emit notice(tr("未知补帧后端"),true);return;}
    auto s = settings();
    const auto want = value == QLatin1String("xess") ? engine::FrameGenerationBackend::XeSS
                    : value == QLatin1String("fsr3") ? engine::FrameGenerationBackend::Fsr
                    : value == QLatin1String("fsr4") ? engine::FrameGenerationBackend::Fsr4
                    : engine::FrameGenerationBackend::Dlss;
    if (s.frameGenerationBackend == want) return;
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
                  : engine::fsrFrameGeneration(want) ? 2 : 6;
    const bool clamped = chain.fgMultiplier > uint32_t(cap);
    if (clamped) chain.fgMultiplier = uint32_t(cap);
    if(!impl_->commit(s)){
        chain.fgMultiplier = beforeMultiplier;
        emit notice(tr("补帧后端切换未被接受，原设置保留"),true);return;
    }
    if (clamped) emit notice(tr("所选补帧后端最高 %1X，倍率已调整为 %1X").arg(cap), false);
    emit settingsChanged();
    emit chainChanged();
}

int QmlPlayerBridge::fgMaxMultiplier() const {
    // AppShell/SettingsWindow rules. Capabilities report GENERATED frames, so the
    // multiplier ceiling is that count + 1 (DLSS MultiFrameCountMax 5 = 6X on
    // Blackwell, 1 = 2X on Ada). Unknown DLSS capability offers the full list; a
    // new context validates it. XeSS: 4X with the audited unlock provider, else
    // 2X, or what a running session already reported above 2X.
    const auto& s = impl_->snapshot;
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
    const bool xess = settings().frameGenerationBackend
                      == engine::FrameGenerationBackend::XeSS;
    const int ceiling = xess ? std::min(fgMaxMultiplier(), 4) : fgMaxMultiplier();
    QVariantList out;
    // Only the engine's legal multipliers (kFgMultiplierChoices: no 5X).
    for (const auto choice : engine::kFgMultiplierChoices) {
        const int m = int(choice);
        if (m < 2 || m > ceiling) continue;
        QVariantMap item;
        item["id"] = QString::number(m);
        item["label"] = QString::number(m) + QStringLiteral("X");
        out << item;
    }
    return out;
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
    emit chainChanged();
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
    emit chainChanged();
}


// --- shell preferences -------------------------------------------------------
bool QmlPlayerBridge::reducedMotion() const { return impl_->facade.reducedMotion(); }
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
    // Read from the runtime manifest the build produces rather than a list written
    // by hand here, so the page cannot claim a component the package lacks.
    QVariantList out;
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
    // Nothing here launches a browser: opening an external URL is an outward-facing
    // action, and it is not wired to a verified destination yet. The UI says so.
    emit notice(tr("打开项目页面尚未接入"), true);
}

void QmlPlayerBridge::copyDiagnostics() {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnosticsReport());
        emit notice(tr("诊断信息已复制"), false);
    }
}


// --- capture dialog ----------------------------------------------------------
QVariantList QmlPlayerBridge::captureDevices() const {
    // The source's own enumeration (asynchronous, see refreshCaptureDevices). An
    // empty list means the machine reports no capture devices.
    QVariantList out;
    for (const auto& d : impl_->captureVideo)
        out << QVariantMap{{"id", utf8Of(d.path)}, {"label", utf8Of(d.name)}, {"embeddedAudio", d.hasEmbeddedAudio}};
    return out;
}
QString QmlPlayerBridge::captureDeviceId() const { return utf8Of(impl_->captureDevice); }
void QmlPlayerBridge::setCaptureDeviceId(const QString& value) {
    if (impl_->captureDevice == wideOf(value)) return;
    impl_->captureDevice = wideOf(value);
    impl_->captureFormatList.clear(); impl_->captureFormatKey.clear();
    // Colour follows the device, as in 1.4.4; audio returns to "none" unless it
    // is the remembered device.
    impl_->captureColor = impl_->capturePrefs.colorForDevice(impl_->captureDevice);
    if (impl_->captureDevice != impl_->capturePrefs.videoPath) impl_->captureAudioChoice = source::kCaptureAudioDisabled;
    if (!impl_->captureDevice.empty()) queryCapture(1, impl_->captureDevice);
    emit captureChanged();
}
QString QmlPlayerBridge::captureDeviceLabel() const {
    for (const auto& d : impl_->captureVideo) if (d.path == impl_->captureDevice) return utf8Of(d.name);
    return impl_->captureDevice.empty() ? tr("未选择设备") : tr("设备未连接");
}
QVariantList QmlPlayerBridge::captureFormats() const {
    QVariantList out;
    for (const auto& f : impl_->captureFormatList)
        out << QVariantMap{{"id", utf8Of(f.key)}, {"label", uiText(f.label)}, {"tier", f.tier},
                           {"costHint", source::captureFormatNeedsCostHint(static_cast<source::CaptureFormatTier>(f.tier))}};
    return out;
}
QString QmlPlayerBridge::captureFormatKey() const { return utf8Of(impl_->captureFormatKey); }
void QmlPlayerBridge::setCaptureFormatKey(const QString& key) {
    if (impl_->captureFormatKey == wideOf(key)) return;
    impl_->captureFormatKey = wideOf(key);
    emit captureChanged();
}
QVariantList QmlPlayerBridge::captureAudioInputs() const {
    // Only what the user picks explicitly is ever opened (1.4.4 rule).
    QVariantList out{QVariantMap{{"id", source::kCaptureAudioDisabled}, {"label", tr("不监听音频")}}};
    for (const auto& d : impl_->captureVideo)
        if (d.path == impl_->captureDevice)
            out << QVariantMap{{"id", source::kCaptureAudioFromVideoDevice},
                               {"label", d.hasEmbeddedAudio ? tr("视频设备内置音频（已检测）") : tr("尝试视频设备内置音频")}};
    for (size_t i = 0; i < impl_->captureAudio.size(); ++i)
        out << QVariantMap{{"id", int(i)}, {"label", QStringLiteral("[%1] %2").arg(impl_->captureAudio[i].wasapi ? QStringLiteral("WASAPI") : QStringLiteral("DirectShow"), utf8Of(impl_->captureAudio[i].name))}};
    return out;
}
int QmlPlayerBridge::captureAudioChoice() const { return impl_->captureAudioChoice; }
void QmlPlayerBridge::setCaptureAudioChoice(int choice) {
    if (choice < source::kCaptureAudioFromVideoDevice || choice >= int(impl_->captureAudio.size())) return;
    impl_->captureAudioChoice = choice;
    // Remember the audio choice for this device as soon as it is picked, not only on
    // 连接并开始 (field report 2026-10-01: picked, closed, and it was gone next time).
    auto& i = *impl_;
    if (!i.captureDevice.empty() && i.captureDevice == i.capturePrefs.videoPath) {
        const source::CaptureDevice* audio = choice >= 0 && size_t(choice) < i.captureAudio.size() ? &i.captureAudio[size_t(choice)] : nullptr;
        i.capturePrefs.audioPath = audio ? audio->path : std::wstring{};
        i.capturePrefs.audioMode = audio ? (audio->wasapi ? source::kCaptureAudioWasapi : 0) : choice;
        if (!ui::CapturePreferenceStore(i.dataDir).save(i.capturePrefs)) veyra::log::warn("capture-ui", "capture audio choice not saved");
    }
    emit captureChanged();
}
int QmlPlayerBridge::captureColorSpace() const { return int(source::captureColorSpace(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorSpace(int value) {
    if (value < 0 || value > 3) return;
    impl_->captureColor = source::captureColorOverride(unsigned(value), source::captureColorRange(impl_->captureColor)); emit captureChanged();
}
int QmlPlayerBridge::captureColorRange() const { return int(source::captureColorRange(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorRange(int value) {
    if (value < 0 || value > 2) return;
    impl_->captureColor = source::captureColorOverride(source::captureColorSpace(impl_->captureColor), unsigned(value)); emit captureChanged();
}
double QmlPlayerBridge::captureRequestedFps() const { return impl_->captureFps; }
void QmlPlayerBridge::setCaptureRequestedFps(double value) {
    if (!source::validCaptureFrameRate(value)) { emit notice(tr("采集帧率请输入 1–1000 的数字（可带小数），或 0 沿用设备默认"), true); return; }
    if (impl_->captureFps == value) return;
    impl_->captureFps = value;
    // Saved as soon as it is typed, as the audio choice is (field report 2026-10-01: the
    // new rate was lost unless another option was changed before closing).
    auto& i = *impl_;
    if (!i.captureDevice.empty() && i.captureDevice == i.capturePrefs.videoPath) {
        i.capturePrefs.requestedFps = value;
        if (!ui::CapturePreferenceStore(i.dataDir).save(i.capturePrefs)) veyra::log::warn("capture-ui", "capture frame rate not saved");
    }
    veyra::log::info("capture-ui", std::format("requested fps={}", value));
    emit captureChanged();
}
int QmlPlayerBridge::captureAudioIngress() const { return int(settings().captureAudio); }
void QmlPlayerBridge::setCaptureAudioIngress(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureAudio = static_cast<engine::CaptureAudioIngress>(value);
    impl_->commit(std::move(s)); emit settingsChanged();
}
int QmlPlayerBridge::captureBufferMode() const { return int(settings().captureBuffer); }
void QmlPlayerBridge::setCaptureBufferMode(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureBuffer = static_cast<source::CaptureBufferMode>(value);
    impl_->commit(std::move(s)); emit settingsChanged();
}
bool QmlPlayerBridge::captureQueryBusy() const { return impl_->captureBusy; }
QString QmlPlayerBridge::captureStatus() const { return impl_->captureStatus; }
bool QmlPlayerBridge::captureMagewellDevice() const { return source::magewell::isProCaptureDevicePath(impl_->captureDevice); }
QString QmlPlayerBridge::captureMagewellStatus() const {
    std::wstring runtime;
    if (!source::magewell::runtimeAvailable(&runtime))
        return tr("找不到美乐威运行库 LibMWCapture.dll（应在软件的 runtime\\magewell 文件夹里，或装美乐威驱动/SDK）");
    return veyra::ui::i18n::text(source::magewell::statusText());
}
void QmlPlayerBridge::refreshCaptureDevices() {
    impl_->capturePrefs = ui::CapturePreferenceStore(impl_->dataDir).load();
    queryCapture(0, {});
}
void QmlPlayerBridge::queryCapture(int kind, std::wstring device) {
    auto& i = *impl_;
    if (i.captureBusy) { if (kind == 0) i.captureStatus = tr("正在查询设备…"); return; }
    i.captureBusy = true; i.captureQueryKind = kind;
    i.captureStatus = kind == 0 ? tr("正在查询采集设备…当前播放继续") : tr("正在读取设备支持的格式…");
    i.captureQuery = std::async(std::launch::async, [kind, device] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Impl::CaptureQuery q; q.kind = kind; q.device = device;
        try {
            if (kind == 0) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = source::CaptureCardSource::deviceDetails(true); }
            else {
                if (kind == 2) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = source::CaptureCardSource::deviceDetails(true); }
                q.formats = source::CaptureCardSource::formatsByPath(device);
            }
        } catch (...) {}
        CoUninitialize();
        return q;
    });
    emit captureChanged();
}
void QmlPlayerBridge::tickCapture() {
    auto& i = *impl_;
    if (!i.captureBusy || i.captureQuery.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto q = i.captureQuery.get();
    i.captureBusy = false;
    if (q.kind == 0 || q.kind == 2) { i.captureVideo = std::move(q.video); i.captureAudio = std::move(q.audio); i.captureListed = true; }
    // The remembered rate and audio input, matched against the inputs listed now. Both the
    // panel (kind 0) and 继续上次 (kind 2) need it: the resume path skipped it and connected
    // without sound until the panel was opened once (field report 2026-10-02).
    const auto restoreRememberedAudio = [&i] {
        i.captureFps = i.capturePrefs.requestedFps;
        const int mode = i.capturePrefs.audioMode;
        if (mode == source::kCaptureAudioFromVideoDevice) i.captureAudioChoice = mode;
        else if (!i.capturePrefs.audioPath.empty()) {
            i.captureAudioChoice = source::kCaptureAudioDisabled;
            for (size_t k = 0; k < i.captureAudio.size(); ++k)
                if (i.captureAudio[k].path == i.capturePrefs.audioPath &&
                    i.captureAudio[k].wasapi == (mode == source::kCaptureAudioWasapi)) i.captureAudioChoice = int(k);
        } else i.captureAudioChoice = source::kCaptureAudioDisabled;
    };
    if (q.kind == 2 && q.device == i.capturePrefs.videoPath) restoreRememberedAudio();
    if (q.kind == 0) {
        veyra::log::info("capture-ui", std::format("devices video={} audio={}", i.captureVideo.size(), i.captureAudio.size()));
        // Restore the remembered device; otherwise the first one. Never guess audio.
        std::wstring pick;
        for (const auto& d : i.captureVideo) if (d.path == i.capturePrefs.videoPath) pick = d.path;
        if (pick.empty() && !i.captureVideo.empty() && i.capturePrefs.videoPath.empty()) pick = i.captureVideo.front().path;
        i.captureStatus = i.captureVideo.empty() ? tr("未找到采集设备。连接后点击刷新。")
                        : pick.empty() ? tr("上次采集设备未连接。插回后刷新，或手动选择其他设备。") : QString();
        if (!pick.empty()) {
            i.captureDevice.clear();
            setCaptureDeviceId(utf8Of(pick));
            if (pick == i.capturePrefs.videoPath) restoreRememberedAudio();
        }
        emit captureChanged();
        return;
    }
    if (q.device != i.captureDevice) { emit captureChanged(); return; }   // stale answer
    i.captureFormatList = std::move(q.formats);
    const bool remembered = i.captureDevice == i.capturePrefs.videoPath;
    i.captureFormatKey.clear();
    if (remembered && !i.capturePrefs.formatKey.empty()) {
        for (const auto& f : i.captureFormatList) if (f.key == i.capturePrefs.formatKey) i.captureFormatKey = f.key;
    } else if (!i.captureFormatList.empty()) i.captureFormatKey = i.captureFormatList.front().key;
    i.captureStatus = i.captureFormatList.empty() ? tr("未读到有效的 4K 以内采集格式，或设备正被其他应用占用。")
                    : i.captureFormatKey.empty() ? tr("上次格式已不可用，请重新选择格式。")
                    : tr("连接后使用当前增强设置。格式与音频变更需要重新连接。");
    veyra::log::info("capture-ui", std::format("formats={} selected={}", i.captureFormatList.size(), !i.captureFormatKey.empty()));
    emit captureChanged();
    if (q.kind == 2) {
        if (i.captureFormatKey.empty()) emit notice(tr("上次的采集格式不可用，请在采集卡面板重新选择"), true);
        else startCaptureSession();
    }
}
bool QmlPlayerBridge::startCaptureSession() {
    auto& i = *impl_;
    if (i.captureBusy) { emit notice(tr("设备查询还没有完成"), true); return false; }
    int device = -1;
    for (size_t k = 0; k < i.captureVideo.size(); ++k) if (i.captureVideo[k].path == i.captureDevice) device = int(k);
    const auto* format = source::selectCaptureFormat(i.captureFormatList, -1, i.captureFormatKey);
    if (device < 0 || !format) { emit notice(tr("请先选择采集设备和格式"), true); return false; }
    const int audio = i.captureAudioChoice;
    const source::CaptureDevice* audioDevice = audio >= 0 && size_t(audio) < i.captureAudio.size() ? &i.captureAudio[size_t(audio)] : nullptr;
    const auto path = source::CaptureCardSource::makeCapturePath(unsigned(device), i.captureVideo[size_t(device)], format->index, audio,
                                                                 audioDevice, i.captureColor, i.captureFps, format->key);
    if (path.empty()) { emit notice(tr("无法生成采集连接参数"), true); return false; }
    auto colors = i.capturePrefs.deviceColors; colors[i.captureDevice] = i.captureColor;
    i.capturePrefs = {i.captureDevice, format->key, audioDevice ? audioDevice->path : std::wstring{},
                      audioDevice ? (audioDevice->wasapi ? source::kCaptureAudioWasapi : 0) : audio, i.captureColor,
                      i.capturePrefs.formatHintDismissed, i.captureFps, std::move(colors)};
    if (!ui::CapturePreferenceStore(i.dataDir).save(i.capturePrefs)) veyra::log::warn("capture-ui", "capture selection not saved");
    i.facade.noteCaptureSession({i.captureDevice, format->key, i.facade.presets().entries().empty() ? std::wstring{} : wideOf(currentPresetName()),
                                 i.captureFps > 0 ? i.captureFps : format->fps, true});
    veyra::log::info("capture-ui", std::format("connect format={} audio={} color={} fps={}", QString::fromStdWString(format->label).toStdString(), audio, i.captureColor, i.captureFps));
    i.prefs["captureDeviceName"] = utf8Of(i.captureVideo[size_t(device)].name);
    rememberSource(QStringLiteral("capture"), utf8Of(i.captureVideo[size_t(device)].name));
    i.prefs["captureFormatLabel"] = utf8Of(format->label);
    i.savePrefs();
    openSourceUri(path, i.captureVideo[size_t(device)].name);
    emit snapshotChanged();
    return true;
}
void QmlPlayerBridge::openSourceUri(const std::wstring& uri, const std::wstring& label) {
    rememberPosition(true);
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    impl_->sourceLabel = label;
    impl_->resumeSession = 0;
    impl_->screenFillActive = false;
    const bool screen = uri.find(L"screen") != std::wstring::npos;
    veyra::log::info("capture-ui", "open uri " + QString::fromStdWString(uri).toStdString());
    openAfterCinema(screen ? tr("正在开始屏幕捕获…") : tr("正在打开采集设备 · %1").arg(utf8Of(label)), [this, uri] {
        if (impl_->preOpen) impl_->preOpen();
        impl_->engine.previewView({});
        impl_->engine.open(impl_->videoWindow, uri, impl_->options);
        impl_->openingSessionId = impl_->engine.snapshot().sessionId;
        refreshSubtitles({});
    });
}

void QmlPlayerBridge::openAfterCinema(const QString& pending, std::function<void()> open) {
    const int gen = ++impl_->liveOpenGen;
    const bool fromHome = impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home");
    if (!fromHome) { impl_->pendingLiveText.clear(); open(); return; }
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
    impl_->commit(std::move(s));
    emit settingsChanged();
}
bool QmlPlayerBridge::captureFlipVertical() const { return settings().captureFlipVertical; }
void QmlPlayerBridge::setCaptureFlipVertical(bool value) {
    auto s = settings();
    if (s.captureFlipVertical == value) return;
    s.captureFlipVertical = value;
    impl_->commit(std::move(s));
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
                  {"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}};
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
    } else if (key == QLatin1String("cursor") || key == QLatin1String("fill")) {
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
    veyra::log::info("screen-ui", std::format("start kind={} method={} fps={} cursor={} crop={},{},{},{} fill={}", int(value.kind), int(value.method),
        value.fps, value.cursor, value.left, value.top, value.right, value.bottom, o.value("fill").toBool()));
    openSourceUri(value.uri(), wideOf(screenTargetLabel()));
    impl_->screenFillActive = o.value("fill").toBool();
    impl_->screenFitClient = impl_->screenFitImage = QSize();
    rememberSource(QStringLiteral("screen"), screenTargetLabel());
    return true;
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
                if (!k.controller.start()) veyra::log::warn("xbox-input", "SDL gamepad initialization failed");
                if (!k.xboxTimer) {
                    k.xboxTimer = new QTimer(this);
                    k.xboxTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.xboxTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.xboxActive) { m.controller.stop(); m.xboxTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.xboxController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.xboxFeedback(), focused);
                    });
                }
                k.xboxTimer->start(4);
            } else {
                if (k.xboxTimer) k.xboxTimer->stop();
                k.controller.stop();
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
                if (!k.controller.start()) veyra::log::warn("moonlight-input", "SDL gamepad initialization failed");
                if (!k.moonlightTimer) {
                    k.moonlightTimer = new QTimer(this);
                    k.moonlightTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.moonlightTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.moonlightActive) { m.controller.stop(); m.moonlightTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.moonlightController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.moonlightFeedback(), focused);
                    });
                }
                k.moonlightTimer->start(4);
            } else {
                if (k.moonlightTimer) k.moonlightTimer->stop();
                k.controller.stop();
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
            if (!i.controller.start()) veyra::log::warn("remoteplay-input", "SDL gamepad initialization failed");
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
                        j.engine.remotePlayController({}); j.controller.stop(); j.controllerTimer->stop();
                    }
                });
            }
            i.controllerTimer->start(8);
        } else {
            if (i.controllerTimer) i.controllerTimer->stop();
            i.controller.stop();
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
    const auto previous=node;
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
    emit settingsChanged();emit chainChanged();return true;
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
    i.subStatus.clear(); i.subMedia = media;
    if (i.subAlignWorker.joinable()) { i.subAlignWorker.request_stop(); i.subAlignWorker = {}; }
    i.subAligning = false; i.subAlignReady = false;
    if (!i.subLoader) i.subLoader = std::make_unique<engine::SubtitleLoader>();
    i.subGeneration = i.subLoader->request(media);
    emit subtitlesChanged();
}
void QmlPlayerBridge::tickSubtitles() {
    auto& i = *impl_;
    bool changed = false;
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
    for (const auto& line : draw) { if (!joined.isEmpty()) joined += QStringLiteral(" | "); joined += QString::fromStdWString(line.text); }
    if (joined != i.subText) {
        i.subText = joined;
        if (!joined.isEmpty()) veyra::log::info("subtitle-overlay", "text=" + joined.toStdString());
        emit subtitleTextChanged();
    }
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
        if (!t.usable()) note += QStringLiteral(" · ") + (t.note.empty() ? tr("不可用") : QString::fromStdWString(t.note));
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
        tr("字幕文件 (*.srt *.ass *.ssa *.vtt);;所有文件 (*.*)"));
    if (!path.isEmpty()) loadSubtitleFile(path);
}
bool QmlPlayerBridge::loadSubtitleFile(const QString& path) {
    engine::SubtitleTrack track;
    try { track = engine::loadSubtitleFile(wideOf(path)); } catch (...) {}
    if (!track.usable()) {
        veyra::log::warn("subtitle", "external file has no usable cues path=" + path.toStdString());
        emit notice(tr("字幕文件无法读取或没有字幕内容：%1").arg(QFileInfo(path).fileName()), true);
        return false;
    }
    if (track.name.empty()) track.name = QFileInfo(path).fileName().toStdWString();
    impl_->subTracks.insert(impl_->subTracks.begin(), std::move(track));
    ++impl_->subExternal;
    if (impl_->subPrimary >= 0) ++impl_->subPrimary;
    if (impl_->subSecondary >= 0) ++impl_->subSecondary;
    if (impl_->subAlignTrack >= 0) ++impl_->subAlignTrack;
    impl_->subPrimary = 0; impl_->subPrimaryChosen = true;
    if (impl_->subSecondary == 0) impl_->subSecondary = -1;
    impl_->subStatus = tr("已加载外部字幕：%1").arg(QFileInfo(path).fileName());
    veyra::log::info("subtitle", std::format("external loaded cues={} path={}", impl_->subTracks.front().cues.size(), path.toStdString()));
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

void QmlPlayerBridge::setPreOpenHook(std::function<void()> hook) {
    impl_->preOpen = std::move(hook);
}

QString QmlPlayerBridge::appName() const { return QStringLiteral("Veyra"); }
QString QmlPlayerBridge::version() const { return QStringLiteral(VEYRA_DISPLAY_VERSION); }

// --- playback ---------------------------------------------------------------
QString QmlPlayerBridge::statusText() const { return uiText(impl_->snapshot.status); }
bool QmlPlayerBridge::running() const { return impl_->snapshot.running; }
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
    // The container's display aspect already accounts for sample aspect ratio and
    // rotation, so prefer it. The pixel ratio is only a fallback for a source that
    // reported none, and it is never presented as more than that.
    if (s.sourceDisplayAspect > 0.01) return s.sourceDisplayAspect;
    if (s.sourceWidth > 0 && s.sourceHeight > 0)
        return double(s.sourceWidth) / double(s.sourceHeight);
    return 0.0;
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

VEYRA_NUM_PROP(videoSrQuality, setVideoSrQuality, videoSrQuality, int)
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
void QmlPlayerBridge::setVideoHdr(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            setEffectEnabled(int(i), enabled); return;
        }
    if (enabled) emit notice(tr("请先添加 RTX Video HDR 节点"), true);
}
// RTX Video HDR tuning lives on the chain's HDR node, like NR parameters on
// NR nodes: the same values AppShell's settings sliders edited.
QVariantMap QmlPlayerBridge::videoHdrParams() const {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            const auto& v = impl_->chain.nodes[i].videoHdr;
            // Custom: the last four are the HDR->SDR tone-map tuning; their
            // defaults reproduce the previously hard-coded values.
            return {{"contrast", int(v.contrast)}, {"saturation", int(v.saturation)},
                    {"middleGray", int(v.middleGray)}, {"peakNits", int(v.peakNits)},
                    {"sourcePeakNits", int(v.sourcePeakNits)}, {"sdrWhiteNits", int(v.sdrWhiteNits)},
                    {"exposureEv100", int(v.exposureEv100)}, {"shoulderPercent", int(v.shoulderPercent)}};
        }
    return {};
}
bool QmlPlayerBridge::setVideoHdrParameter(const QString& key, double value) {
    if (!std::isfinite(value)) return false;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i) {
        auto& node = impl_->chain.nodes[i];
        if (node.type != engine::EffectType::VideoHdr) continue;
        const auto before = node;
        if (key == "exposureEv100") {
            // Signed on purpose: exposure is the one parameter that goes both ways.
            if (value < -200 || value > 200) return false;
            node.videoHdr.exposureEv100 = int(std::lround(value));
            if (!node.videoHdr.valid()) { node = before; return false; }
            if (node == before) return true;
            const bool accepted = impl_->revalidate(true);
            if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
            emit settingsChanged(); emit chainChanged(); return accepted;
        }
        if (value < 0) return false;
        const auto v = unsigned(std::lround(value));
        if (key == "contrast") node.videoHdr.contrast = v;
        else if (key == "saturation") node.videoHdr.saturation = v;
        else if (key == "middleGray") node.videoHdr.middleGray = v;
        else if (key == "peakNits") node.videoHdr.peakNits = v;
        else if (key == "sourcePeakNits") node.videoHdr.sourcePeakNits = v;
        else if (key == "sdrWhiteNits") node.videoHdr.sdrWhiteNits = v;
        else if (key == "shoulderPercent") node.videoHdr.shoulderPercent = v;
        else return false;
        if (!node.videoHdr.valid()) { node = before; return false; }
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged(); emit chainChanged(); return accepted;
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
    return node && node->enabled ? int(impl_->chain.fgMultiplier) : 1;
}
void QmlPlayerBridge::setFgMultiplier(int multiplier) {
    const bool legal = std::find(std::begin(engine::kFgMultiplierChoices), std::end(engine::kFgMultiplierChoices),
                                 uint32_t(std::max(multiplier, 0))) != std::end(engine::kFgMultiplierChoices);
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
    emit settingsChanged(); emit chainChanged();
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
        const auto before=node;
        // Explicit edits replace legacy multi-rectangle settings with the one global region.
        node.protection.regions={};
        node.protection.regions[0]={float(left),float(top),float(right),float(bottom)};
        node.protection.featherPixels=float(feather);
        if(node==before)return true;
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emit chainChanged();return accepted;
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
        const auto before=*n; n->member=decltype(n->member)(value);          \
        if(!impl_->revalidate(true)){                                       \
            const auto error=impl_->validation.message; *n=before;          \
            impl_->revalidate();emit notice(uiText(error),true);             \
        }                                                                  \
        emit settingsChanged();emit chainChanged();                         \
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
    emit settingsChanged();emit chainChanged();
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
    impl_->commit(std::move(s));
    emit settingsChanged();
}
double QmlPlayerBridge::nrHoldTolerance() const { return double(settings().nrHoldTolerance); }
void QmlPlayerBridge::setNrHoldTolerance(double value) {
    auto s = settings();
    const float next = float(value);
    if (s.nrHoldTolerance == next) return;
    s.nrHoldTolerance = next;
    impl_->commit(std::move(s));
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
            {"uiCorrection",bool(n.model.uiCorrection)},{"total",n.residual.total},
            {"darken",n.residual.darken},{"brighten",n.residual.brighten},
            {"color",n.residual.color},{"luminance",n.residual.luminance},{"temporal",n.temporal},
            {"antiFlicker",int(n.antiFlicker)}};
    }
    return out;
}

bool QmlPlayerBridge::setNrLayerParameter(int index,const QString& key,double value){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::NrEnhance||!std::isfinite(value))return false;
    if(key=="runtime") {
        if(value!=0&&value!=2&&value!=3)return false;
        const auto before=impl_->chain;
        for(uint32_t i=0;i<impl_->chain.nodeCount;++i)
            if(impl_->chain.nodes[i].type==engine::EffectType::NrEnhance)
                impl_->chain.nodes[i].nr.runtime=static_cast<engine::NrRuntime>(int(value));
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;impl_->chain=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emit chainChanged();return accepted;
    }
    auto& node=impl_->chain.nodes[index];const auto before=node;auto& n=node.nr;
    if(key=="intensity")n.model.intensity=float(value);
    else if(key=="tone")n.model.tone=float(value);
    else if(key=="structure")n.model.structure=float(value);
    else if(key=="skin")n.model.skin=float(value);
    else if(key=="total")n.residual.total=float(value);
    else if(key=="darken")n.residual.darken=float(value);
    else if(key=="brighten")n.residual.brighten=float(value);
    else if(key=="color")n.residual.color=float(value);
    else if(key=="luminance")n.residual.luminance=float(value);
    else {
        if(value!=std::floor(value)||value<0||value>5)return false;
        if(key=="sizePolicy")n.sizePolicy=static_cast<pipeline::NrSizePolicy>(int(value));
        else if(key=="style"&&value<=2)n.model.style=int(value);
        else if(key=="autoMask"&&value<=1)n.model.autoMask=int(value);
        else if(key=="uiCorrection"&&value<=1)n.model.uiCorrection=int(value);
        else if(key=="temporal"&&value<=1)n.temporal=value!=0;
        else if(key=="antiFlicker"&&validNrAntiFlicker(engine::NrAntiFlicker(int(value))))n.antiFlicker=static_cast<engine::NrAntiFlicker>(int(value));
        else return false;
    }
    if(node==before)return true;
    const bool accepted=impl_->revalidate(true);
    if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
    emit settingsChanged();emit chainChanged();return accepted;
}

QVariantList QmlPlayerBridge::effectCatalog() const {
    QVariantList out;
    for (const auto& info : engine::effectCatalog()) {
        if (impl_->chain.mode == engine::ChainMode::Node && info.type == engine::EffectType::Protection) continue;
        if (impl_->chain.mode == engine::ChainMode::Node && info.type == engine::EffectType::FrameGeneration) {
            for(const auto& choice : fgBackendChoices()) {
                const auto option=choice.toMap();const auto backend=option.value("id").toString();
                QVariantMap fg;
                fg["id"]=backend+QStringLiteral("-fg");
                fg["label"]=option.value("label");
                fg["maxInstances"]=1;fg["repeatable"]=false;fg["mustBeLast"]=true;
                fg["justBeforeLast"]=false;fg["experimental"]=backend!=QLatin1String("dlss");
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
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::chainError() const { return uiText(impl_->validation.message); }
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
    const auto& configuration = candidate.configurations[size_t(want)];
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
    impl_->options = engine::PlayerOptions::from(next,engine::runtimeOrder(impl_->activeRuntime()));
    emit chainChanged();
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
QString QmlPlayerBridge::hdrBrightnessStatus() const { return uiText(impl_->snapshot.hdrBrightnessStatus); }

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
    const uint32_t want = uint32_t(std::max(0, std::min(2000, value)));
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

void QmlPlayerBridge::openPath(const QString& path) {
    if (path.isEmpty()) return;
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
    ++impl_->thumbnailGeneration;
    emit thumbnailSourceChanged(path);
    emit thumbnailGenerationChanged();
    impl_->facade.noteRecentFile(wide);
    emit recentFilesChanged();
    // Switch to a page with a video area, then settle the native window's
    // geometry, and only then open. The presenter samples the window's client
    // size once at initialisation; opening before that point is what produced a
    // 1x1 swapchain.
    // Only leave the current page when the user is choosing a source from home.
    // Opening a file from the professional page keeps them there, which is what the
    // design's source menu implies.
    if (impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home"))
        emit navigate(QStringLiteral("min"));
    rememberSource(QStringLiteral("file"), path);
    if (impl_->preOpen) impl_->preOpen();
    auto options = impl_->options;
    options.softwareDecode = impl_->prefString("decode") == QLatin1String("software");
    options.hardwareDecodeOnly = impl_->prefString("decode") == QLatin1String("hardware");
    impl_->engine.open(impl_->videoWindow, wide, options);
    impl_->openingSessionId = impl_->engine.snapshot().sessionId;
    impl_->resumeSession = 0;
    if ((impl_->prefBool("rememberPosition", true)||impl_->prefBool("autoResume",false))) {
        const auto saved = impl_->prefs.value("positions").toMap().value(path).toMap();
        if (!saved.isEmpty()) { impl_->resumeSession = impl_->openingSessionId; impl_->resumeAt = saved.value("at").toDouble(); }
    }
    refreshSubtitles(wide);
}

void QmlPlayerBridge::togglePlayPause() {
    // AppShell's Play button: a paused session is still "running" (the worker
    // stays alive), so the decision must come from the transport state.
    const auto& s = impl_->snapshot;
    using T = engine::TransportState;
    if (s.image || s.transport == T::Opening || s.transport == T::Stopping) return;
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
        // Only a real file is reopened; device/stream labels are not paths.
        const QString file = QString::fromStdWString(impl_->sourceLabel);
        if (!file.isEmpty() && QFileInfo(file).isFile()) openPath(file);
        return;
    }
    const bool pause = s.transport == T::Playing;
    impl_->engine.pause(pause);
    veyra::log::info("ui-transport", pause ? "pause" : "resume");
}
void QmlPlayerBridge::stopPlayback() {
    rememberPosition(true);
    ++impl_->liveOpenGen; impl_->pendingLiveText.clear();
    impl_->openingSource = false;
    impl_->openingSessionId = 0;
    impl_->engine.stop();
}
void QmlPlayerBridge::seekTo(double seconds) { impl_->engine.seek(seconds); }
void QmlPlayerBridge::seekBy(double seconds) {
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + seconds));
}
void QmlPlayerBridge::openUrl(const QUrl& url) {
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
    static const QHash<QString, std::pair<int, int>> ranges{
        {"subtitleSize", {16, 56}}, {"subtitleFont", {0, 5}}, {"subtitleOutline", {0, 3}},
        {"subtitleMargin", {0, 240}}, {"subtitleLines", {0, 8}}};
    static const QStringList flags{"subtitleEnabled", "subtitleBackground", "subtitleFit",
                                   "subtitleSecondLanguage", "audioForceStereo", "holdCompare",
                                   "magewellLowLatency", "cinePillHidden", "exportStopsPlayback", "obsGameCapture", "autoResume", "fullscreenMemoryProtection"};
    QVariant stored;
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok);
        const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) return false;
        stored = n;
    } else if (flags.contains(key)) {
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
    } else if (key == QLatin1String("decode")) {
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("software") && v != QLatin1String("hardware")) return false;
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
    if(key==QLatin1String("fullscreenMemoryProtection")){impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));emit snapshotChanged();}
    if (key == QLatin1String("audioDevice")) sink::setPreferredRenderEndpoint(impl_->prefString("audioDevice").toStdWString());
    else if (key == QLatin1String("audioForceStereo")) sink::setForceStereoDownmix(impl_->prefBool("audioForceStereo", false));
    else if (key == QLatin1String("magewellLowLatency")) source::magewell::setLowLatencyPreference(impl_->prefBool("magewellLowLatency", false));
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
    else if (key == QLatin1String("monitorGpu")) { impl_->resolveGpuMonitor(); impl_->gpuUtil = -1.0; emit perfChanged(); }
}
QVariantList QmlPlayerBridge::gpuMonitorChoices() const {
    QVariantList out;
    out << QVariantMap{{"id", QString()}, {"label", tr("自动（独立显卡）")}};
    for (const auto& a : impl_->gpuAdapters) out << QVariantMap{{"id", a.id}, {"label", a.name}};
    return out;
}
QString QmlPlayerBridge::gpuMonitorName() const { return impl_->gpuMonitorName; }
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
                               {"hold", "V"}, {"screenshot", "Ctrl+S"}, {"toggleMode", "Tab"}};
    return d;
}
}
QVariantMap QmlPlayerBridge::shortcuts() const {
    QVariantMap out = shortcutDefaults();
    const auto custom = impl_->prefs.value("shortcuts").toMap();
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (out.contains(it.key())) out[it.key()] = it.value();
    return out;
}
bool QmlPlayerBridge::setShortcut(const QString& action, const QString& sequence) {
    if (!shortcutDefaults().contains(action)) return false;
    const QKeySequence seq(sequence, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1) { emit notice(tr("无效的按键"), true); return false; }
    const QString text = seq.toString(QKeySequence::PortableText);
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
    emit preferencesChanged();
    return true;
}
void QmlPlayerBridge::resetShortcuts() {
    const auto previous = impl_->prefs;
    impl_->prefs.remove("shortcuts");
    if (!impl_->savePrefs()) { impl_->prefs = previous; return; }
    emit preferencesChanged();
}
bool QmlPlayerBridge::obsGameCaptureActive() const { return qApp && qApp->property("veyraObsGameCapture").toBool(); }
bool QmlPlayerBridge::overlayCompatActive() const { return qApp && qApp->property("veyraOverlayCompat").toBool(); }
bool QmlPlayerBridge::rivaTunerRunning() const { return gfx::rivaTunerRunning(); }

int QmlPlayerBridge::uiScaleActive() const { return qApp ? qApp->property("veyraUiScale").toInt() : 0; }
void QmlPlayerBridge::openFeedbackPage() {
    copyDiagnostics();
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/issues")));
    emit notice(tr("诊断信息已复制，可直接粘贴到反馈里"), false);
}
void QmlPlayerBridge::openReleasesPage() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/releases")));
}
bool QmlPlayerBridge::eventFilter(QObject* watched, QEvent* event) {
    const auto type = event->type();
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        auto* key = static_cast<QKeyEvent*>(event);
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
    if (exportRunning()) {
        emit notice(tr("设置已保存，请等待导出结束后重启软件"), true);
        return;
    }
    rememberPosition(true);
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
    const bool fgType=type==QLatin1String("dlss-fg")||type==QLatin1String("xess-fg")||
        type==QLatin1String("fsr3-fg")||type==QLatin1String("fsr4-fg");
    const auto fgBackend=type==QLatin1String("fsr4-fg")?engine::FrameGenerationBackend::Fsr4:
        type==QLatin1String("fsr3-fg")?engine::FrameGenerationBackend::Fsr:
        type==QLatin1String("xess-fg")?engine::FrameGenerationBackend::XeSS:engine::FrameGenerationBackend::Dlss;
    const auto fgCeiling=engine::fsrFrameGeneration(fgBackend)?2u:
        fgBackend==engine::FrameGenerationBackend::XeSS?uint32_t(xessCeiling(impl_->snapshot)):6u;
    if (impl_->chain.mode == engine::ChainMode::Node) {
        const bool fg = fgType;
        const engine::EffectInfo* info = nullptr;
        for (const auto& item : engine::effectCatalog())
            if ((fg && item.type == engine::EffectType::FrameGeneration) || utf8Of(item.id) == type) info = &item;
        if (!info || info->type == engine::EffectType::Protection) {
            emit notice(tr("节点模式不支持该效果类型"), true); return -1;
        }
        auto& c = impl_->chain;
        auto before = std::make_unique<engine::EffectChain>(c);
        const auto oldLayout = impl_->layout; const auto previous = impl_->facade.pendingSettings();
        const auto oldGlobals = impl_->draftGlobals;
        auto pending = settings();
        if (fg){pending.frameGenerationBackend=fgBackend;c.fgMultiplier=std::clamp(c.fgMultiplier,2u,fgCeiling);}
        int index = -1;
        if (fg) for (uint32_t i = 0; i < c.nodeCount; ++i)
            if (c.nodes[i].type == engine::EffectType::FrameGeneration) index = int(i);
        if (index < 0) {
            if (c.nodeCount >= engine::kMaxChainNodes || c.countOf(info->type) >= info->maxInstances ||
                impl_->layout.nextId == std::numeric_limits<uint32_t>::max()) {
                emit notice(tr("该类型节点已达上限（含禁用和未连接节点）"), true); return -1;
            }
            index = int(c.nodeCount++);
            auto& node = c.nodes[index]; node = engine::ChainNode{}; node.type = info->type; node.enabled = true;
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
        emit chainChanged(); emit settingsChanged(); return index;
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
        c.fgMultiplier=std::clamp(c.fgMultiplier,2u,fgCeiling);
        auto next=previous;engine::fromChain(c,next);
        next.frameGenerationBackend=fgBackend;
        impl_->validation=engine::validateChain(c);
        if(!impl_->validation.accepted||!next.validate().empty()||
           !impl_->facade.applySettings(next,engine::runtimeOrder(c))) {
            c=before;impl_->facade.setPending(previous);impl_->validation=engine::validateChain(c);
            emit notice(tr("补帧选择未被接受，保留原设置；请检查倍率和后端能力"),true);return -1;
        }
        impl_->options=engine::PlayerOptions::from(next,engine::runtimeOrder(c));
        emit settingsChanged();emit chainChanged();return slot;
    }
    engine::EffectType wanted = engine::EffectType::NrEnhance;
    bool found = false;
    for (const auto& info : engine::effectCatalog()) {
        if (utf8Of(info.id) == type) {
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
            if (nodeInfo.mustBeLast||(wanted==engine::EffectType::Color&&c.nodes[i].type==engine::EffectType::VideoHdr)||
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
    emit chainChanged();
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
    emit chainChanged(); emit settingsChanged(); return index;
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
    }
    if (!impl_->revalidate(true, &previous)) {
        const auto error = impl_->validation.message; node = before;
        impl_->draftGlobals = oldGlobals;
        impl_->facade.setPending(previous);
        impl_->chain.fgMultiplier = multiplier; impl_->chain.fgStrictAdmission = strict; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit chainChanged(); emit settingsChanged(); return true;
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
    emit chainChanged(); emit settingsChanged(); return true;
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
    emit chainChanged(); emit settingsChanged(); return true;
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
    emit chainChanged(); emit settingsChanged(); return true;
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
    emit chainChanged();emit settingsChanged();return index+1;
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
    emit chainChanged();
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
    emit chainChanged();
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
    emit chainChanged();
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
        emit notice(uiText(error),true);emit chainChanged();return false;
    }
    veyra::log::info("ui-nr",std::format("master switch {} layers={}",enabled?"on":"off",layers.size()));
    emit chainChanged();emit settingsChanged();
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
    emit chainChanged();
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
        engine::ChainGlobalSettings::capture(previous).apply(settings);
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
    emit settingsChanged();
    emit chainChanged();
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
        entry.globals=impl_->draftGlobals;
    }
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
    i.history.sample(s);
    const auto& f = s.metrics.flow;
    const bool active = s.running && !s.image && s.transport == engine::TransportState::Playing;
    QString status = tr("待机"), level = QStringLiteral("idle");
    if (s.failed) { status = tr("错误"); level = QStringLiteral("err"); }
    else if (s.remoteRecovering) { status = tr("恢复中"); level = QStringLiteral("warn"); }
    // Comparison pauses frame generation by design; the output then runs at the source
    // rate, which the rate check would call 输出未达标 (a field user read that as broken FG).
    else if (active && i.compareMode != 0 && s.applied.multiplier > 1) { status = tr("对比中"); level = QStringLiteral("ok"); }
    else if (active && !s.applying && f.rateWindowReady) {
        // Judged on the engine's own words, shown in the interface language.
        const std::wstring rate = i.history.rateStatus(s);
        status = uiText(rate);
        level = rate == L"正常" ? QStringLiteral("ok") : QStringLiteral("warn");
    }
    else if (s.applying && s.running) { status = tr("调整中"); level = QStringLiteral("warn"); }
    else if (active) status = tr("采样中");
    else if (s.transport == engine::TransportState::Paused) status = tr("已暂停");
    QStringList detail;
    if (s.running && !s.image) {
        if (s.applied.multiplier > 1) {
            QString backend;
            switch (s.applied.frameGenerationBackend) {
            case engine::FrameGenerationBackend::Dlss: backend = tr("DLSS"); break;
            case engine::FrameGenerationBackend::XeSS: backend = tr("XeSS"); break;
            case engine::FrameGenerationBackend::Fsr: backend = tr("FSR 3.1"); break;
            case engine::FrameGenerationBackend::Fsr4: backend = tr("FSR 4 ML"); break;
            }
            detail << tr("补帧 %1X · %2").arg(s.applied.multiplier)
                          .arg(backend);
        }
        else detail << tr("未补帧");
        if (s.applied.multiplier > 1 && impl_->compareMode != 0) detail << tr("补帧暂停");
        else if (s.applied.multiplier > 1 && (s.fgBudgetLimited || s.xessGenerationSuppressed)) detail << tr("调度降档");
        if (s.nominalSourceFps > 0.01) detail << tr("源 %1 fps").arg(s.nominalSourceFps, 0, 'f', 2);
    }
    // 1.4.4 DashboardHistory: target = source x multiplier (half-rate capture
    // halves it); actual = what reached present (XeSS: its SDK submit rate).
    double ratio = -1.0;
    if (active && f.rateWindowReady) {
        const bool measuredInput = s.capture && s.captureReceived > 30 && s.captureFps > 0;
        const double source = measuredInput ? std::min(s.nominalSourceFps, s.captureFps) : s.nominalSourceFps;
        const bool xessSink = s.applied.multiplier > 1 && engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
        const double actual = xessSink ? f.xessSdkSubmitFps : f.presentSubmitFps;
        const double target = source * (s.captureHalfRate ? 0.5 : 1.0) * (i.compareMode != 0 ? 1u : s.applied.multiplier);
        if (target > 0) ratio = actual / target;
    }
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
    const bool changed = ratioChanged || active || status != i.runStatus || level != i.runLevel || text != i.runDetail || std::abs(gpu - i.gpuPublished) > 0.05;
    i.runStatus = status; i.runLevel = level; i.runDetail = text; i.gpuPublished = gpu;
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
        const auto before = node;
        if (!edit(node)) return false;
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged(); emit chainChanged();
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
// Custom: per-scene HDR brightness. Globals, not chain nodes, and every field is
// a live uniform, so committing one never rebuilds the graph.
bool QmlPlayerBridge::hdrBrightness()const{return settings().hdrBrightness.enabled;}
void QmlPlayerBridge::setHdrBrightness(bool enabled){
    auto s=settings();if(s.hdrBrightness.enabled==enabled)return;s.hdrBrightness.enabled=enabled;
    commitHdrBrightness(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrBrightnessParams()const{
    const auto& b=settings().hdrBrightness;
    return {{"strength",int(b.strength)},{"targetPeakNits",int(b.targetPeakNits)},{"response",int(b.response)},{"transitionMs",int(b.transitionMs)}};
}
bool QmlPlayerBridge::setHdrBrightnessParameter(const QString& key,double value){
    if(!std::isfinite(value)||value<0)return false;
    auto s=settings();const auto v=unsigned(std::lround(value));
    if(key=="strength")s.hdrBrightness.strength=v;
    else if(key=="targetPeakNits")s.hdrBrightness.targetPeakNits=v;
    else if(key=="response")s.hdrBrightness.response=v;
    else if(key=="transitionMs")s.hdrBrightness.transitionMs=v;
    else return false;
    if(!s.hdrBrightness.valid())return false;
    commitHdrBrightness(std::move(s));return true;
}
// The commit path deliberately re-derives the runtime globals from its own copy
// (Impl::commit), and this block is not part of them, so a plain commit applies
// the change to the engine but leaves it out of the pending settings the session is
// captured from: it took effect and then reverted on the next start (field report:
// target peak 1150 came back as an older value). Re-assert it afterwards, in both
// places the session capture reads, and re-project.
void QmlPlayerBridge::commitHdrBrightness(engine::EnhancementSettings settings){
    const auto brightness=settings.hdrBrightness;
    settings.hdrBrightness=brightness;
    if(!impl_->commit(std::move(settings)))return;
    auto after=impl_->facade.pendingSettings();
    after.hdrBrightness=brightness;
    impl_->facade.setPending(after);
    impl_->draftGlobals.hdrBrightness=brightness;
    impl_->revalidate(true);
    // A global change has no other trigger that writes the session out: the capture
    // runs once, at startup (instrumentation in currentSession showed exactly one
    // line, with the old value), so a changed value took effect and was never
    // stored. Ask for the write explicitly.
    impl_->persistSession();
    emit settingsChanged();emit chainChanged();
}
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
int QmlPlayerBridge::contentRate() const { return int(settings().content); }
void QmlPlayerBridge::setContentRate(int value) {
    if (value < 0 || value > int(engine::ContentRate::Capture60To30)) return;
    auto s = settings();
    if (int(s.content) == value) return;
    s.content = static_cast<engine::ContentRate>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受内容节奏，原设置保留"), true); return; }
    // A player setting, not part of either chain: kept in the shell preferences.
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
}
double QmlPlayerBridge::previewAspect() const {
    const int mode=impl_->screenFillActive?2:impl_->aspectMode;
    if(mode==4)return 16.0/9;if(mode==5)return 4.0/3;if(mode==6)return 21.0/9;
    const auto e=impl_->snapshot.metrics.resolution.output;
    if(mode==1&&e.height)return double(e.width)/e.height;
    return sourceAspect();
}
void QmlPlayerBridge::setDisplaySync(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.display) == value) return;
    impl_->presentation.display = static_cast<engine::DisplaySync>(value);
    applyPresentation();
}
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
        {"lowQueue", p.enabled}, {"display", int(p.display)}, {"outputRate", int(p.outputRate)}, {"customFps", p.customFps}};
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "presentation settings not saved");
    veyra::log::info("ui-presentation", std::format("lowQueue={} display={} outputRate={} customFps={:.3f}",
        p.enabled, int(p.display), int(p.outputRate), p.customFps));
    emit presentationChanged();
    emit settingsChanged();
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
int QmlPlayerBridge::aspectMode() const { return impl_->aspectMode; }
void QmlPlayerBridge::setAspectMode(int value) {
    if (value < 0 || value > 6 || impl_->aspectMode == value) return;
    impl_->aspectMode = value;
    impl_->prefs[QStringLiteral("aspect")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "aspect mode not saved");
    applyAspect(true);
    emit compareChanged();
}
void QmlPlayerBridge::applyAspect(bool force) {
    auto& i = *impl_;
    const int mode = i.screenFillActive ? 2 : i.aspectMode;
    if (!i.videoWindow) return;
    RECT r{}; GetClientRect(i.videoWindow, &r);
    const auto e = i.snapshot.metrics.resolution.output;
    const QSize client(r.right, r.bottom), image(int(e.width), int(e.height));
    if (!force && mode == i.aspectApplied && client == i.screenFitClient && image == i.screenFitImage) return;
    i.screenFitClient = client; i.screenFitImage = image; i.aspectApplied = mode;
    auto view=force?engine::PreviewView{}:i.engine.previewView();
    view.mode=mode;
    const double dar=sourceAspect();
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
    emit settingsChanged(); emit chainChanged();
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
    return s.captureFps > 0.5 ? QStringLiteral("ok") : QStringLiteral("warn");
}
QString QmlPlayerBridge::captureSignalText() const {
    const auto& s = impl_->snapshot;
    if (captureSignalLevel() == QLatin1String("idle")) return tr("未连接");
    if (s.captureRecovering) return tr("正在恢复");
    return s.captureFps > 0.5 ? tr("信号正常") : tr("等待信号");
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

} // namespace veyra::ui
