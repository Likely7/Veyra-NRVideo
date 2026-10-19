// Face mesh output: the 468 points of every tracked face back in the frame (source
// fractions), smoothed over time, and the outlines the node passes mask with
// (BeautyFaces.hlsli kShape*). One group per face slot; every slot is written, an
// empty one as "no mesh".
//
// Smoothing: a One Euro filter per point (speed in face widths per second, the same
// settings as the face tracks), kept per track id so a face keeps its history when
// the track list is compacted; reset frames and pictures start fresh. A face uses its
// mesh when the model's face flag passes (with hysteresis) and the contour agrees with
// the detector's face box; otherwise it keeps the oval from its box.
//   inW / inH: source size; inOff: landmark tensor (1 x 1 x 1404); outOff: flag tensor;
//   resOff: crop parameters; wOff / bOff / aOff: rest cutoff, speed factor, speed
//   cutoff (float bits); resInfo: dt in ms (float bits); convInfo: bit 0 reset,
//   bit 1 still picture, bit 2 state parity
#include "BeautyMesh.hlsli"

RWStructuredBuffer<float2> Shapes : register(u1);
RWStructuredBuffer<float4> State : register(u2);

static const uint kPoints = 468u, kLandmarkQuads = 351u;
static const uint kStateFace = 1u + kPoints, kStateHalf = kBeautyMaxFaces * kStateFace;
static const float kStartLogit = -4.0, kKeepLogit = -5.5;
// MediaPipe face_mesh_connections.py (v0.8.9, Apache-2.0) as closed outlines, in the
// order of BeautyFaces.hlsli: contour, outer lips, left eye, right eye, left brow, right brow.
static const uint kOutline[kShapeOutlinePoints] = {
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149,
    150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 409, 270, 269, 267, 0, 37, 39, 40, 185,
    263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466,
    33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246,
    276, 283, 282, 295, 285, 336, 296, 334, 293, 300,
    46, 53, 52, 65, 55, 107, 66, 105, 63, 70};

groupshared float2 gPoint[kPoints];
groupshared uint gPrevious;

float lowpass(float cutoffHz, float dtMs) {
    const float tauMs = 1000.0 / (6.2831853 * max(cutoffHz, 1e-3));
    return 1.0 / (1.0 + tauMs / max(dtMs, 1e-3));
}
float component(float4 v, uint i) { return i == 0u ? v.x : i == 1u ? v.y : i == 2u ? v.z : v.w; }

[numthreads(512, 1, 1)]
void main(uint3 group : SV_GroupID, uint t : SV_GroupIndex) {
    const uint face = group.z;
    const uint parity = (convInfo >> 2) & 1u;
    const uint previousHalf = parity != 0u ? kStateHalf : 0u, nextHalf = parity != 0u ? 0u : kStateHalf;
    const uint shape = face * kShapeEntries;
    if (face >= meshFaces()) {
        if (t == 0u) {
            Shapes[shape] = float2(0.0, 0.0);
            State[nextHalf + face * kStateFace] = float4(0.0, 0.0, 0.0, 0.0);
        }
        return;
    }
    const BeautyFace f = Faces[1 + face];
    const bool fresh = (convInfo & 3u) != 0u;
    const float dt = asfloat(resInfo);
    if (t == 0u) {
        uint found = kMeshNone;
        if (!fresh && f.meta.x != 0u)
            for (uint s = 0; s < kBeautyMaxFaces; ++s)
                if (asuint(State[previousHalf + s * kStateFace].x) == f.meta.x) found = s;
        gPrevious = found;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint previous = gPrevious;
    const float2 size = float2(inW, inH);
    const float faceWidth = max(2.0 * f.box.z * size.x, 1.0);
    if (t < kPoints) {
        const float4 crop0 = Scratch[resOff + face * 2u], crop1 = Scratch[resOff + face * 2u + 1u];
        const uint base = inOff + face * kLandmarkQuads;
        const float x = component(Scratch[base + (3u * t) / 4u], (3u * t) % 4u);
        const float y = component(Scratch[base + (3u * t + 1u) / 4u], (3u * t + 1u) % 4u);
        // Crop pixels (0..192 across) -> centred -> rotated back -> picture -> fractions.
        const float2 c = (float2(x, y) - 96.0) * (crop0.z / 192.0);
        const float2 p = (crop0.xy + float2(crop1.x * c.x - crop1.y * c.y, crop1.y * c.x + crop1.x * c.y)) / size;
        float4 s = float4(p, 0.0, 0.0);
        if (previous != kMeshNone) {
            const float4 last = State[previousHalf + previous * kStateFace + 1u + t];
            if (dt > 0.0) {
                const float2 speed = lerp(last.zw, (p - last.xy) / dt, lowpass(asfloat(aOff), dt));
                const float facesPerSecond = length(speed * size) / faceWidth * 1000.0;
                s = float4(lerp(last.xy, p, lowpass(asfloat(wOff) + asfloat(bOff) * facesPerSecond, dt)), speed);
            } else s = last;
        }
        State[nextHalf + face * kStateFace + 1u + t] = s;
        gPoint[t] = s.xy;
    }
    GroupMemoryBarrierWithGroupSync();
    if (t < kShapeOutlinePoints) Shapes[shape + kShapeContour + t] = gPoint[kOutline[t]];
    if (t == 0u) {
        float2 lo = 1e9, hi = -1e9, featureLo = 1e9, featureHi = -1e9;
        for (uint i = 0; i < kShapeContourCount; ++i) { lo = min(lo, gPoint[kOutline[i]]); hi = max(hi, gPoint[kOutline[i]]); }
        for (uint j = kShapeContourCount; j < kShapeOutlinePoints; ++j) {
            featureLo = min(featureLo, gPoint[kOutline[j]]);
            featureHi = max(featureHi, gPoint[kOutline[j]]);
        }
        const float logit = Scratch[outOff + face].x;
        const bool wasValid = previous != kMeshNone && State[previousHalf + previous * kStateFace].y > 0.5;
        const float2 contourCentre = (lo + hi) * 0.5 * size;
        const float contourWidth = (hi.x - lo.x) * size.x;
        const bool agrees = all(isfinite(lo)) && all(isfinite(hi)) && length(contourCentre - f.box.xy * size) < 0.35 * faceWidth &&
                            contourWidth > 0.6 * faceWidth && contourWidth < 1.8 * faceWidth;
        const bool valid = isfinite(logit) && logit > (wasValid ? kKeepLogit : kStartLogit) && agrees;
        Shapes[shape] = float2(valid ? 1.0 : 0.0, 1.0 / (1.0 + exp(-clamp(logit, -40.0, 40.0))));
        Shapes[shape + 2u] = lo;
        Shapes[shape + 3u] = hi;
        Shapes[shape + 4u] = featureLo;
        Shapes[shape + 5u] = featureHi;
        State[nextHalf + face * kStateFace] = float4(asfloat(f.meta.x), valid ? 1.0 : 0.0, logit, 0.0);
    }
}
