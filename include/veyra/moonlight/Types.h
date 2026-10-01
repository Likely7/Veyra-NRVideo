// SPDX-License-Identifier: GPL-3.0-only
// Moonlight / GameStream host protocol: shared types.
//
// The pairing, serverinfo and launch logic under src/moonlight is a port of
// moonlight-qt's app/backend (GPL-3.0, https://github.com/moonlight-stream/moonlight-qt,
// commit pinned in scripts/moonlight/dependency-lock.json) with the Qt types
// replaced. See THIRD_PARTY_NOTICES.md.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace veyra::moonlight {

constexpr uint16_t kDefaultHttpPort = 47989;
constexpr uint16_t kDefaultHttpsPort = 47984;

// Server codec support bits of the serverinfo ServerCodecModeSupport field.
// Values are the SCM_* constants of moonlight-common-c's Limelight.h; they are
// repeated here so the protocol library and its tests do not need that header.
constexpr int kServerH264 = 0x00000001;
constexpr int kServerHevc = 0x00000100;
constexpr int kServerHevcMain10 = 0x00000200;
constexpr int kServerAv1Main8 = 0x00010000;
constexpr int kServerAv1Main10 = 0x00020000;

struct HostAddress {
    std::string host;                 // IPv4/IPv6 literal or DNS name, no brackets
    uint16_t httpPort = kDefaultHttpPort;
    bool empty() const { return host.empty(); }
    bool operator==(const HostAddress&) const = default;
};

struct DisplayMode {
    int width = 0, height = 0, refreshRate = 0;
};

struct ServerInfo {
    std::string hostname, uniqueId, mac, localIp, externalIp;
    std::string appVersion, gfeVersion, gpuModel, state;
    uint16_t httpsPort = kDefaultHttpsPort;
    uint16_t externalPort = 0;
    int serverCodecModeSupport = kServerH264;
    int maxLumaPixelsHevc = 0;
    int currentGame = 0;              // 0 unless the host is busy streaming
    bool paired = false;
    // Only an answer over HTTPS with our client certificate says whether the host
    // still trusts us: Sunshine reports PairStatus 0 over plain HTTP to anyone.
    // False means `paired` must not be used to forget a pairing.
    bool pairStatusKnown = false;
    bool nvidiaServerSoftware = false; // GFE / RTX Experience report "MJOLNIR"
    std::vector<DisplayMode> displayModes;

    // First component of appversion (the "server generation"): 7+ pairs with SHA-256.
    int appVersionMajor() const;
};

struct AppEntry {
    std::string name;
    int id = 0;
    bool hdrSupported = false;
    bool isAppCollectorGame = false;
};

// A well-formed response whose status_code is not 200.
struct StatusError : std::runtime_error {
    int status;
    StatusError(int code, const std::string& message) : std::runtime_error(message), status(code) {}
};

// The request itself failed (connect, timeout, TLS, malformed HTTP).
struct TransportError : std::runtime_error {
    enum class Kind { Connect, Timeout, Tls, Protocol, Cancelled };
    Kind kind;
    TransportError(Kind k, const std::string& message) : std::runtime_error(message), kind(k) {}
};

} // namespace veyra::moonlight
