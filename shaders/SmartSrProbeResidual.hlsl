// SmartSrProbeResidual: smart SR render detection, step 2 of 2. Re-upscales one
// candidate's reduced rows with the pixel-centre bilinear scaler and sums, per
// probed row, the squared residual against the source luma plus the luma's
// first two moments (the row energy). One group per probed row.

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
    uint _pad0;
    uint _pad1;
};

groupshared float3 partial[256];

// The probes read the linear source; the console scaled coded values.
float Luma(float4 c)
{
    const float3 v = max(c.rgb, 0.0);
    const float3 coded = select(v <= 0.0031308, v * 12.92, 1.055 * pow(v, 1.0 / 2.4) - 0.055);
    return dot(coded, float3(0.2126, 0.7152, 0.0722));
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
        const float x = (float(j) + 0.5) * scale - 0.5;
        const float f = floor(x);
        const float w1 = x - f;
        const float back = (1.0 - w1) * reducedTex[uint2(clamp(int(f), 0, last), r)] +
                           w1 * reducedTex[uint2(clamp(int(f) + 1, 0, last), r)];
        const float v = Luma(sourceTex[uint2(j, y)]);
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
