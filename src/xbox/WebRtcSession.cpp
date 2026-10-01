// SPDX-License-Identifier: GPL-3.0-only
// Channel set-up, handshake and messages follow Greenlight's player (unknownskl/greenlight
// packages/player/src/client/lib, MIT): channel.ts, control.ts, message.ts, input.ts, sdp.ts.
#include "veyra/xbox/WebRtcSession.h"

#include <windows.h>

#include <rtc/rtc.hpp>

#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

#include "veyra/Log.h"

namespace veyra::xbox {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

int64_t host100ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count() / 100;
}

std::string uuid4() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    uint8_t b[16];
    for (int i = 0; i < 16; i += 8) {
        const uint64_t r = rng();
        std::memcpy(b + i, &r, 8);
    }
    b[6] = uint8_t((b[6] & 0x0F) | 0x40);
    b[8] = uint8_t((b[8] & 0x3F) | 0x80);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
        s += hex[b[i] >> 4];
        s += hex[b[i] & 15];
    }
    return s;
}

std::string message(const std::string& target, const json& content) {
    return json{{"type", "Message"}, {"content", content.dump()}, {"id", uuid4()}, {"target", target}, {"cv", ""}}.dump();
}

void logOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        // VEYRA_XBOX_RTC_DEBUG=1 logs libdatachannel's ICE/DTLS/SCTP details (for field diagnosis).
        const bool debug = GetEnvironmentVariableA("VEYRA_XBOX_RTC_DEBUG", nullptr, 0) > 0;
        rtc::InitLogger(debug ? rtc::LogLevel::Verbose : rtc::LogLevel::Warning, [](rtc::LogLevel level, std::string text) {
            if (level <= rtc::LogLevel::Error) log::warn("xbox-rtc", text);
            else log::info("xbox-rtc", text);
        });
    });
}

// Removes a=candidate / a=end-of-candidates lines: candidates go through the ICE exchange.
std::string withoutCandidates(const std::string& sdp) {
    std::istringstream in(sdp);
    std::string out, line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("a=candidate:", 0) == 0 || line == "a=end-of-candidates") continue;
        out += line;
        out += "\r\n";
    }
    return out;
}

} // namespace

WebRtcSession::WebRtcSession() { logOnce(); }

WebRtcSession::~WebRtcSession() { close(); }

double WebRtcSession::nowMs() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - origin_).count();
}

std::string WebRtcSession::describeCodecs(const std::string& sdp) {
    std::istringstream in(sdp);
    std::string out, line;
    while (std::getline(in, line)) {
        if (line.rfind("a=rtpmap:", 0) == 0 || line.rfind("a=fmtp:", 0) == 0 || line.rfind("m=", 0) == 0) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out += line + "\n";
        }
    }
    return out;
}

bool WebRtcSession::start(Signaling& signaling, std::string* error, std::chrono::milliseconds timeout) {
    const auto fail = [&](const std::string& why) {
        if (error) *error = why;
        log::warn("xbox", "webrtc start failed: " + why);
        return false;
    };
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    config.maxMessageSize = 256 * 1024;
    if (!bindAddress_.empty()) config.bindAddress = bindAddress_;
    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        log::info("xbox", std::string("peer connection state ") + std::to_string(int(state)));
        if (state == rtc::PeerConnection::State::Connected) connected_ = true;
        if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed ||
            state == rtc::PeerConnection::State::Disconnected) {
            const bool wasConnected = connected_.exchange(false);
            failed_ = true;
            if (wasConnected && !closed_ && callbacks_.ended) callbacks_.ended("connection lost");
        }
        changed_.notify_all();
    });
    pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState) { changed_.notify_all(); });

    // Audio (send-receive, as the reference client; nothing is sent) and video (receive only, H.264
    // in the reference client's order: Main, Constrained Baseline, Baseline).
    rtc::Description::Audio audio("0", rtc::Description::Direction::SendRecv);
    audio.addOpusCodec(111, std::string("minptime=10;useinbandfec=1;stereo=1"));
    audio_ = pc_->addTrack(audio);
    rtc::Description::Video video("1", rtc::Description::Direction::RecvOnly);
    video.addH264Codec(102, std::string("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f"));
    video.addH264Codec(104, std::string("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"));
    video.addH264Codec(106, std::string("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f"));
    video_ = pc_->addTrack(video);

    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::LongStartSequence);
    video_->setMediaHandler(depacketizer);
    video_->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    video_->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
        ++videoUnits_;
        videoBytes_ += data.size();
        if (!callbacks_.video) return;
        std::vector<uint8_t> unit(data.size());
        std::memcpy(unit.data(), data.data(), data.size());
        callbacks_.video(std::move(unit), info.timestamp, host100ns());
    });
    audio_->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
    audio_->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    audio_->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
        ++audioPackets_;
        if (callbacks_.audio) callbacks_.audio(reinterpret_cast<const uint8_t*>(data.data()), data.size(), info.timestamp);
    });

    // The channels, in the reference client's creation order.
    const auto channel = [&](const char* label, const char* protocol) {
        rtc::DataChannelInit init;
        init.protocol = protocol;
        return pc_->createDataChannel(label, init);
    };
    chat_ = channel("chat", "chatV1");
    control_ = channel("control", "controlV1");
    input_ = channel("input", "1.0");
    message_ = channel("message", "messageV1");

    // The handshake starts once every channel is open: its acknowledgement triggers sends on control and
    // input, which would be dropped by a channel still opening.
    const auto maybeHandshake = [this] {
        if (!message_ || !control_ || !input_ || !chat_) return;
        if (!message_->isOpen() || !control_->isOpen() || !input_->isOpen() || !chat_->isOpen()) return;
        if (handshakeSent_.exchange(true)) return;
        log::info("xbox", "data channels open; sending handshake");
        sendMessageJson(json{{"type", "Handshake"}, {"version", "messageV1"}, {"id", "be0bfc6d-1e83-4c8a-90ed-fa8601c5a179"}, {"cv", "0"}}.dump());
    };
    for (const auto& dc : {chat_, control_, input_, message_}) dc->onOpen(maybeHandshake);
    message_->onMessage([this](rtc::message_variant data) {
        if (const auto* text = std::get_if<std::string>(&data)) onMessageChannel(*text);
        else if (const auto* bin = std::get_if<rtc::binary>(&data))
            onMessageChannel(std::string(reinterpret_cast<const char*>(bin->data()), bin->size()));
    });
    input_->onMessage([this](rtc::message_variant data) {
        if (const auto* bin = std::get_if<rtc::binary>(&data)) onInputChannel(reinterpret_cast<const uint8_t*>(bin->data()), bin->size());
    });
    control_->onMessage([](rtc::message_variant data) {
        if (const auto* text = std::get_if<std::string>(&data)) log::info("xbox-control", *text);
    });

    // Offer: gather every local candidate first (no trickle on this service).
    pc_->setLocalDescription(rtc::Description::Type::Offer);
    {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(5), [&] { return pc_->gatheringState() == rtc::PeerConnection::GatheringState::Complete; });
    }
    const auto local = pc_->localDescription();
    if (!local) return fail("no local description");
    const std::string offer = withoutCandidates(std::string(*local));
    std::vector<IceCandidate> candidates;
    for (const auto& c : local->candidates()) {
        std::string text = c.candidate();
        if (text.rfind("a=", 0) == 0) text = text.substr(2);
        candidates.push_back({text, c.mid(), c.mid() == "0" ? 0 : (c.mid() == "1" ? 1 : 2)});
        if (GetEnvironmentVariableA("VEYRA_XBOX_RTC_DEBUG", nullptr, 0) > 0) log::info("xbox", "local candidate [" + text + "] mid " + c.mid());
    }
    log::info("xbox", "offer codecs:\n" + describeCodecs(offer));

    std::string answer;
    try {
        answer = signaling.exchangeSdp(offer);
    } catch (const std::exception& e) {
        return fail(std::string("SDP exchange: ") + e.what());
    }
    log::info("xbox", "answer codecs:\n" + describeCodecs(answer));
    try {
        pc_->setRemoteDescription(rtc::Description(answer, rtc::Description::Type::Answer));
    } catch (const std::exception& e) {
        return fail(std::string("the console's answer was not accepted: ") + e.what());
    }
    std::vector<IceCandidate> remote;
    try {
        remote = signaling.exchangeIce(candidates);
    } catch (const std::exception& e) {
        return fail(std::string("ICE exchange: ") + e.what());
    }
    int added = 0;
    for (const auto& c : remote) {
        std::string text = c.candidate;
        if (text.rfind("a=", 0) == 0) text = text.substr(2);
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (GetEnvironmentVariableA("VEYRA_XBOX_RTC_DEBUG", nullptr, 0) > 0) log::info("xbox", "remote candidate [" + text + "] mid " + c.sdpMid);
        try {
            pc_->addRemoteCandidate(rtc::Candidate(text, c.sdpMid.empty() ? "0" : c.sdpMid));
            ++added;
        } catch (const std::exception& e) {
            log::warn("xbox", std::string("remote candidate ignored: ") + e.what() + " [" + text + "]");
        }
    }
    log::info("xbox", "local candidates " + std::to_string(candidates.size()) + ", remote " + std::to_string(added));
    if (added == 0) return fail("the console sent no usable network candidates");

    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, timeout, [&] { return connected_.load() || failed_.load(); }) || !connected_)
        return fail(failed_ ? "the connection to the console failed (network or firewall)" : "timed out connecting to the console");
    return true;
}

bool WebRtcSession::waitReady(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return handshakeDone_.load() || failed_.load(); }) && handshakeDone_;
}

void WebRtcSession::sendMessageJson(const std::string& text) {
    if (message_ && message_->isOpen()) message_->send(rtc::binary(reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data()) + text.size()));
}

void WebRtcSession::sendClientConfig() {
    sendMessageJson(message("/streaming/systemUi/configuration", {{"version", {0, 2, 0}}, {"systemUis", json::array()}}));
    sendMessageJson(message("/streaming/properties/clientappinstallidchanged", {{"clientAppInstallId", "c97d7ee0-73b2-4239-bf1d-9d805a338429"}}));
    sendMessageJson(message("/streaming/characteristics/orientationchanged", {{"orientation", 0}}));
    sendMessageJson(message("/streaming/characteristics/touchinputenabledchanged", {{"touchInputEnabled", false}}));
    sendMessageJson(message("/streaming/characteristics/clientdevicecapabilities", json::object()));
    sendMessageJson(message("/streaming/characteristics/dimensionschanged", {
        {"horizontal", 1920}, {"vertical", 1080}, {"preferredWidth", 1920}, {"preferredHeight", 1080},
        {"safeAreaLeft", 0}, {"safeAreaTop", 0}, {"safeAreaRight", 1920}, {"safeAreaBottom", 1080}, {"supportsCustomResolution", true}}));
}

void WebRtcSession::onMessageChannel(const std::string& text) {
    json j;
    try { j = json::parse(text); } catch (const json::exception&) { log::warn("xbox", "message channel: not JSON"); return; }
    const std::string type = j.value("type", "");
    if (type == "HandshakeAck") {
        log::info("xbox", "handshake acknowledged");
        // Control: authorise, then announce pad 0 (the reference client adds, removes and re-adds it).
        const auto sendControl = [this](const std::string& s) {
            if (control_ && control_->isOpen()) control_->send(rtc::binary(reinterpret_cast<const std::byte*>(s.data()), reinterpret_cast<const std::byte*>(s.data()) + s.size()));
        };
        sendControl(json{{"message", "authorizationRequest"}, {"accessKey", "4BDB3609-C1F1-4195-9B37-FEFF45DA8B8E"}}.dump());
        sendControl(json{{"message", "gamepadChanged"}, {"gamepadIndex", 0}, {"wasAdded", true}}.dump());
        sendControl(json{{"message", "gamepadChanged"}, {"gamepadIndex", 0}, {"wasAdded", false}}.dump());
        sendControl(json{{"message", "gamepadChanged"}, {"gamepadIndex", 0}, {"wasAdded", true}}.dump());
        if (input_ && input_->isOpen()) {
            const auto report = clientMetadataReport(0, nowMs(), 1);
            input_->send(reinterpret_cast<const std::byte*>(report.data()), report.size());
        }
        sendClientConfig();
        handshakeDone_ = true;
        changed_.notify_all();
        return;
    }
    if (type == "TransactionStart" || type == "Message") {
        const std::string target = j.value("target", "");
        const std::string id = j.value("id", "");
        log::info("xbox-message", target + " " + j.value("content", std::string()).substr(0, 300));
        if (target == "/streaming/sessionLifetimeManagement/serverInitiatedDisconnect") {
            sendMessageJson(json{{"type", "TransactionComplete"}, {"content", "\"\""}, {"id", id}, {"cv", ""}}.dump());
            if (callbacks_.ended) callbacks_.ended("the console ended the stream");
        } else if (target == "/streaming/systemUi/messages/ShowMessageDialog") {
            // A system dialog meant for the player; acknowledge it so the console does not wait.
            sendMessageJson(json{{"type", "TransactionComplete"}, {"content", json{{"Result", 0}}.dump()}, {"id", id}, {"cv", ""}}.dump());
        } else if (type == "TransactionStart") {
            sendMessageJson(json{{"type", "TransactionComplete"}, {"content", "\"\""}, {"id", id}, {"cv", ""}}.dump());
        }
        return;
    }
    log::info("xbox-message", "unhandled " + type);
}

void WebRtcSession::onInputChannel(const uint8_t* data, size_t size) {
    if (size < 2) return;
    if (auto v = parseVibration(data, size)) {
        if (callbacks_.vibration) callbacks_.vibration(*v);
        return;
    }
    uint32_t w = 0, h = 0;
    if (parseServerMetadata(data, size, &w, &h)) {
        log::info("xbox", "server video " + std::to_string(w) + "x" + std::to_string(h));
        if (callbacks_.serverVideoSize) callbacks_.serverVideoSize(w, h);
    }
}

void WebRtcSession::sendGamepad(const GamepadFrame& frame) {
    if (!handshakeDone_ || !input_ || !input_->isOpen()) return;
    const auto report = gamepadReport(++inputSequence_, nowMs(), {frame});
    input_->send(reinterpret_cast<const std::byte*>(report.data()), report.size());
}

void WebRtcSession::requestKeyframe() {
    ++keyframeRequests_;
    if (video_) video_->requestKeyframe();
    if (control_ && control_->isOpen() && handshakeDone_) {
        const std::string s = json{{"message", "videoKeyframeRequested"}, {"ifrRequested", true}}.dump();
        control_->send(rtc::binary(reinterpret_cast<const std::byte*>(s.data()), reinterpret_cast<const std::byte*>(s.data()) + s.size()));
    }
}

void WebRtcSession::close() {
    if (closed_.exchange(true)) return;
    if (pc_) {
        try { pc_->close(); } catch (...) {}
    }
    changed_.notify_all();
    chat_.reset(); control_.reset(); input_.reset(); message_.reset();
    audio_.reset(); video_.reset();
    pc_.reset();
}

WebRtcStats WebRtcSession::stats() const {
    WebRtcStats s;
    s.videoUnits = videoUnits_.load();
    s.audioPackets = audioPackets_.load();
    s.keyframeRequests = keyframeRequests_.load();
    s.videoBytes = videoBytes_.load();
    s.handshakeDone = handshakeDone_.load();
    if (const auto pc = pc_) {
        s.bytesReceived = pc->bytesReceived();
        if (const auto rtt = pc->rtt()) s.rttMs = double(rtt->count());
    }
    return s;
}

} // namespace veyra::xbox
