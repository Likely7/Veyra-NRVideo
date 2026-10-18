#pragma once
// Dolby Vision Profile 7 双层重建（BL + EL），按 ETSI GS CCM 001 V1.1.1 5.4 实现。
//
// 这是一个**参考实现**：纯函数、可单测，和 PresentBlit 里的 GPU 版逐句对应。
// 之所以先写 CPU 版：规范只给伪代码，任何一处移位/位深错一位，画面就会偏，
// 而 GPU 上很难逐步对账。这里先用真实 RPU 参数把每个分支跑对，GPU 版照着搬。
//
// 参数来源：FFmpeg 的 AVDOVIRpuDataHeader / AVDOVIDataMapping（av_dovi_get_header /
// av_dovi_get_mapping）。本样本（Dolby FEL Test）实测：luma 逆映射恒等
// （polyOrder 1、斜率 1.0）、coef_log2_denom=23、bit depth 10/10/12、NLQ LINEAR_DZ。
//
// Custom addition for the P5 work; 播放路径在 GPU 版完成前不调用它。
#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dovi_meta.h>
}

namespace veyra::engine {

// 反量化后的 EL 样本：规范要求 17 位有符号。
inline int64_t doviNlq(const AVDOVIDataMapping& mapping, int cmp, int elBitDepth,
                       int coefLog2Denom, int64_t e) {
    const auto& n = mapping.nlq[cmp];
    int64_t rr = e - int64_t(n.nlq_offset);
    if (rr == 0) return 0;
    const int64_t sign = rr < 0 ? -1 : 1;
    rr <<= 1;
    rr -= sign;
    rr <<= (10 - elBitDepth);
    int64_t dq = rr * int64_t(n.linear_deadzone_slope);
    dq += (int64_t(n.linear_deadzone_threshold) << (10 - elBitDepth + 1)) * sign;
    const int64_t limit = int64_t(n.vdr_in_max) << (10 - elBitDepth + 1);
    dq = std::clamp(dq, -limit, limit);
    return dq >> (coefLog2Denom - 5 - elBitDepth);
}

// 分段函数落在哪一段：pivot[i] <= s < pivot[i+1]。
inline int doviPivotIndex(const AVDOVIReshapingCurve& curve, int64_t s) {
    const int pieces = curve.num_pivots > 0 ? int(curve.num_pivots) - 1 : 0;
    for (int i = 0; i < pieces; ++i)
        if (s >= int64_t(curve.pivots[i]) && s < int64_t(curve.pivots[i + 1])) return i;
    return s < int64_t(curve.pivots[0]) ? 0 : std::max(0, pieces - 1);
}

// 5.4.2.3.2 多项式映射（luma）。
inline uint16_t doviPolyMapping(const AVDOVIReshapingCurve& curve, int piece, int64_t s,
                                int blBitDepth, int coefLog2Denom) {
    // 规范伪代码：for (i = 0; i <= poly_order_minus1 + 1; i++)
    const int terms = int(curve.poly_order[piece]) + 2;
    int64_t ss = 1;
    int shift = 20;                                  // 2 * maximum BL bit depth
    int64_t vv = 0;
    for (int i = 0; i < terms && i < 3; ++i) {
        vv += curve.poly_coef[piece][i] * (ss << shift);
        ss *= s;
        shift -= blBitDepth;
    }
    vv = std::max<int64_t>(vv, 0);
    int64_t v = vv >> (4 + coefLog2Denom);
    return uint16_t(std::min<int64_t>(v, 0xffff));
}

// 5.4.2.3.3 MMR 映射（chroma）。s0 必须是**下采样后**的 BL luma。
inline uint16_t doviMmrMapping(const AVDOVIReshapingCurve& curve, int piece,
                               int64_t s0, int64_t s1, int64_t s2,
                               int blBitDepth, int coefLog2Denom) {
    int64_t tt[22] = {};
    const int order = int(curve.mmr_order[piece]);
    tt[0] = int64_t(1) << 20;
    if (order >= 1) {
        tt[1] = s0 << (20 - blBitDepth);
        tt[2] = s1 << (20 - blBitDepth);
        tt[3] = s2 << (20 - blBitDepth);
        tt[4] = (s0 * s1) << (20 - 2 * blBitDepth);
        tt[5] = (s0 * s2) << (20 - 2 * blBitDepth);
        tt[6] = (s1 * s2) << (20 - 2 * blBitDepth);
        tt[7] = (tt[4] * tt[3]) >> 20;
    }
    if (order >= 2) {
        tt[8] = (s0 * s0) << (20 - 2 * blBitDepth);
        tt[9] = (s1 * s1) << (20 - 2 * blBitDepth);
        tt[10] = (s2 * s2) << (20 - 2 * blBitDepth);
        tt[11] = (tt[4] * tt[4]) >> 20;
        tt[12] = (tt[5] * tt[5]) >> 20;
        tt[13] = (tt[6] * tt[6]) >> 20;
        tt[14] = (tt[7] * tt[7]) >> 20;
    }
    if (order >= 3) {
        tt[15] = (tt[1] * tt[8]) >> 20;
        tt[16] = (tt[2] * tt[9]) >> 20;
        tt[17] = (tt[3] * tt[10]) >> 20;
        tt[18] = (tt[4] * tt[11]) >> 20;
        tt[19] = (tt[5] * tt[12]) >> 20;
        tt[20] = (tt[6] * tt[13]) >> 20;
        tt[21] = (tt[7] * tt[14]) >> 20;
    }
    int64_t rr = curve.mmr_constant[piece] * tt[0];
    int cnt = 1;
    for (int i = 1; i <= order; ++i)
        for (int j = 0; j < 7; ++j) {
            rr += curve.mmr_coef[piece][i - 1][j] * tt[cnt];
            ++cnt;
        }
    rr = std::max<int64_t>(rr, 0);
    int64_t v = rr >> (4 + coefLog2Denom);
    return uint16_t(std::min<int64_t>(v, 0xffff));
}

// 5.4.2：BL 逆映射，返回 16 位域的 v。
inline uint16_t doviInverseMap(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                               int cmp, int64_t s0, int64_t s1, int64_t s2) {
    const auto& curve = mapping.curves[cmp];
    const int piece = doviPivotIndex(curve, s0);
    // 分段边界的 clip（规范在两种映射前都会做）。num_pivots 为 0 的曲线只可能来自
    // 解析失败的 RPU，此时不做 clip，避免读 pivots[-1]。
    auto clampToCurve = [](int64_t v, const AVDOVIReshapingCurve& c) {
        if (c.num_pivots == 0) return v;
        return std::clamp<int64_t>(v, c.pivots[0], c.pivots[c.num_pivots - 1]);
    };
    const int64_t c0 = clampToCurve(s0, curve);
    const int64_t c1 = clampToCurve(s1, mapping.curves[1]);
    const int64_t c2 = clampToCurve(s2, mapping.curves[2]);
    if (curve.mapping_idc[piece] == AV_DOVI_MAPPING_POLYNOMIAL)
        return doviPolyMapping(curve, piece, c0, header.bl_bit_depth, header.coef_log2_denom);
    return doviMmrMapping(curve, piece, c0, c1, c2, header.bl_bit_depth, header.coef_log2_denom);
}

// 5.4.3.3：重建。BL 是 ST 2084 时 5.5 整段省略（规范原文），所以直接输出 12 位。
inline uint16_t doviReconstruct(uint16_t v, int64_t residual, bool residualEnabled, int outBitDepth) {
    const int64_t maxOut = (int64_t(1) << outBitDepth) - 1;
    int64_t h = int64_t(v);
    if (residualEnabled) h += residual;
    h += int64_t(1) << (15 - outBitDepth);
    h >>= (16 - outBitDepth);
    return uint16_t(std::clamp<int64_t>(h, 0, maxOut));
}

// 一次算完一个样本（luma）。s0 = BL luma，el = EL luma。
inline uint16_t doviComposeLumaSample(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                                      int64_t blLuma, int64_t elLuma) {
    const uint16_t v = doviInverseMap(mapping, header, 0, blLuma, blLuma, blLuma);
    const int64_t r = doviNlq(mapping, 0, header.el_bit_depth, header.coef_log2_denom, elLuma);
    return doviReconstruct(v, r, header.disable_residual_flag == 0, header.vdr_bit_depth);
}

// 一次算完一个样本（chroma）。三个分量都要传，因为 MMR 用 luma 与另一路的 chroma。
inline uint16_t doviComposeChromaSample(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                                        int cmp, int64_t blLumaDown, int64_t blCb, int64_t blCr,
                                        int64_t elChroma) {
    const uint16_t v = doviInverseMap(mapping, header, cmp, blLumaDown, blCb, blCr);
    const int64_t r = doviNlq(mapping, cmp, header.el_bit_depth, header.coef_log2_denom, elChroma);
    return doviReconstruct(v, r, header.disable_residual_flag == 0, header.vdr_bit_depth);
}

} // namespace veyra::engine
