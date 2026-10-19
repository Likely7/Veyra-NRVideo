// Face mesh: k x k convolution (the 3 x 3 stride-2 stem, the 3 x 3 heads) with bias and
// an optional PReLU. TensorFlow SAME padding arrives as the top / left pad.
#include "BeautyMesh.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint face = id.z;
    if (face >= meshFaces() || id.x >= outW * outH * outQ) return;
    const uint q = id.x % outQ, pixel = id.x / outQ, x = pixel % outW, y = pixel / outW;
    const uint k = convInfo & 0xffu, stride = (convInfo >> 8) & 0xffu;
    const int padTop = int((convInfo >> 16) & 0xffu), padLeft = int(convInfo >> 24);
    float4 acc = Weights[bOff + q];
    for (uint ky = 0; ky < k; ++ky) {
        const int sy = int(y * stride + ky) - padTop;
        if (sy < 0 || sy >= int(inH)) continue;
        for (uint kx = 0; kx < k; ++kx) {
            const int sx = int(x * stride + kx) - padLeft;
            if (sx < 0 || sx >= int(inW)) continue;
            const uint input = tensorAt(inOff, face, uint(sx), uint(sy), inW, inH, inQ);
            const uint w = wOff + ((q * k + ky) * k + kx) * inQ * 4;
            for (uint c = 0; c < inQ; ++c) {
                const float4 v = Scratch[input + c];
                const uint b = w + c * 4;
                acc += Weights[b] * v.x + Weights[b + 1] * v.y + Weights[b + 2] * v.z + Weights[b + 3] * v.w;
            }
        }
    }
    Scratch[tensorAt(outOff, face, x, y, outW, outH, outQ) + q] = meshActivation(acc, q);
}
