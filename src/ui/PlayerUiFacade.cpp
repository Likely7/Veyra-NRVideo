#include "veyra/ui/PlayerUiFacade.h"

#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include "veyra/engine/PresetStore.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>

namespace veyra::ui {
namespace {
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n > 0 ? size_t(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
// Only the fields the UI can show are compared. Comparing the whole struct is
// fine too, but calling it out keeps the intent visible: a change here is what
// makes the UI repaint.
bool sameSnapshot(const veyra::engine::PlayerSnapshot& a, const veyra::engine::PlayerSnapshot& b) {
    return a.transport == b.transport && a.sessionId == b.sessionId && a.position == b.position &&
        a.duration == b.duration && a.frames == b.frames && a.generated == b.generated &&
        a.status == b.status && a.backendWarning == b.backendWarning && a.sourceNotice == b.sourceNotice &&
        a.applying == b.applying && a.rejectedRevision == b.rejectedRevision &&
        a.desired == b.desired && a.applied == b.applied && a.nrActive == b.nrActive &&
        a.srActive == b.srActive && a.fgActive == b.fgActive && a.failed == b.failed &&
        a.volume == b.volume && a.muted == b.muted && a.selectedAudioTrack == b.selectedAudioTrack &&
        a.audioTracks.size() == b.audioTracks.size() && a.running == b.running &&
        a.sourceWidth == b.sourceWidth && a.sourceHeight == b.sourceHeight &&
        a.sourceDisplayAspect == b.sourceDisplayAspect && a.sourceRotationDegrees == b.sourceRotationDegrees &&
        a.capture == b.capture && a.image == b.image &&
        a.playbackRate == b.playbackRate && a.vramRunawayMiB == b.vramRunawayMiB && a.vramFullscreenUnsafe == b.vramFullscreenUnsafe &&
        a.metrics.resolution.base.width == b.metrics.resolution.base.width &&
        a.metrics.resolution.base.height == b.metrics.resolution.base.height &&
        a.metrics.resolution.nr.width == b.metrics.resolution.nr.width &&
        a.metrics.resolution.nr.height == b.metrics.resolution.nr.height &&
        a.metrics.resolution.output.width == b.metrics.resolution.output.width &&
        a.metrics.resolution.output.height == b.metrics.resolution.output.height;
}
} // namespace

PlayerUiFacade::PlayerUiFacade(veyra::engine::EngineController& engine, std::filesystem::path dataDirectory)
    : engine_(engine),
      presets_((dataDirectory.empty() ? veyra::runtime::localDataDirectory() : dataDirectory) / L"presets.v1"),
      dataDirectory_(dataDirectory.empty() ? veyra::runtime::localDataDirectory() : dataDirectory),
      chainSessionStore_(dataDirectory_ / L"chain-session.v1") {}

bool PlayerUiFacade::loadChainSession(veyra::engine::ChainSession& session) {
    const bool loaded = chainSessionStore_.load(session);
    error_ = chainSessionStore_.error();
    return loaded;
}

bool PlayerUiFacade::saveChainSession(const veyra::engine::ChainSession& session) {
    if (chainSessionStore_.save(session)) return true;
    error_ = chainSessionStore_.error(); return false;
}

PlayerUiFacade::Frame PlayerUiFacade::poll() {
    // The engine owns the snapshot lock; taking it here and comparing keeps the
    // UI free of the read-modify pattern it used to do at 4 Hz.
    const auto snapshot = engine_.snapshot();
    if(pendingNrRevision_&&snapshot.rejectedRevision==pendingNrRevision_&&pending_.nrRuntime==requestedNrRuntime_) {
        pending_.nrRuntime=snapshot.desired.nrRuntime;pendingNrRevision_=0;
        for(uint32_t i=0;i<pending_.nrLayerCount&&i<pending_.nrLayers.size();++i)
            pending_.nrLayers[i].runtime=pending_.nrRuntime;
        veyra::log::info("ui-nr-rollback",std::format("rejectedRevision={} restoredRuntime={}",snapshot.rejectedRevision,veyra::engine::nrRuntimeName(pending_.nrRuntime)));
    }
    // A GPU/provider failure arrives after command admission. Match the exact
    // accepted revision so a stale rejection cannot overwrite a newer choice.
    // Reconcile only the FG pair; unrelated editable controls stay untouched.
    if(pendingFgRevision_&&snapshot.rejectedRevision==pendingFgRevision_&&
       pending_.frameGenerationBackend==requestedFgBackend_&&pending_.multiplier==requestedFgMultiplier_){
        pending_.frameGenerationBackend=snapshot.desired.frameGenerationBackend;
        pending_.multiplier=snapshot.desired.multiplier;pendingFgRevision_=0;
        veyra::log::info("ui-fg-rollback",std::format("rejectedRevision={} restoredBackend={} restoredMultiplier={}",
            snapshot.rejectedRevision,int(pending_.frameGenerationBackend),pending_.multiplier));
    }
    Frame frame;
    frame.snapshot = snapshot;
    frame.changed = !haveLast_ || !sameSnapshot(snapshot, last_);
    if (frame.changed) {
        last_ = snapshot;
        haveLast_ = true;
        ++revision_;
    }
    frame.revision = revision_;
    return frame;
}

bool PlayerUiFacade::applySettings(const veyra::engine::EnhancementSettings& settings,
                                  std::optional<veyra::engine::ChainRuntimeOrder> order) {
    uint64_t acceptedRevision=0;
    if (!engine_.requestSettings(settings, order,&acceptedRevision)) return false;
    veyra::log::info("ui-settings-request", std::format(
        "accepted srTarget={} quality={} fgBackend={} flowBackend={}",
        int(settings.srTarget), settings.videoSrQuality, int(settings.frameGenerationBackend), int(settings.opticalFlowBackend)));
    pending_ = settings;
    pendingFgRevision_=acceptedRevision;
    pendingNrRevision_=acceptedRevision;requestedNrRuntime_=settings.nrRuntime;
    requestedFgBackend_=settings.frameGenerationBackend;requestedFgMultiplier_=settings.multiplier;
    return true;
}

bool PlayerUiFacade::applying() const {
    // The engine publishes both the desired and the applied settings; a
    // difference means a transaction is still in flight.
    return engine_.snapshot().applying;
}

bool PlayerUiFacade::applyPreset(size_t index) {
    const auto& entries = presets_.entries();
    if (index >= entries.size()) { error_ = L"预设不存在"; return false; }
    auto settings = pending_;
    veyra::engine::PresetLibrary::apply(entries[index], settings);
    return applySettings(settings);
}

bool PlayerUiFacade::savePreset(const veyra::engine::PresetEntry& entry, bool replace) {
    if (!presets_.put(entry, replace)) { error_ = presets_.error(); return false; }
    return true;
}

bool PlayerUiFacade::importLegacyStores() {
    // The library has to be read first: importing into an unloaded library would
    // save it as built-ins plus imports and overwrite the user's own presets.v1.
    // A corrupt file stays loaded-as-corrupt, and the library then refuses to save.
    if (!presetsLoaded_) {
        presetsLoaded_ = true;
        // 2.0 ships no built-in presets (user decision 2026-09-29).
        presets_.setIncludeBuiltins(false);
        if (!presets_.load()) { error_ = presets_.error(); return false; }
        error_ = presets_.error(); // Successful legacy migration carries an explicit notice.
    }
    // The old files are read-only inputs: they stay on disk untouched so a
    // downgrade still finds them. Their positional schema is owned by
    // PresetStore, so the migration goes through that parser instead of
    // re-implementing it (and silently reading fields in the wrong order).
    std::vector<veyra::engine::PresetEntry> imported;
    auto readStore = [&imported](const std::filesystem::path& path, bool chainOnly) {
        if (!std::filesystem::exists(path)) return;
        veyra::engine::PresetStore store(path);
        if (!store.load()) return;   // corrupt or unsupported: leave it alone
        for (const auto& legacy : store.entries()) {
            if (legacy.name.empty()) continue;
            veyra::engine::PresetEntry entry;
            entry.name = legacy.name;
            entry.note = chainOnly ? L"由旧的 NR 预设导入" : L"由旧预设导入";
            entry.kind = veyra::engine::ChainMode::List;
            entry.builtin = false;
            if (chainOnly) {
                auto settings = legacy.settings;
                settings.color = {};
                settings.videoHdr = {};
                settings.multiplier = 1;
                entry.contents = veyra::engine::presetContentMask(veyra::engine::PresetContent::Chain);
                entry.chain = veyra::engine::toChain(settings);
            } else {
                entry.contents = veyra::engine::kPresetAllContent;
                entry.chain = veyra::engine::toChain(legacy.settings);
                entry.color = legacy.settings.color;
                entry.fg = {std::max(2u, legacy.settings.multiplier), legacy.settings.frameGenerationBackend};
                entry.audioSync = legacy.settings.audioSync;
                entry.audioOffsetMs = legacy.settings.audioOffsetMs;
            }
            imported.push_back(std::move(entry));
        }
    };
    readStore(dataDirectory_ / L"user-presets.v1", false);
    readStore(dataDirectory_ / L"nr-presets.v1", true);
    // Old colour looks are a separate store with its own schema; they keep their
    // own file and loader, so they are not duplicated into the library here.
    if (imported.empty()) return true;
    if (!presets_.importLegacy(imported)) { error_ = presets_.error(); return false; }
    return true;
}

void PlayerUiFacade::noteRecentFile(const std::wstring& path) {
    if (path.empty()) return;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
        [&](const RecentEntry& e) { return e.path == path; }), recent_.end());
    RecentEntry entry;
    entry.path = path;
    const auto slash = path.find_last_of(L"\\/");
    entry.label = slash == std::wstring::npos ? path : path.substr(slash + 1);
    entry.exists = true;
    recent_.insert(recent_.begin(), std::move(entry));
    if (recent_.size() > 8) recent_.resize(8);
    savePreferences();
}

void PlayerUiFacade::refreshRecentFiles() {
    for (auto& entry : recent_) entry.exists = std::filesystem::exists(entry.path);
}

void PlayerUiFacade::clearRecentFiles() {
    recent_.clear();
    savePreferences();
}

void PlayerUiFacade::noteCaptureSession(const CaptureSession& session) {
    captureSession_ = session;
    captureSession_.valid = !session.devicePath.empty();
    savePreferences();
}

std::filesystem::path PlayerUiFacade::preferencesPath() const {
    return dataDirectory_ / L"ui-session.v1";
}

bool PlayerUiFacade::savePreferencesLocked() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "VEYRA_UI_SESSION 1\n";
    out << recent_.size() << '\n';
    for (const auto& entry : recent_) out << std::quoted(utf8(entry.path)) << ' ' << std::quoted(utf8(entry.label)) << '\n';
    out << (captureSession_.valid ? 1 : 0) << ' ' << std::quoted(utf8(captureSession_.devicePath)) << ' '
        << std::quoted(utf8(captureSession_.formatKey)) << ' ' << std::quoted(utf8(captureSession_.presetName)) << ' '
        << captureSession_.fps << '\n';
    // Appended after the capture session so a file written by an earlier build
    // still loads: the reader treats a missing tail as defaults.
    out << (reducedMotion_ ? 1 : 0) << ' ' << std::quoted(utf8(defaultPage_)) << '\n';
    const auto temporary = std::filesystem::path(preferencesPath()).concat(L".tmp");
    { std::ofstream f(temporary, std::ios::binary | std::ios::trunc); if (!f) { error_ = L"界面状态写入失败"; return false; } f << out.str(); if (!f) { error_ = L"界面状态写入失败"; return false; } }
    if (!MoveFileExW(temporary.c_str(), preferencesPath().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        error_ = L"界面状态替换失败";
        return false;
    }
    return true;
}

bool PlayerUiFacade::savePreferences() { return savePreferencesLocked(); }

bool PlayerUiFacade::loadPreferences() {
    // Missing file is the normal first-run case.
    if (!std::filesystem::exists(preferencesPath())) return true;
    std::ifstream f(preferencesPath(), std::ios::binary);
    if (!f) { error_ = L"界面状态无法读取"; return false; }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::istringstream in(data);
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    size_t count = 0;
    if (!(in >> magic >> version) || magic != "VEYRA_UI_SESSION" || version != 1) { error_ = L"界面状态版本不支持"; return false; }
    if (!(in >> count) || count > 32) { error_ = L"界面状态条目数非法"; return false; }
    std::vector<RecentEntry> recent;
    for (size_t i = 0; i < count; ++i) {
        std::string path, label;
        if (!(in >> std::quoted(path) >> std::quoted(label))) { error_ = L"界面状态条目损坏"; return false; }
        RecentEntry entry;
        entry.path = wide(path);
        entry.label = wide(label);
        if (entry.path.empty()) continue;
        entry.exists = std::filesystem::exists(entry.path);
        recent.push_back(std::move(entry));
    }
    CaptureSession session;
    int valid = 0;
    std::string device, format, preset;
    if (!(in >> valid >> std::quoted(device) >> std::quoted(format) >> std::quoted(preset) >> session.fps)) { error_ = L"界面状态采集会话损坏"; return false; }
    session.devicePath = wide(device);
    session.formatKey = wide(format);
    session.presetName = wide(preset);
    session.valid = valid != 0 && !session.devicePath.empty();
    recent_ = std::move(recent);
    captureSession_ = std::move(session);
    // The shell preferences were appended after the capture session. A file from a
    // build that predates them simply has no tail, and that is not an error: the
    // defaults stand and the rest of the file is still valid.
    int reduced = 0;
    std::string defaultPage;
    if (in >> reduced >> std::quoted(defaultPage)) {
        reducedMotion_ = reduced != 0;
        if (!defaultPage.empty()) defaultPage_ = wide(defaultPage);
    }
    return true;
}
}
