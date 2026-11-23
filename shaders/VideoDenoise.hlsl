// SPDX-License-Identifier: GPL-3.0-only
// Bilateral Mean adapted from Blinue/Magpie (7133a2dd4a2e7013b218510db22a724f8bdfb9bf),
// which ports bloc97's Anime4K (MIT). See THIRD_PARTY_NOTICES.md.
Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) {
    uint2 extent;
    float lumaStrength;
    float chromaStrength;
    float hdrWorking;
};

static const uint Block = 8;
static const int Radius = 2;
static const uint Tile = Block + 2 * Radius;
static const float3 Luma709 = float3(0.2126, 0.7152, 0.0722);
static const float Spatial[5] = {0.13533528, 0.60653066, 1.0, 0.60653066, 0.13533528};

// XYZ holds the original linear RGB, W its luma. Share overlapping neighbours
// across the group and preserve the centre's alpha and signed scRGB channels.
groupshared float4 pixels[Tile][Tile];

[numthreads(8, 8, 1)]
void main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint thread : SV_GroupIndex)
{
    const uint2 origin = group.xy * Block;
    for (uint i = thread; i < Tile * Tile; i += Block * Block) {
        const int2 at = clamp(int2(origin) + int2(i % Tile, i / Tile) - Radius,
                              int2(0, 0), int2(extent) - 1);
        const float3 rgb = Input.Load(int3(at, 0)).rgb;
        pixels[i / Tile][i % Tile] = float4(rgb, dot(rgb, Luma709));
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 at = origin + local.xy;
    if (any(at >= extent))
        return;
    const float4 source = Input.Load(int3(at, 0));
    if (lumaStrength == 0 && chromaStrength == 0) {
        Output[at] = source;
        return;
    }

    const float4 center = pixels[local.y + Radius][local.x + Radius];
    const float light = abs(center.w);
    // SDR working white is 1.0. HDR working white is in scRGB units
    // (1.0 = 80 nit). Calibrate the range weights in each linear domain.
    const bool hdr = hdrWorking > 0.5;
    const float lumaScale = rcp(hdr ? 0.003 + 0.018 * light : 0.006 + 0.030 * light);
    const float chromaScale = rcp(hdr ? 0.007 + 0.038 * light : 0.015 + 0.044 * light);
    float3 sum = 0.0;
    float weights = 0.0;
    [unroll]
    for (uint y = 0; y < 5; ++y) {
        [unroll]
        for (uint x = 0; x < 5; ++x) {
            const float4 delta = pixels[local.y + y][local.x + x] - center;
            const float dy = delta.w * lumaScale;
            const float3 dc = (delta.rgb - delta.w) * chromaScale;
            const float w = Spatial[y] * Spatial[x] * exp2(-0.7213475 * (dy * dy + dot(dc, dc)));
            sum += w * delta.rgb;
            weights += w;
        }
    }
    const float3 change = sum / weights; // centre weight is 1
    const float y = dot(change, Luma709);
    Output[at] = float4(source.rgb + chromaStrength * (change - y) + lumaStrength * y, source.a);
}
