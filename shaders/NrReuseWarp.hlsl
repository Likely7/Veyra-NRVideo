// NR inference reuse, picture step (docs/NR_INFERENCE_REUSE_2026-11-07.md).
//
// A frame that skips NR inference shows its own source plus the change the
// last inferred frame made, carried along the displacement to that frame:
//     out(x) = S(x) + w(x) * (O_key(q) - S_key(q)),   q = x + D(x)
// S is this frame's NR chain input, O_key / S_key the chain output and input
// of the inferred frame. The change, not the old picture, is carried, so the
// frame's own source detail and position are always current.
//
// w falls to 0 where the carried key source does not match this frame's
// source (uncovered background, a wrong vector, a cut) or where q leaves the
// frame. Those pixels show the unenhanced source rather than an enhancement
// placed on the wrong content. The next frame always infers again.
//
// The NR protection regions are applied again at this frame's own pixels, so
// a carried change never reaches into a protected region.
#include "NrProtection.hlsli"
Texture2D<float4> Source : register(t0);
Texture2D<float4> KeyOutput : register(t1);
Texture2D<float4> KeySource : register(t2);
Texture2D<float4> Displacement : register(t3); // flow extent: xy current->key, z validity
RWTexture2D<float4> Output : register(u0);
cbuffer Settings : register(b0) {
    uint Width;       // NR chain extent
    uint Height;
    uint FlowWidth;   // displacement extent
    uint FlowHeight;
    float Low;        // perceptual box-luma mismatch: full weight below Low,
    float High;       // none above High
    uint Unused0;
    uint Unused1;
    float ProtectionEnabled;
    float ProtectionFeather;
    float DetailLow;  // perceptual detail mismatch: fine change kept below DetailLow,
    float DetailHigh; // only the neighbourhood change above DetailHigh
    float4 Regions[4];
};
SamplerState PointClamp : register(s0);
SamplerState LinearClamp : register(s1);

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// Compare in a display-like encoding so one threshold means the same in
// shadows and highlights; the stored values stay linear.
float Encode(float v) { return pow(max(v, 0), 1.0 / 2.2); }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Width || id.y >= Height) return;
    const float4 source = Source[id.xy];
    const float2 size = float2(Width, Height);
    const float2 uv = (float2(id.xy) + 0.5) / size;
    const float4 d = Displacement.SampleLevel(LinearClamp, uv, 0);
    const float2 at = float2(id.xy) + 0.5 + d.xy * size / float2(FlowWidth, FlowHeight);
    float w = saturate(d.z) * (1 - NrProtection(float2(id.xy) + 0.5, uint2(Width, Height), ProtectionEnabled, ProtectionFeather, Regions));
    if (!all(isfinite(at)) || any(at < 0) || any(at > size)) w = 0;
    float3 result = source.rgb;
    if (w > 0) {
        float3 current = 0, keySource = 0, keyOutput = 0;
        const int2 last = int2(Width, Height) - 1;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x) {
                const float2 q = (at + float2(x, y)) / size;
                current += Source[clamp(int2(id.xy) + int2(x, y), int2(0, 0), last)].rgb;
                keySource += KeySource.SampleLevel(LinearClamp, q, 0).rgb;
                keyOutput += KeyOutput.SampleLevel(LinearClamp, q, 0).rgb;
            }
        current /= 9; keySource /= 9; keyOutput /= 9;
        // The neighbourhood must match: otherwise the change belongs to other content.
        const float mismatch = abs(Encode(Luma(current)) - Encode(Luma(keySource)));
        w *= 1 - smoothstep(Low, High, mismatch);
        // The change is split. Its neighbourhood mean (tone, colour) is carried
        // wherever the neighbourhood matches. Its fine detail is carried only
        // where this pixel's own detail matches the key's, so detail that does
        // not follow the motion (grain, noise, flicker) is never stamped onto new
        // detail; such pixels keep their own detail with the carried tone.
        const float3 centreSource = KeySource.SampleLevel(LinearClamp, at / size, 0).rgb;
        const float3 centreOutput = KeyOutput.SampleLevel(LinearClamp, at / size, 0).rgb;
        const float detailCurrent = Encode(Luma(source.rgb)) - Encode(Luma(current));
        const float detailKey = Encode(Luma(centreSource)) - Encode(Luma(keySource));
        const float detailWeight = 1 - smoothstep(DetailLow, DetailHigh, abs(detailCurrent - detailKey));
        const float3 coarse = keyOutput - keySource;
        const float3 change = coarse + detailWeight * ((centreOutput - centreSource) - coarse);
        if (all(isfinite(change))) result = source.rgb + w * change;
    }
    // SDR only (the host refuses HDR): same floor as NrResidualComposite.
    Output[id.xy] = float4(max(0, result), source.a);
}
