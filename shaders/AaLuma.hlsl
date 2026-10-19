// Anti-aliasing pass 0 (#53): perceptual luma of the linear working colour.
// Every method detects edges on this texture only (see AntiAlias.hlsli).
#include "AntiAlias.hlsli"
Texture2D<float4> source : register(t0);
RWTexture2D<float> luma : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= aaWidth || p.y >= aaHeight) return;
    luma[p.xy] = AaLumaOf(source[p.xy].rgb);
}
