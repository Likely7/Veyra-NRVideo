#pragma once

#include "veyra/base/PortableLayout.h"

#include <cstdint>
#include <string_view>

namespace veyra::portable {

// Same bound as the signed manifest. Kept here so the elevated copy can stop
// without pulling in the Qt manifest type.
inline constexpr std::uint64_t maxSealedArchiveBytes = 3ull << 30;

enum class DownloadRouteId { GitHub, GhProxy, GhFast };

// Pieces of a URL, already split by the caller. Empty user/query/fragment and
// port -1 mean "absent". Host and scheme are compared case-sensitively; Qt
// lowercases both before calling.
struct ApiRedirectView {
    std::string_view scheme;
    std::string_view host;
    std::string_view path;
    std::string_view userInfo;
    std::string_view query;
    std::string_view fragment;
    int port = -1;
};

inline constexpr std::string_view officialLatestApiPath = "/repos/Likely7/Veyra-NRVideo/releases/latest";
inline constexpr std::string_view mirrorPrefixedLatestApiPath =
    "/https://api.github.com/repos/Likely7/Veyra-NRVideo/releases/latest";

// GitHub route: only the official API document. A mirror route also allows
// that mirror, and only when its path is the official URL prefixed by the
// mirror. Queries, fragments, userinfo and an explicit port are rejected.
// gh-proxy.com and ghfast.top are not accepted unless the user selected that route.
inline bool allowedApiRedirectView(const ApiRedirectView& url, DownloadRouteId route) {
    if (!url.userInfo.empty() || !url.query.empty() || !url.fragment.empty()) return false;
    if (url.scheme != "https" || url.port != -1) return false;
    const bool github = url.host == "api.github.com" && url.path == officialLatestApiPath;
    if (route == DownloadRouteId::GitHub) return github;
    if (github) return true;
    const std::string_view mirror = route == DownloadRouteId::GhProxy ? "gh-proxy.com" : "ghfast.top";
    return url.host == mirror && url.path == mirrorPrefixedLatestApiPath;
}

// GitHub release asset names. A name that fails this is ignored, not installed.
// The signed manifest still has to name the zip exactly.
inline bool acceptableGitHubAssetName(std::string_view name) {
    if (name.empty() || name.size() > 180) return false;
    const auto head = static_cast<unsigned char>(name.front());
    const bool headOk = (head >= 'A' && head <= 'Z') || (head >= 'a' && head <= 'z') || (head >= '0' && head <= '9');
    if (!headOk) return false;
    for (const unsigned char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Valid: WinVerifyTrust accepted a signature.
// Absent: no Authenticode signature. That is not authentication. Current public
// builds are unsigned, so absence is tolerated only for bytes that already
// matched the signed manifest inside a directory the user cannot write.
// Rejected: the signature is present but invalid, or the status is unknown.
enum class AuthenticodeClass { Valid, Absent, Rejected };

inline AuthenticodeClass classifyAuthenticode(long status, unsigned long lastError) {
    if (status == 0) return AuthenticodeClass::Valid;
    if (acceptAuthenticode(status, lastError)) return AuthenticodeClass::Absent;
    return AuthenticodeClass::Rejected;
}

// Elevated code may run only from a privileged extract of a zip whose hash
// matched the signed manifest. Unsigned is not enough on its own, and a
// user-writable directory is never enough.
inline bool elevatedFileMayRun(AuthenticodeClass trust, bool privilegedExtract, bool zipHashMatched) {
    if (!privilegedExtract || !zipHashMatched) return false;
    return trust == AuthenticodeClass::Valid || trust == AuthenticodeClass::Absent;
}

struct PlayerExitState {
    int code = 0;
    bool scheduled = false;
    bool exportRunning = false;
    bool needsElevation = false;
    bool installing = false;
};

// Code returned to the launcher. A staged package is installed only when the
// player asks for it. An elevated install never asks the launcher to start
// the root worker, including when the user quits while that worker is running.
inline int launcherExitCode(const PlayerExitState& state) {
    if (state.needsElevation || state.installing) {
        if (state.code == applyExitCode || state.code == applyRestartExitCode) return 0;
        return state.code;
    }
    if ((state.code == applyExitCode || state.code == applyRestartExitCode)
        && state.scheduled && !state.exportRunning) return state.code;
    if (state.code == applyExitCode || state.code == applyRestartExitCode) return 0;
    return state.code;
}

inline bool launcherStartsWorker(int code) {
    return code == applyExitCode || code == applyRestartExitCode;
}

// In-process gate. The worker also holds a session mutex so a second process
// cannot install the same root while the first one is inside apply.
inline bool elevatedInstallMayStart(bool scheduled, bool installing, bool authorizing, bool needsElevation) {
    return scheduled && needsElevation && !installing && !authorizing;
}

} // namespace veyra::portable
