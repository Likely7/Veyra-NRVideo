// SmartSrReduce: one separable pass of the least-squares inverse of a
// pixel-centre bilinear upscale ("descale"). Smart SR uses it when the source
// enlarged its own render (a console's 1080p game sent as 4K): an area shrink
// blurs that render again, the inverse recovers it. The console scaled coded values, so the filter runs on coded
// samples (sRGB for SDR, PQ for HDR10: SmartSrCoding.hlsli): rows read the
// linear source and code each tap, columns decode what they write. One dispatch
// covers one tap phase: outputs phase, phase + q, ... along the filtered axis.
// Taps: pipeline/SmartSr.h smartSrInverseAxis.
#include "SmartSrCoding.hlsli"

Texture2D<float4> inputTex : register(t0);
RWTexture2D<float4> outputTex : register(u0);

cbuffer SmartSrConstants : register(b0)
{
    uint inputLength;   // source samples along the filtered axis
    uint otherLength;   // lines across the axis to write
    uint outputLength;  // render samples along the filtered axis
    uint vertical;      // 0: filter rows (x), 1: filter columns (y)
    uint phase;         // render index modulo phaseCount handled here
    uint phaseCount;    // q render samples ...
    uint period;        // ... per p source samples
    uint coding;        // bit 0: input is linear, code each tap; bit 1: decode the output to linear;
                        // bit 2: PQ (HDR10) instead of sRGB codes
    uint _pad0;
    float4 taps[7];     // 28 taps over source samples centre-14 .. centre+13
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Threads along x walk adjacent texels of the line for both orientations.
    const uint o = phase + (vertical != 0 ? id.y : id.x) * phaseCount;
    const uint k = vertical != 0 ? id.x : id.y;
    if (o >= outputLength || k >= otherLength) {
        return;
    }
    const int centre = int(((2 * o + 1) * period) / (2 * phaseCount));
    const int last = int(inputLength) - 1;
    const uint domain = (coding >> 2) & 1u;
    float4 sum = 0.0;
    [unroll]
    for (int t = 0; t < 28; ++t) {
        const int s = clamp(centre - 14 + t, 0, last);
        float4 value = vertical != 0 ? inputTex[uint2(k, s)] : inputTex[uint2(s, k)];
        if ((coding & 1u) != 0) {
            value.rgb = SmartSrEncode(value.rgb, domain);
        }
        sum += taps[t >> 2][t & 3] * value;
    }
    if ((coding & 2u) != 0) {
        sum = float4(SmartSrDecode(sum.rgb, domain), saturate(sum.a));
    }
    if (vertical != 0) {
        outputTex[uint2(k, o)] = sum;
    } else {
        outputTex[uint2(o, k)] = sum;
    }
}
