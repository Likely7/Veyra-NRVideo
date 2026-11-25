// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS shaders/PushHudLockCompose.hlsl (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
// Live HUD Lock, step 2 (pipeline/HudLock.h): a generated frame takes its locked
// pixels from the held real frame. A pixel is locked when it has been still for about a second
// itself and it, or a neighbour that is just as still, has an edge: the inside of a thin stroke
// and the pixel beside an edge belong to the overlay too. A pixel that moves is never locked.
Texture2D<float4> generatedTex : register(t0);
Texture2D<float4> heldTex : register(t1);
Texture2D<float2> stateTex : register(t2);
Texture2D<float> dynamicMask : register(t3);
Texture2D<float4> newerTex : register(t4);
RWTexture2D<float4> outputTex : register(u0);
cbuffer Constants : register(b0) {
    uint width, height, outputMode, lockStatic;
    uint dynamicCell, useNewer, pad0, pad1;
};
#include "HudLockWeight.hlsli"
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float4 g = generatedTex[id.xy];
    if (dynamicCell != 0) {
        if (dynamicMask[id.xy / dynamicCell] > 0.5) {
            const float3 hud = useNewer != 0 ? newerTex[id.xy].rgb : heldTex[id.xy].rgb;
            outputTex[id.xy] = float4(hud, g.a);
            return;
        }
    }
    if (lockStatic == 0) {
        outputTex[id.xy] = g;
        return;
    }
    const float w = hudStaticWeight(stateTex,id.xy,width,height,outputMode);
    outputTex[id.xy] = w > 0 ? float4(lerp(g.rgb, heldTex[id.xy].rgb, w), g.a) : g;
}
