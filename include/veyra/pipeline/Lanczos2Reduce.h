#pragma once

#include "veyra/pipeline/GpuPassUtils.h"
#include <algorithm>
#include <bit>

namespace veyra::pipeline {

// Tiling ABI of Lanczos2Reduce.hlsli. The footprint of N outputs is
// (N + 3) source/output pixels wide. Leave room for rounding at either edge.
inline void recordLanczos2Reduce(ID3D12GraphicsCommandList* list, const ComputePass& pass,
                                 uint32_t sourceW, uint32_t sourceH, uint32_t outW, uint32_t outH,
                                 bool hdr, UINT srv, UINT uav, bool srgbView = false)
{
    const auto tile = [](uint32_t source, uint32_t output, unsigned capacity) {
        return uint32_t(std::clamp(int(uint64_t(capacity - 1)*output/source) - 3, 1, 8));
    };
    const uint32_t tileW = tile(sourceW, outW, 48);
    const uint32_t tileH = tile(sourceH, outH, 32);
    const auto bits = [](uint32_t value) { return std::bit_cast<float>(value); };
    const float constants[8] = {bits(sourceW), bits(sourceH), bits(outW), bits(outH),
                                hdr ? 1.0f : 0.0f, bits(tileW), bits(tileH), bits(srgbView)};
    pass.bind(list, constants, gpuHandleOf(pass,srv).ptr, gpuHandleOf(pass,uav).ptr);
    list->Dispatch((outW + tileW - 1)/tileW, (outH + tileH - 1)/tileH, 1);
}

}
