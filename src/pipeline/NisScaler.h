// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include <array>

namespace veyra::gfx { class CommandSlotRing; }

namespace veyra::pipeline {

class NisScaler {
public:
    static Extent inputExtent(Extent input, Extent output);

    bool initialize(ID3D12Device* device, gfx::CommandSlotRing& ring, Extent source, Extent output, bool hdr);
    void dispatch(ID3D12GraphicsCommandList* list, uint32_t slot, StateTracker& tracker,
                  ID3D12Resource* input, ID3D12Resource* output);

private:
    bool uploadCoefficients(gfx::CommandSlotRing& ring);

    ID3D12Device* device_ = nullptr;
    Extent source_, input_, output_;
    bool hdr_ = false;
    std::array<float, 28> constants_{};
    ComputePass scaler_, prepare_;
    DescriptorStager descriptors_;
    std::array<ComPtr<ID3D12Resource>, 2> coefficients_;
    ComPtr<ID3D12Resource> upload_, prepared_;
};

} // namespace veyra::pipeline
