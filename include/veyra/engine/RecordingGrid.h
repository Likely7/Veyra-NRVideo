#pragma once

#include <algorithm>
#include <cstdint>

namespace veyra::engine {

// A fixed, zero-based recording timeline. Capture and audio use microseconds;
// MP4 video uses integer frame indices to keep every sample exactly one frame.
class RecordingGrid {
public:
    explicit constexpr RecordingGrid(uint32_t fps) : fps_(std::clamp(fps,1u,480u)) {}

    constexpr int64_t timeUs(int64_t index) const {
        return (index * kUsPerSecond + fps_ / 2) / fps_;
    }

    constexpr int64_t indexAt(int64_t elapsedUs) const {
        return (elapsedUs * fps_ + kUsPerSecond / 2) / kUsPerSecond;
    }

    constexpr int64_t deadlineUs(int64_t index) const {
        // Round up so a frame on the last eligible clock tick can still win.
        return ((2 * index + 1) * kUsPerSecond + 2 * fps_ - 1) / (2 * fps_);
    }

private:
    static constexpr int64_t kUsPerSecond = 1000000;
    int64_t fps_;
};

} // namespace veyra::engine
