#pragma once
// Profile 7 reconstruction parameters in the exact layout the YUV upload pass
// reads (dv0..dv6 plus the small coefficient texture on t14). Kept free of
// FFmpeg types so frame packets can carry it.
#include <cstdint>

namespace veyra::pipeline {

inline constexpr int kDoviP7FloatCount = 28;
// MMR weighting: per component, up to three orders, two float4s each (seven
// coefficients per order, the trailing lane unused). Same packing libplacebo's
// reshape shader consumes, so the GLSL and HLSL can be read side by side.
// Eight segments of seven vec4s each (six for MMR coefficients, one for the
// segment kind/order), followed by three vec4s holding the pivots.
inline constexpr int kDoviP7CurveVec4 = 59;
inline constexpr int kDoviP7CurveSegments = 8;
inline constexpr int kDoviP7Components = 3;

// Component modes, stored in params[21..23].
enum DoviP7CurveMode : int {
    DoviP7CurveIdentity = 0,   // base layer codes pass through unchanged
    DoviP7CurveMmr = 1,        // single-piece MMR mapping, coefficients in curve[]
    DoviP7CurveMulti = 2,      // segmented mapping, pivots and per-segment data in curve[]
};

struct DoviP7Constants {
    float params[kDoviP7FloatCount] = {};
    // curve[component][order * 2 + half]
    float curve[kDoviP7Components][kDoviP7CurveVec4][4] = {};
    bool ready = false;   // false: this RPU cannot be expressed, keep the base layer
};

} // namespace veyra::pipeline
