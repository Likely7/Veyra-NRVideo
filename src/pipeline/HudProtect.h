// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "veyra/diagnostics/GpuTimer.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <chrono>

namespace veyra::pipeline {

// The history and post-composition scratch are bounded by the union of the
// configured regions. Only the textures handed to the FG providers are full size.
class HudProtect {
public:
    static constexpr unsigned countersPerRegion=7, regionCount=engine::HudProtectRegions{}.size();
    enum Counter : unsigned { Density, Core, Repaired, Remembered, Evidence, Trusted, CentredMark };
    using Counters=std::array<uint32_t,countersPerRegion*regionCount>;
    bool initialize(ID3D12Device* device,DXGI_FORMAT format,uint32_t width,uint32_t height,
                    unsigned outputMode,const engine::HudProtectRegions& regions,
                    bool externalMotion,unsigned generatedCount);
    void createViews(ID3D12Device* device,DescriptorStager& stager,
                     ID3D12Resource* const (&real)[2],ID3D12Resource* const* generated,ID3D12Resource* denseFlow);
    void prepare(ID3D12GraphicsCommandList* list,StateTracker& tracker,diagnostics::GpuTimer& timer,
                 unsigned parity,bool reset,bool haveFlow,ID3D12Fence* fence);
    void submitted(uint64_t fence);
    static bool clean(const Counters& counters,unsigned region,float limit);
    static bool observed(const Counters& counters,unsigned region,float limit);
    ID3D12Resource* fixMotion(ID3D12GraphicsCommandList* list,StateTracker& tracker,
                             diagnostics::GpuTimer& timer,unsigned parity,ID3D12Resource* input);
    void compose(ID3D12GraphicsCommandList* list,StateTracker& tracker,unsigned slot,
                 ID3D12Resource* generated,unsigned maskParity,unsigned heldParity);
    ID3D12Resource* input(unsigned parity) const { return input_[parity].Get(); }
    ID3D12Resource* mask(unsigned parity) const { return mask_[parity].Get(); }
    ID3D12Resource* motion(unsigned parity) const { return motion_[parity].Get(); }
    ID3D12Resource* counters(unsigned parity) const { return density_[parity].Get(); }
    const D3D12_BOX& bounds() const { return bounds_; }

    static D3D12_BOX regionBounds(uint32_t width,uint32_t height,const engine::HudProtectRegions& regions);

private:
    using Constants=std::array<float,30>;
    using Clock=std::chrono::steady_clock;
    static constexpr unsigned diagnosticSlots=6;
    struct DiagnosticSample {
        uint64_t fence=0;
        unsigned generated=0;
        bool previousValid=false;
    };
    struct Period {
        unsigned frames=0,trusted=0,confirmed=0,repaired=0,generated=0,cleanPairs=0,fallback=0,skipped=0;
    };
    void sample(ID3D12GraphicsCommandList* list,StateTracker& tracker,unsigned parity,bool reset);
    void collect(ID3D12Fence* fence);
    void account(const Counters& current,const Counters& previous,unsigned generated);
    void dispatch(ID3D12GraphicsCommandList* list,const ComputePass& pass,const Constants& values,
                  unsigned table,unsigned srvCount) const;

    D3D12_BOX bounds_{};
    std::array<float,regionCount> densityLimits_{};
    unsigned generatedCount_=0;
    Constants constants_{};
    ID3D12Resource* real_[2]{};
    ID3D12Resource* denseFlow_=nullptr;
    ComPtr<ID3D12Resource> input_[2],confidence_[2],appearance_[2],mask_[2],motion_[2],density_[2];
    ComPtr<ID3D12Resource> correctedFlow_,scratch_,diagnostics_;
    std::array<DiagnosticSample,diagnosticSlots> samples_{};
    unsigned sampleRead_=0,sampleWrite_=0,sampleCount_=0;
    bool sampleRecorded_=false,confirmed_=false,everConfirmed_=false;
    Clock::time_point summaryAt_=Clock::now();
    Period period_{};
    ComputePass matchPass_,detectPass_,repairPass_,flowPass_,composePass_;
};

}
