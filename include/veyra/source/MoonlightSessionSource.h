// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MoonlightSessionSource: one streaming session from a Sunshine / GameStream
// host, presented to the engine through the same source contract as capture
// cards and PS5 Remote Play. moonlight-common-c owns the network protocol
// (RTSP, RTP, FEC, ENet control); this class launches the app on the host,
// decodes the video with the shared hardware decoder path and plays the audio.
// The effect chain (NR / SR / frame generation / colour) is not part of it.
//
// moonlight-common-c keeps process-global state, so only one session can exist
// at a time; connect() on a second instance fails while one is active.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "veyra/moonlight/Client.h"
#include "veyra/moonlight/InputMap.h"
#include "veyra/moonlight/StreamConfig.h"
#include "veyra/remoteplay/Types.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/IFrameSource.h"

struct AVFrame;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::source {

struct MoonlightStreamOptions {
    int width = 1920, height = 1080, fps = 60;
    int bitrateKbps = 0;                          // 0: the default for the resolution and frame rate
    moonlight::CodecChoice codec = moonlight::CodecChoice::Auto;
    bool hdr = false;                             // offer only 10-bit formats
    int audioChannels = 2;                        // 2, 6 or 8
    bool sops = false;                            // let the host change its own resolution
    bool localAudio = false;                      // keep playing audio on the host as well
    int displayRefreshHz = 0;                     // told to the host when known
    int gamepadMask = 0;                          // bit per connected controller
    bool av1HardwareDecode = false;               // lets "automatic" offer AV1
};

struct MoonlightConnectDesc {
    moonlight::Identity identity;                 // wiped as soon as the app has been launched
    moonlight::HostAddress address;
    uint16_t httpsPort = moonlight::kDefaultHttpsPort;
    std::string serverCertPem;                    // pinned at pairing
    moonlight::ServerInfo host;                   // a fresh serverinfo answer
    int appId = 0;
    bool resume = false;                          // the app already runs on the host
    MoonlightStreamOptions options;
    // Set by the engine; shared ownership keeps the adapter and queue alive until
    // the decoder owner has stopped, including after a failed connect.
    std::shared_ptr<ID3D12Device> decodeDevice;
    std::shared_ptr<ID3D12CommandQueue> decodeQueue;
};

struct MoonlightStats {
    enum class State : uint8_t { Idle, Launching, Connecting, Streaming, Ended, Failed };
    State state = State::Idle;
    std::wstring message;                         // what to tell the user when Ended or Failed
    int errorCode = 0;                            // moonlight-common-c code, 0 = none
    uint32_t width = 0, height = 0;
    double fps = 0;                               // requested
    std::string codec;                            // "H.264", "HEVC", "HEVC 10-bit", "AV1"...
    bool hdr = false;
    bool hardwareDecode = false;
    bool encrypted = false;
    uint64_t units = 0, decoded = 0, dropped = 0, decodeErrors = 0, idrRequests = 0;
    double receivedFps = 0, decodedFps = 0;
    double videoMbps = 0;                         // video payload actually received
    double hostLatencyMs = 0;                     // average host capture+encode, reported by Sunshine
    double receiveMs = 0;                         // first packet to complete frame
    double queueMs = 0;                           // complete frame to decoder start
    double decodeMs = 0;
    double rttMs = 0, rttVarianceMs = 0;
    uint32_t videoPackets = 0, fecPackets = 0, fecRecovered = 0, fecFailed = 0, outOfSequence = 0;
};

class MoonlightSessionSource final : public IFrameSource {
public:
    MoonlightSessionSource();
    ~MoonlightSessionSource() override;

    MoonlightSessionSource(const MoonlightSessionSource&) = delete;
    MoonlightSessionSource& operator=(const MoonlightSessionSource&) = delete;

    // Launches (or resumes) the app, connects and starts decoding. Blocks until
    // the stream is up or failed; false means see stats().message.
    bool connect(MoonlightConnectDesc desc);

    bool open(const SourceOpenDesc&) override { return false; }   // credentials do not fit; use connect()
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket&, const AVFrame**) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;

    // Ends the session. `quitApp` also closes the app on the host (otherwise it
    // keeps running and can be resumed).
    void disconnect(bool quitApp);

    MoonlightStats stats() const;
    uint64_t skipped() const;
    uint64_t unitsReceived() const;   // cheap: no library calls
    sink::CaptureAudioState audioState() const { return audio_.snapshot(); }
    void setAudioGain(float value) { audio_.setGain(value); }
    void setAudioSync(unsigned mode, int offset) { audio_.setSync(mode, offset); }
    void videoPresented(double ptsMs, int64_t host100ns);
    void videoReset(bool resetAudio = true) { audio_.videoReset(resetAudio); }

    // --- input: callable from any thread, ignored unless the stream is up ---------------------
    // Controller 0 only. The pad state is sent when it changes; a device or focus loss (an
    // inactive state) releases everything on the host.
    void controller(const remoteplay::ControllerState& state);
    // Rumble the host asked for (and the last non-zero value again every 2 s, since the host
    // only reports changes and SDL rumble has to be renewed).
    remoteplay::ControllerFeedback takeFeedback();
    void keyboard(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down);
    void mouseMove(int dx, int dy);
    void mouseButton(int button, bool down);   // 1 left, 2 middle, 3 right, 4 X1, 5 X2
    void scroll(int delta, bool horizontal);   // WHEEL_DELTA units (120 per notch)
    // Releases every key, mouse button and the pad on the host (capture lost, focus lost, end).
    void releaseInput();

    // The C callbacks of moonlight-common-c land here (static trampolines in the .cpp).
    struct Callbacks;

private:
    friend struct Callbacks;
    struct Impl;

    void run(std::stop_token stop, MoonlightConnectDesc desc);
    void publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info);

    std::unique_ptr<Impl> p_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::jthread owner_;
    std::atomic<bool> cancel_{false};
    bool initialized_ = false, started_ = false;
    SourceInfo info_, publishedInfo_;
    struct Frame { std::shared_ptr<AVFrame> frame; pipeline::FramePacket packet; SourceInfo info; };
    std::optional<Frame> latest_;
    std::shared_ptr<AVFrame> view_;
    uint64_t sequence_ = 0, skipped_ = 0;
    bool openFlagPending_ = true;
    MoonlightStats stats_;
    sink::CaptureAudioSession audio_;
};

} // namespace veyra::source
