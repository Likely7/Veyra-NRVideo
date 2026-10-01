// SPDX-License-Identifier: GPL-3.0-only
// Offline tests for the Moonlight host protocol layer. No real host is involved:
// a mock host on the loopback interface implements the host side of the
// GameStream pairing handshake, so the client's cryptography, TLS certificate
// pinning, HTTP parsing and every pairing failure path are exercised end to end.
// The mock is written from the same protocol description as the client, so it
// proves the client is self-consistent and safe against bad answers; interop with
// a real Sunshine host is verified separately on real machines.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "veyra/moonlight/Client.h"
#include "veyra/moonlight/Crypto.h"
#include "veyra/moonlight/Discovery.h"
#include "veyra/moonlight/Http.h"
#include "veyra/moonlight/IdentityStore.h"
#include "veyra/moonlight/Pairing.h"
#include "veyra/moonlight/StreamConfig.h"
#include "veyra/moonlight/InputMap.h"
#include "veyra/moonlight/InputRouter.h"
#include "veyra/moonlight/Xml.h"
#include "MockHost.h"

using namespace veyra::moonlight;
using namespace veyra::moonlight::mock;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("FAIL %s\n", what); }
}

// ---------------------------------------------------------------- tests

void xmlTests() {
    const auto root = xml::parse(kServerInfoXml);
    check(root.has_value(), "serverinfo parses");
    const auto info = ServerClient::parseServerInfo(*root);
    check(info.hostname == "DESKTOP-TEST", "hostname");
    check(info.appVersionMajor() == 7, "appversion major");
    check(info.httpsPort == 48000, "https port from serverinfo");
    check(info.externalPort == 47999, "external port");
    check(info.serverCodecModeSupport == 197377, "codec support bits");
    check((info.serverCodecModeSupport & kServerAv1Main10) != 0, "AV1 10-bit advertised");
    check(info.paired, "pair status");
    check(!info.nvidiaServerSoftware, "sunshine is not GFE");
    check(info.currentGame == 7, "busy server reports the running game");
    check(info.displayModes.size() == 2 && info.displayModes[0].width == 1920 && info.displayModes[1].width == 3840, "display modes sorted ascending");
    check(info.gfeVersion == "3.23.0.74", "gfe version");

    // An idle server must report no current game even if the tag is set (GFE 2.8+ behaviour).
    std::string idle = kServerInfoXml;
    idle.replace(idle.find("SUNSHINE_SERVER_BUSY"), 20, "SUNSHINE_SERVER_FREE");
    check(ServerClient::parseServerInfo(*xml::parse(idle)).currentGame == 0, "idle server has no current game");

    const auto apps = ServerClient::parseAppList(*xml::parse(kAppListXml));
    check(apps.size() == 3, "three apps");
    check(apps[0].name == "Desktop" && apps[0].id == 881448767 && apps[0].hdrSupported, "desktop app");
    check(apps[1].name == "Tom & Jerry <Deluxe> \xE4\xB8\xAD", "entities and numeric character reference decoded");
    check(apps[2].name.empty() && apps[2].id == 13, "empty AppTitle is an empty name");

    check(!xml::parse("<root><a></b></root>").has_value(), "mismatched tags rejected");
    check(!xml::parse("").has_value(), "empty document rejected");
    check(!xml::parse("<root>").has_value(), "unterminated document rejected");
    std::string deep;
    for (int i = 0; i < 64; ++i) deep += "<a>";
    for (int i = 0; i < 64; ++i) deep += "</a>";
    check(!xml::parse(deep).has_value(), "excessive nesting rejected");

    bool threw = false;
    try { ServerClient::checkedRoot("<root status_code=\"401\" status_message=\"denied\"/>"); }
    catch (const StatusError& e) { threw = e.status == 401; }
    check(threw, "non-200 status raises StatusError with the code");
    threw = false;
    try { ServerClient::checkedRoot("<root status_code=\"4294967295\" status_message=\"Invalid\"/>"); }
    catch (const StatusError& e) { threw = e.status == 418; }
    check(threw, "GFE audio capture error is translated");
    threw = false;
    try { ServerClient::checkedRoot("not xml"); }
    catch (const StatusError& e) { threw = e.status == -1; }
    check(threw, "malformed body raises StatusError(-1)");

    check(xml::fromHex("0aFf").value() == std::string("\x0a\xff", 2), "hex decoding");
    check(!xml::fromHex("abc").has_value() && !xml::fromHex("zz").has_value(), "invalid hex rejected");
}

void cryptoTests() {
    // FIPS-197 appendix C.1 known answer.
    std::string key, plain;
    for (int i = 0; i < 16; ++i) { key.push_back(char(i)); plain.push_back(char(i * 0x11)); }
    const std::string cipher = crypto::aesEcbEncrypt(plain, key);
    check(xml::toHex(cipher) == "69c4e0d86a7b0430d8cdb78070b4c55a", "AES-128 known answer");
    check(crypto::aesEcbDecrypt(cipher, key) == plain, "AES round trip");
    check(crypto::aesEcbEncrypt("short", key).empty(), "AES rejects non block sized input");

    check(xml::toHex(crypto::digest("abc", true)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known answer");
    check(xml::toHex(crypto::digest("abc", false)) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 known answer");

    const Identity id = createIdentity();
    check(id.valid(), "new identity is valid");
    check(id.uniqueId.size() == 16, "unique id length");
    const std::string signature = crypto::sign(id, "message");
    check(!signature.empty() && crypto::verify("message", signature, id.certPem), "sign/verify");
    check(!crypto::verify("other", signature, id.certPem), "verify rejects another message");
    check(!crypto::verify("message", signature, createIdentity().certPem), "verify rejects another certificate");
    check(crypto::certificateSignature(id.certPem).size() == 256, "certificate signature is the RSA-2048 block");
    check(!crypto::certificateDer(id.certPem).empty(), "certificate DER");

    Identity broken = id;
    broken.keyPem = createIdentity().keyPem;
    check(!broken.valid(), "a key that does not belong to the certificate is invalid");
    check(crypto::randomBytes(16) != crypto::randomBytes(16), "random bytes differ");
    check(generatePin().size() == 4, "pin has four digits");
}

void streamConfigTests() {
    // moonlight-qt's table: 1080p60 = 20 Mbps, 4K60 = 80 Mbps, 720p30 = 5 Mbps.
    check(defaultBitrateKbps(1920, 1080, 60) == 20000, "1080p60 default bitrate");
    check(defaultBitrateKbps(3840, 2160, 60) == 80000, "4K60 default bitrate");
    check(defaultBitrateKbps(1280, 720, 30) == 5000, "720p30 default bitrate");
    check(defaultBitrateKbps(3840, 2160, 30) == 40000, "4K30 default bitrate");
    check(defaultBitrateKbps(3840, 2160, 120) == 113000, "frame rate factor grows with the square root above 60 fps");
    check(defaultBitrateKbps(1920, 1080, 60, true) == 40000, "4:4:4 doubles the bitrate");
    check(defaultBitrateKbps(2560, 1440, 60) > defaultBitrateKbps(1920, 1080, 60) && defaultBitrateKbps(2560, 1440, 60) < defaultBitrateKbps(3840, 2160, 60), "bitrate grows with resolution");
    check(defaultBitrateKbps(320, 200, 30) == 1000, "never below the smallest table entry");
    check(defaultBitrateKbps(7680, 4320, 60) == 80000, "never above the largest table entry");

    const int sunshine = 197377;   // H.264, HEVC, HEVC Main10, AV1 Main8, AV1 Main10
    check(chooseVideoFormats(CodecChoice::Auto, false, sunshine, false) == (kFormatH264 | kFormatH265), "auto SDR offers H.264 and HEVC, not AV1 without hardware decode");
    check(chooseVideoFormats(CodecChoice::Auto, false, sunshine, true) == (kFormatH264 | kFormatH265 | kFormatAv1Main8), "auto SDR adds AV1 when it decodes in hardware");
    check(chooseVideoFormats(CodecChoice::Hevc, false, sunshine, false) == kFormatH265, "HEVC only");
    check(chooseVideoFormats(CodecChoice::Auto, true, sunshine, false) == kFormatH265Main10, "auto HDR offers HEVC Main10 only");
    check(chooseVideoFormats(CodecChoice::Auto, true, sunshine, true) == (kFormatH265Main10 | kFormatAv1Main10), "auto HDR adds AV1 Main10 with hardware decode");
    check(chooseVideoFormats(CodecChoice::H264, true, sunshine, true) == 0, "HDR cannot be carried by H.264");
    check(chooseVideoFormats(CodecChoice::Hevc, false, kServerH264, false) == 0, "a host without HEVC cannot give HEVC");
    check(chooseVideoFormats(CodecChoice::Auto, false, kServerH264, true) == kFormatH264, "an H.264-only host still streams");
    check((chooseVideoFormats(CodecChoice::Av1, true, sunshine, false) & kFormatMask10Bit) == kFormatAv1Main10, "explicit AV1 HDR");
}

void storeTests() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const auto file = std::filesystem::path(temp) / (L"veyra-moonlight-test-" + std::to_wstring(GetCurrentProcessId()) + L".dat");
    std::filesystem::remove(file);
    bool created = false;
    const Identity first = loadOrCreateIdentity(file, &created);
    check(created && first.valid(), "identity created on first use");
    const Identity second = loadOrCreateIdentity(file, &created);
    check(!created && second.certPem == first.certPem && second.uniqueId == first.uniqueId, "identity reloaded unchanged");
    // The key must not be readable in the file.
    std::ifstream in(file, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(raw.find("PRIVATE KEY") == std::string::npos && raw.find(first.uniqueId) == std::string::npos, "identity file is encrypted");
    in.close();
    {   // A damaged file is not an identity.
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "garbage";
    }
    check(!loadIdentity(file).has_value(), "damaged file is rejected");
    std::filesystem::remove(file);
}

ServerClient makeClient(const Identity& id, const MockHost& host, const std::string& pinned = {}) {
    return ServerClient(id, HostAddress{"127.0.0.1", host.httpPort}, pinned, host.httpsPort, true);
}

void protocolTests() {
    const Identity client = createIdentity();

    try {   // --- pairing with the right PIN
        MockHost host("4242");
        ServerClient http = makeClient(client, host);
        ServerInfo info = http.serverInfo();          // plain HTTP, chunked answer
        check(info.hostname == "DESKTOP-TEST" && !info.paired, "serverinfo over HTTP before pairing");
        check(http.httpsPort() == host.httpsPort, "HTTPS port learnt from serverinfo");

        std::atomic<bool> cancel{false};
        host.enterPin();
        const PairOutcome outcome = pairWithHost(http, client, info, "4242", &cancel);
        check(outcome.result == PairResult::Paired, "pairing with the right PIN succeeds");
        check(outcome.serverCertPem == host.identity.certPem, "the host certificate is returned for pinning");

        ServerClient paired = makeClient(client, host, outcome.serverCertPem);
        paired.setHttpsPort(host.httpsPort);
        info = paired.serverInfo();                    // HTTPS with the pinned certificate
        check(info.paired, "serverinfo over pinned HTTPS reports paired");
        const auto apps = paired.appList();
        check(apps.size() == 3, "applist over pinned HTTPS");
        check(paired.boxArt(12) == "\x89PNG-MOCK", "box art bytes");

        LaunchRequest launch;
        launch.appId = 881448767; launch.width = 3840; launch.height = 2160; launch.fps = 60;
        for (size_t i = 0; i < 16; ++i) launch.remoteInputKey[i] = uint8_t(i);
        launch.remoteInputKeyId = {0x80, 0x00, 0x00, 0x01};
        launch.hdr = true; launch.surroundAudioInfo = 196610; launch.gamepadMask = 3; launch.extraQuery = "&corever=1";
        const std::string url = paired.launch("launch", launch, false);
        check(url == "rtsp://127.0.0.1:48010", "launch returns the RTSP session URL");
        const std::string sent = host.launchQuery();
        check(sent.find("&mode=3840x2160x60") != std::string::npos, "mode is WIDTHxHEIGHTxFPS");
        check(sent.find("&rikey=000102030405060708090a0b0c0d0e0f") != std::string::npos, "remote input key is hex encoded");
        check(sent.find("&rikeyid=-2147483647") != std::string::npos, "remote input key id is the big endian int32 of the IV prefix");
        check(sent.find("&hdrMode=1") != std::string::npos, "HDR mode requested for a 10-bit format");
        check(sent.find("&corever=1") != std::string::npos, "library query parameters appended");
        check(sent.find("&uniqueid=" + client.uniqueId) != std::string::npos, "the true unique id is sent to Sunshine");
        launch.fps = 120;
        paired.launch("resume", launch, true);
        check(host.launchQuery().find("&mode=3840x2160x0") != std::string::npos, "GFE gets fps 0 above 60 (SOPS workaround)");

        // The host forgets us: HTTPS no longer works, serverinfo falls back to HTTP and says so.
        host.forgetPairing();
        info = paired.serverInfo();
        check(!info.paired, "after the host forgets the client, serverinfo falls back to HTTP and reports not paired");

        // A different server certificate than the pinned one is refused before anything is sent.
        host.presentDifferentCertificate();
        bool mismatch = false;
        try { paired.request(true, "applist", "", 3000); }
        catch (const TransportError& e) { mismatch = e.kind == TransportError::Kind::Tls; }
        check(mismatch, "a server certificate that is not the pinned one is rejected");
    } catch (const std::exception& e) { ++g_failures; std::printf("FAIL exception: %s\n", e.what()); }

    try {   // --- wrong PIN
        MockHost host("1111");
        ServerClient http = makeClient(client, host);
        const ServerInfo info = http.serverInfo();
        host.enterPin();
        std::atomic<bool> cancel{false};
        const PairOutcome outcome = pairWithHost(http, client, info, "2222", &cancel);
        check(outcome.result == PairResult::PinWrong, "a wrong PIN is reported as PinWrong");
        check(host.unpairCalls >= 1, "a failed pairing tells the host to forget it");
    } catch (const std::exception& e) { ++g_failures; std::printf("FAIL exception: %s\n", e.what()); }

    try {   // --- the host is busy with another client
        MockHost host("1111");
        host.busyPairing = true;
        ServerClient http = makeClient(client, host);
        const ServerInfo info = http.serverInfo();
        std::atomic<bool> cancel{false};
        const PairOutcome outcome = pairWithHost(http, client, info, "1111", &cancel);
        check(outcome.result == PairResult::AlreadyInProgress, "a busy host is reported as AlreadyInProgress");
    } catch (const std::exception& e) { ++g_failures; std::printf("FAIL exception: %s\n", e.what()); }

    try {   // --- cancelled while waiting for the PIN to be typed on the host
        MockHost host("1111");
        ServerClient http = makeClient(client, host);
        const ServerInfo info = http.serverInfo();
        std::atomic<bool> cancel{false};
        std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); cancel = true; });
        const auto begin = std::chrono::steady_clock::now();
        const PairOutcome outcome = pairWithHost(http, client, info, "1111", &cancel);
        canceller.join();
        check(outcome.result == PairResult::Cancelled, "cancelling while waiting for the PIN returns Cancelled");
        check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(3), "cancel is prompt");
    } catch (const std::exception& e) { ++g_failures; std::printf("FAIL exception: %s\n", e.what()); }

    try {   // --- transport failures
        ServerClient http(client, HostAddress{"127.0.0.1", 1}, {}, 2, true);   // nothing listens on port 1
        bool refused = false;
        try { http.serverInfo(true); } catch (const TransportError& e) { refused = e.kind == TransportError::Kind::Connect || e.kind == TransportError::Kind::Timeout; }
        check(refused, "an unreachable host is a Connect or Timeout error (Windows can take ~2 s to report a refused loopback connect)");

        HttpClient bare(&client, {});
        bool needsPin = false;
        try { bare.get(HttpTarget{"127.0.0.1", 1, true}, "/x", HttpOptions{}); }
        catch (const TransportError& e) { needsPin = e.kind == TransportError::Kind::Tls; }
        check(needsPin, "HTTPS without a pinned certificate is refused");
    } catch (const std::exception& e) { ++g_failures; std::printf("FAIL exception: %s\n", e.what()); }
}

struct RecordingSink final : InputSink {
    struct Key { uint32_t vk, scan; bool extended, down; };
    std::vector<Key> keys;
    std::vector<std::pair<int, bool>> buttons;
    std::vector<std::pair<int, int>> moves, scrolls;
    void key(uint32_t vk, uint32_t scan, bool extended, bool down) override { keys.push_back({vk, scan, extended, down}); }
    void mouseMove(int dx, int dy) override { moves.emplace_back(dx, dy); }
    void mouseButton(int button, bool down) override { buttons.emplace_back(button, down); }
    void scroll(int delta, bool horizontal) override { scrolls.emplace_back(delta, horizontal ? 1 : 0); }
};

LPARAM keyParam(unsigned scan, bool extended, bool up = false) {
    return LPARAM(1u | (scan << 16) | (extended ? 1u << 24 : 0u) | (up ? (3u << 30) : 0u));
}

void routerTests() {
    RecordingSink sink;
    InputRouter router(sink);
    check(router.message(WM_KEYDOWN, 'A', keyParam(0x1E, false)) && sink.keys.size() == 1 && sink.keys[0].vk == 'A' && sink.keys[0].down, "a key press reaches the sink and is consumed");
    check(router.message(WM_KEYUP, 'A', keyParam(0x1E, false, true)) && sink.keys.size() == 2 && !sink.keys[1].down, "and its release");
    check(router.message(WM_CHAR, 'a', 0) && sink.keys.size() == 2, "characters are consumed but not forwarded");
    check(router.message(WM_SYSCOMMAND, SC_KEYMENU, 0) && !router.message(WM_SYSCOMMAND, SC_CLOSE, 0), "the Alt menu is swallowed, other system commands are not");
    check(!router.message(WM_PAINT, 0, 0) && !router.message(WM_SIZE, 0, 0), "unrelated messages pass through");

    check(router.message(WM_LBUTTONDOWN, MK_LBUTTON, 0) && router.message(WM_LBUTTONUP, 0, 0) && sink.buttons.size() == 2 && sink.buttons[0] == std::make_pair(1, true) && sink.buttons[1] == std::make_pair(1, false), "left button");
    router.message(WM_RBUTTONDOWN, MK_RBUTTON, 0);
    router.message(WM_MBUTTONDOWN, MK_MBUTTON, 0);
    router.message(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON1), 0);
    router.message(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), 0);
    check(sink.buttons[2].first == 3 && sink.buttons[3].first == 2 && sink.buttons[4].first == 4 && sink.buttons[5].first == 5, "right, middle and the two side buttons");
    router.message(WM_LBUTTONDBLCLK, MK_LBUTTON, 0);
    check(sink.buttons.back() == std::make_pair(1, true), "a double click is another press");
    check(router.message(WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)) && sink.moves.empty(), "window mouse moves are consumed; movement comes from raw input");
    router.rawMouse(7, -3);
    check(sink.moves.size() == 1 && sink.moves[0] == std::make_pair(7, -3), "raw movement is forwarded");
    router.message(WM_MOUSEWHEEL, MAKEWPARAM(0, WORD(short(-240))), 0);
    router.message(WM_MOUSEHWHEEL, MAKEWPARAM(0, WORD(short(120))), 0);
    check(sink.scrolls.size() == 2 && sink.scrolls[0] == std::make_pair(-240, 0) && sink.scrolls[1] == std::make_pair(120, 1), "wheel and horizontal wheel");

    // Reserved hotkey: Ctrl+Alt+Shift+Z is taken by the app; the modifiers still reached the host
    // (the session releases them when the capture ends), the Z never does.
    RecordingSink other;
    InputRouter hotkeys(other);
    hotkeys.message(WM_KEYDOWN, 0x11, keyParam(0x1D, false));
    hotkeys.message(WM_SYSKEYDOWN, 0x12, keyParam(0x38, false));
    hotkeys.message(WM_KEYDOWN, 0x10, keyParam(0x2A, false));
    check(hotkeys.takeReserved() == Reserved::None, "modifiers alone are not a hotkey");
    check(hotkeys.message(WM_KEYDOWN, 'Z', keyParam(0x2C, false)) && hotkeys.takeReserved() == Reserved::ReleaseCapture, "Ctrl+Alt+Shift+Z is reported");
    check(hotkeys.takeReserved() == Reserved::None, "and only once");
    const size_t before = other.keys.size();
    hotkeys.message(WM_KEYUP, 'Z', keyParam(0x2C, false, true));
    check(other.keys.size() == before, "the hotkey's key-up is not forwarded either");
    bool zSent = false;
    for (const auto& k : other.keys) zSent = zSent || k.vk == 'Z';
    check(!zSent, "the hotkey key itself never reaches the host");
    hotkeys.reset();
    hotkeys.message(WM_KEYDOWN, 'Z', keyParam(0x2C, false));
    check(other.keys.back().vk == 'Z' && hotkeys.takeReserved() == Reserved::None, "after a reset Z is an ordinary key");
}

void discoveryTests() {
    // There is no Sunshine host here, so this only proves that the DNS-SD browse starts and stops
    // cleanly (also repeatedly, and with a late callback after stop), not that a host is found.
    std::atomic<int> found{0};
    Discovery discovery;
    const bool started = discovery.start([&](const DiscoveredHost&) { ++found; });
    check(started == discovery.running(), "running() follows start()");
    if (started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        discovery.stop();
        check(!discovery.running(), "stop() ends the browse");
        check(discovery.start([&](const DiscoveredHost&) { ++found; }), "a second start works");
        discovery.stop();
        discovery.stop();
        check(true, "stopping twice is harmless");
    } else {
        std::printf("note: DNS-SD browse is not available on this machine\n");
    }
}

void inputTests() {
    using veyra::remoteplay::ControllerState;
    {
        ControllerState s;
        s.inputActive = true;
        s.buttons = ControllerState::Cross | ControllerState::Triangle | ControllerState::L1 | ControllerState::Options | ControllerState::Share | ControllerState::Down;
        s.l2 = 200; s.r2 = 7; s.leftX = 1000; s.leftY = 32767; s.rightX = -5; s.rightY = -32768;
        const PadFrame f = mapPad(s);
        check(f.buttons == (kPadA | kPadY | kPadLeftBumper | kPadStart | kPadBack | kPadDown), "pad buttons keep the positional layout");
        check(f.leftTrigger == 200 && f.rightTrigger == 7, "triggers pass through");
        check(f.leftX == 1000 && f.leftY == -32767, "stick Y is flipped (SDL down-positive to host up-positive)");
        check(f.rightX == -5 && f.rightY == 32767, "the most negative stick value flips without overflow");
        s.inputActive = false;
        check(mapPad(s) == PadFrame{}, "an inactive pad (focus or device lost) releases everything");
        check(flipAxis(0) == 0 && flipAxis(INT16_MAX) == -INT16_MAX, "flipAxis is symmetric");
    }
    {
        check(hostKey('A', 0x1E, false).code == hostCode('A'), "letters keep their virtual-key code with bit 15 set");
        check(hostKey(0x10, 0x2A, false).code == hostCode(0xA0) && hostKey(0x10, 0x36, false).code == hostCode(0xA1), "VK_SHIFT resolves by scan code to left or right");
        check(hostKey(0x11, 0x1D, false).code == hostCode(0xA2) && hostKey(0x11, 0x1D, true).code == hostCode(0xA3), "VK_CONTROL resolves by the extended flag");
        check(hostKey(0x12, 0x38, false).code == hostCode(0xA4) && hostKey(0x12, 0x38, true).code == hostCode(0xA5), "VK_MENU resolves by the extended flag (AltGr is right Alt)");
        check(hostKey(0x10, 0x2A, false).modifier == kModShift && hostKey(0x5B, 0x5B, true).modifier == kModMeta && hostKey('A', 0x1E, false).modifier == 0, "modifier bits");
        check(hostKey(0x26, 0x48, false).code == hostCode(0x68), "keypad Up with Num Lock off is sent as Numpad8");
        check(hostKey(0x26, 0x48, true).code == hostCode(0x26), "the arrow key stays an arrow key");
        check(hostKey(0x2E, 0x53, false).code == hostCode(0x6E), "keypad Delete with Num Lock off is sent as the decimal key");
    }
    {
        HeldKeys held;
        const HostKey shift = hostKey(0x10, 0x2A, false), a = hostKey('A', 0x1E, false);
        check(held.press(shift.code, shift.modifier) && held.modifiers() == kModShift, "shift goes down");
        check(held.press(a.code, 0) && held.count() == 2, "a second key goes down");
        check(!held.press(a.code, 0) && held.count() == 2, "auto-repeat of a held key is not a new press");
        check(held.release(shift.code) && held.modifiers() == 0, "releasing shift clears the modifier");
        check(!held.release(shift.code), "releasing a key that is not held does nothing");
        held.clear();
        check(held.count() == 0 && held.modifiers() == 0, "clear drops everything");
    }
    {
        constexpr uint8_t all = kModShift | kModCtrl | kModAlt;
        check(reservedHotkey('Z', all) == Reserved::ReleaseCapture, "Ctrl+Alt+Shift+Z releases the capture");
        check(reservedHotkey('Q', all) == Reserved::Quit, "Ctrl+Alt+Shift+Q quits");
        check(reservedHotkey('S', all) == Reserved::ToggleStats, "Ctrl+Alt+Shift+S toggles the stats");
        check(reservedHotkey('Z', kModCtrl | kModAlt) == Reserved::None, "Ctrl+Alt+Z alone is forwarded to the host");
        check(reservedHotkey('A', all) == Reserved::None, "other keys are forwarded");
        check(clampDelta(100000) == 32767 && clampDelta(-100000) == -32767 && clampDelta(-3) == -3, "mouse deltas are clamped");
    }
}

} // namespace

int main() {
    xmlTests();
    cryptoTests();
    storeTests();
    streamConfigTests();
    inputTests();
    routerTests();
    discoveryTests();
    protocolTests();
    std::printf("moonlight protocol tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
