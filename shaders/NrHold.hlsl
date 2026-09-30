// Output stabiliser (anti-flicker), stage 5 of
// docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md.
//
// Independent implementation of the behaviour publicly described by
// DLSS5-Feeder 1.17/1.18's "Output stabiliser": hold the previous output where
// the source has not visibly changed. No code was taken from that project; only
// the described behaviour. See THIRD_PARTY_NOTICES.md.
//
// Deliberate differences from that description, and why:
//   * No reprojection. The plan explicitly excludes it: a wrong motion vector
//     would drag the previous output into a region that did move, which is the
//     ghosting this pass exists to avoid. Regions that moved simply stop
//     holding.
//   * The change test compares the *source* (the image going into NR) against
//     the previous source, not against our own output. Comparing the output
//     with itself would feed the hold back into its own decision and let a held
//     region stay held forever, which is a smear.
Texture2D<float4> Current : register(t0);        // this frame's NR result
Texture2D<float4> PreviousOutput : register(t1); // what we emitted last frame
Texture2D<float4> Base : register(t2);           // this frame's NR input
Texture2D<float4> PreviousBase : register(t3);   // that input one frame ago
RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> NextBase : register(u1);
cbuffer Settings : register(b0) {
    uint2 Size;      // NR output extent (the dispatch grid)
    uint2 BaseSize;  // source extent; may differ under the realtime NR policy
    float HoldStrength;   // 0 is refused by the host, which skips the dispatch
    float Tolerance;      // relative box-luma delta that still counts as unchanged
    uint UseHistory;      // 0 on reset: publish current, seed history, hold nothing
};
// Perceptual luminance, matching the guide distance in NrTemporal.hlsl, so a
// given tolerance means the same thing in shadows as in highlights. The stored
// value is always linear, never mapped.
float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
int2 ClampBase(int2 p) { return clamp(p, int2(0, 0), int2(BaseSize) - int2(1, 1)); }
// Mean luminance of the 3x3 neighbourhood, clamped at the frame edge. A mean
// rather than a max: one hot pixel should not veto the hold for its neighbours,
// and one dead pixel should not let it through. Base is sampled at its own
// extent, so a downscaled NR extent still sees the same neighbourhood shape.
float BoxLumaBase(int2 p, float2 scale) {
    const int2 c = int2(float2(p) * scale);
    float sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            sum += Luma(Base[ClampBase(c + int2(x, y))].rgb);
    return sum / 9.0;
}
float BoxLumaPreviousBase(int2 p, float2 scale) {
    const int2 c = int2(float2(p) * scale);
    float sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            sum += Luma(PreviousBase[ClampBase(c + int2(x, y))].rgb);
    return sum / 9.0;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 p = int2(id.xy);
    if (any(p >= int2(Size))) return;
    const float4 current = Current.Load(int3(p, 0));
    // Publish this frame's source unconditionally as the next anchor: the anchor
    // must describe the source as it is now, whatever we decide about the hold.
    const float2 scale = float2(BaseSize) / float2(Size);
    const int2 baseTexel = ClampBase(int2((float2(p) + .5) * scale));
    NextBase[p] = Base.Load(int3(baseTexel, 0));
    // A non-finite sample must not be held: publish it and let the next frame
    // decide, rather than freezing corruption in place.
    if (!all(isfinite(current))) { Output[p] = current; return; }
    if (UseHistory == 0) { Output[p] = current; return; }
    const float4 previous = PreviousOutput.Load(int3(p, 0));
    if (!all(isfinite(previous)) || previous.a < .999) { Output[p] = current; return; }
    const float currentLuma = BoxLumaBase(p, scale);
    const float previousLuma = BoxLumaPreviousBase(p, scale);
    // Relative difference, so one tolerance holds in both shadows and
    // highlights. The epsilon keeps a near-black anchor from turning the
    // smallest representable change into a large relative one.
    const float delta = abs(currentLuma - previousLuma) / max(max(currentLuma, previousLuma), 1e-4);
    // Moved (or visibly changed) beyond tolerance: publish the new value and
    // hold nothing. This is what keeps motion from smearing.
    if (delta > Tolerance) { Output[p] = current; return; }
    // Unchanged: blend toward the previous output.
    Output[p] = float4(lerp(current.rgb, previous.rgb, HoldStrength), current.a);
}
