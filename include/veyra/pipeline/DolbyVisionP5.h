#pragma once
// Constant-buffer contract for the Dolby Vision profile-5 base-layer conversion
// (custom addition; upstream has no profile-5 path).
//
// YuvToLinearRgb.hlsl declares the block as seven float4s appended after the
// colour-grade constants, so the lane map here and the HLSL declaration must be
// changed together:
//
//   [ 0.. 2] ycc_to_rgb row 0        [ 3] curve[0].x
//   [ 4.. 6] ycc_to_rgb row 1        [ 7] curve[0].y
//   [ 8..10] ycc_to_rgb row 2        [11] curve[0].z
//   [12..14] lms_to_rgb row 0        [15] curve[1].x
//   [16..18] lms_to_rgb row 1        [19] curve[1].y
//   [20..22] lms_to_rgb row 2        [23] curve[1].z
//   [24..26] curve[2]                [27] 1.0 when the block is live, else 0.0
#include "veyra/pipeline/FramePacket.h"
#include <cstdint>

namespace veyra::pipeline {

inline constexpr int kDolbyVisionP5ConstantCount = 28;

// Written on every frame so a source switch back to non-Dolby Vision content
// cannot leave a stale enable flag behind (the flag lives in the block).
inline void packDolbyVisionP5Constants(const DolbyVisionP5& dv, float* out) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row * 4 + col] = dv.yccToRgb[row * 3 + col];
            out[12 + row * 4 + col] = dv.lmsToRgb[row * 3 + col];
        }
        out[row * 4 + 3] = dv.curve[0][row];
        out[12 + row * 4 + 3] = dv.curve[1][row];
    }
    out[24] = dv.curve[2][0];
    out[25] = dv.curve[2][1];
    out[26] = dv.curve[2][2];
    out[27] = dv.active ? 1.0f : 0.0f;
}

} // namespace veyra::pipeline
