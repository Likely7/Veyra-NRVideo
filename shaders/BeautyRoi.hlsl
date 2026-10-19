// Beauty node: the region this node touches this frame, as indirect dispatch
// arguments. The union of the accepted faces' regions (their contour box, or the
// oval without a mesh, grown by the restore and smoothing reach and the smoothing
// filter's footprint) clamped to the node; the whole frame when smoothing every
// skin pixel; nothing at all without faces. The passes
// after this one only run on that region; the rest of the output is the input copy.
#include "Beauty.hlsli"

StructuredBuffer<BeautyFace> Faces : register(t2);
RWByteAddressBuffer Roi : register(u0);

[numthreads(1, 1, 1)]
void main() {
    const float2 size = float2(bWidth, bHeight);
    const bool smooth = (bFlags & kBeautySmooth) != 0;
    const bool restore = (bFlags & kBeautyRestore) != 0 && (bFlags & kBeautyOriginal) != 0;
    // The statistics grid around a pixel reaches 3 sigma (capped at 12 cells) plus the bilinear neighbour.
    const float filter = smooth ? (min(ceil(3.0 * bSigma), 12.0) * 2.0 + 2.0) * float(bScale) : 0.0;
    float2 lo = size, hi = 0.0;
    uint faces = 0;
    if (smooth && (bFlags & kBeautyAllSkin)) { lo = 0.0; hi = size; faces = 1; }
    else {
        const uint count = min(Faces[0].meta.x, kBeautyMaxFaces);
        for (uint i = 0; i < count; ++i) {
            const BeautyFace f = Faces[1 + i];
            if (f.shape.z < bMinConfidence || f.shape.w <= 0.0) continue;
            float reach = 0.0;
            if (restore) reach = max(reach, (1.0 + bRegionExpand) * (1.0 + bFeather * 2.0));
            if (smooth || (bFlags & kBeautyShowMask)) reach = max(reach, (1.0 + kSmoothExpand + bRegionExpand) * (1.0 + kSmoothFeather * 2.0));
            if (reach <= 0.0) continue;
            if (Shapes[i * kShapeEntries].x >= 0.5) {
                // The mesh contour's box, grown like BeautyFaceRegion.
                const float width = max(2.0 * f.box.z * bWidth, 1.0);
                float grow = 0.0;
                if (restore) grow = max(grow, max(bRegionExpand, 0.0) + bFeather);
                if (smooth || (bFlags & kBeautyShowMask)) grow = max(grow, max(kSmoothExpand + bRegionExpand, 0.0) + kSmoothFeather);
                const float margin = grow * width + 2.0 + filter;
                lo = min(lo, Shapes[i * kShapeEntries + 2] * size - margin);
                hi = max(hi, Shapes[i * kShapeEntries + 3] * size + margin);
                ++faces;
                continue;
            }
            // Bounding circle of the (rotated, lifted) oval.
            const float2 axes = f.box.zw * size * float2(kBeautyOvalX, kBeautyOvalY) * reach;
            const float radius = max(axes.x, axes.y) * 1.05 + kBeautyOvalLift * f.box.w * 2.0 * bHeight + filter + 2.0;
            const float2 centre = f.box.xy * size;
            lo = min(lo, centre - radius);
            hi = max(hi, centre + radius);
            ++faces;
        }
    }
    uint2 origin = 0, extent = 0;
    if (faces) {
        origin = uint2(clamp(floor(lo), 0.0, size));
        const uint2 end = uint2(clamp(ceil(hi), 0.0, size));
        extent = select(end > origin, end - origin, uint2(0, 0));
    }
    if (any(extent == 0)) { origin = 0; extent = 0; }
    // Statistics cells covering the region (bScale node pixels each).
    const uint2 lowOrigin = origin / bScale;
    const uint2 lowEnd = min((origin + extent + bScale - 1) / bScale, uint2(bLowWidth, bLowHeight));
    const uint2 lowExtent = any(extent == 0) ? uint2(0, 0) : lowEnd - lowOrigin;
    Roi.Store3(kRoiFullArgs, uint3((extent + 7) / 8, any(extent == 0) ? 0u : 1u));
    Roi.Store3(kRoiLowArgs, uint3((lowExtent + 7) / 8, any(lowExtent == 0) ? 0u : 1u));
    Roi.Store2(kRoiOrigin, origin);
    Roi.Store2(kRoiSize, extent);
    Roi.Store2(kRoiLowOrigin, lowOrigin);
    Roi.Store2(kRoiLowSize, lowExtent);
    Roi.Store2(kRoiFaces, uint2(faces, bFlags));
}
