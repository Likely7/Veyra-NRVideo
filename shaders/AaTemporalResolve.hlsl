// Experimental no-jitter temporal SMAA resolve (#53), after the "Clamped
// temporal resolve" pass of SMAA_T2x_NoJitter_Experimental.hlsl /
// SMAA_4x_NoJitter_Experimental.hlsl in SAOG0721/Magpie d687997 (GPL-3.0).
// Captured frames cannot be jittered, so this is an approximation of SMAA T2x /
// 4x: the official subsample-index sets alternate per frame (AaSmaaWeights) and
// the history is clamped to the current 3x3 neighbourhood. Veyra changes: the
// colours are linear (the rejection distance is measured on the perceptual
// transfer of AntiAlias.hlsli), history validity is a root constant set by the
// pass (reset on seek, scene cut and rebuild) instead of the history alpha, and
// the centre's alpha is kept.
#include "AntiAlias.hlsli"
Texture2D<float4> current : register(t0);
Texture2D<float4> history : register(t1);
RWTexture2D<float4> target : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    const float4 now = current.Load(int3(p, 0));
    float3 neighborhoodMin = now.rgb;
    float3 neighborhoodMax = now.rgb;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float3 sampleColor = current.Load(int3(AaClamp(p + int2(x, y)), 0)).rgb;
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }

    const float3 previous = clamp(history.Load(int3(p, 0)).rgb, neighborhoodMin, neighborhoodMax);
    const float3 difference = abs(AaPerceptual3(previous) - AaPerceptual3(now.rgb));
    const float maxDifference = max(difference.r, max(difference.g, difference.b));
    const float confidence = saturate(1.0 - maxDifference * aaHistoryRejection);
    const float weight = aaHistoryWeight * confidence * aaHistoryValid;

    target[id.xy] = float4(lerp(now.rgb, previous, weight), now.a);
}
