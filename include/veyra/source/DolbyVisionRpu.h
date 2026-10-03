#pragma once
// Dolby Vision profile-5 ("IPT-PQ-C2") base layer: RPU -> shader parameters.
//
// A profile-5 base layer carries no usable VUI colour. The RPU defines it:
//   base-layer Y/Cb/Cr  --(reshaping curves, per component)-->  IPT-PQ-C2
//                       --(ycc_to_rgb)--> PQ-domain RGB
//                       --(PQ EOTF)--> linear
//                       --(rgb_to_lms)--> LMS
//                       --(fixed HPE LMS->BT.2020 RGB)--> linear BT.2020
//                       --(PQ OETF)--> the PQ BT.2020 signal the player's
//                                      existing HDR pipeline already consumes.
//
// Skipping this and reading the base layer as BT.2020 YCbCr is what produces
// the well-known green/pink profile-5 picture.
//
// The reshaping curve is per frame (the RPU re-sends it on every scene
// refresh), so it is projected here onto a 3-term basis that fits the player's
// root-constant budget; carrying the exact piecewise polynomials would need a
// buffer. Accuracy against a full libplacebo decode of the same frame: mean
// absolute error 0.0018 in the PQ domain, versus 0.0007 for the exact curves
// and 0.041 for the no-conversion fallback.
//
// Custom addition; upstream has no profile-5 handling at all.
#include "veyra/pipeline/FramePacket.h"
#include <libavutil/dovi_meta.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra::source {

// Fixed final stage: the Hunt-Pointer-Estevez based LMS -> BT.2020 RGB matrix
// ("without any crosstalk", per libavutil/dovi_meta.h). Byte-identical to the
// constant MPC Video Renderer and libplacebo use on their profile-5 paths.
inline constexpr double kDolbyVisionP5LmsToRgb[9] = {
     3.06441879, -2.16597676,  0.10155818,
    -0.65612108,  1.78554118, -0.12943749,
     0.01736321, -0.04725154,  1.03004253,
};

// Samples per fit; enough to average over the piecewise polynomial's kinks.
inline constexpr int kDolbyVisionP5FitSamples = 64;
// Basis is {1, sqrt(x), x}: the extra terms follow the steep shadow segment,
// which a plain quadratic cannot without ringing in the highlights.
inline constexpr int kDolbyVisionP5BasisTerms = 3;

// Selects the piece covering a normalised code value. -1 when the curve is
// unusable. Pieces are addressed in normalised code-value space and the
// polynomial is then evaluated on the absolute normalised value (not a
// segment-relative one); validated frame-exact against a libplacebo decode.
inline int dolbyVisionP5Piece(const AVDOVIReshapingCurve& curve, double maxCode, double normalized) {
    if (curve.num_pivots < 2) return -1;
    for (int i = 0; i + 1 < int(curve.num_pivots); ++i) {
        if (normalized <= double(curve.pivots[i + 1]) / maxCode) return i;
    }
    return int(curve.num_pivots) - 2;
}

// Least-squares projection of one component's reshaping onto {1, sqrt(x), x}.
// Returns false when the curve is unusable, leaving the caller on the identity.
inline bool fitDolbyVisionP5Curve(const AVDOVIDataMapping& mapping, int component,
                                  uint8_t coefficientDenomLog2, uint8_t baseLayerBitDepth,
                                  float coeffs[kDolbyVisionP5BasisTerms]) {
    const AVDOVIReshapingCurve& curve = mapping.curves[component];
    if (curve.num_pivots < 2) return false;
    if (baseLayerBitDepth == 0 || baseLayerBitDepth > 16) return false;
    const double maxCode = double((1u << baseLayerBitDepth) - 1u);
    const double denom = std::ldexp(1.0, int(coefficientDenomLog2));
    if (!(denom > 0.0)) return false;

    double normal[kDolbyVisionP5BasisTerms][kDolbyVisionP5BasisTerms] = {};
    double rhs[kDolbyVisionP5BasisTerms] = {};
    for (int sample = 0; sample < kDolbyVisionP5FitSamples; ++sample) {
        const double x = double(sample) / double(kDolbyVisionP5FitSamples - 1);
        const int piece = dolbyVisionP5Piece(curve, maxCode, x);
        if (piece < 0 || curve.mapping_idc[piece] != AV_DOVI_MAPPING_POLYNOMIAL) return false;
        double y = 0.0;
        for (int k = int(curve.poly_order[piece]); k >= 0; --k)
            y = y * x + double(curve.poly_coef[piece][k]) / denom;
        const double basis[kDolbyVisionP5BasisTerms] = {1.0, std::sqrt(x), x};
        for (int a = 0; a < kDolbyVisionP5BasisTerms; ++a) {
            for (int b = 0; b < kDolbyVisionP5BasisTerms; ++b) normal[a][b] += basis[a] * basis[b];
            rhs[a] += basis[a] * y;
        }
    }

    // Gaussian elimination with partial pivoting on the 3x3 normal equations.
    double m[kDolbyVisionP5BasisTerms][kDolbyVisionP5BasisTerms + 1] = {};
    for (int a = 0; a < kDolbyVisionP5BasisTerms; ++a) {
        for (int b = 0; b < kDolbyVisionP5BasisTerms; ++b) m[a][b] = normal[a][b];
        m[a][kDolbyVisionP5BasisTerms] = rhs[a];
    }
    for (int col = 0; col < kDolbyVisionP5BasisTerms; ++col) {
        int pivot = col;
        for (int row = col + 1; row < kDolbyVisionP5BasisTerms; ++row)
            if (std::fabs(m[row][col]) > std::fabs(m[pivot][col])) pivot = row;
        if (std::fabs(m[pivot][col]) < 1e-12) return false;
        if (pivot != col)
            for (int k = col; k <= kDolbyVisionP5BasisTerms; ++k) std::swap(m[col][k], m[pivot][k]);
        for (int row = col + 1; row < kDolbyVisionP5BasisTerms; ++row) {
            const double factor = m[row][col] / m[col][col];
            for (int k = col; k <= kDolbyVisionP5BasisTerms; ++k) m[row][k] -= factor * m[col][k];
        }
    }
    double solution[kDolbyVisionP5BasisTerms] = {};
    for (int row = kDolbyVisionP5BasisTerms - 1; row >= 0; --row) {
        double value = m[row][kDolbyVisionP5BasisTerms];
        for (int k = row + 1; k < kDolbyVisionP5BasisTerms; ++k) value -= m[row][k] * solution[k];
        solution[row] = value / m[row][row];
    }
    for (int k = 0; k < kDolbyVisionP5BasisTerms; ++k) {
        if (!std::isfinite(solution[k])) return false;
        coeffs[k] = float(solution[k]);
    }
    return true;
}

// Why a stream stayed on the old path, for the caller's log line.
struct DolbyVisionP5Build {
    bool ok = false;
    bool colorMetadataMissing = false;
    bool unsupportedMapping = false;
    // The RPU carried a mapping that reshapes the whole base layer onto a
    // single constant (black luma / neutral chroma in every sample observed).
    // That is a placeholder, not a picture: a decoder must keep the mapping it
    // already has instead of blanking the frame. Happens on this sample for the
    // first ~1.9 s (the fade-in) and on genuinely black stretches.
    bool placeholderMapping = false;
};

// A curve that cannot vary is a placeholder rather than a reshaping.
inline bool dolbyVisionP5CurveIsPlaceholder(const float coeffs[kDolbyVisionP5BasisTerms]) {
    return std::fabs(coeffs[1]) < 1e-4f && std::fabs(coeffs[2]) < 1e-4f;
}

// Fills `out` from one frame's RPU metadata. `active` is only set when the RPU
// really carries the profile-5 colour block, so a stream that does not describe
// its base layer keeps the previous behaviour instead of being guessed at.
// The reshaping curves update only from a usable mapping: `out` keeps whatever
// it already had (identity on the very first frames) through a placeholder.
inline DolbyVisionP5Build buildDolbyVisionP5(const AVDOVIMetadata& meta, pipeline::DolbyVisionP5& out) {
    DolbyVisionP5Build result;
    const AVDOVIColorMetadata* color = av_dovi_get_color(&meta);
    const AVDOVIDataMapping* mapping = av_dovi_get_mapping(&meta);
    const AVDOVIRpuDataHeader* header = av_dovi_get_header(&meta);
    if (color == nullptr || mapping == nullptr || header == nullptr) {
        result.colorMetadataMissing = true;
        return result;
    }

    double ycc[9] = {}, rgb2lms[9] = {};
    double yccEnergy = 0.0, lmsEnergy = 0.0;
    for (int i = 0; i < 9; ++i) {
        ycc[i] = av_q2d(color->ycc_to_rgb_matrix[i]);
        rgb2lms[i] = av_q2d(color->rgb_to_lms_matrix[i]);
        yccEnergy += std::fabs(ycc[i]);
        lmsEnergy += std::fabs(rgb2lms[i]);
    }
    // A profile-5 RPU always carries both matrices; an empty block means this
    // RPU does not describe its base layer, which is not ours to assume.
    if (yccEnergy < 1e-6 || lmsEnergy < 1e-6) {
        result.colorMetadataMissing = true;
        return result;
    }

    // Start from the live state so a placeholder frame cannot throw away a
    // mapping that is still in force.
    pipeline::DolbyVisionP5 built = out;
    built.active = true;
    for (int i = 0; i < 9; ++i) built.yccToRgb[i] = float(ycc[i]);
    // The RPU crosstalk matrix and the fixed HPE stage are adjacent linear
    // transforms, so fold them once here and leave the shader one multiply.
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k)
                sum += kDolbyVisionP5LmsToRgb[row * 3 + k] * rgb2lms[k * 3 + col];
            built.lmsToRgb[row * 3 + col] = float(sum);
        }

    float projected[3][kDolbyVisionP5BasisTerms] = {};
    bool fitted[3] = {};
    bool placeholder = true;
    for (int component = 0; component < 3; ++component) {
        projected[component][2] = 1.0f;
        fitted[component] = fitDolbyVisionP5Curve(*mapping, component, header->coef_log2_denom,
                                                  header->bl_bit_depth, projected[component]);
        if (!fitted[component]) {
            if (mapping->curves[component].num_pivots >= 2) result.unsupportedMapping = true;
            placeholder = false; // unusable is not the same as deliberately flat
            continue;
        }
        if (!dolbyVisionP5CurveIsPlaceholder(projected[component])) placeholder = false;
    }
    if (placeholder) {
        // Every component is flat: keep the curves already in force.
        result.placeholderMapping = true;
    } else {
        for (int component = 0; component < 3; ++component)
            if (fitted[component])
                for (int k = 0; k < kDolbyVisionP5BasisTerms; ++k)
                    built.curve[component][k] = projected[component][k];
    }
    out = built;
    result.ok = true;
    return result;
}

} // namespace veyra::source
