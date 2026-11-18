#pragma once
// Pack the Profile 7 BL+EL reconstruction parameters into the seven float4s the
// YUV upload pass already carries for Dolby Vision (dv0..dv6), plus the small
// coefficient texture the MMR curve needs.
//
// Why reuse the lanes: the root signature of that pass has all 60 of its usable
// constants spoken for, so the mapping coefficients travel on t14 instead.
// Profile 5 and Profile 7 are mutually exclusive, so the same lanes can carry
// either set once dv6.w says which one is live (1 = profile 5 colour conversion,
// 2 = profile 7 reconstruction, 3 = the same plus the residual drawn as a grey
// picture).
//
// Layout consumed by shaders/YuvToLinearRgb.hlsl:
//   dv0 = channel 0 (Y)  nlq: (offset, slope, threshold, inMax)
//   dv1 = channel 1 (Cb) nlq: (offset, slope, threshold, inMax)
//   dv2 = channel 2 (Cr) nlq: (offset, slope, threshold, inMax)
//   dv3 = (coefLog2Denom, elBitDepth, outBitDepth, 0)
//   dv4 = (elWidth, elHeight, mmrConstant[0], mmrConstant[1])
//   dv5 = (mmrConstant[2], mode[0], mode[1], mode[2])
//   dv6 = (order[0], order[1], order[2], mode-live flag)
// Custom addition for the P5 work.
#include <cstdint>
#include <format>
#include <string>

#include "veyra/pipeline/DoviP7Constants.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dovi_meta.h>
}

namespace veyra::pipeline {

// What the RPU's reshaping curves look like. This exists so the log can say WHY a
// stream is composited or kept as base layer only: "not ready" on its own hid the
// difference between a MEL layer, a multi-piece curve and an unsupported method.
struct DoviP7CurveReport {
    enum class Kind : uint8_t {
        Identity,      // codes pass through unchanged
        SinglePoly,    // one piece, polynomial order 1..2
        SingleMmr,     // one piece, MMR, order 1..3 (what Veyra can evaluate)
        MultiPiece,    // more than one piece, or per-piece methods differ
        Unsupported,   // a single non-identity piece that is not MMR
    };
    enum class Refusal : uint8_t {
        None,
        ResidualDisabled,  // MEL: the layer carries no residual at all
        ExtMapping,        // ext_mapping_idc_0_4 != 0
        NlqMethod,         // not LINEAR_DZ
        Curve,             // a component this shader cannot evaluate
    };
    Kind kind[3] = {Kind::Identity, Kind::Identity, Kind::Identity};
    uint8_t pieces[3] = {};      // num_pivots - 1
    uint8_t polyOrder[3] = {};
    uint8_t mmrOrder[3] = {};
    bool packable = false;       // every component was accepted
    Refusal refusal = Refusal::None;
};

// True when one reshaping curve is the identity on the codes this shader feeds
// it: every piece is a linear polynomial from zero with slope 1.0.
inline bool doviP7CurveIsIdentity(const AVDOVIReshapingCurve& curve, int coefLog2Denom) {
    // No curve at all means "nothing is mapped", which is also the identity.
    if (curve.num_pivots == 0) return true;
    if (curve.num_pivots < 2) return false;
    const int64_t one = int64_t(1) << coefLog2Denom;
    for (int i = 0; i + 1 < int(curve.num_pivots); ++i) {
        if (curve.mapping_idc[i] != AV_DOVI_MAPPING_POLYNOMIAL) return false;
        if (curve.poly_order[i] != 1) return false;
        if (curve.poly_coef[i][0] != 0 || curve.poly_coef[i][1] != one || curve.poly_coef[i][2] != 0)
            return false;
    }
    return true;
}

// Store one component's mapping. The shader adds the enhancement residual to the
// codes this produces, so a mapping it cannot evaluate has to be refused rather
// than approximated: those frames keep the plain base layer.
inline bool doviP7PackCurve(const AVDOVIReshapingCurve& curve, int coefLog2Denom,
                            int component, DoviP7Constants& out, DoviP7CurveReport& report) {
    const int pieces = curve.num_pivots > 0 ? int(curve.num_pivots) - 1 : 0;
    report.pieces[component] = uint8_t(pieces);
    if (doviP7CurveIsIdentity(curve, coefLog2Denom)) {
        report.kind[component] = DoviP7CurveReport::Kind::Identity;
        out.params[21 + component] = float(DoviP7CurveIdentity);
        out.params[24 + component] = 0.0f;
        return true;
    }
    // Segment layout inside curve[component][*][*]:
    //   [seg*6 + 0..5]  MMR coefficients (two vec4s per order) or the polynomial
    //                   coefficients in lane .x of the first three rows
    //   [seg*6 + 0].w   segment kind (0 = polynomial, 1 = MMR)
    //   [seg*6 + 1].w   order (polynomial 1..2, MMR 1..3)
    //   [18..20]        normalised pivots, [20].w = segment count
    // The first segment starts at row 0, so a single-piece curve keeps the exact
    // layout the shader already consumed.
    if (pieces < 1 || pieces > kDoviP7CurveSegments) {
        report.kind[component] = DoviP7CurveReport::Kind::Unsupported;
        return false;
    }
    const float scale = 1.0f / float(int64_t(1) << coefLog2Denom);
    bool anyMmr = false, anyPoly = false;
    for (int seg = 0; seg < pieces; ++seg) {
        const int base = seg * 7;
        if (curve.mapping_idc[seg] == AV_DOVI_MAPPING_POLYNOMIAL) {
            const int order = int(curve.poly_order[seg]);
            if (order < 1 || order > 2) {
                report.kind[component] = DoviP7CurveReport::Kind::Unsupported;
                return false;
            }
            // Coefficients share row 0 of the segment (.x/.y/.z = x^0/x^1/x^2),
            // matching PolyEvaluate(); .w stays the segment kind.
            for (int k = 0; k <= order; ++k)
                out.curve[component][base][k] = scale * float(curve.poly_coef[seg][k]);
            out.curve[component][base + 6][0] = 0.0f;
            out.curve[component][base + 6][1] = float(order);
            anyPoly = true;
        } else if (curve.mapping_idc[seg] == AV_DOVI_MAPPING_MMR) {
            const int order = int(curve.mmr_order[seg]);
            if (order < 1 || order > 3) {
                report.kind[component] = DoviP7CurveReport::Kind::Unsupported;
                return false;
            }
            out.curve[component][base + 6][0] = 1.0f;
            out.curve[component][base + 6][1] = float(order);
            if (seg == 0) out.params[18 + component] = scale * float(curve.mmr_constant[0]);
            for (int j = 0; j < order; ++j) {
                for (int k = 0; k < 3; ++k)
                    out.curve[component][base + j * 2][k] = scale * float(curve.mmr_coef[seg][j][k]);
                for (int k = 0; k < 4; ++k)
                    out.curve[component][base + j * 2 + 1][k] = scale * float(curve.mmr_coef[seg][j][3 + k]);
            }
            anyMmr = true;
        } else {
            report.kind[component] = DoviP7CurveReport::Kind::Unsupported;
            return false;
        }
    }
    // Normalised pivots (codes / 1023) so the shader compares them against its own
    // 0..1 signal without knowing the base-layer bit depth.
    constexpr float kPivotScale = 1.0f / 1023.0f;
    for (int i = 0; i < curve.num_pivots && i < 9; ++i)
        out.curve[component][kDoviP7CurveSegments * 7 + i / 4][i % 4] = float(curve.pivots[i]) * kPivotScale;
    out.curve[component][kDoviP7CurveVec4 - 1][3] = float(pieces);
    // A single-piece polynomial is NOT the identity: it has to go through the
    // segmented path so the shader evaluates PolyEvaluate instead of skipping the
    // component. (Writing Identity here meant non-identity luma curves packed
    // "successfully" and then did nothing.)
    const bool segmented = pieces > 1 || anyPoly;
    out.params[21 + component] = float(segmented ? DoviP7CurveMulti : DoviP7CurveMmr);
    out.params[24 + component] = float(segmented ? 0.0f : int(curve.mmr_order[0]));
    report.kind[component] = pieces > 1 ? DoviP7CurveReport::Kind::MultiPiece
                           : anyPoly ? DoviP7CurveReport::Kind::SinglePoly
                                     : DoviP7CurveReport::Kind::SingleMmr;
    if (anyPoly) report.polyOrder[component] = uint8_t(curve.poly_order[0]);
    report.mmrOrder[component] = uint8_t(anyMmr ? curve.mmr_order[0] : 0);
    return true;
}

// Supported so far: LINEAR_DZ quantisation on every component and an inverse
// mapping this shader can evaluate (identity or single-piece MMR). Anything else
// reports not-ready so the caller keeps the plain base-layer path instead of
// guessing; the CPU reference in DoviBlElCompose.h covers the general case.
// `reportOut` is diagnostic only and never changes the decision.
inline bool doviP7ConstantsSupported(const AVDOVIDataMapping& mapping,
                                     const AVDOVIRpuDataHeader& header,
                                     uint32_t elWidth, uint32_t elHeight,
                                     DoviP7Constants& out,
                                     DoviP7CurveReport* reportOut = nullptr) {
    DoviP7CurveReport report;
    out = DoviP7Constants{};
    const auto refuse = [&](DoviP7CurveReport::Refusal why) {
        report.refusal = why;
        if (reportOut != nullptr) *reportOut = report;
        return false;
    };
    if (header.disable_residual_flag != 0) return refuse(DoviP7CurveReport::Refusal::ResidualDisabled);
    if (header.ext_mapping_idc_0_4 != 0) return refuse(DoviP7CurveReport::Refusal::ExtMapping);
    if (mapping.nlq_method_idc != AV_DOVI_NLQ_LINEAR_DZ) return refuse(DoviP7CurveReport::Refusal::NlqMethod);
    for (int c = 0; c < 3; ++c) {
        if (!doviP7PackCurve(mapping.curves[c], int(header.coef_log2_denom), c, out, report))
            return refuse(DoviP7CurveReport::Refusal::Curve);
    }
    for (int c = 0; c < 3; ++c) {
        const auto& nlq = mapping.nlq[c];
        out.params[c * 4 + 0] = float(nlq.nlq_offset);
        out.params[c * 4 + 1] = float(nlq.linear_deadzone_slope);
        out.params[c * 4 + 2] = float(nlq.linear_deadzone_threshold);
        out.params[c * 4 + 3] = float(nlq.vdr_in_max);
    }
    out.params[12] = float(header.coef_log2_denom);
    out.params[13] = float(header.el_bit_depth);
    out.params[14] = float(header.vdr_bit_depth);
    out.params[15] = 0.0f;
    out.params[16] = float(elWidth);
    out.params[17] = float(elHeight);
    // dv6 is params[24..27]; the flag lives in .w, i.e. params[27]. Writing
    // params[24] (dv6.x) left the shader's dv6.w at zero, so nothing composited.
    out.params[27] = 2.0f;
    report.packable = true;
    if (reportOut != nullptr) *reportOut = report;
    out.ready = true;
    return true;
}

// One line for the log: which mapping each component carries and, when the RPU was
// refused, why. Purely descriptive text; nothing here feeds the renderer.
inline std::string doviP7CurveSummary(const DoviP7CurveReport& report, const AVDOVIRpuDataHeader& header) {
    const auto one = [&](int c) {
        switch (report.kind[c]) {
        case DoviP7CurveReport::Kind::Identity: return std::string("identity");
        case DoviP7CurveReport::Kind::SinglePoly:
            return std::format("poly(1piece,order{})", int(report.polyOrder[c]));
        case DoviP7CurveReport::Kind::SingleMmr:
            return std::format("mmr(1piece,order{})", int(report.mmrOrder[c]));
        case DoviP7CurveReport::Kind::MultiPiece:
            return std::format("multi-piece({}pieces)", int(report.pieces[c]));
        default:
            return std::format("unsupported({}pieces)", int(report.pieces[c]));
        }
    };
    const char* why = "supported";
    switch (report.refusal) {
    case DoviP7CurveReport::Refusal::None: break;
    case DoviP7CurveReport::Refusal::ResidualDisabled: why = "refused: residual disabled (MEL)"; break;
    case DoviP7CurveReport::Refusal::ExtMapping: why = "refused: ext_mapping_idc"; break;
    case DoviP7CurveReport::Refusal::NlqMethod: why = "refused: nlq method is not LINEAR_DZ"; break;
    case DoviP7CurveReport::Refusal::Curve: why = "refused: curve this shader cannot evaluate"; break;
    }
    return std::format("Y={} Cb={} Cr={} el={} vdr={} coefDenom={} ext={} -> {}",
        one(0), one(1), one(2), int(header.el_bit_depth), int(header.vdr_bit_depth),
        int(header.coef_log2_denom), int(header.ext_mapping_idc_0_4), why);
}

} // namespace veyra::pipeline
