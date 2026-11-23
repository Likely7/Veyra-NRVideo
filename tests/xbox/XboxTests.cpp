// SPDX-License-Identifier: GPL-3.0-only
// Offline tests for the Xbox streaming core: the input/report formats, the session API and the sign-in
// chain against scripted service replies. None of this proves that Microsoft's services or a real
// console accept the client; it proves the client does what the reference client does and survives
// the replies it can get. The media plane (Chromium/WebView2) is covered by the gamepad mapping tests
// and the on-device probes.
#include <windows.h>

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

    // The device info must match the reference browser client identity: the service
    // picks its encoder policy from it, so any drift changes the delivered stream.
    const std::string reference = R"({"appInfo":{"env":{"clientAppId":"www.xbox.com","clientAppType":"browser","clientAppVersion":"26.1.97","clientSdkVersion":"10.3.7","httpEnvironment":"prod","sdkInstallId":""}},"dev":{"hw":{"make":"Microsoft","model":"unknown","sdktype":"web"},"os":{"name":"windows","ver":"22631.2715","platform":"desktop"},"displayInfo":{"dimensions":{"widthInPixels":1920,"heightInPixels":1080},"pixelDensity":{"dpiX":1,"dpiY":1}},"browser":{"browserName":"chrome","browserVersion":"130.0"}}})";
    check(deviceInfoForTier(QualityTier::P1080) == reference, "the default tier's device info matches the reference browser client");
    const json hq = json::parse(deviceInfoForTier(QualityTier::P1080Hq));
    check(hq["dev"]["os"]["name"] == "tizen" && hq["appInfo"]["env"]["clientAppId"] == "www.xbox.com" &&
          hq["dev"]["browser"]["browserName"] == "chrome",
          "the HQ tier keeps the browser identity and switches os.name to tizen");

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
              info["dev"]["displayInfo"]["dimensions"]["widthInPixels"] == 1920,
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
    std::printf("xbox tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
