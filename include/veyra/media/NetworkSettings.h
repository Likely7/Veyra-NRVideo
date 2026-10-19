#pragma once
// Network playback settings, their resolved buffer plan and the statistics the
// shared demux session publishes (#48). Pure data: no FFmpeg or Windows headers,
// so the engine snapshot, the UI bridge and the tests can include it freely.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace veyra::media {

enum class NetworkBufferLevel : uint8_t { Auto = 0, Off, Small, Medium, Large };
// What to do with pictures the decoder flags as damaged (network packet loss).
//   Conceal      : show FFmpeg's concealed picture (default, keeps motion going)
//   WaitKeyframe : drop damaged pictures and wait for the next keyframe
enum class CorruptPolicy : uint8_t { Conceal = 0, WaitKeyframe };

struct NetworkSettings {
    NetworkBufferLevel level = NetworkBufferLevel::Auto;
    int startupPrebufferSeconds = -1;   // -1 follows the level, 0 does not wait, >0 seconds
    bool autoReconnect = true;
    CorruptPolicy corrupt = CorruptPolicy::Conceal;
    bool operator==(const NetworkSettings&) const = default;
};

inline const char* networkBufferLevelId(NetworkBufferLevel level) {
    switch (level) {
    case NetworkBufferLevel::Off: return "off";
    case NetworkBufferLevel::Small: return "small";
    case NetworkBufferLevel::Medium: return "medium";
    case NetworkBufferLevel::Large: return "large";
    default: return "auto";
    }
}
inline NetworkBufferLevel networkBufferLevelFromId(std::string_view id) {
    if (id == "off") return NetworkBufferLevel::Off;
    if (id == "small") return NetworkBufferLevel::Small;
    if (id == "medium") return NetworkBufferLevel::Medium;
    if (id == "large") return NetworkBufferLevel::Large;
    return NetworkBufferLevel::Auto;
}
inline CorruptPolicy corruptPolicyFromId(std::string_view id) {
    return id == "keyframe" ? CorruptPolicy::WaitKeyframe : CorruptPolicy::Conceal;
}

// Resolved numbers. `ahead` is how far past the playback position the demux
// thread keeps reading (VOD forward window / live jitter ceiling, whichever
// applies); `start` is what must be queued before the first picture and after a
// seek; `lowWater` is the lead below which playback pauses for buffering.
struct NetworkBufferPlan {
    double aheadSeconds = 30;
    uint64_t aheadBytes = 256ull << 20;
    double startSeconds = 3;
    double lowWaterSeconds = 0.3;
    // Auto only: the forward window the player may grow to when the server is
    // slower than the stream (resume target is clamped to it).
    double growSeconds = 120;
    uint64_t growBytes = 1024ull << 20;
    bool holds = true;   // false: never pause for buffering (level Off)
};

inline NetworkBufferPlan networkBufferPlan(const NetworkSettings& settings, bool live) {
    NetworkBufferPlan plan;
    constexpr uint64_t MiB = 1ull << 20;
    if (live) {
        switch (settings.level) {
        case NetworkBufferLevel::Off:    plan = {1.0, 32 * MiB, 0.0, 0.0, 1.0, 32 * MiB, false}; break;
        case NetworkBufferLevel::Small:  plan = {1.5, 32 * MiB, 0.5, 0.1, 1.5, 32 * MiB, true}; break;
        case NetworkBufferLevel::Medium: plan = {3.0, 64 * MiB, 1.5, 0.2, 3.0, 64 * MiB, true}; break;
        case NetworkBufferLevel::Large:  plan = {6.0, 128 * MiB, 3.0, 0.3, 6.0, 128 * MiB, true}; break;
        default:                         plan = {3.0, 64 * MiB, 1.0, 0.15, 6.0, 128 * MiB, true}; break;
        }
    } else {
        switch (settings.level) {
        case NetworkBufferLevel::Off:    plan = {2.0, 32 * MiB, 0.0, 0.0, 2.0, 32 * MiB, false}; break;
        case NetworkBufferLevel::Small:  plan = {8.0, 64 * MiB, 2.0, 0.25, 8.0, 64 * MiB, true}; break;
        case NetworkBufferLevel::Medium: plan = {30.0, 256 * MiB, 5.0, 0.4, 30.0, 256 * MiB, true}; break;
        case NetworkBufferLevel::Large:  plan = {120.0, 1024 * MiB, 10.0, 0.5, 120.0, 1024 * MiB, true}; break;
        default:                         plan = {30.0, 256 * MiB, 3.0, 0.3, 120.0, 1024 * MiB, true}; break;
        }
    }
    if (settings.startupPrebufferSeconds >= 0 && plan.holds)
        plan.startSeconds = std::min(double(settings.startupPrebufferSeconds), live ? 30.0 : 120.0);
    // The window must be able to hold what the player waits for.
    plan.aheadSeconds = std::max(plan.aheadSeconds, plan.startSeconds * 1.5 + 2.0);
    plan.growSeconds = std::max(plan.growSeconds, plan.aheadSeconds);
    return plan;
}

// Statistics for the signal panel and the status line. Plain values, copied out
// of the session; fields that are not measured yet stay 0 (the UI shows "—").
struct NetworkStats {
    enum class State : uint8_t { Idle = 0, Prebuffering, Playing, Buffering, Reconnecting, Failed, Ended };
    bool active = false;          // the source goes through the shared demux session
    bool live = false;            // not seekable: no resume position
    State state = State::Idle;
    double bufferedSeconds = 0;   // content queued ahead of the playback position
    double targetSeconds = 0;     // what the current wait is collecting
    uint64_t bufferedBytes = 0;
    uint64_t capBytes = 0;
    double downloadBytesPerSecond = 0;   // arrival rate over the last ~2 s
    double serverBytesPerSecond = 0;     // throughput while actually reading
    double bitrateBytesPerSecond = 0;    // media bitrate (container or measured)
    bool bandwidthLow = false;           // server slower than the stream
    uint32_t reconnects = 0;             // successful reconnects
    uint32_t reconnectAttempt = 0;       // current attempt, 0 when not reconnecting
    uint32_t stalls = 0;                 // times playback paused for buffering
    uint64_t corruptPackets = 0;         // video packets flagged damaged by the demuxer
    uint64_t corruptFrames = 0;          // pictures the decoder flagged damaged
    uint64_t skippedFrames = 0;          // damaged pictures dropped (WaitKeyframe)
    uint64_t sessionBytes = 0;           // total bytes demuxed
    uint32_t connections = 0;            // connections opened over the session (1 + reconnects)
    std::wstring message;                // explanation shown to the user, empty when none
    bool holding() const { return state == State::Prebuffering || state == State::Buffering; }
};

}  // namespace veyra::media
