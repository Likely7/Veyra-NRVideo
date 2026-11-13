#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace veyra::engine {
// Repeated-picture removal and content cadence for live sources (2026-10-11,
// exact pictures 2026-10-12).
//
// A game locked to 30 or 40 fps behind a 60 Hz capture repeats pictures; frame
// generation then interpolates across a still pair, or across a doubled step
// where a blind sampler dropped a real picture. The content modes (auto /
// 30 / 50 / 60 fps) drop a sample here when its picture repeats the last kept
// one; "60->30" is the plain every-second-sample CaptureHalfRate, not this.
//
// A repeat is a sample with the same picture number as the last kept one: the
// capture callback (or the engine, for CPU frames) compares every component of
// the whole picture with the last new picture, and a sample within the
// capture-noise band but for a few isolated components keeps its number
// (PictureCompare.h). The first version
// judged 64x36 sampled points with an 8-level gate: a running HDR game seen
// from a still position lost most real pictures (6-16 kept of 60 per second,
// field log 2026-10-12). Numbers rather than pictures also keep a picture whose
// first sample the engine never read (mailbox overwrite).
//
// Kept frames get a content timestamp from a clamped phase-locked cadence: the
// picture changed after the previous sample and no later than this one, so the
// predicted time is clamped into that window. Regular content (30 in 58 Hz,
// 40 in 60 Hz) gets evenly spaced timestamps that are never later than the
// sample; hitches and variable rates keep their sample times.
//
// A picture that stays identical past a hitch (paused game, menu) is held at
// the content cadence instead of one frame per 150-250 ms: the kept rate, and
// with it the GPU work of NR and frame generation, stays steady.
class ContentDedup {
public:
    struct Decision {
        bool keep = true;
        int64_t contentPts = 0;   // 100 ns, kept frames
        int64_t interval = 0;     // 100 ns from the previous kept frame (0 = unknown)
        int64_t holdInterval = 0; // static refresh cadence, not the preceding hitch span
        const char* reason = "first";
    };
    struct Stats {
        uint64_t observed = 0, kept = 0, repeats = 0, held = 0, blind = 0;
        uint64_t regular = 0, irregular = 0;
        double maxShiftMs = 0;    // largest sample-to-content correction
    };

    void reset() {
        haveKept_ = haveContent_ = skipSpan_ = holding_ = false; prevSample_ = 0; lastKeptSample_ = 0; lastContent_ = 0;
        timeCount_ = 0; timeHead_ = 0; period_ = 0; misses_ = 0;
        rateCount_ = rateHead_ = 0; observedPeriod_ = 0;ratePictures_=rateLastPicture_=rateLastSequence_=0;
    }
    // picture: picture number of the sample, or nullptr (no CPU picture: keep everything).
    // hintPeriod: expected content period of the selected content rate, 0 = unknown.
    // captureSequence is supplied only with all-callback PictureReference ids:
    // their low 56 bits count new pictures even if the engine missed a sample.
    // Engine-only comparisons have no knowledge of skipped pictures, so pass 0.
    Decision observe(const uint64_t* picture, int64_t pts, int64_t transport, bool boundary, int64_t hintPeriod = 0,uint64_t captureSequence=0) {
        Decision d;
        ++stats_.observed;
        if (transport <= 0) transport = 166667;
        const int64_t previous = prevSample_;
        const bool continuous = haveKept_ && !boundary && pts > previous && pts - previous <= transport * 6;
        prevSample_ = pts;
        if (!picture) {
            // No CPU picture (hardware-decoded streams): keep everything. A
            // boundary (open, seek, settings change) re-anchors the cadence.
            ++stats_.blind; haveKept_ = false;
            if (boundary) { reset(); prevSample_ = pts; }
            return keep(d, pts, previous, transport, nullptr, Timing::Sample, "no-picture");
        }
        if (!continuous) {
            reset(); prevSample_ = pts;
            return keep(d, pts, previous, transport, picture, Timing::Sample, boundary ? "boundary" : "first",0,captureSequence);
        }
        if (*picture == kept_) {
            const auto measured=measuredPeriod();
            const int64_t cadence = std::min<int64_t>(measured > 0 ? measured : hintPeriod > 0 ? hintPeriod : transport * 2, 666667);
            if (!holding_) {
                // A hitch repeats one picture for a few samples: drop those, so
                // the next picture spans the whole wait.
                if (pts - lastKeptSample_ < std::max(cadence * 5 / 2, transport * 3)) { ++stats_.repeats; d.keep = false; d.reason = "repeat"; return d; }
                holding_ = true; holdDue_ = pts;
            }
            // Past that the picture is still: hold it on the content cadence's
            // grid (40 fps in 60 Hz keeps 33/17 ms), never slower than 15 fps.
            if (pts + transport / 4 < holdDue_) { ++stats_.repeats; d.keep = false; d.reason = "repeat"; return d; }
            holdDue_ = std::max(holdDue_ + cadence, pts + cadence / 2);
            ++stats_.held;
            d.holdInterval=cadence;
            return keep(d, pts, previous, transport, picture, Timing::Sample, "hold");
        }
        holding_ = false;
        return keep(d, pts, previous, transport, picture, Timing::Cadence, "new", hintPeriod,captureSequence);
    }
    // A failed constant-period fit is not an unknown content rate. Measure the
    // moving pictures separately, including genuinely varying game cadence.
    // Held static-screen refreshes do not manufacture new content observations.
    double contentFps() const { const auto period=measuredPeriod();return period > 0 ? 1e7 / double(period) : 0.0; }
    int64_t intervalHint(int64_t transport) const {
        return std::max(transport, measuredPeriod() > 0 ? measuredPeriod() : transport);
    }
    const Stats& stats() const { return stats_; }

private:
    enum class Timing { Sample, Cadence };
    static constexpr unsigned kTimes = 48;
    int64_t measuredPeriod() const {
        // A stable fit removes transport-grid quantization from the rate
        // display, without overriding all-callback picture counts after a miss.
        if(period_>0&&misses_==0&&observedPeriod_>0&&std::llabs(period_-observedPeriod_)*100<=observedPeriod_*3)
            return period_;
        return observedPeriod_;
    }
    Decision& keep(Decision& d, int64_t pts, int64_t previous, int64_t transport, const uint64_t* picture, Timing timing,
                   const char* reason, int64_t hint = 0,uint64_t captureSequence=0) {
        d.keep = true; d.reason = reason; ++stats_.kept;
        int64_t content = pts;
        if (!haveContent_&&picture) observeRate(pts, transport, false,*picture,captureSequence);
        if (timing == Timing::Cadence) {
            observeRate(pts, transport, skipSpan_,*picture,captureSequence);
            // A span that started at a held (still-screen) frame is not a content period.
            if (skipSpan_) { timeCount_ = 0; timeHead_ = 0; }
            if (timeCount_ == 0) push(lastKeptSample_);
            push(pts);
            if (timeCount_ >= 8) {
                // Least-squares slope of sample time over kept-frame index: the
                // sample grid quantizes each time by up to one slot, and the
                // slope over n frames is off by about slot/n^1.5 (0.05 ms at 58 Hz).
                const unsigned n = timeCount_, first = (timeHead_ + kTimes - n) % kTimes;
                const double origin = double(times_[first]), mean = (n - 1) / 2.0;
                double sxx = 0, sxt = 0, st = 0;
                for (unsigned i = 0; i < n; ++i) st += double(times_[(first + i) % kTimes]) - origin;
                for (unsigned i = 0; i < n; ++i) {
                    const double x = i - mean, t = double(times_[(first + i) % kTimes]) - origin - st / n;
                    sxx += x * x; sxt += x * t;
                }
                period_ = std::max<int64_t>(int64_t(std::llround(sxt / sxx)), transport - transport / 8);
                if (hint > 0 && std::llabs(period_ - hint) * 100 < hint * 3) period_ = hint;
            }
            // The picture changed after the previous sample and by this one.
            const int64_t lo = std::max(lastKeptSample_, previous), hi = pts;
            if (period_ > 0) {
                const int64_t predicted = lastContent_ + period_, tolerance = transport / 2;
                if (predicted >= lo - tolerance && predicted <= hi + tolerance) {
                    content = std::clamp(predicted, lo + 1, hi); misses_ = 0; ++stats_.regular;
                } else {
                    ++stats_.irregular;
                    // Two misses in a row: the rate changed; relearn it from here.
                    if (++misses_ >= 2) { timeCount_ = 0; timeHead_ = 0; period_ = 0; misses_ = 0; push(pts); }
                }
            }
        }
        if (haveContent_) {
            content = std::max(content, lastContent_ + 10000);
            d.interval = content - lastContent_;
        }
        skipSpan_ = timing == Timing::Sample && haveContent_;
        haveContent_ = true;
        stats_.maxShiftMs = std::max(stats_.maxShiftMs, double(pts - content) / 1e4);
        d.contentPts = content;
        if (picture) { kept_ = *picture; haveKept_ = true; }
        lastKeptSample_ = pts; lastContent_ = content;
        return d;
    }

    void push(int64_t time) { times_[timeHead_] = time; timeHead_ = (timeHead_ + 1) % kTimes; timeCount_ = std::min(timeCount_ + 1, kTimes); }

    void observeRate(int64_t pts, int64_t transport, bool afterHold,uint64_t picture,uint64_t captureSequence) {
        // A still-screen refresh is not an input picture. Do not count the long
        // hold as a game-frame interval when motion resumes; retain the last
        // observed rate until the new moving window can replace it.
        if (afterHold) rateCount_ = rateHead_ = 0;
        uint64_t changes=1;
        if(captureSequence&&rateLastSequence_&&captureSequence>rateLastSequence_&&
           picture>rateLastPicture_&&(picture>>56)==(rateLastPicture_>>56)&&
           picture-rateLastPicture_<=captureSequence-rateLastSequence_)
            changes=picture-rateLastPicture_;
        rateLastPicture_=picture;rateLastSequence_=captureSequence;ratePictures_+=changes;
        rateTimes_[rateHead_] = pts;
        rateCounts_[rateHead_] = ratePictures_;
        rateHead_ = (rateHead_ + 1) % kTimes;
        rateCount_ = std::min(rateCount_ + 1, kTimes);
        auto first = (rateHead_ + kTimes - rateCount_) % kTimes;
        while (rateCount_ > 8 && pts - rateTimes_[first] > 7500000) {
            --rateCount_; first = (first + 1) % kTimes;
        }
        if (rateCount_ >= 8 && pts > rateTimes_[first]&&ratePictures_>rateCounts_[first])
            observedPeriod_ = std::max(transport, (pts - rateTimes_[first]) / int64_t(ratePictures_-rateCounts_[first]));
    }

    uint64_t kept_ = 0;
    std::array<int64_t, kTimes> times_{};
    std::array<int64_t, kTimes> rateTimes_{};
    std::array<uint64_t, kTimes> rateCounts_{};
    uint64_t ratePictures_=0,rateLastPicture_=0,rateLastSequence_=0;
    unsigned timeCount_ = 0, timeHead_ = 0, misses_ = 0;
    unsigned rateCount_ = 0, rateHead_ = 0;
    bool haveKept_ = false, haveContent_ = false, skipSpan_ = false, holding_ = false;
    int64_t prevSample_ = 0, lastKeptSample_ = 0, lastContent_ = 0, period_ = 0, holdDue_ = 0;
    int64_t observedPeriod_ = 0;
    Stats stats_;
};
}
