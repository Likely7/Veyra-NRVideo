// 2x2 max pooling, stride 2.
#include "BeautyYuNet.hlsli"
RWStructuredBuffer<float4> Input : register(u0);
RWStructuredBuffer<float4> Out : register(u2);

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH * outQ;
    if (id.x >= total) return;
    const uint q = id.x % outQ, pixel = id.x / outQ;
    const uint x = (pixel % outW) * 2, y = (pixel / outW) * 2;
    float4 m = Input[(y * inW + x) * inQ + q];
    m = max(m, Input[(y * inW + min(x + 1, inW - 1)) * inQ + q]);
    m = max(m, Input[(min(y + 1, inH - 1) * inW + x) * inQ + q]);
    m = max(m, Input[(min(y + 1, inH - 1) * inW + min(x + 1, inW - 1)) * inQ + q]);
    Out[id.x] = m;
}
