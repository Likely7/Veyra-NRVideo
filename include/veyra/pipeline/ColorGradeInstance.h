#pragma once
#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {
// One non-ingress grade. All video pixels stay on the GPU. Upload buffers are
// indexed by the acquired command slot, so live table edits cannot overwrite
// data still referenced by an older submission. The owner drains before close.
class ColorGradeInstance {
public:
    bool initialize(ID3D12Device* device,uint32_t commandSlots);
    bool createPass(ID3D12Device* device);
    void refresh(const engine::ColorSettings& settings);
    bool setLut(ID3D12Device* device,const float* rgb,unsigned size);
    void createViews(ID3D12Device* device,DescriptorStager& stager,ID3D12Resource* input,ID3D12Resource* output);
    void stageLut(DescriptorStager& stager);
    bool dispatch(ID3D12GraphicsCommandList* list,StateTracker& tracker,uint32_t slot,
                  uint32_t width,uint32_t height,bool hdrWorking);
    bool identity()const{return tables_.identity;}
private:
    ComputePass pass_;
    ColorGradeTables tables_;
    engine::ColorSettings settings_;
    std::array<ComPtr<ID3D12Resource>,3> textures_;
    std::vector<std::array<ComPtr<ID3D12Resource>,3>> uploads_;
    ComPtr<ID3D12Resource> lut_,lutUpload_;
    ID3D12Resource* input_=nullptr;
    ID3D12Resource* output_=nullptr;
    unsigned lutSize_=0,lutPitch_=0;
    bool dirty_=true,lutPending_=false,exposureOnly_=false;
};
}
