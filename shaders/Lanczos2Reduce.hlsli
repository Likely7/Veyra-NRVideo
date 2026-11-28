#include "Lanczos2AA.hlsli"

// Share a tile of linear samples from ReadLinear(), so the SR resolve
// decodes/restores a texel once per group.
// The separable pass needs no full-sized intermediate or extra dispatch.
// An extra column keeps adjacent rows from starting on the same LDS bank.
#ifdef VEYRA_RESOLVE_2X
groupshared float4 linearTile[22][23];
groupshared float4 horizontalTile[22][9];
#else
static const uint2 TileCapacity = uint2(48, 32);
groupshared float4 linearTile[32][49];
groupshared float4 horizontalTile[32][9];
groupshared float horizontalWeights[8][48];
groupshared float verticalWeights[8][32];

struct FilterSpan {
    float center;
    float invScale;
    int first;
    int end;
};

FilterSpan Span(uint pixel, float scale)
{
    FilterSpan s;
    s.center = (pixel + 0.5)*scale - 0.5;
    const float radius = 2*max(1.0, scale);
    s.invScale = 2/radius;
    s.first = int(floor(s.center - radius)) + 1;
    s.end = int(ceil(s.center + radius));
    return s;
}

float Weight(FilterSpan span, int texel)
{
    return Lanczos2Weight((texel - span.center)*span.invScale).x;
}

struct FilterSum {
    float4 value;
    float weight;
    float3 lo;
    float3 hi;
    float peak;

    void add(float4 color, float w)
    {
        value += color*w;
        weight += w;
        if (hdr > 0.5 && w != 0.0) {
            peak = max(peak, max(color.r, max(color.g, color.b)));
            if (w > 0.0) {
                lo = min(lo, color.rgb);
                hi = max(hi, color.rgb);
            }
        }
    }

    float4 finish()
    {
        float4 filtered = value/weight;
        // Keep HDR highlights inside the positive lobe's range. This also
        // preserves negative wide-gamut components already in the source.
        if (peak > 1.0) filtered.rgb = clamp(filtered.rgb, lo, hi);
        return filtered;
    }
};

FilterSum EmptySum()
{
    FilterSum sum = (FilterSum)0;
    sum.lo = 1e30;
    sum.hi = -1e30;
    return sum;
}

// Very large reductions use the same full footprint when even one output
// pixel spans more than the shared tile. This is an exact, rare fallback.
float4 ReduceLarge(FilterSpan sx, FilterSpan sy, int2 last)
{
    FilterSum filtered = EmptySum();
    for (int y = sy.first; y < sy.end; ++y) {
        FilterSum row = EmptySum();
        for (int x = sx.first; x < sx.end; ++x) {
            row.add(ReadLinear(uint2(clamp(int2(x,y), int2(0,0), last))), Weight(sx,x));
        }
        filtered.add(row.finish(), Weight(sy,y));
    }
    return filtered.finish();
}
#endif

// 2:1 always samples at half-texel centers. Evaluate the eight known weights
// as four symmetric pairs, with no per-pixel span calculation or weight table.
float4 Filter2x(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h)
{
    const float4 value = -0.00886333*(a+h) - 0.04194003*(b+g)
                          + 0.11650009*(c+f) + 0.43430327*(d+e);
    if (hdr <= 0.5) return value;
    const float3 centerHi = max(max(c.rgb, d.rgb), max(e.rgb, f.rgb));
    const float3 hi = max(centerHi, max(max(a.rgb, b.rgb), max(g.rgb, h.rgb)));
    if (!any(hi > 1.0)) return value;
    const float3 centerLo = min(min(c.rgb, d.rgb), min(e.rgb, f.rgb));
    return float4(clamp(value.rgb, centerLo, centerHi), value.a);
}

void Reduce2x(uint2 origin, uint lane, uint2 thread, bool write, int2 sourceLast)
{
    const int2 first = int2(origin)*2 - 3;
    // Eight output pixels at stride two plus both lobes have a 22-texel span.
    for (uint i = lane; i < 22*22; i += 64) {
        const uint2 p = uint2(i%22, i/22);
        linearTile[p.y][p.x] = ReadLinear(uint2(clamp(first + int2(p), int2(0,0), sourceLast)));
    }
    GroupMemoryBarrierWithGroupSync();

    const uint x = thread.x*2;
    for (uint y = thread.y; y < 22; y += 8) {
        horizontalTile[y][thread.x] = Filter2x(linearTile[y][x], linearTile[y][x+1],
            linearTile[y][x+2], linearTile[y][x+3], linearTile[y][x+4],
            linearTile[y][x+5], linearTile[y][x+6], linearTile[y][x+7]);
    }
    GroupMemoryBarrierWithGroupSync();

    if (write) {
        const uint y = thread.y*2;
        outputTex[origin+thread] = Filter2x(horizontalTile[y][thread.x], horizontalTile[y+1][thread.x],
            horizontalTile[y+2][thread.x], horizontalTile[y+3][thread.x], horizontalTile[y+4][thread.x],
            horizontalTile[y+5][thread.x], horizontalTile[y+6][thread.x], horizontalTile[y+7][thread.x]);
    }
}

[numthreads(8,8,1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex, uint3 thread : SV_GroupThreadID)
{
    const uint2 extent = uint2(outW,outH);
    const uint2 origin = group.xy*uint2(tileW,tileH);
    const uint2 count = min(extent-origin, uint2(tileW,tileH));
    const uint2 pixel = origin + thread.xy;
    const bool write = all(thread.xy < count);
    const int2 sourceLast = int2(sourceW,sourceH) - 1;
#ifdef VEYRA_RESOLVE_2X
    Reduce2x(origin,lane,thread.xy,write,sourceLast);
#else
    const float2 factor = float2(sourceW,sourceH)/float2(extent);
    if (all(factor == 1.0)) {
        if (write) outputTex[pixel] = ReadLinear(pixel);
        return;
    }
    if (all(uint2(sourceW,sourceH) == 2*extent)) {
        Reduce2x(origin,lane,thread.xy,write,sourceLast);
        return;
    }

    const int2 first = int2(Span(origin.x,factor.x).first, Span(origin.y,factor.y).first);
    const uint2 last = origin + count - 1;
    const uint2 size = uint2(int2(Span(last.x,factor.x).end, Span(last.y,factor.y).end) - first);
    const FilterSpan sx = Span(pixel.x,factor.x);
    const FilterSpan sy = Span(pixel.y,factor.y);
    if (any(size > TileCapacity)) {
        if (write) outputTex[pixel] = ReduceLarge(sx,sy,sourceLast);
        return;
    }

    for (uint i = lane; i < size.x*size.y; i += 64) {
        const uint2 p = uint2(i%size.x, i/size.x);
        linearTile[p.y][p.x] = ReadLinear(uint2(clamp(first + int2(p), int2(0,0), sourceLast)));
    }
    for (uint x = thread.y; x < size.x && thread.x < count.x; x += 8)
        horizontalWeights[thread.x][x] = Weight(sx, first.x + int(x));
    for (uint y = thread.x; y < size.y && thread.y < count.y; y += 8)
        verticalWeights[thread.y][y] = Weight(sy, first.y + int(y));
    GroupMemoryBarrierWithGroupSync();

    if (thread.x < count.x) {
        for (uint y = thread.y; y < size.y; y += 8) {
            FilterSum row = EmptySum();
            for (int x = sx.first; x < sx.end; ++x) {
                const uint i = uint(x-first.x);
                row.add(linearTile[y][i], horizontalWeights[thread.x][i]);
            }
            horizontalTile[y][thread.x] = row.finish();
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (write) {
        FilterSum column = EmptySum();
        for (int y = sy.first; y < sy.end; ++y) {
            const uint i = uint(y-first.y);
            column.add(horizontalTile[i][thread.x], verticalWeights[thread.y][i]);
        }
        outputTex[pixel] = column.finish();
    }
#endif
}
