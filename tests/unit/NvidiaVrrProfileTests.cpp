#include "veyra/gfx/NvidiaVrrProfile.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace veyra::gfx;
namespace {
unsigned assertions = 0;
void require(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
VrrProfileSnapshot sample() {
    VrrProfileSnapshot s;
    s.executable = L"e:\\veyra\\veyra_qml_ui.exe"; s.profileName = L"Veyra test";
    s.exists = s.isolated = true; s.settingCount = 2;
    s.settings = {VrrProfileValue{1, true}, VrrProfileValue{1, true}};
    s.global = {VrrProfileValue{2, true}, VrrProfileValue{2, true}};
    return s;
}
void policyTests() {
    auto before = sample();
    auto plan = planVrrProfileRepair(before);
    require(plan.allowed && plan.changes() && !plan.createProfile, "repair a full-path single-app restriction");
    require(plan.edits[0] == VrrProfileEdit::Set && plan.edits[1] == VrrProfileEdit::Set
        && plan.values[0] == 2 && plan.values[1] == 2, "both scope values permit windowed presentation");
    auto repaired = before;
    repaired.settings = {VrrProfileValue{2, true}, VrrProfileValue{2, true}};
    require(repaired.configured() && !planVrrProfileRepair(repaired).changes(), "repeated repair is a no-op");
    auto unsafe = before; unsafe.isolated = false;
    require(!planVrrProfileRepair(unsafe).allowed && !planVrrProfileRestore(before, unsafe).allowed,
        "global, predefined, shared and basename matches must not be written");
    auto otherInstall = repaired; otherInstall.executable = L"e:\\new-version\\veyra_qml_ui.exe";
    require(!planVrrProfileRestore(before, otherInstall).allowed, "a backup cannot target another installation");
    otherInstall = repaired; otherInstall.profileName = L"Another profile";
    require(!planVrrProfileRestore(before, otherInstall).allowed, "a backup cannot target a replacement profile");
    auto drift = repaired; drift.settings[0] = {0, true};
    require(!planVrrProfileRestore(before, drift).allowed, "restoration preserves a later independent edit");
    auto inherited = before; inherited.settings[0] = {2, false};
    plan = planVrrProfileRestore(inherited, repaired);
    require(plan.allowed && plan.edits[0] == VrrProfileEdit::Remove && plan.edits[1] == VrrProfileEdit::Set,
        "restore inheritance by deleting the override instead of freezing a global value");
    auto globalChanged = repaired; globalChanged.global[0] = {0, true};
    require(planVrrProfileRestore(inherited, globalChanged).allowed, "restore scope does not restore global settings");
    auto missing = before; missing.exists = false; missing.settingCount = 0; missing.settings = {};
    plan = planVrrProfileRepair(missing);
    require(plan.allowed && plan.createProfile, "a missing path gets its own profile");
    require(planVrrProfileRestore(missing, repaired).removeProfile, "restore removes an otherwise untouched new profile");
    auto extended = repaired; extended.settingCount = 3;
    plan = planVrrProfileRestore(missing, extended);
    require(plan.allowed && !plan.removeProfile && plan.edits[0] == VrrProfileEdit::Remove
        && plan.edits[1] == VrrProfileEdit::Remove, "preserve other settings added to a newly created profile");
    require(!planVrrProfileRestore(missing, missing).changes(), "restoring an already removed profile is a no-op");
    auto unknown = before; unknown.settings[0].value = 99;
    require(!planVrrProfileRepair(unknown).allowed, "do not reinterpret unknown driver enum values");
    // Cover the combinations of explicit Off/fullscreen/windowed and inherited
    // values. The rollback policy must preserve how each value was supplied.
    for (unsigned a = 0; a != 4; ++a) for (unsigned b = 0; b != 4; ++b) {
        auto original = before;
        original.settings[0] = {a == 3 ? 1u : a, a != 3};
        original.settings[1] = {b == 3 ? 1u : b, b != 3};
        const auto restore = planVrrProfileRestore(original, repaired);
        require(restore.allowed && !restore.removeProfile, "rollback keeps an existing profile");
        for (size_t k = 0; k != 2; ++k) {
            const auto value = original.settings[k];
            require(restore.edits[k] == (!value.local ? VrrProfileEdit::Remove
                : value.value == 2 ? VrrProfileEdit::Keep : VrrProfileEdit::Set), "rollback preserves original local/inherited scope");
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--inspect") {
            // Deliberately read-only. No test option invokes a driver mutation.
            const auto result = inspectVrrProfile(std::filesystem::path(argv[2]).wstring());
            std::cout << "error=" << int(result.error) << " status=" << result.driverStatus
                << " isolated=" << result.snapshot.isolated << " configured=" << result.snapshot.configured()
                << " values=" << result.snapshot.settings[0].value << ',' << result.snapshot.settings[1].value << '\n';
            return result ? 0 : 1;
        }
        if (argc != 1) return 2;
        policyTests();
        std::cout << assertions << " VRR profile policy assertions passed (no driver writes)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
