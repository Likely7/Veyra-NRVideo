// SMAA pass 2 (#53): blending weight calculation. The preset (SMAA_PRESET_LOW /
// MEDIUM / HIGH / ULTRA) is set by the build; subsample indices are root
// constants (zero for SMAA 1x, the official T2x / 4x sets for the experimental
// no-jitter temporal modes).
#include "AntiAlias.hlsli"
#define SMAA_RT_METRICS float4(aaInvWidth, aaInvHeight, float(aaWidth), float(aaHeight))
#define SMAA_LINEAR_SAMPLER aaLinearSampler
#define SMAA_POINT_SAMPLER aaPointSampler
#include "Smaa.hlsli"
Texture2D<float2> edgesTex : register(t0);
Texture2D<float4> areaTex : register(t1);   // R8G8 lookup table, 160x560
Texture2D<float> searchTex : register(t2);  // R8 lookup table, 64x16
RWTexture2D<float4> weights : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    weights[id.xy] = SMAABlendingWeightCalculationPS(AaTexcoord(id.xy), edgesTex, areaTex, searchTex, aaSubsampleIndices);
}
