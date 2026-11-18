#pragma once
// libdovi bridge: decode a raw Dolby Vision RPU NAL into the AVDOVIMetadata
// struct the rest of the pipeline already consumes (doviP7ConstantsSupported,
// extractDoviFrameInfo, the P5 builder).
//
// Why a bridge: FFmpeg only parses an RPU when the base-layer SPS carries a
// dolby_vision_configuration record. Test material produced by Dolby's own tools
// (e.g. FEL_test_for_AVS) carries the RPU in the enhancement-layer stream and no
// config record, so FFmpeg never exposes it. libdovi (MIT) parses the RPU
// payload itself, including those variants.
//
// Scope: header, reshaping mapping and colour metadata. Extension blocks (L1/L2/
// L5 ...) are not copied: the reconstruction path only reads header/mapping, and
// the HDR metadata levels are already handled by FFmpeg's own side data when a
// config record exists.

#include <cstdint>
#include <cstring>

extern "C" {
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libdovi/rpu_parser.h>
}

namespace veyra::source {

// Allocate through FFmpeg so alignment and extension-block layout track its ABI.
inline AVDOVIMetadata* allocDoviMetadata() { return av_dovi_metadata_alloc(nullptr); }

// Copies libdovi's parsed fields into `out` (allocated with
// av_dovi_metadata_alloc by the caller). Returns false when the payload is not a
// type-2 HEVC RPU or the mapping is absent.
inline bool decodeRpuWithLibDovi(const uint8_t* data, size_t size, AVDOVIMetadata* out) {
    if (data == nullptr || out == nullptr || size < 5) return false;
    DoviRpuOpaque* rpu = dovi_parse_unspec62_nalu(data, size);
    if (rpu == nullptr) return false;
    const DoviRpuDataHeader* header = dovi_rpu_get_header(rpu);
    if (header == nullptr) { dovi_rpu_free(rpu); return false; }
    auto* hdr = av_dovi_get_header(out);
    if (hdr == nullptr) { dovi_rpu_free_header(header); dovi_rpu_free(rpu); return false; }
    hdr->rpu_type = header->rpu_type;
    hdr->rpu_format = header->rpu_format;
    hdr->vdr_rpu_profile = header->vdr_rpu_profile;
    hdr->vdr_rpu_level = header->vdr_rpu_level;
    hdr->chroma_resampling_explicit_filter_flag = header->chroma_resampling_explicit_filter_flag;
    hdr->coef_data_type = header->coefficient_data_type;
    hdr->coef_log2_denom = uint8_t(header->coefficient_log2_denom);
    hdr->vdr_rpu_normalized_idc = header->vdr_rpu_normalized_idc;
    hdr->bl_video_full_range_flag = header->bl_video_full_range_flag;
    hdr->bl_bit_depth = uint8_t(header->bl_bit_depth_minus8 + 8);
    hdr->el_bit_depth = uint8_t(header->el_bit_depth_minus8 + 8);
    hdr->vdr_bit_depth = uint8_t(header->vdr_bit_depth_minus8 + 8);
    hdr->spatial_resampling_filter_flag = header->spatial_resampling_filter_flag;
    hdr->el_spatial_resampling_filter_flag = header->el_spatial_resampling_filter_flag;
    hdr->disable_residual_flag = header->disable_residual_flag;
    hdr->ext_mapping_idc_0_4 = 0;
    hdr->ext_mapping_idc_5_7 = 0;
    // Needed when rebuilding fixed-point coefficients (poly_coef = int << denom + frac).
    const int coefLog2Denom = int(header->coefficient_log2_denom);
    dovi_rpu_free_header(header);

    const DoviRpuDataMapping* mapping = dovi_rpu_get_data_mapping(rpu);
    if (mapping == nullptr) { dovi_rpu_free(rpu); return false; }
    auto* dst = av_dovi_get_mapping(out);
    if (dst == nullptr) { dovi_rpu_free_data_mapping(mapping); dovi_rpu_free(rpu); return false; }
    dst->vdr_rpu_id = uint8_t(mapping->vdr_rpu_id);
    dst->mapping_color_space = uint8_t(mapping->mapping_color_space);
    dst->mapping_chroma_format_idc = uint8_t(mapping->mapping_chroma_format_idc);
    dst->num_x_partitions = uint32_t(mapping->num_x_partitions_minus1 + 1);
    dst->num_y_partitions = uint32_t(mapping->num_y_partitions_minus1 + 1);
    dst->nlq_method_idc = AV_DOVI_NLQ_LINEAR_DZ;   // the only method real streams use
    // Non-linear quantisation: P7Residual() divides by these, so leaving them zero
    // (the memset default) silently dropped every enhancement residual.
    if (mapping->nlq != nullptr) {
        const DoviRpuDataNlq* n = mapping->nlq;
        for (int c = 0; c < 3; ++c) {
            dst->nlq[c].nlq_offset = uint16_t(n->nlq_offset[c]);
            dst->nlq[c].vdr_in_max =
                (n->vdr_in_max_int[c] << coefLog2Denom) + n->vdr_in_max[c];
            dst->nlq[c].linear_deadzone_slope =
                (n->linear_deadzone_slope_int[c] << coefLog2Denom) + n->linear_deadzone_slope[c];
            dst->nlq[c].linear_deadzone_threshold =
                (n->linear_deadzone_threshold_int[c] << coefLog2Denom) + n->linear_deadzone_threshold[c];
        }
    }

    for (int c = 0; c < 3; ++c) {
        const DoviReshapingCurve& src = mapping->curves[c];
        AVDOVIReshapingCurve* curve = &dst->curves[c];
        const int pivots = int(src.num_pivots_minus2) + 2;
        if (pivots < 2 || pivots > AV_DOVI_MAX_PIECES + 1) {
            dovi_rpu_free_data_mapping(mapping);
            dovi_rpu_free(rpu);
            return false;
        }
        curve->num_pivots = uint8_t(pivots);
        // libdovi hands back the per-pivot deltas; AVDOVIReshapingCurve wants the
        // absolute (accumulated) values.
        int acc = 0;
        for (int i = 0; i < pivots && size_t(i) < src.pivots.len; ++i) {
            acc += src.pivots.data[i];
            curve->pivots[i] = uint16_t(acc);
        }
        const int pieces = pivots - 1;
        for (int i = 0; i < pieces; ++i) {
            curve->mapping_idc[i] = AVDOVIMappingMethod(src.mapping_idc);
        }
        if (src.mapping_idc == AV_DOVI_MAPPING_POLYNOMIAL) {
            const DoviPolynomialCurve* poly = src.polynomial;
            if (poly == nullptr) continue;
            for (int i = 0; i < pieces; ++i) {
                const int order = int(poly->poly_order_minus1.data[i]) + 1;
                // AVDOVIReshapingCurve::poly_coef holds three coefficients (x^0..x^2);
                // anything larger would write past the row.
                if (order < 1 || order > 2) break;
                curve->poly_order[i] = uint8_t(order);
                // libdovi splits a fixed-point coefficient into an integer part
                // (poly_coef_int) and a fractional part (poly_coef). Reading only
                // the fraction turned an identity curve (0, 1, 0) into (0, 0, 0),
                // which zeroed luma on the AVS material.
                const DoviI64Data* intCoef = size_t(i) < poly->poly_coef_int.len
                    ? poly->poly_coef_int.list[i] : nullptr;
                const DoviU64Data* fracCoef = size_t(i) < poly->poly_coef.len
                    ? poly->poly_coef.list[i] : nullptr;
                for (int k = 0; k <= order; ++k) {
                    const int64_t iv = (intCoef != nullptr && size_t(k) < intCoef->len)
                        ? int64_t(intCoef->data[k]) : 0;
                    const int64_t fv = (fracCoef != nullptr && size_t(k) < fracCoef->len)
                        ? int64_t(fracCoef->data[k]) : 0;
                    curve->poly_coef[i][k] = (iv << coefLog2Denom) + fv;
                }
            }
        } else if (src.mapping_idc == AV_DOVI_MAPPING_MMR) {
            const DoviMMRCurve* mmr = src.mmr;
            if (mmr == nullptr) continue;
            for (int i = 0; i < pieces; ++i) {
                const int order = int(mmr->mmr_order_minus1.data[i]) + 1;
                // mmr_coef's second dimension is three (order - 1).
                if (order < 1 || order > 3) break;
                curve->mmr_order[i] = uint8_t(order);
                if (size_t(i) < mmr->mmr_constant.len)
                    curve->mmr_constant[i] =
                        ((size_t(i) < mmr->mmr_constant_int.len ? mmr->mmr_constant_int.data[i] : 0) * (int64_t(1) << coefLog2Denom))
                        + int64_t(mmr->mmr_constant.data[i]);
                // mmr_coef is a 3D buffer: per piece, an array of per-order weight
                // arrays (seven weights each).
                // Same split as the polynomial coefficients: mmr_coef holds the
                // fractional part, mmr_coef_int the integer part.
                const DoviI64Data2D* intSeg = size_t(i) < mmr->mmr_coef_int.len
                    ? mmr->mmr_coef_int.list[i] : nullptr;
                if (size_t(i) < mmr->mmr_coef.len) {
                    const DoviU64Data2D& seg = *mmr->mmr_coef.list[i];
                    for (int j = 0; j < order && size_t(j) < seg.len; ++j) {
                        const DoviU64Data& coef = *seg.list[j];
                        const DoviI64Data* intCoef = (intSeg != nullptr && size_t(j) < intSeg->len)
                            ? intSeg->list[j] : nullptr;
                        for (int k = 0; k < 7; ++k) {
                            const int64_t iv = (intCoef != nullptr && size_t(k) < intCoef->len)
                                ? int64_t(intCoef->data[k]) : 0;
                            const int64_t fv = (size_t(k) < coef.len) ? int64_t(coef.data[k]) : 0;
                            curve->mmr_coef[i][j][k] = (iv << coefLog2Denom) + fv;
                        }
                    }
                }
            }
        }
    }
    dovi_rpu_free_data_mapping(mapping);

    // Colour metadata (P5 needs it; P7's reconstruction ignores it).
    const DoviVdrDmData* dm = dovi_rpu_get_vdr_dm_data(rpu);
    if (dm != nullptr) {
        auto* color = av_dovi_get_color(out);
        if (color != nullptr) {
            // libdovi exposes the matrix coefficients as flat int16 fields.
            const int16_t coefs[9] = {
                dm->ycc_to_rgb_coef0, dm->ycc_to_rgb_coef1, dm->ycc_to_rgb_coef2,
                dm->ycc_to_rgb_coef3, dm->ycc_to_rgb_coef4, dm->ycc_to_rgb_coef5,
                dm->ycc_to_rgb_coef6, dm->ycc_to_rgb_coef7, dm->ycc_to_rgb_coef8};
            for (int i = 0; i < 9; ++i)
                color->ycc_to_rgb_matrix[i] = AVRational{int(coefs[i]), 1 << 13};
            const int16_t lms[9] = {
                dm->rgb_to_lms_coef0, dm->rgb_to_lms_coef1, dm->rgb_to_lms_coef2,
                dm->rgb_to_lms_coef3, dm->rgb_to_lms_coef4, dm->rgb_to_lms_coef5,
                dm->rgb_to_lms_coef6, dm->rgb_to_lms_coef7, dm->rgb_to_lms_coef8};
            for (int i = 0; i < 9; ++i)
                color->rgb_to_lms_matrix[i] = AVRational{int(lms[i]), 1 << 14};
            const uint32_t offsets[3] = { dm->ycc_to_rgb_offset0, dm->ycc_to_rgb_offset1, dm->ycc_to_rgb_offset2 };
            for (int i = 0; i < 3; ++i)
                color->ycc_to_rgb_offset[i] = AVRational{int(offsets[i]), 1 << 28};
            color->signal_bit_depth = uint8_t(dm->signal_bit_depth);
            color->signal_color_space = uint8_t(dm->signal_color_space);
            color->signal_chroma_format = uint8_t(dm->signal_chroma_format);
            color->signal_full_range_flag = uint8_t(dm->signal_full_range_flag);
            color->source_min_pq = uint16_t(dm->source_min_pq);
            color->source_max_pq = uint16_t(dm->source_max_pq);
            color->source_diagonal = uint16_t(dm->source_diagonal);
        }
        dovi_rpu_free_vdr_dm_data(dm);
    }
    dovi_rpu_free(rpu);
    return true;
}

} // namespace veyra::source
