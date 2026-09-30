#pragma once
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/engine/EnhancementSettings.h"
#include <array>

namespace veyra::pipeline {
// Residual history stays on the graph's ordered GPU queue. Descriptor sets are
// immutable; neither CPU waits nor descriptor rewrites occur per frame.
//
// The anti-flicker tier (engine::NrAntiFlicker) selects how the accumulation
// behaves; see shaders/NrTemporal.hlsl for what each route means. Route
// LowFrequency additionally runs a half-resolution reduce pass and rebuilds a
// low-frequency observation, which is why this pass owns the reduce shader and
// the pair of half-resolution textures.
class NrTemporalPass {
public:
    bool initialize(ID3D12Device*,ID3D12Resource* base,ID3D12Resource* motion,ID3D12Resource* output,
                    engine::NrAntiFlicker tier=engine::NrAntiFlicker::Flow);
    ID3D12Resource* raw()const{return raw_.Get();}
    void run(ID3D12GraphicsCommandList*,StateTracker&,bool reset,bool haveMotion,double frameMs,
             float total,const engine::ProtectionSettings& protection);
    void reset(){valid_=false;}
    void close();
private:
    ComputePass pass_;
    // Only allocated for the low-frequency tier. The reduce pass writes both
    // every frame and the main pass reads both back in the same frame, so they
    // are two distinct signals (residual and guide), not a ping-pong pair --
    // matching the upstream shader's u0/u1 outputs.
    ComputePass reducePass_;
    ComPtr<ID3D12Resource> lowResidual_,lowGuide_;
    ComPtr<ID3D12Resource> raw_,history_[2],guide_[2];
    ID3D12Resource* base_=nullptr;ID3D12Resource* motion_=nullptr;ID3D12Resource* output_=nullptr;
    unsigned width_=0,height_=0,lowWidth_=0,lowHeight_=0,index_=0;
    engine::NrAntiFlicker tier_=engine::NrAntiFlicker::Flow;
    bool valid_=false;
};
}
