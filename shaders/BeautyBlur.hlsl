// Beauty node, smoothing steps 2-5: separable Gaussian on the statistics grid,
// restricted to this frame's region (taps clamp to it, so cells outside it are
// never read). bPass 0 / 1: horizontal / vertical blur of the statistics; pass 1
// also turns the local mean and variance into the guided-filter coefficients
// (a from the luma variance against bEpsilon, b = (1 - a) * mean per channel).
// bPass 2 / 3: horizontal / vertical blur of those coefficients.
#include "Beauty.hlsli"

Texture2D<float4> Source : register(t0);
ByteAddressBuffer Roi : register(t3);
RWTexture2D<float4> Target : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 lowExtent = Roi.Load2(kRoiLowSize);
    if (any(id.xy >= lowExtent)) return;
    const int2 origin = int2(Roi.Load2(kRoiLowOrigin));
    const int2 cell = origin + int2(id.xy);
    const int2 axis = (bPass & 1u) ? int2(0, 1) : int2(1, 0);
    const int2 lo = origin, hi = origin + int2(lowExtent) - 1;
    const float sigma = max(bSigma, 0.35);
    const int radius = int(min(ceil(3.0 * sigma), 12.0));
    float4 sum = 0.0;
    float weights = 0.0;
    for (int k = -radius; k <= radius; ++k) {
        const float w = exp(-0.5 * float(k * k) / (sigma * sigma));
        sum += w * Source.Load(int3(clamp(cell + axis * k, lo, hi), 0));
        weights += w;
    }
    float4 value = sum / weights;
    if (bPass == 1u) {
        const float meanY = BeautyLuma(value.rgb);
        const float variance = max(value.a - meanY * meanY, 0.0);
        const float a = variance / (variance + bEpsilon);
        value = float4(value.rgb * (1.0 - a), a);
    }
    Target[cell] = value;
}
