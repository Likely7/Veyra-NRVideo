#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <vector>

namespace veyra::pipeline {
// NR inference reuse, GPU side (docs/NR_INFERENCE_REUSE_2026-11-07.md).
//
// Owns everything a reused frame needs and nothing else: the last inferred
// frame's NR chain output and input, the motion back to that frame, and the
// passes that carry the change. The graph's own textures are borrowed:
//   source  - the NR chain input (EnhanceGraph::nrBaseInput)
//   output  - the NR chain output (residualRgba_), written on reused frames
//   flow    - this frame's current->previous flow at the flow extent
//   targets - NR motion inputs; after a reused frame they receive the motion
//             back to the last inferred frame, which is what NR's own history
//             holds, instead of the one-frame flow
// A graph built without reuse never creates this pass, so its command stream
// is unchanged.
class NrReusePass {
public:
    struct MotionTarget { ID3D12Resource* texture = nullptr; uint32_t width = 0, height = 0; };
    bool initialize(ID3D12Device* device, uint32_t width, uint32_t height,
                    uint32_t flowWidth, uint32_t flowHeight,
                    ID3D12Resource* source, ID3D12Resource* output, ID3D12Resource* flow,
                    const std::vector<MotionTarget>& targets);
    // Every frame with flow: motion back to the last inferred frame. `chain`
    // is true when the previous frame reused.
    void compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool chain);
    // Inferred frame after a reused one: overwrite NR's motion inputs.
    void writeMotionTargets(ID3D12GraphicsCommandList* list, StateTracker& tracker);
    // Inferred frame: keep its chain output and input for a following reuse.
    void store(ID3D12GraphicsCommandList* list, StateTracker& tracker);
    // Reused frame: output = source + carried change, outside `protection`.
    void reuse(ID3D12GraphicsCommandList* list, StateTracker& tracker, const engine::ProtectionSettings& protection);
    // Motion for the temporal stabiliser: identical to the flow after an
    // inferred frame, chained after a reused one.
    ID3D12Resource* motion() const { return motion_.Get(); }
    uint64_t bytes() const { return bytes_; }
    void close();

    // Perceptual box-luma mismatch: full weight below kLow, none above kHigh.
    static constexpr float kLow = 0.03f, kHigh = 0.08f;
    // Per-pixel detail mismatch: the fine part of the change is carried fully
    // below kDetailLow and not at all above kDetailHigh.
    static constexpr float kDetailLow = 0.03f, kDetailHigh = 0.09f;
    static constexpr unsigned kWarpConstants = 28;

private:
    ComputePass compose_, warp_, adapt_;
    ComPtr<ID3D12Resource> displacement_[2], motion_, keyOutput_, keySource_;
    std::vector<MotionTarget> targets_;
    ID3D12Resource* source_ = nullptr;
    ID3D12Resource* output_ = nullptr;
    ID3D12Resource* flow_ = nullptr;
    uint32_t width_ = 0, height_ = 0, flowWidth_ = 0, flowHeight_ = 0;
    unsigned latest_ = 0; // displacement_[latest_] holds this frame's result
    uint64_t bytes_ = 0;
};
}
