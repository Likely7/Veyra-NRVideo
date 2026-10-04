#pragma once
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <array>

namespace veyra::pipeline {
// Experimental file/export producer. Reuses product colour/NVOF/densify,
// bounded to two snapshots. No capture lookahead or pixel CPU readback.
class FlowPrefetch {
public:
    ~FlowPrefetch(){shutdown();}
    bool initialize(gfx::D3D12DeviceContext& owner,const EnhanceGraphDesc& desc);
    bool prepare(const AVFrame* frame,double pts,bool reset,uint64_t source,
                 const ColorDescription* color,EnhanceGraph::PreparedFlow& output);
    void consumed(ID3D12Fence* fence,uint64_t value);
    void shutdown();
private:
    struct Slot {ComPtr<ID3D12Resource> flow,confidence;ComPtr<ID3D12Fence> consumer;uint64_t consumerValue=0;};
    gfx::D3D12DeviceContext context_;gfx::CommandSlotRing ring_;
    std::unique_ptr<EnhanceGraph> graph_;std::array<Slot,2> slots_;
    uint64_t sequence_=0,previous_=0,revision_=0;unsigned activeSlot_=0;
};
}
