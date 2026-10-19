// Beauty node, smoothing step 1: statistics of the face-restored picture P on a
// coarse grid (bScale x bScale node pixels per cell): the mean perceptual colour
// and the mean squared perceptual luma. With the blur passes they become the
// local mean / variance of a fast guided filter (He & Sun 2015) - the
// variance-adaptive skin blend of GPUPixel's BeautyFaceUnitFilter (pixpark/gpupixel
// ef552bf, Apache-2.0) at a fraction of the cost, and without SkinSoft's sparse
// 9-tap blur, which ghosts at large radii.
#include "Beauty.hlsli"

Texture2D<float4> Input : register(t0);
Texture2D<float4> Original : register(t1);
StructuredBuffer<BeautyFace> Faces : register(t2);
ByteAddressBuffer Roi : register(t3);
RWTexture2D<float4> Stats : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 lowExtent = Roi.Load2(kRoiLowSize);
    if (any(id.xy >= lowExtent)) return;
    const uint2 cell = Roi.Load2(kRoiLowOrigin) + id.xy;
    const uint2 first = cell * bScale;
    const uint2 last = min(first + bScale, uint2(bWidth, bHeight));
    float3 sum = 0.0;
    float sumYY = 0.0, n = 0.0;
    for (uint y = first.y; y < last.y; ++y) {
        for (uint x = first.x; x < last.x; ++x) {
            float mask;
            const float3 i = BeautyPerceptual(BeautyRestored(Input, Original, Faces, int2(x, y), mask));
            const float l = BeautyLuma(i);
            sum += i; sumYY += l * l; n += 1.0;
        }
    }
    Stats[cell] = n > 0.0 ? float4(sum / n, sumYY / n) : 0.0;
}
