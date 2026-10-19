// Beauty node: constants, colour domain, skin selection and face masks shared by
// the node passes (BeautyRoi / BeautyStats / BeautyBlur / BeautyApply).
//
// Working textures are linear FP16: SDR BT.709 with white = 1.0, or HDR scRGB
// (1 = 80 cd/m2) whose 203-nit reference white bWorkingToWhite maps to 1.0. The
// smoothing runs on a perceptual copy (the sRGB transfer below white, continued
// logarithmically above it with the same slope, sign kept for out-of-709
// components); every blend back into the picture uses linear values, so HDR
// highlights and negative wide-gamut components survive wherever a mask is 0.
//
// Skin selection follows SkinSoft (yasunari01/obs-filters fd5690f,
// skinsoft-filter/data/effects/skinsoft.effect, GPL-2.0-or-later): HSV hue
// distance plus saturation / luma windows with a shared softness, evaluated on
// the display-referred picture.
#ifndef VEYRA_BEAUTY_HLSLI
#define VEYRA_BEAUTY_HLSLI

cbuffer BeautyParams : register(b0) {
    uint bWidth; uint bHeight;          // node extent
    uint bSrcWidth; uint bSrcHeight;    // original (source) extent
    uint bLowWidth; uint bLowHeight;    // statistics grid
    uint bScale;                        // node pixels per statistics cell (each axis)
    uint bFlags;
    float bWorkingToWhite;
    float bRestoreStrength; float bRegionExpand; float bFeather;
    float bStrength; float bSmoothAmount; float bDetail; float bTexture;
    float bBrighten; float bSigma; float bEpsilon; float bMinConfidence;
    float bHueCenter; float bHueRange; float bSatMin; float bSatMax;
    float bLumaMin; float bLumaMax; float bSoftness; uint bPass;
    uint bMaxFaces; uint bReserved0; uint bReserved1; uint bReserved2;
};
static const uint kBeautyRestore = 1u;     // face restore active
static const uint kBeautySmooth = 2u;      // skin smoothing active
static const uint kBeautyRestoreSkin = 4u; // restore only the skin of the face
static const uint kBeautyAllSkin = 8u;     // smooth every skin-coloured pixel, not only faces
static const uint kBeautyShowMask = 16u;
static const uint kBeautyOriginal = 32u;   // the original texture holds this frame

SamplerState bPointSampler : register(s0);  // point, clamp
SamplerState bLinearSampler : register(s1); // bilinear, clamp

#include "BeautyFaces.hlsli"

// Face outlines from the face mesh (kShape* layout), one block per face slot.
StructuredBuffer<float2> Shapes : register(t5);

// Restore uses the estimated face oval; smoothing reaches a little further
// (forehead, chin, neck) and relies on the skin selection for its edge.
static const float kBeautyOvalX = 1.0, kBeautyOvalY = 1.12, kBeautyOvalLift = 0.06;
static const float kSmoothExpand = 0.10, kSmoothFeather = 0.12;

float BeautyEncode(float relative) {
    const float y = abs(relative);
    const float e = y > 1.0 ? 1.0 + 0.30469 * log2(y) : (y <= 0.0031308 ? 12.92 * y : 1.055 * pow(y, 1.0 / 2.4) - 0.055);
    return relative < 0.0 ? -e : e;
}
float BeautyDecode(float encoded) {
    const float e = abs(encoded);
    const float y = e > 1.0 ? exp2((e - 1.0) / 0.30469) : (e <= 0.04045 ? e / 12.92 : pow((e + 0.055) / 1.055, 2.4));
    return encoded < 0.0 ? -y : y;
}
float3 BeautyPerceptual(float3 linearRgb) {
    const float3 c = linearRgb * bWorkingToWhite;
    return float3(BeautyEncode(c.r), BeautyEncode(c.g), BeautyEncode(c.b));
}
float3 BeautyLinear(float3 perceptual) {
    return float3(BeautyDecode(perceptual.r), BeautyDecode(perceptual.g), BeautyDecode(perceptual.b)) / bWorkingToWhite;
}
float BeautyLuma(float3 rgb) { return dot(rgb, float3(0.2126, 0.7152, 0.0722)); }

// --- SkinSoft skin selection -------------------------------------------------
float3 BeautyRgbToHsv(float3 c) {
    const float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    const float4 p = lerp(float4(c.bg, k.wz), float4(c.gb, k.xy), step(c.b, c.g));
    const float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    const float d = q.x - min(q.w, q.y);
    const float e = 0.00001;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float BeautyRange(float value, float lo, float hi, float softness) {
    const float soft = max(softness, 0.001);
    return smoothstep(lo - soft, lo + soft, value) * (1.0 - smoothstep(hi - soft, hi + soft, value));
}
// `display`: perceptual (display-referred) RGB; values above white count as white.
float BeautySkin(float3 display) {
    const float3 c = saturate(display);
    const float3 hsv = BeautyRgbToHsv(c);
    const float luma = BeautyLuma(c);
    const float soft = bSoftness * 0.18;
    const float d = abs(hsv.x - bHueCenter);
    const float hue = 1.0 - smoothstep(max(bHueRange - soft, 0.001), bHueRange + soft, min(d, 1.0 - d));
    return saturate(hue * BeautyRange(hsv.y, bSatMin, bSatMax, soft) * BeautyRange(luma, bLumaMin, bLumaMax, soft));
}

// --- Face masks ----------------------------------------------------------------
// Soft oval of one face in node pixels: `expand` grows the axes, `feather` is the
// soft edge as a fraction of the face width.
float BeautyFaceOval(BeautyFace f, float2 p, float expand, float feather) {
    const float2 size = float2(bWidth, bHeight);
    // Eyes run along (cos, sin); the face's up direction is perpendicular to them.
    const float2 up = float2(f.shape.y, -f.shape.x);
    const float2 centre = f.box.xy * size + up * (kBeautyOvalLift * f.box.w * 2.0 * bHeight);
    const float2 axes = max(f.box.zw * size * float2(kBeautyOvalX, kBeautyOvalY) * (1.0 + expand), 1.0);
    const float2 d = p - centre;
    const float2 local = float2(dot(d, f.shape.xy), dot(d, float2(-f.shape.y, f.shape.x)));
    const float rho = length(local / axes);
    const float width = feather * 2.0 * f.box.z * bWidth / min(axes.x, axes.y);
    const float edge = max(width * 0.5, 0.002);
    return 1.0 - smoothstep(1.0 - edge, 1.0 + edge, rho);
}
// Signed distance in node pixels from p to a closed outline of the face mesh
// (negative inside, even-odd rule).
float BeautyOutlineDistance(uint first, uint count, float2 p) {
    const float2 size = float2(bWidth, bHeight);
    float2 previous = Shapes[first + count - 1] * size;
    float nearest = 1e30;
    bool inside = false;
    for (uint i = 0; i < count; ++i) {
        const float2 v = Shapes[first + i] * size;
        const float2 edge = previous - v, w = p - v;
        const float2 b = w - edge * saturate(dot(w, edge) / max(dot(edge, edge), 1e-12));
        nearest = min(nearest, dot(b, b));
        if ((v.y > p.y) != (previous.y > p.y) && p.x < edge.x * (p.y - v.y) / (previous.y - v.y) + v.x) inside = !inside;
        previous = v;
    }
    return inside ? -sqrt(nearest) : sqrt(nearest);
}
// Face width in node pixels (the detector's box), the unit of every margin below.
float BeautyFaceWidth(BeautyFace f) { return max(2.0 * f.box.z * bWidth, 1.0); }
// Soft region of face slot `slot` in node pixels: the mesh contour when the face has a
// usable mesh, grown by `expand` and softened over `feather` (fractions of the face
// width, the edge centred on the grown contour); otherwise the oval from its box.
float BeautyFaceRegion(uint slot, BeautyFace f, float2 p, float expand, float feather) {
    const uint base = slot * kShapeEntries;
    if (Shapes[base].x < 0.5) return BeautyFaceOval(f, p, expand, feather);
    const float2 size = float2(bWidth, bHeight);
    const float width = BeautyFaceWidth(f);
    const float grow = expand * width, soft = max(feather * width, 1.5);
    const float reach = max(grow, 0.0) + soft;
    if (any(p < Shapes[base + 2] * size - reach) || any(p > Shapes[base + 3] * size + reach)) return 0.0;
    const float d = BeautyOutlineDistance(base + kShapeContour, kShapeContourCount, p) - grow;
    return 1.0 - smoothstep(-0.5 * soft, 0.5 * soft, d);
}
// Eyes, brows and lips of face slot `slot` (grown a little, soft edge): 1 where the
// smoothing must not reach. Faces without a usable mesh have none.
float BeautyFeatures(uint slot, BeautyFace f, float2 p) {
    const uint base = slot * kShapeEntries;
    if (Shapes[base].x < 0.5) return 0.0;
    const float2 size = float2(bWidth, bHeight);
    const float width = BeautyFaceWidth(f);
    const float margin = 0.02 * width, soft = max(0.04 * width, 1.5);
    const float reach = margin + soft;
    if (any(p < Shapes[base + 4] * size - reach) || any(p > Shapes[base + 5] * size + reach)) return 0.0;
    float d = BeautyOutlineDistance(base + kShapeLips, kShapeLipsCount, p);
    d = min(d, BeautyOutlineDistance(base + kShapeLeftEye, kShapeEyeCount, p));
    d = min(d, BeautyOutlineDistance(base + kShapeRightEye, kShapeEyeCount, p));
    d = min(d, BeautyOutlineDistance(base + kShapeLeftBrow, kShapeBrowCount, p));
    d = min(d, BeautyOutlineDistance(base + kShapeRightBrow, kShapeBrowCount, p));
    return 1.0 - smoothstep(-0.5 * soft, 0.5 * soft, d - margin);
}
// Largest region over the faces this node accepts.
float BeautyFaces(StructuredBuffer<BeautyFace> faces, float2 p, float expand, float feather) {
    const uint count = min(faces[0].meta.x, kBeautyMaxFaces);
    float mask = 0.0;
    for (uint i = 0; i < count; ++i) {
        const BeautyFace f = faces[1 + i];
        if (f.shape.z < bMinConfidence || f.shape.w <= 0.0) continue;
        mask = max(mask, f.shape.w * BeautyFaceRegion(i, f, p, expand, feather));
    }
    return mask;
}
// Restore region, smoothing region and the eyes / brows / lips over the faces this
// node accepts, with one contour distance per face (the regions differ only in how
// far they grow and how soft their edge is).
struct BeautyMaskSet { float restore; float smooth; float features; };
BeautyMaskSet BeautyFaceMasks(StructuredBuffer<BeautyFace> faces, float2 p, bool smoothing) {
    BeautyMaskSet m;
    m.restore = 0.0; m.smooth = 0.0; m.features = 0.0;
    const uint count = min(faces[0].meta.x, kBeautyMaxFaces);
    const float2 size = float2(bWidth, bHeight);
    for (uint i = 0; i < count; ++i) {
        const BeautyFace f = faces[1 + i];
        if (f.shape.z < bMinConfidence || f.shape.w <= 0.0) continue;
        const uint base = i * kShapeEntries;
        if (Shapes[base].x < 0.5) {
            m.restore = max(m.restore, f.shape.w * BeautyFaceOval(f, p, bRegionExpand, bFeather));
            if (smoothing) m.smooth = max(m.smooth, f.shape.w * BeautyFaceOval(f, p, kSmoothExpand + bRegionExpand, kSmoothFeather));
            continue;
        }
        const float width = BeautyFaceWidth(f);
        const float restoreGrow = bRegionExpand * width, restoreSoft = max(bFeather * width, 1.5);
        const float smoothGrow = (kSmoothExpand + bRegionExpand) * width, smoothSoft = max(kSmoothFeather * width, 1.5);
        const float reach = max(max(restoreGrow, 0.0) + restoreSoft, smoothing ? max(smoothGrow, 0.0) + smoothSoft : 0.0);
        if (any(p < Shapes[base + 2] * size - reach) || any(p > Shapes[base + 3] * size + reach)) continue;
        const float d = BeautyOutlineDistance(base + kShapeContour, kShapeContourCount, p);
        m.restore = max(m.restore, f.shape.w * (1.0 - smoothstep(-0.5 * restoreSoft, 0.5 * restoreSoft, d - restoreGrow)));
        if (smoothing) {
            m.smooth = max(m.smooth, f.shape.w * (1.0 - smoothstep(-0.5 * smoothSoft, 0.5 * smoothSoft, d - smoothGrow)));
            m.features = max(m.features, f.shape.w * BeautyFeatures(i, f, p));
        }
    }
    return m;
}
// The eyes, brows and lips of the faces this node accepts.
float BeautyFeatureMask(StructuredBuffer<BeautyFace> faces, float2 p) {
    const uint count = min(faces[0].meta.x, kBeautyMaxFaces);
    float mask = 0.0;
    for (uint i = 0; i < count; ++i) {
        const BeautyFace f = faces[1 + i];
        if (f.shape.z < bMinConfidence || f.shape.w <= 0.0) continue;
        mask = max(mask, f.shape.w * BeautyFeatures(i, f, p));
    }
    return mask;
}

// --- Region (BeautyRoi.hlsl writes it, every other pass reads it) -------------
// Bytes: 0 full-size dispatch (x, y, 1), 12 statistics-grid dispatch (x, y, 1),
// 24 full origin, 32 full size, 40 grid origin, 48 grid size, 56 faces, flags.
static const uint kRoiFullArgs = 0u, kRoiLowArgs = 12u, kRoiOrigin = 24u, kRoiSize = 32u;
static const uint kRoiLowOrigin = 40u, kRoiLowSize = 48u, kRoiFaces = 56u, kRoiBytes = 64u;

// The face-restored picture P at node pixel p: the node input C with the same
// frame's original blended in. Restore masks follow the face contour (the oval
// without a mesh); the skin scope keeps only the original's skin inside it.
// `region`: the restore region at p (BeautyFaceMasks), before the strength.
float3 BeautyRestoredIn(Texture2D<float4> input, Texture2D<float4> original, int2 p, float region, out float mask) {
    const float3 c = input.Load(int3(p, 0)).rgb;
    mask = 0.0;
    if ((bFlags & kBeautyRestore) == 0 || (bFlags & kBeautyOriginal) == 0) return c;
    mask = region * bRestoreStrength;
    if (mask <= 0.0) return c;
    // Same size as the source: the original texel itself; otherwise bilinear.
    const float3 o = (bWidth == bSrcWidth && bHeight == bSrcHeight)
        ? original.Load(int3(p, 0)).rgb
        : original.SampleLevel(bLinearSampler, (float2(p) + 0.5) / float2(bWidth, bHeight), 0).rgb;
    if (bFlags & kBeautyRestoreSkin) mask *= BeautySkin(BeautyPerceptual(o));
    return mask >= 1.0 ? o : lerp(c, o, mask);   // a full restore is the original texel itself
}
float3 BeautyRestored(Texture2D<float4> input, Texture2D<float4> original, StructuredBuffer<BeautyFace> faces, int2 p, out float mask) {
    const bool restoring = (bFlags & kBeautyRestore) != 0 && (bFlags & kBeautyOriginal) != 0;
    const float region = restoring ? BeautyFaceMasks(faces, float2(p) + 0.5, false).restore : 0.0;
    return BeautyRestoredIn(input, original, p, region, mask);
}
#endif
