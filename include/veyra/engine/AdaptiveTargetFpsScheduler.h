// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/DisplayFgBudget.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace veyra::engine {

// Single-pass adaptive target-FPS frame-time scheduler.
//
// Console games often run at variable frame rates (e.g. a demanding 30 fps
// title dipping to 24-29.5 fps). Integer-multiplier frame generation (2X) turns
// 27 fps into 54 fps, causing judder on a fixed 60 Hz display. Pre-interpolating
// before FG would double-interpolate and compound artifacts + latency.
//
// Instead, this scheduler drives a single interpolation stage directly onto a
// locked target-Hz output grid (e.g. 60.00 Hz, period = 16.667 ms). For each
// real source pair (ptsA, ptsB), it emits the exact output slots whose target
// display ticks fall in (ptsA, ptsB] along with each slot's normalized phase
// t = (tick - ptsA) / (ptsB - ptsA) in (0, 1].
//
// - Backends that accept an arbitrary float timestep (such as RifeDmlBackend
//   via setTimestep/generateSlot) evaluate `slot.phase` directly.
// - Backends restricted to discrete integer subframes (NVIDIA DLSS MFG up to
//   6X, VFG up to 8X) can map `slot.phase` via quantizePhaseToMultiFrame().
//
// Units: 100 ns (consistent with Veyra's engine timeline).
class AdaptiveTargetFpsScheduler {
public:
    static constexpr unsigned kMaxSlotsPerPair = 8;

    struct Config {
        double targetFps = 60.0;
        float snapEpsilon = 0.04f;          // snap to real A/B within 4% of pair span
        int64_t maxPairSpan100ns = 2000000; // >200 ms stall re-anchors cleanly
        unsigned maxFramesPerPair = kMaxSlotsPerPair;
    };

    struct OutputSlot {
        int64_t presentPts100ns = 0;
        float phase = 1.0f; // normalized position in [0, 1] between A (0) and B (1)
        bool useRealA = false;
        bool useRealB = false;
        bool interpolated() const { return !useRealA && !useRealB; }
    };

    struct Plan {
        std::array<OutputSlot, kMaxSlotsPerPair> slots{};
        unsigned count = 0;
        bool reanchored = false;
    };

    AdaptiveTargetFpsScheduler() = default;
    explicit AdaptiveTargetFpsScheduler(Config cfg) : cfg_(cfg) {}

    void configure(Config cfg) {
        cfg_ = cfg;
        cfg_.maxFramesPerPair = std::clamp(cfg_.maxFramesPerPair, 1u, kMaxSlotsPerPair);
        cfg_.snapEpsilon = std::clamp(cfg_.snapEpsilon, 0.0f, 0.25f);
        reset();
    }

    const Config& config() const { return cfg_; }

    void reset() {
        anchored_ = false;
        nextTick_ = 0.0;
    }

    Plan planPair(int64_t ptsA100ns, int64_t ptsB100ns, bool discontinuity = false) {
        Plan plan{};
        if (!std::isfinite(cfg_.targetFps) || cfg_.targetFps <= 0.0) {
            plan.slots[0] = OutputSlot{ptsB100ns, 1.0f, false, true};
            plan.count = 1;
            plan.reanchored = true;
            return plan;
        }

        const double period = 10000000.0 / cfg_.targetFps;
        const int64_t span = ptsB100ns - ptsA100ns;
        const unsigned maxSlots = std::clamp(cfg_.maxFramesPerPair, 1u, kMaxSlotsPerPair);

        if (!anchored_ || discontinuity || span <= 0 || span > cfg_.maxPairSpan100ns) {
            // Re-anchor cleanly on startup, scene cut, or stall: emit real B
            // without smearing across unrelated pictures, and schedule the next
            // display tick one exact period after ptsB.
            anchored_ = true;
            nextTick_ = double(ptsB100ns) + period;
            plan.slots[0] = OutputSlot{ptsB100ns, 1.0f, false, true};
            plan.count = 1;
            plan.reanchored = true;
            return plan;
        }

        // Guard against tiny backward drift if an upstream clock nudged ptsA forward.
        if (nextTick_ <= double(ptsA100ns)) {
            const double behind = double(ptsA100ns) - nextTick_;
            const double steps = std::floor(behind / period) + 1.0;
            nextTick_ += steps * period;
        }

        const double endLimit = double(ptsB100ns) + period * 1e-6;
        while (nextTick_ <= endLimit && plan.count < maxSlots) {
            const double rawT = (nextTick_ - double(ptsA100ns)) / double(span);
            const float t = float(std::clamp(rawT, 0.0, 1.0));
            OutputSlot slot{};
            slot.presentPts100ns = int64_t(std::llround(nextTick_));
            if (t <= cfg_.snapEpsilon) {
                slot.phase = 0.0f;
                slot.useRealA = true;
            } else if (t >= 1.0f - cfg_.snapEpsilon) {
                slot.phase = 1.0f;
                slot.useRealB = true;
            } else {
                slot.phase = t;
            }
            plan.slots[plan.count++] = slot;
            nextTick_ += period;
        }

        // If a very fast source pair fell strictly between two display ticks
        // (e.g. source temporarily faster than targetFps), plan.count == 0
        // intentionally skips presenting an extra off-grid frame.
        return plan;
    }

private:
    Config cfg_{};
    bool anchored_ = false;
    double nextTick_ = 0.0;
};

struct QuantizedSubframe {
    bool useRealA = false;
    bool useRealB = false;
    unsigned multiplier = 1;
    unsigned subframeIndex = 0; // 1-based in [1, multiplier - 1] when interpolated
    float actualPhase = 1.0f;
    float error = 0.0f; // |actualPhase - requestedPhase|
};

// Maps an arbitrary requested phase in [0, 1] to the closest selectable discrete
// subframe (sub / multiplier) for backends that do not expose continuous float t
// (e.g. RTX 50-series NVIDIA DLSS MFG 6X offers {1/6, 1/4, 1/3, 1/2, 2/3, 3/4, 5/6},
// bounding quantization error to <= 1/12 ~= 2.8 ms on a 30 fps source pair).
inline QuantizedSubframe quantizePhaseToMultiFrame(
    float phase,
    FrameGenerationBackend backend,
    unsigned maxMultiplier,
    float snapEpsilon = 0.04f)
{
    const float clamped = std::clamp(phase, 0.0f, 1.0f);
    QuantizedSubframe best{};

    // Start with the nearest real endpoint (A at 0.0 or B at 1.0).
    if (clamped <= 0.5f) {
        best.useRealA = true;
        best.actualPhase = 0.0f;
        best.error = clamped;
    } else {
        best.useRealB = true;
        best.actualPhase = 1.0f;
        best.error = 1.0f - clamped;
    }

    if (clamped <= snapEpsilon || clamped >= 1.0f - snapEpsilon || maxMultiplier < 2) {
        return best;
    }

    const unsigned cap = std::min(maxMultiplier, kDisplayFgMultiplierCeiling);
    for (unsigned m = 2; m <= cap; ++m) {
        if (!displayFgMultiplierSelectable(backend, m)) continue;
        for (unsigned sub = 1; sub < m; ++sub) {
            const float candidate = float(sub) / float(m);
            const float err = std::abs(candidate - clamped);
            // Prefer strictly lower error; on a tie (e.g. 1/2 vs 2/4 vs 3/6),
            // keep the lowest multiplier so GPU work stays minimal.
            if (err < best.error - 1e-6f) {
                best.useRealA = false;
                best.useRealB = false;
                best.multiplier = m;
                best.subframeIndex = sub;
                best.actualPhase = candidate;
                best.error = err;
            }
        }
    }
    return best;
}

} // namespace veyra::engine
