// SPDX-License-Identifier: GPL-3.0-only
// Unit tests for PS5 HD Haptics PCM -> dual-motor rumble fallback.
#include "veyra/remoteplay/HapticsRumbleFallback.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace veyra::remoteplay;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

static std::vector<int16_t> sineStereo(double freqHz, double amp, size_t frames, double sampleRate = 3000.0) {
    std::vector<int16_t> out(frames * 2);
    constexpr double kTwoPi = 6.28318530717958647692;
    for (size_t i = 0; i < frames; ++i) {
        const int16_t s = int16_t(std::lround(amp * 32767.0 * std::sin(kTwoPi * freqHz * double(i) / sampleRate)));
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
    return out;
}

int main() {
    // 1. Silence and sub-noise-floor hiss never wake the motors.
    {
        HapticsRumbleFallback fb;
        const std::vector<int16_t> silence(120, 0);
        const auto hiss = sineStereo(300.0, 0.01, 60);
        check(!fb.feed(silence, 100000).has_value() && !fb.active(), "silence produces no rumble");
        check(!fb.feed(hiss, 300000).has_value() && !fb.active(), "sub-threshold hiss produces no rumble");
    }

    // 2. Low-frequency bass (40 Hz) primarily drives the low-frequency motor.
    {
        HapticsRumbleFallback fb;
        const auto bass = sineStereo(40.0, 0.6, 120);
        HapticsRumbleFallback::Rumble last{};
        int64_t t = 0;
        for (int k = 0; k < 5; ++k) {
            t += 200000;
            if (auto r = fb.feed(bass, t)) last = *r;
        }
        check(last.low > 30000 && last.low > last.high * 2,
              "40 Hz bass drives low-freq motor strongly (low=" + std::to_string(last.low) +
              ", high=" + std::to_string(last.high) + ")");
    }

    // 3. High-frequency texture (600 Hz) primarily drives the high-frequency motor.
    {
        HapticsRumbleFallback fb;
        const auto texture = sineStereo(600.0, 0.6, 120);
        HapticsRumbleFallback::Rumble last{};
        int64_t t = 0;
        for (int k = 0; k < 5; ++k) {
            t += 200000;
            if (auto r = fb.feed(texture, t)) last = *r;
        }
        check(last.high > 30000 && last.high > last.low * 2,
              "600 Hz texture drives high-freq motor strongly (low=" + std::to_string(last.low) +
              ", high=" + std::to_string(last.high) + ")");
    }

    // 4. Returning to silence cleanly decays and emits an explicit (0, 0) stop command.
    {
        HapticsRumbleFallback fb;
        const auto hit = sineStereo(60.0, 0.8, 120);
        const std::vector<int16_t> silence(120, 0);
        int64_t t = 200000;
        check(fb.feed(hit, t).has_value() && fb.active(), "impact activates fallback rumble");
        bool sawStop = false;
        for (int k = 0; k < 40; ++k) {
            t += 200000;
            if (auto r = fb.feed(silence, t)) {
                if (r->low == 0 && r->high == 0) { sawStop = true; break; }
            }
        }
        check(sawStop && !fb.active(), "silence cleanly stops motors with explicit (0, 0)");
    }

    // 5. Rate limiting prevents spamming small steady-state updates faster than ~60 Hz.
    {
        HapticsRumbleFallback fb;
        const auto tone = sineStereo(80.0, 0.4, 60);
        const auto toneSlightlyLouder = sineStereo(80.0, 0.43, 60);
        // Let the envelope settle first.
        int64_t t = 1000000;
        for (int i = 0; i < 6; ++i) { t += 200000; (void)fb.feed(tone, t); }
        auto settled = fb.feed(toneSlightlyLouder, t + 200000);
        auto tooSoon = fb.feed(tone, t + 250000); // only 5 ms after previous emit
        check(settled.has_value() && !tooSoon.has_value(), "small updates within 16ms are rate-limited");
    }

    std::printf("%s\n", failures ? "HapticsRumbleFallback tests FAILED" : "HapticsRumbleFallback tests passed");
    return failures ? 1 : 0;
}
