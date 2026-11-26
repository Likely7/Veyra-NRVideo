#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Adapted for Veyra from PushFPS 731589e2. Source detection and composition stay on the GPU.
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/pipeline/SubtitleHoldRule.h"
#include <string>

namespace veyra::pipeline {

class SubtitleHold {
public:
    static constexpr UINT kConstants = 20;
    static constexpr UINT kMaxGenerated = 32;
    // outputMode: how the frames are encoded, as Live HUD Lock (0 sRGB, 1 scRGB, 2 PQ).
    // Optional already-cleaned inputs are shared with the reticle pass.
    //
    bool initialize(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, unsigned outputMode,
                    ID3D12Resource* const shared[2]);
    // real[0..1]: the graph's two real-frame textures by parity; generated: its generated-frame textures.
    void createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* const real[2],
                     ID3D12Resource* const* generated, unsigned generatedCount, ID3D12Resource* const detection[2]);
    // After real frame `parity` is final. reset: there is no valid frame before it (start, seek, settings).
    bool update(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned parity, bool reset);
    // What frame generation is given in place of real frame `parity` (valid after update(parity)).
    ID3D12Resource* input(unsigned parity) const { return parity < 2 ? input_[parity].Get() : nullptr; }
    // generated[slot] was made of input(1 - parity) and input(parity), `t` of the way from the older to the newer.
    bool compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned slot, unsigned parity, float t);
    bool composes(unsigned slot) const { return slot < generatedCount_ && writable_[slot]; }
    overlay::Zone zone() const { return zone_; }
    // For tests: what the last update left (the zone's size).
    ID3D12Resource* maskTexture(unsigned parity) const { return parity < 2 ? mask_[parity].Get() : nullptr; }
    ID3D12Resource* countTexture() const { return state_.Get(); }
    ID3D12Resource* movingTexture() const { return moving_.Get(); }
    std::string describe() const;
private:
    void constants(float* c, unsigned stage, bool reset) const;
    uint32_t width_ = 0, height_ = 0;
    unsigned outputMode_ = 0, generatedCount_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    overlay::Zone zone_;
    overlay::Rule rule_;
    uint32_t blocks_ = 0, blocksY_ = 0;
    bool sharesInput_ = false;
    static constexpr const char* name_ = "subtitle-hold";
    ComputePass track_, fill_, compose_;
    bool primed_ = false;
    ComPtr<ID3D12Resource> input_[2];                 // the frames frame generation gets (the picture's size)
    ComPtr<ID3D12Resource> state_;                    // the counts, four kinds a pixel
    ComPtr<ID3D12Resource> memory_[2];                // what is remembered, by the parity of the frame that wrote it
    ComPtr<ID3D12Resource> accept_, mask_[2];         // accepted bars; the mask each frame was filled with
    ComPtr<ID3D12Resource> blockMoves_, moving_;      // per block, and for the zone: does the picture move
    ComPtr<ID3D12Resource> zoneCopy_;                 // the zone of a generated frame as it was made
    bool writable_[kMaxGenerated]{};
    ID3D12Resource* real_[2]{};
    ID3D12Resource* detection_[2]{};
    ID3D12Resource* generated_[kMaxGenerated]{};
};
}
