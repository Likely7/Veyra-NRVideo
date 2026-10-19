// Beauty node: shared face tracking, entirely on the GPU, once per source frame.
//
// The detector runs on every new frame. Its candidates (BeautyYuNetDecode.hlsl) go
// through the same non-maximum suppression as OpenCV's FaceDetectorYN (whole-pixel
// boxes, highest score first, ties in anchor order, IoU above tNms drops a box),
// keeping at most tMaxFaces faces. Those faces update up to eight tracks with stable
// ids. Each track is smoothed by a One Euro filter (Casiez, Roussel and Vogel 2012):
// a low-pass whose cutoff rises with the face's speed, so a face at rest does not
// jitter and a moving face is followed within about a frame, without the overshoot
// a predicting filter has where the motion stops. Speeds are in face widths per
// second, so the behaviour does not depend on the picture size. A face the detector
// misses keeps moving at its measured speed for the grace period, then fades out
// where it stopped; a new face fades in, except after a reset or in a picture. A
// track continues at a lower score than a new one needs (tKeepThreshold against
// tStartThreshold), so a face near the threshold does not flicker. Nothing is read
// back for the picture.
#include "BeautyFaces.hlsli"

cbuffer TrackParams : register(b0) {
    uint tDetWidth; uint tDetHeight;    // detector content size, pixels
    uint tCapacity;                     // candidate list capacity
    uint tFlags;                        // bit 0 detected this frame, bit 1 reset, bit 2 still picture
    float tDtMs;                        // time since the previous frame (source timestamps)
    float tMinCutoff;                   // One Euro cutoff of a face at rest, Hz
    float tNms;                         // suppression IoU
    float tGraceMs;                     // a missed face keeps moving this long
    float tFadeMs;                      // then fades out over this long
    uint tMaxFaces;                     // 1 .. 8
    float tKeepThreshold;               // score that keeps a track (the candidate list's own threshold)
    float tBeta;                        // cutoff increase per face width per second of speed
    float tDerivativeCutoff;            // Hz: smoothing of the measured speed
    float tFadeInMs;                    // a new face fades in over this long
    float tStartThreshold;              // score a new track needs
    uint tReserved0;
};
RWStructuredBuffer<BeautyFace> Faces : register(u0);
RWByteAddressBuffer Candidates : register(u1);

static const uint kGroup = 1024u;
static const float kMatchIou = 0.2;       // against the track moved on to this frame
static const float kScoreDecay = 0.97;    // per matched frame: the track's confidence is its recent peak

// 24 KB of the 32 KB limit: boxes are whole pixels as 16-bit pairs, a removed
// candidate has a negative score.
groupshared float gScore[kGroup];
groupshared uint gId[kGroup];
groupshared uint2 gBox[kGroup];
groupshared float gBestScore[kGroup];
groupshared uint gBestIndex[kGroup];
groupshared uint gKept[kBeautyMaxFaces];
groupshared uint gKeptCount;

uint pack16(int a, int b) { return (uint(a) & 0xffffu) | (uint(b) << 16); }
int4 unpackBox(uint2 p) {
    return int4((int(p.x << 16)) >> 16, int(p.x) >> 16, (int(p.y << 16)) >> 16, int(p.y) >> 16);
}
float overlapInt(int4 a, int4 b) {
    const int x1 = max(a.x, b.x), y1 = max(a.y, b.y);
    const int x2 = min(a.x + a.z, b.x + b.z), y2 = min(a.y + a.w, b.y + b.w);
    const int inter = (x2 > x1 && y2 > y1) ? (x2 - x1) * (y2 - y1) : 0;
    const int uni = a.z * a.w + b.z * b.w - inter;
    return uni > 0 ? float(inter) / float(uni) : 0.0;
}
// Normalised centre / half-size boxes.
float overlap(float4 a, float4 b) {
    const float2 lo = max(a.xy - a.zw, b.xy - b.zw), hi = min(a.xy + a.zw, b.xy + b.zw);
    const float2 extent = max(hi - lo, 0.0);
    const float inter = extent.x * extent.y;
    const float uni = 4.0 * (a.z * a.w + b.z * b.w) - inter;
    return uni > 0.0 ? inter / uni : 0.0;
}
// Exponential smoothing factor of a first-order low-pass with this cutoff over dtMs.
float lowpass(float cutoffHz, float dtMs) {
    const float tauMs = 1000.0 / (6.2831853 * max(cutoffHz, 1e-3));
    return 1.0 / (1.0 + tauMs / max(dtMs, 1e-3));
}
float2 lastCentre(BeautyFace f) { return float2(asfloat(f.meta.z), asfloat(f.meta.w)); }

[numthreads(1024, 1, 1)]
void main(uint t : SV_GroupIndex) {
    const bool detected = (tFlags & 1u) != 0;
    const uint n = detected ? min(Candidates.Load(0), min(tCapacity, kGroup)) : 0u;
    gScore[t] = -1.0;
    gId[t] = 0xffffffffu;
    gBox[t] = uint2(0u, 0u);
    if (t < n) {
        const uint o = 16u + t * 64u;
        const float4 box = asfloat(Candidates.Load4(o));
        const uint4 tail = Candidates.Load4(o + 48u);
        const float score = asfloat(tail.z);
        const bool valid = score >= tKeepThreshold && box.z > 0.0 && box.w > 0.0 && !any(isnan(box)) && !any(isinf(box)) &&
            all(abs(box) < 16384.0);
        const int4 b = int4(box);   // truncation toward zero, like OpenCV's Rect2i
        gScore[t] = valid ? score : -1.0;
        gId[t] = tail.w;
        gBox[t] = uint2(pack16(b.x, b.y), pack16(b.z, b.w));
    }
    if (t == 0) gKeptCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    // Greedy suppression, tMaxFaces rounds of "best remaining box, drop what overlaps it".
    for (uint k = 0; k < min(tMaxFaces, kBeautyMaxFaces); ++k) {
        gBestScore[t] = gScore[t];
        gBestIndex[t] = t;
        GroupMemoryBarrierWithGroupSync();
        for (uint stride = kGroup / 2; stride > 0; stride >>= 1) {
            if (t < stride) {
                const float a = gBestScore[t], b = gBestScore[t + stride];
                const uint ia = gBestIndex[t], ib = gBestIndex[t + stride];
                if (b > a || (b == a && b >= 0.0 && gId[ib] < gId[ia])) { gBestScore[t] = b; gBestIndex[t] = ib; }
            }
            GroupMemoryBarrierWithGroupSync();
        }
        const bool found = gBestScore[0] >= 0.0;
        const uint winner = gBestIndex[0];
        if (found && t == 0) { gKept[k] = winner; gKeptCount = k + 1u; }
        if (found && t < n && gScore[t] >= 0.0 && (t == winner || overlapInt(unpackBox(gBox[t]), unpackBox(gBox[winner])) > tNms))
            gScore[t] = -1.0;
        GroupMemoryBarrierWithGroupSync();
    }
    if (t != 0) return;

    const bool reset = (tFlags & 2u) != 0, still = (tFlags & 4u) != 0;
    const float dt = still ? 0.0 : max(tDtMs, 0.0);
    // A frame without a step on the timeline (reset, picture) shows its faces at once.
    const bool instant = reset || still || dt <= 0.0;
    BeautyFace header = Faces[0];
    uint count = (reset || still) ? 0u : min(header.meta.x, kBeautyMaxFaces);
    uint nextId = reset ? 1u : max(header.meta.y, 1u);
    BeautyFace tracks[kBeautyMaxFaces];
    float4 predicted[kBeautyMaxFaces];
    bool matched[kBeautyMaxFaces];
    for (uint i = 0; i < kBeautyMaxFaces; ++i) {
        tracks[i] = Faces[1 + i];
        matched[i] = false;
        if (i < count) tracks[i].motion.z += dt;
        // Where the face is now if it kept its speed since its last detection (for the grace period at most).
        predicted[i] = tracks[i].box;
        predicted[i].xy = lastCentre(tracks[i]) + tracks[i].motion.xy * min(tracks[i].motion.z, tGraceMs);
    }

    // 1. Detections, best first, update the best-overlapping unmatched track or start one.
    const float2 det = float2(max(tDetWidth, 1u), max(tDetHeight, 1u));
    for (uint d = 0; d < gKeptCount; ++d) {
        const uint o = 16u + gKept[d] * 64u;
        const float4 raw = asfloat(Candidates.Load4(o));
        const float4 eyes = asfloat(Candidates.Load4(o + 16u));       // right eye x, y, left eye x, y
        const float score = asfloat(Candidates.Load(o + 56u));
        const float4 box = float4((raw.xy + raw.zw * 0.5) / det, raw.zw * 0.5 / det);
        float2 roll = eyes.zw - eyes.xy;
        if (roll.x < 0.0) roll = -roll;
        roll = dot(roll, roll) > 1e-6 ? normalize(roll) : float2(1.0, 0.0);
        int best = -1; float bestOverlap = kMatchIou;
        for (uint j = 0; j < count; ++j) {
            if (matched[j]) continue;
            const float v = overlap(predicted[j], box);
            if (v > bestOverlap) { bestOverlap = v; best = int(j); }
        }
        if (best >= 0) {
            BeautyFace f = tracks[best];
            const float since = max(f.motion.z, 1e-3);    // ms since this track's last detection
            const float width = max(2.0 * f.box.z, 1e-4);
            const float2 from = lastCentre(f);             // filtered centre at that detection: speeds
            const float2 shown = f.box.xy;                  // where it is drawn now (moved on after a miss): blends
            if (instant || length(box.xy - shown) > width) {
                // A cut or a jump of more than a face width: the filter restarts there.
                f.box = box;
                f.shape.xy = roll;
                f.motion = float4(0.0, 0.0, 0.0, 0.0);
            } else {
                // One Euro: the measured speed (centre and half width, per ms), low-passed,
                // sets the cutoff for the position, size and roll.
                const float3 measured = float3(box.xy - from, box.z - f.box.z) / since;
                const float3 speed = lerp(float3(f.motion.xy, f.motion.w), measured, lowpass(tDerivativeCutoff, since));
                const float facesPerSecond = (length(speed.xy) / width + abs(speed.z) / max(f.box.z, 1e-4)) * 1000.0;
                const float a = lowpass(tMinCutoff + tBeta * facesPerSecond, since);
                f.box.xy = lerp(shown, box.xy, a);
                f.box.zw = lerp(f.box.zw, box.zw, a);
                const float2 r = lerp(f.shape.xy, roll, a);
                f.shape.xy = dot(r, r) > 1e-6 ? normalize(r) : roll;
                f.motion = float4(speed.xy, 0.0, speed.z);
            }
            f.shape.z = max(score, f.shape.z * kScoreDecay);
            f.shape.w = instant ? 1.0 : saturate(f.shape.w + dt / max(tFadeInMs, 1.0));
            f.motion.z = 0.0;
            f.meta.y += 1u;
            f.meta.zw = asuint(f.box.xy);
            tracks[best] = f;
            matched[best] = true;
        } else if (score >= tStartThreshold && count < min(tMaxFaces, kBeautyMaxFaces)) {
            BeautyFace f;
            f.box = box;
            f.shape = float4(roll, score, instant ? 1.0 : saturate(dt / max(tFadeInMs, 1.0)));
            f.motion = 0.0;
            f.meta = uint4(nextId++, 1u, asuint(box.x), asuint(box.y));
            tracks[count] = f;
            matched[count] = true;
            ++count;
        }
    }
    // 2. Missed faces: they keep their speed during the grace period, then fade out where they stopped.
    uint kept = 0;
    for (uint m = 0; m < count; ++m) {
        BeautyFace f = tracks[m];
        if (!matched[m]) {
            f.box.xy = predicted[m].xy;
            if (f.motion.z > tGraceMs) f.shape.w = saturate(f.shape.w - dt / max(tFadeMs, 1.0));
            if (f.shape.w <= 0.0) continue;
        }
        tracks[kept++] = f;
    }
    header.meta = uint4(kept, nextId, header.meta.z + 1u, n);
    Faces[0] = header;
    for (uint w = 0; w < kBeautyMaxFaces; ++w) {
        BeautyFace f = tracks[w];
        if (w >= kept) { f.box = 0.0; f.shape = float4(1.0, 0.0, 0.0, 0.0); f.motion = 0.0; f.meta = 0u; }
        Faces[1 + w] = f;
    }
}
