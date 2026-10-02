// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// XboxSessionSource: one home-streaming session from the user's own Xbox console (unofficial; protocol of
// Greenlight). It provisions the session through Microsoft's streaming service, receives H.264 and Opus
// over WebRTC (libdatachannel), decodes the video with the shared hardware decoder and plays the audio.
// The effect chain is not part of it.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "veyra/remoteplay/Types.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/IFrameSource.h"

struct AVFrame;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::xbox { class Account; }

namespace veyra::source {

struct XboxConnectDesc {
    std::shared_ptr<xbox::Account> account;
    std::string serverId;            // the console
    std::string consoleName;         // for messages
    bool gamepad = true;             // forward the PC's controller
    std::string locale = "zh-CN";
    std::shared_ptr<ID3D12Device> decodeDevice;
    std::shared_ptr<ID3D12CommandQueue> decodeQueue;
};

struct XboxStats {
    enum class State : uint8_t { Idle, Starting, Connecting, Streaming, Ended, Failed };
    State state = State::Idle;
    std::wstring message;           // progress while starting; the reason when Ended or Failed
    uint32_t width = 0, height = 0;
    bool hardwareDecode = false;
    uint64_t units = 0, decoded = 0, dropped = 0, decodeErrors = 0, keyframeRequests = 0;
    double receivedFps = 0, decodedFps = 0, videoMbps = 0;
    double decodeMs = 0, rttMs = 0;
};

class XboxSessionSource final : public IFrameSource {
public:
    XboxSessionSource();
    ~XboxSessionSource() override;
    XboxSessionSource(const XboxSessionSource&) = delete;
    XboxSessionSource& operator=(const XboxSessionSource&) = delete;

    // Starts the console session and connects. Blocks until the stream is up or failed (up to about a
    // minute while the console wakes); false means see stats().message. cancel() aborts it from another thread.
    bool connect(XboxConnectDesc desc);
    void cancel() { cancel_ = true; }

    bool open(const SourceOpenDesc&) override { return false; }
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket&, const AVFrame**) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;

    XboxStats stats() const;
    uint64_t skipped() const;
    uint64_t unitsReceived() const;
    sink::CaptureAudioState audioState() const { return audio_.snapshot(); }
    void setAudioGain(float value) { audio_.setGain(value); }
    void setAudioSync(unsigned mode, int offset) { audio_.setSync(mode, offset); }
    void videoPresented(double ptsMs, int64_t host100ns, bool sourceFrame = true);
    void videoReset(bool resetAudio = true) { audio_.videoReset(resetAudio); }

    // Controller 0; sent when it changes and at least every 33 ms (the console expects a heartbeat).
    void controller(const remoteplay::ControllerState& state);
    remoteplay::ControllerFeedback takeFeedback();

private:
    struct Impl;
    void run(XboxConnectDesc desc);
    void decodeLoop();
    void publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info);
    void setState(XboxStats::State state, std::wstring message);

    std::unique_ptr<Impl> p_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::atomic<bool> cancel_{false};
    bool initialized_ = false, started_ = false;
    SourceInfo info_, publishedInfo_;
    struct Frame { std::shared_ptr<AVFrame> frame; pipeline::FramePacket packet; SourceInfo info; };
    std::optional<Frame> latest_;
    std::shared_ptr<AVFrame> view_;
    uint64_t sequence_ = 0, skipped_ = 0;
    bool openFlagPending_ = true;
    XboxStats stats_;
    sink::CaptureAudioSession audio_;
    std::jthread owner_;
};

} // namespace veyra::source
