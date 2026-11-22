// SPDX-License-Identifier: GPL-3.0-only
// RIFE (RifeDmlBackend) input: the enhanced colour frame -> planar float RGB in
// [0, 1], NCHW, padded to the model size by repeating the last row/column.
// mode 1 fills one plane with a constant instead (the timestep input).

cbuffer Constants : register(b0)
{
    uint width;        // colour frame
    uint height;
    uint paddedWidth;  // model tensor
    uint paddedHeight;
    uint mode;         // 0 colour -> planes, 1 fill
    float value;       // mode 1
    uint planeCount;   // mode 1
    uint pad0;
};

Texture2D<float4> color : register(t0);
RWByteAddressBuffer planes : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= paddedWidth || id.y >= paddedHeight) return;
    const uint plane = paddedWidth * paddedHeight;
    const uint index = id.y * paddedWidth + id.x;
    if (mode == 1) {
        for (uint c = 0; c < planeCount; ++c) planes.Store((c * plane + index) * 4, asuint(value));
        return;
    }
    const float3 rgb = saturate(color.Load(int3(min(id.x, width - 1), min(id.y, height - 1), 0)).rgb);
    planes.Store((0 * plane + index) * 4, asuint(rgb.r));
    planes.Store((1 * plane + index) * 4, asuint(rgb.g));
    planes.Store((2 * plane + index) * 4, asuint(rgb.b));
}
