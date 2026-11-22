#include "HudProtect.hlsli"

Texture2D<float4> maskTex : register(t0);
Texture2D<float4> motionTex : register(t1);
RWTexture2D<float2> flowTex : register(u0);

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize)||maskTex.Load(int3(id.xy,0)).g==0)return;
    const int2 p=int2(id.xy+roiOrigin);
    const int region=hudRegion(p);
    if(region<0)return;
    const float4 motion=motionTex.Load(int3(region,0,0));
    if(motion.z>0)flowTex[p]=motion.xy;
}
