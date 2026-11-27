// SPDX-License-Identifier: GPL-3.0-only
// Regression tests for PS5 Remote Play VFR clock tracking and Auto Audio Sync.
#include "veyra/remoteplay/Timeline.h"
#include "veyra/sink/CaptureSyncTarget.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace veyra::remoteplay;
using namespace veyra::sink;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

int main() {
    // 1. Steady 30 fps + +/-1400 ppm crystal drift and jitter stays tightly locked.
    for (unsigned fps : {30u, 60u}) {
        for (int ppm : {-1400, 1400}) {
            RemotePlayClock clk;
            clk.reset(0, fps);
            int64_t prevPts = -1;
            double maxErrMs = 0;
            bool monotonic = true, noGap = true;
            for (uint64_t i = 0; i < fps * 120u; ++i) {
                const int64_t base = int64_t(i * uint64_t(1000000 + ppm) * 10 / fps);
                const int64_t jitter = (i % 97 == 11) ? 20000 : int64_t(i % 7) * 1000;
                const auto st = clk.video(i, base + jitter);
                if (!st || st->pts100ns <= prevPts) monotonic = false;
                if (st && st->discontinuity) noGap = false;
                if (st) {
                    prevPts = st->pts100ns;
                    if (i > fps * 5u) {
                        maxErrMs = std::max(maxErrMs, std::abs(double(st->pts100ns - base)) / 10000.0);
                    }
                }
            }
            check(monotonic && noGap && maxErrMs < 15.0,
                  "steady " + std::to_string(fps) + "fps ppm=" + std::to_string(ppm) +
                  " maxErrMs=" + std::to_string(maxErrMs));
        }
    }

    // 2. Reproduces the live PS5 29.5 fps gameplay signature (04:29-04:31 log):
    // previously snowballed arrivalMinusPtsMs past 1,500 ms in ~2 minutes.
    {
        RemotePlayClock clk;
        CaptureSyncTarget sync;
        const HostTime origin = 237512080000LL;
        clk.reset(origin, 30);
        const double ingressMs = double(origin) / 10000.0;
        const double pipelineMs = 50.0;
        int64_t prevPts = -1;
        double maxLagMs = 0, maxCompErrMs = 0;
        bool monotonic = true, noGap = true;

        // 300 seconds at 29.50 fps (8,850 frames).
        for (uint64_t i = 0; i < 8850; ++i) {
            const HostTime arrival = origin + int64_t(std::llround(double(i) * (10000000.0 / 29.5)));
            const auto st = clk.video(i, arrival);
            if (!st || st->pts100ns <= prevPts) monotonic = false;
            if (st && st->discontinuity) noGap = false;
            if (!st) continue;
            prevPts = st->pts100ns;

            const double arrivalMs = double(arrival) / 10000.0;
            const double hostMs = arrivalMs + pipelineMs;
            const double vPtsMs = double(st->pts100ns) / 10000.0;
            const double lagMs = (arrivalMs - ingressMs) - vPtsMs;
            maxLagMs = std::max(maxLagMs, std::abs(lagMs));

            const auto res = sync.observe(hostMs - vPtsMs - ingressMs, hostMs, arrivalMs);
            if (i > 60) {
                maxCompErrMs = std::max(maxCompErrMs, std::abs(res.targetMs - pipelineMs));
            }
        }
        check(monotonic && noGap, "29.5 fps sustained VFR stays monotonic with zero discontinuities");
        check(maxLagMs < 20.0, "29.5 fps 5-min session arrivalMinusPtsMs bounded <20ms (got " + std::to_string(maxLagMs) + ")");
        check(maxCompErrMs < 20.0, "29.5 fps Auto Audio Sync stays within 20ms of true 50ms pipeline (maxErr=" + std::to_string(maxCompErrMs) + ")");
    }

    // 3. Heavy VFR swings (24 fps -> 30 fps -> 26 fps -> 30 fps) stay bounded and recover cleanly.
    {
        RemotePlayClock clk;
        const HostTime origin = 100000000LL;
        clk.reset(origin, 30);
        HostTime now = origin;
        int64_t prevPts = -1;
        double maxLagMs = 0;
        bool monotonic = true, noGap = true;
        const double rates[] = {30.0, 24.0, 27.0, 30.0, 25.0, 30.0};
        uint64_t idx = 0;
        for (double fps : rates) {
            for (int f = 0; f < 300; ++f, ++idx) {
                if (idx > 0) now += int64_t(std::llround(10000000.0 / fps));
                const auto st = clk.video(idx, now);
                if (!st || st->pts100ns <= prevPts) monotonic = false;
                if (st && st->discontinuity) noGap = false;
                if (st) {
                    prevPts = st->pts100ns;
                    const double lagMs = double((now - origin) - st->pts100ns) / 10000.0;
                    maxLagMs = std::max(maxLagMs, std::abs(lagMs));
                }
            }
        }
        check(monotonic && noGap, "heavy 24-30 fps VFR swings stay monotonic with zero discontinuities");
        check(maxLagMs < 55.0, "heavy 24-30 fps VFR swings lag stays bounded <55ms (got " + std::to_string(maxLagMs) + ")");
    }

    // 4. Passing arrivalMs activates CaptureSyncTarget fallback if upstream PTS is ever wildly skewed.
    {
        CaptureSyncTarget sync;
        const double pipelineMs = 52.0;
        CaptureSyncTarget::Result last{};
        for (int i = 0; i < 10; ++i) {
            const double arrivalMs = 10000.0 + i * 33.333;
            const double hostMs = arrivalMs + pipelineMs;
            // Simulate a 900 ms bogus upstream PTS skew.
            last = sync.observe(pipelineMs + 900.0, hostMs, arrivalMs);
        }
        check(last.fallback && std::abs(last.targetMs - pipelineMs) < 0.5,
              "CaptureSyncTarget with arrivalMs falls back to true local pipeline latency (" +
              std::to_string(last.targetMs) + " ms)");
    }

    std::printf("%s\n", failures ? "RemotePlayVfrSync tests FAILED" : "RemotePlayVfrSync tests passed");
    return failures ? 1 : 0;
}
