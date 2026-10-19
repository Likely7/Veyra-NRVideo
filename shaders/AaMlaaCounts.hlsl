// MLAA pass 2 (#53): morphological line search (notice and changes in AaMlaa.hlsli).
#include "AaMlaa.hlsli"
Texture2D<float2> edgeMask : register(t0);
RWTexture2D<float4> edgeCounts : register(u0);

float LoadEdge(int2 p, uint channel) {
    return edgeMask.Load(int3(AaClamp(p), 0))[channel];
}

// Low three bits contain the length; bit 3 records a found endpoint.
float EncodeCount(uint count, bool endpointFound) {
    return float(min(count, MAX_EDGE_LENGTH) + (endpointFound ? 8 : 0)) / 15.0;
}

float2 SearchEdge(int2 p, int2 negativeDir, int2 positiveDir, uint channel) {
    uint negativeCount = 0;
    uint positiveCount = 0;
    bool negativeActive = true;
    bool positiveActive = true;
    bool negativeEnd = false;
    bool positiveEnd = false;
    [unroll]
    for (uint i = 1; i <= MAX_EDGE_LENGTH; ++i) {
        if (negativeActive) {
            if (LoadEdge(p + negativeDir * int(i), channel) > 0.5) ++negativeCount;
            else { negativeActive = false; negativeEnd = true; }
        }
        if (positiveActive) {
            if (LoadEdge(p + positiveDir * int(i), channel) > 0.5) ++positiveCount;
            else { positiveActive = false; positiveEnd = true; }
        }
    }
    return float2(EncodeCount(negativeCount, negativeEnd),
        EncodeCount(positiveCount, positiveEnd));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    const float2 edges = edgeMask.Load(int3(p, 0)).rg;
    float4 result = 0;

    if (edges.r > 0.5) {
        result.xy = SearchEdge(p, int2(-1, 0), int2(1, 0), 0);
    }
    if (edges.g > 0.5) {
        result.zw = SearchEdge(p, int2(0, 1), int2(0, -1), 1);
    }

    edgeCounts[id.xy] = result;
}
