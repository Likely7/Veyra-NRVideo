// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace veyra::remoteplay {

struct AimCurve {
    static constexpr std::array<double, 9> points{.02, .05, .10, .20, .35, .50, .65, .80, 1.};
    double deadzone = 0., exponent = 2., maxPixelsPerSecond = 0., acceleration = 1.;
    std::array<double, points.size()> rates{};

    bool valid() const {
        return maxPixelsPerSecond > 0. && deadzone >= 0. && deadzone <= .4 &&
            std::is_sorted(rates.begin(), rates.end()) && rates.front() >= 0. && rates.back() == 1.;
    }
    bool operator==(const AimCurve&) const = default;

    double deflection(double rate) const {
        const auto next = std::lower_bound(rates.begin(), rates.end(), rate);
        if (next == rates.end()) return 1.;
        const size_t i = size_t(next - rates.begin());
        const double inverse = 1. / exponent;
        const double low = i ? std::pow(rates[i - 1], inverse) : 0.;
        const double lowPoint = i ? points[i - 1] : 0.;
        const double position = (std::pow(rate, inverse) - low) / std::max(1e-9, std::pow(*next, inverse) - low);
        return std::lerp(deadzone, 1., std::lerp(lowPoint, points[i], std::clamp(position, 0., 1.)));
    }

    // Fit only the deadzone and exponent. The inverse mapping uses the measured
    // velocities so games can have curves other than a single power law.
    struct Fit { double deadzone = 0., exponent = 2., error = 1.; };
    static Fit fit(std::span<const double> deflections, std::span<const double> relativeRates) {
        Fit best;
        if (deflections.size() != relativeRates.size() || deflections.size() < 4) return best;
        for (int d = 0; d <= 80; ++d) {
            const double deadzone = d * .005;
            for (int k = 0; k <= 120; ++k) {
                const double exponent = .8 + k * .02;
                double error = 0.;
                for (size_t i = 0; i < deflections.size(); ++i) {
                    const double expected = std::pow(std::max(0., (deflections[i] - deadzone) / (1. - deadzone)), exponent);
                    const double delta = expected - relativeRates[i];
                    error += delta * delta;
                }
                error = std::sqrt(error / deflections.size());
                if (error < best.error) best = {deadzone, exponent, error};
            }
        }
        return best;
    }
};

} // namespace veyra::remoteplay
