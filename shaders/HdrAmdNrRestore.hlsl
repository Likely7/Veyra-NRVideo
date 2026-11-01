// Model output is SDR linear FP16. Restore only its bounded delta over the
// original HDR base, preserving highlights, negative gamut components and alpha.
#include "HdrColor.hlsli"
cbuffer Dimensions : register(b0) { uint width; uint height; uint2 unused0; uint4 unused1; };
Texture2D<float4> original : register(t0);
Texture2D<float4> proxy : register(t1);
Texture2D<float4> enhanced : register(t2);
RWTexture2D<float4> restored : register(u0);
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if(p.x>=width || p.y>=height)return;
    float4 base=original[p.xy];
    restored[p.xy]=float4(HdrRestore(base.rgb,proxy[p.xy].rgb,enhanced[p.xy].rgb),base.a);
}
