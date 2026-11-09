#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace veyra::engine {
// Chooses the NR inference reuse level in Auto (pipeline/NrReuse.h): 0 infers
// every frame, 1/2/3 reuse one frame in every 4/3/2.
//
// Unlike the NR size pool a level change costs nothing (no instance rebuild,
// no drained presentation), so this reacts within about a second. It only
// steps down when the lower level is predicted to fit with room to spare,
// using the measured NR inference time, so it does not bounce.
//
// Inputs come from the same one-second FrameFlowWindow:
//   meanMs - mean enhancement processing per source frame. Reuse lowers the
//            mean; the P95 stays an inferred frame's cost, so it cannot judge.
//   nrMs   - mean NR inference time of an inferred frame, all layers.
//   budget - 80% of the transport frame interval, as for the size pool.
class AutoNrReuseController {
public:
    static constexpr unsigned kMaxLevel = 3;
    static constexpr double reusedShare(unsigned level) {
        return level == 1 ? 0.25 : level == 2 ? 1.0 / 3.0 : level == 3 ? 0.5 : 0.0;
    }
    void reset(int64_t now100ns, unsigned level = 0) {
        level_ = level > kMaxLevel ? 0 : level;
        discardObservations(now100ns);
    }
    void discardObservations(int64_t now100ns) {
        next_ = now100ns + kSettle;
        overSince_.reset();
        lowSince_.reset();
    }
    std::optional<unsigned> observe(int64_t now100ns, std::optional<double> meanMs, uint64_t samples,
                                    std::optional<double> nrMs, double budgetMs) {
        if (now100ns < next_) return {};
        next_ = now100ns + kStep;
        if (!meanMs || !std::isfinite(*meanMs) || *meanMs <= 0 || samples < 8 ||
            !std::isfinite(budgetMs) || budgetMs <= 0) {
            overSince_.reset();
            lowSince_.reset();
            return {};
        }
        const bool over = *meanMs > budgetMs;
        if (over) { if (!overSince_) overSince_ = now100ns; }
        else overSince_.reset();
        bool fitsLower = false;
        if (level_ > 0 && !over) {
            // The inferences one level down would add back. Without an NR
            // measurement assume the whole mean, which only delays a step down.
            const double perInference = nrMs && std::isfinite(*nrMs) && *nrMs > 0 ? *nrMs : *meanMs;
            const double added = (reusedShare(level_) - reusedShare(level_ - 1)) * perInference;
            fitsLower = *meanMs + added < budgetMs * kLowerFit;
        }
        if (fitsLower) { if (!lowSince_) lowSince_ = now100ns; }
        else lowSince_.reset();
        if (over && level_ < kMaxLevel && now100ns - *overSince_ >= kRaiseAfter) return change(now100ns, level_ + 1);
        if (fitsLower && now100ns - *lowSince_ >= kLowerAfter) return change(now100ns, level_ - 1);
        return {};
    }
    unsigned level() const { return level_; }

private:
    // The flow window spans one second and refreshes every 0.25 s: after a
    // change, wait until it holds only frames measured at the new level.
    static constexpr int64_t kSettle = 12500000, kStep = 2500000;
    static constexpr int64_t kRaiseAfter = 5000000, kLowerAfter = 30000000;
    static constexpr double kLowerFit = 0.85;
    std::optional<unsigned> change(int64_t now100ns, unsigned level) {
        level_ = level;
        discardObservations(now100ns);
        return level_;
    }
    unsigned level_ = 0;
    int64_t next_ = 0;
    std::optional<int64_t> overSince_, lowSince_;
};
}
