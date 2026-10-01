// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/xbox/StreamApi.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace veyra::xbox {
namespace {

using json = nlohmann::json;

// What the session API expects to hear about the client (Greenlight sends the same).
const char* kDeviceInfo = R"({"appInfo":{"env":{"clientAppId":"Microsoft.GamingApp","clientAppType":"native","clientAppVersion":"2203.1001.4.0","clientSdkVersion":"8.5.2","httpEnvironment":"prod","sdkInstallId":""}},"dev":{"hw":{"make":"Microsoft","model":"Surface Pro","sdktype":"native"},"os":{"name":"Windows 11","ver":"22631.2715","platform":"desktop"},"displayInfo":{"dimensions":{"widthInPixels":1920,"heightInPixels":1080},"pixelDensity":{"dpiX":1,"dpiY":1}}}})";

json parse(const HttpsResponse& response, const std::string& what) {
    try {
        return json::parse(response.body);
    } catch (const json::exception&) {
        throw std::runtime_error(what + " returned something that is not JSON");
    }
}

} // namespace

StreamApi::StreamApi(std::shared_ptr<HttpsTransport> transport, std::string host, std::string gsToken)
    : http_(std::move(transport)), host_(std::move(host)), token_(std::move(gsToken)) {}

HttpsResponse StreamApi::call(const std::string& method, const std::string& path, const std::string& body,
                              const std::map<std::string, std::string>& extraHeaders) {
    HttpsRequest r;
    r.method = method;
    r.host = host_;
    r.path = path;
    r.headers["Content-Type"] = "application/json";
    r.headers["Authorization"] = "Bearer " + token_;
    for (const auto& [k, v] : extraHeaders) r.headers[k] = v;
    if (method == "POST") r.body = body.empty() ? "{}" : body;
    const HttpsResponse response = http_->send(r);
    if (response.status < 200 || response.status > 299)
        throw ServiceError(response.status, response.body, method + " " + path + " returned HTTP " + std::to_string(response.status));
    return response;
}

std::vector<Console> StreamApi::parseConsoles(const std::string& body) {
    std::vector<Console> out;
    const json j = json::parse(body);
    for (const auto& c : j.value("results", json::array())) {
        Console console;
        console.serverId = c.value("serverId", "");
        console.name = c.value("deviceName", "");
        console.powerState = c.value("powerState", "");
        console.consoleType = c.value("consoleType", "");
        console.outOfHomeWarning = c.value("outOfHomeWarning", false);
        if (!console.serverId.empty()) out.push_back(std::move(console));
    }
    return out;
}

std::vector<Console> StreamApi::consoles() {
    const HttpsResponse response = call("GET", "/v6/servers/home", "");
    try {
        return parseConsoles(response.body);
    } catch (const json::exception&) {
        throw std::runtime_error("console list is not JSON");
    }
}

std::string StreamApi::play(const std::string& serverId, const std::string& locale) {
    const json body = {
        {"titleId", ""}, {"systemUpdateGroup", ""}, {"clientSessionId", ""},
        {"settings", {{"nanoVersion", "V3;WebrtcTransport.dll"}, {"enableTextToSpeech", false}, {"highContrast", 0},
                      {"locale", locale}, {"useIceConnection", false}, {"timezoneOffsetMinutes", 480},
                      {"sdkType", "web"}, {"osName", "windows"}}},
        {"serverId", serverId}, {"fallbackRegionNames", json::array()},
    };
    const json j = parse(call("POST", "/v5/sessions/home/play", body.dump(), {{"X-MS-Device-Info", kDeviceInfo}}), "play");
    const std::string path = j.value("sessionPath", "");
    // "v5/sessions/home/<id>"
    const auto slash = path.find_last_of('/');
    const std::string id = slash == std::string::npos ? path : path.substr(slash + 1);
    if (id.empty()) throw std::runtime_error("play returned no session");
    return id;
}

SessionState StreamApi::state(const std::string& sessionId) {
    const json j = parse(call("GET", "/v5/sessions/home/" + sessionId + "/state", ""), "session state");
    SessionState s;
    s.state = j.value("state", "");
    if (j.contains("errorDetails") && j["errorDetails"].is_object()) {
        const json& e = j["errorDetails"];
        s.errorCode = e.contains("code") && e["code"].is_string() ? e["code"].get<std::string>() : std::string();
        s.errorMessage = e.contains("message") && e["message"].is_string() ? e["message"].get<std::string>() : std::string();
    }
    return s;
}

void StreamApi::connect(const std::string& sessionId, const std::string& transferToken) {
    call("POST", "/v5/sessions/home/" + sessionId + "/connect", json{{"userToken", transferToken}}.dump());
}

// The service answers 204 until the console has replied; poll a bounded number of times.
std::string StreamApi::getExchange(const std::string& path) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        const HttpsResponse response = call("GET", path, "");
        if (response.status != 204 && !response.body.empty()) {
            const json j = parse(response, path);
            if (j.contains("errorDetails") && j["errorDetails"].is_object() && j["errorDetails"].value("code", "") != "")
                throw std::runtime_error(path + ": " + j["errorDetails"].value("message", j["errorDetails"].value("code", "")));
            return j.value("exchangeResponse", "");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs_));
    }
    throw std::runtime_error(path + ": the console did not answer");
}

std::string StreamApi::exchangeSdp(const std::string& sessionId, const std::string& offerSdp) {
    const json body = {
        {"messageType", "offer"}, {"sdp", offerSdp},
        {"configuration", {
            {"chatConfiguration", {{"bytesPerSample", 2}, {"expectedClipDurationMs", 20},
                                   {"format", {{"codec", "opus"}, {"container", "webm"}}},
                                   {"numChannels", 1}, {"sampleFrequencyHz", 24000}}},
            {"chat", {{"minVersion", 1}, {"maxVersion", 1}}},
            {"control", {{"minVersion", 1}, {"maxVersion", 3}}},
            {"input", {{"minVersion", 1}, {"maxVersion", 8}}},
            {"message", {{"minVersion", 1}, {"maxVersion", 1}}},
        }},
    };
    const std::string path = "/v5/sessions/home/" + sessionId + "/sdp";
    call("POST", path, body.dump());
    const std::string exchange = getExchange(path);
    try {
        const json answer = json::parse(exchange);
        const std::string sdp = answer.value("sdp", "");
        if (sdp.empty()) throw std::runtime_error("the console's answer has no SDP (status: " + answer.value("status", std::string("?")) + ")");
        return sdp;
    } catch (const json::exception&) {
        throw std::runtime_error("the console's answer is not JSON");
    }
}

bool StreamApi::teredoIpv4(const std::string& ipv6, std::string* ipv4, int* port) {
    in6_addr address{};
    if (InetPtonA(AF_INET6, ipv6.c_str(), &address) != 1) return false;
    const uint8_t* b = address.s6_addr;
    if (!(b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00)) return false;
    const int p = ((b[10] << 8) | b[11]) ^ 0xFFFF;
    char text[INET_ADDRSTRLEN] = {};
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", b[12] ^ 0xFFu, b[13] ^ 0xFFu, b[14] ^ 0xFFu, b[15] ^ 0xFFu);
    if (ipv4) *ipv4 = text;
    if (port) *port = p;
    return true;
}

std::vector<IceCandidate> StreamApi::parseIce(const std::string& exchangeResponse) {
    std::vector<IceCandidate> out;
    const json list = json::parse(exchangeResponse);
    for (const auto& c : list) {
        IceCandidate candidate;
        candidate.candidate = c.value("candidate", "");
        candidate.sdpMid = c.contains("sdpMid") ? (c["sdpMid"].is_string() ? c["sdpMid"].get<std::string>() : std::to_string(c["sdpMid"].get<int>())) : "0";
        candidate.sdpMLineIndex = c.contains("sdpMLineIndex") ? (c["sdpMLineIndex"].is_string() ? std::atoi(c["sdpMLineIndex"].get<std::string>().c_str()) : c["sdpMLineIndex"].get<int>()) : 0;
        if (candidate.candidate.empty() || candidate.candidate == "a=end-of-candidates") continue;
        // Greenlight: a Teredo candidate also reaches the console on the IPv4 address and port it hides,
        // and on port 9002 of that address.
        std::istringstream fields(candidate.candidate);
        std::vector<std::string> parts;
        for (std::string part; fields >> part;) parts.push_back(part);
        std::string ipv4;
        int port = 0;
        if (parts.size() > 4 && parts[4].rfind("2001", 0) == 0 && teredoIpv4(parts[4], &ipv4, &port)) {
            out.push_back({"a=candidate:10 1 UDP 1 " + ipv4 + " 9002 typ host ", candidate.sdpMid, candidate.sdpMLineIndex});
            out.push_back({"a=candidate:11 1 UDP 1 " + ipv4 + " " + std::to_string(port) + " typ host ", candidate.sdpMid, candidate.sdpMLineIndex});
        }
        out.push_back(std::move(candidate));
    }
    return out;
}

std::vector<IceCandidate> StreamApi::exchangeIce(const std::string& sessionId, const std::vector<IceCandidate>& local) {
    json candidates = json::array();
    for (const auto& c : local) candidates.push_back({{"candidate", c.candidate}, {"sdpMid", c.sdpMid}, {"sdpMLineIndex", c.sdpMLineIndex}});
    const std::string path = "/v5/sessions/home/" + sessionId + "/ice";
    call("POST", path, json{{"messageType", "iceCandidate"}, {"candidate", candidates}}.dump());
    try {
        return parseIce(getExchange(path));
    } catch (const json::exception&) {
        throw std::runtime_error("the console's ICE candidates are not JSON");
    }
}

void StreamApi::keepalive(const std::string& sessionId) {
    call("POST", "/v5/sessions/home/" + sessionId + "/keepalive", "{}");
}

void StreamApi::stop(const std::string& sessionId) {
    call("DELETE", "/v5/sessions/home/" + sessionId, "");
}

} // namespace veyra::xbox
