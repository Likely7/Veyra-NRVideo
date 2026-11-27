#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace veyra::sink {

inline std::optional<double> originalVideoResidence(double hostMs, std::optional<int64_t> arrival100ns) {
    if (!arrival100ns || *arrival100ns <= 0 || !std::isfinite(hostMs)) return std::nullopt;
    const double arrival = double(*arrival100ns) / 10000.0;
    return arrival <= hostMs ? std::optional<double>(hostMs - arrival) : std::nullopt;
}

// A completed Present is recent even when that picture spent hundreds of
// milliseconds in the startup backlog. Wait only after observing that case.
class RemotePlayAudioStartup {
public:
    bool observe(double nowMs, std::optional<double> residence, bool distinctPicture) {
        if (ready_) return true;
        constexpr double freshPictureMs = 120.0;
        if (!residence && !firstStaleMs_) return true;
        if (residence && *residence <= freshPictureMs) {
            ready_ = true;
            return true;
        }
        if (!firstStaleMs_) firstStaleMs_ = nowMs;
        if (residence && distinctPicture) {
            constexpr double stableBandMs = 20.0;
            const auto low = std::min(minimumDelay_, *residence);
            const auto high = std::max(maximumDelay_, *residence);
            if (!stablePictures_ || high - low > stableBandMs) {
                minimumDelay_ = maximumDelay_ = *residence;
                stablePictures_ = 1;
            } else {
                minimumDelay_ = low;
                maximumDelay_ = high;
                ++stablePictures_;
            }
        }
        // Allow a genuinely slow but steady video path to start. Keep a time
        // limit for a variable path, independent of how often it reports.
        ready_ = stablePictures_ >= 3 || nowMs - *firstStaleMs_ >= 350.0;
        return ready_;
    }

private:
    std::optional<double> firstStaleMs_;
    double minimumDelay_ = 0.0, maximumDelay_ = 0.0;
    unsigned stablePictures_ = 0;
    bool ready_ = false;
};

inline size_t keepRemotePlayStartupPcm(size_t proposedDiscard, size_t queuedFrames, size_t prefillFrames,
                                       double targetMs, double inputBlockMs, unsigned rate) {
    // Keep the most recent sample frames for both channels. A temporary
    // mismatch in the two arrival clocks cannot flush newly collected PCM.
    const auto delayFrames = size_t(std::ceil((targetMs + inputBlockMs) * rate / 1000.0));
    const auto keep = std::min(queuedFrames, std::max(prefillFrames, delayFrames));
    return std::min(proposedDiscard, queuedFrames - keep);
}

}
