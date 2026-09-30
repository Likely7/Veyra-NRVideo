#pragma once
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {
// Output stabiliser: holds the previous output where the source has not
// visibly changed (stage 5 of docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md).
//
// It sits after the last NR layer and the residual composite, before SR/FG, so
// it can only smooth what NR emitted -- never a generated frame. It is disabled
// by default (strength 0), and at 0 the caller skips run() entirely, which
// keeps the disabled path byte-identical to a build without this pass.
//
// The pass owns its own textures and never writes into the graph's NR output:
// it reads that output, produces a stabilised result in its own ping-pong
// target, and the caller copies it back. That copy is the price of leaving
// every existing SR/FG/present binding untouched, and it is one full-extent
// blit behind the NR cost.
//
// No reprojection and no motion vectors: a wrong vector would drag the previous
// output into a region that did move, which is exactly the ghosting this pass
// exists to avoid. Regions that moved stop holding.
class NrHoldPass {
public:
    // `current` is the graph's NR result for this frame, `base` the image that
    // fed NR (possibly a different extent under the realtime NR policy, which
    // the shader rescales). Both are borrowed; the pass allocates its own
    // history at the NR extent.
    bool initialize(ID3D12Device* device, uint32_t width, uint32_t height,
                    ID3D12Resource* current, ID3D12Resource* base);
    // `reset` (seek, scene cut, resize, source switch, pause/resume) publishes
    // the current value and holds nothing. `strength` 0 is refused: callers must
    // skip the call instead, so the disabled path records no dispatch at all.
    void run(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool reset,
             float strength, float tolerance);
    // The stabilised result, valid until the next run(). The caller copies this
    // into whatever texture the rest of the graph already reads.
    ID3D12Resource* result() const { return output_[index_ ^ 1u].Get(); }
    bool active() const { return valid_; }
    void reset() { valid_ = false; }
    void close();

private:
    ComputePass pass_;
    ComPtr<ID3D12Resource> output_[2];     // ping-pong results
    ComPtr<ID3D12Resource> previousBase_[2]; // ping-pong source anchors
    ID3D12Resource* current_ = nullptr;
    ID3D12Resource* base_ = nullptr;
    unsigned width_ = 0, height_ = 0, baseWidth_ = 0, baseHeight_ = 0, index_ = 0;
    bool valid_ = false;
};
}
