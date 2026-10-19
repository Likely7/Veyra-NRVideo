// Face mesh: depthwise 3 x 3 convolution (stride 1 or 2, TensorFlow SAME padding) with bias.
#include "BeautyMesh.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint face = id.z;
    if (face >= meshFaces() || id.x >= outW * outH * outQ) return;
    const uint q = id.x % outQ, pixel = id.x / outQ, x = pixel % outW, y = pixel / outW;
    const uint stride = (convInfo >> 8) & 0xffu;
    const int padTop = int((convInfo >> 16) & 0xffu), padLeft = int(convInfo >> 24);
    float4 acc = Weights[bOff + q];
    [unroll] for (uint ky = 0; ky < 3; ++ky) {
        const int sy = int(y * stride + ky) - padTop;
        [unroll] for (uint kx = 0; kx < 3; ++kx) {
            const int sx = int(x * stride + kx) - padLeft;
            if (sx >= 0 && sy >= 0 && sx < int(inW) && sy < int(inH))
                acc += Weights[wOff + q * 9 + ky * 3 + kx] * Scratch[tensorAt(inOff, face, uint(sx), uint(sy), inW, inH, inQ) + q];
        }
    }
    Scratch[tensorAt(outOff, face, x, y, outW, outH, outQ) + q] = meshActivation(acc, q);
}
