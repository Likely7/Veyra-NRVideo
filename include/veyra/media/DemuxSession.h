#pragma once
// DemuxSession: one connection, one demux thread, bounded packet queues for the
// video and the selected audio stream (#48, docs/NETWORK_BUFFER_48_PLAN_2026-10-17.md).
//
// Network sources (http/https/rtsp) and files on a server (UNC paths, mapped
// drives) used to be opened twice - FFmpegDemuxer for the picture, AudioPipeline for
// the sound - so every byte crossed the link twice and a stall of either blocked the
// engine. A session owns the only AVFormatContext: its thread reads packets ahead of
// playback, so a slow or briefly broken link is absorbed by the queue, an interrupted
// connection is re-established behind the queue, and the player can pause for
// buffering instead of failing. Local drives never use it.
//
// Threading: the demux thread owns the AVFormatContext. Consumers (the engine thread
// for video, the audio thread for sound) only pop packets and read the stream
// description copied at open, which stays valid across reconnects.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "veyra/media/NetworkInput.h"
#include "veyra/media/NetworkSettings.h"

namespace veyra::media {

struct CodecParametersDeleter { void operator()(AVCodecParameters*) const; };

// What the consumers need from a stream, copied at open (AVStream pointers do not
// survive a reconnect).
struct SessionStreamInfo {
    int index = -1;
    int type = -1;                       // AVMediaType
    std::unique_ptr<AVCodecParameters, CodecParametersDeleter> parameters;
    int timeBaseNum = 0, timeBaseDen = 1;
    int avgRateNum = 0, avgRateDen = 0;          // avg_frame_rate
    int nominalRateNum = 0, nominalRateDen = 0;  // r_frame_rate
    int sarNum = 0, sarDen = 0;
    int64_t duration = INT64_MIN;        // stream time base, AV_NOPTS_VALUE when unknown
    std::string language, title, rotate;
};

class DemuxSession {
public:
    struct OpenParams {
        std::wstring path;
        NetworkSettings settings;
        const std::atomic<bool>* cancelled = nullptr;  // owner stop (engine): aborts every blocking call
        int audioStream = -2;                          // -2 best audio, -1 none, >=0 explicit
    };
    // Connects and probes (bounded by the connect/probe limits and `cancelled`),
    // then starts the demux thread. Null with `error` set on failure.
    static std::shared_ptr<DemuxSession> open(const OpenParams& params, std::wstring* error = nullptr);
    ~DemuxSession();
    DemuxSession(const DemuxSession&) = delete;
    DemuxSession& operator=(const DemuxSession&) = delete;

    // ---- description (immutable after open) -----------------------------------
    const std::vector<SessionStreamInfo>& streams() const { return streams_; }
    const SessionStreamInfo* stream(int index) const { return index >= 0 && size_t(index) < streams_.size() ? &streams_[size_t(index)] : nullptr; }
    int videoStream() const { return videoIndex_; }
    int audioStream() const { return audioIndex_.load(); }
    bool live() const { return live_; }
    bool seekable() const { return !live_; }
    int64_t durationUs() const { return durationUs_; }
    int64_t containerBitRate() const { return containerBitRate_; }
    const std::string& formatName() const { return formatName_; }
    const std::wstring& path() const { return path_; }
    const NetworkSettings& settings() const { return settings_; }
    const NetworkBufferPlan& plan() const { return plan_; }

    // ---- consumers --------------------------------------------------------------
    enum class Pop { Packet, Empty, Eof, Failed, Cancelled };
    struct Meta {
        bool discontinuity = false;   // first packet after a live reconnect: reset the decoder
        uint32_t generation = 0;      // seek counter the packet belongs to
    };
    // Moves the next packet into `dst` (which must be unreferenced), waiting up to
    // waitMs for one. Video honours the buffering hold (Empty while playback is
    // paused for buffering) only when asked: offline readers pass false.
    Pop popVideo(AVPacket* dst, Meta& meta, int waitMs, bool honourHold);
    Pop popAudio(AVPacket* dst, Meta& meta, int waitMs);
    // Audio packets are queued only for the selected stream; -1 drops them all.
    bool selectAudioStream(int index);
    // Where the player is (media timeline, microseconds): drives how far ahead the
    // thread reads and when playback pauses for buffering.
    void setPlaybackPositionUs(int64_t us);
    // Repositions (seekable sources only). Returns once the new position is set; the
    // queues restart from there and generation() advances.
    bool seek(int64_t targetUs);
    uint32_t generation() const { return generation_.load(); }
    // Drops the connection and re-establishes it behind the queue.
    void requestReconnect(const char* reason);
    // True while the player should hold still (start-up wait or underflow).
    bool holding() const;
    NetworkStats stats() const;
    void addCorruptFrames(uint64_t flagged, uint64_t skipped);
    std::wstring failureMessage() const;
    bool eof() const;

private:
    DemuxSession();
    struct Item {
        AVPacket* packet = nullptr;
        uint32_t generation = 0;
        bool discontinuity = false;
        int64_t startUs = 0, endUs = 0;
        int64_t dtsUs = INT64_MIN;       // AV_NOPTS_VALUE when the container gave none
        size_t bytes = 0;
    };
    struct Queue {
        std::deque<Item> items;
        size_t bytes = 0;
        int64_t durationUs = 0;          // content waiting in the queue: the sum of the packets' durations (never their timestamps)
        int64_t firstUs = INT64_MIN;     // first content time since open / seek
        int64_t lastEndUs = INT64_MIN;   // end of the last packet queued (kept after pops)
        int64_t lastDtsUs = INT64_MIN;   // decode time of the last packet queued (resume de-duplication)
        bool ended = false;
        bool dedupe = false;             // after a VOD reconnect: drop packets at or before lastDtsUs
        void clear();
    };
    struct Interrupt;
    struct FileAvio;
    enum class Phase { Prebuffering, Playing, Buffering };

    // demux thread
    void threadMain();
    bool openContext(std::wstring* error, bool initial);
    void closeContext();
    void applyDiscardFlags();
    bool verifyStreams();
    void onPacket(AVPacket* packet);
    bool pushLocked(Item&& item, bool video);
    bool recover(int code, const char* why);
    void doSeek();
    bool flowBlockedLocked() const;
    void fail(const std::wstring& message);
    void flushPendingAudio(int64_t anchorUs);

    // shared state (m_)
    double leadSecondsLocked() const;
    void updatePhaseLocked();
    double resumeTargetLocked() const;
    double aheadSecondsLocked() const;
    uint64_t aheadBytesLocked() const;
    double bitrateLocked(double* measured) const;
    double serverRateLocked() const;
    void noteArrivalLocked(size_t bytes);
    bool endedLocked() const;
    void enterPhaseLocked(Phase phase, const char* why);
    void logSummaryLocked(bool force);

    std::vector<SessionStreamInfo> streams_;
    int videoIndex_ = -1;
    std::atomic<int> audioIndex_{-1};
    bool live_ = true;
    int64_t durationUs_ = 0, containerBitRate_ = 0;
    std::string formatName_;
    std::wstring path_;
    NetworkSettings settings_;
    NetworkBufferPlan plan_;
    std::atomic<uint32_t> generation_{1};
    const std::atomic<bool>* cancelled_ = nullptr;

    mutable std::mutex m_;
    std::condition_variable consumerCv_, producerCv_, seekCv_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::unique_ptr<Interrupt> irq_;
    std::unique_ptr<FileAvio> file_;
    AVFormatContext* context_ = nullptr;   // demux thread only
    bool network_ = false;                 // http/https/rtsp (otherwise a file)

    Queue video_, audio_;
    Phase phase_ = Phase::Prebuffering;
    double targetSeconds_ = 0;
    bool afterSeek_ = false;
    int64_t playbackUs_ = -1;              // hint from the player (media timeline)
    bool hasHint_ = false;                 // the player supplies playbackUs_; otherwise consumption is the position
    int64_t consumedUs_ = -1;              // start of the last video packet a consumer took
    bool eof_ = false, failed_ = false, reconnecting_ = false;
    uint32_t reconnectAttempt_ = 0, reconnects_ = 0, stalls_ = 0, connections_ = 0;
    std::wstring failure_;
    bool bandwidthLow_ = false;
    uint64_t corruptPackets_ = 0, corruptFrames_ = 0, skippedFrames_ = 0, totalBytes_ = 0;

    // rate measurement (m_)
    struct Arrival { int64_t ns; size_t bytes; };
    std::deque<Arrival> arrivals_;
    double readBytesDecayed_ = 0, readSecondsDecayed_ = 0;
    int64_t lastRateNs_ = 0;
    struct RateSample { int64_t contentUs; uint64_t cumulativeBytes; };
    std::deque<RateSample> rateSamples_;
    uint64_t contentBytes_ = 0;
    int64_t lastSummaryNs_ = 0;
    int64_t phaseSinceNs_ = 0;

    // commands (m_)
    bool seekPending_ = false, seekDone_ = true, seekOk_ = false;
    int64_t seekTargetUs_ = 0;
    bool reconnectPending_ = false;
    std::string reconnectReason_;

    bool discardDirty_ = false;            // m_: the demux thread re-applies stream discard flags

    // demux thread only
    int64_t offsetUs_ = 0;                 // live reconnect: keeps the timeline continuous
    bool gateKeyframe_ = false;            // live reconnect or restarted timeline: wait for a video keyframe
    int64_t lastAnchorNs_ = 0;             // when the timeline was last re-anchored (late audio of the old one is dropped for a while)
    bool videoDiscontinuityPending_ = false, audioDiscontinuityPending_ = false;
    double liveBackCap_ = 0.0;             // seconds: the start buffer of a live stream that just reconnected (0: not applied)
    bool videoStarved_ = false;            // the video consumer found its queue empty: reading more is always right
    int timestampJumpLogs_ = 0;            // diagnostics: leaps in the stream's timestamps, logged a few times per session
    std::vector<Item> pendingAudio_;       // audio read before the live gate opened
    int64_t pendingAudioBytes_ = 0;
    int64_t reconnectBudgetMs_ = 60000;
};

}  // namespace veyra::media
