#pragma once

#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace veyra::portable {

inline constexpr auto playerName = L"veyra_qml_ui.exe";
inline constexpr auto launcherName = L"Veyra.exe";
inline constexpr auto workerName = L"veyra_update_worker.exe";
inline constexpr auto packageMarker = L"portable-package.txt";
inline constexpr auto versionState = L"current-version.txt";
inline constexpr auto versionsFolder = L"versions";
inline constexpr auto updateFolder = L".veyra-update";
inline constexpr int applyExitCode = 43;
inline constexpr int overlayRestartExitCode = 44;

inline std::optional<std::array<int, 3>> parseVersion(std::string_view version) {
    std::array<int, 3> numbers{};
    for (size_t i = 0; i < numbers.size(); ++i) {
        const auto end = version.find('.');
        if ((i < numbers.size() - 1) != (end != std::string_view::npos)) return {};
        const auto part = version.substr(0, end);
        if (part.empty() || part.size() > 5 || (part.size() > 1 && part.front() == '0')) return {};
        const auto result = std::from_chars(part.data(), part.data() + part.size(), numbers[i]);
        if (result.ec != std::errc{} || result.ptr != part.data() + part.size()
            || numbers[i] < 0 || numbers[i] > 65535) return {};
        version.remove_prefix(part.size() + (end != std::string_view::npos));
    }
    return numbers;
}

inline std::string_view versionFromDirectory(std::string_view name) {
    const auto separator = name.find('-');
    const auto version = name.substr(0, separator);
    if (!parseVersion(version)) return {};
    if (separator == std::string_view::npos) return version;
    const auto suffix = name.substr(separator + 1);
    if (suffix.size() != 12) return {};
    for (const auto c : suffix)
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return {};
    return version;
}

inline std::string readSmallFile(const std::filesystem::path& path, size_t limit = 128) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(limit)) return {};
    std::string data(static_cast<size_t>(size), '\0');
    file.seekg(0);
    return file.read(data.data(), size) ? data : std::string{};
}

inline std::string packageFlavor(const std::filesystem::path& root) {
    const auto marker = readSmallFile(root / packageMarker);
    if (marker == "VEYRA_PORTABLE_V1\nNVIDIA\n") return "NVIDIA";
    if (marker == "VEYRA_PORTABLE_V1\nAMD\n") return "AMD";
    return {};
}

struct InstalledVersions {
    std::string current;
    std::string previous;
};

inline InstalledVersions installedVersions(const std::filesystem::path& root) {
    const auto data = readSmallFile(root / versionState);
    const auto newline = data.find('\n');
    if (newline == std::string::npos || data.back() != '\n') return {};
    InstalledVersions state{data.substr(0, newline), data.substr(newline + 1)};
    if (state.previous.empty()) return {};
    state.previous.pop_back();
    if (versionFromDirectory(state.current).empty()
        || (!state.previous.empty() && versionFromDirectory(state.previous).empty())
        || state.current == state.previous) return {};
    return state;
}

inline std::filesystem::path rootFromVersionDirectory(const std::filesystem::path& directory) {
    const auto versions = directory.parent_path();
    if (versions.filename() != versionsFolder) return {};
    const auto native = directory.filename().wstring();
    if (native.find_first_not_of(L"0123456789abcdef.-") != std::wstring::npos
        || versionFromDirectory(directory.filename().string()).empty()) return {};
    const auto root = versions.parent_path();
    return packageFlavor(root).empty() ? std::filesystem::path{} : root;
}

} // namespace veyra::portable
