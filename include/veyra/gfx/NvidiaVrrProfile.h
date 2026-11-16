#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace veyra::gfx {
inline constexpr std::array<uint32_t, 2> vrrProfileSettingIds{0x1194F158, 0x1094F1F7};
struct VrrProfileValue {
    uint32_t value = 0;
    bool local = false;
    bool operator==(const VrrProfileValue&) const = default;
};
struct VrrProfileSnapshot {
    std::wstring executable, profileName;
    bool exists = false;
    // True only for an exact full-path association in a custom, single-app
    // profile, or for a missing association that can get its own profile.
    bool isolated = false;
    uint32_t settingCount = 0;
    std::array<VrrProfileValue, 2> settings{}, global{};
    bool configured() const {
        return exists && isolated && settings[0] == VrrProfileValue{2, true}
            && settings[1] == VrrProfileValue{2, true};
    }
    bool operator==(const VrrProfileSnapshot&) const = default;
};

enum class VrrProfileError { None, Unavailable, Driver, UnsafeProfile, Changed, Verification, Busy };
struct VrrProfileResult {
    VrrProfileSnapshot snapshot;
    VrrProfileError error = VrrProfileError::None;
    int driverStatus = 0;
    bool changed = false;
    explicit operator bool() const { return error == VrrProfileError::None; }
};
enum class VrrProfileEdit { Keep, Set, Remove };
struct VrrProfilePlan {
    bool allowed = false, createProfile = false, removeProfile = false;
    std::array<VrrProfileEdit, 2> edits{};
    std::array<uint32_t, 2> values{};
    bool changes() const {
        return createProfile || removeProfile || edits[0] != VrrProfileEdit::Keep
            || edits[1] != VrrProfileEdit::Keep;
    }
};
// These policy functions do no I/O. In particular restoration removes an
// override that was previously inherited; it does not freeze the old global
// value into the application's profile.
inline VrrProfilePlan planVrrProfileRepair(const VrrProfileSnapshot& state) {
    VrrProfilePlan plan;
    if (!state.isolated || state.executable.empty() || state.profileName.empty()) return plan;
    for (const auto& value : state.settings) if (value.value > 2) return plan;
    plan.allowed = true;
    plan.createProfile = !state.exists;
    for (size_t k = 0; k < plan.edits.size(); ++k) {
        plan.edits[k] = state.settings[k] == VrrProfileValue{2, true}
            ? VrrProfileEdit::Keep : VrrProfileEdit::Set;
        plan.values[k] = 2;
    }
    return plan;
}
inline VrrProfilePlan planVrrProfileRestore(const VrrProfileSnapshot& original,
                                           const VrrProfileSnapshot& current) {
    VrrProfilePlan plan;
    if (!original.isolated || !current.isolated || original.executable.empty()
        || original.executable != current.executable || original.profileName != current.profileName) return plan;
    if (!current.exists) { plan.allowed = !original.exists; return plan; }
    for (size_t k = 0; k < plan.edits.size(); ++k) {
        const auto before = original.settings[k], now = current.settings[k];
        if ((!before.local && !now.local) || before == now) continue;
        // Someone changed it after our repair: preserve their change.
        if (now != VrrProfileValue{2, true}) return {};
        plan.edits[k] = before.local ? VrrProfileEdit::Set : VrrProfileEdit::Remove;
        plan.values[k] = before.value;
    }
    plan.allowed = true;
    const auto ownSettings = uint32_t(current.settings[0].local) + uint32_t(current.settings[1].local);
    // Only remove a profile we created if it has acquired no other settings.
    plan.removeProfile = !original.exists && current.settingCount == ownSettings;
    return plan;
}

// System32 NVAPI only. Each call uses its own DRS session. Mutations re-read
// and compare the expected snapshot before saving, then verify in a fresh
// session. Callers must durably save the original snapshot BEFORE repair.
VrrProfileResult inspectVrrProfile(const std::wstring& executable);
VrrProfileResult repairVrrProfile(const VrrProfileSnapshot& expected);
VrrProfileResult restoreVrrProfile(const VrrProfileSnapshot& original,
                                  const VrrProfileSnapshot& expected);
} // namespace veyra::gfx
