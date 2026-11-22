// SPDX-License-Identifier: GPL-3.0-only
// Shot-change test for one RIFE input pair (RifeDmlBackend), motion compensated.
// The histogram/SAD test of VfgShotChange.hlsl cannot tell a cut inside one game
// location (same palette and light) from a fast camera turn: on PS5 GTA capture
// both reach SAD 0.17-0.22 and histogram distance 0.25-0.34 (2026-11-22). Here
// each of 32 blocks of a 128x72 luma picture of A is matched in B within
// +-12 x +-6 pixels (about +-180 x +-90 at 1080p); a pan or turn still finds
// its match, a cut does not. The median best-match error decides.
// Measured on 30 fps GTA capture: continuous pairs stay below 0.043 (909 pairs),
// the clip's two real cuts reach 0.125 and 0.132, and random pairs at least 3 s
// apart exceed 0.07 in 94%.
//
// Three dispatches of 256 threads per real frame: mode 0 writes this frame's
// 128x72 luma (box average), mode 1 matches the 32 blocks, mode 2 takes the
// median. flags (64-bit at offset 0) = 1 when the pair crosses a cut; the
// subframe conversions are predicated on it and show the new frame instead.

cbuffer Constants : register(b0)
{
    uint width;          // colour frame
    uint height;
    uint paddedWidth;    // model tensor
    uint paddedHeight;
    uint mode;
    uint current;        // luma slot of this frame (0/1); the other holds A
    uint compare;        // 0 when the pair has no valid A: never a cut
    float threshold;     // median best-match error (0..1) above which a pair is a cut
};

RWByteAddressBuffer planes : register(u0);   // this frame, planar float RGB
RWByteAddressBuffer work : register(u1);     // luma[2][128*72], errors[32]
RWByteAddressBuffer flags : register(u2);

static const uint kW = 128;
static const uint kH = 72;
static const uint kPixels = kW * kH;
static const uint kBlockW = 16;
static const uint kBlockH = 18;
static const uint kBlocks = (kW / kBlockW) * (kH / kBlockH);   // 32
static const int kRangeX = 12;
static const int kRangeY = 6;
static const uint kWindowW = kBlockW + 2 * kRangeX;   // 40
static const uint kWindowH = kBlockH + 2 * kRangeY;   // 30
static const uint kCandidates = (kRangeX + 1) * (kRangeY + 1);   // steps of 2: 13 x 7

groupshared float blockA[kBlockW * kBlockH];
groupshared float windowB[kWindowW * kWindowH];
groupshared float candidate[256];

float lumaLoad(uint slot, uint x, uint y) { return asfloat(work.Load((slot * kPixels + y * kW + x) * 4)); }

[numthreads(256, 1, 1)]
void main(uint3 group : SV_GroupID, uint index : SV_GroupIndex)
{
    if (mode == 0) {
        const uint i = group.x * 256 + index;
        if (i >= kPixels) return;
        const uint x = i % kW, y = i / kW;
        const uint x0 = x * width / kW, x1 = max(x0 + 1, (x + 1) * width / kW);
        const uint y0 = y * height / kH, y1 = max(y0 + 1, (y + 1) * height / kH);
        const uint plane = paddedWidth * paddedHeight;
        float sum = 0;
        for (uint yy = y0; yy < y1; ++yy)
            for (uint xx = x0; xx < x1; ++xx) {
                const uint p = yy * paddedWidth + xx;
                sum += 0.299 * asfloat(planes.Load(p * 4)) + 0.587 * asfloat(planes.Load((plane + p) * 4)) +
                       0.114 * asfloat(planes.Load((2 * plane + p) * 4));
            }
        work.Store((current * kPixels + i) * 4, asuint(sum / float((x1 - x0) * (y1 - y0))));
        return;
    }
    if (mode == 1) {
        const uint previous = 1 - current;
        const uint bx = (group.x % (kW / kBlockW)) * kBlockW, by = (group.x / (kW / kBlockW)) * kBlockH;
        for (uint k = index; k < kBlockW * kBlockH; k += 256)
            blockA[k] = lumaLoad(previous, bx + k % kBlockW, by + k / kBlockW);
        for (uint k = index; k < kWindowW * kWindowH; k += 256) {
            const int wx = int(bx) - kRangeX + int(k % kWindowW), wy = int(by) - kRangeY + int(k / kWindowW);
            windowB[k] = (wx >= 0 && wy >= 0 && wx < int(kW) && wy < int(kH)) ? lumaLoad(current, uint(wx), uint(wy)) : 0;
        }
        GroupMemoryBarrierWithGroupSync();
        float error = 1e9;
        if (index < kCandidates) {
            const int dx = -kRangeX + 2 * int(index % (kRangeX + 1)), dy = -kRangeY + 2 * int(index / (kRangeX + 1));
            if (int(bx) + dx >= 0 && int(by) + dy >= 0 && int(bx) + dx + int(kBlockW) <= int(kW) && int(by) + dy + int(kBlockH) <= int(kH)) {
                float sad = 0;
                for (uint k = 0; k < kBlockW * kBlockH; ++k) {
                    const uint kx = k % kBlockW, ky = k / kBlockW;
                    sad += abs(blockA[k] - windowB[(uint(dy + kRangeY) + ky) * kWindowW + uint(dx + kRangeX) + kx]);
                }
                error = sad / float(kBlockW * kBlockH);
            }
        }
        candidate[index] = error;
        GroupMemoryBarrierWithGroupSync();
        if (index == 0) {
            float best = 1e9;
            for (uint c = 0; c < kCandidates; ++c) best = min(best, candidate[c]);
            work.Store((2 * kPixels + group.x) * 4, asuint(best));
        }
        return;
    }
    if (index != 0) return;
    float v[kBlocks];
    for (uint b = 0; b < kBlocks; ++b) v[b] = asfloat(work.Load((2 * kPixels + b) * 4));
    for (uint i = 1; i < kBlocks; ++i) {
        const float key = v[i];
        int j = int(i) - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; --j; }
        v[j + 1] = key;
    }
    const float median = 0.5 * (v[kBlocks / 2 - 1] + v[kBlocks / 2]);
    const bool cut = compare != 0 && median > threshold;
    flags.Store2(0, uint2(cut ? 1u : 0u, 0u));
    flags.Store(8, asuint(median));
    if (cut) {
        uint previousCount;
        flags.InterlockedAdd(16, 1, previousCount);
    }
}
