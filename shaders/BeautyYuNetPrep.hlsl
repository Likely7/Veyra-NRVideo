// Face detector input: area-averaged, sRGB-coded, BGR 0..255 copy of the frame
// (what the model was trained on: an 8-bit display-referred picture). The model
// input is padded with zeros to a multiple of 32 on the right and bottom.
//
// Source: the linear working texture of the graph (RGBA16F). SDR frames are
// encoded with the sRGB curve; HDR (scRGB, 1 = 80 nits) frames first go through
// the fixed 203-nit proxy used by the other HDR-aware effects, so highlights
// compress instead of clipping and negative wide-gamut components clamp at zero
// in this analysis copy only (the picture itself is never touched here).
#include "BeautyYuNet.hlsli"
#include "HdrColor.hlsli"

Texture2D<float4> Source : register(t1);
RWStructuredBuffer<float4> Out : register(u2);

float3 srgbEncode(float3 linearValue) {
    const float3 c = saturate(linearValue);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

// outW/outH = padded size; arg0/arg1 = source size; arg2/arg3 = content size
// inside the padded grid; arg4 = HDR flag; arg5 = taps per axis (1..8);
// arg6 = region origin x | y << 16 in source pixels (whole frame: 0).
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH;
    if (id.x >= total) return;
    const uint x = id.x % outW, y = id.x / outW;
    if (x >= arg2 || y >= arg3) { Out[id.x] = float4(0, 0, 0, 0); return; }
    const uint2 origin = uint2(arg6 & 0xffffu, arg6 >> 16);
    const float2 scale = float2(arg0, arg1) / float2(arg2, arg3);
    const uint taps = max(arg5, 1u);
    float3 sum = 0;
    for (uint j = 0; j < taps; ++j) {
        for (uint i = 0; i < taps; ++i) {
            const float2 p = (float2(x, y) + (float2(i, j) + 0.5) / taps) * scale;
            const uint2 pixel = origin + min(uint2(p), uint2(arg0 - 1, arg1 - 1));
            float3 c = Source.Load(int3(pixel, 0)).rgb;
            c = arg4 ? HdrProxy(c) : c;
            sum += srgbEncode(c);
        }
    }
    const float3 rgb = sum / float(taps * taps) * 255.0;
    Out[id.x] = float4(rgb.b, rgb.g, rgb.r, 0.0);
}
