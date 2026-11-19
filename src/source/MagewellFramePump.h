#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace veyra::source::magewell {

enum class ArmResult { NoFrame, Armed, Stop };

// One DMA at a time. If the next buffering notification is already pending, start
// its DMA in the other slot before copying the completed frame into Veyra's mailbox.
// With no notification pending, publish immediately. This introduces no frame queue.
template <class Arm, class Complete, class Publish>
void pumpFrames(Arm&& arm, Complete&& complete, Publish&& publish) {
    unsigned slot = 0;
    bool armed = false;
    for (;;) {
        if (!armed) {
            const auto result = arm(slot, true);
            if (result == ArmResult::Stop) return;
            if (result == ArmResult::NoFrame) continue;
        }
        if (!complete(slot)) return;
        const auto next = arm(slot ^ 1u, false);
        if (next == ArmResult::Stop || !publish(slot)) return;
        slot ^= 1u;
        armed = next == ArmResult::Armed;
    }
}

inline int partialLinesFor(unsigned height, size_t frameBytes) {
    // Full-size 4K 10-bit/RGB frames use a coarser partial notification.
    // The SDK still starts DMA while the image is being buffered.
    return height >= 2160 && frameBytes >= 16u * 1024 * 1024 ? 256 : 64;
}

// Device time and undelivered capture slots are distinct from host callback FPS.
// Require both the input interval and the card's ring index to confirm missing
// frames. A slower HDMI mode or irregular input alone is not a fallback trigger.
class CaptureCadence {
public:
    struct Result {
        unsigned missed = 0;
        bool fallback = false;
    };

    Result observe(int64_t pts, uint32_t period, unsigned index, unsigned ringSize, bool boundary) {
        const bool hadPrevious = previousPts_ >= 0;
        const int64_t delta = hadPrevious && pts >= previousPts_ ? pts - previousPts_ : 0;
        const unsigned previousIndex = previousIndex_;
        const auto periodChange = std::max(previousPeriod_, period) - std::min(previousPeriod_, period);
        const bool sameMode = periodChange <= period / 100 && previousRingSize_ == ringSize;
        previousPts_ = pts;
        previousPeriod_ = period;
        previousIndex_ = index;
        previousRingSize_ = ringSize;
        if (boundary || !sameMode || !period || !delta || delta > 5 * int64_t(period) || ringSize < 2 ||
            index >= ringSize || previousIndex >= ringSize) {
            resetWindow();
            return {};
        }

        const auto slots = (delta + period / 2) / period;
        const auto distance = (index + uint64_t(ringSize) - previousIndex) % ringSize;
        const auto expectedTime = slots * period;
        if (slots < 1 || slots > 4 || slots >= ringSize || uint64_t(slots) != distance ||
            delta < expectedTime - period / 6 || delta > expectedTime + period / 6) {
            resetWindow();
            return {};
        }

        const auto missed = static_cast<unsigned>(slots - 1);
        missed_ += missed;
        slots_ += static_cast<unsigned>(slots);
        windowTime_ += delta;
        if (windowTime_ < 20'000'000) return {missed, false};
        const bool unhealthy = missed_ >= 3 && missed_ * 100 >= slots_ * 3;
        unhealthyWindows_ = unhealthy ? unhealthyWindows_ + 1 : 0;
        missed_ = slots_ = 0;
        windowTime_ = 0;
        return {missed, unhealthyWindows_ >= 2};
    }

private:
    void resetWindow() {
        windowTime_ = 0;
        missed_ = slots_ = unhealthyWindows_ = 0;
    }

    int64_t previousPts_ = -1, windowTime_ = 0;
    uint32_t previousPeriod_ = 0;
    unsigned previousIndex_ = 0, previousRingSize_ = 0;
    unsigned missed_ = 0, slots_ = 0, unhealthyWindows_ = 0;
};

} // namespace veyra::source::magewell
