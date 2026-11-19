// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Types.h"
#include "Timeline.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
namespace veyra::remoteplay {
struct QueueLimits {
    std::size_t maxFrames=4, maxBytes=8u*1024u*1024u, maxConfigBytes=64u*1024u;
    HostTime maxAge100ns=1000000; // 100 ms development safety ceiling, not target latency.
    void validate() const;
};
// Limits of a product session. The queue is empty in normal running: the owner loop decodes
// a frame as it arrives. The limit only matters while that loop does not run. With the
// development ceiling above, a pause of about 70 ms (four frames at 60 fps) dropped the
// queue and refused every frame until a requested keyframe arrived, although nothing was
// lost on the network: the picture froze for another 150 ms and the processing history was
// reset (field log 2026-11-19, PS5 over wired LAN: three times in 98 minutes, 0 packets
// lost). Twelve frames / 250 ms let the decoder catch up instead; a longer pause still
// ends in the keyframe recovery.
inline constexpr QueueLimits kSessionQueueLimits{12,8u*1024u*1024u,64u*1024u,2500000};
enum class PushStatus {Accepted,ConfigStored,WaitingForIdr,Malformed,WrongGeneration,Closed};
struct VideoQueueStats {
    std::uint64_t accessUnits=0,configMessages=0,accepted=0,consumed=0,dropped=0,
        rejected=0,recoveries=0,idrRequests=0;
    std::size_t depth=0,bytes=0,highWaterDepth=0,highWaterBytes=0;
    bool waitingForIdr=true;std::uint64_t epoch=1;
};
struct QueuedVideo {
    VideoSample sample;
    // Full configuration snapshot must be fed before this AU when non-null.
    // The caller drains EAGAIN then retries; do not drop it as "not a picture".
    std::shared_ptr<const VideoSample> configBefore;
    std::uint64_t epoch=1;
    bool resetDecoder=false;
};
class VideoIngress {
public:
    explicit VideoIngress(QueueLimits limits={});
    void begin(Generation);
    PushStatus push(VideoSample,HostTime now);
    std::optional<QueuedVideo> tryPop(HostTime now);
    bool waitForData(std::chrono::milliseconds timeout);
    // Poll from the protocol OWNER, never request an IDR while holding a C callback's lock.
    bool takeIdrRequest(HostTime now);
    void requireKeyframe(); // Decoder reports fatal error / lost references.
    void close();
    VideoQueueStats stats() const;
private:
    void recoverLocked();
    void updateStatsLocked();
    bool expiredLocked(HostTime now) const;
    QueueLimits limits_;mutable std::mutex mutex_;std::condition_variable cv_;
    std::deque<QueuedVideo> queue_;std::shared_ptr<const VideoSample> config_;
    Generation generation_=0;std::size_t payloadBytes_=0;bool closed_=true,waiting_=true,pendingIdr_=false;
    HostTime lastIdr_=-1;Sequence16Extender sequence_;VideoQueueStats stats_{};
};
struct AudioQueueStats {std::uint64_t accepted=0,consumed=0,dropped=0,rejected=0;double bufferedMs=0;};
class AudioIngress {
public:
    explicit AudioIngress(std::uint32_t maxBufferedMs=100);
    void begin(Generation);
    bool push(PcmBlock);
    std::optional<PcmBlock> tryPop();
    void close();
    AudioQueueStats stats() const;
private:
    mutable std::mutex mutex_;std::deque<PcmBlock> queue_;
    Generation generation_=0;std::uint32_t rate_=0,channels_=0,maxMs_;std::size_t frames_=0;
    std::uint64_t nextSample_=0;bool closed_=true,gap_=false;AudioQueueStats stats_{};
};
} // namespace veyra::remoteplay
