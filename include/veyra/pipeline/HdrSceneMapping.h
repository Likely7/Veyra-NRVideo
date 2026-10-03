#pragma once
// Custom addition: per-scene HDR brightness measurement.
//
// The graph already builds a 256-bin luma histogram every frame for its
// cadence/scene detection and anti-flicker work, and the scene-change signal
// comes from the same place, so the measurement this needs is free: no extra
// GPU pass and no readback.
//
// For a PQ source the sampled value is the high byte of the P010/P016 luma
// plane (see EnhanceGraph::analyzeLuma), i.e. the PQ code at 8-bit precision, so
// a bin index converts to absolute nits with the PQ EOTF below. Only HDR sources
// reach this path: an SDR source keeps its own tone map.
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veyra::pipeline {

struct HdrSceneMeasurement {
    float gain = 1.0f;          // exposure that brings the scene to the anchor
    float sourcePeakNits = 0.0f; // peak the tone map should treat the scene as having
    float brightNits = 0.0f;     // p90, for the log line
    float peakNits = 0.0f;       // p99.5, for the log line
};

// ST2084 EOTF: normalised PQ code -> absolute cd/m2.
inline float pqCodeToNits(float code) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float q = std::pow(std::max(code, 0.0f), 1.0f / m2);
    const float num = std::max(q - c1, 0.0f);
    const float den = std::max(c2 - c3 * q, 1e-6f);
    return 10000.0f * std::pow(num / den, 1.0f / m1);
}

// Luminance at or below this is letterbox black or near black. It carries no
// scene brightness information and would otherwise drag a percentile to zero on a
// frame with wide black bars - observed on the field sample, where p90 came back
// as 0 nit on two of four scenes and pinned the gain at its 4x ceiling, which is
// exactly the "dark scene looks washed out" case.
inline constexpr float kSceneBlackFloorNits = 0.1f;

// Bin index of a cumulative fraction, linearly placed inside the bin. The
// near-black tail is skipped unless the frame is entirely near black.
template <typename T>
inline float histogramPercentileNits(const T* histogram, std::size_t bins, float fraction) {
    if (bins < 2) return 0.0f;
    std::size_t first = 0;
    while (first + 1 < bins && pqCodeToNits(float(first) / float(bins - 1)) < kSceneBlackFloorNits) ++first;
    double total = 0.0;
    for (std::size_t i = first; i < bins; ++i) total += double(histogram[i]);
    if (total <= 0.0) { total = 0.0; first = 0; for (std::size_t i = 0; i < bins; ++i) total += double(histogram[i]); }
    if (total <= 0.0) return 0.0f;
    const double want = double(std::clamp(fraction, 0.0f, 1.0f)) * total;
    double seen = 0.0;
    for (std::size_t i = first; i < bins; ++i) {
        const double next = seen + double(histogram[i]);
        if (next >= want && double(histogram[i]) > 0.0) {
            const double within = (want - seen) / double(histogram[i]);
            const double bin = double(i) + std::clamp(within, 0.0, 1.0);
            return pqCodeToNits(float(bin / double(bins - 1)));
        }
        seen = next;
    }
    return pqCodeToNits(1.0f);
}

// The scene's brightness is read from a high percentile rather than the mean so
// that letterbox bars and large black areas do not read as "a dark scene".
template <typename T>
inline HdrSceneMeasurement measureHdrScene(const T* histogram, std::size_t bins, float targetPeakNits) {
    HdrSceneMeasurement m;
    // p95 rather than p90: with black excluded the reference should still ignore
    // the dim bulk of a mostly dark frame and follow the part that reads as lit.
    m.brightNits = histogramPercentileNits(histogram, bins, 0.95f);
    m.peakNits = histogramPercentileNits(histogram, bins, 0.995f);
    // Anchor: what a normally exposed scene should put at p95, scaled with the
    // display so the same content behaves the same on a 1000 or a 2000 nit panel.
    // 4% rather than 6%: a dark scene must not be lifted all the way to the
    // ceiling just because it is dark.
    const float anchor = std::max(1.0f, targetPeakNits * 0.04f);
    // Ceiling/floor kept tight on purpose: +-1.2 stops is what still reads as a
    // correction rather than as the picture breathing. Strength scales inside it.
    m.gain = std::clamp(anchor / std::max(m.brightNits, 0.5f), 0.6f, 2.2f);
    // Headroom over the measured peak: the proxy is 8-bit and a frame can lack
    // the scene's brightest shot. The floor keeps the map from ever expanding
    // the range, so a scene below the target is left alone.
    m.sourcePeakNits = std::clamp(m.peakNits * 1.5f, targetPeakNits * 1.02f, 10000.0f);
    return m;
}

} // namespace veyra::pipeline
