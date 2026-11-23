// SPDX-License-Identifier: GPL-3.0-only
// Loss recovery for the Xbox video track. libdatachannel's stock receive chain has
// no retransmission requests and its depacketizer assembles whatever arrives: one
// lost RTP packet becomes a torn FU-A NAL, the decoder conceals it, and every
// following P frame references the damaged picture (the field "smearing" reports).
// Chromium-based reference clients recover because libwebrtc NACKs lost packets and
// reorders them before depacketization. This handler adds the same two pieces in
// front of the stock H264RtpDepacketizer:
//   1. a bounded per-frame reorder buffer that also waits briefly for NACKed packets;
//   2. RFC 4585 NACK requests (the offer already advertises a=rtcp-fb:* nack).
// Frames still incomplete after the wait are dropped whole (never fed torn to the
// decoder); the existing keyframe-request path then rebuilds the reference chain.
#pragma once
#include <rtc/rtc.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace veyra::xbox {

struct LossRecoveryStats {
    uint64_t packetsReceived = 0;
    uint64_t packetsReordered = 0;   // arrived out of order but before assembly
    uint64_t packetsRecovered = 0;   // arrived after a NACK, before the frame deadline
    uint64_t nacksSent = 0;
    uint64_t framesDropped = 0;      // incomplete after the wait window
    uint64_t framesLate = 0;         // completed after their frame was already dropped
};

class VideoLossRecovery final : public rtc::MediaHandler {
public:
    // maxWait: how long an incomplete frame may wait for retransmissions.
    // maxBufferedPackets: hard memory bound; overflow flushes the oldest frame.
    explicit VideoLossRecovery(std::chrono::milliseconds maxWait = std::chrono::milliseconds(60),
                               size_t maxBufferedPackets = 2048);

    void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override;

    LossRecoveryStats stats() const;

private:
    using Clock = std::chrono::steady_clock;

    struct Packet {
        rtc::message_ptr message;
        Clock::time_point arrival;
    };
    struct Frame {
        uint32_t timestamp = 0;
        std::map<uint32_t, Packet> packets; // ordered by extended 32-bit sequence
        uint32_t baseSeq = 0;               // extended sequence of the first packet seen
        uint16_t baseRaw = 0;
        bool haveBase = false;
        uint32_t headSeq = 0;               // extended sequence of the frame's true head
        bool headKnown = false;             // the head position is inferred
        bool headArrived = false;           // the head packet itself is present
        bool markerSeen = false;
        Clock::time_point firstArrival{};
        Clock::time_point gapSince{};       // when the current hole appeared
        bool gapOpen = false;
        std::set<uint32_t> requested;       // extended seqs already NACKed
        Clock::time_point lastNack{};
        unsigned nackRounds = 0;
    };

    uint32_t unwrap(Frame& frame, uint16_t seq) const;
    void flushFrame(Frame& frame, rtc::message_vector& out, bool complete);
    void sendNacks(Frame& frame, const rtc::message_callback& send, uint32_t ssrc);
    static bool frameComplete(const Frame& frame);

    mutable std::mutex mutex_;
    std::map<uint32_t, Frame> frames_; // by RTP timestamp; small, ordered
    std::vector<uint32_t> order_;      // timestamps in arrival order
    size_t bufferedPackets_ = 0;
    uint32_t lastSsrc_ = 0;
    bool haveSeq_ = false;
    uint16_t lastSeq_ = 0;
    uint32_t seqCycles_ = 0;
    const std::chrono::milliseconds maxWait_;
    const size_t maxBufferedPackets_;
    LossRecoveryStats stats_;
};

} // namespace veyra::xbox