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
// The player asks the launcher to install a verified package and open the new version.
inline constexpr int applyRestartExitCode = 45;

// A new process that never really started should return to the previous version.
// Restart and update exit codes are intentional and must not roll back.
inline bool startupShouldRollback(bool launchFailed, unsigned long exitCode, unsigned long long elapsedMs) {
    if (launchFailed) return true;
    if (exitCode == 0 || exitCode == 42 || exitCode == static_cast<unsigned long>(applyExitCode)
        || exitCode == static_cast<unsigned long>(overlayRestartExitCode)
        || exitCode == static_cast<unsigned long>(applyRestartExitCode)) return false;
    return elapsedMs < 15000;
}

// Zip members are relative, bounded path segments. Absolute paths, drive
// prefixes, alternate streams and parent segments are rejected before extract.
inline bool safeArchiveEntry(std::string_view name) {
    if (name.empty() || name.size() > 240 || name.find('\0') != std::string_view::npos) return false;
    if (name.back() == '/' || name.back() == '\\') name.remove_suffix(1);
    if (name.empty() || name.front() == '/' || name.front() == '\\') return false;
    if (name.find(':') != std::string_view::npos) return false;
    std::size_t begin = 0;
    while (begin < name.size()) {
        const auto end = name.find_first_of("/\\", begin);
        const auto part = name.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        if (part.empty() || part == "." || part == ".." || part.size() > 120) return false;
        if (part.back() == ' ' || part.back() == '.') return false;
        for (const char raw : part) {
            const auto c = static_cast<unsigned char>(raw);
            if (c < 32 || c == 127) return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

// The elevated worker may only read a directory we created under LocalAppData.
inline bool stagingDirectoryId(const std::filesystem::path& base, const std::filesystem::path& staging) {
    const auto relative = staging.lexically_normal().lexically_relative(base.lexically_normal());
    if (relative.empty() || relative.is_absolute() || relative.has_parent_path()) return false;
    const auto name = relative.wstring();
    if (name.size() != 16) return false;
    for (const wchar_t c : name)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    return true;
}

// WinVerifyTrust: a missing Authenticode signature is allowed because current
// public builds are unsigned. A present but invalid signature is not.
inline bool acceptAuthenticode(long status, unsigned long lastError) {
    constexpr long success = 0;
    constexpr long noSignature = static_cast<long>(0x800B0100L);
    constexpr unsigned long providerUnknown = 0x800B0001L;
    constexpr unsigned long formUnknown = 0x800B0003L;
    constexpr unsigned long noSignatureError = 0x800B0100L;
    if (status == success) return true;
    if (status == noSignature)
        return lastError == noSignatureError || lastError == formUnknown || lastError == providerUnknown;
    return false;
}

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

// Stable tags are major.minor.patch. A local build may carry a single ".0"
// resource suffix or a hyphenated label; a remote tag with either is rejected.
inline std::optional<std::array<int, 3>> releaseNumbers(std::string_view text, bool local) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    if (local) {
        const auto dash = text.find('-');
        if (dash != std::string_view::npos) text = text.substr(0, dash);
        if (text.size() > 2 && text.ends_with(".0")) {
            const auto trimmed = text.substr(0, text.size() - 2);
            if (parseVersion(trimmed)) text = trimmed;
        }
    } else if (text.find('-') != std::string_view::npos) {
        return {};
    }
    return parseVersion(text);
}

// Positive when the remote release is newer. Empty when either side is malformed.
inline std::optional<int> compareRelease(std::string_view local, std::string_view remote) {
    const auto left = releaseNumbers(local, true);
    const auto right = releaseNumbers(remote, false);
    if (!left || !right) return {};
    if (*right > *left) return 1;
    if (*right < *left) return -1;
    return 0;
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
