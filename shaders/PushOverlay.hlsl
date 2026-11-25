// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS Crosshair Hold, the steps of one real frame (pipeline/PushOverlay.h is the same rule without
// graphics, statement for statement: change both together). One shader, the step chosen by `stage`; the
// textures in t0..t2 and u0..u1 are what that step needs:
//   0 blocks   t0 this real frame, t1 the one before          u0 per block of 16 x 16: 1 when its picture moves
//   1 decide   t0 the blocks                                  u0 one texel: 1 when the zone moves
//   2 count    t0 this real frame, t1 that texel, t2 blocks   u0 the counts, four kinds a pixel (read and written)
//   3 judge    t0 the counts, t1 the memory before            u0 1 where a bar is accepted, u1 the memory after
//   4 mask     t0 the accepted bars                           u0 the mask: the bars, a few pixels round them, and the inside of a small closed shape
// Everything but the two frames is the size of the zone and addressed in the zone's own pixels. (The fill is
// PushOverlayFill.hlsl: it writes a frame, and a texture of a frame's format is only ever written here, never read
// back through the same view.)
Texture2D<float4> texA : register(t0);
Texture2D<float4> texB : register(t1);
Texture2D<float4> texC : register(t2);
RWTexture2D<float4> outA : register(u0);
RWTexture2D<float4> outB : register(u1);
cbuffer Constants : register(b0) {
    uint zoneX, zoneY, zoneSize, stage;
    uint outputMode, reset, flank, stand;      // outputMode: 0 sRGB, 1 scRGB (linear), 2 PQ
    uint longest, shortest, fresh, memory;
    uint grow, reach, blocks, hole;
    // PushFPS Subtitle Hold: a zone may be a rectangle. zoneSize and blocks are its width, these its height
    // (for the crosshair's square, zoneHeight = zoneSize and blocksY = blocks).
    uint zoneHeight, blocksY, pad0, pad1;
};
static const float kContrast = 0.10;
static const float kMoveRatio = 2.0;
static const float kMoveFloor = 0.004 * 0.004;
static const int kMoveShareDivisor = 10;
static const int kBlock = 16;
static const int kNearBlocks = 1;      // a frame counts for a pixel when a block this near to its own moves
static const int kMaxFlank = 8, kMaxGrow = 4, kMaxLongest = 160, kMaxHole = 12;

float perceptual1(float v) { return outputMode == 1 ? sqrt(max(v, 0) / 2.5) : v; }
float lumaOf(float3 c) { return 0.2126 * perceptual1(c.r) + 0.7152 * perceptual1(c.g) + 0.0722 * perceptual1(c.b); }
bool inZone(int2 p) { return p.x >= 0 && p.y >= 0 && p.x < int(zoneSize) && p.y < int(zoneHeight); }
float lumaA(int2 p) { return lumaOf(texA[p + int2(zoneX, zoneY)].rgb); }   // the frame in t0, at a pixel of the zone
float lumaB(int2 p) { return lumaOf(texB[p + int2(zoneX, zoneY)].rgb); }   // the frame in t1

bool barAt(int2 p, int kind) {
    const float sign = kind < 2 ? 1.0 : -1.0;
    const int2 d = (kind & 1) != 0 ? int2(0, 1) : int2(1, 0);
    const float here = sign * lumaA(p);
    float a = 1e30, b = 1e30;
    [loop] for (int i = 1; i <= int(flank) && i <= kMaxFlank; ++i) {
        const int2 q = p - i * d, r = p + i * d;
        if (inZone(q)) a = min(a, sign * lumaA(q));
        if (inZone(r)) b = min(b, sign * lumaA(r));
    }
    return here - a >= kContrast && here - b >= kContrast;
}
float nextCount(float count, bool bar, bool moving) {
    if (bar) {
        const float c = count >= 1.0 ? count : 1.0;
        return moving ? min(c + 1.0, 255.0) : c;
    }
    const float c = count <= -1.0 ? count : -1.0;
    return moving ? max(c - 1.0, -float(stand) - 1.0) : c;
}
bool isBar(float count) { return count >= 1.0; }
bool hasStood(float count) { return count >= float(stand) + 1.0; }
bool longNoBar(float count) { return count <= -float(stand) - 1.0; }
float pick(float4 v, int kind) { return kind == 0 ? v.x : kind == 1 ? v.y : kind == 2 ? v.z : v.w; }

// x: accepted, y: found
float2 judge(int2 p, int kind) {
    const float own = pick(texA[p], kind);
    if (!isBar(own)) return float2(0, 0);
    const int2 d = (kind & 1) != 0 ? int2(1, 0) : int2(0, 1);
    bool stood = hasStood(own);
    int length = 1;
    int known = (reset == 0 && pick(texB[p], kind) > 0) ? 1 : 0;
    float beyond0 = 0, beyond1 = 0;
    [loop] for (int side = 0; side < 2; ++side) {
        const int way = side != 0 ? 1 : -1;
        int2 q = p;
        bool ended = false;
        [loop] for (int i = 0; i <= kMaxLongest; ++i) {
            q += way * d;
            if (!inZone(q)) return float2(0, 0);                 // the bar reaches the zone's edge
            const float c = pick(texA[q], kind);
            if (!isBar(c)) { if (side == 0) beyond0 = c; else beyond1 = c; ended = true; break; }
            if (++length > int(longest)) return float2(0, 0);
            stood = stood && hasStood(c);
            known += (reset == 0 && pick(texB[q], kind) > 0) ? 1 : 0;
        }
        if (!ended) return float2(0, 0);
    }
    if (length < int(shortest)) return float2(0, 0);
    if (stood && longNoBar(beyond0) && longNoBar(beyond1)) return float2(1, 1);
    return float2((known >= int(shortest) && length - known <= int(fresh)) ? 1 : 0, 0);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 p = int2(id.xy);
    if (stage == 0) {                       // does this block's picture move
        if (id.x >= blocks || id.y >= blocksY) return;
        float change = 0, detail = 0;
        if (reset == 0) {
            [loop] for (int y = 0; y < kBlock; ++y)
                [loop] for (int x = 0; x < kBlock; ++x) {
                    const int2 q = p * kBlock + int2(x, y);
                    const float now = lumaA(q), before = lumaB(q);
                    const float dd = before - now;
                    change += dd * dd;
                    float g = 0;
                    if (q.x > 0) { const float u = before - lumaB(q - int2(1, 0)), v = now - lumaA(q - int2(1, 0)); g += u * u + v * v; }
                    if (q.y > 0) { const float u = before - lumaB(q - int2(0, 1)), v = now - lumaA(q - int2(0, 1)); g += u * u + v * v; }
                    detail += g * 0.5;
                }
            const float n = float(kBlock * kBlock);
            change /= n; detail /= n;
        }
        outA[p] = float4((reset == 0 && change > kMoveFloor && change >= kMoveRatio * (detail + 1e-7)) ? 1 : 0, 0, 0, 0);
        return;
    }
    if (stage == 1) {                       // does the zone move: a tenth of its blocks
        if (id.x != 0 || id.y != 0) return;
        int moving = 0;
        [loop] for (uint y = 0; y < blocksY; ++y)
            [loop] for (uint x = 0; x < blocks; ++x) moving += texA[int2(x, y)].x > 0.5 ? 1 : 0;
        outA[int2(0, 0)] = float4((reset == 0 && moving * kMoveShareDivisor >= int(blocks * blocksY)) ? 1 : 0, 0, 0, 0);
        return;
    }
    if (id.x >= zoneSize || id.y >= zoneHeight) return;
    if (stage == 2) {                       // the counts: a frame counts when the zone moves, and the picture near this pixel with it
        bool moving = false;
        if (reset == 0 && texB[int2(0, 0)].x > 0.5) {
            const int2 b = min(p / kBlock, int2(int(blocks) - 1, int(blocksY) - 1));
            [loop] for (int j = max(b.y - kNearBlocks, 0); j <= min(b.y + kNearBlocks, int(blocksY) - 1); ++j)
                [loop] for (int i = max(b.x - kNearBlocks, 0); i <= min(b.x + kNearBlocks, int(blocks) - 1); ++i)
                    if (texC[int2(i, j)].x > 0.5) moving = true;
        }
        const float4 c = reset != 0 ? float4(0, 0, 0, 0) : outA[p];
        outA[p] = float4(nextCount(c.x, barAt(p, 0), moving), nextCount(c.y, barAt(p, 1), moving),
                         nextCount(c.z, barAt(p, 2), moving), nextCount(c.w, barAt(p, 3), moving));
        return;
    }
    if (stage == 3) {                       // which bars are found or followed, and what is remembered
        const float4 before = reset != 0 ? float4(0, 0, 0, 0) : texB[p];
        float4 after;
        bool accepted = false;
        [unroll] for (int kind = 0; kind < 4; ++kind) {
            const float2 j = judge(p, kind);
            accepted = accepted || j.x > 0.5;
            const float m = j.y > 0.5 ? float(memory) : max(pick(before, kind) - 1.0, 0.0);
            if (kind == 0) after.x = m; else if (kind == 1) after.y = m; else if (kind == 2) after.z = m; else after.w = m;
        }
        outA[p] = float4(accepted ? 1 : 0, 0, 0, 0);
        outB[p] = after;
        return;
    }
    {                                       // stage 4, the mask: the accepted bars and a few pixels round them
        bool any = false;
        const int g = min(int(grow), kMaxGrow);
        [loop] for (int j = -g; j <= g; ++j)
            [loop] for (int i = -g; i <= g; ++i) {
                const int2 q = p + int2(i, j);
                if (i * i + j * j <= g * g + 1 && inZone(q) && texA[q].x > 0.5) any = true;
            }
        if (!any) {                         // inside a small closed shape: an accepted bar near on all four sides
            static const int2 dirs[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
            const int h = min(int(hole), kMaxHole);
            bool all = true;
            [loop] for (int n = 0; n < 4; ++n) {
                bool hit = false;
                [loop] for (int k = 1; k <= h; ++k) {
                    const int2 q = p + dirs[n] * k;
                    if (inZone(q) && texA[q].x > 0.5) { hit = true; break; }
                }
                if (!hit) { all = false; break; }
            }
            any = all;
        }
        outA[p] = float4(any ? 1 : 0, 0, 0, 0);
        return;
    }
}
