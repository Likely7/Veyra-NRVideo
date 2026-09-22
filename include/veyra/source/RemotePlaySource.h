#pragma once

// RemotePlaySource adapts the Chiaki callback stream to the same source
// contract used by local files and capture cards.  Chiaki owns the network
// protocol; this class owns FFmpeg decoder state and decoded-frame lifetime.
#include "veyra/source/IFrameSource.h"
#include "veyra/remoteplay/ChiakiBackend.h"
#include "veyra/remoteplay/SessionInbox.h"
#include "veyra/remoteplay/Timeline.h"
#include "veyra/sink/AudioPcmSource.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>

struct AVCodecContext;
struct AVFrame;
struct ID3D12Device;

namespace veyra::source {

struct RemotePlayConnectDesc {
    remoteplay::NativeConnectRequest request;
    remoteplay::QueueLimits queueLimits{};
    enum class DecodeMode { Automatic, Software, Hardware };
    DecodeMode decodeMode=DecodeMode::Automatic;
    bool highQualitySampling=true; // local reconstruction, never a server quality promise
    // Combined PS5-control + capture-card mode: the session keeps the controller,
    // keepalive and feedback channels but discards video/audio on receipt. The
    // protocol has no "stop sending media" switch, so the console still
    // transmits; only the local receive side drops it.
    bool controlOnly=false;
    // Set by the engine; shared ownership keeps the exact presentation adapter
    // alive until the decoder owner has stopped, including failed connects.
    std::shared_ptr<ID3D12Device> decodeDevice;
};

class RemotePlaySource final : public IFrameSource {
public:
    RemotePlaySource();
    ~RemotePlaySource() override;

    RemotePlaySource(const RemotePlaySource&) = delete;
    RemotePlaySource& operator=(const RemotePlaySource&) = delete;

    // IFrameSource::open cannot carry credentials.  Product callers use
    // connect() and then read(); open() deliberately fails closed.
    bool open(const SourceOpenDesc& desc) override;
    bool connect(const RemotePlayConnectDesc& desc);
    remoteplay::BackendResult submitController(const remoteplay::ControllerState& state) { return backend_.submitController(state); }
    remoteplay::BackendResult submitLoginPin(std::string_view pin) { return backend_.submitLoginPin(pin); }
    remoteplay::ControllerFeedback takeFeedback(){return backend_.takeFeedback();}
    remoteplay::NativeSnapshot nativeSnapshot()const{return backend_.snapshot();} // session owner only
    remoteplay::BackendResult disconnectTransportForTest(){return backend_.stop();}
    bool connected() const noexcept { return connected_; }
    remoteplay::SessionInbox::Snapshot sessionSnapshot() const;
    std::shared_ptr<const remoteplay::SessionInbox> telemetryInbox()const{return inbox_;}
    void recoverVideo(){flushDecoder();if(token_)inbox_->decodeFailed(token_->generation());}

    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) override;
    bool seek(const pipeline::Rational&) override { return false; }
    void close() noexcept override;

    // Pull decoded Opus PCM for the existing WASAPI AudioPcmSource adapter.
    std::size_t pullAudio(float* stereo, std::size_t frames, double* firstPtsMs);

private:
    friend struct RemotePlaySourceTestAccess;
    struct DecodedFrame {
        std::shared_ptr<AVFrame> frame;
        pipeline::FramePacket packet;
    };

    bool openDecoder(remoteplay::Codec codec, std::uint32_t width, std::uint32_t height);
    bool submitPacket(std::span<const std::uint8_t> bytes, std::uint64_t sourceIndex,
        const pipeline::FramePacket& sourcePacket);
    bool drainDecoder(std::uint64_t sourceIndex, const pipeline::FramePacket& sourcePacket);
    void flushDecoder() noexcept;
    pipeline::FramePacket makePacket(const remoteplay::VideoSample& sample,
        std::uint64_t sourceIndex, std::uint64_t epoch, bool reset);

    std::shared_ptr<remoteplay::SessionInbox> inbox_;
    remoteplay::ChiakiBackend backend_;
    std::optional<remoteplay::SessionInbox::Token> token_;
    remoteplay::RemotePlayClock clock_;
    remoteplay::NativeConnectRequest request_;
    SourceInfo info_{};
    AVCodecContext* codecContext_ = nullptr;
    AVFrame* decoderFrame_ = nullptr;
    std::deque<DecodedFrame> ready_;
    std::shared_ptr<AVFrame> lastFrame_;
    struct PacketStamp { std::int64_t id; pipeline::FramePacket packet; };
    std::deque<PacketStamp> packetStamps_;
    std::optional<remoteplay::PcmBlock> audioBlock_;
    std::size_t audioOffset_ = 0;
    std::optional<std::uint64_t> audioAnchorSample_;
    std::int64_t audioAnchorPts_ = 0;
    std::uint64_t audioNextSample_ = 0;
    std::uint32_t audioRate_ = 0;
    std::int64_t nextPacketId_ = 0;
    remoteplay::Sequence16Extender wireSequence_;
    std::uint64_t sequence_ = 0;
    std::uint64_t fallbackSourceIndex_ = 0;
    std::uint64_t decoderEpoch_ = 1;
    std::uint64_t origin100ns_ = 0;
    bool connected_ = false;
    bool stopFailed_ = false;
    bool decoderReady_ = false;
    bool waitingForFirstFrame_ = true;
    bool pendingOpenFlag_ = true;
    RemotePlayConnectDesc::DecodeMode decodeMode_=RemotePlayConnectDesc::DecodeMode::Automatic;
    std::shared_ptr<ID3D12Device> decodeDevice_;
    bool hardwareFallback_=false;
    bool discardMedia_=false;
};

class RemotePlayAudioSource final : public sink::AudioPcmSource {
public:
    explicit RemotePlayAudioSource(RemotePlaySource& source) : source_(source) {}
    std::size_t pull(float* stereo, std::size_t frames, double* firstPtsMs) override;
    std::optional<double> lastPullEndPtsMs() const override { return lastEndPtsMs_; }
    bool padUnderruns() const override { return false; }

private:
    RemotePlaySource& source_;
    std::optional<double> lastEndPtsMs_;
};

} // namespace veyra::source
