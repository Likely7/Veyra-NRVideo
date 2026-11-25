// SmartSrProbeResidual: smart SR render detection, step 2 of 2. Re-upscales one
// candidate's reduced rows with its scaler (pixel-centre bilinear, or the aligned
// 2x filter: pipeline/SmartSr.h SmartSrScaler) and sums, per probed row, the
// squared residual against the source luma plus the luma's first two moments
// (the row energy). One group per probed row.
// The probes read the linear source; the console scaled coded values.
#include "SmartSrCoding.hlsli"

Texture2D<float4> sourceTex : register(t0);      // linear SR stage input
Texture2D<float> reducedTex : register(t1);      // SmartSrProbeReduce output
RWStructuredBuffer<float4> stats : register(u0); // [candidate * rows + row]: residual, sum, sum of squares

cbuffer SmartSrProbeResidualConstants : register(b0)
{
    uint sourceWidth;
    uint rows;
    uint reducedWidth;
    uint rowBase;
    uint rowSpan;
    uint candidate;
    uint domain;        // SmartSrCoding.hlsli: 0 sRGB (SDR), 1 PQ (HDR10)
    uint scaler;        // 0 pixel-centre bilinear, 1 aligned 2x
    float4 evenTaps;    // aligned 2x: weights 0, 1, 2, 3 render samples away
    float4 oddTaps;     // aligned 2x: weights 0.5, 1.5, 2.5, 3.5 render samples away
};

groupshared float3 partial[256];

// Render sample k of probe row r, edge samples repeated.
float Render(int k, uint r, int last)
{
    return reducedTex[uint2(clamp(k, 0, last), r)];
}

[numthreads(256, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    const uint r = group.x;
    const uint y = rowBase + (r * rowSpan) / rows;  // pipeline/SmartSr.h smartSrProbeRow
    const float scale = float(reducedWidth) / float(sourceWidth);
    const int last = int(reducedWidth) - 1;
    float3 acc = 0.0;
    for (uint j = local.x; j < sourceWidth; j += 256) {
        float back;
        if (scaler == 0) {
            const float x = (float(j) + 0.5) * scale - 0.5;
            const float f = floor(x);
            const float w1 = x - f;
            back = (1.0 - w1) * Render(int(f), r, last) + w1 * Render(int(f) + 1, r, last);
        } else {
            const int k = int(j >> 1);
            if ((j & 1) == 0) {
                back = evenTaps.x * Render(k, r, last) +
                       evenTaps.y * (Render(k - 1, r, last) + Render(k + 1, r, last)) +
                       evenTaps.z * (Render(k - 2, r, last) + Render(k + 2, r, last)) +
                       evenTaps.w * (Render(k - 3, r, last) + Render(k + 3, r, last));
            } else {
                back = oddTaps.x * (Render(k, r, last) + Render(k + 1, r, last)) +
                       oddTaps.y * (Render(k - 1, r, last) + Render(k + 2, r, last)) +
                       oddTaps.z * (Render(k - 2, r, last) + Render(k + 3, r, last)) +
                       oddTaps.w * (Render(k - 3, r, last) + Render(k + 4, r, last));
            }
        }
        const float v = SmartSrLuma(sourceTex[uint2(j, y)], domain);
        const float d = v - back;
        acc += float3(d * d, v, v * v);
    }
    partial[local.x] = acc;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint s = 128; s > 0; s >>= 1) {
        if (local.x < s) {
            partial[local.x] += partial[local.x + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (local.x == 0) {
        stats[candidate * rows + r] = float4(partial[0], 0.0);
    }
}
