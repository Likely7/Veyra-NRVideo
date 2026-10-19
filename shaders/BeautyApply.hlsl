// Beauty node, final step: one pass over this frame's region.
//
//   P  = the node input with the original restored inside the faces
//   B  = a * I + b, the guided-filter result of P's perceptual copy I (coefficients
//        from the statistics grid, bilinear)
//   SkinSoft (yasunari01/obs-filters fd5690f, GPL-2.0-or-later) then decides how
//   much of B to take: skin selection x face region (minus the eyes, brows and lips
//   of the face mesh), detail protection
//   1 - smoothstep(0.02, 0.22, |I - B| * detail * 3), texture restore back to I.
//   Brightening lifts the skin's perceptual luma (gamma 1 / (1 + 0.6 k), only
//   below white). The result goes back to linear and mixes with P by the
//   smoothing strength; outside the masks the input passes through unchanged.
// The mask view tints the restore area blue and the smoothing area magenta.
#include "Beauty.hlsli"

Texture2D<float4> Input : register(t0);
Texture2D<float4> Original : register(t1);
StructuredBuffer<BeautyFace> Faces : register(t2);
ByteAddressBuffer Roi : register(t3);
Texture2D<float4> Coefficients : register(t4);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 extent = Roi.Load2(kRoiSize);
    if (any(id.xy >= extent)) return;
    const int2 p = int2(Roi.Load2(kRoiOrigin) + id.xy);
    const float4 c = Input.Load(int3(p, 0));
    const bool smoothing = (bFlags & (kBeautySmooth | kBeautyShowMask)) != 0;
    const BeautyMaskSet masks = BeautyFaceMasks(Faces, float2(p) + 0.5, smoothing);
    float restoreMask;
    const float3 restored = BeautyRestoredIn(Input, Original, p, masks.restore, restoreMask);
    float3 result = restored;
    float smoothMask = 0.0;
    if (smoothing) {
        const float3 i = BeautyPerceptual(restored);
        // Every skin pixel, or the faces' regions; the eyes, brows and lips of the faces stay out.
        const float region = (bFlags & kBeautyAllSkin) ? 1.0 : masks.smooth;
        const float features = (bFlags & kBeautyAllSkin) ? BeautyFeatureMask(Faces, float2(p) + 0.5) : masks.features;
        smoothMask = region > 0.0 ? region * BeautySkin(i) * (1.0 - features) : 0.0;
        if ((bFlags & kBeautySmooth) && smoothMask > 0.0) {
            // Statistics cell centres sit at (cell + 0.5) * bScale; stay inside this frame's cells.
            const float2 lowOrigin = float2(Roi.Load2(kRoiLowOrigin)), lowExtent = float2(Roi.Load2(kRoiLowSize));
            const float2 grid = clamp((float2(p) + 0.5) / float(bScale), lowOrigin + 0.5, lowOrigin + lowExtent - 0.5);
            const float4 k = Coefficients.SampleLevel(bLinearSampler, grid / float2(bLowWidth, bLowHeight), 0);
            const float3 blurred = k.a * i + k.rgb;
            const float protection = 1.0 - smoothstep(0.02, 0.22, length(i - blurred) * bDetail * 3.0);
            const float3 smoothed = lerp(i, blurred, smoothMask * bSmoothAmount * protection);
            float3 retouched = lerp(smoothed, i, smoothMask * bTexture);
            if (bBrighten > 0.0) {
                const float y = BeautyLuma(retouched);
                if (y > 1e-4 && y < 1.0) retouched *= pow(y, 1.0 / (1.0 + 0.6 * bBrighten * smoothMask)) / y;
            }
            result = lerp(restored, BeautyLinear(retouched), bStrength);
        }
    }
    if (bFlags & kBeautyShowMask) {
        const float white = 1.0 / bWorkingToWhite;
        result = lerp(result, float3(0.10, 0.35, 1.0) * white, 0.45 * saturate(restoreMask));
        result = lerp(result, float3(1.0, 0.15, 0.85) * white, 0.45 * saturate(smoothMask));
    }
    Output[p] = float4(result, c.a);
}
