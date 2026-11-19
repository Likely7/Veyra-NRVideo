// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra::remoteplay {
// How the DualSense Bluetooth haptics sender fills its reports.
//
// The pad plays one 32-frame block per report (10.667 ms at 3 kHz). The audio for the blocks
// reaches the sender through the UI timer in uneven batches (nothing, 10 ms or 20 ms of audio
// per 8 ms tick), and the console's 3 kHz clock is not this PC's clock. The sender therefore
// needs a reserve of queued audio, and the reserve has to be kept:
//   - a stream starts when kStartFrames are queued, or when a shorter effect has stopped
//     arriving;
//   - when the queue came close to empty during the last reports, 31 source frames are
//     stretched to a block, which adds one frame of reserve per report;
//   - when the reserve stayed high, 33 frames are squeezed into a block;
//   - when there is not enough audio although it is still arriving, the report waits. The
//     last partial block of an effect waits the same way, for at most 40 ms, when its audio
//     simply stops: until then a stop cannot be told from a delay.
// A 3% change of length is far below anything a vibration reveals. Without the reserve
// control a sender that starts with about 60 frames queued waits for audio at many reports:
// at 7.0% of 180775 in a 98-minute field session, at up to 74% within five seconds of
// continuous audio. With it: at 0.5% of 60744 in 49 minutes, at most 1.6% within five
// seconds of continuous audio.
class HapticsBlockPacer {
public:
    static constexpr unsigned kBlock = 32, kStartFrames = 96;
    static constexpr unsigned kLowEnter = 44, kLowExit = 76, kHighEnter = 200, kHighExit = 150;
    static constexpr unsigned kLong = 64, kShort = 16;
    struct Step {
        bool send = false;    // audio goes into a report now
        bool starved = false; // not enough audio although it is still arriving: wait
        unsigned take = 0;    // source frames to take from the queue (at most 33)
    };
    void reset() { flowing_ = false; shortEffect_ = false; count_ = 0; head_ = 0; mode_ = 0; }
    // Smallest queue seen right before a report within the last `reports` reports (frames).
    unsigned reserve(unsigned reports = kLong) const {
        const unsigned n = std::min(reports, count_);
        if (!n) return 0;
        unsigned low = ~0u;
        for (unsigned i = 1; i <= n; ++i) low = std::min(low, levels_[(head_ + kLong - i) % kLong]);
        return low;
    }
    // queued: frames in the queue. sinceLastPushMs: time since audio was last added.
    // waitedMs: how long the oldest queued frame has waited (only used before the start).
    Step next(unsigned queued, unsigned sinceLastPushMs, unsigned waitedMs) {
        Step step;
        const bool arriving = sinceLastPushMs < 28;
        if (!flowing_) {
            // Hold a starting effect until the reserve is there. A shorter effect goes out
            // once its audio has stopped arriving; a trickle is not held for ever.
            if (!queued || (queued < kStartFrames && arriving && waitedMs < 80)) return step;
            flowing_ = true; count_ = 0; head_ = 0; mode_ = 0;
            // A short effect that is already complete is sent out without waiting for more.
            shortEffect_ = queued < kStartFrames && !arriving;
        }
        if (arriving) shortEffect_ = false;
        const unsigned want = mode_ > 0 ? kBlock - 1 : mode_ < 0 ? kBlock + 1 : kBlock;
        if (queued < want) {
            if (!shortEffect_ && sinceLastPushMs < 40) { step.starved = true; return step; }
            // The audio has stopped: the rest of the effect goes out, padded with silence.
            if (!queued) { flowing_ = false; return step; }
        }
        step.send = true; step.take = std::min(queued, want);
        observe(queued);
        return step;
    }

private:
    void observe(unsigned level) {
        levels_[head_] = level; head_ = (head_ + 1) % kLong; count_ = std::min(count_ + 1, kLong);
        if (count_ < kShort) return;
        const unsigned recent = reserve(kShort);
        if (mode_ > 0) { if (recent >= kLowExit) mode_ = 0; }
        else if (mode_ < 0) { if (recent <= kHighExit) mode_ = 0; }
        else if (recent < kLowEnter) mode_ = 1;
        else if (count_ == kLong && reserve(kLong) > kHighEnter) mode_ = -1;
    }
    std::array<unsigned, kLong> levels_{};
    unsigned count_ = 0, head_ = 0;
    int mode_ = 0; // 0 normal, +1 stretching (reserve low), -1 squeezing (reserve high)
    bool flowing_ = false, shortEffect_ = false; // flowing_: an effect has started and its queue has not run empty
};

// `count` interleaved stereo frames (1..33) to exactly 32. 31 and 33 frames are resampled
// with linear interpolation (first and last frame kept, so consecutive blocks join without a
// step); 32 are copied; anything shorter is the end of an effect and is padded with silence.
inline void hapticsFitBlock(const std::int16_t* in, unsigned count, std::int16_t* out) {
    constexpr unsigned block = HapticsBlockPacer::kBlock;
    if (count < block - 1 || count == block) {
        const unsigned n = std::min(count, block);
        std::copy_n(in, std::size_t(n) * 2, out);
        std::fill(out + std::size_t(n) * 2, out + std::size_t(block) * 2, std::int16_t(0));
        return;
    }
    count = std::min(count, block + 1);
    for (unsigned i = 0; i < block; ++i) {
        // Position in the source, in 1/31 frames.
        const unsigned scaled = i * (count - 1), index = scaled / (block - 1), part = scaled % (block - 1);
        const unsigned after = std::min(index + 1, count - 1);
        for (unsigned channel = 0; channel < 2; ++channel) {
            const int a = in[std::size_t(index) * 2 + channel], b = in[std::size_t(after) * 2 + channel];
            const int mixed = a * int(block - 1 - part) + b * int(part);
            const int half = int(block - 1) / 2;
            out[std::size_t(i) * 2 + channel] = static_cast<std::int16_t>((mixed >= 0 ? mixed + half : mixed - half) / int(block - 1));
        }
    }
}
} // namespace veyra::remoteplay
