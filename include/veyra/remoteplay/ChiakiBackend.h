// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "SessionInbox.h"
#include <array>
#include <chrono>
#include <memory>
#include <stop_token>
#include <string>

namespace veyra::remoteplay {
// No serialization/ostream support: never put registration/session keys into
// command lines, logs, fixtures or source control. DPAPI storage is NOT in patch 01.
struct PairingCredentials {
    PairingCredentials() = default;
    PairingCredentials(const PairingCredentials&) = delete;
    PairingCredentials& operator=(const PairingCredentials&) = delete;
    PairingCredentials(PairingCredentials&&) noexcept;
    PairingCredentials& operator=(PairingCredentials&&) noexcept;
    AccountId accountId{};
    std::array<char,16> registrationKey{};
    std::array<std::uint8_t,16> sessionKey{};
    ~PairingCredentials();
};
struct BackendResult {
    bool ok=false;
    int code=0;
    // One of our fixed operation names, never an unfiltered server string.
    std::string operation;
};
struct NativeConnectRequest {
    std::string host;
    std::string consoleId; // Stable discovery host_id / registration MAC, normalized hex.
    VideoProfile video;
    bool viewOnly=false;
    PairingCredentials credentials;
};
struct NativeSnapshot {
    bool started=false,connected=false;
    std::uint64_t warnings=0,errors=0,videoCallbacks=0,audioCallbacks=0;
    int lastQuitReason=0,lastApiError=0;
    uint64_t callbackRejected=0,packetReceived=0,packetLost=0;
    uint64_t transportErrors=0,assemblyErrors=0;
    bool automaticRetryAllowed=true;
    bool startupRetryAllowed=false; // Only transient RP_IN_USE, never an auth rejection.
    int64_t serverTargetBitrate=-1; // Raw upstream field; diagnostic opt-in only.
    uint64_t qualityReports=0;
};
BackendResult initializeChiaki();
class ChiakiBackend {
public:
    ChiakiBackend();
    ~ChiakiBackend();
    ChiakiBackend(const ChiakiBackend&)=delete;
    ChiakiBackend& operator=(const ChiakiBackend&)=delete;
    // All methods, including snapshot(), must be called from one session-owner thread,
    // NEVER from a Chiaki callback. This object is not a renderer or a decoder.
    // discardMedia: keep controller/keepalive/feedback, drop video and audio in
    // the receive callbacks (combined PS5-control mode). The console still
    // transmits; there is no protocol-level media switch.
    BackendResult start(const NativeConnectRequest&,SessionInbox::Token,bool discardMedia=false);
    BackendResult stop(); // Does not call PS5 goto_bed; must precede inbox.finishStop().
    BackendResult requestIdr();
    BackendResult submitController(const ControllerState&);
    BackendResult submitLoginPin(std::string_view);
    NativeSnapshot snapshot() const;
    ControllerFeedback takeFeedback();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
struct PairResult {
    BackendResult result;
    PairingCredentials credentials;
    std::array<std::uint8_t,6> mac{};
    std::string nickname;
    bool canceled=false,timedOut=false;
};
// Synchronous OWNERSHIP operation; run on a dedicated session/setup worker, not HWND.
// Cancellation stops the upstream registration and joins before returning.
PairResult pairLocalPs5(std::string host,const AccountId&,std::string_view eightDigitPin,
    std::stop_token stop={},std::chrono::milliseconds timeout=std::chrono::seconds(30));
} // namespace veyra::remoteplay
