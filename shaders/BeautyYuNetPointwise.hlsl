// 1x1 convolution (groups = 1) over a channel-last quad tensor.
#include "BeautyYuNet.hlsli"
RWStructuredBuffer<float4> Input : register(u0);
RWStructuredBuffer<float4> Out : register(u2);

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH * outQ;
    if (id.x >= total) return;
    const uint g = id.x % outQ, pixel = id.x / outQ;
    float4 acc = Weights[bOff + g];
    const uint weightBase = wOff + g * inQ * 4;
    for (uint q = 0; q < inQ; ++q) {
        const float4 v = Input[pixel * inQ + q];
        const uint base = weightBase + q * 4;
        acc += Weights[base] * v.x + Weights[base + 1] * v.y + Weights[base + 2] * v.z + Weights[base + 3] * v.w;
    }
    Out[id.x] = finishLayer(acc);
}
