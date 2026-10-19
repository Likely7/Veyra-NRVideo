// SMAA pass 3 (#53): neighbourhood blending of the linear working colour.
#include "AntiAlias.hlsli"
#define SMAA_RT_METRICS float4(aaInvWidth, aaInvHeight, float(aaWidth), float(aaHeight))
#define SMAA_LINEAR_SAMPLER aaLinearSampler
#define SMAA_POINT_SAMPLER aaPointSampler
#include "Smaa.hlsli"
Texture2D<float4> colorTex : register(t0);
Texture2D<float4> blendTex : register(t1);
RWTexture2D<float4> target : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    target[id.xy] = SMAANeighborhoodBlending(id.xy, colorTex, blendTex);
}
