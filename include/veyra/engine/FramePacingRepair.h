// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS include/veyra/engine/PushPacingRepair.h (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace veyra::engine {
// Frame pacing repair (EnhancementSettings::pacingRepair).
//
// Many console games locked to 30 FPS do not show their frames evenly: instead
// of every frame staying on screen for two 60 Hz refreshes, some stay for one
// and others for three (17/50 ms instead of 33/33). The game simulated them
// 33 ms apart, so the motion judders even though the frame rate is right, and
// frame generation spreads its frames over the uneven pairs and keeps the
// judder. The console cannot fix this; the picture it sends is already uneven.
//
// Veyra's duplicate removal (ContentDedup) already evens out regular cadences
// (30 in 58 Hz, 40 in 60 Hz), but it never gives a frame a time later than the
// sample it arrived in, so an early frame keeps its early time. This repair
// gives each real frame its slot on an even grid instead: a least-squares line
// through the sample times of the last kept frames (their index against their
// time), evaluated at the newest one. The line sits in the middle of the
// pattern, so a repaired time is at most about half a refresh early or late for
// the common 1/3 pattern, and never more than one capture refresh either way.
// Successive times are phase-locked to that line (one period on from the last
// repaired time, a fifth of the way back to the line), so they stay evenly spaced.
// Graph frame generation already shows a real frame one content interval after
// it arrives, so a frame due up to one refresh later still has its picture.
//
// It only acts on what it can explain as uneven showing of a locked rate:
//   - the content is slower than the capture signal (repeated pictures exist:
//     the slope is at least 1.4 capture refreshes);
//   - every frame in the window lies within 1.1 refreshes of the line, so the
//     unevenness is the refresh quantization of a steady rate.
// A stall (the newest frame far off the line) restarts the window there; a
// genuinely variable frame rate fails the second test and keeps Veyra's times.
// Units: 100 ns.
class FramePacingRepair {
public:
    static constexpr unsigned kWindow = 16;
    static constexpr unsigned kMinimum = 6;
    struct Result {
        int64_t pts = 0;        // the time to use for this kept frame
        bool repaired = false;  // false: the content time came through unchanged
        int64_t shift = 0;      // pts - sample (positive: shown later than it arrived)
    };
    struct Stats { uint64_t frames = 0, repaired = 0, restarts = 0, variable = 0; int64_t maxLate = 0, maxEarly = 0; };

    // A boundary (seek, settings change): the time base may jump, nothing carries over.
    void reset() { restart(); haveLast_ = false; }
    const Stats& stats() const { return stats_; }

    // sample: when the capture delivered this new picture; content: the time
    // duplicate removal gave it; transport: one capture refresh; boundary: a
    // seek, settings change or still-picture hold (the cadence starts again).
    Result apply(int64_t sample, int64_t content, int64_t transport, bool boundary) {
        ++stats_.frames;
        Result r; r.pts = content; r.shift = content - sample;
        if (boundary || transport <= 0) { reset(); push(sample); return keep(r); }
        // A capture time that went back, or a stall below: the fit starts again, but output times stay
        // in order with what was already handed on.
        if (haveLast_ && sample <= lastSample_) { restart(); push(sample); return keep(r); }
        push(sample);
        if (count_ < kMinimum) return keep(r);
        double slope = 0, intercept = 0;
        fit(slope, intercept);
        const double tolerance = double(transport) * 1.1;
        const double newest = intercept + slope * double(count_ - 1);
        if (std::abs(double(sample) - newest) > tolerance) {
            // A stall or a rate change: start again from this frame.
            ++stats_.restarts; restart(); push(sample); return keep(r);
        }
        if (slope < double(transport) * 1.4) return keep(r);   // no repeated pictures to re-time
        for (unsigned i = 0; i < count_; ++i) {
            const double line = intercept + slope * double(i);
            if (std::abs(double(at(i)) - line) > tolerance) { ++stats_.variable; return keep(r); }
        }
        // Phase-locked: step one period from the last repaired time and move a fifth of
        // the way to the line, so the line's end-point wobble does not reach the output.
        double target = newest;
        if (lastRepaired_) target = double(lastOut_) + slope + 0.2 * (newest - (double(lastOut_) + slope));
        int64_t pts = int64_t(target + (target >= 0 ? 0.5 : -0.5));
        pts = std::clamp<int64_t>(pts, sample - transport, sample + transport);
        // Times stay in order, at least half a period apart.
        if (haveLast_) pts = std::max<int64_t>(pts, lastOut_ + int64_t(slope / 2));
        r.pts = pts; r.repaired = true; r.shift = pts - sample;
        lastRepaired_ = true;
        ++stats_.repaired;
        stats_.maxLate = std::max(stats_.maxLate, r.shift);
        stats_.maxEarly = std::max(stats_.maxEarly, -r.shift);
        return keep(r);
    }

private:
    void restart() { count_ = head_ = 0; lastRepaired_ = false; }
    Result keep(const Result& r) {
        if (!r.repaired) lastRepaired_ = false;
        // A pass-through time must not land before the last repaired one.
        Result out = r;
        // The shift is reported against this frame's own sample (r.pts - r.shift), not the previous one.
        if (haveLast_ && out.pts <= lastOut_) { const int64_t sample = r.pts - r.shift; out.pts = lastOut_ + 10000; out.shift = out.pts - sample; }
        lastOut_ = out.pts; lastSample_ = count_ ? at(count_ - 1) : lastSample_; haveLast_ = true;
        return out;
    }
    void push(int64_t t) { times_[head_] = t; head_ = (head_ + 1) % kWindow; count_ = std::min(count_ + 1, kWindow); }
    int64_t at(unsigned i) const { return times_[(head_ + kWindow - count_ + i) % kWindow]; }
    void fit(double& slope, double& intercept) const {
        // Relative to the first time, so the sums stay small.
        const double origin = double(at(0)), n = double(count_), meanX = (n - 1) / 2;
        double meanY = 0;
        for (unsigned i = 0; i < count_; ++i) meanY += double(at(i)) - origin;
        meanY /= n;
        double sxx = 0, sxy = 0;
        for (unsigned i = 0; i < count_; ++i) {
            const double x = double(i) - meanX, y = double(at(i)) - origin - meanY;
            sxx += x * x; sxy += x * y;
        }
        slope = sxx > 0 ? sxy / sxx : 0;
        intercept = origin + meanY - slope * meanX;
    }
    std::array<int64_t, kWindow> times_{};
    unsigned count_ = 0, head_ = 0;
    int64_t lastOut_ = 0, lastSample_ = 0;
    bool haveLast_ = false, lastRepaired_ = false;
    Stats stats_;
};
}
