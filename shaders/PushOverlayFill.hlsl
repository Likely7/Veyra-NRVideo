// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Crosshair Hold, the fill (pipeline/PushOverlay.h, fillAt: the same statements). The frame that frame
// generation gets is a copy of the real frame; here its masked pixels are filled in from the nearest pixels
// outside the mask in eight directions, each weighted by one over its distance. A masked pixel with nothing
// unmasked within reach, and every pixel outside the mask, stays the copy.
Texture2D<float4> frameTex : register(t0);   // the real frame
Texture2D<float4> maskTex : register(t1);    // the mask (the zone's size)
RWTexture2D<float4> outTex : register(u0);   // the frame frame generation gets: only masked pixels are written
cbuffer Constants : register(b0) {
    uint zoneX, zoneY, zoneSize, reach;
    uint zoneHeight, pad0, pad1, pad2;     // PushFPS Subtitle Hold: a zone may be a rectangle (zoneSize = its width)
};
static const int kMaxReach = 32;
bool inZone(int2 p) { return p.x >= 0 && p.y >= 0 && p.x < int(zoneSize) && p.y < int(zoneHeight); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= zoneSize || id.y >= zoneHeight) return;
    const int2 p = int2(id.xy);
    if (!(maskTex[p].x > 0.5)) return;
    const int2 zone = int2(zoneX, zoneY);
    static const int2 dirs[8] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1), int2(1, 1), int2(1, -1), int2(-1, 1), int2(-1, -1)};
    float weight = 0;
    float3 sum = float3(0, 0, 0);
    const int farthest = min(int(reach), kMaxReach);
    [loop] for (int n = 0; n < 8; ++n) {
        const float unit = (dirs[n].x != 0 && dirs[n].y != 0) ? 1.41421356 : 1.0;
        [loop] for (int k = 1; k <= farthest; ++k) {
            const int2 q = p + dirs[n] * k;
            if (!inZone(q)) break;
            if (maskTex[q].x > 0.5) continue;
            const float w = 1.0 / (float(k) * unit);
            sum += frameTex[q + zone].rgb * w; weight += w;
            break;
        }
    }
    // Dense, outlined glyphs can cover every near sample. Search farther only for these interiors.
    if (!(weight > 0)) {
        [loop] for (int k=farthest+1;k<=min(kMaxReach,3*farthest) && !(weight>0);++k) {
            [unroll] for(int n=0;n<8;++n) {
                int2 q=p+dirs[n]*k;
                if(inZone(q)&&maskTex[q].x<=0.5){sum+=frameTex[q+zone].rgb;weight+=1;}
            }
        }
        if (!(weight>0)) return;
    }
    outTex[p + zone] = float4(sum / weight, frameTex[p + zone].a);
}
