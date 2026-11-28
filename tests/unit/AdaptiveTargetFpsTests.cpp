// SPDX-License-Identifier: GPL-3.0-only
// Unit tests for single-pass adaptive target-FPS (60 Hz) fractional scheduling
// and multi-frame quantized subframe selection.
#include "veyra/engine/AdaptiveTargetFpsScheduler.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace veyra::engine;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

static void verifyLockedTargetRate(double sourceFps, double targetFps, unsigned pairs, const char* label) {
    AdaptiveTargetFpsScheduler sched({targetFps, 0.04f, 2000000, 8});
    const double srcPeriod = 10000000.0 / sourceFps;
    const double dstPeriod = 10000000.0 / targetFps;

    int64_t prevOutPts = -1;
    uint64_t totalOut = 0;
    double maxTickJitterMs = 0.0;
    bool monotonic = true, validPhases = true;

    for (unsigned i = 0; i < pairs; ++i) {
        const int64_t ptsA = int64_t(std::llround(double(i) * srcPeriod));
        const int64_t ptsB = int64_t(std::llround(double(i + 1) * srcPeriod));
        const auto plan = sched.planPair(ptsA, ptsB);
        for (unsigned s = 0; s < plan.count; ++s) {
            const auto& slot = plan.slots[s];
            if (slot.phase < 0.0f || slot.phase > 1.0f) validPhases = false;
            if (prevOutPts >= 0) {
                if (slot.presentPts100ns <= prevOutPts) monotonic = false;
                const double dtMs = double(slot.presentPts100ns - prevOutPts) / 10000.0;
                maxTickJitterMs = std::max(maxTickJitterMs, std::abs(dtMs - dstPeriod / 10000.0));
            }
            prevOutPts = slot.presentPts100ns;
            ++totalOut;
        }
    }

    const double totalSec = double(pairs) / sourceFps;
    const double measuredFps = double(totalOut - 1) / totalSec;
    check(monotonic && validPhases, std::string(label) + ": monotonic ticks and valid phases");
    check(maxTickJitterMs < 0.05, std::string(label) + ": exact 16.67ms grid (maxJitterMs=" + std::to_string(maxTickJitterMs) + ")");
    check(std::abs(measuredFps - targetFps) < 0.15,
          std::string(label) + ": locked target FPS (got " + std::to_string(measuredFps) + ", want " + std::to_string(targetFps) + ")");
}

int main() {
    // 1. Exact 30 -> 60 Hz produces alternating 0.5 interpolated + real B frames.
    {
        AdaptiveTargetFpsScheduler sched({60.0, 0.04f, 2000000, 8});
        const auto p0 = sched.planPair(0, 333333);
        check(p0.reanchored && p0.count == 1 && p0.slots[0].useRealB, "first pair anchors on real B");
        const auto p1 = sched.planPair(333333, 666667);
        check(!p1.reanchored && p1.count == 2 &&
              p1.slots[0].interpolated() && std::abs(p1.slots[0].phase - 0.5f) < 0.01f &&
              p1.slots[1].useRealB,
              "30 -> 60 Hz pair emits exact t=0.5 interpolated + real B");
    }

    // 2. Constant under-rate console streams lock cleanly to 60.00 Hz.
    verifyLockedTargetRate(30.0, 60.0, 600, "30.0 fps -> 60 Hz");
    verifyLockedTargetRate(29.5, 60.0, 600, "29.5 fps -> 60 Hz");
    verifyLockedTargetRate(27.0, 60.0, 600, "27.0 fps -> 60 Hz");
    verifyLockedTargetRate(24.0, 60.0, 600, "24.0 fps -> 60 Hz");

    // 3. Wildly varying 22-30 fps gameplay trace still outputs exact locked 60.00 Hz.
    {
        AdaptiveTargetFpsScheduler sched({60.0, 0.04f, 2000000, 8});
        const double rates[] = {30.0, 29.5, 27.0, 22.0, 25.0, 28.5, 30.0, 24.0};
        int64_t ptsA = 0, prevOut = -1;
        uint64_t totalOut = 0;
        double maxJitterMs = 0.0;
        bool ok = true;
        for (double fps : rates) {
            for (int f = 0; f < 90; ++f) {
                const int64_t ptsB = ptsA + int64_t(std::llround(10000000.0 / fps));
                const auto plan = sched.planPair(ptsA, ptsB);
                for (unsigned s = 0; s < plan.count; ++s) {
                    if (prevOut >= 0) {
                        const double dtMs = double(plan.slots[s].presentPts100ns - prevOut) / 10000.0;
                        maxJitterMs = std::max(maxJitterMs, std::abs(dtMs - 1000.0 / 60.0));
                        if (plan.slots[s].presentPts100ns <= prevOut) ok = false;
                    }
                    prevOut = plan.slots[s].presentPts100ns;
                    ++totalOut;
                }
                ptsA = ptsB;
            }
        }
        const double measuredFps = double(totalOut - 1) / (double(ptsA) / 10000000.0);
        check(ok && maxJitterMs < 0.05 && std::abs(measuredFps - 60.0) < 0.2,
              "variable 22-30 fps gameplay locks to 60.00 Hz (got " + std::to_string(measuredFps) +
              " fps, maxJitterMs=" + std::to_string(maxJitterMs) + ")");
    }

    // 4. Stall / discontinuity cleanly re-anchors without smearing.
    {
        AdaptiveTargetFpsScheduler sched({60.0, 0.04f, 2000000, 8});
        (void)sched.planPair(0, 333333);
        const auto stall = sched.planPair(333333, 3500000); // >200 ms stall
        check(stall.reanchored && stall.count == 1 && stall.slots[0].useRealB,
              "stall >200ms re-anchors cleanly on real B");
        const auto cut = sched.planPair(3500000, 3833333, true);
        check(cut.reanchored && cut.count == 1 && cut.slots[0].useRealB,
              "explicit discontinuity re-anchors cleanly on real B");
    }

    // 5. DLSS 6X (RTX 50-series) and VFG 8X quantized subframe mapping.
    {
        float maxDlssErr = 0.0f;
        bool dlssNever5x = true;
        for (int i = 0; i <= 100; ++i) {
            const float t = float(i) / 100.0f;
            const auto q = quantizePhaseToMultiFrame(t, FrameGenerationBackend::Dlss, 6);
            maxDlssErr = std::max(maxDlssErr, q.error);
            if (q.multiplier == 5) dlssNever5x = false;
        }
        check(dlssNever5x, "DLSS 6X quantization never selects unsupported 5X");
        // Worst gap on {0, 1/6, 1/4, 1/3, 1/2, 2/3, 3/4, 5/6, 1} is 1/12 = 0.0833 (~2.8 ms at 30 fps).
        check(maxDlssErr <= (1.0f / 12.0f + 1e-4f),
              "DLSS 6X max phase quantization error <= 1/12 (got " + std::to_string(maxDlssErr) + ")");

        const auto half = quantizePhaseToMultiFrame(0.50f, FrameGenerationBackend::Dlss, 6);
        check(!half.useRealA && !half.useRealB && half.multiplier == 2 && half.subframeIndex == 1,
              "t=0.50 chooses cheapest 2X subframe 1/2 instead of 3/6");

        const auto nearEnd = quantizePhaseToMultiFrame(0.98f, FrameGenerationBackend::Dlss, 6);
        check(nearEnd.useRealB && nearEnd.multiplier == 1, "t=0.98 snaps to real B");
    }

    std::printf("%s\n", failures ? "AdaptiveTargetFps tests FAILED" : "AdaptiveTargetFps tests passed");
    return failures ? 1 : 0;
}
