// Neck merge: nearest-neighbour 2x upsample of the coarser map (u0) added to the
// skip connection (u1). ONNX Resize(nearest, asymmetric, floor) == source index x / 2.
#include "BeautyYuNet.hlsli"
RWStructuredBuffer<float4> Coarse : register(u0);
RWStructuredBuffer<float4> Skip : register(u1);
RWStructuredBuffer<float4> Out : register(u2);

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH * outQ;
    if (id.x >= total) return;
    const uint q = id.x % outQ, pixel = id.x / outQ;
    const uint x = pixel % outW, y = pixel / outW;
    Out[id.x] = Skip[id.x] + Coarse[((y >> 1) * inW + (x >> 1)) * inQ + q];
}
