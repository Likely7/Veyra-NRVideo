// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "SessionInbox.h"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace veyra::remoteplay {
// Ephemeral bearer token. Deliberately move-only, with no serialization support.
class SecretString {
public:
    SecretString()=default;
    SecretString(const SecretString&)=delete;
    SecretString& operator=(const SecretString&)=delete;
    SecretString(SecretString&&) noexcept;
    SecretString& operator=(SecretString&&) noexcept;
    ~SecretString();
    void assign(std::string_view);
    void clear() noexcept;
    std::string_view view() const noexcept;
    const char* c_str() const noexcept;
    bool empty() const noexcept { return view().empty(); }
private:
    std::vector<char> bytes_;
};
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
    // Locally formatted phase timings and numeric flags only; never server text.
    std::string diagnostics{};
};
struct NativeConnectRequest {
    bool ps5=true;
    bool psn=false;
    std::string host;
    std::string consoleId; // Stable discovery host_id / registration MAC, normalized hex.
    std::string psnDuid; // 64 hex digits for PS5; 64 zeroes select the account's main PS4.
    SecretString psnToken; // Access token only; never stored in a pairing profile.
    VideoProfile video;
    bool viewOnly=false;
    PairingCredentials credentials;
    std::optional<std::string> validate() const;
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
    // discardMedia (PS5 手柄控制): video samples are counted and accepted without
    // a copy, audio goes to a no-op sink, so Chiaki keeps its reference-frame
    // bookkeeping and never reports a dropped sample to the console as a
    // corrupt frame. The DualSense channel is forced on; feedback is unchanged.
    BackendResult start(const NativeConnectRequest&,SessionInbox::Token,bool discardMedia=false,std::stop_token stop={});
    BackendResult stop(); // Does not call PS5 goto_bed; must precede inbox.finishStop().
    BackendResult requestIdr();
    BackendResult submitController(const ControllerState&);
    BackendResult submitLoginPin(std::string_view);
    // User-triggered only. Waits on the session owner for the control sender,
    // never treats queue insertion as a sent command. stop() itself never sleeps a console.
    BackendResult requestStandby(std::stop_token stop={});
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
