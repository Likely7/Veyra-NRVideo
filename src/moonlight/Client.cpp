// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Client.h"

#include <algorithm>
#include <cstdlib>

namespace veyra::moonlight {

namespace {

constexpr int kFastFailTimeoutMs = 2000;
constexpr int kRequestTimeoutMs = 5000;
constexpr int kLaunchTimeoutMs = 120000;
constexpr int kQuitTimeoutMs = 30000;
// GFE lets a client quit another client's game when everybody uses this id.
constexpr const char* kSharedUniqueId = "0123456789ABCDEF";

int toInt(const std::string& text, int fallback = 0) {
    if (text.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : int(value);
}

uint16_t toPort(const std::string& text) {
    const long value = std::strtol(text.c_str(), nullptr, 10);
    return (value > 0 && value <= 65535) ? uint16_t(value) : uint16_t(0);
}

} // namespace

int ServerInfo::appVersionMajor() const {
    return std::atoi(appVersion.c_str());
}

ServerClient::ServerClient(const Identity& identity, HostAddress address, std::string pinnedServerCertPem,
                           uint16_t httpsPort, bool useTrueUniqueId)
    : identity_(identity), address_(std::move(address)), httpsPort_(httpsPort), useTrueUniqueId_(useTrueUniqueId),
      http_(&identity_, std::move(pinnedServerCertPem)) {}

ServerClient::ServerClient(const ServerClient& other)
    : identity_(other.identity_), address_(other.address_), httpsPort_(other.httpsPort_),
      useTrueUniqueId_(other.useTrueUniqueId_), http_(&identity_, other.http_.pinnedServerCertificate()), cancel_(other.cancel_) {}

HttpResponse ServerClient::fetch(bool tls, const std::string& command, const std::string& arguments, int timeoutMs) {
    const std::string uuid = xml::toHex(crypto::randomBytes(16));
    std::string path = "/" + command + "?uniqueid=" + (useTrueUniqueId_ ? identity_.uniqueId : std::string(kSharedUniqueId)) + "&uuid=" + uuid;
    if (!arguments.empty()) path += "&" + arguments;
    HttpTarget target;
    target.host = address_.host;
    target.port = tls ? httpsPort_ : address_.httpPort;
    target.tls = tls;
    HttpOptions options;
    options.timeoutMs = timeoutMs;
    options.cancel = cancel_;
    return http_.get(target, path, options);
}

std::string ServerClient::request(bool tls, const std::string& command, const std::string& arguments, int timeoutMs) {
    return fetch(tls, command, arguments, timeoutMs).body;
}

xml::Node ServerClient::checkedRoot(const std::string& body) {
    auto root = xml::parse(body);
    if (!root || root->name != "root") throw StatusError(-1, "Malformed XML (missing root element)");
    const auto code = root->attributes.find("status_code");
    // The status can be 0xFFFFFFFF in rare cases, so read it unsigned and cast.
    const int status = code == root->attributes.end() ? -1 : int(uint32_t(std::strtoul(code->second.c_str(), nullptr, 10)));
    if (status == 200) return std::move(*root);
    std::string message;
    if (const auto it = root->attributes.find("status_message"); it != root->attributes.end()) message = it->second;
    int reported = status;
    // GFE's unhelpful answer to a missing audio capture device.
    if (status == -1 && message == "Invalid") {
        reported = 418;
        message = "Missing audio capture device";
    }
    throw StatusError(reported, message);
}

ServerInfo ServerClient::parseServerInfo(const xml::Node& root) {
    ServerInfo info;
    info.hostname = xml::textOf(root, "hostname");
    if (info.hostname.empty()) info.hostname = "UNKNOWN";
    info.uniqueId = xml::textOf(root, "uniqueid");
    info.mac = xml::textOf(root, "mac");
    const std::string codecSupport = xml::textOf(root, "ServerCodecModeSupport");
    info.serverCodecModeSupport = codecSupport.empty() ? kServerH264 : toInt(codecSupport, kServerH264);
    info.maxLumaPixelsHevc = toInt(xml::textOf(root, "MaxLumaPixelsHEVC"));
    info.localIp = xml::textOf(root, "LocalIP");
    const uint16_t https = toPort(xml::textOf(root, "HttpsPort"));
    info.httpsPort = https ? https : kDefaultHttpsPort;
    info.externalPort = toPort(xml::textOf(root, "ExternalPort"));
    info.externalIp = xml::textOf(root, "ExternalIP");
    info.state = xml::textOf(root, "state");
    info.nvidiaServerSoftware = info.state.find("MJOLNIR") != std::string::npos;
    info.paired = xml::textOf(root, "PairStatus") == "1";
    info.appVersion = xml::textOf(root, "appversion");
    info.gfeVersion = xml::textOf(root, "GfeVersion");
    info.gpuModel = xml::textOf(root, "gputype");
    // GFE 2.8+ keeps currentgame set to the last game played, so it only counts
    // while the server reports itself busy.
    const auto endsWith = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    info.currentGame = endsWith(info.state, "_SERVER_BUSY") ? toInt(xml::textOf(root, "currentgame")) : 0;
    for (const xml::Node* mode : root.findAll("DisplayMode")) {
        DisplayMode parsed;
        parsed.width = toInt(xml::textOf(*mode, "Width"));
        parsed.height = toInt(xml::textOf(*mode, "Height"));
        parsed.refreshRate = toInt(xml::textOf(*mode, "RefreshRate"));
        info.displayModes.push_back(parsed);
    }
    std::stable_sort(info.displayModes.begin(), info.displayModes.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return uint64_t(a.width) * uint64_t(a.height) * uint64_t(a.refreshRate) < uint64_t(b.width) * uint64_t(b.height) * uint64_t(b.refreshRate);
    });
    return info;
}

std::vector<AppEntry> ServerClient::parseAppList(const xml::Node& root) {
    std::vector<AppEntry> apps;
    for (const xml::Node* app : root.findAll("App")) {
        // An app needs at least a title element (an empty one is allowed) and an id.
        if (!app->find("AppTitle") || !app->find("ID")) throw StatusError(-1, "Invalid applist XML");
        AppEntry entry;
        entry.name = xml::textOf(*app, "AppTitle");
        entry.id = toInt(xml::textOf(*app, "ID"));
        entry.hdrSupported = xml::textOf(*app, "IsHdrSupported") == "1";
        entry.isAppCollectorGame = xml::textOf(*app, "IsAppCollectorGame") == "1";
        apps.push_back(std::move(entry));
    }
    return apps;
}

ServerInfo ServerClient::serverInfo(bool fastFail) {
    const int timeout = fastFail ? kFastFailTimeoutMs : kRequestTimeoutMs;
    for (;;) {
        const bool httpsPossible = !http_.pinnedServerCertificate().empty() && httpsPort_ != 0;
        if (httpsPossible) {
            // Always try HTTPS first, since it properly reports the pairing status.
            try {
                ServerInfo info = parseServerInfo(checkedRoot(request(true, "serverinfo", "", timeout)));
                info.pairStatusKnown = true;
                return info;
            } catch (const StatusError& e) {
                if (e.status != 401) throw;
            } catch (const TransportError& e) {
                // Certificate mismatch or a client certificate the host does not know: fall back to HTTP.
                if (e.kind != TransportError::Kind::Tls) throw;
            }
            // The host turned our certificate away: that is a real "not paired".
            ServerInfo info = parseServerInfo(checkedRoot(request(false, "serverinfo", "", timeout)));
            info.paired = false;
            info.pairStatusKnown = true;
            return info;
        }
        // Before pairing only HTTP is used. It also tells us the HTTPS port.
        const ServerInfo info = parseServerInfo(checkedRoot(request(false, "serverinfo", "", timeout)));
        httpsPort_ = info.httpsPort;
        if (!http_.pinnedServerCertificate().empty()) continue;   // now the HTTPS port is known
        return info;
    }
}

std::vector<AppEntry> ServerClient::appList() {
    return parseAppList(checkedRoot(request(true, "applist", "", kRequestTimeoutMs)));
}

std::string ServerClient::launch(const char* verb, const LaunchRequest& r, bool nvidiaServerSoftware) {
    // A frame rate above 60 makes GFE's SOPS fall back to 720p60; asking for 0 keeps
    // the requested resolution. Sunshine does not need the workaround.
    const int fps = (r.fps > 60 && nvidiaServerSoftware) ? 0 : r.fps;
    const int32_t keyId = int32_t((uint32_t(r.remoteInputKeyId[0]) << 24) | (uint32_t(r.remoteInputKeyId[1]) << 16) |
                                  (uint32_t(r.remoteInputKeyId[2]) << 8) | uint32_t(r.remoteInputKeyId[3]));
    std::string key(reinterpret_cast<const char*>(r.remoteInputKey.data()), r.remoteInputKey.size());
    std::string arguments = "appid=" + std::to_string(r.appId) +
        "&mode=" + std::to_string(r.width) + "x" + std::to_string(r.height) + "x" + std::to_string(fps) +
        "&additionalStates=1&sops=" + (r.sops ? "1" : "0") +
        "&rikey=" + xml::toHex(key) + "&rikeyid=" + std::to_string(keyId) +
        (r.hdr ? "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0" : "") +
        "&localAudioPlayMode=" + (r.localAudio ? "1" : "0") +
        "&surroundAudioInfo=" + std::to_string(r.surroundAudioInfo) +
        "&remoteControllersBitmap=" + std::to_string(r.gamepadMask) +
        "&gcmap=" + std::to_string(r.gamepadMask) +
        "&gcpersist=" + (r.persistGamepads ? "1" : "0") + r.extraQuery;
    const xml::Node root = checkedRoot(request(true, verb, arguments, kLaunchTimeoutMs));
    return xml::textOf(root, "sessionUrl0");
}

void ServerClient::quit() {
    checkedRoot(request(true, "cancel", "", kQuitTimeoutMs));
    // Newer GFE answers success even when quitting another client's game fails.
    if (serverInfo().currentGame != 0) throw StatusError(599, "");
}

void ServerClient::unpair() {
    request(false, "unpair", "", kRequestTimeoutMs);
}

std::string ServerClient::boxArt(int appId) {
    const HttpResponse response = fetch(true, "appasset", "appid=" + std::to_string(appId) + "&AssetType=2&AssetIdx=0", kRequestTimeoutMs);
    if (response.status != 200) throw StatusError(response.status, "box art not available");
    return response.body;
}

} // namespace veyra::moonlight
