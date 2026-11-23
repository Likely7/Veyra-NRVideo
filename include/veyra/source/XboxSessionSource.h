// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// XboxSessionSource (V1 原版): one home-streaming session from the user's own Xbox console (unofficial;
// protocol of Greenlight). It provisions the session through Microsoft's streaming service, receives
// H.264 and Opus over WebRTC (libdatachannel), decodes the video with the shared hardware decoder and
// plays the audio. The effect chain is not part of it. Selected by transportEngine=1; the V2 enhanced
// engine (XboxWebSource, Chromium/WebView2) is the default.
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
#include "veyra/source/IXboxSource.h"

struct AVFrame;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::xbox { class Account; }

namespace veyra::source {

class XboxSessionSource final : public IXboxSource {
public:
    XboxSessionSource();
    ~XboxSessionSource() override;
    XboxSessionSource(const XboxSessionSource&) = delete;
    XboxSessionSource& operator=(const XboxSessionSource&) = delete;

    // Starts the console session and connects. Blocks until the stream is up or failed (up to about a
    // minute while the console wakes); false means see stats().message. cancel() aborts it from another thread.
    bool connect(XboxConnectDesc desc) override;
    void cancel() override { cancel_ = true; }

    bool open(const SourceOpenDesc&) override { return false; }
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket&, const AVFrame**) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;

    XboxStats stats() const override;
    uint64_t skipped() const override;
    uint64_t unitsReceived() const override;
    sink::CaptureAudioState audioState() const override { return audio_.snapshot(); }
    void setAudioGain(float value) override { audio_.setGain(value); }
    void setAudioSync(unsigned mode, int offset) override { audio_.setSync(mode, offset); }
    void videoPresented(double ptsMs, int64_t host100ns, bool sourceFrame = true) override;
    void videoReset(bool resetAudio = true) override { audio_.videoReset(resetAudio); }

    // Controller 0; sent when it changes and at least every 33 ms (the console expects a heartbeat).
    void controller(const remoteplay::ControllerState& state) override;
    remoteplay::ControllerFeedback takeFeedback() override;

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