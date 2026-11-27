// SPDX-License-Identifier: GPL-3.0-only
Texture2D<float4> sourceImage : register(t0);
RWBuffer<uint> resultImage : register(u0);
SamplerState linearClamp : register(s1);

cbuffer Params : register(b0)
{
    float hdrMode;
};

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x >= 64 || thread.y >= 144) return;
    uint pixel = thread.x * 4;
    uint packed = 0;
    for (uint i = 0; i < 4; ++i)
    {
        float2 uv = (float2(pixel + i, thread.y) + 0.5) / float2(256, 144);
        float3 rgb = sourceImage.SampleLevel(linearClamp, uv, 0).rgb;
        float y = max(0, dot(rgb, hdrMode > 1.5 ? float3(.2627, .6780, .0593) : float3(.2126, .7152, .0722)));
        if (hdrMode == 1) y = y / (1 + y);
        packed |= (uint)round(saturate(y) * 255.0) << (i * 8);
    }
    resultImage[thread.y * 64 + thread.x] = packed;
}
