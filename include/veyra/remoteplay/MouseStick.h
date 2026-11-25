// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>

namespace veyra::remoteplay {

// Raw Input writes relative motion; the PS5 session samples it at send time.
// All timestamps use the local monotonic clock in microseconds.
class MouseStick {
public:
    struct Tuning {
        double horizontal = 1.0, vertical = 1.0, curve = 1.0, deadzone = 0.15, smoothingMs = 0;
        double turnBoost = 3.6;
        double inputDeadzone = 0;
        bool invertY = false;
        bool mouseLook = true;
        bool operator==(const Tuning&) const = default;
    };

    explicit MouseStick(Tuning tuning) : tuning_(tuning) {}

    void setTuning(Tuning tuning) {
        std::lock_guard lock(mutex_);
        tuning_ = tuning;
        clear();
    }

    void move(int dx, int dy, int64_t nowUs) {
        if (dx == 0 && dy == 0) return;
        std::lock_guard lock(mutex_);
        if (!tuning_.mouseLook) return;

        int64_t interval = DefaultPeriodUs;
        if (used_ > 0) {
            auto& last = reports_[newest_];
            if (nowUs < last.timeUs) return;
            interval = nowUs - last.timeUs;
            if (interval == 0) {
                // A batch can carry several reports stamped at the same time.
                last.dx += dx;
                last.dy += dy;
                return;
            }
            if (interval >= RestartUs) {
                clear();
                interval = DefaultPeriodUs;
            }
        }
        newest_ = (newest_ + 1) % reports_.size();
        reports_[newest_] = {nowUs, std::max<int64_t>(interval, 125), double(dx), double(dy)};
        used_ = std::min(used_ + 1, reports_.size());
    }

    std::pair<int16_t, int16_t> sample(int64_t nowUs) {
        std::lock_guard lock(mutex_);
        if (!used_) return {};

        const auto& last = reports_[newest_];
        const int64_t age = std::max<int64_t>(nowUs - last.timeUs, 0);
        // Hold through the next expected report plus normal scheduling jitter.
        const int64_t holdUs = std::clamp(last.periodUs + 4000, int64_t{6000}, int64_t{16000});
        if (age >= holdUs + ReleaseUs) {
            clear();
            return {};
        }

        // Time-weight the newest reports. A 125 Hz mouse keeps the same
        // velocity on intervening 250 Hz sends; there is no per-send count bucket.
        double dx = 0, dy = 0;
        int64_t measuredUs = 0;
        for (size_t i = 0; i < used_ && measuredUs < VelocityWindowUs; ++i) {
            const auto& report = reports_[(newest_ + reports_.size() - i) % reports_.size()];
            const int64_t overlap = std::min(report.periodUs, VelocityWindowUs - measuredUs);
            const double fraction = double(overlap) / report.periodUs;
            dx += report.dx * fraction;
            dy += report.dy * fraction;
            measuredUs += overlap;
        }
        const double velocityScale = 1000000.0 / double(measuredUs);
        const double velocityX = dx * velocityScale;
        const double velocityY = dy * velocityScale;
        const double rawSpeed = std::hypot(velocityX, velocityY);
        const double speed = std::max(0.0, rawSpeed - tuning_.inputDeadzone * AimVelocityScale);
        // Separate slow and fast gains as in XY-CtoPS5's mapping.py (see THIRD_PARTY_NOTICES.md).
        // Ease between them before applying axis sensitivity, so a swipe can
        // reach full deflection without sacrificing corrections near centre.
        const double turn = std::clamp((speed - AimSpeed) / (TurnSpeed - AimSpeed), 0.0, 1.0);
        const double turnBlend = turn * turn * (3.0 - 2.0 * turn);
        const double scale = rawSpeed > 0 ? (speed / rawSpeed) *
            std::lerp(1.0, tuning_.turnBoost, turnBlend) / AimVelocityScale : 0.0;
        const double x = velocityX * scale * tuning_.horizontal;
        const double y = velocityY * scale * tuning_.vertical * (tuning_.invertY ? -1.0 : 1.0);
        const double magnitude = std::hypot(x, y);
        const double deflection = std::min(magnitude, 1.0);
        // The aim curve rejoins the linear response below the fast-turn range.
        const double aim = std::min(deflection / AimDeflection, 1.0);
        const double curved = std::lerp(AimDeflection * std::pow(aim, tuning_.curve), deflection, aim);
        const double response = magnitude > 0 ?
            (tuning_.deadzone + (1.0 - tuning_.deadzone) * curved) / magnitude : 0;
        const int64_t filterTimeUs = std::min(nowUs, last.timeUs + holdUs);
        const int64_t elapsedUs = lastSampleUs_ < 0 ? 4000 :
            std::max<int64_t>(filterTimeUs - lastSampleUs_, 0);
        lastSampleUs_ = filterTimeUs;
        const double alpha = tuning_.smoothingMs > 0 ?
            -std::expm1(-double(elapsedUs) / (1000.0 * tuning_.smoothingMs)) : 1.0;
        filteredX_ += (x * response - filteredX_) * alpha;
        filteredY_ += (y * response - filteredY_) * alpha;

        const double tail = std::clamp(double(age - holdUs) / ReleaseUs, 0.0, 1.0);
        const double gain = 1.0 - tail * tail * (3.0 - 2.0 * tail);
        return {int16_t(std::lround(filteredX_ * gain * 32767.0)),
                int16_t(std::lround(filteredY_ * gain * 32767.0))};
    }

private:
    static constexpr int64_t DefaultPeriodUs = 8000;
    static constexpr int64_t VelocityWindowUs = 12000;
    static constexpr int64_t ReleaseUs = 12000;
    static constexpr int64_t RestartUs = 32000;
    // Mouse counts per second, before per-axis gains.
    static constexpr double AimVelocityScale = 5000;
    static constexpr double AimSpeed = 250;
    static constexpr double TurnSpeed = 1200;
    static constexpr double AimDeflection = 0.30;

    struct Report {
        int64_t timeUs;
        int64_t periodUs;
        double dx;
        double dy;
    };

    void clear() {
        used_ = 0;
        lastSampleUs_ = -1;
        filteredX_ = filteredY_ = 0;
    }

    std::mutex mutex_;
    Tuning tuning_;
    std::array<Report, 96> reports_{};
    size_t newest_ = 0, used_ = 0;
    int64_t lastSampleUs_ = -1;
    double filteredX_ = 0, filteredY_ = 0;
};

} // namespace veyra::remoteplay
