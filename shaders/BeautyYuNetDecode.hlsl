// YuNet head decode for one stride. The merged head tensor holds 16 channels per
// anchor: 0 cls, 1 obj, 2..5 box, 6..15 five landmarks (x,y pairs). Anchors whose
// sqrt(sigmoid(cls) * sigmoid(obj)) reaches the threshold are appended to a
// bounded candidate list (atomic counter in the first 16 bytes, then 64-byte
// records). The face tracker runs NMS on the GPU (BeautyTrack.hlsl); with
// readback (the detector test) the host reads the list back and runs it.
//   arg0 stride | arg1 capacity | arg2 threshold (float bits) | arg3 scale index
// Record: x, y, w, h, 5 x (lx, ly), score, id (scale << 24 | anchor).
#include "BeautyYuNet.hlsli"
RWStructuredBuffer<float4> Head : register(u0);
RWByteAddressBuffer Candidates : register(u2);

float sigmoid(float x) { return 1.0 / (1.0 + exp(-x)); }

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint total = outW * outH;
    if (id.x >= total) return;
    const uint pixel = id.x;
    const float4 a = Head[pixel * 4 + 0];
    const float score = sqrt(saturate(sigmoid(a.x)) * saturate(sigmoid(a.y)));
    if (!(score >= asfloat(arg2))) return;
    const float4 b = Head[pixel * 4 + 1];
    const float4 c = Head[pixel * 4 + 2];
    const float4 d = Head[pixel * 4 + 3];
    const float stride = float(arg0);
    const float col = float(pixel % outW), row = float(pixel / outW);
    const float cx = (col + a.z) * stride, cy = (row + a.w) * stride;
    const float w = exp(b.x) * stride, h = exp(b.y) * stride;
    uint slot;
    Candidates.InterlockedAdd(0, 1, slot);
    if (slot >= arg1) return;
    const uint o = 16 + slot * 64;
    Candidates.Store4(o,      asuint(float4(cx - w * 0.5, cy - h * 0.5, w, h)));
    Candidates.Store4(o + 16, asuint(float4((b.z + col) * stride, (b.w + row) * stride, (c.x + col) * stride, (c.y + row) * stride)));
    Candidates.Store4(o + 32, asuint(float4((c.z + col) * stride, (c.w + row) * stride, (d.x + col) * stride, (d.y + row) * stride)));
    Candidates.Store4(o + 48, uint4(asuint((d.z + col) * stride), asuint((d.w + row) * stride), asuint(score), (arg3 << 24) | pixel));
}
