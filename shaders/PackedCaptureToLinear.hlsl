// N1: packed capture ingress. The driver's bytes are uploaded 1:1 into an
// R8G8B8A8_UNORM texture; this shader unpacks BGR24 / RGB555 / RGB565 /
// UYVY / YVYU with the same color contract as RgbToLinear / Yuy2ToLinear.
// packing: 1 BGR24, 2 RGB555, 3 RGB565, 4 UYVY, 5 YVYU.
#include "HdrColor.hlsli"
#include "ColorGrade.hlsli"
cbuffer Params : register(b0) {
    uint width; uint height; uint transfer; uint limitedU;
    float limited; float matrix709; float primaries2020; uint packing;
    float4 colorRow0; float4 colorRow1; float4 colorRow2; float4 colorControls; float4 colorFlags;
};
Texture2D<float4> packedBytes : register(t0);
RWTexture2D<float4> linearRgb : register(u0);
float decode(float v) {
    if (transfer == 3) return pow(max(v, 0), 2.4);
    if (transfer == 2) return v < 0.081 ? v / 4.5 : pow((v + 0.099) / 1.099, 1.0 / 0.45);
    if (transfer == 0) return v;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
uint byteAt(uint y, uint index) {
    const float4 t = packedBytes[uint2(index >> 2, y)];
    return uint(round(saturate(t[index & 3]) * 255.0));
}
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float3 rgb;
    if (packing == 4 || packing == 5) {
        const float4 pair = packedBytes[uint2(p.x / 2, p.y)];
        const bool second = (p.x & 1) != 0;
        float y, cb, cr;
        if (packing == 4) { y = second ? pair.w : pair.y; cb = pair.x; cr = pair.z; }
        else { y = second ? pair.z : pair.x; cb = pair.w; cr = pair.y; }
        float2 uv = float2(cb, cr) - 128.0 / 255.0;
        if (limited > 0.5) { y = (y - 16.0 / 255.0) * (255.0 / 219.0); uv *= 255.0 / 224.0; }
        rgb = matrix709 > 0.5 ? float3(y + 1.5748 * uv.y, y - 0.187324 * uv.x - 0.468124 * uv.y, y + 1.8556 * uv.x)
                              : float3(y + 1.402 * uv.y, y - 0.344136 * uv.x - 0.714136 * uv.y, y + 1.772 * uv.x);
        if (matrix709 > 1.5) rgb = float3(y + 1.4746 * uv.y, y - 0.164553 * uv.x - 0.571353 * uv.y, y + 1.8814 * uv.x);
        rgb = saturate(rgb);
    } else if (packing == 1) {
        const uint base = p.x * 3;
        rgb = float3(byteAt(p.y, base + 2), byteAt(p.y, base + 1), byteAt(p.y, base)) / 255.0;
    } else {
        const uint base = p.x * 2;
        const uint v = byteAt(p.y, base) | (byteAt(p.y, base + 1) << 8);
        if (packing == 2) rgb = float3(((v >> 10) & 31) / 31.0, ((v >> 5) & 31) / 31.0, (v & 31) / 31.0);
        else rgb = float3(((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0);
    }
    if (limitedU != 0 && packing <= 3) rgb = saturate((rgb - 16.0 / 255.0) * (255.0 / 219.0));
    float3 decoded = float3(decode(rgb.r), decode(rgb.g), decode(rgb.b));
    if (primaries2020 > 0.5) decoded = HdrTo709(decoded);
    if (colorFlags.x > 0.5) { ColorGradeParams grade = { colorRow0, colorRow1, colorRow2, colorControls, colorFlags }; decoded = ColorGradeApplyWithFlags(decoded, grade); }
    linearRgb[p.xy] = float4(decoded, 1);
}
