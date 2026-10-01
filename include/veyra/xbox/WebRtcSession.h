// SPDX-License-Identifier: GPL-3.0-only
// The media side of an Xbox home stream, natively with libdatachannel (no browser): one Opus audio
// track, one H.264 video track (received as Annex-B access units for the shared hardware decoder) and the
// four data channels the console expects (chat, control, input, message). The signalling is an interface,
// so a test can connect two local peers; the real one goes through StreamApi.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "veyra/xbox/Protocol.h"
#include "veyra/xbox/StreamApi.h"

namespace rtc {
class PeerConnection;
class DataChannel;
class Track;
}

namespace veyra::xbox {

class Signaling {
public:
    virtual ~Signaling() = default;
    virtual std::string exchangeSdp(const std::string& offerSdp) = 0;
    virtual std::vector<IceCandidate> exchangeIce(const std::vector<IceCandidate>& local) = 0;
};

struct WebRtcCallbacks {
    // One access unit, Annex-B, with the RTP timestamp (90 kHz) and the local arrival time (100 ns ticks).
    std::function<void(std::vector<uint8_t>&& unit, uint32_t rtpTimestamp, int64_t arrival100ns)> video;
    // One Opus packet, 48 kHz timestamp.
    std::function<void(const uint8_t* data, size_t size, uint32_t rtpTimestamp)> audio;
    std::function<void(const Vibration&)> vibration;
    std::function<void(uint32_t width, uint32_t height)> serverVideoSize;
    // The console ended the stream (serverInitiatedDisconnect) or the connection dropped.
    std::function<void(const std::string& reason)> ended;
};

struct WebRtcStats {
    uint64_t bytesReceived = 0;      // data channels (libdatachannel's count)
    uint64_t videoBytes = 0;         // access units handed to the decoder
    uint64_t videoUnits = 0, audioPackets = 0, keyframeRequests = 0;
    double rttMs = 0;
    bool handshakeDone = false;
};

class WebRtcSession {
public:
    WebRtcSession();
    ~WebRtcSession();
    WebRtcSession(const WebRtcSession&) = delete;
    WebRtcSession& operator=(const WebRtcSession&) = delete;

    void setCallbacks(WebRtcCallbacks callbacks) { callbacks_ = std::move(callbacks); }
    // Tests: gather candidates on this address only (e.g. "127.0.0.1"). Empty: every interface.
    void setBindAddress(std::string address) { bindAddress_ = std::move(address); }

    // Offer, signalling, ICE; returns once connected, false (with `error`) on failure or timeout.
    bool start(Signaling& signaling, std::string* error, std::chrono::milliseconds timeout = std::chrono::seconds(20));
    // True once the console acknowledged the message-channel handshake (input is accepted from then on).
    bool ready() const { return handshakeDone_.load(); }
    bool waitReady(std::chrono::milliseconds timeout);

    void sendGamepad(const GamepadFrame& frame);   // sent when it changes, and at least every 33 ms by the caller
    void requestKeyframe();
    void close();

    WebRtcStats stats() const;

    // The SDP offer this session would send (built without connecting), for tests and diagnostics.
    static std::string describeCodecs(const std::string& sdp);

private:
    void onMessageChannel(const std::string& text);
    void onInputChannel(const uint8_t* data, size_t size);
    void sendMessageJson(const std::string& text);
    void sendClientConfig();
    double nowMs() const;

    WebRtcCallbacks callbacks_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> audio_, video_;
    std::shared_ptr<rtc::DataChannel> chat_, control_, input_, message_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> connected_{false}, failed_{false}, closed_{false}, handshakeDone_{false}, handshakeSent_{false};
    std::atomic<uint32_t> inputSequence_{0};
    std::atomic<uint64_t> videoUnits_{0}, audioPackets_{0}, keyframeRequests_{0}, videoBytes_{0};
    std::chrono::steady_clock::time_point origin_ = std::chrono::steady_clock::now();
    std::string failure_;
    std::string bindAddress_;
};

} // namespace veyra::xbox
