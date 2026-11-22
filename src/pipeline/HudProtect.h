// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {

// The history and post-composition scratch are bounded by the union of the
// configured regions. Only the textures handed to the FG providers are full size.
class HudProtect {
public:
    bool initialize(ID3D12Device* device,DXGI_FORMAT format,uint32_t width,uint32_t height,
                    unsigned outputMode,const engine::HudProtectRegions& regions,
                    bool externalMotion,unsigned generatedCount);
    void createViews(ID3D12Device* device,DescriptorStager& stager,
                     ID3D12Resource* const (&real)[2],ID3D12Resource* const* generated);
    void prepare(ID3D12GraphicsCommandList* list,StateTracker& tracker,diagnostics::GpuTimer& timer,
                 unsigned parity,bool reset);
    ID3D12Resource* fixMotion(ID3D12GraphicsCommandList* list,StateTracker& tracker,
                             diagnostics::GpuTimer& timer,unsigned parity,ID3D12Resource* input);
    void compose(ID3D12GraphicsCommandList* list,StateTracker& tracker,unsigned slot,
                 ID3D12Resource* generated,unsigned maskParity,unsigned heldParity);
    ID3D12Resource* input(unsigned parity) const { return input_[parity].Get(); }
    ID3D12Resource* mask(unsigned parity) const { return mask_[parity].Get(); }
    const D3D12_BOX& bounds() const { return bounds_; }

    static D3D12_BOX regionBounds(uint32_t width,uint32_t height,const engine::HudProtectRegions& regions);

private:
    using Constants=std::array<float,28>;
    void dispatch(ID3D12GraphicsCommandList* list,const ComputePass& pass,const Constants& values,
                  unsigned table,unsigned srvCount) const;

    D3D12_BOX bounds_{};
    unsigned generatedCount_=0;
    Constants constants_{};
    ID3D12Resource* real_[2]{};
    ComPtr<ID3D12Resource> input_[2],confidence_[2],mask_[2],motion_,density_,correctedFlow_,scratch_;
    ComputePass matchPass_,detectPass_,repairPass_,flowPass_,composePass_;
};

}
