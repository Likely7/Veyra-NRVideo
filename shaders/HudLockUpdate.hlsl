// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS shaders/PushHudLockUpdate.hlsl (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
// Live HUD Lock, step 1 (pipeline/HudLock.h): per pixel, how long it has stayed the
// same (x, 0..1) and how much contrast it has with its neighbours (y, a held maximum that fades).
Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
RWTexture2D<float2> stateTex : register(u0);
cbuffer Constants : register(b0) {
    uint width, height, reset, outputMode; // 0 sRGB, 1 scRGB (linear), 2 PQ
    uint pad0, pad1, pad2, pad3;
};
// A roughly perceptual value: sRGB and PQ are already encoded; scRGB is linear (1 = 80 nits).
float3 perceptual(float3 v) { return outputMode == 1 ? sqrt(max(v, 0) / 2.5) : v; }
float luma(float3 v) { return dot(v, float3(0.2126, 0.7152, 0.0722)); }
float3 at(int2 p) { return perceptual(currentTex[clamp(p, int2(0, 0), int2(width - 1, height - 1))].rgb); }

// 1/0.06: about 38 frames (1.3 s at 30 FPS) to reach 0.9 from zero.
static const float kRise = 0.06;
static const float kStill = 0.02;   // largest channel change still counted as the same pixel
static const float kFade = 0.97;    // held contrast fades about 3 % per frame

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    if (reset != 0) { stateTex[id.xy] = float2(0, 0); return; }
    const int2 p = int2(id.xy);
    const float3 c = at(p);
    const float3 d = abs(c - perceptual(previousTex[id.xy].rgb));
    const float change = max(d.r, max(d.g, d.b));
    const float l = luma(c);
    const float contrast = max(max(abs(l - luma(at(p + int2(1, 0)))), abs(l - luma(at(p - int2(1, 0))))),
                               max(abs(l - luma(at(p + int2(0, 1)))), abs(l - luma(at(p - int2(0, 1))))));
    float2 s = stateTex[id.xy];
    s.x = change < kStill ? s.x + (1 - s.x) * kRise : 0;
    s.y = change < kStill ? max(contrast, s.y * kFade) : contrast;
    stateTex[id.xy] = s;
}
