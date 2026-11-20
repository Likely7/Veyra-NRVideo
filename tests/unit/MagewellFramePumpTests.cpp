#include "MagewellFramePump.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
using veyra::source::magewell::ArmResult;
using veyra::source::magewell::CaptureCadence;
using veyra::source::magewell::partialLinesFor;
using veyra::source::magewell::pumpFrames;

unsigned checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

// 100 ns ticks, with a 59.94 Hz HDMI input. Calibrated example, not measured
// driver timings: completion cannot precede the end of the frame, and DMA +
// the application's copy take 17.75 ms if they are serialized.
class Device {
public:
    explicit Device(int64_t completionAge, int64_t copyTime = 35'000)
        : completionAge_(completionAge), copyTime_(copyTime) {}

    ArmResult arm(unsigned slot, bool wait) {
        if (delivered == 600) return ArmResult::Stop;
        check(active_ < 0, "no second in-flight DMA");
        const auto earliest = (lastArmed_ + 1) * period;
        if (now_ < earliest) {
            if (!wait) return ArmResult::NoFrame;
            now_ = earliest;
        }
        lastArmed_ = now_ / period;
        frames_[slot] = lastArmed_;
        active_ = static_cast<int>(slot);
        completesAt_ = std::max(now_ + 142'500, lastArmed_ * period + completionAge_);
        if (!wait) ++prearmed;
        return ArmResult::Armed;
    }
    bool complete(unsigned slot) {
        check(active_ == static_cast<int>(slot), "completion belongs to the requested slot");
        now_ = completesAt_;
        active_ = -1;
        return true;
    }
    bool publish(unsigned slot) {
        check(active_ != static_cast<int>(slot), "CPU never reads the buffer being written by DMA");
        check(frames_[slot] > lastPublished_, "published device timestamps are strictly increasing");
        skipped += frames_[slot] - lastPublished_ - 1;
        lastPublished_ = frames_[slot];
        now_ += copyTime_;
        return ++delivered < 600;
    }
    double rate() const { return delivered * 1e7 / now_; }

    static constexpr int64_t period = 166'833;
    int64_t skipped = 0;
    unsigned delivered = 0, prearmed = 0;

private:
    int64_t completionAge_, copyTime_;
    int64_t now_ = 0, completesAt_ = 0, lastArmed_ = -1, lastPublished_ = -1;
    int active_ = -1;
    std::array<int64_t, 2> frames_{};
};

void testOverlap() {
    Device legacy(245'000);
    do {
        check(legacy.arm(0, true) == ArmResult::Armed, "serial baseline submits");
        legacy.complete(0);
    } while (legacy.publish(0));

    Device pipelined(245'000);
    pumpFrames([&](unsigned i, bool wait) { return pipelined.arm(i, wait); },
        [&](unsigned i) { return pipelined.complete(i); },
        [&](unsigned i) { return pipelined.publish(i); });
    check(legacy.skipped > 30 && legacy.rate() < 57.0, "serial baseline reproduces the observed symptom");
    check(pipelined.skipped == 0 && pipelined.rate() > 59.8 && pipelined.prearmed >= 590,
        "prearming preserves the input cadence in the same model");
    std::printf("MODEL 59.94Hz: serial %.2f FPS, %lld missing; overlapped %.2f FPS, %lld missing\n",
        legacy.rate(), static_cast<long long>(legacy.skipped), pipelined.rate(), static_cast<long long>(pipelined.skipped));
}

void testIdleAndStop() {
    Device quick(145'000, 10'000);
    pumpFrames([&](unsigned i, bool wait) { return quick.arm(i, wait); },
        [&](unsigned i) { return quick.complete(i); },
        [&](unsigned i) { return quick.publish(i); });
    check(quick.prearmed == 0 && quick.skipped == 0, "publish promptly when the next frame has not started");

    unsigned publications = 0;
    pumpFrames([](unsigned, bool) { return ArmResult::Armed; },
        [](unsigned) { return false; },
        [&](unsigned) { ++publications; return true; });
    check(publications == 0, "DMA timeout never publishes an unfinished frame");
    pumpFrames([](unsigned, bool wait) { return wait ? ArmResult::Armed : ArmResult::Stop; },
        [](unsigned) { return true; },
        [&](unsigned) { ++publications; return true; });
    check(publications == 0, "shutdown ends the pump even between completion and submission");
}

void testCadence() {
    constexpr uint32_t p = uint32_t(Device::period);
    constexpr unsigned ringSize = 7;
    CaptureCadence guard;
    unsigned missed = 0;
    bool fallback = false;
    for (unsigned frame = 0; frame < 310; ++frame) {
        if (frame % 16 == 7) continue;
        const auto sample = guard.observe(int64_t(frame) * p, p + frame % 3, frame % ringSize, ringSize, frame == 0);
        missed += sample.missed;
        if (sample.fallback) { fallback = true; break; }
    }
    check(missed >= 12 && fallback, "confirmed recurring skipped capture frames trigger fallback");

    // The HDMI clock varies but each delivered frame is the next card slot.
    CaptureCadence variable;
    int64_t pts = 0;
    for (unsigned i = 0; i < 400; ++i) {
        pts += i % 4 == 0 ? p * 2 : p;
        const auto sample = variable.observe(pts, p, i % ringSize, ringSize, false);
        check(!sample.missed && !sample.fallback, "variable input alone must not be classified as capture drops");
    }

    CaptureCadence transient;
    for (unsigned i = 0; i < 600; ++i) {
        if (i < 120 && i % 16 == 7) continue;
        const auto sample = transient.observe(int64_t(i) * p, p, i % ringSize, ringSize, i == 120);
        check(!sample.fallback, "startup burst, transition, and stable capture stay on the SDK");
    }
    check(partialLinesFor(2160, 24'883'200) == 256, "4K P010 partial transfers");
    check(partialLinesFor(2160, 12'441'600) == 64, "4K NV12 retains its partial size");
    check(partialLinesFor(1080, 6'220'800) == 64, "1080p P010 retains its partial size");
}
}

int main() {
    testOverlap();
    testIdleAndStop();
    testCadence();
    std::printf("Magewell frame pump: %u checks, 0 failures\n", checks);
}
