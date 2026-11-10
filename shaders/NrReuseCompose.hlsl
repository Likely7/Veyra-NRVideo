// NR inference reuse, motion step (docs/NR_INFERENCE_REUSE_2026-11-07.md).
//
// Veyra's own design: a frame that skips NR inference reuses the change made
// by the last frame that did infer. This pass keeps the motion from the
// current frame back to that inferred frame.
//
// Flow is current->previous in flow-extent pixels (previous = current + flow).
// When the previous frame inferred, the result is just this frame's flow, so
// a stream that never reuses carries exactly the vectors it always had. After
// a reused frame the stored displacement is chained once more:
//     D(p) = f(p) + D_prev(p + f(p))
// with D_prev sampled bilinearly. z is a 0..1 validity weight: 0 where the
// chained position leaves the frame or a vector is not finite.
Texture2D<float2> Flow : register(t0);
Texture2D<float4> Previous : register(t1);
RWTexture2D<float4> Displacement : register(u0);
// The same displacement in the flow texture's own format, so NR inputs and
// the temporal stabiliser can read it exactly like the ordinary flow.
RWTexture2D<float2> Motion : register(u1);
cbuffer Settings : register(b0) {
    uint Width;   // flow extent
    uint Height;
    uint Chain;   // 0: previous frame inferred; 1: chain through Previous
    uint Unused0;
    float4 Unused1;
};
SamplerState PointClamp : register(s0);
SamplerState LinearClamp : register(s1);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Width || id.y >= Height) return;
    const float2 f = Flow[id.xy];
    if (!all(isfinite(f))) {
        Displacement[id.xy] = float4(0, 0, 0, 0);
        Motion[id.xy] = float2(0, 0);
        return;
    }
    if (Chain == 0) {
        // Bit-exact passthrough: the vectors an inferring stream always had.
        const float2 at = float2(id.xy) + 0.5 + f;
        const bool inside = all(at >= 0) && all(at <= float2(Width, Height));
        Displacement[id.xy] = float4(f, inside ? 1 : 0, 0);
        Motion[id.xy] = f;
        return;
    }
    const float2 size = float2(Width, Height);
    const float2 at = float2(id.xy) + 0.5 + f;
    float valid = all(at >= 0) && all(at <= size) ? 1 : 0;
    const float4 previous = Previous.SampleLevel(LinearClamp, at / size, 0);
    float2 d = f + previous.xy;
    valid *= saturate(previous.z);
    if (!all(isfinite(d))) { d = f; valid = 0; }
    Displacement[id.xy] = float4(d, valid, 0);
    Motion[id.xy] = d;
}
