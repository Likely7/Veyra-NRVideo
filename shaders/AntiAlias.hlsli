// Spatial anti-aliasing (#53): constants and colour domain shared by every pass.
//
// Working textures are linear FP16. SDR: BT.709 with white = 1.0. HDR working:
// scRGB (1 = 80 cd/m2) with the 203-nit reference white; aaWorkingToWhite maps
// either one to "1.0 = white". Edges are found on a perceptual luma: the sRGB
// transfer below white, continued logarithmically above it with the same slope
// at 1.0, so HDR highlights still form edges without dominating the thresholds
// the algorithms were tuned with on gamma-encoded SDR. Every colour blend uses
// the linear values, which keeps HDR highlights and out-of-709 (negative)
// components exactly where no edge is found.
#ifndef VEYRA_ANTI_ALIAS_HLSLI
#define VEYRA_ANTI_ALIAS_HLSLI

cbuffer AaParams : register(b0) {
    uint aaWidth; uint aaHeight; float aaInvWidth; float aaInvHeight;
    float aaWorkingToWhite; float aaThreshold; float aaStrength; float aaHistoryWeight;
    float4 aaSubsampleIndices;
    float aaHistoryRejection; float aaHistoryValid; float aaReserved0; float aaReserved1;
};
SamplerState aaPointSampler : register(s0);  // point, clamp
SamplerState aaLinearSampler : register(s1); // bilinear, clamp

float AaPerceptual(float relative) {
    const float y = max(relative, 0.0);
    if (y > 1.0) return 1.0 + 0.30469 * log2(y); // 0.30469 = ln(2) * 1.055 / 2.4
    return y <= 0.0031308 ? 12.92 * y : 1.055 * pow(y, 1.0 / 2.4) - 0.055;
}
float AaLumaOf(float3 linearRgb) {
    return AaPerceptual(dot(linearRgb, float3(0.2126, 0.7152, 0.0722)) * aaWorkingToWhite);
}
float3 AaPerceptual3(float3 linearRgb) {
    const float3 c = linearRgb * aaWorkingToWhite;
    return float3(AaPerceptual(c.r), AaPerceptual(c.g), AaPerceptual(c.b));
}
int2 AaClamp(int2 p) { return clamp(p, int2(0, 0), int2(aaWidth, aaHeight) - 1); }
float2 AaTexcoord(uint2 p) { return (float2(p) + 0.5) * float2(aaInvWidth, aaInvHeight); }
#endif
