// SmartSrProbeReduce: smart SR render detection, step 1 of 2. Reduces the luma
// of the probed source rows to one candidate width with the inverse of its
// scaler (taps: pipeline/SmartSr.h).
// One dispatch per tap phase: outputs phase, phase + q, ... of every row.
// The probes read the linear source; the console scaled coded values.
#include "SmartSrCoding.hlsli"

Texture2D<float4> sourceTex : register(t0);   // linear SR stage input
RWTexture2D<float> reducedTex : register(u0); // probed rows x candidate width

cbuffer SmartSrProbeReduceConstants : register(b0)
{
    uint sourceWidth;
    uint rows;          // probed rows
    uint reducedWidth;  // candidate width
    uint rowBase;       // source row of probe 0
    uint phase;
    uint phaseCount;    // q candidate samples ...
    uint period;        // ... per p source samples
    uint rowSpan;       // probes cover rowBase .. rowBase + rowSpan
    uint domain;        // SmartSrCoding.hlsli: 0 sRGB (SDR), 1 PQ (HDR10)
    float4 taps[7];     // 28 taps over source samples centre-14 .. centre+13
};

[numthreads(64, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint o = phase + id.x * phaseCount;
    const uint r = id.y;
    if (o >= reducedWidth || r >= rows) {
        return;
    }
    const uint y = rowBase + (r * rowSpan) / rows;  // pipeline/SmartSr.h smartSrProbeRow
    const int centre = int(((2 * o + 1) * period) / (2 * phaseCount));
    const int last = int(sourceWidth) - 1;
    float sum = 0.0;
    [unroll]
    for (int t = 0; t < 28; ++t) {
        sum += taps[t >> 2][t & 3] * SmartSrLuma(sourceTex[uint2(clamp(centre - 14 + t, 0, last), y)], domain);
    }
    reducedTex[uint2(o, r)] = sum;
}
