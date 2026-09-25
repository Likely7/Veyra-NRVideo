#include "veyra/ui/PlayerUiFacade.h"

#include "veyra/RuntimePaths.h"
#include "veyra/engine/PresetStore.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
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
      dataDirectory_(dataDirectory.empty() ? veyra::runtime::localDataDirectory() : dataDirectory) {}

PlayerUiFacade::Frame PlayerUiFacade::poll() {
    // The engine owns the snapshot lock; taking it here and comparing keeps the
    // UI free of the read-modify pattern it used to do at 4 Hz.
    const auto snapshot = engine_.snapshot();
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

bool PlayerUiFacade::applySettings(const veyra::engine::EnhancementSettings& settings) {
    pending_ = settings;
    return engine_.requestSettings(settings);
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
    return true;
}
}
