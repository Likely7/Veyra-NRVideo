// Shared declarations for the YuNet face detector kernels.
//
// Tensors are float4 buffers, channel-last: element index = pixel * quads + quad,
// quad k holding channels 4k..4k+3. Weights and biases of every layer live in one
// float4 buffer; the host writes the offsets (in float4 units) into the root
// constants. Layouts (host side packing is in YuNetGpu.cpp):
//   pointwise  : W[wOff + g * (inQuads * 4) + c]  = (w[4g+0][c], .., w[4g+3][c])
//   depthwise  : W[wOff + q * 9 + (ky * 3 + kx)]  = (w[4q+0], .., w[4q+3]) of that tap
//   stem 3x3/s2: W[wOff + (q * 9 + tap) * 3 + c]  = (w[4q+0][c][tap], .., w[4q+3][c][tap])
//   bias       : W[bOff + q]                       = (b[4q+0], .., b[4q+3])
cbuffer Params : register(b0) {
    uint inW; uint inH; uint inQ;
    uint outW; uint outH; uint outQ;
    uint wOff; uint bOff;
    uint flags;                 // bit 0: ReLU on the result
    uint arg0; uint arg1; uint arg2; uint arg3; uint arg4; uint arg5; uint arg6;
};
StructuredBuffer<float4> Weights : register(t0);

static const uint kFlagRelu = 1u;

float4 finishLayer(float4 value) {
    return (flags & kFlagRelu) ? max(value, 0.0) : value;
}
