#pragma once
// Dolby Vision RPU bitstream decoder (profile 7 support).
//
// Custom addition. Some profile-7 encodes - notably AVS test material - carry
// the enhancement layer and the RPU in the bitstream but never write
// dolby_vision_configuration into the base-layer SPS. FFmpeg only parses the
// RPU SEI when that record is present, so such a stream has no
// AV_FRAME_DATA_DOVI_METADATA and the base layer can only be watched as HDR10,
// even though the EL decodes fine.
//
// This decodes the RPU payload itself, using the same field order and semantics
// as FFmpeg's ff_dovi_rpu_parse (LGPL reference: libavcodec/dovi_rpudec.c),
// and fills an AVDOVIMetadata so the existing reconstruction path can consume it
// unchanged.
//
// Scope: header + reshaping mapping + colour metadata. Extension blocks (L1/L2/
// L5 ...) are not parsed: the reconstruction only reads header/mapping/colour,
// and leaving num_ext_blocks at 0 keeps every consumer on the old code path.

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/dovi_meta.h>
}

namespace veyra::source {

// Set to 1 to print which validation rejected an RPU (kept because the format
// has many optional branches and a rejection is otherwise unexplained).
#ifndef DV_RPU_DEBUG
#define DV_RPU_DEBUG 0
#endif
#if DV_RPU_DEBUG
#define RPU_FAIL(line) (fprintf(stderr, "rpu fail @%d\n", line), false)
#else
#define RPU_FAIL(line) false
#endif

// MSB-first bit reader with the two Exp-Golomb codes the RPU uses.
class RpuBitReader {
public:
    RpuBitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ok() const { return !overflow_; }
    size_t consumedBits() const { return pos_; }
    size_t bitsLeft() const { return size_ * 8 > pos_ ? size_ * 8 - pos_ : 0; }

    uint32_t bits(int n) {
        uint32_t value = 0;
        for (int i = 0; i < n; ++i) {
            if (pos_ >= size_ * 8) { overflow_ = true; return value; }
            const uint8_t byte = data_[pos_ >> 3];
            const int shift = 7 - int(pos_ & 7);
            value = (value << 1) | ((byte >> shift) & 1u);
            ++pos_;
        }
        return value;
    }

    void skip(int n) {
        if (n <= 0) return;
        if (pos_ + size_t(n) > size_ * 8) { overflow_ = true; pos_ = size_ * 8; return; }
        pos_ += size_t(n);
    }

    // Unsigned Exp-Golomb (ue(v)); returns 0 and marks overflow when malformed.
    uint64_t ueGolomb() {
        int leadingZeros = 0;
        while (bits(1) == 0) {
            if (overflow_ || leadingZeros > 32) { overflow_ = true; return 0; }
            ++leadingZeros;
        }
        if (overflow_) return 0;
        const uint64_t suffix = leadingZeros ? bits(leadingZeros) : 0;
        return ((uint64_t(1) << leadingZeros) - 1) + suffix;
    }

    // Signed Exp-Golomb (se(v)).
    int64_t seGolomb() {
        const uint64_t u = ueGolomb();
        if (overflow_) return 0;
        const int64_t magnitude = int64_t((u + 1) >> 1);
        return (u & 1) ? magnitude : -magnitude;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool overflow_ = false;
};

// Coefficient coding shared by polynomial, MMR and NLQ: an integer part in the
// matching Golomb code followed by coef_log2_denom fractional bits.
inline uint64_t readUnsignedCoef(RpuBitReader& gb, int coefDataType, int coefLog2Denom) {
    if (coefDataType == 0) { // fixed point
        const uint64_t integer = gb.ueGolomb();
        const uint32_t fraction = gb.bits(coefLog2Denom);
        return (integer << coefLog2Denom) | fraction;
    }
    // float coefficients are not emitted by any real stream; treat as unsupported.
    gb.skip(32);
    return 0;
}

inline int64_t readSignedCoef(RpuBitReader& gb, int coefDataType, int coefLog2Denom) {
    if (coefDataType == 0) {
        const int64_t integer = gb.seGolomb();
        const uint32_t fraction = gb.bits(coefLog2Denom);
#if DV_RPU_DEBUG
        fprintf(stderr, "    coef sg=%lld frac=0x%x pos=%zu\n", (long long)integer, fraction, gb.consumedBits());
#endif
        return int64_t((uint64_t(integer) << coefLog2Denom) | fraction);
    }
    gb.skip(32);
    return 0;
}

// Decodes one RPU payload into `out` (allocated with av_dovi_alloc by the caller).
// Returns false when the payload is not a type-2 RPU or is malformed; the caller
// then keeps its previous behaviour (no reconstruction).
inline bool decodeDolbyVisionRpu(const uint8_t* data, size_t size, AVDOVIMetadata* out) {
    if (data == nullptr || out == nullptr || size < 5) return RPU_FAIL(__LINE__);

    // HEVC RPU NAL units carry a one-byte 0x19 prefix; AV1 uses an EMDF carrier,
    // which this path does not handle (profile 7 is HEVC-only).
    if (data[0] == 25) { ++data; --size; }
    if (size < 4) return RPU_FAIL(__LINE__);

    RpuBitReader gb(data, size);
    auto* header = av_dovi_get_header(out);
    auto* mapping = av_dovi_get_mapping(out);
    auto* color = av_dovi_get_color(out);
    if (header == nullptr || mapping == nullptr || color == nullptr) return RPU_FAIL(__LINE__);

    const uint32_t rpuType = gb.bits(6);
    if (rpuType != 2) return RPU_FAIL(__LINE__);
    header->rpu_type = uint8_t(rpuType);
    header->rpu_format = uint16_t(gb.bits(11));

    header->vdr_rpu_profile = uint8_t(gb.bits(4));
    header->vdr_rpu_level = uint8_t(gb.bits(4));

    const uint32_t vdrSeqInfoPresent = gb.bits(1);
    if (!vdrSeqInfoPresent) return RPU_FAIL(__LINE__); // no samples with this shape

    header->chroma_resampling_explicit_filter_flag = uint8_t(gb.bits(1));
    header->coef_data_type = uint8_t(gb.bits(2));
    if (header->coef_data_type != 0) return RPU_FAIL(__LINE__); // only fixed point is real-world
    header->coef_log2_denom = uint8_t(gb.ueGolomb());
    if (!gb.ok() || header->coef_log2_denom < 13 || header->coef_log2_denom > 32) return RPU_FAIL(__LINE__);

    header->vdr_rpu_normalized_idc = uint8_t(gb.bits(2));
    header->bl_video_full_range_flag = uint8_t(gb.bits(1));

    if ((header->rpu_format & 0x700) != 0) return RPU_FAIL(__LINE__); // unsupported RPU format

    const int blBitDepthMinus8 = int(gb.ueGolomb());
    const uint64_t elBitDepthRaw = gb.ueGolomb();
    const int vdrBitDepthMinus8 = int(gb.ueGolomb());
    const int extMappingIdc = int(elBitDepthRaw >> 8);
    const int elBitDepthMinus8 = int(elBitDepthRaw & 0xFF);
    if (!gb.ok() || blBitDepthMinus8 > 8 || elBitDepthMinus8 > 8 || vdrBitDepthMinus8 > 8) return RPU_FAIL(__LINE__);
    header->bl_bit_depth = uint8_t(blBitDepthMinus8 + 8);
    header->el_bit_depth = uint8_t(elBitDepthMinus8 + 8);
    header->ext_mapping_idc_0_4 = uint8_t(extMappingIdc & 0x1F);
    header->ext_mapping_idc_5_7 = uint8_t(extMappingIdc >> 5);
    header->vdr_bit_depth = uint8_t(vdrBitDepthMinus8 + 8);
    header->spatial_resampling_filter_flag = uint8_t(gb.bits(1));
    const uint32_t dmCompression = gb.bits(3); // no device emits compressed DM here
    header->el_spatial_resampling_filter_flag = uint8_t(gb.bits(1));
    header->disable_residual_flag = uint8_t(gb.bits(1));
    if (!gb.ok()) return RPU_FAIL(__LINE__);

    const uint32_t vdrDmMetadataPresent = gb.bits(1);
    const uint32_t usePrevVdrRpu = gb.bits(1);
    if (!gb.ok()) return RPU_FAIL(__LINE__);
    if (usePrevVdrRpu) return RPU_FAIL(__LINE__); // needs a previous RPU we do not retain
    if (dmCompression != 0) return RPU_FAIL(__LINE__);

    // ---- vdr_rpu_data_payload ------------------------------------------------
    const uint32_t vdrRpuId = uint32_t(gb.ueGolomb());
    if (!gb.ok() || vdrRpuId > 63) return RPU_FAIL(__LINE__);
    mapping->vdr_rpu_id = uint8_t(vdrRpuId);
    mapping->mapping_color_space = uint8_t(gb.ueGolomb());
    mapping->mapping_chroma_format_idc = uint8_t(gb.ueGolomb());

    for (int c = 0; c < 3; ++c) {
        AVDOVIReshapingCurve* curve = &mapping->curves[c];
        const int numPivotsMinus2 = int(gb.ueGolomb());
        if (!gb.ok() || numPivotsMinus2 < 0 || numPivotsMinus2 > AV_DOVI_MAX_PIECES - 1) return RPU_FAIL(__LINE__);
        curve->num_pivots = uint8_t(numPivotsMinus2 + 2);
        int pivot = 0;
        for (int i = 0; i < curve->num_pivots; ++i) {
            pivot += int(gb.bits(header->bl_bit_depth));
            curve->pivots[i] = uint16_t(pivot);
        }
    }

    const bool useNlq = !header->disable_residual_flag;
    if (useNlq) {
        int nlqPivot = 0;
        mapping->nlq_method_idc = AVDOVINLQMethod(gb.bits(3));
        for (int i = 0; i < 2; ++i) {
            nlqPivot += int(gb.bits(header->bl_bit_depth));
            mapping->nlq_pivots[i] = uint16_t(nlqPivot);
        }
        if (mapping->nlq_method_idc != AV_DOVI_NLQ_LINEAR_DZ) {
            if (mapping->nlq_method_idc != AV_DOVI_NLQ_NONE) return RPU_FAIL(__LINE__);
        }
    } else {
        mapping->nlq_method_idc = AV_DOVI_NLQ_NONE;
    }

    mapping->num_x_partitions = uint32_t(gb.ueGolomb() + 1);
    mapping->num_y_partitions = uint32_t(gb.ueGolomb() + 1);
    if (!gb.ok()) return RPU_FAIL(__LINE__);

    for (int c = 0; c < 3; ++c) {
        AVDOVIReshapingCurve* curve = &mapping->curves[c];
        for (int i = 0; i < curve->num_pivots - 1; ++i) {
            const int mappingIdc = int(gb.ueGolomb());
#if DV_RPU_DEBUG
            fprintf(stderr, "c=%d i=%d idc=%d pos=%zu left=%zu xpart=%u ypart=%u\n", c, i, mappingIdc, gb.consumedBits(), gb.bitsLeft(), mapping->num_x_partitions, mapping->num_y_partitions);
#endif
            if (!gb.ok() || mappingIdc < 0 || mappingIdc > 1) return RPU_FAIL(__LINE__);
            curve->mapping_idc[i] = AVDOVIMappingMethod(mappingIdc);
            if (mappingIdc == AV_DOVI_MAPPING_POLYNOMIAL) {
                const int polyOrderMinus1 = int(gb.ueGolomb());
                if (!gb.ok() || polyOrderMinus1 < 0 || polyOrderMinus1 > 1) return RPU_FAIL(__LINE__);
                curve->poly_order[i] = uint8_t(polyOrderMinus1 + 1);
#if DV_RPU_DEBUG
                fprintf(stderr, "  poly: c=%d i=%d order_minus1=%d pos=%zu\n", c, i, polyOrderMinus1, gb.consumedBits());
#endif
                if (polyOrderMinus1 == 0) {
                    const uint32_t lin = gb.bits(1);
#if DV_RPU_DEBUG
                    fprintf(stderr, "  lin_interp=%u pos=%zu\n", lin, gb.consumedBits());
#endif
                    if (lin != 0) return RPU_FAIL(__LINE__); // linear interpolation: undocumented
                }
                for (int k = 0; k <= curve->poly_order[i]; ++k) {
                    curve->poly_coef[i][k] = readSignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
                }
            } else {
                const int mmrOrderMinus1 = int(gb.bits(2));
                if (mmrOrderMinus1 > 2) return RPU_FAIL(__LINE__);
                curve->mmr_order[i] = uint8_t(mmrOrderMinus1 + 1);
                curve->mmr_constant[i] = readSignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
                for (int j = 0; j < curve->mmr_order[i]; ++j) {
                    for (int k = 0; k < 7; ++k) {
                        curve->mmr_coef[i][j][k] = readSignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
                    }
                }
            }
        }
    }

    if (useNlq) {
        for (int c = 0; c < 3; ++c) {
            AVDOVINLQParams* nlq = &mapping->nlq[c];
            nlq->nlq_offset = uint16_t(gb.bits(header->el_bit_depth));
            nlq->vdr_in_max = readUnsignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
            if (mapping->nlq_method_idc == AV_DOVI_NLQ_LINEAR_DZ) {
                nlq->linear_deadzone_slope = readUnsignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
                nlq->linear_deadzone_threshold = readUnsignedCoef(gb, header->coef_data_type, header->coef_log2_denom);
            }
        }
    }
    if (!gb.ok()) return RPU_FAIL(__LINE__);

    // ---- vdr_dm_data (colour metadata) --------------------------------------
    if (vdrDmMetadataPresent) {
        const uint32_t affectedDmId = uint32_t(gb.ueGolomb());
        const uint32_t currentDmId = uint32_t(gb.ueGolomb());
        if (!gb.ok() || affectedDmId != currentDmId) return RPU_FAIL(__LINE__);
        color->dm_metadata_id = uint8_t(affectedDmId);
        color->scene_refresh_flag = uint8_t(gb.ueGolomb());
        for (int i = 0; i < 9; ++i) {
            const int v = int(int16_t(gb.bits(16)));
            color->ycc_to_rgb_matrix[i] = AVRational{v, 1 << 13};
        }
        for (int i = 0; i < 3; ++i) {
            const uint32_t offset = gb.bits(32);
            color->ycc_to_rgb_offset[i] = AVRational{int(offset), 1 << 30};
        }
        for (int i = 0; i < 9; ++i) {
            const int v = int(int16_t(gb.bits(16)));
            color->rgb_to_lms_matrix[i] = AVRational{v, 1 << 14};
        }
        color->signal_eotf = uint16_t(gb.bits(16));
        color->signal_eotf_param0 = uint16_t(gb.bits(16));
        color->signal_eotf_param1 = uint16_t(gb.bits(16));
        color->signal_eotf_param2 = gb.bits(32);
        color->signal_bit_depth = uint8_t(gb.bits(5));
        color->signal_color_space = uint8_t(gb.bits(2));
        color->signal_chroma_format = uint8_t(gb.bits(2));
        color->signal_full_range_flag = uint8_t(gb.bits(2));
        color->source_min_pq = uint16_t(gb.bits(12));
        color->source_max_pq = uint16_t(gb.bits(12));
        color->source_diagonal = uint16_t(gb.bits(10));
    }
    if (!gb.ok()) return RPU_FAIL(__LINE__);

    // Extension blocks and the trailing CRC32 are intentionally not parsed: no
    // consumer of this metadata reads them, and skipping keeps this decoder small.
    return true;
}

} // namespace veyra::source
