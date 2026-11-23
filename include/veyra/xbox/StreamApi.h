// SPDX-License-Identifier: GPL-3.0-only
// The Xbox home-streaming (xHome) session API on the region host the streaming sign-in returned.
// Ported from Greenlight (unknownskl/greenlight, MIT): xcloudapi.ts and streammanager.ts. The signalling
// goes through Microsoft's servers even when the console is on the same network; the media then flows
// over WebRTC (terminated by the Chromium player, see XboxWebSource).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "veyra/xbox/Https.h"
#include "veyra/xbox/StreamProfile.h"

namespace veyra::xbox {

struct Console {
    std::string serverId;       // "F4001234ABCD5678"
    std::string name;
    std::string powerState;     // "On", "ConnectedStandby", "Off" ...
    std::string consoleType;    // "XboxSeriesX", "XboxOne" ...
    bool outOfHomeWarning = false;
};

struct SessionState {
    std::string state;          // Provisioning, ReadyToConnect, Provisioned, WaitingForResources, Failed
    std::string errorCode, errorMessage;
};

struct IceCandidate {
    std::string candidate;      // "a=candidate:..." as the service sends it
    std::string sdpMid;
    int sdpMLineIndex = 0;
};

class StreamApi {
public:
    StreamApi(std::shared_ptr<HttpsTransport> transport, std::string host, std::string gsToken);

    std::vector<Console> consoles();
    // Starts a session on a console; returns the session id.
    std::string play(const std::string& serverId, const std::string& locale = "zh-CN",
                     const StreamProfile& profile = {});
    SessionState state(const std::string& sessionId);
    void connect(const std::string& sessionId, const std::string& transferToken);
    // Sends the local offer, returns the console's answer SDP.
    std::string exchangeSdp(const std::string& sessionId, const std::string& offerSdp);
    // Sends local candidates, returns the console's (plus the IPv4 ones hidden in Teredo addresses).
    // The session profile's fullTeredo adds the server4/3074 combinations the reference clients use.
    std::vector<IceCandidate> exchangeIce(const std::string& sessionId, const std::vector<IceCandidate>& local);
    // The session's keep-alive pulse from /configuration; 0 when the service does not send one.
    int keepAlivePulseInSeconds(const std::string& sessionId);
    void keepalive(const std::string& sessionId);
    void stop(const std::string& sessionId);

    // Exposed for tests.
    static std::vector<Console> parseConsoles(const std::string& body);
    static std::vector<IceCandidate> parseIce(const std::string& exchangeResponse, bool fullTeredo = false);
    // A Teredo address (2001:0:...) carries the client's public IPv4 and port, obfuscated.
    // serverIpv4, when given, receives the Teredo server's IPv4 (bytes 4..7, not obfuscated).
    static bool teredoIpv4(const std::string& ipv6, std::string* ipv4, int* port, std::string* serverIpv4 = nullptr);

    void setRetryDelayMs(int ms) { retryDelayMs_ = ms; }
    // The quality/ICE profile of this session (play tier, Teredo expansion).
    void setProfile(const StreamProfile& profile) { profile_ = profile; }
    // Called once per request when the service answers 401/403; returns a fresh gsToken.
    void setTokenRefresh(std::function<std::string()> refresh) { tokenRefresh_ = std::move(refresh); }

private:
    HttpsResponse call(const std::string& method, const std::string& path, const std::string& body,
                       const std::map<std::string, std::string>& extraHeaders = {});
    std::string getExchange(const std::string& path);

    std::shared_ptr<HttpsTransport> http_;
    std::string host_, token_;
    StreamProfile profile_;
    std::function<std::string()> tokenRefresh_;
    int retryDelayMs_ = 750;
};

} // namespace veyra::xbox
