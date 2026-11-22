#include "HudProtect.hlsli"

Texture2D<float4> generatedTex : register(t0);
Texture2D<float4> heldTex : register(t1);
Texture2D<float> maskTex : register(t2);
RWTexture2D<float4> scratchTex : register(u0);

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const uint2 p=id.xy+roiOrigin;
    const float4 generated=generatedTex.Load(int3(p,0));
    scratchTex[id.xy]=maskTex.Load(int3(id.xy,0))>0
        ?float4(heldTex.Load(int3(p,0)).rgb,generated.a):generated;
}
