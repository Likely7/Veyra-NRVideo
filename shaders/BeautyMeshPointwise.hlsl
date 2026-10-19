// Face mesh: 1 x 1 convolution with bias, then the block's residual and PReLU, fused:
//   mode 1: + the block input (same size and channels)
//   mode 2: + the block input max-pooled 2 x 2 (it has twice the size); channels beyond
//           its own are zero (the TFLite PAD that widens the shortcut)
#include "BeautyMesh.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint face = id.z;
    if (face >= meshFaces() || id.x >= outW * outH * outQ) return;
    const uint q = id.x % outQ, pixel = id.x / outQ, x = pixel % outW, y = pixel / outW;
    float4 acc = Weights[bOff + q];
    const uint input = tensorAt(inOff, face, x, y, inW, inH, inQ);
    const uint w = wOff + q * inQ * 4;
    for (uint c = 0; c < inQ; ++c) {
        const float4 v = Scratch[input + c];
        const uint b = w + c * 4;
        acc += Weights[b] * v.x + Weights[b + 1] * v.y + Weights[b + 2] * v.z + Weights[b + 3] * v.w;
    }
    const uint mode = resInfo >> 16, resQ = resInfo & 0xffffu;
    if (mode == 1u) {
        acc += Scratch[tensorAt(resOff, face, x, y, outW, outH, resQ) + q];
    } else if (mode == 2u && q < resQ) {
        const uint rw = outW * 2, rh = outH * 2;
        const uint r = tensorAt(resOff, face, x * 2, y * 2, rw, rh, resQ) + q;
        acc += max(max(Scratch[r], Scratch[r + resQ]), max(Scratch[r + rw * resQ], Scratch[r + (rw + 1) * resQ]));
    }
    Scratch[tensorAt(outOff, face, x, y, outW, outH, outQ) + q] = meshActivation(acc, q);
}
