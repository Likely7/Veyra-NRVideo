// SPDX-License-Identifier: GPL-3.0-only
// Quality and ICE profile of an xHome session. The osName tiers follow the reference clients
// (Greenlight lineage, MIT; XStreaming desktop/player, MIT): android=720p, windows=1080p,
// tizen=1080p HQ. The service decides the actual encode from the client identity it is told
// about, so the tier must stay consistent across play settings, X-MS-Device-Info and the
// message-channel dimensionschanged report. Pure data and helpers only; no service calls.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace veyra::xbox {

enum class QualityTier : uint8_t { P720 = 0, P1080 = 1, P1080Hq = 2 };

struct IceServerConfig {
    std::string url;        // "stun:host[:port]" or "turn:host[:port]"
    std::string username;   // TURN only
    std::string credential; // TURN only
};

// Hardware decode API for the H.264 stream. Auto = D3D12VA (the historical path);
// D3D11VA = the DXVA path the HEVC stream already uses, clean on Intel iGPUs
// whose D3D12VA H.264 output is silently damaged.
enum class DecodeBackend : uint8_t { Auto = 0, D3D11VA = 1 };

struct StreamProfile {
    QualityTier tier = QualityTier::P1080; // default = the historical behaviour, byte for byte
    int videoBitrateKbps = 0;              // 0 = no b=AS line; the console decides
    int audioBitrateKbps = 0;
    bool useStun = false;                  // public STUN list below; off keeps host-only gathering
    bool fullTeredo = false;               // false = the historical two IPv4 candidates
    DecodeBackend decodeBackend = DecodeBackend::Auto;
    std::vector<IceServerConfig> iceServers; // user STUN/TURN, appended after the defaults
};

inline const char* osNameForTier(QualityTier tier) {
    return tier == QualityTier::P1080Hq ? "tizen" : tier == QualityTier::P720 ? "android" : "windows";
}

// The viewport the client reports (dimensionschanged). HQ keeps a 1080p viewport; the higher
// bitrate comes from the tizen identity, not from a larger reported window.
inline void viewportForTier(QualityTier tier, int* width, int* height) {
    *width = tier == QualityTier::P720 ? 1280 : 1920;
    *height = tier == QualityTier::P720 ? 720 : 1080;
}

// X-MS-Device-Info for the play request. P1080 must stay byte-identical to the historical
// constant (the regression test asserts it). The other tiers follow the reference clients:
// os.name from the tier and a 4096x2160 display for 720p/HQ.
std::string deviceInfoForTier(QualityTier tier); // StreamApi.cpp

// Parses "stun:host[:port]" / "turn:host[:port]" (default port 3478). Bracketed IPv6
// literals are accepted. Returns false for an unknown scheme or an empty host.
bool parseIceServerUrl(const std::string& url, std::string* host, uint16_t* port, bool* turn);

// The public STUN list the reference clients use (facts, not code). Order preserved.
inline const std::vector<IceServerConfig>& defaultStunServers() {
    static const std::vector<IceServerConfig> servers = {
        {"stun:worldaz.relay.teams.microsoft.com:3478", {}, {}},
        {"stun:stun.l.google.com:19302", {}, {}},
        {"stun:stun1.l.google.com:19302", {}, {}},
        {"stun:relay1.expressturn.com:3478", {}, {}},
        {"stun:relay2.expressturn.com:3478", {}, {}},
        {"stun:stun.kinesisvideo.us-east-1.amazonaws.com:443", {}, {}},
        {"stun:stun.douyucdn.cn:18000", {}, {}},
    };
    return servers;
}

} // namespace veyra::xbox