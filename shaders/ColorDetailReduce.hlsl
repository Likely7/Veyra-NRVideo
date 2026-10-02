// Colour effects (texture / clarity / dehaze), step 1 of 3: one low-resolution
// texel per block of the graded image, holding the mean log2 luminance (the
// large-scale base clarity lifts detail against) and the block minimum of the
// dark channel (min of R, G, B): the dark-channel prior, near zero in a clear
// picture however dark it is, so dehaze leaves clear shadows alone. Scene-referred units.
cbuffer Params : register(b0) {
    uint srcWidth; uint srcHeight; uint lowWidth; uint lowHeight;
    uint block; float workingToScene; float pad0; float pad1;
};
Texture2D<float4> source : register(t0);
RWTexture2D<float4> low : register(u0);

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= lowWidth || id.y >= lowHeight) return;
    // A 4x4 grid inside the block is plenty for a base that is blurred afterwards.
    const uint step = max(1u, block / 4u);
    const uint2 origin = id.xy * block;
    float sumLog = 0.0, minDark = 1e9, count = 0.0;
    for (uint y = step / 2; y < block; y += step) {
        for (uint x = step / 2; x < block; x += step) {
            const uint2 p = min(origin + uint2(x, y), uint2(srcWidth - 1, srcHeight - 1));
            const float3 c = max(source[p].rgb * workingToScene, 0.0);
            sumLog += log2(max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-4));
            minDark = min(minDark, min(c.r, min(c.g, c.b)));
            count += 1.0;
        }
    }
    low[id.xy] = float4(sumLog / count, minDark, 0.0, 1.0);
}
