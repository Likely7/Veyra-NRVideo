#pragma once
// Pack the Profile 7 BL+EL reconstruction parameters into the seven float4s the
// YUV upload pass already carries for Dolby Vision (dv0..dv6).
//
// Why reuse them: the root signature of that pass has 60 of its 64 constants in
// use, and adding an SRV table for the enhancement layer changes the descriptor
// heap layout for every pass. Profile 5 and Profile 7 are mutually exclusive, so
// the same lanes can carry either set once dv6.w says which one is live
// (1 = profile 5 colour conversion, 2 = profile 7 reconstruction).
//
// Layout consumed by shaders/YuvToLinearRgb.hlsl:
//   dv0 = (nlqOffset, nlqSlope, nlqThreshold, inMax)
//   dv1 = (coefLog2Denom, elBitDepth, outBitDepth, 0)
//   dv2 = (elWidth, elHeight, 0, 0)
// Custom addition for the P5 work.
#include <cstdint>

#include "veyra/pipeline/DoviP7Constants.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dovi_meta.h>
}

namespace veyra::pipeline {

// Only the case the sample proves is supported so far: identity luma inverse
// mapping (polyOrder 1, slope 1.0), polynomial on all pieces, LINEAR_DZ
// quantisation. Anything else reports not-ready so the caller keeps the plain
// base-layer path instead of guessing.
inline bool doviP7ConstantsSupported(const AVDOVIDataMapping& mapping,
                                     const AVDOVIRpuDataHeader& header,
                                     uint32_t elWidth, uint32_t elHeight,
                                     DoviP7Constants& out) {
    out = DoviP7Constants{};
    if (header.disable_residual_flag != 0) return false;
    if (header.ext_mapping_idc_0_4 != 0) return false;
    if (mapping.nlq_method_idc != AV_DOVI_NLQ_LINEAR_DZ) return false;
    const auto& luma = mapping.curves[0];
    if (luma.num_pivots < 2) return false;
    for (int i = 0; i + 1 < int(luma.num_pivots); ++i) {
        if (luma.mapping_idc[i] != AV_DOVI_MAPPING_POLYNOMIAL) return false;
        if (luma.poly_order[i] != 1) return false;
        // slope 1.0 in 2^coef_log2_denom, offset 0, no quadratic term
        const int64_t one = int64_t(1) << header.coef_log2_denom;
        if (luma.poly_coef[i][0] != 0 || luma.poly_coef[i][1] != one || luma.poly_coef[i][2] != 0)
            return false;
    }
    const auto& nlq = mapping.nlq[0];
    out.params[0] = float(nlq.nlq_offset);
    out.params[1] = float(nlq.linear_deadzone_slope);
    out.params[2] = float(nlq.linear_deadzone_threshold);
    out.params[3] = float(nlq.vdr_in_max);
    out.params[4] = float(header.coef_log2_denom);
    out.params[5] = float(header.el_bit_depth);
    out.params[6] = float(header.vdr_bit_depth);
    out.params[7] = 0.0f;
    out.params[8] = float(elWidth);
    out.params[9] = float(elHeight);
    // dv6 is params[24..27]; the flag lives in .w, i.e. params[27]. Writing
    // params[24] (dv6.x) left the shader's dv6.w at zero, so nothing composited.
    out.params[27] = 2.0f;
    out.ready = true;
    return true;
}

} // namespace veyra::pipeline
