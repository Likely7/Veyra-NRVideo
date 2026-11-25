// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS shaders/PushHudLockCompose.hlsl (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
float stillOf(float2 s) { return smoothstep(0.80, 0.95, s.x); }
float edgeOf(float2 s, uint outputMode) {
    const float edge = outputMode == 1 ? 0.03 : 0.04;
    return smoothstep(edge, edge * 2.5, s.y);
}
float hudStaticWeight(Texture2D<float2> stateTex, uint2 pixel, uint width, uint height, uint outputMode) {
    const float still = stillOf(stateTex[pixel]);
    float edge = 0;
    if (still > 0) {
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x) {
                const float2 q = stateTex[clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(width - 1, height - 1))];
                edge = max(edge, edgeOf(q, outputMode) * stillOf(q));
            }
    }
    return still * edge;
}
