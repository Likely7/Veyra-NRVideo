// SPDX-License-Identifier: GPL-3.0-only
// Requests to one GameStream host: serverinfo, applist, launch/resume/cancel,
// box art and unpair, plus the parsing of their answers. Ported from
// moonlight-qt's NvHTTP; the pairing handshake is in Pairing.h.
#pragma once
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "veyra/moonlight/Crypto.h"
#include "veyra/moonlight/Http.h"
#include "veyra/moonlight/Types.h"
#include "veyra/moonlight/Xml.h"

namespace veyra::moonlight {

struct LaunchRequest {
    int appId = 0;
    int width = 1920, height = 1080, fps = 60;
    std::array<uint8_t, 16> remoteInputKey{};   // AES key of the input stream
    std::array<uint8_t, 4> remoteInputKeyId{};  // first 4 bytes of the input IV, big endian
    bool hdr = false;                           // a 10-bit video format was requested
    bool sops = false;                          // let the host change its own resolution
    bool localAudio = false;                    // keep playing audio on the host too
    int surroundAudioInfo = 0;                  // SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION
    int gamepadMask = 0;
    bool persistGamepads = false;
    std::string extraQuery;                     // LiGetLaunchUrlQueryParameters(), already "&k=v..."
};

class ServerClient {
public:
    ServerClient(const Identity& identity, HostAddress address, std::string pinnedServerCertPem,
                 uint16_t httpsPort, bool useTrueUniqueId);
    // The HTTP client points at this object's own identity, so a copy rebinds it.
    ServerClient(const ServerClient& other);
    ServerClient& operator=(const ServerClient&) = delete;

    void setCancel(const std::atomic<bool>* cancel) { cancel_ = cancel; }
    const HostAddress& address() const { return address_; }
    uint16_t httpsPort() const { return httpsPort_; }
    const std::string& pinnedServerCertificate() const { return http_.pinnedServerCertificate(); }
    void setPinnedServerCertificate(std::string pem) { http_.setPinnedServerCertificate(std::move(pem)); }
    void setHttpsPort(uint16_t port) { httpsPort_ = port; }
    void setUseTrueUniqueId(bool value) { useTrueUniqueId_ = value; }

    // HTTPS first once a server certificate is pinned (it reports the real pairing
    // state); HTTP otherwise, or when the TLS side refuses us (not paired / new
    // certificate). Throws StatusError or TransportError.
    ServerInfo serverInfo(bool fastFail = false);
    std::vector<AppEntry> appList();

    // `verb` is "launch" for a fresh app or "resume" for the running one.
    // Returns the RTSP session URL the stream connects to.
    std::string launch(const char* verb, const LaunchRequest& request, bool nvidiaServerSoftware);
    void quit();
    void unpair();
    std::string boxArt(int appId);

    // One request with the standard uniqueid/uuid prefix; the raw answer body.
    std::string request(bool tls, const std::string& command, const std::string& arguments, int timeoutMs);

    static ServerInfo parseServerInfo(const xml::Node& root);
    static std::vector<AppEntry> parseAppList(const xml::Node& root);
    // Throws StatusError unless the root element reports status_code 200.
    static xml::Node checkedRoot(const std::string& body);

private:
    HttpResponse fetch(bool tls, const std::string& command, const std::string& arguments, int timeoutMs);

    Identity identity_;
    HostAddress address_;
    uint16_t httpsPort_;
    bool useTrueUniqueId_;
    HttpClient http_;
    const std::atomic<bool>* cancel_ = nullptr;
};

} // namespace veyra::moonlight
