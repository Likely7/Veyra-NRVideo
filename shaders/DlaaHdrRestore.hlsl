// DLAA in HDR (scRGB, linear BT.709, 1 = 80 nits): DLSS keeps colours inside
// BT.709 only, so a colour outside it (a negative component) comes back clipped
// to the gamut edge. The negative part of the input is added back to the DLSS
// result: inside BT.709 (no negative component) positive output is DLSS's own, and a
// flat out-of-709 area returns to its input value instead of a brighter,
// desaturated one. FSR HDR uses the same pass with bilinear negative-component
// reconstruction at the output size. This also avoids adding a negative twice
// if a runtime begins returning signed colour rather than clipping it.
Texture2D<float4> inputTex : register(t0);   // the DLAA instance's input
Texture2D<float4> dlaaTex : register(t1);    // the DLSS output for it
RWTexture2D<float4> outputTex : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    outputTex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    uint iw, ih;
    inputTex.GetDimensions(iw, ih);
    const float2 at = (float2(id.xy) + 0.5) * float2(iw, ih) / float2(w, h) - 0.5;
    const int2 p = int2(floor(at));
    const float2 f = frac(at);
    const int2 hi = int2(iw, ih) - 1;
    const float3 a = min(inputTex[clamp(p, int2(0, 0), hi)].rgb, 0.0);
    const float3 b = min(inputTex[clamp(p + int2(1, 0), int2(0, 0), hi)].rgb, 0.0);
    const float3 c = min(inputTex[clamp(p + int2(0, 1), int2(0, 0), hi)].rgb, 0.0);
    const float3 e = min(inputTex[clamp(p + int2(1, 1), int2(0, 0), hi)].rgb, 0.0);
    const float3 negative = lerp(lerp(a, b, f.x), lerp(c, e, f.x), f.y);
    const float4 d = dlaaTex[id.xy];
    outputTex[id.xy] = float4(max(d.rgb, 0.0) + negative, d.a);
}
