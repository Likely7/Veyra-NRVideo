// YuNet stem: 3x3 convolution, stride 2, padding 1, 3 -> 16 channels, then ReLU.
#include "BeautyYuNet.hlsli"
RWStructuredBuffer<float4> Input : register(u0);
RWStructuredBuffer<float4> Out : register(u2);

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH * outQ;
    if (id.x >= total) return;
    const uint q = id.x % outQ, pixel = id.x / outQ;
    const int x = int(pixel % outW), y = int(pixel / outW);
    float4 acc = Weights[bOff + q];
    [unroll] for (int ky = 0; ky < 3; ++ky) {
        [unroll] for (int kx = 0; kx < 3; ++kx) {
            const int ix = x * 2 + kx - 1, iy = y * 2 + ky - 1;
            if (ix < 0 || iy < 0 || ix >= int(inW) || iy >= int(inH)) continue;
            const float4 v = Input[uint(iy) * inW + uint(ix)];
            const uint base = wOff + (q * 9 + uint(ky * 3 + kx)) * 3;
            acc += Weights[base] * v.x + Weights[base + 1] * v.y + Weights[base + 2] * v.z;
        }
    }
    Out[id.x] = finishLayer(acc);
}
