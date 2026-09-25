#pragma once

// FFmpeg demuxer wrapper (Playbook section 13.1). Video-only for Phase 3;
// PTS-driven, never assumes a fixed frame rate.

#include <cstdint>
#include <string>

struct AVFormatContext;
struct AVCodecParameters;
struct AVPacket;

namespace veyra::media {

struct DemuxerStats {
    uint64_t packetsRead = 0;
    int64_t firstPts = 0;
    int64_t lastPts = 0;
    uint64_t ptsNonMonotonicCount = 0;
};

class FFmpegDemuxer {
public:
    FFmpegDemuxer() = default;
    ~FFmpegDemuxer();

    FFmpegDemuxer(const FFmpegDemuxer&) = delete;
    FFmpegDemuxer& operator=(const FFmpegDemuxer&) = delete;

    bool open(const std::wstring& path);
    void close();

    bool opened() const { return context_ != nullptr; }
    int videoStreamIndex() const { return videoStreamIndex_; }
    int64_t durationUs() const;
    double averageFps() const; // informational only; the pipeline is PTS-driven
    int nominalRateNum() const;
    int nominalRateDen() const;
    const AVCodecParameters* videoCodecParameters() const;
    int videoTimeBaseNum() const; // stream time_base for frame PTS conversion
    int videoTimeBaseDen() const;
    std::string formatName() const;
    // Stream sample aspect ratio (numerator, denominator) as stored in the
    // container; 0/0 = unknown. The display aspect is this ratio applied to the
    // coded size, then swapped for 90/270 degree rotations.
    void sampleAspect(int& num, int& den) const;
    // Container display-matrix rotation in degrees (0/90/180/270); 0 = none.
    int rotationDegrees() const;

    // Returns false at end of file. The packet stays owned by this demuxer
    // and remains valid until the next call.
    bool readVideoPacket(bool& endOfFile);

    // Seek to a time in microseconds; the next read yields a packet at or
    // after that time. Flushing the decoder afterwards is the caller's job.
    bool seekToUs(int64_t targetUs);

    const AVPacket* currentPacket() const { return packet_; }
    const DemuxerStats& stats() const { return stats_; }

private:
    AVFormatContext* context_ = nullptr;
    AVPacket* packet_ = nullptr;
    int videoStreamIndex_ = -1;
    DemuxerStats stats_{};
    bool havePendingPacket_ = false;
};

} // namespace veyra::media
