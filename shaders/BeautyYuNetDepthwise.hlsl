// 3x3 depthwise convolution, stride 1, padding 1, one 3x3 kernel per channel.
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
            const int ix = x + kx - 1, iy = y + ky - 1;
            if (ix < 0 || iy < 0 || ix >= int(inW) || iy >= int(inH)) continue;
            acc += Weights[wOff + q * 9 + uint(ky * 3 + kx)] * Input[(uint(iy) * inW + uint(ix)) * inQ + q];
        }
    }
    Out[id.x] = finishLayer(acc);
}
