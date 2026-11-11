#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace veyra::engine {
// Preview-only correction for a raw capture clock whose timestamps drift
// significantly from a stable callback cadence. Times are in 100 ns units.
class CapturePreviewClock {
    struct Sample {
        uint64_t sequence = 0;
        int64_t pts = 0;
        int64_t arrival = 0;
    };
    Sample previous_, window_;
    int64_t correctedPts_ = 0;
    int64_t anchorPts_ = 0;
    int64_t anchorArrival_ = 0;
    bool correcting_ = false;

public:
    struct Result {
        int64_t pts;
        bool corrected;
        bool discontinuity;
    };

    bool correcting() const { return correcting_; }

    Result observe(uint64_t sequence, int64_t pts, int64_t arrival, int64_t period, bool boundary) {
        const Sample sample{sequence, pts, arrival};
        if (!previous_.sequence || boundary || sequence <= previous_.sequence || period <= 0 ||
            arrival <= previous_.arrival || pts <= previous_.pts) {
            return restart(sample, correcting_);
        }

        const auto count = sequence - previous_.sequence;
        const int64_t arrivalSpan = arrival - previous_.arrival;
        const int64_t expected = static_cast<int64_t>(count) * period;
        const int64_t slack = period / 3 + expected / 40;
        if (std::llabs(arrivalSpan - expected) > slack) {
            // Sequence gaps can come from a late reader. Only excess wall time
            // establishes a source stall when the driver's PTS hides it.
            return restart(sample, correcting_ || arrivalSpan - expected > 1000000);
        }

        bool changed = false;
        const auto windowCount = sequence - window_.sequence;
        if (windowCount >= 16) {
            const int64_t hostSpan = arrival - window_.arrival;
            const int64_t ptsSpan = pts - window_.pts;
            const int64_t expectedSpan = static_cast<int64_t>(windowCount) * period;
            const bool stable = std::llabs(hostSpan - expectedSpan) <= expectedSpan / 40;
            const int64_t error = std::llabs(ptsSpan - hostSpan);
            if (correcting_ && (!stable || error <= hostSpan / 20))
                return restart(sample, true);
            if (!correcting_ && stable && error >= hostSpan / 8) {
                correcting_ = changed = true;
                anchorPts_ = correctedPts_ = previous_.pts + expected;
                anchorArrival_ = arrival;
            }
            window_ = sample;
        }

        previous_ = sample;
        if (!correcting_) return {pts, false, false};
        if (!changed) {
            correctedPts_ += expected;
            // Follow slow drift against the callback clock while rejecting the
            // per-frame delivery jitter. The result advances monotonically.
            const auto phaseError = anchorPts_ + arrival - anchorArrival_ - correctedPts_;
            correctedPts_ += std::clamp(phaseError / 16, -period / 64, period / 64);
        }
        return {correctedPts_, true, changed};
    }

private:
    Result restart(Sample sample, bool discontinuity) {
        previous_ = window_ = sample;
        correcting_ = false;
        return {sample.pts, false, discontinuity};
    }
};
}
