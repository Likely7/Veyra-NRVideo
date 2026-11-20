// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS tests/unit/PushAdaptiveFgTests.cpp (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
// Adaptive frame generation (engine/AdaptiveFgMultiplier.h): the per-pair multiplier choice and its hold.
#include "veyra/engine/AdaptiveFgMultiplier.h"
#include <cstdio>
#include <string>

using namespace veyra::engine;
static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static void expect(FrameGenerationBackend b, unsigned ceiling, double content, double target, unsigned want, const char* label) {
    const unsigned got = adaptiveFgMultiplier(b, ceiling, content, target);
    check(got == want, std::string(label) + " (got " + std::to_string(got) + "X, want " + std::to_string(want) + "X)");
}

int main() {
    using B = FrameGenerationBackend;
    // Steady console rates at a 120 FPS target.
    expect(B::Dlss, 6, 30, 120, 4, "DLSS: 30 FPS -> 4X at 120");
    expect(B::Dlss, 6, 40, 120, 3, "DLSS: 40 FPS mode -> 3X at 120");
    expect(B::Dlss, 6, 60, 120, 2, "DLSS: 60 FPS -> 2X at 120");
    expect(B::Anr, 6, 24, 120, 5, "ANR: 24 FPS -> 5X at 120");
    expect(B::Vfg, 8, 20, 160, 8, "VFG: 20 FPS -> 8X at 160");
    // A game that wanders: closest output on a log scale, lower on a tie.
    expect(B::Dlss, 6, 36, 120, 3, "36 FPS -> 3X (108 beats 144)");
    expect(B::Dlss, 6, 33, 120, 4, "33 FPS -> 4X (132 beats 99)");
    expect(B::Dlss, 6, 25, 120, 4, "DLSS 25 FPS -> 4X (no 5X on DLSS; 100 beats 150)");
    expect(B::Anr, 6, 25, 120, 5, "ANR 25 FPS -> 5X (125)");
    expect(B::Dlss, 4, 20, 120, 4, "the ceiling is never exceeded");
    expect(B::Dlss, 6, 30, 60, 2, "60 FPS target: 30 FPS -> 2X");
    expect(B::Dlss, 6, 45, 144, 3, "45 FPS at 144 Hz -> 3X");
    // DLSS never gets 5X, whatever the rate.
    bool no5 = true;
    for (double fps = 15; fps <= 60; fps += 0.25)
        if (adaptiveFgMultiplier(B::Dlss, 6, fps, 120) == 5) no5 = false;
    check(no5, "DLSS is never asked for 5X");
    // Output stays within a factor of 1.25 of the target whenever the ceiling allows it (ANR/VFG step 1).
    bool close = true;
    for (double fps = 24; fps <= 60; fps += 0.5) {
        const unsigned m = adaptiveFgMultiplier(B::Anr, 6, fps, 120);
        const double ratio = fps * m / 120;
        if (ratio < 0.8 || ratio > 1.25) close = false;
    }
    check(close, "ANR 24-60 FPS: output within 0.8-1.25 x target");
    // Unknown rates keep the ceiling; 2X-only backends stay at 2X.
    expect(B::Dlss, 4, 0, 120, 4, "unknown content rate keeps the ceiling");
    expect(B::Dlss, 4, 30, 0, 4, "unknown target keeps the ceiling");
    expect(B::Fsr, 2, 30, 120, 2, "FSR stays 2X");

    // Gate: first choice at once, a change only after it held 0.5 s, a flicker never applies.
    AdaptiveFgGate gate;
    bool changed = false;
    check(gate.update(4, 0, &changed) == 4 && changed, "first choice applies at once");
    check(gate.update(3, 1000000, &changed) == 4 && !changed, "a new choice waits");
    check(gate.update(4, 2000000, &changed) == 4 && !changed, "flicker back: nothing changes");
    check(gate.update(3, 3000000) == 4, "the hold restarts");
    check(gate.update(3, 7000000) == 4, "0.4 s is not enough");
    check(gate.update(3, 8000001, &changed) == 3 && changed, "0.5 s later the new choice applies");
    gate.reset();
    check(gate.update(6, 8100000, &changed) == 6 && changed, "after a reset the first choice applies at once");

    std::printf("%s\n", failures ? "AdaptiveFg tests FAILED" : "AdaptiveFg tests passed");
    return failures ? 1 : 0;
}
