#pragma once

// FFmpeg demuxer wrapper (Playbook section 13.1). Video-only for Phase 3;
// PTS-driven, never assumes a fixed frame rate.
//
// Two modes. Direct (default): this class owns an AVFormatContext and reads it on
// the caller's thread - local files, exactly as before. Shared (openShared): the
// stream description and the packets come from a DemuxSession that also feeds the
// audio pipeline (network sources and files on a server, #48).

#include <cstdint>
#include <memory>
#include <string>
#include "veyra/media/DemuxSession.h"
#include "veyra/media/NetworkInput.h"

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

    bool open(const std::wstring& path,const std::atomic<bool>* cancelled=nullptr);
    // Shared mode: packets and stream parameters come from the session.
    bool openShared(std::shared_ptr<DemuxSession> session);
    void close();

    bool opened() const { return context_ != nullptr || session_ != nullptr; }
    bool shared() const { return session_ != nullptr; }
    const std::shared_ptr<DemuxSession>& session() const { return session_; }
    int videoStreamIndex() const { return videoStreamIndex_; }
    int64_t durationUs() const;
    bool seekable() const;
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
    // and remains valid until the next call. In shared mode this waits for data
    // (and never pauses for buffering); false with endOfFile=false is a failure.
    bool readVideoPacket(bool& endOfFile);

    // Playback read. Direct mode behaves like readVideoPacket. Shared mode waits at
    // most waitMs and, with honourHold, reports Waiting while playback is paused
    // for buffering.
    enum class ReadResult { Packet, Eof, Error, Waiting, Cancelled };
    ReadResult readVideoPacketEx(int waitMs, bool honourHold);
    // The last packet came first after a live reconnect: reset the decoder.
    bool packetDiscontinuity() const { return packetDiscontinuity_; }

    // Seek to a time in microseconds; the next read yields a packet at or
    // after that time. Flushing the decoder afterwards is the caller's job.
    bool seekToUs(int64_t targetUs);

    const AVPacket* currentPacket() const { return packet_; }
    const DemuxerStats& stats() const { return stats_; }

private:
    void notePacket();

    AVFormatContext* context_ = nullptr;
    std::shared_ptr<DemuxSession> session_;
    AVPacket* packet_ = nullptr;
    int videoStreamIndex_ = -1;
    DemuxerStats stats_{};
    bool havePendingPacket_ = false;
    bool packetDiscontinuity_ = false;
    InputInterrupt interrupt_;
};

} // namespace veyra::media
