// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS tests/unit/PushPacingRepairTests.cpp (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
// Frame Pacing Repair (engine/FramePacingRepair.h) on synthetic capture streams.
#include "veyra/engine/FramePacingRepair.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace veyra::engine;
static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static const int64_t T60 = 166667, T120 = 83333;

struct Run { std::vector<int64_t> out, in; uint64_t repaired = 0; int64_t maxLate = 0, maxEarly = 0; bool shiftsRight = true; };
// samples: when each new picture arrived (on the capture grid). content = sample (Veyra's time
// for an irregular frame).
static Run run(const std::vector<int64_t>& samples, int64_t transport, std::vector<bool> boundary = {}) {
    FramePacingRepair p; Run r;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto res = p.apply(samples[i], samples[i], transport, i < boundary.size() && boundary[i]);
        r.out.push_back(res.pts); r.in.push_back(samples[i]);
        // The reported shift always belongs to this frame: its time minus its own sample.
        if (res.shift != res.pts - samples[i]) r.shiftsRight = false;
        if (res.repaired) { ++r.repaired; r.maxLate = std::max(r.maxLate, res.shift); r.maxEarly = std::max(r.maxEarly, -res.shift); }
    }
    return r;
}
// Standard deviation (ms) of the intervals from index `from` on.
static double jitterMs(const std::vector<int64_t>& t, size_t from) {
    std::vector<double> d;
    for (size_t i = from + 1; i < t.size(); ++i) d.push_back(double(t[i] - t[i - 1]) / 1e4);
    double mean = 0; for (double v : d) mean += v; mean /= d.size();
    double var = 0; for (double v : d) var += (v - mean) * (v - mean);
    return std::sqrt(var / d.size());
}
static bool ordered(const std::vector<int64_t>& t) { for (size_t i = 1; i < t.size(); ++i) if (t[i] <= t[i - 1]) return false; return true; }

int main() {
    // 1. A 30 FPS game shown 1-3-1-3 refreshes in a 60 Hz signal (17/50 ms): repaired to an even 33 ms.
    std::vector<int64_t> s; int64_t t = 0;
    for (int i = 0; i < 120; ++i) { s.push_back(t); t += (i % 2 ? 3 : 1) * T60; }
    auto r = run(s, T60);
    std::printf("  1/3 pattern: input jitter %.2f ms, output jitter %.2f ms, late %.2f ms, early %.2f ms\n",
                jitterMs(r.in, 10), jitterMs(r.out, 10), r.maxLate / 1e4, r.maxEarly / 1e4);
    check(jitterMs(r.in, 10) > 16, "the input judders (17/50 ms)");
    check(jitterMs(r.out, 10) < 1.0, "repaired: frames 33 ms apart (within 1 ms)");
    check(r.maxLate <= T60 && r.maxEarly <= T60, "no frame moves more than one refresh");
    check(r.maxLate <= T60 / 2 + 5000, "the 1/3 pattern needs about half a refresh at most");
    check(r.repaired >= 110, "repairs after a few frames of warm-up");
    check(ordered(r.out), "times stay in order");

    // 2. Even pacing (2-2-2): nothing to repair, times barely move.
    s.clear(); t = 0;
    for (int i = 0; i < 90; ++i) { s.push_back(t); t += 2 * T60; }
    r = run(s, T60);
    int64_t moved = 0; for (size_t i = 0; i < s.size(); ++i) moved = std::max<int64_t>(moved, std::llabs(r.out[i] - s[i]));
    check(moved < 10000, "even pacing stays where it is (under 1 ms)");

    // 3. Random 1/2/3 refresh pacing around 30 FPS (average 2 refreshes): smoothed.
    std::mt19937 rng(7); s.clear(); t = 0; int debt = 0;
    for (int i = 0; i < 200; ++i) {
        s.push_back(t);
        int hold = 2 + (rng() % 3 == 0 ? (debt > 0 ? -1 : 1) : 0);
        if (debt != 0 && rng() % 2) hold = 2 - (debt > 0 ? 1 : -1);
        debt += hold - 2; t += hold * T60;
    }
    r = run(s, T60);
    std::printf("  random pacing: input jitter %.2f ms, output jitter %.2f ms\n", jitterMs(r.in, 10), jitterMs(r.out, 10));
    check(jitterMs(r.out, 10) < jitterMs(r.in, 10) / 3, "random 1/2/3 pacing: jitter cut by more than 3x");
    check(r.maxLate <= T60 && r.maxEarly <= T60 && ordered(r.out), "random pacing: bounded and in order");

    // 4. A 40 FPS mode in a 120 Hz signal shown 2-4 refreshes (17/33 ms) instead of 3-3: repaired.
    s.clear(); t = 0;
    for (int i = 0; i < 120; ++i) { s.push_back(t); t += (i % 2 ? 4 : 2) * T120; }
    r = run(s, T120);
    check(jitterMs(r.out, 10) < 1.0, "40 FPS in 120 Hz, 2/4 pattern: repaired to 25 ms");

    // 5. A stall (a 150 ms hitch): the repair restarts from the frame after it, no huge shift.
    s.clear(); t = 0;
    for (int i = 0; i < 60; ++i) { s.push_back(t); t += (i % 2 ? 3 : 1) * T60; if (i == 30) t += 9 * T60; }
    r = run(s, T60);
    int64_t worst = 0; for (size_t i = 0; i < s.size(); ++i) worst = std::max<int64_t>(worst, std::llabs(r.out[i] - s[i]));
    check(worst <= T60, "a stall never makes a frame move more than one refresh");
    check(ordered(r.out), "a stall keeps times in order");
    check(jitterMs(std::vector<int64_t>(r.out.begin() + 45, r.out.end()), 0) < 1.0, "repaired again after the stall");

    // 6. A variable frame rate (an unlocked game wandering between 35 and 55 FPS in 60 Hz): left alone.
    s.clear(); t = 0;
    for (int i = 0; i < 150; ++i) {
        s.push_back(t);
        const double fps = 45 + 10 * std::sin(i * 0.15);
        t += int64_t(std::llround(1e7 / fps / T60)) * T60;
    }
    r = run(s, T60);
    check(r.repaired < 30, "a wandering frame rate is mostly left as it came (repaired " + std::to_string(r.repaired) + ")");

    // 7. A 60 FPS game in 60 Hz: no repeated pictures, nothing to do.
    s.clear(); for (int i = 0; i < 60; ++i) s.push_back(i * T60);
    r = run(s, T60);
    check(r.repaired == 0, "60 FPS content is never re-timed");

    // 8. A boundary (seek, settings change, still picture) starts again.
    s.clear(); t = 0; std::vector<bool> b;
    for (int i = 0; i < 40; ++i) { s.push_back(t); t += (i % 2 ? 3 : 1) * T60; b.push_back(i == 20); }
    r = run(s, T60, b);
    check(r.out[20] == s[20], "the frame at a boundary keeps its own time");
    check(ordered(r.out), "boundary: times in order");

    // 9. Fuzz: random cadences, stalls, boundaries and early frames. Times stay in order, no frame moves
    // more than one refresh (or the in-order nudge), and every reported shift is pts minus that frame's
    // own sample, also when the in-order fallback moves a frame (audit item, November 2026).
    std::mt19937 fz(2026);
    bool allOrdered = true, allShifts = true;
    int fallbacks = 0;
    for (int seq = 0; seq < 400; ++seq) {
        s.clear(); b.clear(); t = 0;
        const int64_t T = fz() % 2 ? T60 : T120;
        for (int i = 0; i < 80; ++i) {
            s.push_back(t);
            b.push_back(fz() % 40 == 0);
            const int r2 = int(fz() % 10);
            t += (r2 < 4 ? 1 : r2 < 8 ? 3 : r2 < 9 ? 2 : 8) * T;
        }
        r = run(s, T, b);
        // In order everywhere except across a boundary (a seek may go back in time on purpose).
        for (size_t i = 1; i < r.out.size(); ++i) if (!b[i] && r.out[i] <= r.out[i - 1]) allOrdered = false;
        allShifts = allShifts && r.shiftsRight;
        for (size_t i = 0; i < s.size(); ++i) if (r.out[i] > s[i] + T) ++fallbacks;
    }
    check(allOrdered, "fuzz: times always in order (except across a boundary)");
    check(allShifts, "fuzz: every reported shift is the frame's own pts minus its sample");
    // Direct case for the in-order fallback: a repaired, late-shifted frame followed by one whose
    // own time would land before it.
    {
        FramePacingRepair p;
        int64_t tt = 0;
        FramePacingRepair::Result last{};
        for (int i = 0; i < 30; ++i) { last = p.apply(tt, tt, T60, false); tt += (i % 2 ? 3 : 1) * T60; }
        // A pass-through frame (variable rate breaks the fit) right after the last one.
        const int64_t early = last.pts - last.shift + 1;  // just after the last frame's own sample
        const auto next = p.apply(early, early, T60, false);
        check(next.pts > last.pts, "in-order fallback keeps time moving forward");
        check(next.shift == next.pts - early, "in-order fallback reports this frame's shift (" + std::to_string(next.shift) + ")");
    }
    std::printf("  fuzz: frames nudged forward for order: %d\n", fallbacks);

    std::printf("%s\n", failures ? "FramePacingRepair tests FAILED" : "FramePacingRepair tests passed");
    return failures ? 1 : 0;
}
