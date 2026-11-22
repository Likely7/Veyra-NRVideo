// SPDX-License-Identifier: GPL-3.0-or-later
// A small screen-space mask for changing HUD contents inside stable rails.
// The full-resolution stillness pass supplies the temporal evidence. A rail
// must be a thin, continuous line with a stable inset on the inside.
Texture2D<float4> currentTex : register(t0);
Texture2D<float2> stateTex : register(t1);
Texture2D<float4> previousTex : register(t2);
RWTexture2D<float> maskTex : register(u0);
cbuffer Constants : register(b0) {
    uint width, height, reset, outputMode;
    uint cellSize, maskWidth, maskHeight, pad;
};

float hudLuma(float3 rgb) {
    if (outputMode == 1) rgb = sqrt(max(rgb, 0) / 2.5);
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}
float lumaAt(int2 p) { return hudLuma(currentTex[p].rgb); }

struct Rail {
    int distance;
    float luminance;
    float inset;
};

Rail findRail(int2 p, int2 outward, int2 along, int radius, int probe, float edge) {
    Rail result = {0, 0, 0};
    const int2 limit = int2(width, height);
    const int2 extent = abs(outward) * (probe + 1) + abs(along) * int(cellSize);
    [loop] for (int i = int(cellSize / 2) + 1; i <= radius; ++i) {
        const int2 q = p + outward * i;
        if (any(q - extent < 0) || any(q + extent >= limit)) break;
        const float2 center = stateTex[q];
        if (center.x < 0.88 || center.y < edge) continue;

        const float luminance = lumaAt(q);
        const float sideA = luminance - lumaAt(q + outward * probe);
        const float sideB = luminance - lumaAt(q - outward * probe);
        if (sideA * sideB <= 0 || min(abs(sideA), abs(sideB)) < edge) continue;

        const int2 span = along * int(cellSize);
        const float2 left = stateTex[q - span];
        const float2 right = stateTex[q + span];
        if (min(left.x, right.x) < 0.88 || min(left.y, right.y) < edge ||
            max(abs(lumaAt(q - span) - luminance), abs(lumaAt(q + span) - luminance)) > edge * 0.5)
            continue;

        const int2 inset = q - outward * (probe + 1);
        if (stateTex[inset].x < 0.88) continue;
        result.distance = i;
        result.luminance = luminance;
        result.inset = lumaAt(inset);
        return result;
    }
    return result;
}

// A line-shaped scene element may have the same rails. Replace only cells
// where a foreground change exposes or covers the stable inset colour.
bool changesOverInset(int2 p, float inset, float edge) {
    const float a = hudLuma(previousTex[p].rgb);
    const float b = lumaAt(p);
    return abs(a - b) >= edge * 0.75 && min(abs(a - inset), abs(b - inset)) <= edge * 0.35;
}

bool shouldProtect(int2 p, int2 normal, int2 along, int radius, int probe, float edge) {
    const Rail a = findRail(p, -normal, along, radius, probe, edge);
    if (a.distance == 0) return false;
    const Rail b = findRail(p, normal, along, radius, probe, edge);
    if (b.distance == 0 || a.distance + b.distance < probe * 4 + 2) return false;
    if (abs(a.luminance - b.luminance) > edge * 0.5 ||
        abs(a.inset - b.inset) > edge * 0.5 ||
        abs(a.luminance - a.inset) < edge) return false;

    const float inset = (a.inset + b.inset) * 0.5;
    const int offsets[3] = {-int(cellSize / 2), 0, int((cellSize - 1) / 2)};
    // Corners keep one-pixel strokes at cell boundaries from slipping through.
    [unroll] for (int v = 0; v < 3; ++v) {
        [unroll] for (int u = 0; u < 3; ++u) {
            if (changesOverInset(p + normal * offsets[v] + along * offsets[u], inset, edge))
                return true;
        }
    }
    return false;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= maskWidth || id.y >= maskHeight) return;
    if (reset != 0) {
        maskTex[id.xy] = 0;
        return;
    }
    const int2 center = min(int2(id.xy * cellSize + cellSize / 2), int2(width, height) - 1);
    const int radius = int(cellSize) * 5;
    const int probe = max(1, int(cellSize) / 2);
    const float edge = outputMode == 0 ? 0.12 : 0.08;
    // Record the rail orientation so the next pass bridges gaps only along it.
    maskTex[id.xy] = shouldProtect(center, int2(0, 1), int2(1, 0), radius, probe, edge) ? 1.0 :
                     shouldProtect(center, int2(1, 0), int2(0, 1), radius, probe, edge) ? 0.5 : 0.0;
}
