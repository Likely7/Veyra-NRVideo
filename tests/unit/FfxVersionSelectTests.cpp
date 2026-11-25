// FSR SR provider selection: the loader's ffxQueryDescGetVersions display names
// ("4.1.1") are parsed into semver, matched against the user's explicit FSR 3.1
// / 4.x choice, and ranked so the backend creates the newest matching provider.
// Pure standard library, so this runs on the host toolchain without FFX.
#include "veyra/gfx/FfxVersionSelect.h"

#include <iostream>
#include <string>
#include <vector>

using veyra::gfx::FsrSrVersionMode;
using veyra::gfx::matchesFsrSrVersion;
using veyra::gfx::parseFfxSemver;
using veyra::gfx::rankedFfxProviderIndices;

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::cout << "FAIL " << why << '\n'; }
}
void version(std::string_view name, uint32_t major, uint32_t minor, uint32_t patch) {
    const auto parsed = parseFfxSemver(name);
    check(parsed.valid, "semver parses");
    check(parsed.major == major && parsed.minor == minor && parsed.patch == patch, "semver components");
}
void invalid(std::string_view name, const char* why) {
    check(!parseFfxSemver(name).valid, why);
}
std::vector<size_t> expected(std::initializer_list<size_t> values) {
    return std::vector<size_t>(values);
}
} // namespace

int main() {
    // Accepted shapes: the loader's names, plus optional suffix/extra fields that
    // a future provider may append after the three numbers.
    version("3.1.5", 3, 1, 5);
    version("4.1.1-rc1", 4, 1, 1);
    version("4.1.1", 4, 1, 1);
    version("3.1", 3, 1, 0);
    version("4.0.0-abc.7", 4, 0, 0);
    version("0.0.0", 0, 0, 0);
    version("4.1.1 ", 4, 1, 1);
    // Rejected: nothing to select on, a wrong start, a truncated second field,
    // a negative number, or a component past 32 bits.
    invalid("", "empty name");
    invalid(" ", "blank name");
    invalid("fsr", "non-numeric name");
    invalid("v4.1.1", "prefixed name");
    invalid("4", "missing minor");
    invalid("4.", "missing minor digits");
    invalid("4.1.", "missing patch digits");
    invalid(".4.1", "missing major");
    invalid("-1.2", "negative major");
    invalid("4.-1", "negative minor");
    invalid("4294967296.0", "major overflow");
    invalid("4.4294967296", "minor overflow");
    invalid("4.1.4294967296", "patch overflow");
    // 32-bit components are still accepted; only past that is rejected.
    version("4294967295.4294967295.4294967295", 4294967295u, 4294967295u, 4294967295u);

    // Auto keeps the existing behaviour: any non-empty inventory entry is usable.
    check(matchesFsrSrVersion("4.1.1", FsrSrVersionMode::Auto), "auto accepts 4.x");
    check(matchesFsrSrVersion("3.1.5", FsrSrVersionMode::Auto), "auto accepts 3.1");
    check(matchesFsrSrVersion("custom", FsrSrVersionMode::Auto), "auto accepts unversioned");
    check(!matchesFsrSrVersion("", FsrSrVersionMode::Auto), "auto skips empty");
    // FSR 3.1 is exactly 3.1.x, not 3.0 or 3.2.
    check(matchesFsrSrVersion("3.1.5", FsrSrVersionMode::Fsr31), "3.1 matches 3.1");
    check(matchesFsrSrVersion("3.1", FsrSrVersionMode::Fsr31), "3.1 without patch matches 3.1");
    check(!matchesFsrSrVersion("3.0.9", FsrSrVersionMode::Fsr31), "3.0 is not 3.1");
    check(!matchesFsrSrVersion("3.2.0", FsrSrVersionMode::Fsr31), "3.2 is not 3.1");
    check(!matchesFsrSrVersion("4.1.1", FsrSrVersionMode::Fsr31), "4.x is not 3.1");
    check(!matchesFsrSrVersion("custom", FsrSrVersionMode::Fsr31), "unversioned is not 3.1");
    // FSR 4 is any 4.x.
    check(matchesFsrSrVersion("4.0.0", FsrSrVersionMode::Fsr4), "4.0 matches 4.x");
    check(matchesFsrSrVersion("4.1.1", FsrSrVersionMode::Fsr4), "4.1 matches 4.x");
    check(!matchesFsrSrVersion("3.1.5", FsrSrVersionMode::Fsr4), "3.1 is not 4.x");
    check(!matchesFsrSrVersion("5.0.0", FsrSrVersionMode::Fsr4), "5.0 is not 4.x");
    check(!matchesFsrSrVersion("", FsrSrVersionMode::Fsr4), "empty is not 4.x");

    // Auto: newest first across majors/minors/patches.
    const std::vector<std::string> inventory{"3.1.5", "4.1.1", "4.1.2"};
    check(rankedFfxProviderIndices(inventory, FsrSrVersionMode::Auto) == expected({2, 1, 0}), "auto ranks descending");
    // Equal versions keep inventory order: a repeated run picks the same provider.
    check(rankedFfxProviderIndices({"4.1.1", "4.1.1", "4.1.1"}, FsrSrVersionMode::Auto) == expected({0, 1, 2}),
          "auto keeps ties in inventory order");
    // An unversioned provider is still usable, but only after every ranked one.
    check(rankedFfxProviderIndices({"custom", "3.1.5", "amd"}, FsrSrVersionMode::Auto) == expected({1, 0, 2}),
          "auto appends unversioned names");
    // Empty names are never attempted.
    check(rankedFfxProviderIndices({"", "4.1.1", ""}, FsrSrVersionMode::Auto) == expected({1}), "auto skips empty");
    check(rankedFfxProviderIndices({}, FsrSrVersionMode::Auto).empty(), "auto with empty inventory");

    // Explicit modes filter first, then rank the survivors newest first.
    check(rankedFfxProviderIndices({"4.1.1", "3.1.5", "3.1.2"}, FsrSrVersionMode::Fsr31) == expected({1, 2}),
          "3.1 filters out 4.x and ranks");
    check(rankedFfxProviderIndices({"4.1.1", "3.1.5", "3.1.2"}, FsrSrVersionMode::Fsr4) == expected({0}),
          "4.x filters out 3.1");
    check(rankedFfxProviderIndices({"3.1.5", "4.0.0", "4.1.0"}, FsrSrVersionMode::Fsr4) == expected({2, 1}),
          "4.x ranks 4.1 before 4.0");
    // An explicit choice never falls back to an unversioned or wrong-major provider.
    check(rankedFfxProviderIndices({"custom", "3.0.5"}, FsrSrVersionMode::Fsr31).empty(),
          "3.1 rejects unversioned and 3.0");
    check(rankedFfxProviderIndices({"custom", "3.1.5"}, FsrSrVersionMode::Fsr4).empty(),
          "4.x rejects unversioned and 3.1");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
