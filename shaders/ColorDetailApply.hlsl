// Colour effects, step 3 of 3, Lightroom's 效果 group:
//   纹理 texture  - fine detail: log luminance against an 8-tap ring a few pixels wide;
//   清晰度 clarity - midtone local contrast against the blurred block base;
//   去朦胧 dehaze  - dark-channel haze removal (positive) or a uniform veil (negative).
// Detail is changed as a luminance ratio, so hue and saturation of each pixel stay;
// the step is limited to 1.5 stops so a hard edge cannot ring into a black or
// white halo. Values are scene-referred (1.0 = SDR white) inside the shader.
cbuffer Params : register(b0) {
    uint width; uint height; uint lowWidth; uint lowHeight;
    float block; float workingToScene; float textureAmount; float clarityAmount;
    float dehazeAmount; float ringRadius; float pad0; float pad1;
};
Texture2D<float4> source : register(t0);
Texture2D<float4> low : register(t1);
RWTexture2D<float4> target : register(u0);

float logLuma(float3 c) {
    return log2(max(dot(max(c, 0.0), float3(0.2126, 0.7152, 0.0722)), 1e-4));
}

float4 lowAt(uint2 p) {
    // Bilinear between the four block centres around this pixel.
    const float2 g = (float2(p) + 0.5) / block - 0.5;
    const float2 f = frac(g);
    const int2 i0 = int2(floor(g));
    const int2 hi = int2(lowWidth - 1, lowHeight - 1);
    const float4 a = low[clamp(i0, int2(0, 0), hi)];
    const float4 b = low[clamp(i0 + int2(1, 0), int2(0, 0), hi)];
    const float4 c = low[clamp(i0 + int2(0, 1), int2(0, 0), hi)];
    const float4 d = low[clamp(i0 + int2(1, 1), int2(0, 0), hi)];
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float4 original = source[id.xy];
    float3 c = original.rgb * workingToScene;
    const float lum = logLuma(c);
    float delta = 0.0;

    if (textureAmount != 0.0) {
        const int r = int(ringRadius);
        const int2 taps[8] = {int2(-r, 0), int2(r, 0), int2(0, -r), int2(0, r),
                              int2(-r, -r), int2(r, r), int2(-r, r), int2(r, -r)};
        const int2 hi = int2(width - 1, height - 1);
        float ring = 0.0;
        [unroll] for (int k = 0; k < 8; ++k)
            ring += logLuma(source[clamp(int2(id.xy) + taps[k], int2(0, 0), hi)].rgb * workingToScene);
        delta += textureAmount * 0.9 * (lum - ring / 8.0);
    }
    const float4 base = lowAt(id.xy);
    if (clarityAmount != 0.0) {
        // Midtones carry clarity; deep shadows and highlights are left alone.
        const float m = saturate((lum + 8.0) / 9.0);
        const float midtone = saturate(1.0 - (2.0 * m - 1.0) * (2.0 * m - 1.0));
        delta += clarityAmount * 0.7 * (lum - base.x) * midtone;
    }
    c *= exp2(clamp(delta, -1.5, 1.5));

    if (dehazeAmount > 0.0) {
        // Remove a veil v against airlight at diffuse white: (c - v) / (1 - v).
        // v comes from the blurred dark channel (the haze), plus a small constant so
        // a clear picture still gains some depth. It is capped at half of this
        // pixel's own darkest channel: a dark pixel beside a bright region (the
        // block minimum there is high) cannot be crushed into a black halo.
        const float own = min(c.r, min(c.g, c.b));
        const float v = dehazeAmount * min(0.9 * saturate(base.y) + 0.03, 0.5 * max(own, 0.0));
        // Values above diffuse white (HDR highlights) keep their excess unchanged.
        const float3 clipped = min(c, 1.0);
        c = (clipped - v) / (1.0 - v) + (c - clipped);
    } else if (dehazeAmount < 0.0) {
        // A flat veil at a fifth of diffuse white. Linear light, so at -100 black
        // lifts to roughly a quarter of the display range.
        const float t = 1.0 + 0.3 * dehazeAmount;
        c = c * t + 0.2 * (1.0 - t);
    }
    target[id.xy] = float4(max(c, 0.0) / workingToScene, original.a);
}
