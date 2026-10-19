// MLAA pass 1 (#53): edge detection (notice and changes in AaMlaa.hlsli).
#include "AaMlaa.hlsli"
Texture2D<float> luma : register(t0);
RWTexture2D<float2> edgeMask : register(u0);

float LoadLuma(int2 p) { return luma.Load(int3(AaClamp(p), 0)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    const float center = LoadLuma(p);
    const float upper = LoadLuma(p + int2(0, -1));
    const float right = LoadLuma(p + int2(1, 0));

    // R: edge above this pixel. G: edge to the right of this pixel.
    edgeMask[id.xy] = float2(abs(center - upper) > aaThreshold ? 1.0 : 0.0,
                             abs(center - right) > aaThreshold ? 1.0 : 0.0);
}
