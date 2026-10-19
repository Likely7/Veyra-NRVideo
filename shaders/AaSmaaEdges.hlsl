// SMAA pass 1 (#53): luma edge detection on the perceptual luma texture.
// SMAALumaEdgeDetectionPS of iryoku/smaa via SAOG0721/Magpie d687997 (MIT notice
// in Smaa.hlsli); point samples with clamp addressing are clamped Loads here and
// the threshold of the selected preset is a root constant.
#include "AntiAlias.hlsli"
#ifndef SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#endif
Texture2D<float> luma : register(t0);
RWTexture2D<float2> edges : register(u0);

float LumaAt(int2 p) { return luma.Load(int3(AaClamp(p), 0)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    const float2 threshold = float2(aaThreshold, aaThreshold);

    // Calculate lumas:
    const float L = LumaAt(p);
    const float Lleft = LumaAt(p + int2(-1, 0));
    const float Ltop = LumaAt(p + int2(0, -1));

    // We do the usual threshold:
    float4 delta;
    delta.xy = abs(L - float2(Lleft, Ltop));
    float2 e = step(threshold, delta.xy);

    // Then stop if there is no edge:
    if (dot(e, float2(1.0, 1.0)) == 0.0) {
        edges[id.xy] = float2(0.0, 0.0);
        return;
    }

    // Calculate right and bottom deltas:
    const float Lright = LumaAt(p + int2(1, 0));
    const float Lbottom = LumaAt(p + int2(0, 1));
    delta.zw = abs(L - float2(Lright, Lbottom));

    // Calculate the maximum delta in the direct neighborhood:
    float2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    const float Lleftleft = LumaAt(p + int2(-2, 0));
    const float Ltoptop = LumaAt(p + int2(0, -2));
    delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    const float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    e.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    edges[id.xy] = e;
}
