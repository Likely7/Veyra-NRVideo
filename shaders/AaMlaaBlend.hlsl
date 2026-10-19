// MLAA pass 3 (#53): morphological blending (notice and changes in AaMlaa.hlsli).
#include "AaMlaa.hlsli"
Texture2D<float4> source : register(t0);    // linear working colour
Texture2D<float> luma : register(t1);       // AaLuma output
Texture2D<float4> edgeCounts : register(t2);
RWTexture2D<float4> target : register(u0);

float4 LoadColor(int2 p) { return source.Load(int3(AaClamp(p), 0)); }
float LoadLuma(int2 p) { return luma.Load(int3(AaClamp(p), 0)); }

uint DecodeRaw(float value) {
    return (uint)round(saturate(value) * 15.0);
}

bool ColorsDiffer(int2 a, int2 b) {
    return abs(LoadLuma(a) - LoadLuma(b)) > aaThreshold;
}

void BlendEdge(float2 encodedCounts, int2 edgePos, int2 acrossEdge,
    int2 alongEdge, bool inverse, inout float4 color) {
    const uint rawNegative = DecodeRaw(encodedCounts.x);
    const uint rawPositive = DecodeRaw(encodedCounts.y);
    const bool negativeEnd = rawNegative >= 8;
    const bool positiveEnd = rawPositive >= 8;
    if (!negativeEnd && !positiveEnd) return;
    uint negativeCount = rawNegative & 7;
    uint positiveCount = rawPositive & 7;
    const float4 adjacent = LoadColor(edgePos + acrossEdge);
    if (negativeCount + positiveCount == 0) {
        const float weight = 0.125 * aaStrength;
        color.rgb = lerp(color.rgb, adjacent.rgb, weight);
        return;
    }
    if (!positiveEnd) positiveCount = MAX_EDGE_LENGTH + 1;
    if (!negativeEnd) negativeCount = MAX_EDGE_LENGTH + 1;
    const float length = negativeCount + positiveCount + 1.0;
    const float midpoint = length * 0.5;
    const float distance = negativeCount;
    uint shape = 0;
    if (ColorsDiffer(edgePos - alongEdge * int(negativeCount),
        edgePos - alongEdge * int(negativeCount + 1))) shape |= 1;
    if (ColorsDiffer(edgePos + alongEdge * int(positiveCount),
        edgePos + alongEdge * int(positiveCount + 1))) shape |= 2;
    const bool shouldBlend =
        (inverse && ((shape == 2 && float(negativeCount) <= midpoint) ||
            (shape == 1 && float(negativeCount) >= midpoint) || shape == 0)) ||
        (!inverse && ((shape == 2 && float(negativeCount) >= midpoint) ||
            (shape == 1 && float(negativeCount) <= midpoint) || shape == 3));
    if (shouldBlend) {
        const float h0 = abs((length - distance) / length - 0.5);
        const float h1 = abs((length - distance - 1.0) / length - 0.5);
        const float area = 0.5 * (h0 + h1) * aaStrength;
        color.rgb = lerp(color.rgb, adjacent.rgb, area);
    }
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    float4 color = LoadColor(p);

    const float4 current = edgeCounts.Load(int3(p, 0));
    const int2 below = AaClamp(p + int2(0, 1));
    const int2 left = AaClamp(p + int2(-1, 0));
    const float2 belowHorizontal = edgeCounts.Load(int3(below, 0)).xy;
    const float2 leftVertical = edgeCounts.Load(int3(left, 0)).zw;

    if (any(current.xy > 0)) {
        BlendEdge(current.xy, p, int2(0, -1), int2(1, 0), false, color);
    }
    if (any(belowHorizontal > 0)) {
        BlendEdge(belowHorizontal, below, int2(0, 1), int2(1, 0), true, color);
    }
    if (any(current.zw > 0)) {
        BlendEdge(current.zw, p, int2(1, 0), int2(0, -1), false, color);
    }
    if (any(leftVertical > 0)) {
        BlendEdge(leftVertical, left, int2(-1, 0), int2(0, -1), true, color);
    }

    target[id.xy] = color;
}
