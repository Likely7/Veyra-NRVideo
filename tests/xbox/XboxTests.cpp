// SPDX-License-Identifier: GPL-3.0-only
// Offline tests for the Xbox streaming core: the input/report formats, the session API and the sign-in
// chain against scripted service replies, and the WebRTC session against a second local libdatachannel
// peer that plays the console (real ICE, DTLS, SCTP and RTP on loopback). None of this proves that
// Microsoft's services or a real console accept the client; it proves the client does what the reference
// client does and survives the replies it can get.
#include <windows.h>

#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "veyra/xbox/Account.h"
#include "veyra/xbox/Protocol.h"
#include "veyra/xbox/StreamApi.h"
#include "veyra/xbox/StreamProfile.h"
#include "veyra/xbox/WebRtcSession.h"
#include "veyra/xbox/DisconnectPolicy.h"
#include "veyra/xbox/VideoDecodeRecovery.h"

using namespace veyra::xbox;
using json = nlohmann::json;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("FAIL %s\n", what); }
    else if (GetEnvironmentVariableA("VEYRA_TEST_VERBOSE", nullptr, 0) > 0) std::printf("ok   %s\n", what);
    std::fflush(stdout);
}

// ---------------------------------------------------------------- scripted HTTPS

struct FakeTransport final : HttpsTransport {
    struct Reply { std::string host, path; int status; std::string body; };
    std::deque<Reply> script;
    std::vector<HttpsRequest> seen;
    std::mutex mutex;
    HttpsResponse send(const HttpsRequest& r) override {
        std::lock_guard lock(mutex);
        seen.push_back(r);
        if (script.empty()) return {599, "no scripted reply for " + r.host + r.path};
        Reply reply = script.front();
        if (reply.host != r.host || reply.path != r.path) return {598, "unexpected request " + r.method + " " + r.host + r.path + " (wanted " + reply.host + reply.path + ")"};
        script.pop_front();
        return {reply.status, reply.body};
    }
    void add(std::string host, std::string path, int status, std::string body) { script.push_back({std::move(host), std::move(path), status, std::move(body)}); }
};

// ---------------------------------------------------------------- protocol

void protocolTests() {
    const auto meta = clientMetadataReport(0, 12.5, 1);
    check(meta.size() == 15 && meta[0] == 8 && meta[1] == 0 && meta[14] == 1, "client metadata report: type 8, 15 bytes, touch points last");
    double t = 0;
    std::memcpy(&t, meta.data() + 6, 8);
    check(t == 12.5, "report timestamp is a little-endian double at offset 6");

    GamepadFrame pad;
    pad.index = 0;
    pad.buttons = BtnA | BtnMenu | BtnNexus;
    pad.leftX = 1000; pad.leftY = -2; pad.rightX = -32767; pad.rightY = 32767;
    pad.leftTrigger = 65535; pad.rightTrigger = 257;
    const auto r = gamepadReport(7, 0, {pad});
    check(r.size() == 14 + 1 + 23, "gamepad report: 14 header + count + 23 per pad");
    check(r[0] == 2 && r[2] == 7 && r[14] == 1 && r[15] == 0, "type 2, sequence, one pad, index 0");
    check(r[16] == uint8_t((BtnA | BtnMenu | BtnNexus) & 0xFF) && r[17] == 0, "button mask little-endian");
    check(r[18] == 0xE8 && r[19] == 0x03 && r[20] == 0xFE && r[21] == 0xFF, "left stick X/Y as signed little-endian");
    check(r[26] == 0xFF && r[27] == 0xFF && r[28] == 0x01 && r[29] == 0x01, "triggers 0..65535");
    check(r[30] == 1 && r[31] == 0 && r[32] == 0 && r[33] == 0 && r[34] == 0 && r[35] == 0 && r[36] == 0 && r[37] == 1,
          "physicality LE 1 then virtual physicality written big-endian, as the reference client");

    veyra::remoteplay::ControllerState s;
    s.inputActive = true;
    s.buttons = veyra::remoteplay::ControllerState::Cross | veyra::remoteplay::ControllerState::Options |
                veyra::remoteplay::ControllerState::Share | veyra::remoteplay::ControllerState::PS | veyra::remoteplay::ControllerState::Down;
    s.leftY = 32767; s.rightY = -32768; s.leftX = -32768; s.l2 = 255; s.r2 = 1;
    const GamepadFrame f = gamepadFromController(s);
    check(f.buttons == (BtnA | BtnMenu | BtnView | BtnNexus | BtnDown), "SDL buttons map positionally; Guide is Nexus");
    check(f.leftY == -32767 && f.rightY == 32767 && f.leftX == -32767, "sticks: Y flipped to up-positive, clamped to +-32767");
    check(f.leftTrigger == 65535 && f.rightTrigger == 257, "triggers scaled from 0..255");
    s.inputActive = false;
    check(gamepadFromController(s) == GamepadFrame{}, "an inactive pad sends a neutral frame");

    const uint8_t vib[] = {128, 0, 0, 0, 80, 20, 0, 5, 0x10, 0x00, 0x05, 0x00, 1};
    const auto v = parseVibration(vib, sizeof(vib));
    check(v && v->leftMotor == 80 && v->rightMotor == 20 && v->rightTrigger == 5 && v->durationMs == 16 && v->delayMs == 5 && v->repeat == 1, "vibration report");
    check(!parseVibration(vib, 5), "a short vibration report is ignored");
    const uint8_t meta2[] = {16, 0, 0x38, 0x04, 0, 0, 0x80, 0x07, 0, 0};
    uint32_t w = 0, h = 0;
    check(parseServerMetadata(meta2, sizeof(meta2), &w, &h) && w == 1920 && h == 1080, "server metadata: height then width");
}

// ---------------------------------------------------------------- quality / ICE profile

void profileTests() {
    check(std::string(osNameForTier(QualityTier::P720)) == "android" &&
          std::string(osNameForTier(QualityTier::P1080)) == "windows" &&
          std::string(osNameForTier(QualityTier::P1080Hq)) == "tizen",
          "the quality tiers map to the reference clients' osName values");
    int w = 0, h = 0;
    viewportForTier(QualityTier::P720, &w, &h);
    check(w == 1280 && h == 720, "the 720p tier reports a 1280x720 viewport");
    viewportForTier(QualityTier::P1080Hq, &w, &h);
    check(w == 1920 && h == 1080, "HQ keeps the 1080p viewport; the bitrate comes from the identity");

    // The default device info must stay byte-identical to the historical constant.
    const std::string historical = R"({"appInfo":{"env":{"clientAppId":"Microsoft.GamingApp","clientAppType":"native","clientAppVersion":"2203.1001.4.0","clientSdkVersion":"8.5.2","httpEnvironment":"prod","sdkInstallId":""}},"dev":{"hw":{"make":"Microsoft","model":"Surface Pro","sdktype":"native"},"os":{"name":"Windows 11","ver":"22631.2715","platform":"desktop"},"displayInfo":{"dimensions":{"widthInPixels":1920,"heightInPixels":1080},"pixelDensity":{"dpiX":1,"dpiY":1}}}})";
    check(deviceInfoForTier(QualityTier::P1080) == historical, "the default tier's device info is byte-identical to the historical constant");
    const json hq = json::parse(deviceInfoForTier(QualityTier::P1080Hq));
    check(hq["dev"]["os"]["name"] == "tizen" && hq["dev"]["displayInfo"]["dimensions"]["widthInPixels"] == 4096 &&
          hq["dev"]["displayInfo"]["dimensions"]["heightInPixels"] == 2160 &&
          hq["appInfo"]["env"]["clientAppId"] == "Microsoft.GamingApp",
          "the HQ tier changes only os.name and the display dimensions");

    std::string host; uint16_t port = 0; bool turn = true;
    check(parseIceServerUrl("stun:stun.l.google.com:19302", &host, &port, &turn) && host == "stun.l.google.com" && port == 19302 && !turn,
          "a STUN URL parses with its port");
    check(parseIceServerUrl("turn:relay.example.com", &host, &port, &turn) && host == "relay.example.com" && port == 3478 && turn,
          "a TURN URL without a port defaults to 3478");
    check(parseIceServerUrl("stun:[2001:db8::1]:3478", &host, &port, &turn) && host == "2001:db8::1" && port == 3478,
          "bracketed IPv6 literals parse");
    check(!parseIceServerUrl("turns:secure.example.com:5349", &host, &port, &turn) &&
          !parseIceServerUrl("stun:", &host, &port, &turn) &&
          !parseIceServerUrl("stun:host:0", &host, &port, &turn) &&
          !parseIceServerUrl("stun:host:99999", &host, &port, &turn) &&
          !parseIceServerUrl("http:host:80", &host, &port, &turn),
          "unknown schemes, empty hosts and invalid ports are rejected");
    check(defaultStunServers().size() == 7, "the reference STUN list has seven entries");

    // Full Teredo expansion: client4/server4 x decoded/candidate/3074 ports, deduplicated.
    const auto full = StreamApi::parseIce(R"([{"candidate":"a=candidate:1 1 UDP 100 2001:0:4136:e378:8000:63bf:3fff:fdd2 9002 typ host","sdpMid":"0","sdpMLineIndex":"0"},{"candidate":"a=end-of-candidates"}])", true);
    check(full.size() == 6, "full expansion adds five IPv4 candidates and keeps the Teredo one");
    std::string server4, client4;
    int teredoPort = 0;
    check(StreamApi::teredoIpv4("2001:0:4136:e378:8000:63bf:3fff:fdd2", &client4, &teredoPort, &server4) && server4 == "65.54.227.120",
          "the Teredo server IPv4 is bytes 4..7, unobfuscated");
    bool hasClientPort = false, hasServerCandidate = false, hasClientCandidate = false, hasServer3074 = false, hasClient3074 = false;
    for (const auto& c : full) {
        hasClientPort |= c.candidate.find("192.0.2.45 40000") != std::string::npos;
        hasServerCandidate |= c.candidate.find("65.54.227.120 9002") != std::string::npos;
        hasClientCandidate |= c.candidate.find("192.0.2.45 9002") != std::string::npos;
        hasServer3074 |= c.candidate.find("65.54.227.120 3074") != std::string::npos;
        hasClient3074 |= c.candidate.find("192.0.2.45 3074") != std::string::npos;
    }
    check(hasClientPort && hasServerCandidate && hasClientCandidate && hasServer3074 && hasClient3074,
          "the five combinations cover client/server addresses and the decoded/candidate/3074 ports");
    const auto dedup = StreamApi::parseIce(R"([{"candidate":"a=candidate:1 1 UDP 100 2001:0:4136:e378:8000:63bf:3fff:fdd2 3074 typ host","sdpMid":"0","sdpMLineIndex":"0"}])", true);
    check(dedup.size() == 4, "a candidate already on port 3074 does not duplicate the 3074 pairs");
}

// ---------------------------------------------------------------- token refresh / pulse

void resilienceTests() {
    auto http = std::make_shared<FakeTransport>();
    const std::string host = "uks.core.gssv-play-prodxhome.xboxlive.com";
    StreamApi api(http, host, "OLD");
    api.setRetryDelayMs(1);
    int refreshes = 0;
    api.setTokenRefresh([&] { ++refreshes; return std::string("NEW"); });
    http->add(host, "/v5/sessions/home/S1/keepalive", 401, R"({"code":"Unauthorized"})");
    http->add(host, "/v5/sessions/home/S1/keepalive", 200, "{}");
    api.keepalive("S1");
    check(refreshes == 1 && http->seen.size() == 2 && http->seen.back().headers["Authorization"] == "Bearer NEW",
          "a 401 refreshes the token once and replays the request");
    http->add(host, "/v5/sessions/home/S1/keepalive", 401, "{}");
    http->add(host, "/v5/sessions/home/S1/keepalive", 401, "{}");
    bool threw = false;
    try { api.keepalive("S1"); } catch (const ServiceError& e) { threw = e.status == 401; }
    check(threw && refreshes == 2, "a second refusal is a real error; the refresh is not looped");

    auto plain = std::make_shared<FakeTransport>();
    StreamApi noRefresh(plain, host, "GS");
    noRefresh.setRetryDelayMs(1);
    plain->add(host, "/v5/sessions/home/S1/keepalive", 403, "{}");
    threw = false;
    try { noRefresh.keepalive("S1"); } catch (const ServiceError& e) { threw = e.status == 403; }
    check(threw && plain->seen.size() == 1, "without a refresh callback a 403 is not retried");

    http->add(host, "/v5/sessions/home/S1/configuration", 200, R"({"keepAlivePulseInSeconds":120})");
    check(api.keepAlivePulseInSeconds("S1") == 120, "the configuration pulse is read");
    http->add(host, "/v5/sessions/home/S1/configuration", 200, R"({"serverDetails":{}})");
    check(api.keepAlivePulseInSeconds("S1") == 0, "a missing pulse falls back to the caller's default");
    http->add(host, "/v5/sessions/home/S1/configuration", 200, "not json");
    check(api.keepAlivePulseInSeconds("S1") == 0, "a malformed configuration does not break the session");
}

// ---------------------------------------------------------------- session API

void apiTests() {
    std::string ip;
    int port = 0;
    check(StreamApi::teredoIpv4("2001:0:4136:e378:8000:63bf:3fff:fdd2", &ip, &port) && ip == "192.0.2.45" && port == 40000, "Teredo address decodes to the RFC 4380 example");
    check(!StreamApi::teredoIpv4("2001:db8::1", &ip, &port), "a non-Teredo 2001: address is not decoded");

    const auto consoles = StreamApi::parseConsoles(R"({"totalItems":2,"results":[{"deviceName":"客厅 Series X","serverId":"F4001234ABCD5678","powerState":"ConnectedStandby","consoleType":"XboxSeriesX","playPath":"x","outOfHomeWarning":false},{"deviceName":"broken"}],"continuationToken":null})");
    check(consoles.size() == 1 && consoles[0].serverId == "F4001234ABCD5678" && consoles[0].name == "客厅 Series X" && consoles[0].powerState == "ConnectedStandby", "console list (entries without a server id are skipped)");

    const auto ice = StreamApi::parseIce(R"([{"candidate":"a=candidate:1 1 UDP 100 2001:0:4136:e378:8000:63bf:3fff:fdd2 9002 typ host","sdpMid":"0","sdpMLineIndex":"0"},{"candidate":"a=candidate:2 1 UDP 90 192.168.1.30 9002 typ host","sdpMid":"0","sdpMLineIndex":0},{"candidate":"a=end-of-candidates"}])");
    check(ice.size() == 4, "a Teredo candidate adds two IPv4 candidates; end-of-candidates is dropped");
    check(ice[0].candidate.find("192.0.2.45 9002") != std::string::npos && ice[1].candidate.find("192.0.2.45 40000") != std::string::npos, "the IPv4 candidates use port 9002 and the Teredo port");

    auto http = std::make_shared<FakeTransport>();
    const std::string host = "uks.core.gssv-play-prodxhome.xboxlive.com";
    StreamApi api(http, host, "GS");
    api.setRetryDelayMs(1);
    http->add(host, "/v6/servers/home", 200, R"({"results":[{"deviceName":"X","serverId":"F1","powerState":"On"}]})");
    check(api.consoles().size() == 1, "consoles() reads the list");
    check(http->seen.back().headers["Authorization"] == "Bearer GS", "the streaming token is the bearer");
    http->add(host, "/v5/sessions/home/play", 200, R"({"sessionPath":"v5/sessions/home/ABC-123","state":"Provisioning"})");
    check(api.play("F1") == "ABC-123", "play returns the session id from sessionPath");
    {
        const json body = json::parse(http->seen.back().body);
        check(body["serverId"] == "F1" && body["settings"]["nanoVersion"] == "V3;WebrtcTransport.dll" && !http->seen.back().headers["X-MS-Device-Info"].empty(),
              "play sends the server id, nano V3 and the device info header");
    }
    // The HQ tier must reach both the play settings and the device-info header, consistently.
    http->add(host, "/v5/sessions/home/play", 200, R"({"sessionPath":"v5/sessions/home/HQ-1","state":"Provisioning"})");
    StreamProfile hq;
    hq.tier = QualityTier::P1080Hq;
    check(api.play("F1", "zh-CN", hq) == "HQ-1", "play accepts the HQ profile");
    {
        const json body = json::parse(http->seen.back().body);
        const json info = json::parse(http->seen.back().headers["X-MS-Device-Info"]);
        check(body["settings"]["osName"] == "tizen" && info["dev"]["os"]["name"] == "tizen" &&
              info["dev"]["displayInfo"]["dimensions"]["widthInPixels"] == 4096,
              "the HQ tier is consistent across settings.osName and the device-info header");
    }
    http->add(host, "/v5/sessions/home/ABC-123/state", 200, R"({"state":"Failed","errorDetails":{"code":"WNSError","message":"WaitingForServerToRegister"}})");
    const auto st = api.state("ABC-123");
    check(st.state == "Failed" && st.errorCode == "WNSError" && st.errorMessage == "WaitingForServerToRegister", "state with error details");
    http->add(host, "/v5/sessions/home/ABC-123/sdp", 202, "");
    http->add(host, "/v5/sessions/home/ABC-123/sdp", 204, "");
    http->add(host, "/v5/sessions/home/ABC-123/sdp", 204, "");
    http->add(host, "/v5/sessions/home/ABC-123/sdp", 200, json{{"exchangeResponse", json{{"sdp", "v=0\r\n"}, {"sdpType", "answer"}, {"status", "success"}}.dump()}}.dump());
    check(api.exchangeSdp("ABC-123", "v=0 offer") == "v=0\r\n", "SDP exchange waits through 204 replies for the answer");
    {
        const json body = json::parse(http->seen[http->seen.size() - 4].body);
        check(body["messageType"] == "offer" && body["configuration"]["input"]["maxVersion"] == 8, "the offer carries the channel versions");
    }
    http->add(host, "/v5/sessions/home/ABC-123/ice", 200, "");
    http->add(host, "/v5/sessions/home/ABC-123/ice", 200, json{{"exchangeResponse", R"([{"candidate":"a=candidate:2 1 UDP 90 192.168.1.30 9002 typ host","sdpMid":"0","sdpMLineIndex":0}])"}}.dump());
    check(api.exchangeIce("ABC-123", {{"candidate:1 1 udp 2122260223 192.168.1.10 50000 typ host", "0", 0}}).size() == 1, "ICE exchange returns the console's candidates");
    http->add(host, "/v5/sessions/home/ABC-123/connect", 401, R"({"code":"Unauthorized"})");
    bool threw = false;
    try { api.connect("ABC-123", "LPT"); } catch (const ServiceError& e) { threw = e.status == 401; }
    check(threw, "a refused connect is a ServiceError with the status");

    // The xHome wrapper is nullable even on success (Greenlight's SDPResponse).
    // A pending wrapper is different from a console error; neither may be turned
    // into a json type exception or mistaken for a completed negotiation.
    const std::string answer = json{{"sdp", "v=0\r\n"}, {"sdpType", "answer"}, {"status", "success"}}.dump();
    const auto sdpReply = [&](const std::vector<std::pair<int, json>>& replies, std::string* error = nullptr) {
        auto transport = std::make_shared<FakeTransport>();
        StreamApi session(transport, host, "GS");
        session.setRetryDelayMs(0);
        const std::string path = "/v5/sessions/home/test/sdp";
        transport->add(host, path, 202, "");
        for (const auto& [status, body] : replies) transport->add(host, path, status, body.dump());
        try { return session.exchangeSdp("test", "v=0 offer"); }
        catch (const std::exception& e) { if (error) *error = e.what(); return std::string(); }
    };
    check(sdpReply({{200, {{"exchangeResponse", answer}, {"errorDetails", {{"code", nullptr}, {"message", nullptr}}}}}}) == "v=0\r\n",
          "a successful SDP with null error fields is accepted (field log 11 regression)");
    check(sdpReply({{200, {{"exchangeResponse", answer}, {"errorDetails", nullptr}}}}) == "v=0\r\n",
          "a successful SDP with null errorDetails is accepted");
    check(sdpReply({{200, {{"exchangeResponse", answer}, {"errorDetails", {{"code", 0}, {"message", nullptr}}}}}}) == "v=0\r\n",
          "a zero numeric error code does not reject a successful answer");
    check(sdpReply({{200, {{"exchangeResponse", nullptr}, {"errorDetails", {{"code", nullptr}, {"message", nullptr}}}}},
                    {202, {{"exchangeResponse", ""}, {"errorDetails", nullptr}}},
                    {200, {{"exchangeResponse", answer}, {"errorDetails", nullptr}}}}) == "v=0\r\n",
          "pending 200/202 wrappers are polled until the SDP is ready");
    std::string error;
    check(sdpReply({{200, {{"exchangeResponse", answer}, {"errorDetails", {{"code", 17}, {"message", nullptr}}}}}}, &error).empty() &&
              error.find("17") != std::string::npos,
          "a nonzero numeric service error is not silently accepted even with SDP present");
    error.clear();
    check(sdpReply({{200, {{"exchangeResponse", nullptr}, {"errorDetails", {{"code", "OfferRejected"}, {"message", "unsupported codec"}}}}}}, &error).empty() &&
              error.find("OfferRejected") != std::string::npos && error.find("unsupported codec") != std::string::npos,
          "a real console refusal preserves its code and message");
    error.clear();
    std::vector<std::pair<int, json>> pending(40, {200, {{"exchangeResponse", nullptr}, {"errorDetails", nullptr}}});
    check(sdpReply(pending, &error).empty() && error.find("did not answer") != std::string::npos && error.find("refused") == std::string::npos,
          "an unfinished exchange times out within the bounded polling budget without claiming refusal");
    try {
        const auto nullableIce = StreamApi::parseIce(R"([{"candidate":null,"sdpMid":null,"sdpMLineIndex":null},{"candidate":"a=candidate:1 1 UDP 90 192.168.1.30 9002 typ host","sdpMid":null,"sdpMLineIndex":null}])");
        check(nullableIce.size() == 1 && nullableIce[0].sdpMid == "0" && nullableIce[0].sdpMLineIndex == 0,
              "null ICE metadata and end markers do not abort the connection after SDP succeeds");
    } catch (const std::exception&) { check(false, "null ICE metadata and end markers do not abort the connection after SDP succeeds"); }
}

// ---------------------------------------------------------------- sign-in

void accountTests() {
    const auto dir = std::filesystem::temp_directory_path() / ("veyra-xbox-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "account.bin";
    int64_t now = 1'800'000'000;

    auto http = std::make_shared<FakeTransport>();
    {
        Account account(http, file);
        account.setClock([&] { return now; });
        check(!account.signedIn(), "no saved sign-in at first");
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/devicecode", 200,
                  R"({"user_code":"ABCD-EFGH","device_code":"DEV","verification_uri":"https://www.microsoft.com/link","expires_in":900,"interval":5,"message":"..."})");
        const DeviceCode code = account.beginSignIn();
        check(code.userCode == "ABCD-EFGH" && code.verificationUri == "https://www.microsoft.com/link" && code.intervalSeconds == 5, "device code");
        check(http->seen.back().body.find("client_id=1f907974-e22b-4810-a9de-d9647380c97e") != std::string::npos &&
              http->seen.back().body.find("scope=xboxlive.signin%20openid%20profile%20offline_access") != std::string::npos, "device code uses the xbox.com client id and scope");
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/token", 400, R"({"error":"authorization_pending"})");
        check(account.pollSignIn(code) == SignInPoll::Pending, "pending while the user has not signed in");
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/token", 400, R"({"error":"slow_down"})");
        check(account.pollSignIn(code) == SignInPoll::SlowDown, "slow_down is reported");
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/token", 200, R"({"access_token":"AT1","refresh_token":"RT1","expires_in":3600})");
        check(account.pollSignIn(code) == SignInPoll::Done && account.signedIn(), "signed in");
        check(std::filesystem::exists(file), "the sign-in is saved");
        std::ifstream in(file, std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        check(raw.find("RT1") == std::string::npos, "the saved file does not contain the refresh token in clear");
    }
    {
        Account account(http, file);
        account.setClock([&] { return now; });
        check(account.signedIn(), "the sign-in survives a restart");
        http->add("user.auth.xboxlive.com", "/user/authenticate", 200, R"({"Token":"UT","NotAfter":"2027-01-01T00:00:00.0000000Z","DisplayClaims":{"xui":[{"uhs":"123"}]}})");
        http->add("xsts.auth.xboxlive.com", "/xsts/authorize", 200, R"({"Token":"GSSV","NotAfter":"2027-01-01T00:00:00Z","DisplayClaims":{"xui":[{"uhs":"123"}]}})");
        http->add("xhome.gssv-play-prod.xboxlive.com", "/v2/login/user", 200,
                  R"({"offeringSettings":{"regions":[{"name":"WestEurope","baseUri":"https://weu.core.gssv-play-prodxhome.xboxlive.com","isDefault":false},{"name":"EastAsia","baseUri":"https://eas.core.gssv-play-prodxhome.xboxlive.com/","isDefault":true}]},"market":"CN","gsToken":"GS","tokenType":"bearer","durationInSeconds":14400})");
        const StreamingAccess access = account.streamingAccess();
        check(access.host == "eas.core.gssv-play-prodxhome.xboxlive.com" && access.gsToken == "GS" && access.regionName == "EastAsia", "the default region and the streaming token");
        const json userAuth = json::parse(http->seen[http->seen.size() - 3].body);
        check(userAuth["Properties"]["RpsTicket"] == "d=AT1" && userAuth["RelyingParty"] == "http://auth.xboxlive.com", "user authentication uses the access token as an RPS ticket");
        const json xsts = json::parse(http->seen[http->seen.size() - 2].body);
        check(xsts["RelyingParty"] == "http://gssv.xboxlive.com/" && xsts["Properties"]["UserTokens"][0] == "UT", "XSTS for the gssv relying party");
        check(http->seen.back().headers["x-gssv-client"] == "XboxComBrowser" && json::parse(http->seen.back().body)["offeringId"] == "xhome", "streaming sign-in for the xhome offering");
        const size_t before = http->seen.size();
        account.streamingAccess();
        check(http->seen.size() == before, "a valid streaming token is reused");

        http->add("login.live.com", "/oauth20_token.srf", 200, R"({"access_token":"LPT","refresh_token":"RT-X"})");
        check(account.transferToken() == "LPT", "console transfer token");
        check(http->seen.back().body.find("PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN") != std::string::npos && http->seen.back().body.find("refresh_token=RT1") != std::string::npos,
              "the transfer token comes from the refresh token");

        // An hour later the access token has expired: it is refreshed and the rotated refresh token kept.
        now += 3700;
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/token", 200, R"({"access_token":"AT2","refresh_token":"RT2","expires_in":3600})");
        http->add("login.live.com", "/oauth20_token.srf", 200, R"({"access_token":"LPT2"})");
        check(account.transferToken() == "LPT2" && http->seen.back().body.find("refresh_token=RT2") != std::string::npos, "an expired access token is refreshed and the new refresh token used");
    }
    {
        Account account(http, file);
        account.setClock([&] { return now + 7200; });
        http->add("login.microsoftonline.com", "/consumers/oauth2/v2.0/token", 400, R"({"error":"invalid_grant"})");
        bool expired = false;
        try { account.transferToken(); } catch (const ServiceError& e) { expired = e.status == 400; }
        check(expired && !account.signedIn() && !std::filesystem::exists(file), "a refused refresh signs out and deletes the saved sign-in");
    }
    check(describeXboxError(R"({"Identity":"0","XErr":2148916233,"Message":""})").find(L"Xbox 档案") != std::wstring::npos, "XErr: no Xbox profile");
    check(describeXboxError("not json").empty(), "an unknown error has no sentence");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---------------------------------------------------------------- WebRTC loopback

// A second libdatachannel peer playing the console: answers the offer, sends H.264 and Opus, speaks the
// message-channel handshake and records what the client sends.
class FakeConsole final : public Signaling {
public:
    FakeConsole() {
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        config.bindAddress = "127.0.0.1";   // loopback only: VPN/TUN adapters must not take the test's packets
        pc = std::make_shared<rtc::PeerConnection>(config);
        pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState) { cv.notify_all(); });
        pc->onTrack([this](std::shared_ptr<rtc::Track> track) {
            auto desc = track->description();
            if (desc.type() == "video") {
                desc.addSSRC(4242, "video");
                track->setDescription(desc);
                auto config = std::make_shared<rtc::RtpPacketizationConfig>(4242, "video", 102, rtc::H264RtpPacketizer::ClockRate);
                track->setMediaHandler(std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::LongStartSequence, config, 1200));
                video = track;
                videoConfig = config;
            } else if (desc.type() == "audio") {
                desc.addSSRC(4343, "audio");
                track->setDescription(desc);
                auto config = std::make_shared<rtc::RtpPacketizationConfig>(4343, "audio", 111, 48000);
                track->setMediaHandler(std::make_shared<rtc::OpusRtpPacketizer>(config));
                audio = track;
                audioConfig = config;
            }
        });
        pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            std::lock_guard lock(mutex);
            channels.push_back(dc);   // libdatachannel drops the callbacks of a channel nobody holds
            labels.push_back(dc->label() + "/" + dc->protocol());
            if (dc->label() == "message") {
                message = dc;
                dc->onMessage([this](rtc::message_variant data) {
                    std::string text;
                    if (auto* b = std::get_if<rtc::binary>(&data)) text.assign(reinterpret_cast<const char*>(b->data()), b->size());
                    else text = std::get<std::string>(data);
                    const json j = json::parse(text);
                    std::lock_guard l(mutex);
                    messages.push_back(j.value("type", "") + " " + j.value("target", ""));
                    rawMessages.push_back(text);
                    if (j.value("type", "") == "Handshake") {
                        const std::string ack = json{{"type", "HandshakeAck"}, {"id", j.value("id", "")}, {"cv", ""}}.dump();
                        message->send(ack);
                    }
                    cv.notify_all();
                });
            } else if (dc->label() == "control") {
                dc->onMessage([this](rtc::message_variant data) {
                    std::string text;
                    if (auto* b = std::get_if<rtc::binary>(&data)) text.assign(reinterpret_cast<const char*>(b->data()), b->size());
                    else text = std::get<std::string>(data);
                    std::lock_guard l(mutex);
                    control.push_back(json::parse(text).value("message", ""));
                    cv.notify_all();
                });
            } else if (dc->label() == "input") {
                input = dc;
                dc->onMessage([this](rtc::message_variant data) {
                    if (auto* b = std::get_if<rtc::binary>(&data)) {
                        std::lock_guard l(mutex);
                        reports.emplace_back(reinterpret_cast<const uint8_t*>(b->data()), reinterpret_cast<const uint8_t*>(b->data()) + b->size());
                        cv.notify_all();
                    }
                });
            }
        });
    }
    ~FakeConsole() override { if (pc) pc->close(); }

    std::string exchangeSdp(const std::string& offer) override {
        offerSdp = offer;
        pc->setRemoteDescription(rtc::Description(offer, rtc::Description::Type::Offer));
        pc->setLocalDescription(rtc::Description::Type::Answer);
        std::unique_lock lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete; });
        std::string sdp = std::string(*pc->localDescription());
        // Answer without candidates: they go through the ICE exchange, as with the service.
        std::string out, line;
        std::istringstream in(sdp);
        while (std::getline(in, line)) {
            if (line.rfind("a=candidate", 0) == 0 || line.rfind("a=end-of-candidates", 0) == 0) continue;
            out += line + "\n";
        }
        return out;
    }

    std::vector<IceCandidate> exchangeIce(const std::vector<IceCandidate>& local) override {
        for (const auto& c : local) pc->addRemoteCandidate(rtc::Candidate(c.candidate, c.sdpMid));
        std::vector<IceCandidate> out;
        for (const auto& c : pc->localDescription()->candidates()) out.push_back({"a=" + std::string(c.candidate()), "0", 0});
        return out;
    }

    template <typename F> bool waitFor(F f, int ms = 5000) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(ms), f);
    }

    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video, audio;
    std::shared_ptr<rtc::RtpPacketizationConfig> videoConfig, audioConfig;
    std::shared_ptr<rtc::DataChannel> message, input;
    std::vector<std::shared_ptr<rtc::DataChannel>> channels;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> labels, messages, rawMessages, control;
    std::vector<std::vector<uint8_t>> reports;
    std::string offerSdp;
};

std::vector<uint8_t> nal(uint8_t header, size_t size, uint8_t seed) {
    std::vector<uint8_t> out = {0, 0, 0, 1, header};
    for (size_t i = 0; i < size; ++i) out.push_back(uint8_t((i * 31 + seed) | 1));   // never a start-code pattern
    return out;
}

void webrtcTests() {
    FakeConsole console;
    WebRtcSession session;
    session.setBindAddress("127.0.0.1");
    std::mutex m;
    std::vector<std::vector<uint8_t>> units;
    std::vector<std::vector<uint8_t>> opus;
    std::optional<Vibration> vibration;
    std::string endedReason;
    uint32_t serverW = 0, serverH = 0;
    WebRtcCallbacks callbacks;
    callbacks.video = [&](std::vector<uint8_t>&& unit, uint32_t, int64_t arrival) { std::lock_guard l(m); if (arrival > 0) units.push_back(std::move(unit)); };
    callbacks.audio = [&](const uint8_t* d, size_t n, uint32_t) { std::lock_guard l(m); opus.emplace_back(d, d + n); };
    callbacks.vibration = [&](const Vibration& v) { std::lock_guard l(m); vibration = v; };
    callbacks.serverVideoSize = [&](uint32_t w, uint32_t h) { std::lock_guard l(m); serverW = w; serverH = h; };
    callbacks.ended = [&](const std::string& reason) { std::lock_guard l(m); endedReason=reason; };
    session.setCallbacks(callbacks);

    std::string error;
    const bool started = session.start(console, &error, std::chrono::seconds(15));
    check(started, ("the client connects to a local peer playing the console: " + error).c_str());
    if (!started) return;
    check(console.offerSdp.find("profile-level-id=4d001f") != std::string::npos && console.offerSdp.find("profile-level-id=42e01f") != std::string::npos,
          "the offer lists H.264 Main and Constrained Baseline");
    check(console.offerSdp.find("a=candidate") == std::string::npos, "the offer carries no candidates (they go through the ICE exchange)");
    check(session.waitReady(std::chrono::seconds(5)), "the message-channel handshake completes");
    check(console.waitFor([&] { return console.labels.size() == 4; }), "four data channels arrive");
    {
        std::lock_guard l(console.mutex);
        std::vector<std::string> sorted = console.labels;
        std::sort(sorted.begin(), sorted.end());
        check(sorted == std::vector<std::string>{"chat/chatV1", "control/controlV1", "input/1.0", "message/messageV1"}, "channel labels and protocols as the reference client");
    }
    check(console.waitFor([&] { return console.control.size() >= 4; }), "control messages arrive");
    {
        std::lock_guard l(console.mutex);
        check(console.control.size() >= 2 && console.control[0] == "authorizationRequest" && console.control[1] == "gamepadChanged", "control: authorization, then the pad");
    }
    check(console.waitFor([&] { return !console.reports.empty(); }), "the input channel receives the client metadata report");
    {
        std::lock_guard l(console.mutex);
        check(!console.reports.empty() && console.reports[0].size() == 15 && console.reports[0][0] == 8, "first input report is client metadata");
    }
    check(console.waitFor([&] {
        for (const auto& s : console.messages) if (s.find("/streaming/characteristics/dimensionschanged") != std::string::npos) return true;
        return false;
    }), "the client sends its configuration messages");

    GamepadFrame pad;
    pad.buttons = BtnA;
    pad.leftX = 1234;
    session.sendGamepad(pad);
    check(console.waitFor([&] { return console.reports.size() >= 2; }), "a gamepad report arrives");
    {
        std::lock_guard l(console.mutex);
        const auto r = console.reports.empty() ? std::vector<uint8_t>{} : console.reports.back();
        const auto expected = gamepadReport(1, 0, {pad});
        check(r.size() == expected.size() && std::equal(r.begin() + 14, r.end(), expected.begin() + 14) && r[0] == 2, "gamepad report bytes as built");
    }

    // Console -> client: a vibration report and the server's video size on the input channel.
    const uint8_t vib[] = {128, 0, 0, 0, 60, 40, 0, 0, 100, 0, 0, 0, 0};
    console.input->send(reinterpret_cast<const std::byte*>(vib), sizeof(vib));
    const uint8_t size[] = {16, 0, 0x38, 0x04, 0, 0, 0x80, 0x07, 0, 0};
    console.input->send(reinterpret_cast<const std::byte*>(size), sizeof(size));

    // Media: wait until the console's tracks are open, then send an IDR access unit larger than one
    // packet (fragmented) and a P frame, and two Opus packets.
    check(console.waitFor([&] { return console.video && console.video->isOpen() && console.audio && console.audio->isOpen(); }), "the console's media tracks open");
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> idr;
    for (const auto& part : {nal(0x67, 12, 1), nal(0x68, 4, 2), nal(0x65, 5000, 3)}) idr.insert(idr.end(), part.begin(), part.end());
    const auto p1 = nal(0x41, 800, 4);
    sent.push_back(idr);
    sent.push_back(p1);
    uint32_t ts = 0;
    for (const auto& unit : sent) {
        console.videoConfig->timestamp = ts;
        console.video->sendFrame(reinterpret_cast<const std::byte*>(unit.data()), unit.size(), rtc::FrameInfo(ts));
        ts += 1500;
    }
    // A third frame: the depacketizer emits a frame once the next timestamp starts.
    const auto p2 = nal(0x41, 100, 5);
    console.video->sendFrame(reinterpret_cast<const std::byte*>(p2.data()), p2.size(), rtc::FrameInfo(ts));
    const std::vector<uint8_t> opus1 = {0xFC, 1, 2, 3}, opus2 = {0xFC, 4, 5, 6, 7};
    console.audio->sendFrame(reinterpret_cast<const std::byte*>(opus1.data()), opus1.size(), rtc::FrameInfo(uint32_t(0)));
    console.audio->sendFrame(reinterpret_cast<const std::byte*>(opus2.data()), opus2.size(), rtc::FrameInfo(uint32_t(960)));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard l(m);
            if (units.size() >= 2 && opus.size() >= 2 && vibration && serverW) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        std::lock_guard l(m);
        check(units.size() >= 2, "access units arrive");
        check(units.size() >= 2 && units[0] == sent[0], "the fragmented IDR access unit (SPS, PPS, IDR) arrives byte for byte, Annex-B");
        check(units.size() >= 2 && units[1] == sent[1], "the P frame arrives byte for byte");
        check(opus.size() >= 2 && opus[0] == opus1 && opus[1] == opus2, "Opus packets arrive unchanged");
        check(vibration && vibration->leftMotor == 60 && vibration->rightMotor == 40 && vibration->durationMs == 100, "vibration reaches the callback");
        check(serverW == 1920 && serverH == 1080, "server video size reaches the callback");
    }
    session.requestKeyframe();
    check(console.waitFor([&] { for (const auto& c : console.control) if (c == "videoKeyframeRequested") return true; return false; }), "a keyframe request goes out on the control channel");
    const auto stats = session.stats();
    if (!(stats.videoUnits >= 2 && stats.audioPackets >= 2 && stats.handshakeDone && stats.videoBytes > 5000))
        std::printf("stats: units=%llu audio=%llu handshake=%d bytes=%llu\n", (unsigned long long)stats.videoUnits, (unsigned long long)stats.audioPackets, int(stats.handshakeDone), (unsigned long long)stats.bytesReceived);
    check(stats.videoUnits >= 2 && stats.audioPackets >= 2 && stats.handshakeDone && stats.videoBytes > 5000, "statistics");
    // Malformed fields used to throw through the asynchronous callback.
    console.message->send(std::string("{\"type\":null}"));
    console.message->send(std::string("{\"type\":\"Message\",\"target\":null}"));
    session.videoPresented(0xFABCDE12,1000000,1100000,1200000,1300000);
    check(console.waitFor([&]{for(const auto& report:console.reports)if(report.size()==43&&report[0]==ReportMetadata)return true;return false;}),"rendered video feedback is sent even without a gamepad change");
    {
        std::lock_guard lock(console.mutex);
        bool valid=false;
        for(const auto& report:console.reports)if(report.size()==43&&report[0]==ReportMetadata)
            valid=report[14]==1&&detail::get32(report.data()+15)==0xFABCDE12;
        check(valid,"feedback preserves the original RTP key and count");
    }
    const auto encoded=videoFeedbackReport(7,1234,{0xFABCDE12,101,102,103,104});
    check(encoded.size()==43&&detail::get32(encoded.data()+2)==7&&detail::get32(encoded.data()+19)==101&&
          detail::get32(encoded.data()+23)==102&&detail::get32(encoded.data()+27)==103&&
          detail::get32(encoded.data()+31)==104&&detail::get32(encoded.data()+35)==1234&&
          detail::get32(encoded.data()+39)==1234,"frame feedback matches Greenlight's seven-field wire format");
    // Exercise real transport shutdown while the caller sends input/keyframe
    console.message->send(json{{"type","TransactionStart"},{"target","/streaming/sessionLifetimeManagement/serverInitiatedDisconnect"},{"id","shutdown-test"},{"content",json{{"reason","KickForServerShutdown"}}.dump()}}.dump());
    for(unsigned i=0;i<100;++i){ {std::lock_guard l(m);if(!endedReason.empty())break;} std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    {std::lock_guard l(m);check(endedReason=="KickForServerShutdown","specific server shutdown reason survives the real message channel");}

    // Exercise real transport shutdown while the caller sends input/keyframe
    // requests. This covers both the isOpen/send race and local close racing
    // callbacks; exceptions escaping any thread terminate this test process.
    std::atomic<bool> stopped=false;
    std::thread sender([&]{while(!stopped){session.sendGamepad(pad);session.requestKeyframe();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
    console.pc->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    session.close();stopped=true;sender.join();
    session.sendGamepad(pad);session.requestKeyframe();session.close();
    check(!session.ready(), "remote/local close with active sends is contained and clears readiness");
}

// The explicit-offer path: b=AS hints must survive createOffer -> setLocalDescription and the
// peer must still connect and handshake. The 720p tier must reach dimensionschanged.
void webrtcProfileTests() {
    FakeConsole console;
    WebRtcSession session;
    session.setBindAddress("127.0.0.1");
    StreamProfile profile;
    profile.tier = QualityTier::P720;
    profile.videoBitrateKbps = 15000;
    profile.audioBitrateKbps = 128;
    std::string error;
    const bool started = session.start(console, &error, std::chrono::seconds(15), profile);
    check(started, ("the munged-offer client connects to the local peer: " + error).c_str());
    if (!started) return;
    check(console.offerSdp.find("b=AS:15000") != std::string::npos && console.offerSdp.find("b=AS:128") != std::string::npos,
          "the exchanged offer carries the video and audio bandwidth hints");
    check(console.offerSdp.find("profile-level-id=4d001f") != std::string::npos,
          "the explicit offer keeps the reference codec order");
    check(session.waitReady(std::chrono::seconds(5)), "the handshake completes on the munged offer");
    check(console.waitFor([&] {
        for (const auto& s : console.rawMessages) {
            if (s.find("/streaming/characteristics/dimensionschanged") == std::string::npos) continue;
            // The content field is a JSON-encoded string: parse the outer message, then the content.
            const json outer = json::parse(s);
            const json dims = json::parse(outer.value("content", ""));
            if (dims.value("horizontal", 0) == 1280 && dims.value("vertical", 0) == 720 &&
                dims.value("preferredWidth", 0) == 1280 && dims.value("safeAreaRight", 0) == 1280)
                return true;
        }
        return false;
    }), "the 720p tier reports a 1280x720 viewport on the message channel");
    session.close();
}

// ---------------------------------------------------------------- loss recovery

// Builds one RTP packet carrying a single-NAL payload (no fragmentation needed to
// exercise ordering/NACK; the FU-A path is covered by the loopback test).
rtc::message_ptr rtpPacket(uint16_t seq, uint32_t timestamp, bool marker, uint8_t payload = 0x41) {
    auto m = std::make_shared<rtc::Message>(sizeof(rtc::RtpHeader) + 4);
    auto* h = reinterpret_cast<rtc::RtpHeader*>(m->data());
    std::memset(h, 0, sizeof(*h));
    h->preparePacket();
    h->setPayloadType(102);
    h->setSeqNumber(seq);
    h->setTimestamp(timestamp);
    h->setSsrc(0x12345678);
    h->setMarker(marker);
    m->data()[sizeof(rtc::RtpHeader)] = std::byte(payload);
    return m;
}

struct RecoveryHarness {
    VideoLossRecovery recovery{std::chrono::milliseconds(40), 2048};
    rtc::message_vector delivered;
    std::vector<rtc::message_ptr> nacks;
    void feed(rtc::message_vector packets) {
        recovery.incoming(packets, [this](rtc::message_ptr m) { nacks.push_back(std::move(m)); });
        for (auto& m : packets) delivered.push_back(std::move(m));
    }
};

void lossRecoveryTests() {
    // 1. In-order complete frame passes through with zero added latency.
    {
        RecoveryHarness h;
        h.feed({rtpPacket(100, 1000, false), rtpPacket(101, 1000, false), rtpPacket(102, 1000, true)});
        check(h.delivered.size() == 3 && h.nacks.empty(), "a complete in-order frame passes straight through");
        check(h.recovery.stats().framesDropped == 0, "no drops on a clean frame");
    }
    // 2. Out-of-order packets within a frame are reordered before depacketization.
    {
        RecoveryHarness h;
        h.feed({rtpPacket(102, 2000, true), rtpPacket(100, 2000, false), rtpPacket(101, 2000, false)});
        check(h.delivered.size() == 3, "an out-of-order complete frame is released once whole");
        bool ordered = true;
        uint16_t prev = 99;
        for (auto& m : h.delivered) {
            const auto* hdr = reinterpret_cast<const rtc::RtpHeader*>(m->data());
            if (hdr->seqNumber() != uint16_t(prev + 1)) ordered = false;
            prev = hdr->seqNumber();
        }
        check(ordered, "released packets are in sequence order");
        check(h.recovery.stats().packetsReordered >= 1, "reordering is counted");
    }
    // 3. A lost packet triggers NACK; the retransmission completes the frame.
    {
        RecoveryHarness h;
        h.feed({rtpPacket(200, 3000, false), rtpPacket(202, 3000, true)}); // 201 missing
        check(h.delivered.empty(), "an incomplete frame is held, not fed torn");
        check(!h.nacks.empty(), "the hole triggers a NACK request");
        bool nackHas201 = false;
        for (auto& n : h.nacks) {
            auto* nack = reinterpret_cast<rtc::RtcpNack*>(n->data());
            for (unsigned i = 0; i < nack->getSeqNoCount(); ++i)
                for (auto s : nack->parts[i].getSequenceNumbers())
                    if (s == 201) nackHas201 = true;
        }
        check(nackHas201, "the NACK names the missing sequence 201");
        h.feed({rtpPacket(201, 3000, false)}); // retransmission
        check(h.delivered.size() == 3, "the retransmission completes and releases the frame");
        check(h.recovery.stats().packetsRecovered == 1, "the recovered packet is counted");
    }
    // 4. A frame still incomplete after the wait window is dropped whole.
    {
        RecoveryHarness h;
        h.feed({rtpPacket(300, 4000, false), rtpPacket(302, 4000, true)});
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        h.feed({rtpPacket(400, 5000, true)}); // next frame forces the flush
        check(h.recovery.stats().framesDropped == 1, "the expired incomplete frame is dropped whole");
        bool sawTorn = false;
        for (auto& m : h.delivered) {
            const auto* hdr = reinterpret_cast<const rtc::RtpHeader*>(m->data());
            if (hdr->timestamp() == 4000) sawTorn = true;
        }
        check(!sawTorn, "no packet of the dropped frame reaches the depacketizer");
    }
    // 5. NACK rounds are bounded.
    {
        RecoveryHarness h;
        h.feed({rtpPacket(500, 6000, false), rtpPacket(505, 6000, true)});
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            rtc::message_vector empty;
            h.feed(std::move(empty));
        }
        check(h.nacks.size() <= 8, "NACK requests stay bounded (max 4 rounds x 2 packets)");
    }
}

} // namespace

int main(int argc, char** argv) {
    {
        VideoDecodeRecovery recovery;
        check(recovery.failed(true)==VideoDecodeRecoveryAction::ResetHistory,
              "one decode error resets references without replacing hardware");
        recovery.picture();
        check(recovery.failed(true)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(true)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(true)==VideoDecodeRecoveryAction::ReopenHardware,
              "three consecutive hard errors permit one hardware reopen");
        recovery.picture();
        check(recovery.failed(true)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(true)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(true)==VideoDecodeRecoveryAction::UseSoftware,
              "a recovered picture does not refill the hardware reopen budget");
        check(recovery.failed(false)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(false)==VideoDecodeRecoveryAction::ResetHistory&&
              recovery.failed(false)==VideoDecodeRecoveryAction::Stop,
              "persistent failure after software fallback ends explicitly");
        struct Unit { unsigned id; bool inputLoss=false; };
        std::deque<Unit> queue;
        for(unsigned i=0;i<8;++i)queue.push_back({i});
        check(trimVideoInputs(queue,8)==1&&queue.size()==7&&queue.front().id==1&&queue.front().inputLoss,
              "dropped compressed AU marks the first survivor for reference reset");
        queue.push_back({8});
        check(trimVideoInputs(queue,8)==1&&queue.front().id==2&&queue.front().inputLoss,
              "repeated queue overflow propagates the lost-chain boundary");
        queue.pop_front();
        check(trimVideoInputs(queue,8)==0&&!queue.front().inputLoss,
              "continuous queued units do not invent extra resets");
    }
    check(reconnectableDisconnect("KickForServerShutdown"), "server service restart is recoverable");
    check(reconnectableDisconnect("connection lost"), "transport loss is recoverable");
    check(!reconnectableDisconnect("KickForStreamingClientDisconnect") && !reconnectableDisconnect("unknown server disconnect"), "unknown kicks and takeover are not retried");
    ReconnectBudget budget;
    check(budget.take() && budget.take() && budget.take() && !budget.take(), "retry budget stops at three");
    budget.decoded(1);budget.decoded(10000001);budget.disconnected();budget.decoded(400000000);
    check(!budget.take(), "a brief return or a gap cannot refill retries");
    for(int64_t t=400000000;t<=710000000;t+=5000000)budget.decoded(t);
    check(budget.take() && budget.attempts()==1, "30 seconds of continuous decoded progress refills retries");
    // --live-device-code: asks Microsoft for a sign-in code with the client id the product uses (no account
    // is involved, nothing is signed in). Confirms the first step of the real service still answers.
    if (argc > 1 && std::string(argv[1]) == "--live-device-code") {
        const auto dir = std::filesystem::temp_directory_path() / "veyra-xbox-live-check";
        Account account(makeWinHttpTransport(), dir / "unused.bin");
        try {
            const DeviceCode code = account.beginSignIn();
            std::printf("device code issued: user code %zu characters, verification %s, expires in %d s, interval %d s\n",
                        code.userCode.size(), code.verificationUri.c_str(), code.expiresInSeconds, code.intervalSeconds);
            return code.userCode.empty() ? 1 : 0;
        } catch (const ServiceError& e) {
            std::printf("service error %d: %s\n", e.status, e.body.substr(0, 300).c_str());
        } catch (const std::exception& e) {
            std::printf("error: %s\n", e.what());
        }
        return 1;
    }
    protocolTests();
    apiTests();
    profileTests();
    resilienceTests();
    accountTests();
    webrtcTests();
    webrtcProfileTests();
    lossRecoveryTests();
    std::printf("xbox tests: %d checks, %d failures\n", g_checks, g_failures);
    rtc::Cleanup();
    return g_failures == 0 ? 0 : 1;
}
