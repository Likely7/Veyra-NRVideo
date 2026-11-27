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
        // The normalized phase correlation peak follows a sinc. The
        // three-point parabolic estimator undercounts fine movements.
        const auto denominator = 2. * m + l + r;
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
    enum class CalibrationFailure { None, NoResponse, MovingScene, InconsistentCurve };
    static constexpr unsigned kMinLatencySamples = 3;
    struct Calibration {
        AimCurve curve;
        CalibrationFailure failure = CalibrationFailure::None;
        unsigned measuredSteps = 0;
        double backgroundSpeed = 0., fullSpeed = 0., firstMotion = 0., fitError = 0.;
    };
    struct Status {
        State state = State::Waiting;
        bool fg = false;
        int percent = 0, progress = 0;
        uint32_t motionSamples = 0;
        double manualDeadzone = 0.;
        std::array<double, 2> latencyMs{};
        std::array<unsigned, 2> latencySamples{};
    };
    explicit AimTest(Mode mode, bool compareFg = false) : mode_(mode), compareFg_(compareFg) {
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
        if (manual && (key == 0x25 || key == 0x27) && status_.state == State::Running) {
            manualHold_ = true;
            status_.percent = std::clamp(status_.percent + (key == 0x25 ? -1 : 1), 0, 50);
        } else if (manual && key == 0x20 && status_.state == State::Running) {
            status_.manualDeadzone = status_.percent / 100.;
            status_.state = State::Complete;
        } else if (key == 0x1b) {
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
            if (elapsed >= 28000000) { status_.state = State::Unreliable; return {}; }
            // Start with a moment in neutral and leave time to confirm at 50%.
            if (!manualHold_) status_.percent = int(std::clamp<int64_t>((elapsed - 600000) / 400000, 0, 50));
            (mode_ == Mode::Vertical ? pad.rightY : pad.rightX) = int16_t(status_.percent * 32767 / 100);
            return pad;
        }
        if (mode_ == Mode::Latency) {
            constexpr int64_t cycle = 1400000;
            constexpr int64_t maxPulses = 8;
            const auto step = elapsed / cycle;
            const auto samples = latency_[status_.fg].size();
            if (step >= maxPulses || samples >= kTargetLatencySamples) {
                summarizeLatency(status_.fg);
                if (!status_.fg && compareFg_ && status_.latencySamples[0] >= kMinLatencySamples) {
                    status_.fg = true;
                    status_.progress = 50;
                    start_ = pulse_ = 0;
                    ready_ = false;
                } else {
                    status_.progress = 100;
                    status_.state = status_.latencySamples[0] >= kMinLatencySamples ? State::Complete : State::Unreliable;
                }
                return {};
            }
            const int phaseSize = compareFg_ ? 50 : 100;
            const int progress = std::max(int(step * phaseSize / maxPulses), int(samples * phaseSize / kTargetLatencySamples));
            status_.progress = int(status_.fg) * phaseSize + progress;
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

        status_.progress = int(std::min<int64_t>(99, elapsed * 100 / kFinishUs));
        if (elapsed < kScanStartUs) return pad;
        if (elapsed < kSweepStartUs) {
            const auto index = size_t((elapsed - kScanStartUs) / kScanStepUs);
            deflections_[index + 1] = index * .02;
            pad.rightX = int16_t(std::lround(deflections_[index + 1] * 32767.));
        } else if (elapsed < kHoldStartUs) {
            if (!scanDeadzone_) {
                double observed = .23;
                for (size_t i = 1; i <= kScanSteps; ++i)
                    if (counts_[i] >= 2 && mean(i) > std::max(1.5, 4. * mean(0))) { observed = deflections_[i]; break; }
                scanDeadzone_ = std::clamp(observed - .04, .01, .4);
            }
            const auto index = size_t((elapsed - kSweepStartUs) / kSweepStepUs);
            const auto bin = kSweepFirst + index;
            deflections_[bin] = std::lerp(*scanDeadzone_, 1., AimCurve::points[index]);
            pad.rightX = int16_t(std::lround(deflections_[bin] * (index % 2 == 0 ? 32767. : -32767.)));
        } else if (elapsed < kHoldEndUs) {
            pad.rightX = -32767;
        } else {
            pad = {};
            if (elapsed >= kFinishUs) {
                // The UI finalizes the fit. Keep the network sender's work
                // bounded once the final neutral state is sent.
                status_.state = State::Complete;
                status_.progress = 100;
            }
        }
        return pad;
    }

    Calibration calibration() const {
        std::array<double, kSweepEnd> velocity{}, deflection{};
        std::array<unsigned, kSweepEnd> counts{};
        double peak = 0.;
        {
            std::lock_guard lock(mutex_);
            deflection = deflections_;
            counts = counts_;
            for (size_t i = 0; i < velocity.size(); ++i) velocity[i] = mean(i);
            peak = peakSpeed_;
        }
        Calibration result;
        const double maximum = velocity.back();
        result.backgroundSpeed = velocity.front();
        result.fullSpeed = maximum;
        if (counts.back() < 3 || maximum < 8.) {
            result.failure = CalibrationFailure::NoResponse;
            return result;
        }
        if (maximum < 5. * result.backgroundSpeed) {
            result.failure = CalibrationFailure::MovingScene;
            return result;
        }
        const double motionFloor = std::max({.18, .002 * maximum, 4. * result.backgroundSpeed});
        for (size_t i = 1; i < kScanSteps; ++i) {
            if (counts[i] >= 2 && counts[i + 1] >= 2 && velocity[i] > motionFloor && velocity[i + 1] > motionFloor) {
                result.firstMotion = deflection[i];
                break;
            }
        }
        std::vector<double> inputs, relative;
        for (size_t i = 1; i < kSweepEnd; ++i) {
            if (deflection[i] <= 0. || counts[i] < 2 || velocity[i] < motionFloor) continue;
            inputs.push_back(deflection[i]);
            relative.push_back(std::min(1., velocity[i] / maximum));
        }
        result.measuredSteps = unsigned(inputs.size());
        const auto fit = AimCurve::fit(inputs, relative);
        result.fitError = fit.error;
        // Early saturation can drive the fit to its minimum exponent while
        // reporting a much larger dead zone. Leave that profile to manual calibration.
        const bool clippedCurve = fit.exponent <= .82 && counts[kSweepEnd - 2] >= 3 &&
            velocity[kSweepEnd - 2] >= .97 * maximum;
        const double expectedOnset = std::lerp(fit.deadzone, 1., std::pow(motionFloor / maximum, 1. / fit.exponent));
        const bool conflictsWithScan = result.firstMotion > 0. && expectedOnset > result.firstMotion + .04;
        // A fit at the upper exponent bound can explain high-stick samples
        // while guessing the dead zone. Keep the existing profile in that case.
        if (fit.error > .07 || fit.exponent >= 3.2 || clippedCurve || conflictsWithScan) {
            result.failure = CalibrationFailure::InconsistentCurve;
            return result;
        }
        result.curve = {fit.deadzone, fit.exponent, maximum, std::max(1., peak / maximum)};
        auto& curve = result.curve;
        // Monotonic measured curve with the fitted low end as an anchor.
        for (size_t i = 0; i < curve.points.size(); ++i) {
            const double stick = std::lerp(curve.deadzone, 1., curve.points[i]);
            size_t j = kSweepFirst;
            while (j < kSweepEnd - 1 && deflection[j] < stick) ++j;
            const double low = j == kSweepFirst ? curve.deadzone : deflection[j - 1];
            const double lowVelocity = j == kSweepFirst ? 0. : velocity[j - 1];
            const double weight = std::clamp((stick - low) / std::max(1e-6, deflection[j] - low), 0., 1.);
            const double rate = std::lerp(lowVelocity, velocity[j], weight) / maximum;
            curve.rates[i] = std::clamp(rate, i ? curve.rates[i - 1] : 0., 1.);
        }
        curve.rates.back() = 1.;
        return result;
    }

private:
    static constexpr unsigned kTargetLatencySamples = 5;
    static constexpr size_t kScanSteps = 21;
    static constexpr size_t kSweepFirst = kScanSteps + 1;
    static constexpr size_t kSweepEnd = kSweepFirst + AimCurve::points.size();
    static constexpr int64_t kScanStartUs = 600000, kScanStepUs = 450000, kScanSettleUs = 225000;
    static constexpr int64_t kSweepStepUs = 800000, kSweepSettleUs = 350000;
    static constexpr int64_t kSweepStartUs = kScanStartUs + kScanSteps * kScanStepUs;
    static constexpr int64_t kHoldStartUs = kSweepStartUs + AimCurve::points.size() * kSweepStepUs;
    static constexpr int64_t kHoldEndUs = kHoldStartUs + 2000000, kFinishUs = kHoldEndUs + 200000;

    struct MotionWindow { size_t bin; int64_t settledSince; };
    static MotionWindow calibrationWindow(int64_t at) {
        if (at >= kHoldStartUs) return {kSweepEnd, kHoldStartUs + kSweepSettleUs};
        if (at >= kSweepStartUs) {
            const auto index = (at - kSweepStartUs) / kSweepStepUs;
            return {kSweepFirst + size_t(index), kSweepStartUs + index * kSweepStepUs + kSweepSettleUs};
        }
        if (at >= kScanStartUs) {
            const auto index = (at - kScanStartUs) / kScanStepUs;
            return {1 + size_t(index), kScanStartUs + index * kScanStepUs + kScanSettleUs};
        }
        return {0, 0};
    }

    double mean(size_t i) const { return counts_[i] ? speeds_[i] / counts_[i] : 0.; }
    void summarizeLatency(size_t index) {
        auto& results = latency_[index];
        status_.latencySamples[index] = unsigned(results.size());
        if (results.size() < kMinLatencySamples) return;
        std::sort(results.begin(), results.end());
        status_.latencyMs[index] = (results[results.size() / 2] + results[(results.size() - 1) / 2]) * .5;
    }
    void analyze(std::stop_token stop) {
        std::deque<AimImage> history;
        int64_t windowStart = -1;
        while (!stop.stop_requested()) {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [this] { return !images_.empty(); });
            if (stop.stop_requested()) return;
            auto image = std::move(images_.front());
            images_.pop_front();
            const auto started = start_;
            lock.unlock();
            const auto host = image.presentUs;
            const auto at = host - started;
            const auto window = mode_ == Mode::Automatic ? calibrationWindow(at) : MotionWindow{0, 0};
            if (at < window.settledSince || (mode_ == Mode::Automatic && at >= kHoldEndUs)) continue;
            const auto since = started + window.settledSince;
            if (windowStart != since) {
                history.clear();
                windowStart = since;
            }
            if (!history.empty()) {
                if (host <= history.back().presentUs) continue;
                if (image.luma == history.back().luma) {
                    if (mode_ == Mode::Latency) history.back().presentUs = host;
                    continue;
                }
            }

            const bool slowStep = mode_ == Mode::Automatic && window.bin > 0 && window.bin < kSweepFirst + 6;
            int64_t delta = history.empty() ? 0 : host - history.back().presentUs;
            auto shift = !slowStep && delta > 0 && delta < 200000 ? aimImageShift(history.back(), image) : std::nullopt;
            if (mode_ == Mode::Automatic) {
                while (!history.empty() && (host - history.front().presentUs > 120000 || history.size() >= 24))
                    history.pop_front();
                // Use a longer interval across small shifts, but keep adjacent
                // frames for pans that would exceed the search radius.
                if (!history.empty() && (!shift || std::abs(*shift) < 1.25)) {
                    const auto interval = host - history.front().presentUs;
                    if (interval >= 90000) {
                        if (const auto longer = aimImageShift(history.front(), image)) {
                            shift = longer;
                            delta = interval;
                        }
                    }
                }
            } else {
                history.clear();
            }
            history.push_back(std::move(image));
            if (!shift) continue;
            const auto speed = std::abs(*shift) * 1000000. / delta;
            lock.lock();
            if (status_.state != State::Running || !ready_ || start_ != started) continue;
            ++status_.motionSamples;
            if (mode_ == Mode::Latency) {
                auto& results = latency_[status_.fg];
                if (pulse_ && pulse_ != lastPulse_ && host >= pulse_ && host - pulse_ < 600000 && std::abs(*shift) > .25) {
                    results.push_back((host - pulse_) / 1000.);
                    lastPulse_ = pulse_;
                }
            } else if (window.bin == kSweepEnd) {
                peakSpeed_ = std::max(peakSpeed_, speed);
            } else {
                speeds_[window.bin] += speed;
                ++counts_[window.bin];
            }
        }
    }

    const Mode mode_;
    const bool compareFg_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<AimImage> images_;
    Status status_;
    bool ready_ = false, manualHold_ = false;
    int64_t start_ = 0, pulse_ = 0, lastPulse_ = 0;
    std::optional<double> scanDeadzone_;
    std::array<double, kSweepEnd> speeds_{}, deflections_{};
    std::array<unsigned, kSweepEnd> counts_{};
    double peakSpeed_ = 0.;
    std::array<std::vector<double>, 2> latency_;
    std::jthread worker_;
};

} // namespace veyra::remoteplay
