#include "veyra/pipeline/NrHoldPass.h"
#include <bit>
#include <cmath>

namespace veyra::pipeline {

bool NrHoldPass::initialize(ID3D12Device* device, uint32_t width, uint32_t height,
                            ID3D12Resource* current, ID3D12Resource* base)
{
    close();
    if (!device || !current || !base || width == 0 || height == 0) return false;
    current_ = current;
    base_ = base;
    width_ = width;
    height_ = height;
    // The source may be a different extent from the NR output under the
    // realtime NR policy; the shader rescales its neighbourhood accordingly.
    const auto baseDesc = base->GetDesc();
    baseWidth_ = uint32_t(baseDesc.Width);
    baseHeight_ = baseDesc.Height;
    for (unsigned i = 0; i < 2; ++i) {
        output_[i] = makeTexture(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        previousBase_[i] = makeTexture(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        if (!output_[i] || !previousBase_[i]) return false;
    }
    std::vector<uint8_t> shader;
    // One descriptor set per ping-pong phase (8 slots each): 4 SRVs + 2 UAVs.
    // The set is staged once and never rewritten per frame.
    if (!pass_.loadShader("NrHold.dxil", shader) || !pass_.create(device, shader, 16, 4, 2)) return false;
    DescriptorStager staging;
    if (!staging.initialize(device, 16)) return false;
    for (unsigned i = 0; i < 2; ++i) {
        // Reading the *other* phase's results as history keeps the slot mapping
        // stable across frames.
        ID3D12Resource* sources[] = {current, output_[1 - i].Get(), base, previousBase_[1 - i].Get()};
        for (unsigned j = 0; j < 4; ++j) staging.stageSrv(sources[j], nullptr, pass_.heap.Get(), i * 8 + j);
        makeUav(device, output_[i].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(pass_, i * 8 + 4));
        makeUav(device, previousBase_[i].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(pass_, i * 8 + 5));
    }
    return true;
}

void NrHoldPass::run(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool reset,
                     float strength, float tolerance)
{
    if (pass_.pso == nullptr || strength <= 0.f || !std::isfinite(strength)) return;
    const unsigned i = index_;
    // History is only meaningful when the previous frame was actually written
    // by this pass; on a reset the shader sees UseHistory 0 and publishes the
    // current value unchanged while still seeding the anchor.
    const float c[8] = {
        std::bit_cast<float>(width_), std::bit_cast<float>(height_),
        std::bit_cast<float>(baseWidth_), std::bit_cast<float>(baseHeight_),
        strength, tolerance, valid_ && !reset ? 1.f : 0.f, 0.f};
    tracker.transition(list, current_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, output_[1 - i].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, base_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, previousBase_[1 - i].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (auto* r : {output_[i].Get(), previousBase_[i].Get()})
        tracker.transition(list, r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pass_.bind(list, c, gpuHandleOf(pass_, i * 8).ptr, gpuHandleOf(pass_, i * 8 + 4).ptr);
    list->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    for (auto* r : {output_[i].Get(), previousBase_[i].Get()})
        tracker.uavBarrier(list, r);
    // The written pair becomes the next frame's history source. A reset still
    // advances the ping-pong: the current frame is what the next one compares
    // against, reset or not.
    valid_ = true;
    index_ = 1 - i;
}

void NrHoldPass::close()
{
    pass_ = {};
    for (auto& r : output_) r.Reset();
    for (auto& r : previousBase_) r.Reset();
    current_ = base_ = nullptr;
    width_ = height_ = baseWidth_ = baseHeight_ = index_ = 0;
    valid_ = false;
}

} // namespace veyra::pipeline
