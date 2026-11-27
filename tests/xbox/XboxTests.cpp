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
#include <future>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "veyra/xbox/Account.h"
#include "veyra/xbox/Protocol.h"
#include "veyra/xbox/StreamApi.h"
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
    std::function<void()> beforeReply;
    HttpsResponse send(const HttpsRequest& r) override {
        std::lock_guard lock(mutex);
        seen.push_back(r);
        if (script.empty()) return {599, "no scripted reply for " + r.host + r.path};
        Reply reply = script.front();
        if (reply.host != r.host || reply.path != r.path) return {598, "unexpected request " + r.method + " " + r.host + r.path + " (wanted " + reply.host + reply.path + ")"};
        script.pop_front();
        if (beforeReply) beforeReply();
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
    const uint32_t phys = PhysA | PhysMenu | PhysNexus | PhysLeftTrigger | PhysRightTrigger |
                          PhysLeftThumbX | PhysLeftThumbY | PhysRightThumbX | PhysRightThumbY;
    check(r[30] == uint8_t(phys & 0xffu) && r[31] == uint8_t((phys >> 8) & 0xffu) && r[32] == uint8_t((phys >> 16) & 0xffu) && r[33] == uint8_t((phys >> 24) & 0xffu),
          "physicality: little-endian bits for every button, trigger and stick in use");
    check(r[34] == 0 && r[35] == 0 && r[36] == 0 && r[37] == 0, "virtual physicality 0");
    check(gamepadPhysicality(GamepadFrame{}) == 0, "an idle pad reports no physicality");
    check(clientMetadataReport(0, 0)[14] == 0, "client metadata defaults to no touch points");

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

    const std::string keepalive = "/v5/sessions/home/ABC-123/keepalive";
    int renewals = 0;
    http->add(host, keepalive, 401, "{}");
    http->add(host, keepalive, 200, "{}");
    api.keepalive("ABC-123", [&] { ++renewals; return std::string("RENEWED"); });
    check(renewals == 1 && http->seen.back().headers.at("Authorization") == "Bearer RENEWED",
          "an expired keepalive lease is renewed and retried once");
    http->add(host, keepalive, 401, "{}");
    http->add(host, keepalive, 401, "{}");
    threw = false;
    try { api.keepalive("ABC-123", [&] { ++renewals; return std::string("REJECTED"); }); }
    catch (const ServiceError& e) { threw = e.status == 401; }
    check(threw && renewals == 2 && http->script.empty(), "a second 401 is reported without another renewal");
    http->add(host, keepalive, 401, "original service error");
    threw = false;
    try { api.keepalive("ABC-123", []() -> std::string { throw NetworkError("simulated renewal timeout"); }); }
    catch (const ServiceError& e) { threw = e.status == 401 && e.body == "original service error"; }
    check(threw, "a renewal timeout retains the original keepalive failure");
    http->add(host, keepalive, 404, "{}");
    threw = false;
    try { api.keepalive("ABC-123", [&] { ++renewals; return std::string("UNUSED"); }); }
    catch (const ServiceError& e) { threw = e.status == 404; }
    check(threw && renewals == 2, "an ended session is not renewed");
    api.setToken("PROACTIVE");
    http->add(host, keepalive, 200, "{}");
    api.keepalive("ABC-123");
    check(http->seen.back().headers.at("Authorization") == "Bearer PROACTIVE", "an early renewal is used on the next keepalive");

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
        http->add("user.auth.xboxlive.com", "/user/authenticate", 200, R"({"Token":"UT","NotAfter":"2032-01-01T00:00:00.0000000Z","DisplayClaims":{"xui":[{"uhs":"123"}]}})");
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

        auto renewal = std::make_shared<FakeTransport>();
        renewal->add("xsts.auth.xboxlive.com", "/xsts/authorize", 503, "{}");
        std::promise<void> requestEntered, releaseRequest;
        auto requestPending = requestEntered.get_future();
        auto release = releaseRequest.get_future();
        renewal->beforeReply = [&] { requestEntered.set_value(); release.wait(); };
        auto failedRenewal = std::async(std::launch::async, [&] {
            try { account.renewStreamingAccess("GS", *renewal); }
            catch (const ServiceError& e) { return e.status == 503; }
            return false;
        });
        const bool pending = requestPending.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
        auto signedIn = std::async(std::launch::async, [&] { return account.signedIn(); });
        const bool statusReady = signedIn.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
        releaseRequest.set_value();
        check(pending && statusReady && signedIn.get(), "UI sign-in status is available while token renewal blocks in HTTP");
        check(failedRenewal.get() && account.signedIn() && account.streamingAccess().gsToken == "GS",
              "an unavailable renewal endpoint preserves the valid lease and sign-in");
        renewal->beforeReply = {};
        renewal->add("xsts.auth.xboxlive.com", "/xsts/authorize", 200, R"({"Token":"GSSV2"})");
        renewal->add("xhome.gssv-play-prod.xboxlive.com", "/v2/login/user", 200,
                     R"({"offeringSettings":{"regions":[{"name":"EastAsia","baseUri":"https://eas.core.gssv-play-prodxhome.xboxlive.com/","isDefault":true}]},"gsToken":"GS2","durationInSeconds":14400})");
        const auto access2 = account.renewStreamingAccess("GS", *renewal);
        check(access2.gsToken == "GS2" && access2.expiresAt == now + 14400 && account.streamingAccess().gsToken == "GS2",
              "forced renewal uses the session transport and publishes the replacement lease");
        check(account.renewStreamingAccess("GS", *renewal).gsToken == "GS2" && renewal->seen.size() == 3,
              "a caller holding a stale lease reuses the already renewed one");

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
                    if (j.value("type", "") == "ReceiverCancel") cancelledTouchId = j.value("id", "");
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
    std::vector<std::string> labels, messages, control;
    std::vector<std::vector<uint8_t>> reports;
    std::string offerSdp;
    std::string cancelledTouchId;
};

std::vector<uint8_t> nal(uint8_t header, size_t size, uint8_t seed) {
    std::vector<uint8_t> out = {0, 0, 0, 1, header};
    for (size_t i = 0; i < size; ++i) out.push_back(uint8_t((i * 31 + seed) | 1));   // never a start-code pattern
    return out;
}

void videoRtpWindowTests() {
    VideoRtpReceiveWindow window;
    window.observe(42, 65534);
    const auto gap = window.observe(42, 0);
    check(gap.received == 2 && gap.pendingMissing == 1 && gap.expiredMissing == 0,
          "video RTP treats a skipped packet across 16-bit wrap as pending");
    const auto recovered = window.observe(42, 65535);
    check(recovered.pendingMissing == 0 && recovered.recoveredReorder == 1 && recovered.expiredMissing == 0,
          "a late packet can fill the wrapped sequence gap");
    check(window.observe(42, 65535).duplicate == 1, "a repeated sequence does not create another recovery");
    const auto aged = window.observe(42, 72);
    check(aged.expiredMissing == 8 && aged.pendingMissing == 63,
          "a jump past the observation window confirms only the aged-out holes");
    const auto late = window.observe(42, 2);
    check(late.outsideWindow == 1 && late.expiredMissing == 8 && late.recoveredReorder == 1,
          "an arrival older than the tracked window does not rewrite the loss history");
    check(window.observe(42, 4000).sequenceResets == 0, "one distant sequence does not create a discontinuity");
    const auto restart = window.observe(42, 4001);
    check(restart.sequenceResets == 1 && restart.pendingMissing == 0 && restart.expiredMissing == 8,
          "two consecutive packets confirm a sequence restart without counting unknown loss");
    check(window.observe(43, 500).sequenceResets == 1, "one other SSRC does not replace the current video sequence");
    check(window.observe(43, 501).sequenceResets == 2, "a new consecutive SSRC closes the old sequence epoch");
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
        check(!console.reports.empty() && console.reports[0].size() == 15 && console.reports[0][0] == 8 && console.reports[0][14] == 0,
              "first input report advertises no touch points");
    }
    check(console.waitFor([&] {
        for (const auto& s : console.messages) if (s.find("/streaming/characteristics/dimensionschanged") != std::string::npos) return true;
        return false;
    }), "the client sends its configuration messages");
    console.message->send(json{{"type", "TransactionStart"}, {"target", "/streaming/touchcontrols/showlayoutv2"},
                               {"id", "touch-layout"}, {"content", "{}"}}.dump());
    check(console.waitFor([&] { return console.cancelledTouchId == "touch-layout"; }),
          "a console touch layout is declined on the message channel");

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
    check(stats.videoRtp.received > stats.videoUnits && stats.videoRtp.pendingMissing == 0,
          "native RTP counts fragments separately from depacketized pictures");
    const auto rtpReady = [&](uint64_t packets) {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < limit) {
            if (session.stats().videoRtp.received >= packets) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    const auto sendVideo = [&] {
        ts += 1500;
        console.video->sendFrame(reinterpret_cast<const std::byte*>(p2.data()), p2.size(), rtc::FrameInfo(ts));
    };
    const uint16_t missingSequence = console.videoConfig->sequenceNumber++;
    sendVideo();
    check(rtpReady(stats.videoRtp.received + 1) && session.stats().videoRtp.pendingMissing == 1,
          "one intentionally delayed local video RTP packet appears as a pending gap");
    const uint16_t nextSequence = console.videoConfig->sequenceNumber;
    console.videoConfig->sequenceNumber = missingSequence;
    sendVideo();
    console.videoConfig->sequenceNumber = nextSequence;
    check(rtpReady(stats.videoRtp.received + 2) && session.stats().videoRtp.recoveredReorder == 1 &&
          session.stats().videoRtp.pendingMissing == 0,
          "the late SRTP packet reaches native receive diagnostics and recovers the gap");
    ++console.videoConfig->sequenceNumber;
    for (int count = 0; count < 64; ++count) sendVideo();
    check(rtpReady(stats.videoRtp.received + 66) && session.stats().videoRtp.expiredMissing == 1 &&
          session.stats().videoRtp.pendingMissing == 0,
          "one omitted loopback packet ages into the expired sequence counter");
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
    accountTests();
    videoRtpWindowTests();
    webrtcTests();
    std::printf("xbox tests: %d checks, %d failures\n", g_checks, g_failures);
    rtc::Cleanup();
    return g_failures == 0 ? 0 : 1;
}
