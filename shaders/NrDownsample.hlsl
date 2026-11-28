Texture2D<float4> linearTex : register(t0);
RWTexture2D<float4> outputTex : register(u0);
cbuffer Dims : register(b0) {
    uint sourceW, sourceH, outW, outH;
    float hdr;
    uint tileW, tileH, unused;
}

float4 ReadLinear(uint2 p) { return linearTex[p]; }
#include "Lanczos2Reduce.hlsli"
