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
    report.pieces[component] = curve.num_pivots > 0 ? uint8_t(curve.num_pivots - 1) : uint8_t(0);
    if (doviP7CurveIsIdentity(curve, coefLog2Denom)) {
        report.kind[component] = DoviP7CurveReport::Kind::Identity;
        out.params[21 + component] = float(DoviP7CurveIdentity);
        out.params[24 + component] = 0.0f;
        return true;
    }
    // Single-piece MMR is what a real FEL stream uses for the chroma components.
    if (curve.num_pivots == 2 && curve.mapping_idc[0] == AV_DOVI_MAPPING_MMR) {
        const int order = int(curve.mmr_order[0]);
        if (order >= 1 && order <= 3) {
            report.kind[component] = DoviP7CurveReport::Kind::SingleMmr;
            report.mmrOrder[component] = uint8_t(order);
            const float scale = 1.0f / float(int64_t(1) << coefLog2Denom);
            out.params[18 + component] = scale * float(curve.mmr_constant[0]);
            out.params[21 + component] = float(DoviP7CurveMmr);
            out.params[24 + component] = float(order);
            for (int j = 0; j < order; ++j) {
                // Two vec4s per order, seven weights in total, exactly the order the
                // shader's MmrEvaluate() reads them back.
                for (int k = 0; k < 3; ++k) out.curve[component][j * 2][k] = scale * float(curve.mmr_coef[0][j][k]);
                for (int k = 0; k < 4; ++k) out.curve[component][j * 2 + 1][k] = scale * float(curve.mmr_coef[0][j][3 + k]);
            }
            return true;
        }
        report.kind[component] = DoviP7CurveReport::Kind::Unsupported;
        return false;
    }
    report.kind[component] = report.pieces[component] > 1
        ? DoviP7CurveReport::Kind::MultiPiece
        : DoviP7CurveReport::Kind::Unsupported;
    return false;
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
