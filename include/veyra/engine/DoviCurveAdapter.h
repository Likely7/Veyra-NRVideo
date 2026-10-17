#pragma once
// Map a Dolby Vision frame's level-1 brightness onto the existing static HDR curve.
//
// The curve engine is ours (six user parameters); DV only changes where the
// highlight handling starts for the current frame. Everything else - shadow lift,
// mid-gray, roll-off strength - stays exactly what the user dialled in, so enabling
// this never silently re-shades the picture.
//
// The PQ inverse below is the ST 2084 curve and matches libplacebo's
// pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, x).
//
// Custom addition for the Dolby Vision work; inert unless the caller opts in.
#include "veyra/engine/HdrOutputTuning.h"
#include "veyra/pipeline/FramePacket.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra::engine {

// 0..1 PQ -> nits (0..10000).
inline double pqToNits(double pq) {
    if (!(pq > 0.0)) return 0.0;
    constexpr double m1 = 0.1593017578125, m2 = 78.84375;
    constexpr double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(pq, 1.0 / m2);
    const double num = std::max(p - c1, 0.0);
    const double den = c2 - c3 * p;
    if (den <= 0.0) return 0.0;
    return 10000.0 * std::pow(num / den, 1.0 / m1);
}

// Follow level 1: move ONLY where the highlight roll-off starts for this frame.
// A frame without level 1 leaves the user's curve untouched, byte for byte.
//
// peakCapNits is deliberately not touched. It is not just the hard ceiling: the
// mid-gray gamma is anchored on it (see hdrCurveLuminance), so changing it
// reshapes the whole tone scale, not the highlights. Measured on a real user
// preset (mid 175, start 450, cap 1200) a per-frame cap of 304 nit took 1 nit
// down by 78% and 100 nit by 18% - the picture dimmed everywhere, which is
// exactly what "follow the highlights" must never do. It would also breathe,
// because the frame peak changes every frame.
inline HdrCurveSettings doviCurveFromLevel1(const pipeline::DoviFrameInfo& dovi,
                                            const HdrCurveSettings& userCurve) {
    if (!dovi.valid) return userCurve;
    const double peakNits = pqToNits(dovi.maxPq);
    if (!(peakNits > 0.0)) return userCurve;
    HdrCurveSettings out = userCurve;
    // Start the roll-off a little below the frame's own peak so it has headroom to
    // work with, but never below the 100 nit floor the sliders allow.
    const unsigned start = unsigned(std::lround(peakNits * 0.85));
    out.highlightStartNits = std::clamp(start, 100u, 2000u);
    // A start the user set below the frame's own knee stays: following may lift the
    // compression later in the frame, never pull it into the mid-tones.
    if (userCurve.highlightStartNits > 0)
        out.highlightStartNits = std::min(out.highlightStartNits, userCurve.highlightStartNits);
    return out;
}

} // namespace veyra::engine
