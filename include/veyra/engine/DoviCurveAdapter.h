#pragma once
// Map a frame's dynamic brightness metadata onto the existing static HDR curve.
//
// The curve engine is ours (six user parameters); the metadata only changes where
// the highlight handling starts for the current frame. Everything else - shadow
// lift, mid-gray, roll-off strength - stays exactly what the user dialled in, so
// enabling this never silently re-shades the picture.
//
// Used for two sources: Dolby Vision level 1 and HDR10+ (SMPTE ST 2094-40) scene
// metadata. Both arrive as DoviFrameInfo because the arithmetic only needs a peak
// and an average; HDR10+ marks itself with kHdr10PlusProfile.
//
// The PQ inverse below is the ST 2084 curve and matches libplacebo's
// pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, x).
//
// Custom addition for the Dolby Vision work; inert unless the caller opts in.
#include "veyra/engine/HdrOutputTuning.h"
#include "veyra/pipeline/FramePacket.h"
#include "veyra/pipeline/PqTransfer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra::engine {

// Both directions live in pipeline/PqTransfer.h so the source side (which reads
// HDR10+ scene metadata as linear nits) and this adapter cannot drift apart.
using pipeline::nitsToPq;
using pipeline::pqToNits;

inline HdrCurveSettings doviCurveFromLevel1(const pipeline::DoviFrameInfo& dovi,
                                            const HdrCurveSettings& userCurve,
                                            bool useAverage = false,
                                            double smoothedAvgPq = -1.0,
                                            double smoothedPeakPq = -1.0) {
    if (!dovi.valid) return userCurve;
    // `smoothedPeakPq` is the caller's smoothed peak. Falling back to the raw value
    // keeps single-shot and test callers working unchanged.
    double peakNits = pqToNits(smoothedPeakPq > 0.0 ? smoothedPeakPq : dovi.maxPq);
    // A scene that claims more than 4000 nits is outside every mastering standard
    // this project targets. Test material does contain such frames (the HDR10+
    // lake clip reports maxscl = 1.0, i.e. 10000 nits), and without this one frame
    // pins the knee at its ceiling and the next drops it, which is visible as
    // flicker. Clamping keeps a bad frame from moving the curve at all.
    constexpr double kMaxSanePeakNits = 4000.0;
    peakNits = std::min(peakNits, kMaxSanePeakNits);
    if (!(peakNits > 0.0)) return userCurve;
    HdrCurveSettings out = userCurve;
    // Start the roll-off a little below the frame's own peak so it has headroom to
    // work with, but never below the 100 nit floor the sliders allow.
    double kneeNits = peakNits * 0.85;
    if (useAverage) {
        // libplacebo's st2094_pick_knee uses the scene average rather than the peak:
        // a dark film's knee moves down (compresses earlier), a bright one moves up.
        // Keep it bounded so a very dark frame cannot drag the knee into the mids.
        const double avgPq = smoothedAvgPq >= 0.0 ? smoothedAvgPq : dovi.avgPq;
        const double avgNits = pqToNits(avgPq);
        if (avgNits > 0.0) kneeNits = std::max(avgNits * 1.2, kneeNits * 0.6);
    }
    const unsigned start = unsigned(std::lround(kneeNits));
    // Upper bound raised from 2000: the knee is peak*0.85, so a 3000-nit scene wants
    // 2550 and clamping it to the slider's own maximum held the curve at a plateau
    // and then released it in one step when the peak dropped. The user's own setting
    // still wins through the min() below.
    out.highlightStartNits = std::clamp(start, 100u, 4000u);
    // A start the user set below the frame's own knee stays: following may lift the
    // compression later in the frame, never pull it into the mid-tones.
    if (userCurve.highlightStartNits > 0)
        out.highlightStartNits = std::min(out.highlightStartNits, userCurve.highlightStartNits);
    return out;
}

} // namespace veyra::engine
