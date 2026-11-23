// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/xbox/VideoLossRecovery.h"

#include <algorithm>

namespace veyra::xbox {
namespace {
constexpr unsigned kMaxNackRounds = 4;
constexpr auto kNackInterval = std::chrono::milliseconds(15);
constexpr size_t kMaxNackSeqsPerPacket = 17; // RFC 4585 FCI fields per RTCP packet
constexpr size_t kMaxBufferedFrames = 8;

int32_t seqDelta(uint16_t a, uint16_t b) { return int32_t(int16_t(a - b)); }
} // namespace

VideoLossRecovery::VideoLossRecovery(std::chrono::milliseconds maxWait, size_t maxBufferedPackets)
    : maxWait_(maxWait), maxBufferedPackets_(maxBufferedPackets) {}

LossRecoveryStats VideoLossRecovery::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

uint32_t VideoLossRecovery::unwrap(Frame& frame, uint16_t seq) const {
    if (!frame.haveBase) {
        frame.baseSeq = seq;
        frame.baseRaw = seq;
        frame.haveBase = true;
        return seq;
    }
    // Extend relative to the frame's first packet. A signed 16-bit delta from the
    // base raw sequence handles one wrap in either direction; the span inside a
    // single frame is far below 32768 packets.
    const int32_t delta = seqDelta(seq, frame.baseRaw);
    return uint32_t(int64_t(frame.baseSeq) + delta);
}

bool VideoLossRecovery::frameComplete(const Frame& frame) {
    if (frame.packets.empty() || !frame.markerSeen || !frame.headKnown || !frame.headArrived) return false;
    // The run must start at the frame's true head and be contiguous to the marker.
    if (frame.packets.begin()->first != frame.headSeq) return false;
    uint32_t expected = frame.headSeq;
    for (const auto& [seq, packet] : frame.packets) {
        if (seq != expected) return false;
        ++expected;
    }
    const auto& last = frame.packets.rbegin()->second;
    const auto* header = reinterpret_cast<const rtc::RtpHeader*>(last.message->data());
    return header->marker() != 0;
}

void VideoLossRecovery::sendNacks(Frame& frame, const rtc::message_callback& send, uint32_t ssrc) {
    if (!frame.gapOpen || frame.nackRounds >= kMaxNackRounds) return;
    const auto now = Clock::now();
    if (frame.nackRounds > 0 && now - frame.lastNack < kNackInterval) return;

    // Collect missing sequences from the true head to the highest received.
    std::vector<uint16_t> missing;
    if (!frame.headKnown || frame.packets.empty()) return;
    uint32_t expected = frame.headSeq;
    for (const auto& [seq, packet] : frame.packets) {
        while (seq > expected) {
            if (frame.requested.insert(expected).second || frame.nackRounds > 0)
                missing.push_back(uint16_t(expected));
            ++expected;
        }
        ++expected;
    }
    // A missing tail (marker seen but holes before it) is covered above; if the
    // marker itself is lost we cannot know the tail, the wait window handles it.
    if (missing.empty()) return;

    for (size_t offset = 0; offset < missing.size(); offset += kMaxNackSeqsPerPacket) {
        const size_t count = std::min(kMaxNackSeqsPerPacket, missing.size() - offset);
        const size_t size = rtc::RtcpNack::Size(unsigned(count));
        auto buffer = std::make_shared<rtc::Message>(size, rtc::Message::Control);
        std::fill(buffer->begin(), buffer->end(), std::byte{0});
        auto* nack = reinterpret_cast<rtc::RtcpNack*>(buffer->data());
        nack->preparePacket(ssrc, unsigned(count));
        unsigned fciCount = 0;
        uint16_t fciPid = 0;
        for (size_t i = 0; i < count; ++i)
            nack->addMissingPacket(&fciCount, &fciPid, missing[offset + i]);
        send(buffer);
        ++stats_.nacksSent;
    }
    frame.lastNack = now;
    ++frame.nackRounds;
}

void VideoLossRecovery::flushFrame(Frame& frame, rtc::message_vector& out, bool complete) {
    std::fprintf(stderr, "FLUSH ts=%u complete=%d pkts=%zu head=%u headKnown=%d headArrived=%d marker=%d\n",
        frame.timestamp, complete, frame.packets.size(), frame.headSeq, frame.headKnown, frame.headArrived, frame.markerSeen);
    if (complete) {
        // Hand the packets to the depacketizer in sequence order, one message each;
        // it reassembles by timestamp/marker exactly as with live traffic.
        for (auto& [seq, packet] : frame.packets)
            out.push_back(std::move(packet.message));
    } else {
        ++stats_.framesDropped;
    }
    bufferedPackets_ -= frame.packets.size();
}

void VideoLossRecovery::incoming(rtc::message_vector& messages, const rtc::message_callback& send) {
    std::lock_guard lock(mutex_);
    rtc::message_vector result;
    const auto now = Clock::now();

    for (auto& message : messages) {
        if (message->type != rtc::Message::Binary || message->size() < sizeof(rtc::RtpHeader)) {
            result.push_back(std::move(message)); // RTCP and runt packets pass through
            continue;
        }
        const auto* header = reinterpret_cast<const rtc::RtpHeader*>(message->data());
        if (header->version() != 2) {
            result.push_back(std::move(message));
            continue;
        }
        const uint16_t seq = header->seqNumber();
        const uint32_t timestamp = header->timestamp();
        lastSsrc_ = header->ssrc();
        ++stats_.packetsReceived;

        // Global ordering: a packet older than the previous one arrived out of order.
        const bool hadSeq = haveSeq_;
        const uint16_t prevSeq = lastSeq_;
        if (haveSeq_ && seqDelta(seq, lastSeq_) < 0) ++stats_.packetsReordered;
        if (!haveSeq_ || seqDelta(seq, lastSeq_) > 0) { lastSeq_ = seq; haveSeq_ = true; }

        auto it = frames_.find(timestamp);
        bool createdThisPacket = false;
        if (it == frames_.end()) {
            createdThisPacket = true;
            // A new frame starts: flush every older frame that is complete or expired.
            for (auto old = order_.begin(); old != order_.end();) {
                auto& frame = frames_.at(*old);
                const bool complete = frameComplete(frame);
                const bool expired = frame.gapOpen && now - frame.gapSince >= maxWait_;
                const bool tailLost = !frame.markerSeen && frame.gapOpen && now - frame.gapSince >= maxWait_;
                if (complete || expired || tailLost) {
                    if (!complete && !frame.packets.empty()) {
                        // Try one last NACK round before dropping (bounded by rounds).
                        sendNacks(frame, send, lastSsrc_);
                    }
                    flushFrame(frame, result, complete);
                    frames_.erase(*old);
                    old = order_.erase(old);
                } else {
                    ++old;
                }
            }
            Frame frame;
            frame.timestamp = timestamp;
            frame.firstArrival = now;
            // Head inference happens after unwrap below (needs the extended base).
            it = frames_.emplace(timestamp, std::move(frame)).first;
            order_.push_back(timestamp);
        }
        Frame& frame = it->second;
        const uint32_t unwrapped = unwrap(frame, seq);
        const bool retransmitted = frame.gapOpen && frame.requested.count(unwrapped) > 0;
        if (frame.packets.emplace(unwrapped, Packet{std::move(message), now}).second) {
            ++bufferedPackets_;
            if (retransmitted) ++stats_.packetsRecovered;
        }
        if (header->marker()) frame.markerSeen = true;

        // Head inference from the global sequence anchor. prevSeq is the highest
        // packet seen BEFORE this one. If this packet continues the global stream
        // (delta==1) it is the head of its frame. If it jumped ahead (delta>1) the
        // head is prevSeq+1 (lost or in flight). If it is older (delta<=0) it belongs
        // to an already-started frame and tells us nothing new about the head.
        // Head inference at frame creation when a global anchor exists. Frames
        // created without an anchor (session start) are resolved in the end-of-batch
        // pass below, once all their packets are buffered.
        if (!frame.headKnown && hadSeq && createdThisPacket) {
            const int32_t delta = seqDelta(seq, prevSeq);
            if (delta <= 1) {
                frame.headSeq = unwrapped;
                frame.headKnown = true;
            } else {
                frame.headSeq = uint32_t(int64_t(unwrapped) - (delta - 1));
                frame.headKnown = true;
            }
        }
        if (frame.headKnown && frame.packets.count(frame.headSeq)) frame.headArrived = true;

        // Hole state: with the true head known, any gap from head to the highest
        // received packet is a real loss worth NACKing.
        bool hole = false;
        if (frame.headKnown) {
            uint32_t expected = frame.headSeq;
            for (const auto& [s, p] : frame.packets) {
                if (s != expected) { hole = true; break; }
                ++expected;
            }
            if (!hole && frame.markerSeen && expected != frame.packets.rbegin()->first + 1) hole = true;
        }
        if (hole) {
            if (!frame.gapOpen) { frame.gapOpen = true; frame.gapSince = now; }
            sendNacks(frame, send, lastSsrc_);
        } else {
            frame.gapOpen = false;
            frame.requested.clear();
            frame.nackRounds = 0;
        }

        // Release only when the whole run from the true head is present; the marker
        // alone is not enough because earlier packets may still be in flight.
        if (frameComplete(frame)) {
            flushFrame(frame, result, true);
            frames_.erase(it);
            order_.erase(std::remove(order_.begin(), order_.end(), timestamp), order_.end());
        }
    }

    // End of batch: resolve frames created without a global anchor (session start).
    // Now that all packets are buffered, the lowest received sequence relative to
    // the global stream tells us the head: if it continues the stream it IS the head;
    // if it sits above, the head is the gap start (lost). A contiguous run to the
    // marker with no global context is assumed whole.
    for (auto old = order_.begin(); old != order_.end();) {
        auto& frame = frames_.at(*old);
        if (!frame.headKnown && !frame.packets.empty()) {
            const uint32_t lowest = frame.packets.begin()->first;
            const uint32_t highest = frame.packets.rbegin()->first;
            bool contiguous = true;
            uint32_t expected = lowest;
            for (const auto& [s, p] : frame.packets) {
                if (s != expected) { contiguous = false; break; }
                ++expected;
            }
            const bool runToMarker = contiguous && frame.markerSeen && expected == highest + 1;
            if (runToMarker) {
                // Whole run present; lowest is the head.
                frame.headSeq = lowest;
                frame.headKnown = true;
            } else if (frame.markerSeen && haveSeq_) {
                // Gapped run with marker present: the head is prevGlobal+1. Since
                // lastSeq_ is the highest seen (>= highest of this frame), the head
                // is lastSeq_+1 only if this frame's marker IS the global highest.
                const int32_t highDelta = seqDelta(uint16_t(highest), lastSeq_);
                if (highDelta == 0) {
                    // This frame's marker is the newest packet: head = lowest minus
                    // the gap between the previous frame's end and this frame's start.
                    // We don't know the previous frame's end here, so use the global
                    // anchor: head = lastSeq_+1 - (packets in this frame incl. gaps).
                    // Simpler: the missing head is below lowest; NACK from lowest-1
                    // downward is unsafe. Instead mark head as lowest and let the
                    // hole logic NACK the interior gap; a truly lost head is caught
                    // by the grace window.
                    frame.headSeq = lowest;
                    frame.headKnown = true;
                }
            }
            if (frame.headKnown && frame.packets.count(frame.headSeq)) frame.headArrived = true;
        }
        if (frameComplete(frame)) {
            flushFrame(frame, result, true);
            frames_.erase(*old);
            old = order_.erase(old);
        } else {
            if (frame.headKnown) {
                bool hole = false;
                uint32_t expected = frame.headSeq;
                for (const auto& [s, p] : frame.packets) {
                    if (s != expected) { hole = true; break; }
                    ++expected;
                }
                if (hole) {
                    if (!frame.gapOpen) { frame.gapOpen = true; frame.gapSince = now; }
                    sendNacks(frame, send, lastSsrc_);
                }
            }
            ++old;
        }
    }

    // Time-based flush: expired incomplete frames drop whole; late retransmissions
    // for already-dropped frames are counted and discarded, never fed torn.
    for (auto old = order_.begin(); old != order_.end();) {
        auto& frame = frames_.at(*old);
        // Head still unknown after the grace window: assume the lowest received is
        // the head (no evidence of anything lower) when the run reaches the marker.
        if (!frame.headKnown && frame.markerSeen && !frame.packets.empty() &&
            now - frame.firstArrival >= maxWait_) {
            const uint32_t lowest = frame.packets.begin()->first;
            uint32_t expected = lowest;
            bool contiguous = true;
            for (const auto& [s, p] : frame.packets) {
                if (s != expected) { contiguous = false; break; }
                ++expected;
            }
            if (contiguous && expected == frame.packets.rbegin()->first + 1) {
                frame.headSeq = lowest;
                frame.headKnown = true;
                frame.headArrived = true;
            }
        }
        const bool complete = frameComplete(frame);
        const bool expired = frame.gapOpen && now - frame.gapSince >= maxWait_;
        const bool tailLost = !frame.markerSeen && now - frame.firstArrival >= maxWait_;
        const bool headUnknownExpired = !frame.headKnown && now - frame.firstArrival >= maxWait_;
        if (complete || expired || tailLost || headUnknownExpired) {
            if (!complete) sendNacks(frame, send, lastSsrc_);
            flushFrame(frame, result, complete);
            frames_.erase(*old);
            old = order_.erase(old);
        } else {
            // Still waiting: keep the NACK cadence alive.
            sendNacks(frame, send, lastSsrc_);
            ++old;
        }
    }

    // Hard bounds: too many buffered packets or frames -> drop the oldest incomplete.
    while ((bufferedPackets_ > maxBufferedPackets_ || order_.size() > kMaxBufferedFrames) && !order_.empty()) {
        auto& frame = frames_.at(order_.front());
        flushFrame(frame, result, frameComplete(frame));
        frames_.erase(order_.front());
        order_.erase(order_.begin());
    }

    messages.swap(result);
}

} // namespace veyra::xbox