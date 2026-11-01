// Native HDR keeps its signed scRGB base; lmxxf's linear SDR ABI sees only
// the same fixed-reference proxy used by Veyra's existing HDR NR contract.
#include "HdrColor.hlsli"
cbuffer Dimensions : register(b0) { uint width; uint height; uint2 unused0; uint4 unused1; };
Texture2D<float4> original : register(t0);
RWTexture2D<float4> proxy : register(u0);
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if(p.x>=width || p.y>=height)return;
    float4 base=original[p.xy];
    proxy[p.xy]=float4(HdrProxy(base.rgb),base.a);
}
