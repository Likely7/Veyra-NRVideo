// SPDX-License-Identifier: GPL-3.0-only
// RIFE (RifeDmlBackend) output: planar float RGB (NCHW, padded) -> the
// generated colour frame, cropped to the working size, alpha 1.

cbuffer Constants : register(b0)
{
    uint width;
    uint height;
    uint paddedWidth;
    uint paddedHeight;
    uint pad0, pad1, pad2, pad3;
};

// The planes stay in UNORDERED_ACCESS: DirectML writes them in that state.
RWByteAddressBuffer planes : register(u0);
RWTexture2D<float4> output : register(u1);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    const uint plane = paddedWidth * paddedHeight;
    const uint index = id.y * paddedWidth + id.x;
    const float r = asfloat(planes.Load((0 * plane + index) * 4));
    const float g = asfloat(planes.Load((1 * plane + index) * 4));
    const float b = asfloat(planes.Load((2 * plane + index) * 4));
    output[id.xy] = float4(saturate(float3(r, g, b)), 1.0);
}
