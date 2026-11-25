#pragma once

// FidelityFX provider version selection, shared by the upscaler backend and its
// unit tests. The loader reports the installed providers through
// ffxQueryDescGetVersions (ffx_api.h:97) with display names such as "4.1.1";
// the user's explicit FSR 3.1 / 4.x choice is matched against those names here.
// Pure standard library, header-only, no FFX dependency.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::gfx {

// Wire/preset values: 0 keeps the previous automatic choice.
enum class FsrSrVersionMode : uint32_t { Auto = 0, Fsr31 = 1, Fsr4 = 2 };

struct FfxSemver {
    uint32_t major = 0, minor = 0, patch = 0;
    bool valid = false;
};

// Leading "MAJOR.MINOR[.PATCH]" of a provider name. Empty, a non-digit, a
// missing minor or an overflow to 32 bits is rejected; anything after the
// parsed components (a suffix, another field) is ignored.
inline FfxSemver parseFfxSemver(std::string_view name) {
    size_t i = 0;
    const auto number = [&](uint32_t& value) {
        if (i >= name.size() || name[i] < '0' || name[i] > '9') return false;
        uint64_t parsed = 0;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
            parsed = parsed * 10 + uint64_t(name[i] - '0');
            if (parsed > 0xFFFFFFFFu) return false;
            ++i;
        }
        value = uint32_t(parsed);
        return true;
    };
    FfxSemver parsed;
    if (!number(parsed.major)) return {};
    if (i >= name.size() || name[i] != '.') return {};
    ++i;
    if (!number(parsed.minor)) return {};
    if (i < name.size() && name[i] == '.') {
        ++i;
        if (!number(parsed.patch)) return {};
    }
    parsed.valid = true;
    return parsed;
}

inline bool matchesFsrSrVersion(std::string_view name, FsrSrVersionMode mode) {
    switch (mode) {
    case FsrSrVersionMode::Fsr31: {
        const auto version = parseFfxSemver(name);
        return version.valid && version.major == 3 && version.minor == 1;
    }
    case FsrSrVersionMode::Fsr4: {
        const auto version = parseFfxSemver(name);
        return version.valid && version.major == 4;
    }
    default: return !name.empty();
    }
}

// Indices into `names` the backend should try, best first. Descending
// (major, minor, patch); ties keep the inventory order so a repeated run picks
// the same provider. Auto prefers parseable names and appends any remaining
// non-empty entry (an unversioned provider is still usable, just unranked).
inline std::vector<size_t> rankedFfxProviderIndices(const std::vector<std::string>& names, FsrSrVersionMode mode) {
    struct Entry {
        size_t index = 0;
        FfxSemver version;
    };
    const auto ranked = [](const Entry& a, const Entry& b) {
        if (a.version.major != b.version.major) return a.version.major > b.version.major;
        if (a.version.minor != b.version.minor) return a.version.minor > b.version.minor;
        return a.version.patch > b.version.patch;
    };
    std::vector<Entry> parsed, unversioned;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!matchesFsrSrVersion(names[i], mode)) continue;
        const auto version = parseFfxSemver(names[i]);
        if (version.valid) parsed.push_back({i, version});
        else unversioned.push_back({i, version});
    }
    std::stable_sort(parsed.begin(), parsed.end(), ranked);
    std::vector<size_t> order;
    order.reserve(parsed.size() + unversioned.size());
    for (const auto& entry : parsed) order.push_back(entry.index);
    for (const auto& entry : unversioned) order.push_back(entry.index);
    return order;
}

} // namespace veyra::gfx
