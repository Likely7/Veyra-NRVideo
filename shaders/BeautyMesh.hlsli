// Shared declarations for the face mesh kernels (src/beauty/FaceMeshGpu.cpp): the
// MediaPipe face landmark network (468 points) on a 192 x 192 crop of every tracked face.
//
// Tensors live in one float4 scratch buffer, channel-last with the face slot outermost:
// element = offset + ((face * height + y) * width + x) * quads + quad, quad k holding
// channels 4k..4k+3. Weights, biases and PReLU slopes of every layer live in one float4
// buffer (offsets in float4 units); the host packs them as
//   convolution k x k: W[wOff + ((q * k + ky) * k + kx) * inQ * 4 + c] = (w[4q+0][ky][kx][c], .., w[4q+3][ky][kx][c])
//   depthwise 3 x 3  : W[wOff + q * 9 + ky * 3 + kx]                    = (w[ky][kx][4q+0], .., w[ky][kx][4q+3])
//   bias, slope      : W[bOff + q], W[aOff + q]
#ifndef VEYRA_BEAUTY_MESH_HLSLI
#define VEYRA_BEAUTY_MESH_HLSLI
#include "BeautyFaces.hlsli"

cbuffer MeshParams : register(b0) {
    uint inW; uint inH; uint inQ;
    uint outW; uint outH; uint outQ;
    uint wOff; uint bOff; uint aOff;        // aOff: PReLU slopes, kMeshNone without
    uint inOff; uint outOff; uint resOff;   // tensor offsets, float4 units
    uint resInfo;                           // residual quads | mode << 16: 0 none, 1 the same-size tensor, 2 its 2x2 max-pool (channel padded)
    uint convInfo;                          // kernel | stride << 8 | pad top << 16 | pad left << 24
    uint capacity;                          // face slots to process (the max faces setting)
    uint forcedCount;                       // 0: the tracked face count (Faces header); else this many slots (tests)
};
StructuredBuffer<float4> Weights : register(t0);
StructuredBuffer<BeautyFace> Faces : register(t1);
RWStructuredBuffer<float4> Scratch : register(u0);

static const uint kMeshNone = 0xffffffffu;

uint meshFaces() {
    const uint tracked = min(Faces[0].meta.x, kBeautyMaxFaces);
    return min(forcedCount != 0u ? forcedCount : tracked, capacity);
}
uint tensorAt(uint offset, uint face, uint x, uint y, uint w, uint h, uint quads) {
    return offset + ((face * h + y) * w + x) * quads;
}
float4 meshActivation(float4 v, uint q) {
    if (aOff == kMeshNone) return v;
    const float4 slope = Weights[aOff + q];
    return select(v >= 0.0, v, v * slope);
}
#endif
