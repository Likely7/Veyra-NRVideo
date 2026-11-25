// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Crosshair Hold, the step of one generated frame (pipeline/PushOverlay.h, composeAt: the same
// statements). Where either of the two real frames was filled in before frame generation saw it, the
// generated frame becomes
//     lerp(A, B, t) + m * (G - lerp(fA, fB, t))
// A, B the real frames, fA, fB what frame generation was given, G what it made, m how much of the background
// shows through the overlay there (from the two frames themselves, over 5 x 5 masked pixels). Only those
// pixels are written; the rest of the generated frame is never read or touched.
Texture2D<float4> genTex : register(t0);     // the zone of the generated frame, as it was made (the zone's size)
Texture2D<float4> realA : register(t1);      // the older real frame
Texture2D<float4> realB : register(t2);      // the newer
Texture2D<float4> fillA : register(t3);      // what frame generation was given of the older
Texture2D<float4> fillB : register(t4);      // and of the newer
Texture2D<float4> maskA : register(t5);      // the mask the older was filled with (the zone's size)
Texture2D<float4> maskB : register(t6);      // and the newer's
RWTexture2D<float4> outTex : register(u0);   // the generated frame
cbuffer Constants : register(b0) {
    uint zoneX, zoneY, zoneSize, zoneHeight;   // PushFPS Subtitle Hold: a zone may be a rectangle (zoneSize = its width)
    float t, pad1, pad2, pad3;
};
static const float kWeightFloor = 1e-4;
bool masked(int2 p) { return p.x >= 0 && p.y >= 0 && p.x < int(zoneSize) && p.y < int(zoneHeight) && (maskA[p].x > 0.5 || maskB[p].x > 0.5); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= zoneSize || id.y >= zoneHeight) return;
    const int2 p = int2(id.xy);
    if (!masked(p)) return;
    const int2 zone = int2(zoneX, zoneY);
    const float3 a = realA[p + zone].rgb, b = realB[p + zone].rgb;
    // An unchanged stroke in both masks is opaque here. Moving background
    // around a thin outline must not leak into its steady center.
    if (maskA[p].x > 0.5 && maskB[p].x > 0.5 && all(abs(a - b) < 0.5 / 1023.0)) {
        outTex[p + zone] = float4(lerp(a, b, t), genTex[p].a);
        return;
    }
    float num = 0, den = 0;
    [loop] for (int j = -2; j <= 2; ++j)
        [loop] for (int i = -2; i <= 2; ++i) {
            const int2 q = p + int2(i, j);
            if (!masked(q)) continue;
            const float3 d = realA[q + zone].rgb - realB[q + zone].rgb;
            const float3 e = fillA[q + zone].rgb - fillB[q + zone].rgb;
            num += dot(d, e); den += dot(e, e);
        }
    const float m = clamp(num / (den + kWeightFloor), 0.0, 1.0);
    const float4 g = genTex[p];
    const float3 fa = fillA[p + zone].rgb, fb = fillB[p + zone].rgb;
    outTex[p + zone] = float4(a + (b - a) * t + m * (g.rgb - (fa + (fb - fa) * t)), g.a);
}
