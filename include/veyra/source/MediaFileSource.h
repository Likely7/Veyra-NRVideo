#pragma once

// MediaFileSource - local video file input (FFmpeg containers/codecs,
// including H.264/HEVC/AV1 and professional MOV codecs when the selected
// FFmpeg prefix provides their decoders)
// composing the product veyra_media libraries. No second decode
// implementation exists here: FFmpegDemuxer + FFmpegVideoDecoder do the
// work; this class owns contract translation (PTS/duration/color/flags/
// epoch) and the seek protocol.
#include <cstdint>
#include <string_view>

#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"

#include <deque>
#include <map>
#include "veyra/media/FFmpegDeinterlacer.h"
#include "veyra/source/IFrameSource.h"

namespace veyra::source {

class MediaFileSource final : public IFrameSource {
public:
    MediaFileSource() = default;
    ~MediaFileSource() override;

    MediaFileSource(const MediaFileSource&) = delete;
    MediaFileSource& operator=(const MediaFileSource&) = delete;

    bool open(const SourceOpenDesc& desc) override;
    const SourceInfo& info() const override { return info_; }
    SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) override;
    bool seek(const pipeline::Rational& targetSeconds) override;
    void close() noexcept override;

    uint64_t framesRead() const { return framesRead_; }
    uint64_t seekCount() const { return seekCount_; }
    uint64_t epoch() const { return epoch_; }
    const std::wstring& errorMessage() const { return errorMessage_; }

private:
    pipeline::ColorDescription parseColor(const AVCodecParameters* params) const;
    bool fallbackToSoftware(std::string_view reason);

    media::FFmpegDemuxer demuxer_;
    media::FFmpegVideoDecoder decoder_;
    media::FFmpegDeinterlacer deinterlacer_;
    // Custom (P5): Dolby Vision Profile 7 enhancement layer. The EL rides inside
    // each access unit as private NAL type 63, so a second decoder consumes the
    // same packets. Off unless the caller asks for it, so BL playback is byte
    // identical while this is still being brought up.
    media::FFmpegVideoDecoder elDecoder_;
    AVFrame* elFrame_ = nullptr;      // EL frame matched to the frame in flight (owned)
    // Custom (P5): decoded EL frames keyed by timestamp, waiting for their base frame.
    std::map<int64_t, AVFrame*> elByPts_;
    uint32_t elLastWidth_ = 0, elLastHeight_ = 0;   // last decoded EL extent
    // A base frame held back while waiting for its EL counterpart (bounded wait).
    std::deque<std::pair<pipeline::FramePacket, AVFrame*>> baseWaiting_;
    std::vector<std::vector<uint8_t>> elParamSets_; // VPS/SPS/PPS of the EL, re-sent per AU
    bool elLayerEnabled_ = false;
    bool elLayerOpen_ = false;
    unsigned elLayerFrames_ = 0, elLayerMisses_ = 0;
    // Returns true when this packet actually carried enhancement layer data.
    bool feedEnhancementLayer(const AVPacket* packet);
    SourceReadStatus readBase(pipeline::FramePacket& out, const AVFrame** decodedFrame);
    void drainElFrames();
    bool takeElForPts(int64_t pts, AVFrame** outFrame);
    void closeEnhancementLayer();
    bool deinterlaceEnabled_=true,filterDraining_=false;
    pipeline::DoviFrameInfo doviFrameInfo{};   // Dolby Vision L1 (RPU), invalid unless present
    SourceInfo info_{};
    uint64_t sequence_ = 0;
    uint64_t epoch_ = 1;
    uint64_t framesRead_ = 0;
    uint64_t ptsFallbacks_ = 0; // frames delivered with best_effort/dts timestamps
    uint64_t seekCount_ = 0;
    bool pendingSeekFlag_ = false;
    bool draining_ = false;
    bool eofSignalled_ = false;
    std::wstring errorMessage_;
    int64_t lastPtsUs_ = INT64_MIN;
    std::wstring path_;
    bool preferHardwareDecode_ = false;
    bool requireHardwareDecode_ = false; // "强制硬解": never fall back to software
    const std::atomic<bool>* cancelled_ = nullptr;
};

} // namespace veyra::source
