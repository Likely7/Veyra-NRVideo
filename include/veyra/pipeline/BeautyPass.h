#pragma once
#include "veyra/engine/BeautySettings.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <string>

namespace veyra::pipeline {
// One beauty node on linear float textures of one size (the node's place in the
// chain). The graph owns the input, the output, the frame's original (the
// picture before every effect node) and the faces buffer of the shared tracker;
// the pass owns a 64-byte region buffer, two statistics-grid textures (about
// 960x540 whatever the node size) and the indirect-dispatch signature.
//
// Each frame the output starts as a copy of the input, so every pixel outside
// this frame's face region is bit-identical; shaders/BeautyRoi.hlsl then sizes
// the remaining dispatches to that region (none without faces). Every setting is
// live (refresh): only the node's existence changes the graph.
class BeautyPass {
public:
    static constexpr UINT kConstantCount = 32;
    bool initialize(ID3D12Device* device, const engine::BeautySettings& settings, uint32_t width, uint32_t height,
                    uint32_t sourceWidth, uint32_t sourceHeight);
    void refresh(const engine::BeautySettings& settings) { settings_ = settings; }
    void createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* input, ID3D12Resource* output,
                     ID3D12Resource* original, ID3D12Resource* faces, ID3D12Resource* shapes);
    // `originalValid`: the original texture holds this frame (face restore needs it).
    bool dispatch(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool hdrWorking, bool originalValid);
    const engine::BeautySettings& settings() const { return settings_; }
    std::string describe() const;
    uint32_t statisticsScale() const { return scale_; }
    // Node pixels of the smoothing sigma for a radius setting (1080p equivalent).
    float sigmaPixels() const;
private:
    struct Stage { ComputePass pass; };
    bool createStage(ID3D12Device* device, Stage& stage, const char* shader, UINT sets);
    void constants(uint32_t* c, bool hdrWorking, bool originalValid) const;
    engine::BeautySettings settings_{};
    uint32_t width_ = 0, height_ = 0, sourceWidth_ = 0, sourceHeight_ = 0, lowWidth_ = 0, lowHeight_ = 0, scale_ = 1;
    Stage roi_, stats_, blur_, apply_;
    ComPtr<ID3D12Resource> region_, lowA_, lowB_;
    ComPtr<ID3D12CommandSignature> signature_;
    ID3D12Resource* input_ = nullptr;
    ID3D12Resource* output_ = nullptr;
    ID3D12Resource* original_ = nullptr;
    ID3D12Resource* faces_ = nullptr;
    ID3D12Resource* shapes_ = nullptr;
};
}
