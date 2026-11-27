// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Types.h"
#include <algorithm>
#include <cmath>

namespace veyra::remoteplay {

// Adapted from Veyra-NRVideo PR #381. Translate 3 kHz stereo haptics into
// two motor envelopes only while neither native audio nor legacy rumble owns
// the controller's effects. The output is rate limited regardless of signal.
class HapticsRumbleFallback {
public:
    static constexpr unsigned legacyDurationMs = 5000;
    static constexpr unsigned pulseDurationMs = 250;
    struct Rumble {
        uint16_t low = 0, high = 0;
    };

    void reset() { *this = {}; }
    bool active() const { return last_.low != 0 || last_.high != 0; }
    void noteLegacyRumble(bool running, HostTime now) {
        reset();
        if (running) legacyUntil_ = now + HostTime(legacyDurationMs) * 10000;
    }

    std::optional<Rumble> feed(std::span<const int16_t> stereo, HostTime now) {
        const auto frames = stereo.size() / 2;
        if (frames == 0 || now < legacyUntil_) return std::nullopt;
        double lowEnergy = 0.0, highEnergy = 0.0;
        for (size_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < 2; ++channel) {
                const float sample = float(stereo[frame * 2 + channel]) / 32768.0f;
                low1_[channel] += 0.22f * (sample - low1_[channel]);
                low2_[channel] += 0.22f * (low1_[channel] - low2_[channel]);
                const float high = sample - low1_[channel];
                lowEnergy += double(low2_[channel]) * low2_[channel];
                highEnergy += double(high) * high;
            }
        }
        const auto smooth = [frames](double energy, float& envelope) {
            const float rms = float(std::sqrt(energy / (2.0 * double(frames))));
            const float amplitude = std::max(0.0f, rms - 0.025f) * 1.85f;
            const float target = std::clamp(std::pow(amplitude, 0.75f), 0.0f, 1.0f);
            envelope += (target > envelope ? 0.70f : 0.30f) * (target - envelope);
            if (envelope < 0.01f) envelope = 0.0f;
            return uint16_t(std::lround(envelope * 65535.0f));
        };
        const Rumble next{smooth(lowEnergy, low_), smooth(highEnergy, high_)};
        const bool silent = next.low == 0 && next.high == 0;
        if (silent && !active()) return std::nullopt;
        const auto elapsed = now - lastEmit_;
        if (!silent && lastEmit_ >= 0 &&
            (elapsed < 160000 || (next.low == last_.low && next.high == last_.high && elapsed < 2000000)))
            return std::nullopt;
        lastEmit_ = now;
        last_ = next;
        return next;
    }

private:
    std::array<float, 2> low1_{}, low2_{};
    float low_ = 0, high_ = 0;
    HostTime lastEmit_ = -1, legacyUntil_ = 0;
    Rumble last_;
};

}
