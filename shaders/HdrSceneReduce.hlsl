// Custom: reduce the decoded luma plane to a 64x36 grid so the CPU can build the
// per-scene histogram for HDR brightness management.
//
// On the hardware-decode path the frame never reaches the CPU, so this is the
// only way to measure a scene without a full-frame readback. One thread per
// output cell, striding over its cell rather than averaging every sample: the
// measurement is percentile based, so a few dozen samples per cell are plenty.
//
// The SRV is the source luma plane (R8_UNORM for 8-bit, R16_UNORM for P010/P016
// where the sampled value is the high byte of the container). In both cases the
// normalised value IS the 8-bit luma proxy the software-decode path already
// feeds the same histogram, so the CPU side needs no special case.
Texture2D<float> sourceLuma : register(t0);
RWStructuredBuffer<float> reducedLuma : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint outW = 64u, outH = 36u;
    if (id.x >= outW || id.y >= outH) return;
    uint width = 0u, height = 0u;
    sourceLuma.GetDimensions(width, height);
    if (width == 0u || height == 0u) { reducedLuma[id.y * outW + id.x] = 0.0; return; }
    const uint x0 = id.x * width / outW, x1 = max(x0 + 1u, (id.x + 1u) * width / outW);
    const uint y0 = id.y * height / outH, y1 = max(y0 + 1u, (id.y + 1u) * height / outH);
    const uint stepX = max(1u, (x1 - x0) / 8u), stepY = max(1u, (y1 - y0) / 8u);
    float sum = 0.0; uint count = 0u;
    for (uint y = y0; y < y1; y += stepY) {
        for (uint x = x0; x < x1; x += stepX) {
            sum += sourceLuma.Load(int3(int(x), int(y), 0));
            ++count;
        }
    }
    reducedLuma[id.y * outW + id.x] = count ? sum / float(count) : 0.0;
}
