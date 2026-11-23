// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// XboxWebSource: the Xbox v2 transport engine. Session provisioning reuses the
// v1 services (Account/StreamApi); the media plane is the reference Chromium
// player inside WebView2 (WebView2Host). Encoded H.264 access units arrive as
// complete Annex-B units and feed the same CaptureCompressedDecoder (D3D12VA)
// as v1; Opus packets go to the same CaptureAudioSession. The enhancement
// chain sees the identical FramePacket contract, so super resolution and frame
// generation are transport-independent.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "veyra/remoteplay/Types.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/IFrameSource.h"
#include "veyra/source/IXboxSource.h"

namespace veyra::source {

class XboxWebSource final : public IXboxSource {
public:
    XboxWebSource();
    ~XboxWebSource() override;
    XboxWebSource(const XboxWebSource&) = delete;
    XboxWebSource& operator=(const XboxWebSource&) = delete;

    // Same contract as v1: blocks until the stream is up or failed (console
    // wake included); false means see stats().message. cancel() aborts it.
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

    // Controller 0; on change and at least every 33 ms (console heartbeat).
    void controller(const remoteplay::ControllerState& state) override;
    remoteplay::ControllerFeedback takeFeedback() override;

private:
    struct Impl;
    void run(XboxConnectDesc desc);
    void decodeLoop();
    void publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info);
    void setState(XboxStats::State state, std::wstring message);
    void requestKeyframe();

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