#pragma once

#include "veyra/engine/PresentationSettings.h"

namespace veyra::engine {

enum class TearingHint { None, Driver, Nvidia };

struct ScanoutWindow {
    double seconds = 0, refreshHz = 0, medianMs = 0;
    uint64_t refreshes = 0, samples = 0, presentCalls = 0;
    DisplaySync sync = DisplaySync::Automatic;
    bool countersValid = false, vrrCapable = false, vrrActive = false;
};

// A sustained short DXGI timing is a hint. It cannot establish the actual
// scanout mode, detect torn pixels, or identify a driver override by itself.
class PresentTearingDetector {
public:
    bool sample(const ScanoutWindow& window) {
        if (reported_) return false;
        const bool validWindow = window.countersValid && window.seconds > 0 && window.seconds <= 1.6
            && std::isfinite(window.refreshHz) && window.refreshHz > 1 && window.samples
            && window.medianMs >= 0 && window.medianMs < 1;
        const bool modeChanged = presentationModeChanged(window.sync);
        const bool nearVrrCeiling = window.vrrCapable &&
            double(window.presentCalls) >= window.refreshHz * window.seconds * 0.9;
        if (!validWindow || window.vrrActive || nearVrrCeiling || modeChanged ||
            window.sync == DisplaySync::Tearing) {
            resetWindow();
            return false;
        }
        const double measuredHz = double(window.refreshes) / window.seconds;
        if (std::abs(measuredHz - window.refreshHz) > std::max(2.0, window.refreshHz * .05)) {
            resetWindow();
            return false;
        }
        seconds_ += window.seconds;
        samples_ += window.samples;
        reported_ = seconds_ >= 5 && samples_ >= 30;
        return reported_;
    }
    void resetWindow() { seconds_ = 0; samples_ = 0; }
    double seconds() const { return seconds_; }
    uint64_t samples() const { return samples_; }

private:
    bool presentationModeChanged(DisplaySync sync) {
        const bool changed = seconds_ > 0 && mode_ != sync;
        mode_ = sync;
        return changed;
    }
    DisplaySync mode_ = DisplaySync::Automatic;
    double seconds_ = 0;
    uint64_t samples_ = 0;
    bool reported_ = false;
};

} // namespace veyra::engine
