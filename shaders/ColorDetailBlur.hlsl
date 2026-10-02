// Colour effects, step 2 of 3: one direction of a separable Gaussian (sigma 2.5
// low-resolution texels) over the block statistics. Run horizontally, then
// vertically. At about 135 texel rows the base spans roughly 2-3 % of the
// picture height, the scale clarity works at.
cbuffer Params : register(b0) {
    uint width; uint height; int stepX; int stepY;
    float pad0; float pad1; float pad2; float pad3;
};
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    static const float weights[5] = {0.1974, 0.1747, 0.1210, 0.0656, 0.0278};
    float4 sum = source[id.xy] * weights[0];
    float total = weights[0];
    for (int i = 1; i < 5; ++i) {
        const int2 offset = int2(stepX, stepY) * i;
        const int2 a = clamp(int2(id.xy) + offset, int2(0, 0), int2(width - 1, height - 1));
        const int2 b = clamp(int2(id.xy) - offset, int2(0, 0), int2(width - 1, height - 1));
        sum += (source[a] + source[b]) * weights[i];
        total += 2.0 * weights[i];
    }
    target[id.xy] = sum / total;
}
