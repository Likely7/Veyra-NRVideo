// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace veyra::xbox {

struct VideoRtpStats {
    uint64_t received = 0;
    uint64_t recoveredReorder = 0;
    uint64_t duplicate = 0;
    uint64_t expiredMissing = 0;
    uint64_t outsideWindow = 0;
    uint64_t sequenceResets = 0;
    unsigned pendingMissing = 0;
};

// The native H.264 track carries one video SSRC at a time. Record continuity
// before depacketization without changing packet delivery or requesting retransmits.
class VideoRtpReceiveWindow {
public:
    const VideoRtpStats& observe(uint32_t ssrc, uint16_t sequence) {
        ++stats_.received;
        if (!span_) {
            start(ssrc, sequence);
            return stats_;
        }

        const unsigned ahead = uint16_t(sequence - highest_);
        const unsigned behind = uint16_t(highest_ - sequence);
        if (ssrc == ssrc_ && ahead != 0 && ahead < kMaxForward) {
            haveCandidate_ = false;
            advance(sequence, ahead);
        } else if (ssrc == ssrc_ && behind <= kMaxLate) {
            if (behind >= span_) {
                ++stats_.outsideWindow;
            } else if (const uint64_t marker = uint64_t{1} << behind; seen_ & marker) {
                ++stats_.duplicate;
            } else {
                seen_ |= marker;
                ++stats_.recoveredReorder;
                --stats_.pendingMissing;
            }
        } else if (haveCandidate_ && ssrc == candidateSsrc_ && sequence == uint16_t(candidateSequence_ + 1)) {
            // Treat an SSRC change or large sequence jump as a new epoch only
            // after consecutive packets. Pending gaps from the old epoch stay unconfirmed.
            ++stats_.sequenceResets;
            start(ssrc, candidateSequence_);
            advance(sequence, 1);
        } else {
            candidateSsrc_ = ssrc;
            candidateSequence_ = sequence;
            haveCandidate_ = true;
            ++stats_.outsideWindow;
        }
        return stats_;
    }

private:
    static constexpr unsigned kWindow = 64;
    static constexpr unsigned kMaxForward = 3000;
    static constexpr unsigned kMaxLate = 100;

    void start(uint32_t ssrc, uint16_t sequence) {
        ssrc_ = ssrc;
        highest_ = sequence;
        span_ = 1;
        seen_ = 1;
        stats_.pendingMissing = 0;
        haveCandidate_ = false;
    }

    void advance(uint16_t sequence, unsigned distance) {
        if (span_ + distance > kWindow) {
            const unsigned kept = distance < kWindow ? kWindow - distance : 0;
            stats_.expiredMissing += span_ - kept - unsigned(std::popcount(seen_ >> kept));
            if (distance > kWindow) stats_.expiredMissing += distance - kWindow;
        }
        seen_ = distance < kWindow ? (seen_ << distance) | 1 : 1;
        span_ = std::min(kWindow, span_ + distance);
        highest_ = sequence;
        stats_.pendingMissing = span_ - unsigned(std::popcount(seen_));
    }

    VideoRtpStats stats_;
    uint64_t seen_ = 0;
    uint32_t ssrc_ = 0, candidateSsrc_ = 0;
    uint16_t highest_ = 0, candidateSequence_ = 0;
    unsigned span_ = 0;
    bool haveCandidate_ = false;
};

} // namespace veyra::xbox
