#pragma once
#include "veyra/engine/EngineController.h"
#include "veyra/engine/PresetLibrary.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace veyra::ui {
// The interface the user interface talks to.
//
// The QML front end must not hold EngineController, D3D12 objects, decoded
// frames or the audio clock, and it must not poll a 70-field snapshot at a
// fixed rate. This facade:
//   * exposes one snapshot per change, tagged with a counter the UI compares
//     (so a binding updates only what moved);
//   * turns commands into queued actions and never blocks the UI thread;
//   * owns the user-facing services: preset library, recent files, the last
//     capture session.
//
// It is deliberately free of Win32 and Qt so the same code serves both the
// transitional Win32 shell and the QML front end.
class PlayerUiFacade {
public:
    // A snapshot plus the counter the UI uses to skip unchanged data.
    struct Frame {
        veyra::engine::PlayerSnapshot snapshot;
        uint64_t revision = 0;      // increments whenever `snapshot` changed
        bool changed = false;
    };

    explicit PlayerUiFacade(veyra::engine::EngineController& engine,
                            std::filesystem::path dataDirectory = {});

    // --- snapshot -----------------------------------------------------------
    // Cheap: compares the engine's snapshot against the last one handed out and
    // only reports a change when something actually differs.
    Frame poll();
    const veyra::engine::PlayerSnapshot& last() const { return last_; }
    uint64_t revision() const { return revision_; }

    // --- commands ----------------------------------------------------------
    // Applied to the engine's desired settings, keeping the facade's copy in
    // step so the UI can show the pending value before the engine confirms it.
    bool applySettings(const veyra::engine::EnhancementSettings& settings);
    veyra::engine::EnhancementSettings pendingSettings() const { return pending_; }
    void setPending(veyra::engine::EnhancementSettings settings) { pending_ = std::move(settings); }
    // True while the engine has not yet applied what the UI asked for.
    bool applying() const;

    // --- presets -----------------------------------------------------------
    veyra::engine::PresetLibrary& presets() { return presets_; }
    const veyra::engine::PresetLibrary& presets() const { return presets_; }
    bool applyPreset(size_t index);
    // Saves the settings the UI is currently showing (pending, not applied -
    // the user saves what they see).
    bool savePreset(const veyra::engine::PresetEntry& entry, bool replace);
    // Migrates the older single-purpose stores the first time the library is
    // empty, so nobody loses their presets. Never overwrites an existing name.
    bool importLegacyStores();

    // --- recent files ------------------------------------------------------
    struct RecentEntry {
        std::wstring path;
        std::wstring label;
        bool exists = false;
        bool operator==(const RecentEntry&) const = default;
    };
    void noteRecentFile(const std::wstring& path);
    const std::vector<RecentEntry>& recentFiles() const { return recent_; }
    // Drops entries whose file no longer exists (checked lazily, not on every
    // poll: the list is short and the check is a filesystem call).
    void refreshRecentFiles();
    // Drops every recent entry and persists the empty list. The UI offers
    // this, so it has to actually clear rather than just re-scan.
    void clearRecentFiles();

    // --- shell preferences --------------------------------------------------
    // Reduced motion turns off the springs and transitions. Default page is what
    // the app opens on (home / minimal / professional). Both persist here so they
    // survive a restart, and both are preferences rather than enhancement settings:
    // they never affect the picture.
    bool reducedMotion() const { return reducedMotion_; }
    void setReducedMotion(bool value) { reducedMotion_ = value; savePreferences(); }
    const std::wstring& defaultPage() const { return defaultPage_; }
    void setDefaultPage(std::wstring value) { defaultPage_ = std::move(value); savePreferences(); }

    // --- last capture session ----------------------------------------------
    struct CaptureSession {
        std::wstring devicePath;
        std::wstring formatKey;
        std::wstring presetName;
        double fps = 0;
        bool valid = false;
        bool operator==(const CaptureSession&) const = default;
    };
    // Records what the user last captured with, including the preset in use, so
    // the home screen can offer "continue last session" in one click.
    void noteCaptureSession(const CaptureSession& session);
    const CaptureSession& lastCaptureSession() const { return captureSession_; }
    bool hasCaptureSession() const { return captureSession_.valid; }

    // --- persistence -------------------------------------------------------
    bool loadPreferences();
    bool savePreferences();
    const std::wstring& error() const { return error_; }

private:
    veyra::engine::EngineController& engine_;
    veyra::engine::PresetLibrary presets_;
    std::filesystem::path dataDirectory_;
    veyra::engine::PlayerSnapshot last_{};
    veyra::engine::EnhancementSettings pending_{};
    uint64_t revision_ = 0;
    bool haveLast_ = false;
    std::vector<RecentEntry> recent_;
    CaptureSession captureSession_{};
    bool reducedMotion_ = false;
    std::wstring defaultPage_ = L"home";
    std::wstring error_;
    std::filesystem::path preferencesPath() const;
    bool savePreferencesLocked();
};
}
