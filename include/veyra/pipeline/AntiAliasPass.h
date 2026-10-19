#pragma once
#include "veyra/engine/AntiAliasSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <string>

namespace veyra::pipeline {
// One spatial anti-aliasing instance (#53) on linear FP16 textures of one size:
// FXAA (three tiers), SMAA (four tiers, optionally with the experimental
// no-jitter temporal accumulation) or MLAA. The graph owns input and output;
// the pass owns its intermediate textures, the SMAA lookup tables and the
// temporal history. Method, tier and temporal mode are fixed at creation (a
// change rebuilds the graph); the MLAA threshold / strength and the history
// weight / rejection are live (refresh).
class AntiAliasPass {
public:
    static constexpr UINT kConstantCount=16;
    bool initialize(ID3D12Device* device,const engine::AntiAliasSettings& settings,uint32_t width,uint32_t height);
    void refresh(const engine::AntiAliasSettings& settings);
    void createViews(ID3D12Device* device,DescriptorStager& stager,ID3D12Resource* input,ID3D12Resource* output);
    // Records every pass of the method; `reset` (seek, scene cut, timeline
    // discontinuity) drops the temporal history before this frame.
    bool dispatch(ID3D12GraphicsCommandList* list,StateTracker& tracker,bool hdrWorking,bool reset);
    const engine::AntiAliasSettings& settings() const { return settings_; }
    std::string describe() const;
    bool historyValid() const { return historyValid_; }
private:
    struct Stage { ComputePass pass; UINT srvCount=0; };
    bool createStage(ID3D12Device* device,Stage& stage,const char* shader,UINT srvCount);
    bool prepareLookups(ID3D12Device* device);
    void uploadLookups(ID3D12GraphicsCommandList* list,StateTracker& tracker);
    float edgeThreshold() const;
    engine::AntiAliasSettings settings_{};
    uint32_t width_=0,height_=0,frame_=0;
    UINT areaPitch_=0,searchPitch_=0;
    bool historyValid_=false,lookupsPending_=false;
    DXGI_FORMAT edgesFormat_=DXGI_FORMAT_R8G8_UNORM,weightsFormat_=DXGI_FORMAT_R8G8B8A8_UNORM;
    Stage luma_,fxaa_,smaaEdges_,smaaWeights_,smaaBlend_,temporal_,mlaaEdges_,mlaaCounts_,mlaaBlend_;
    ComPtr<ID3D12Resource> lumaTexture_,edges_,weights_,counts_,current_,history_;
    ComPtr<ID3D12Resource> area_,search_,areaUpload_,searchUpload_;
    ID3D12Resource* input_=nullptr;
    ID3D12Resource* output_=nullptr;
};
}
