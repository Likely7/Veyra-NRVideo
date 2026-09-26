#include "veyra/ui/QmlPlayerBridge.h"

#include <QRegion>
#include <QWindow>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/ui/PlaybackPowerGuard.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/ScreenCaptureSource.h"

namespace veyra::ui {
namespace {
QString utf8Of(std::string_view s) { return QString::fromUtf8(s.data(), int(s.size())); }
QString utf8Of(const std::wstring& w) { return QString::fromWCharArray(w.c_str(), int(w.size())); }
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
} // namespace

struct QmlPlayerBridge::Impl {
    engine::EngineController& engine;
    PlayerUiFacade facade;

    // Last published snapshot and the revision it came from. Polling compares
    // revisions, so an unchanged snapshot costs one comparison per tick.
    engine::PlayerSnapshot snapshot;
    uint64_t publishedRevision = 0;
    bool haveSnapshot = false;
    bool openingSource = false;
    uint64_t openingSessionId = 0;

    // The chain the UI edits, kept in step with the facade's pending settings.
    engine::EffectChain chain;
    engine::ChainValidation validation;

    // The window the engine presents into, and the last file the UI opened.
    HWND videoWindow = nullptr;
    std::wstring sourceLabel;
    int thumbnailGeneration = 0;

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
    engine::ExportJobSnapshot exportSnapshot;
    bool exportHevc = false;
    uint32_t exportBitrateMbps = 0;
    sink::ExportRateControl exportRateControl = sink::ExportRateControl::Cq;
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

    Impl(engine::EngineController& e, std::filesystem::path dataDirectory)
        : engine(e), facade(e, std::move(dataDirectory)) {
        chain = engine::toChain(facade.pendingSettings());
        validation = engine::validateChain(chain);
        options = engine::PlayerOptions::from(facade.pendingSettings());
    }

    void poll() {
        auto frame = facade.poll();
        if (!frame.changed && haveSnapshot) return;
        snapshot = frame.snapshot;
        publishedRevision = frame.revision;
        haveSnapshot = true;
        if (openingSource && openingSessionId != 0 && snapshot.sessionId == openingSessionId &&
            (snapshot.running || snapshot.image || snapshot.failed ||
             snapshot.transport == engine::TransportState::Empty ||
             snapshot.transport == engine::TransportState::Stopping))
            openingSource = false;
    }

    // The chain is the source of truth for the stage settings; every commit
    // writes it back over the struct so the two can never disagree.
    void commit(engine::EnhancementSettings settings) {
        engine::fromChain(chain, settings);
        facade.setPending(settings);
        facade.applySettings(settings);
        options = engine::PlayerOptions::from(settings);
    }

    void revalidate() {
        validation = engine::validateChain(chain);
        auto s = facade.pendingSettings();
        engine::fromChain(chain, s);
        facade.setPending(s);
        options = engine::PlayerOptions::from(s);
    }
};

QmlPlayerBridge::QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory,
                                QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(engine, std::move(dataDirectory))) {
    impl_->facade.importLegacyStores();
    // Recent files, the last capture session and the shell preferences are saved
    // on every change; without this load they were written but never read back,
    // so every restart came up with an empty history. A damaged file is logged
    // and the defaults stand.
    if (!impl_->facade.loadPreferences())
        veyra::log::error("ui", "preferences not loaded: " + utf8Of(impl_->facade.error()).toStdString());
    // 60 Hz ceiling, and it does nothing at all when the snapshot is unchanged.
    impl_->timer = new QTimer(this);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        const bool had = impl_->haveSnapshot;
        const uint64_t before = impl_->publishedRevision;
        const bool wasRunning = impl_->snapshot.running;
        const bool wasImage = impl_->snapshot.image;
        const bool wasFailed = impl_->snapshot.failed;
        const auto wasTransport = impl_->snapshot.transport;
        const uint64_t wasSession = impl_->snapshot.sessionId;
        impl_->poll();
        if (!had || impl_->publishedRevision != before) {
            const auto& s = impl_->snapshot;
            if (!had || wasSession != s.sessionId || wasTransport != s.transport ||
                wasRunning != s.running || wasImage != s.image || wasFailed != s.failed)
                veyra::log::info("qml-window", std::format(
                    "source-state session={} transport={} running={} image={} failed={} opening={} revision={}",
                    s.sessionId, int(s.transport), s.running, s.image, s.failed,
                    impl_->openingSource, impl_->publishedRevision));
            impl_->power.update(s.running && !s.failed && !s.image && s.transport == engine::TransportState::Playing);
            emit snapshotChanged();
        }
        // An export in flight needs its own poll; it is a separate job and its
        // snapshot is not part of the player snapshot.
        if (impl_->exportSnapshot.active()) pollExport();
    });
    impl_->timer->start(16);
}

QmlPlayerBridge::~QmlPlayerBridge() = default;


// --- professional-page readouts ---------------------------------------------
// These read fields the engine genuinely publishes. Where it publishes nothing,
// the getter says so and the UI shows an explicit "unmeasured" rather than a
// zero or a stand-in.
QString QmlPlayerBridge::outputSummary() const {
    // The engine publishes the source extent and the preview scale, not the final
    // working extent, so the honest answer is the source size at the reported
    // scale factor. Nothing here invents a resolution the graph did not produce.
    const auto& s = impl_->snapshot;
    if (s.sourceWidth == 0 || s.sourceHeight == 0) return {};
    return QStringLiteral("%1x%2").arg(s.sourceWidth).arg(s.sourceHeight);
}

// A display rate is only real when the system's display event can be read. No
// counter is wired in this build, so it reports unknown and the UI shows "未测" -
// which is exactly what the design requires instead of passing the submit rate off
// as a measurement that was never taken.
double QmlPlayerBridge::displayFps() const { return 0.0; }
bool QmlPlayerBridge::displayFpsKnown() const { return false; }

double QmlPlayerBridge::queuedFrames() const {
    // Preview frames the scheduler chose not to present, as a running count. This
    // is in-process scheduling, NOT screen latency, and the UI labels it so.
    return double(impl_->snapshot.previewSkipped);
}

int QmlPlayerBridge::skippedFrames() const { return int(impl_->snapshot.previewSkipped); }

QString QmlPlayerBridge::flowBackend() const {
    // The snapshot carries a performance figure and a content rate, not a backend
    // name; report the fact we do have and let it read as unmeasured otherwise.
    const auto& s = impl_->snapshot;
    if (s.flowPerf == 0) return {};
    return QStringLiteral("GPU 光流");
}

QVariantList QmlPlayerBridge::stageTimings() const {
    // The engine publishes CPU-side percentiles per phase, not per-GPU-stage
    // timings, so the three signals it does publish are what is shown. A stage
    // with no sample reports as unmeasured instead of a zero-length bar.
    const auto& s = impl_->snapshot;
    QVariantList out;
    struct Row { const char* label; double ms; const char* color; bool measured; };
    const Row rows[] = {
        {"调度", s.schedulingWaitP95Ms, "#6EA8FF", s.schedulingWaitP95Ms > 0.0},
        {"处理", s.processCpuP95Ms,     "#FF8A3D", s.processCpuP95Ms > 0.0},
        {"呈现", s.presentCpuP95Ms,     "#3DDC84", s.presentCpuP95Ms > 0.0},
    };
    const double budget = stageBudgetMs();
    for (const auto& r : rows) {
        QVariantMap item;
        item["label"] = QString::fromUtf8(r.label);
        item["ms"] = r.ms;
        item["measured"] = r.measured;
        item["color"] = QString::fromUtf8(r.color);
        item["fraction"] = (r.measured && budget > 0.01) ? r.ms / budget : 0.0;
        out << item;
    }
    return out;
}

double QmlPlayerBridge::stageBudgetMs() const {
    // One source period: the budget a stage has to stay inside. Zero when the
    // source rate is unknown, which the UI shows as unmeasured.
    const double fps = impl_->snapshot.nominalSourceFps;
    return fps > 0.01 ? 1000.0 / fps : 0.0;
}

double QmlPlayerBridge::scheduleP95Ms() const { return impl_->snapshot.schedulingWaitP95Ms; }

double QmlPlayerBridge::chainTotalMs() const {
    // Sum of the phases the engine measured. Zero means nothing measured yet, and
    // the UI shows "未测" rather than a zero that would read as instant.
    const auto& s = impl_->snapshot;
    const double total = s.schedulingWaitP95Ms + s.processCpuP95Ms + s.presentCpuP95Ms;
    return total > 0.0 ? total : 0.0;
}


// --- frame generation --------------------------------------------------------
QString QmlPlayerBridge::fgBackendName() const {
    return impl_->facade.pendingSettings().frameGenerationBackend
                   == engine::FrameGenerationBackend::XeSS
               ? QStringLiteral("xess") : QStringLiteral("dlss");
}
void QmlPlayerBridge::setFgBackendName(const QString& value) {
    auto s = settings();
    const auto want = value == QLatin1String("xess") ? engine::FrameGenerationBackend::XeSS
                                                     : engine::FrameGenerationBackend::Dlss;
    if (s.frameGenerationBackend == want) return;
    s.frameGenerationBackend = want;
    impl_->commit(std::move(s));
    emit settingsChanged();
}

int QmlPlayerBridge::fgMaxMultiplier() const {
    // Start from 2, the multiplier every path supports, and raise it only on a
    // capability the engine actually reported.
    const auto& s = impl_->snapshot;
    int best = 2;
    if (s.fgCapabilityKnown) best = std::max(best, s.fgMultiFrameMax);
    if (s.xessMaxInterpolatedFrames > 0) best = std::max(best, s.xessMaxInterpolatedFrames + 1);
    if (s.fsrMaxGeneratedFrames > 0) best = std::max(best, s.fsrMaxGeneratedFrames + 1);
    return std::min(best, 6);
}

QVariantList QmlPlayerBridge::fgMultiplierChoices() const {
    // Only multipliers the chosen backend supports are offered: DLSS reaches 6X on
    // capable hardware, XeSS stops at 4X. Offering a value the provider would
    // refuse is the same lie as a dead control.
    const bool xess = impl_->facade.pendingSettings().frameGenerationBackend
                      == engine::FrameGenerationBackend::XeSS;
    const int ceiling = xess ? std::min(fgMaxMultiplier(), 4) : fgMaxMultiplier();
    QVariantList out;
    for (int m = 2; m <= ceiling; ++m) {
        QVariantMap item;
        item["id"] = QString::number(m);
        item["label"] = QString::number(m) + QStringLiteral("X");
        out << item;
    }
    return out;
}

bool QmlPlayerBridge::fgStrict() const { return impl_->facade.pendingSettings().fgStrictAdmission; }
void QmlPlayerBridge::setFgStrict(bool value) {
    auto s = settings();
    if (s.fgStrictAdmission == value) return;
    s.fgStrictAdmission = value;
    impl_->commit(std::move(s));
    emit settingsChanged();
}

bool QmlPlayerBridge::fgLowQueue() const { return impl_->facade.pendingSettings().lowLatency; }
void QmlPlayerBridge::setFgLowQueue(bool value) {
    auto s = settings();
    if (s.lowLatency == value) return;
    s.lowLatency = value;
    impl_->commit(std::move(s));
    emit settingsChanged();
}

// --- colour ------------------------------------------------------------------
double QmlPlayerBridge::colorExposure() const { return settings().color.exposure; }
void QmlPlayerBridge::setColorExposure(double v) {
    auto s = settings();
    if (double(s.color.exposure) == v) return;
    s.color.exposure = float(v);
    impl_->commit(std::move(s));
    emit settingsChanged();
}
double QmlPlayerBridge::colorContrast() const { return settings().color.contrast; }
void QmlPlayerBridge::setColorContrast(double v) {
    auto s = settings();
    if (double(s.color.contrast) == v) return;
    s.color.contrast = float(v);
    impl_->commit(std::move(s));
    emit settingsChanged();
}
double QmlPlayerBridge::colorSaturation() const { return settings().color.saturation; }
void QmlPlayerBridge::setColorSaturation(double v) {
    auto s = settings();
    if (double(s.color.saturation) == v) return;
    s.color.saturation = float(v);
    impl_->commit(std::move(s));
    emit settingsChanged();
}
double QmlPlayerBridge::colorTemperature() const { return settings().color.temperature; }
void QmlPlayerBridge::setColorTemperature(double v) {
    auto s = settings();
    if (double(s.color.temperature) == v) return;
    s.color.temperature = float(v);
    impl_->commit(std::move(s));
    emit settingsChanged();
}

void QmlPlayerBridge::resetCurrentPage() {
    // Reset only the enhancement settings this page edits. Presets, recent files,
    // the capture session and the export selection are deliberately untouched.
    auto s = settings();
    s.nr = false; s.sr = false; s.multiplier = 1;
    s.lowLatency = false; s.nrTemporal = false;
    s.model = engine::NrSettings{};
    s.residual = engine::ResidualSettings{};
    s.protection = engine::ProtectionSettings{};
    s.color = engine::ColorSettings{};
    s.videoHdr = engine::VideoHdrSettings{};
    impl_->commit(std::move(s));
    emit settingsChanged();
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
    return tr("首页");
}

QString QmlPlayerBridge::initialPage() const {
    // The saved preference unless a test override was set.
    return impl_->initialPageOverride.isEmpty() ? defaultPage() : impl_->initialPageOverride;
}
void QmlPlayerBridge::setInitialPage(const QString& value) {
    impl_->initialPageOverride = value;
    emit settingsChanged();
}

QString QmlPlayerBridge::currentPage() const { return impl_->currentPage; }
void QmlPlayerBridge::setCurrentPage(const QString& value) { impl_->currentPage = value; }

QString QmlPlayerBridge::remotePlayState() const {
    // The engine's own remote-play state word, or empty when there is none. No
    // invented "connected" wording.
    return utf8Of(impl_->snapshot.remoteRecoveryMessage);
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
        const QString version = object.value(QStringLiteral("version")).toString();
        const QString signature = object.value(QStringLiteral("signature")).toString();
        const bool experimental = object.value(QStringLiteral("experimental")).toBool();
        item["detail"] = QStringList{version, signature}.join(QStringLiteral(" · "));
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
    // The source's own enumeration. An empty list means the machine reports no
    // capture devices, and the dialog says so rather than listing examples.
    QVariantList out;
    for (const auto& name : source::CaptureCardSource::devices(false)) {
        QVariantMap item;
        item["id"] = utf8Of(name);
        item["label"] = utf8Of(name);
        out << item;
    }
    return out;
}
QString QmlPlayerBridge::captureDeviceId() const { return utf8Of(impl_->captureDevice); }
void QmlPlayerBridge::setCaptureDeviceId(const QString& value) {
    impl_->captureDevice = wideOf(value);
    emit captureChanged();
}
QString QmlPlayerBridge::captureDeviceLabel() const {
    const QString id = captureDeviceId();
    return id.isEmpty() ? tr("未选择设备") : id;
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
    // Enumerated from the source, both kinds, so the dialog can offer windows and
    // monitors from what the machine actually has.
    QVariantList out;
    for (auto kind : {source::ScreenTargetKind::Window, source::ScreenTargetKind::Monitor}) {
        for (const auto& target : source::ScreenCaptureSource::targets(kind)) {
            QVariantMap item;
            item["id"] = QString::number(target.handle);
            QString label = QString::fromWCharArray(target.name.c_str());
            if (target.width > 0)
                label += QStringLiteral(" · %1x%2").arg(target.width).arg(target.height);
            if (target.refresh > 0) label += QStringLiteral(" · %1Hz").arg(target.refresh);
            item["label"] = label;
            out << item;
        }
    }
    return out;
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
    // Re-enumeration is what the dialog's refresh does; the lists are read fresh on
    // every call, so this only has to tell the UI to ask again.
    emit captureChanged();
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
    {"splitHighlights","分离高光",     "颜色分级", -100,  100,   1},
    {"splitMidtones", "分离中间调",    "颜色分级", -100,  100,   1},
    {"splitShadows",  "分离阴影",      "颜色分级", -100,  100,   1},
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
    return nullptr;
}
} // namespace

QVariantList QmlPlayerBridge::colourParameters() const {
    QVariantList out;
    for (const auto& p : kColourParams) {
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = QString::fromUtf8(p.label);
        item["group"] = QString::fromUtf8(p.group);
        item["min"] = double(p.minimum);
        item["max"] = double(p.maximum);
        item["step"] = double(p.step);
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
        const QString group = QString::fromUtf8(p.group);
        if (!buckets.contains(group)) order << group;
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = QString::fromUtf8(p.label);
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
    auto c = settings().color;
    const auto utf8 = name.toUtf8();
    if (float* field = colourScalar(c, utf8.constData())) return double(*field);
    return 0.0;
}

bool QmlPlayerBridge::setColourParameter(const QString& name, double value) {
    auto s = settings();
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(s.color, utf8.constData());
    if (field == nullptr) return false;
    const float next = float(value);
    if (*field == next) return true;
    *field = next;
    // Colour is live in the engine: it uploads per frame, so this never rebuilds the
    // graph and does not need a rebuild check.
    impl_->commit(std::move(s));
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::resetColourParameter(const QString& name) {
    auto s = settings();
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(s.color, utf8.constData());
    if (field == nullptr) return false;
    *field = 0.0f;
    impl_->commit(std::move(s));
    emit settingsChanged();
    return true;
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
QString QmlPlayerBridge::statusText() const { return utf8Of(impl_->snapshot.status); }
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
double QmlPlayerBridge::submitFps() const { return impl_->snapshot.submissionFps.value_or(0.0); }
bool QmlPlayerBridge::submitFpsKnown() const { return impl_->snapshot.submissionFps.has_value(); }
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
QString QmlPlayerBridge::backendWarning() const { return utf8Of(impl_->snapshot.backendWarning); }

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

VEYRA_BOOL_PROP(nrEnabled, setNrEnabled, nr)
VEYRA_BOOL_PROP(srEnabled, setSrEnabled, sr)
VEYRA_BOOL_PROP(lowLatency, setLowLatency, lowLatency)
VEYRA_BOOL_PROP(nrTemporal, setNrTemporal, nrTemporal)
VEYRA_BOOL_PROP(videoHdr, setVideoHdr, videoHdr.enabled)
VEYRA_BOOL_PROP(protectionEnabled, setProtectionEnabled, protection.enabled)
VEYRA_BOOL_PROP(colorEnabled, setColorEnabled, color.enabled)
VEYRA_NUM_PROP(fgMultiplier, setFgMultiplier, multiplier, int)
VEYRA_NUM_PROP(videoSrQuality, setVideoSrQuality, videoSrQuality, int)
VEYRA_NUM_PROP(audioOffsetMs, setAudioOffsetMs, audioOffsetMs, int)
VEYRA_NUM_PROP(nrIntensity, setNrIntensity, model.intensity, double)
VEYRA_NUM_PROP(nrTone, setNrTone, model.tone, double)
VEYRA_NUM_PROP(nrStructure, setNrStructure, model.structure, double)
VEYRA_NUM_PROP(nrSkin, setNrSkin, model.skin, double)
#undef VEYRA_BOOL_PROP
#undef VEYRA_NUM_PROP

// The two NR switches the engine keeps as small integers. Kept separate from
// the macro because the property is a bool and the field is not.
bool QmlPlayerBridge::nrAutoMask() const { return settings().model.autoMask != 0; }
void QmlPlayerBridge::setNrAutoMask(bool value) {
    auto s = settings();
    const int32_t want = value ? 1 : 0;
    if (s.model.autoMask == want) return;
    s.model.autoMask = want;
    impl_->commit(std::move(s));
    emit settingsChanged();
}
bool QmlPlayerBridge::nrUiCorrection() const { return settings().model.uiCorrection != 0; }
void QmlPlayerBridge::setNrUiCorrection(bool value) {
    auto s = settings();
    const int32_t want = value ? 1 : 0;
    if (s.model.uiCorrection == want) return;
    s.model.uiCorrection = want;
    impl_->commit(std::move(s));
    emit settingsChanged();
}
int QmlPlayerBridge::nrStyle() const { return int(settings().model.style); }
void QmlPlayerBridge::setNrStyle(int value) {
    auto s = settings();
    if (s.model.style == value) return;
    s.model.style = value;
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

// --- chain ------------------------------------------------------------------
// The settings the UI is showing: the facade's pending copy, which the engine
// has been asked to apply. One accessor so every property reads the same value.
engine::EnhancementSettings QmlPlayerBridge::settings() const {
    return impl_->facade.pendingSettings();
}

QVariantList QmlPlayerBridge::chain() const {
    QVariantList out;
    const auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i) {
        const auto& node = c.nodes[i];
        const auto& info = engine::effectInfo(node.type);
        QVariantMap item;
        item["index"] = int(i);
        item["type"] = utf8Of(info.id);
        item["label"] = utf8Of(info.label);
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

QVariantList QmlPlayerBridge::effectCatalog() const {
    QVariantList out;
    for (const auto& info : engine::effectCatalog()) {
        QVariantMap item;
        item["id"] = utf8Of(info.id);
        item["label"] = utf8Of(info.label);
        item["maxInstances"] = int(info.maxInstances);
        item["repeatable"] = info.repeatable;
        item["mustBeLast"] = info.mustBeLast;
        item["justBeforeLast"] = info.justBeforeLast;
        item["experimental"] = info.experimental;
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::chainError() const { return utf8Of(impl_->validation.message); }
bool QmlPlayerBridge::chainValid() const { return impl_->validation.accepted; }
int QmlPlayerBridge::nodeMode() const { return impl_->chain.mode == engine::ChainMode::Node ? 1 : 0; }
void QmlPlayerBridge::setNodeMode(int mode) {
    const auto want = mode ? engine::ChainMode::Node : engine::ChainMode::List;
    if (impl_->chain.mode == want) return;
    impl_->chain.mode = want;
    emit chainChanged();
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
        item["note"] = utf8Of(e.note);
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
    if (!session.devicePath.empty()) parts << QFileInfo(utf8Of(session.devicePath)).fileName();
    if (!session.formatKey.empty()) parts << utf8Of(session.formatKey);
    if (session.fps > 0.01) parts << QStringLiteral("%1 fps").arg(session.fps, 0, 'f', 2);
    if (!session.presetName.empty()) parts << tr("预设「%1」").arg(utf8Of(session.presetName));
    return parts.join(QStringLiteral(" · "));
}

void QmlPlayerBridge::resumeCaptureSession() {
    const auto& session = impl_->facade.lastCaptureSession();
    if (!session.devicePath.empty()) emit notice(tr("采集会话恢复尚未接入"), true);
}

QString QmlPlayerBridge::colorStatus() const { return utf8Of(impl_->snapshot.colorStatus); }
QString QmlPlayerBridge::videoHdrStatus() const { return utf8Of(impl_->snapshot.videoHdrStatus); }

// --- export -----------------------------------------------------------------
// Poll the export job once per UI tick. Its snapshot is the source of the
// numbers below; nothing here keeps a parallel counter that could drift.
void QmlPlayerBridge::pollExport() {
    impl_->exportSnapshot = impl_->exportJob.poll();
    emit exportChanged();
}

bool QmlPlayerBridge::exportRunning() const { return impl_->exportSnapshot.active(); }
bool QmlPlayerBridge::exportPaused() const {
    return impl_->exportSnapshot.state == engine::ExportState::Paused;
}
double QmlPlayerBridge::exportProgress() const { return impl_->exportSnapshot.progress; }
QString QmlPlayerBridge::exportStatus() const {
    switch (impl_->exportSnapshot.state) {
    case engine::ExportState::Idle: return tr("未开始");
    case engine::ExportState::Preparing: return tr("准备中…");
    case engine::ExportState::Running: return tr("导出中…");
    case engine::ExportState::Paused: return tr("已暂停");
    case engine::ExportState::Finishing: return tr("收尾中…");
    case engine::ExportState::Succeeded: return tr("已完成");
    case engine::ExportState::Failed: return tr("失败");
    case engine::ExportState::Cancelled: return tr("已取消");
    }
    return {};
}
QString QmlPlayerBridge::exportTarget() const {
    const auto& out = impl_->exportSnapshot.output;
    return out.empty() ? utf8Of(impl_->exportOutput) : utf8Of(out);
}
int QmlPlayerBridge::exportEncoded() const { return int(impl_->exportSnapshot.encoded); }
int QmlPlayerBridge::exportGenerated() const { return int(impl_->exportSnapshot.generated); }
double QmlPlayerBridge::exportEtaSeconds() const { return impl_->exportSnapshot.etaSeconds; }
int QmlPlayerBridge::exportQueueCount() const { return int(impl_->exportSnapshot.queued); }

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
    if (index < -1 || index > 3 || impl_->exportResolutionIndex == index) return;
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
    impl_->exportTrimEndSeconds = value;
    emit exportChanged();
}

QString QmlPlayerBridge::srTargetLabel() const {
    // The label follows the SR target, because that is what sets the output size.
    switch (srTargetIndex()) {
    case 1: return QStringLiteral("2K");
    case 2: return QStringLiteral("4K");
    case 3: return QStringLiteral("8K");
    default: return tr("源尺寸");
    }
}

int QmlPlayerBridge::srTargetIndex() const {
    // 0 keeps the source size; 1..3 are the engine's Qhd/Uhd4K/Uhd8K targets.
    switch (settings().srTarget) {
    case pipeline::SrTarget::Qhd: return 1;
    case pipeline::SrTarget::Uhd4K: return 2;
    case pipeline::SrTarget::Uhd8K: return 3;
    }
    return 0;
}
void QmlPlayerBridge::setSrTargetIndex(int index) {
    auto s = settings();
    pipeline::SrTarget want = pipeline::SrTarget::Uhd4K;
    switch (index) {
    case 1: want = pipeline::SrTarget::Qhd; break;
    case 2: want = pipeline::SrTarget::Uhd4K; break;
    case 3: want = pipeline::SrTarget::Uhd8K; break;
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
    lines << tr("状态: %1").arg(utf8Of(s.status));
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
    if (!s.backendWarning.empty()) lines << tr("后端告警: %1").arg(utf8Of(s.backendWarning));
    if (!s.colorStatus.empty()) lines << tr("色彩: %1").arg(utf8Of(s.colorStatus));
    if (!s.videoHdrStatus.empty()) lines << tr("Video HDR: %1").arg(utf8Of(s.videoHdrStatus));
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
    impl_->exportTrimStartSeconds = 0.0;
    impl_->exportTrimEndSeconds = 0.0;
    emit exportChanged();
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    const std::wstring wide = wideOf(path);
    impl_->sourceLabel = wide;
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
    if (impl_->preOpen) impl_->preOpen();
    impl_->engine.open(impl_->videoWindow, wide, impl_->options);
    impl_->openingSessionId = impl_->engine.snapshot().sessionId;
}

void QmlPlayerBridge::togglePlayPause() { impl_->engine.pause(impl_->snapshot.running); }
void QmlPlayerBridge::stopPlayback() {
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
    impl_->engine.comparison(held ? 1 : 0, false);
    veyra::log::info("ui-compare", held ? "hold original on" : "hold original off");
}
void QmlPlayerBridge::stepFrame(int direction) {
    // A frame step is a small seek. The engine owns the real frame duration, so
    // this is honest about being approximate rather than pretending to be exact.
    if (!direction) return;
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + (direction > 0 ? 1.0 / 60.0 : -1.0 / 60.0)));
}

void QmlPlayerBridge::takeScreenshot() {
    const QString name = QStringLiteral("veyra-%1.png")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const std::wstring path = wideOf(QDir(QDir::homePath()).filePath(name));
    impl_->engine.saveFrame(path);
    emit notice(tr("已保存截图: %1").arg(utf8Of(path)), false);
}

void QmlPlayerBridge::chooseExportPath() {
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出到"), QString(),
                                                      tr("MP4 视频 (*.mp4);;所有文件 (*)"));
    setExportPath(path);
}

void QmlPlayerBridge::setExportPath(const QString& path) {
    if (path.isEmpty()) return;
    impl_->exportOutput = wideOf(path);
    impl_->exportStatus = QFileInfo(path).fileName();
    emit exportChanged();
}

void QmlPlayerBridge::startExport() {
    if (impl_->exportOutput.empty()) { emit notice(tr("先选择导出位置"), true); return; }
    if (impl_->sourceLabel.empty()) { emit notice(tr("先打开要导出的文件"), true); return; }
    // The settings are frozen at start, exactly as the engine's job expects: an
    // export must not change under the user mid-run.
    auto settings = impl_->facade.pendingSettings();
    engine::fromChain(impl_->chain, settings);
    if (!impl_->exportPresetName.empty()) {
        const auto& entries = impl_->facade.presets().entries();
        const auto preset = std::find_if(entries.begin(), entries.end(), [this](const auto& entry) {
            return entry.name == impl_->exportPresetName;
        });
        if (preset == entries.end()) { emit notice(tr("导出预设已不存在，请重新选择"), true); return; }
        if (preset->kind == engine::ChainMode::Node) {
            emit notice(tr("节点预设尚未接入导出执行器"), true);
            return;
        }
        engine::PresetLibrary::apply(*preset, settings);
    }
    settings.exportBitrateMbps = impl_->exportBitrateMbps;
    if (impl_->exportResolutionIndex >= 0) {
        settings.sr = impl_->exportResolutionIndex != 0;
        if (settings.sr) {
            switch (impl_->exportResolutionIndex) {
            case 1: settings.srTarget = pipeline::SrTarget::Qhd; break;
            case 2: settings.srTarget = pipeline::SrTarget::Uhd4K; break;
            case 3: settings.srTarget = pipeline::SrTarget::Uhd8K; break;
            default: break;
            }
        }
    }
    if (!settings.validate().empty()) { emit notice(tr("导出设置无效"), true); return; }
    veyra::log::info("qml-export", std::format("preset={} revision={} nr={} sr={} fg={} bitrate={} srTarget={}",
        impl_->exportPresetName.empty() ? "current" : utf8Of(impl_->exportPresetName).toStdString(),
        settings.revision, settings.nr, settings.sr, settings.multiplier,
        settings.exportBitrateMbps, settings.sr ? int(settings.srTarget) : -1));
    const bool started = impl_->exportJob.start(impl_->sourceLabel, impl_->exportOutput,
                                                settings, impl_->exportHevc, 0,
                                                impl_->snapshot.selectedAudioTrack,
                                                impl_->exportTrimStartSeconds,
                                                impl_->exportTrimEndSeconds,
                                                impl_->exportRateControl);
    if (!started) { emit notice(tr("导出启动失败；详见诊断"), true); return; }
    pollExport();
    emit navigate(QStringLiteral("exp"));
}

void QmlPlayerBridge::cancelExport() {
    impl_->exportJob.cancel();
    pollExport();
}

void QmlPlayerBridge::quit() {
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) app->quit();
}

void QmlPlayerBridge::openCaptureDialog() { emit navigate(QStringLiteral("capture")); }
void QmlPlayerBridge::openPs5Dialog() { emit navigate(QStringLiteral("ps5")); }
void QmlPlayerBridge::openScreenCaptureDialog() { emit navigate(QStringLiteral("screen")); }

// --- chain editing ----------------------------------------------------------
// The validator is the authority: an edit that would put frame generation or
// RTX Video HDR where the pipeline cannot honour them is refused with a reason,
// not silently dropped.
int QmlPlayerBridge::addEffect(const QString& type) {
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

    auto& c = impl_->chain;
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return -1; }
    const auto& info = engine::effectInfo(wanted);
    if (c.countOf(wanted) >= info.maxInstances) {
        emit notice(tr("%1 最多 %2 个").arg(utf8Of(info.label)).arg(info.maxInstances), true);
        return -1;
    }

    // Insert before a pinned tail so the validator never has to undo the edit:
    // frame generation stays last and RTX Video HDR stays in front of it.
    uint32_t insertAt = c.nodeCount;
    if (wanted != engine::EffectType::FrameGeneration) {
        for (uint32_t i = 0; i < c.nodeCount; ++i) {
            const auto& nodeInfo = engine::effectInfo(c.nodes[i].type);
            if (nodeInfo.mustBeLast) { insertAt = i; break; }
        }
    }
    for (uint32_t i = c.nodeCount; i > insertAt; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[insertAt] = engine::ChainNode{};
    c.nodes[insertAt].type = wanted;
    c.nodes[insertAt].enabled = true;
    // A new node lands below the others in node mode, in a stable column.
    c.nodes[insertAt].viewX = 40.0f + float(insertAt % 4) * 260.0f;
    c.nodes[insertAt].viewY = 40.0f + float(insertAt / 4) * 200.0f;
    ++c.nodeCount;

    impl_->revalidate();
    emit chainChanged();
    if (!impl_->validation.accepted) {
        emit notice(utf8Of(impl_->validation.message), true);
    } else {
        emit settingsChanged();
    }
    return int(insertAt);
}

bool QmlPlayerBridge::removeEffect(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    const auto& info = engine::effectInfo(c.nodes[index].type);
    if (info.mustBeLast) { emit notice(tr("补帧固定为最后一步，不能删除"), true); return false; }
    for (uint32_t i = uint32_t(index); i + 1 < c.nodeCount; ++i) c.nodes[i] = c.nodes[i + 1];
    --c.nodeCount;
    impl_->revalidate();
    emit chainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::moveEffect(int from, int to) {
    auto& c = impl_->chain;
    if (from < 0 || to < 0 || uint32_t(from) >= c.nodeCount || uint32_t(to) >= c.nodeCount) return false;
    // The two pinned stages cannot be dragged; the UI greys them, and the
    // engine refuses them too so a script cannot route around the UI.
    if (engine::effectInfo(c.nodes[from].type).mustBeLast ||
        engine::effectInfo(c.nodes[to].type).mustBeLast) {
        emit notice(tr("补帧固定为最后一步，不能拖动"), true);
        return false;
    }
    const auto moving = c.nodes[from];
    if (from < to) for (int i = from; i < to; ++i) c.nodes[i] = c.nodes[i + 1];
    else for (int i = from; i > to; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[to] = moving;

    const auto before = impl_->chain;
    impl_->revalidate();
    if (!impl_->validation.accepted) {
        impl_->chain = before;
        impl_->revalidate();
        emit notice(utf8Of(impl_->validation.message), true);
        return false;
    }
    emit chainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::setEffectEnabled(int index, bool enabled) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    if (c.nodes[index].enabled == enabled) return true;
    c.nodes[index].enabled = enabled;
    impl_->revalidate();
    emit chainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::setEffectPosition(int index, double x, double y) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
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
        out["label"] = utf8Of(info.label);
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
    if (!impl_->facade.applyPreset(size_t(index))) return false;
    impl_->chain = engine::toChain(impl_->facade.pendingSettings());
    impl_->revalidate();
    emit settingsChanged();
    emit chainChanged();
    return true;
}

void QmlPlayerBridge::enqueueExportFile(const QString& input, const QString& output) {
    if (input.isEmpty() || output.isEmpty()) { emit notice(tr("队列项目缺少输入或输出"), true); return; }
    auto settings = impl_->facade.pendingSettings();
    engine::fromChain(impl_->chain, settings);
    settings.exportBitrateMbps = impl_->exportBitrateMbps;
    const bool queued = impl_->exportJob.enqueue(wideOf(input), wideOf(output), settings, impl_->exportHevc, 0,
                                                  -1, 0.0, 0.0, impl_->exportRateControl);
    if (!queued) { emit notice(tr("加入导出队列失败；详见诊断"), true); return; }
    pollExport();
}

void QmlPlayerBridge::addExportFilesDialog() {
    const auto files = QFileDialog::getOpenFileNames(nullptr, tr("加入导出队列"), QString(),
                                                      tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts);;所有文件 (*)"));
    if (files.isEmpty()) return;
    const QString folder = QFileDialog::getExistingDirectory(nullptr, tr("选择队列输出目录"));
    if (folder.isEmpty()) return;
    for (const auto& file : files) {
        const QFileInfo info(file);
        enqueueExportFile(file, QDir(folder).filePath(info.completeBaseName() + QStringLiteral(".mp4")));
    }
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
    engine::PresetEntry entry;
    entry.name = wideOf(name);
    entry.kind = nodeMode ? engine::ChainMode::Node : engine::ChainMode::List;
    entry.contents = uint32_t(contentsMask);
    entry.chain = impl_->chain;
    entry.color = impl_->facade.pendingSettings().color;
    entry.fg.multiplier = impl_->facade.pendingSettings().multiplier;
    entry.audioOffsetMs = impl_->facade.pendingSettings().audioOffsetMs;
    const bool ok = impl_->facade.savePreset(entry, false);
    if (ok) emit presetsChanged();
    else emit notice(tr("保存预设失败: %1").arg(utf8Of(impl_->facade.error())), true);
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
        chainParts << utf8Of(engine::effectInfo(impl_->chain.nodes[i].type).label);
    add("chain", tr("效果链"),
        chainParts.isEmpty() ? tr("空") : chainParts.join(QStringLiteral(" → ")),
        impl_->chain.nodeCount > 0);
    add("color", tr("色彩"), s.color.enabled ? tr("已启用") : tr("未启用"), s.color.enabled);
    add("fg", tr("补帧"),
        s.multiplier > 1 ? QStringLiteral("%1X").arg(s.multiplier) : tr("关闭"),
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
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool ok = impl_->facade.presets().setDefault(size_t(index));
    if (ok) emit presetsChanged();
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

} // namespace veyra::ui
