// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/remoteplay/AimCurve.h"
#include "veyra/remoteplay/Types.h"
#include <atomic>
#include <array>
#include <bit>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

namespace veyra::remoteplay {

struct AimImage {
    static constexpr int width = 256, height = 144;
    int64_t presentUs = 0;
    std::array<uint8_t, width * height> luma{};
};

// Estimate a common horizontal shift from several background scanlines. Only
// the center band of rows is used and the central 20% is masked on middle
// rows. At least three textured rows must agree for a usable measurement.
inline std::optional<double> aimImageShift(const AimImage& before, const AimImage& after) {
    if (before.luma == after.luma) return std::nullopt;
    constexpr int n = AimImage::width;
    using Value = std::complex<double>;
    using Spectrum = std::array<Value, n>;
    static const auto roots = [] {
        Spectrum values;
        for (int i = 0; i < n; ++i) values[i] = std::polar(1., -2. * std::numbers::pi * i / n);
        return values;
    }();
    static const auto taper = [] {
        std::array<double, n> values{};
        for (int i = 0; i < n; ++i) values[i] = .5 - .5 * std::cos(2. * std::numbers::pi * i / (n - 1));
        return values;
    }();
    const auto fft = [](Spectrum& bins, bool reverse) {
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n / 2;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(bins[i], bins[j]);
        }
        for (int size = 2; size <= n; size *= 2)
            for (int start = 0; start < n; start += size)
                for (int k = 0; k < size / 2; ++k) {
                    const auto w = roots[n * k / size];
                    const auto a = bins[start + k];
                    const auto b = bins[start + k + size / 2] * (reverse ? std::conj(w) : w);
                    bins[start + k] = a + b;
                    bins[start + k + size / 2] = a - b;
                }
    };
    std::array<double, 12> shifts{};
    size_t used = 0;
    for (int y = 28; y <= 116; y += 8) {
        Spectrum a{}, b{};
        double texture = 0.;
        for (int x = 2; x < n - 2; ++x) {
            if (y >= 57 && y < 87 && x >= 102 && x < 154) continue;
            const int pos = y * n + x;
            // A gradient subtracts local DC (exposure differences).
            a[x] = (int(before.luma[pos + 1]) - int(before.luma[pos - 1])) * taper[x];
            b[x] = (int(after.luma[pos + 1]) - int(after.luma[pos - 1])) * taper[x];
            texture += std::norm(a[x]);
        }
        if (texture < 800.) continue;
        fft(a, false);
        fft(b, false);
        for (int i = 0; i < n; ++i) {
            const Value cross = b[i] * std::conj(a[i]);
            const double energy = std::abs(cross);
            a[i] = cross / std::max(energy, 100.);
        }
        fft(a, true);
        const auto corr = [&a](int dx) { return a[(dx + n) % n].real() / n; };
        int peak = 0;
        for (int d = -40; d <= 40; ++d) if (corr(d) > corr(peak)) peak = d;
        if (corr(peak) < .20 || std::abs(peak) == 40) continue;
        const double l = corr(peak - 1), m = corr(peak), r = corr(peak + 1);
        const auto denominator = 2. * (2. * m - l - r);
        const auto subpixel = denominator > 1e-5 ? std::clamp((r - l) / denominator, -.5, .5) : 0.;
        shifts[used++] = peak + subpixel;
    }
    if (used < 3) return std::nullopt;
    const auto middle = shifts.begin() + used / 2;
    std::nth_element(shifts.begin(), middle, shifts.begin() + used);
    const double result = *middle;
    if (std::count_if(shifts.begin(), shifts.begin() + used, [result](double v) {
            return std::abs(v - result) < std::max(.35, .15 * std::abs(result)); }) < 3) return std::nullopt;
    return result;
}

class AimTest {
public:
    enum class Mode { Horizontal = 1, Vertical, Automatic, Latency };
    enum class State { Waiting, Running, Complete, Cancelled, Unreliable };
    struct Status {
        State state = State::Waiting;
        bool fg = false;
        int percent = 0, progress = 0;
        uint32_t motionSamples = 0;
        double manualDeadzone = 0.;
        std::array<double, 2> latencyMs{};
        AimCurve curve;
    };
    explicit AimTest(Mode mode) : mode_(mode) {
        if (needsFrames()) worker_ = std::jthread([this](std::stop_token stop) { analyze(stop); });
    }
    Mode mode() const { return mode_; }
    bool needsFrames() const { return mode_ == Mode::Automatic || mode_ == Mode::Latency; }
    Status status() const { std::lock_guard lock(mutex_); return status_; }
    void ready(bool on) { std::lock_guard lock(mutex_); ready_ = on; }
    void cancel() {
        std::lock_guard lock(mutex_);
        if (status_.state == State::Running || status_.state == State::Waiting) status_.state = State::Cancelled;
    }
    bool key(int key, bool down) {
        std::lock_guard lock(mutex_);
        if (!down || (status_.state != State::Running && status_.state != State::Waiting)) return true;
        const bool manual = !needsFrames();
        if (manual && (key == 0x25 || key == 0x27)) {
            adjustment_ += key == 0x25 ? -1 : 1;
        } else if (manual && key == 0x20 && status_.state == State::Running) {
            status_.manualDeadzone = std::min(50, status_.percent + 1) / 100.;
            status_.state = State::Complete;
        } else {
            status_.state = State::Cancelled;
        }
        return true;
    }
    void frame(AimImage image) {
        if (!needsFrames()) return;
        std::lock_guard lock(mutex_);
        if (status_.state != State::Running || !ready_) return;
        if (images_.size() == 4) images_.pop_front();
        images_.push_back(std::move(image));
        wake_.notify_one();
    }

    ControllerState sample(int64_t now) {
        std::lock_guard lock(mutex_);
        if (!ready_ || (status_.state != State::Waiting && status_.state != State::Running)) return {};
        if (!start_) { start_ = now; status_.state = State::Running; }
        ControllerState pad;
        pad.inputActive = true;
        const auto elapsed = now - start_;
        if (!needsFrames()) {
            const int step = int(elapsed / 300000) + adjustment_;
            if (step > 50 || elapsed > 22000000) { status_.state = State::Unreliable; return {}; }
            status_.percent = std::clamp(step, 0, 50);
            (mode_ == Mode::Vertical ? pad.rightY : pad.rightX) = int16_t(status_.percent * 32767 / 100);
            return pad;
        }
        if (mode_ == Mode::Latency) {
            constexpr int64_t cycle = 1400000;
            const auto step = elapsed / cycle;
            if (step == 5 && !status_.fg) {
                status_.fg = true;
                status_.progress = 50;
                start_ = pulse_ = 0;
                ready_ = false;
                return {};
            }
            if (step >= 5) {
                finishLatency();
                return {};
            }
            status_.progress = (int(step) + 5 * int(status_.fg)) * 10;
            // Wait in neutral before each pulse so the previous turn settles.
            const auto position = elapsed % cycle;
            if (position >= 500000 && position < 700000) {
                if (!pulse_) pulse_ = now;
                pad.rightX = int16_t(step % 2 == 0 ? 32767 : -32767);
            } else if (position < 500000) {
                pulse_ = 0;
            }
            return pad;
        }

        constexpr int64_t scanStart = 500000, sweepStart = scanStart + 41 * 150000;
        constexpr int64_t accelStart = sweepStart + AimCurve::points.size() * 600000;
        status_.progress = int(std::min<int64_t>(99, elapsed * 100 / (accelStart + 2100000)));
        if (elapsed < scanStart) {
            motionIndex_ = 0;
        } else if (elapsed < sweepStart) {
            const auto index = int((elapsed - scanStart) / 150000);
            motionIndex_ = index + 1;
            deflections_[motionIndex_] = index * .01;
            pad.rightX = int16_t(std::lround(deflections_[motionIndex_] * 32767.));
        } else if (elapsed < accelStart) {
            if (!scanDeadzone_) {
                double observed = .23;
                for (size_t i = 1; i <= 41; ++i)
                    if (mean(i) > std::max(1.5, 4. * mean(0))) { observed = deflections_[i]; break; }
                scanDeadzone_ = std::clamp(observed - .04, .01, .4);
            }
            const size_t index = (elapsed - sweepStart) / 600000;
            motionIndex_ = 42 + index;
            deflections_[motionIndex_] = std::lerp(*scanDeadzone_, 1., AimCurve::points[index]);
            pad.rightX = int16_t(std::lround(deflections_[motionIndex_] * (index % 2 == 0 ? 32767. : -32767.)));
        } else if (elapsed < accelStart + 2000000) {
            motionIndex_ = 51;
            pad.rightX = -32767;
        } else {
            pad = {};
            if (elapsed >= accelStart + 2100000) {
                // The UI finalizes the fit. Keep the network sender's work
                // bounded once the final neutral state is sent.
                status_.state = State::Complete;
                status_.progress = 100;
            }
        }
        return pad;
    }

    AimCurve resultCurve() const {
        std::array<double, 52> velocity{};
        std::array<double, 52> deflection{};
        double peak = 0.;
        {
            std::lock_guard lock(mutex_);
            deflection = deflections_;
            for (size_t i = 0; i < velocity.size(); ++i) velocity[i] = mean(i);
            peak = peakSpeed_;
        }
        const double maximum = velocity[50];
        if (maximum < 15. || maximum < velocity[49] || maximum < 5. * velocity[0]) return {};
        std::vector<double> inputs, relative;
        for (size_t i = 1; i <= 50; ++i) {
            if (deflection[i] <= 0. || velocity[i] < 1.) continue;
            inputs.push_back(deflection[i]);
            relative.push_back(std::min(1., velocity[i] / maximum));
        }
        const auto fit = AimCurve::fit(inputs, relative);
        if (fit.error > .07) return {};
        AimCurve curve{fit.deadzone, fit.exponent, maximum, std::max(1., peak / maximum)};
        // Monotonic measured curve with the fitted low end as an anchor.
        for (size_t i = 0; i < curve.points.size(); ++i) {
            const double stick = std::lerp(curve.deadzone, 1., curve.points[i]);
            size_t j = 42;
            while (j < 50 && deflection[j] < stick) ++j;
            const double low = j == 42 ? curve.deadzone : deflection[j - 1];
            const double lowVelocity = j == 42 ? 0. : velocity[j - 1];
            const double weight = std::clamp((stick - low) / std::max(1e-6, deflection[j] - low), 0., 1.);
            const double rate = std::lerp(lowVelocity, velocity[j], weight) / maximum;
            curve.rates[i] = std::clamp(rate, i ? curve.rates[i - 1] : 0., 1.);
        }
        curve.rates.back() = 1.;
        return curve;
    }

private:
    double mean(size_t i) const { return counts_[i] ? speeds_[i] / counts_[i] : 0.; }
    void finishLatency() {
        for (size_t i = 0; i < latency_.size(); ++i) {
            if (latency_[i].size() != 5) { status_.state = State::Unreliable; return; }
            auto& results = latency_[i];
            std::sort(results.begin(), results.end());
            status_.latencyMs[i] = results[2];
        }
        status_.progress = 100;
        status_.state = State::Complete;
    }
    void analyze(std::stop_token stop) {
        std::optional<AimImage> previous;
        while (!stop.stop_requested()) {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [this] { return !images_.empty(); });
            if (stop.stop_requested()) return;
            auto image = std::move(images_.front());
            images_.pop_front();
            lock.unlock();
            if (previous && image.luma == previous->luma) continue;
            const auto delta = previous ? image.presentUs - previous->presentUs : 0;
            const auto shift = delta > 0 && delta < 200000 ? aimImageShift(*previous, image) : std::nullopt;
            previous = std::move(image);
            if (!shift) continue;
            const auto host = previous->presentUs;
            const auto speed = std::abs(*shift) * 1000000. / delta;
            lock.lock();
            if (status_.state != State::Running || !ready_) continue;
            ++status_.motionSamples;
            if (mode_ == Mode::Latency) {
                auto& results = latency_[status_.fg];
                if (pulse_ && pulse_ != lastPulse_ && host >= pulse_ && host - pulse_ < 600000 && std::abs(*shift) > .25) {
                    results.push_back((host - pulse_) / 1000.);
                    lastPulse_ = pulse_;
                }
            } else {
                const auto at = host - start_;
                constexpr int64_t scan = 500000, sweep = scan + 41 * 150000;
                constexpr int64_t accel = sweep + 9 * 600000;
                if (at < 0) continue;
                if (at >= accel) {
                    peakSpeed_ = std::max(peakSpeed_, speed);
                } else {
                    const auto index = at < scan ? 0 : at < sweep ? 1 + (at - scan) / 150000 : 42 + (at - sweep) / 600000;
                    const bool settled = at < scan ||
                        (at < sweep && (at - scan) % 150000 >= 90000) ||
                        (at >= sweep && (at - sweep) % 600000 >= 300000);
                    if (settled) {
                        speeds_[index] += speed;
                        ++counts_[index];
                    }
                }
            }
        }
    }

    const Mode mode_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<AimImage> images_;
    Status status_;
    bool ready_ = false;
    int adjustment_ = 0;
    int64_t start_ = 0, pulse_ = 0, lastPulse_ = 0;
    size_t motionIndex_ = 0;
    std::optional<double> scanDeadzone_;
    std::array<double, 52> speeds_{}, deflections_{};
    std::array<unsigned, 52> counts_{};
    double peakSpeed_ = 0.;
    std::array<std::vector<double>, 2> latency_;
    std::jthread worker_;
};

} // namespace veyra::remoteplay
