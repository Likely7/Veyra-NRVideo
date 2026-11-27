// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra::remoteplay {

// Converts PS5 Remote Play HD Haptics PCM (3000 Hz stereo signed 16-bit) into
// classic dual-motor rumble (low-frequency / high-frequency uint16_t) when no
// dedicated 4-channel USB DualSense audio endpoint is available (e.g. Bluetooth
// DualSense, Xbox/generic controllers, or Sunshine/Moonlight virtual gamepads).
//
// Uses a 1-pole low-pass / high-pass split around ~120 Hz at 3000 Hz sample
// rate so bass impacts drive the heavy low-frequency motor and fine surface
// textures drive the light high-frequency motor, with noise-floor deadbanding
// and rate-limited updates so HID output reports are not spammed.
class HapticsRumbleFallback {
public:
    struct Rumble {
        uint16_t low = 0;
        uint16_t high = 0;
    };

    struct Config {
        float lowPassAlpha = 0.22f;       // ~120 Hz cutoff at 3000 Hz sample rate
        float noiseFloor = 0.025f;        // suppress idle hiss / DC residue
        float gain = 1.8f;                // scale typical PS5 haptic RMS into motor range
        float attack = 0.70f;             // fast punch on transient impacts
        float release = 0.30f;            // smooth decay between bursts
        int64_t minInterval100ns = 160000; // ~62 Hz max HID rumble update rate
    };

    HapticsRumbleFallback() = default;
    explicit HapticsRumbleFallback(Config cfg) : cfg_(cfg) {}

    void reset() {
        lp1Left_ = lp1Right_ = lp2Left_ = lp2Right_ = 0.0f;
        envLow_ = envHigh_ = 0.0f;
        lastLow_ = lastHigh_ = 0;
        active_ = false;
        lastEmit100ns_ = 0;
    }

    bool active() const { return active_; }

    // Feeds a chunk of interleaved stereo int16_t haptics PCM and returns a
    // rumble command when motors should be updated (or immediately stopped).
    std::optional<Rumble> feed(std::span<const int16_t> stereoPcm, int64_t now100ns) {
        if (stereoPcm.size() < 2) return std::nullopt;
        const size_t frames = stereoPcm.size() / 2;
        double lowSumSq = 0.0;
        double highSumSq = 0.0;
        const float a = std::clamp(cfg_.lowPassAlpha, 0.01f, 0.99f);

        for (size_t i = 0; i < frames; ++i) {
            const float l = float(stereoPcm[i * 2]) / 32768.0f;
            const float r = float(stereoPcm[i * 2 + 1]) / 32768.0f;
            lp1Left_ += a * (l - lp1Left_);
            lp1Right_ += a * (r - lp1Right_);
            lp2Left_ += a * (lp1Left_ - lp2Left_);
            lp2Right_ += a * (lp1Right_ - lp2Right_);
            const float hpLeft = l - lp1Left_;
            const float hpRight = r - lp1Right_;
            lowSumSq += 0.5 * (double(lp2Left_) * lp2Left_ + double(lp2Right_) * lp2Right_);
            highSumSq += 0.5 * (double(hpLeft) * hpLeft + double(hpRight) * hpRight);
        }

        const float lowRms = float(std::sqrt(lowSumSq / double(frames)));
        const float highRms = float(std::sqrt(highSumSq / double(frames)));

        const float targetLow = mapAmplitude(lowRms);
        const float targetHigh = mapAmplitude(highRms);

        envLow_ += (targetLow > envLow_ ? cfg_.attack : cfg_.release) * (targetLow - envLow_);
        envHigh_ += (targetHigh > envHigh_ ? cfg_.attack : cfg_.release) * (targetHigh - envHigh_);
        if (envLow_ < 0.01f) envLow_ = 0.0f;
        if (envHigh_ < 0.01f) envHigh_ = 0.0f;

        const uint16_t low = uint16_t(std::lround(std::clamp(envLow_, 0.0f, 1.0f) * 65535.0f));
        const uint16_t high = uint16_t(std::lround(std::clamp(envHigh_, 0.0f, 1.0f) * 65535.0f));

        const bool stopping = active_ && low == 0 && high == 0;
        if (!stopping && low == 0 && high == 0) return std::nullopt;

        const bool largeJump =
            std::abs(int(low) - int(lastLow_)) >= 8192 ||
            std::abs(int(high) - int(lastHigh_)) >= 8192;
        const bool due = !active_ || lastEmit100ns_ == 0 ||
            (now100ns - lastEmit100ns_) >= cfg_.minInterval100ns;
        if (!stopping && !largeJump && !due) return std::nullopt;

        // Avoid redundant identical non-zero writes within 200 ms.
        if (!stopping && low == lastLow_ && high == lastHigh_ &&
            lastEmit100ns_ != 0 && (now100ns - lastEmit100ns_) < 2000000) {
            return std::nullopt;
        }

        lastLow_ = low;
        lastHigh_ = high;
        lastEmit100ns_ = now100ns;
        active_ = (low != 0 || high != 0);
        return Rumble{low, high};
    }

private:
    float mapAmplitude(float rms) const {
        if (!std::isfinite(rms) || rms <= cfg_.noiseFloor) return 0.0f;
        const float normalized = (rms - cfg_.noiseFloor) / std::max(1e-4f, 1.0f - cfg_.noiseFloor);
        // Mild power curve preserves tactile feel on light footsteps while
        // keeping clean band separation between bass impacts and fine textures.
        return std::clamp(std::pow(normalized * cfg_.gain, 0.75f), 0.0f, 1.0f);
    }

    Config cfg_{};
    float lp1Left_ = 0.0f, lp1Right_ = 0.0f, lp2Left_ = 0.0f, lp2Right_ = 0.0f;
    float envLow_ = 0.0f, envHigh_ = 0.0f;
    uint16_t lastLow_ = 0, lastHigh_ = 0;
    bool active_ = false;
    int64_t lastEmit100ns_ = 0;
};

} // namespace veyra::remoteplay
