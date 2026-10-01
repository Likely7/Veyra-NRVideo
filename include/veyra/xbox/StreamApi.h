// SPDX-License-Identifier: GPL-3.0-only
// The Xbox home-streaming (xHome) session API on the region host the streaming sign-in returned.
// Ported from Greenlight (unknownskl/greenlight, MIT): xcloudapi.ts and streammanager.ts. The signalling
// goes through Microsoft's servers even when the console is on the same network; the media then flows
// over WebRTC (see XboxSessionSource).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "veyra/xbox/Https.h"

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
    std::string play(const std::string& serverId, const std::string& locale = "zh-CN");
    SessionState state(const std::string& sessionId);
    void connect(const std::string& sessionId, const std::string& transferToken);
    // Sends the local offer, returns the console's answer SDP.
    std::string exchangeSdp(const std::string& sessionId, const std::string& offerSdp);
    // Sends local candidates, returns the console's (plus the IPv4 ones hidden in Teredo addresses).
    std::vector<IceCandidate> exchangeIce(const std::string& sessionId, const std::vector<IceCandidate>& local);
    void keepalive(const std::string& sessionId);
    void stop(const std::string& sessionId);

    // Exposed for tests.
    static std::vector<Console> parseConsoles(const std::string& body);
    static std::vector<IceCandidate> parseIce(const std::string& exchangeResponse);
    // A Teredo address (2001:0:...) carries the client's public IPv4 and port, obfuscated.
    static bool teredoIpv4(const std::string& ipv6, std::string* ipv4, int* port);

    void setRetryDelayMs(int ms) { retryDelayMs_ = ms; }

private:
    HttpsResponse call(const std::string& method, const std::string& path, const std::string& body,
                       const std::map<std::string, std::string>& extraHeaders = {});
    std::string getExchange(const std::string& path);

    std::shared_ptr<HttpsTransport> http_;
    std::string host_, token_;
    int retryDelayMs_ = 750;
};

} // namespace veyra::xbox
