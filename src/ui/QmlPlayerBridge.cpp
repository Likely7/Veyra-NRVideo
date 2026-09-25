#include "veyra/ui/QmlPlayerBridge.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "veyra/Log.h"

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

    // The chain the UI edits, kept in step with the facade's pending settings.
    engine::EffectChain chain;
    engine::ChainValidation validation;

    // The window the engine presents into, and the last file the UI opened.
    HWND videoWindow = nullptr;
    std::wstring sourceLabel;
    std::function<void()> preOpen;

    // Export progress is mirror state: the engine reports an export through a
    // progress callback, and there is no export status in PlayerSnapshot yet.
    bool exportRunning = false;
    std::wstring exportOutput;
    QString exportStatus;
    engine::PlayerOptions options;

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
    // 60 Hz ceiling, and it does nothing at all when the snapshot is unchanged.
    impl_->timer = new QTimer(this);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        const bool had = impl_->haveSnapshot;
        const uint64_t before = impl_->publishedRevision;
        impl_->poll();
        if (!had || impl_->publishedRevision != before) emit snapshotChanged();
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
bool QmlPlayerBridge::exportRunning() const { return impl_->exportRunning; }
QString QmlPlayerBridge::exportStatus() const { return impl_->exportStatus; }
QString QmlPlayerBridge::exportTarget() const { return utf8Of(impl_->exportOutput); }

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
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("打开视频或图片"), QString(),
        tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts *.jpg *.jpeg *.png *.bmp *.webp);;所有文件 (*)"));
    if (!path.isEmpty()) openPath(path);
}

void QmlPlayerBridge::openPath(const QString& path) {
    if (path.isEmpty()) return;
    const std::wstring wide = wideOf(path);
    impl_->sourceLabel = wide;
    impl_->facade.noteRecentFile(wide);
    emit recentFilesChanged();
    // Switch to a page with a video area, then settle the native window's
    // geometry, and only then open. The presenter samples the window's client
    // size once at initialisation; opening before that point is what produced a
    // 1x1 swapchain.
    emit navigate(QStringLiteral("minimal"));
    if (impl_->preOpen) impl_->preOpen();
    impl_->engine.open(impl_->videoWindow, wide, impl_->options);
}

void QmlPlayerBridge::togglePlayPause() { impl_->engine.pause(impl_->snapshot.running); }
void QmlPlayerBridge::stopPlayback() { impl_->engine.stop(); }
void QmlPlayerBridge::seekTo(double seconds) { impl_->engine.seek(seconds); }
void QmlPlayerBridge::seekBy(double seconds) {
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + seconds));
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
    if (path.isEmpty()) return;
    impl_->exportOutput = wideOf(path);
    impl_->exportStatus = QFileInfo(path).fileName();
    emit exportChanged();
}

void QmlPlayerBridge::startExport() {
    if (impl_->exportOutput.empty()) { emit notice(tr("先选择导出位置"), true); return; }
    if (impl_->sourceLabel.empty() || impl_->snapshot.capture) {
        emit notice(tr("先打开要导出的文件"), true);
        return;
    }
    impl_->engine.startExport(impl_->sourceLabel, impl_->exportOutput, impl_->options, false);
    impl_->exportRunning = true;
    impl_->exportStatus = tr("导出中…");
    emit exportChanged();
    emit navigate(QStringLiteral("export"));
}

void QmlPlayerBridge::cancelExport() {
    impl_->engine.stop();
    impl_->exportRunning = false;
    impl_->exportStatus = tr("已取消");
    emit exportChanged();
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
    const bool ok = impl_->facade.presets().erase(size_t(index));
    if (ok) emit presetsChanged();
    return ok;
}

bool QmlPlayerBridge::renamePreset(int index, const QString& name) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool ok = impl_->facade.presets().rename(size_t(index), wideOf(name));
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
