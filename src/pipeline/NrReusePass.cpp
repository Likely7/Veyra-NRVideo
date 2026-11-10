#include "veyra/pipeline/NrReusePass.h"
#include <algorithm>
#include <bit>
#include <format>

namespace veyra::pipeline {
namespace {
// A texture CopyResource can fill from `like`: same extent, format and flags.
ComPtr<ID3D12Resource> makeLike(ID3D12Device* device, ID3D12Resource* like)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto desc = like->GetDesc();
    desc.Flags &= ~(D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER);
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) {
        veyra::log::error("nr-reuse", std::format("key texture alloc failed {}x{} format={} hr=0x{:X}",
            uint64_t(desc.Width), desc.Height, int(desc.Format), unsigned(hr)));
        return nullptr;
    }
    return r;
}
uint64_t textureBytes(ID3D12Device* device, ID3D12Resource* r)
{
    const auto desc = r->GetDesc();
    return device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
}
float bits(uint32_t v) { return std::bit_cast<float>(v); }
}

bool NrReusePass::initialize(ID3D12Device* device, uint32_t width, uint32_t height,
                             uint32_t flowWidth, uint32_t flowHeight,
                             ID3D12Resource* source, ID3D12Resource* output, ID3D12Resource* flow,
                             const std::vector<MotionTarget>& targets)
{
    close();
    if (!device || !source || !output || !flow || !width || !height || !flowWidth || !flowHeight) return false;
    const auto sourceDesc = source->GetDesc(), outputDesc = output->GetDesc();
    if (sourceDesc.Width != width || sourceDesc.Height != height ||
        outputDesc.Width != width || outputDesc.Height != height) {
        veyra::log::warn("nr-reuse", std::format("chain extents differ source={}x{} output={}x{} expected={}x{}",
            uint64_t(sourceDesc.Width), sourceDesc.Height, uint64_t(outputDesc.Width), outputDesc.Height, width, height));
        return false;
    }
    source_ = source; output_ = output; flow_ = flow;
    width_ = width; height_ = height; flowWidth_ = flowWidth; flowHeight_ = flowHeight;
    targets_ = targets;
    keyOutput_ = makeLike(device, output);
    keySource_ = makeLike(device, source);
    for (auto& d : displacement_) d = makeTexture(device, flowWidth, flowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    motion_ = makeTexture(device, flowWidth, flowHeight, DXGI_FORMAT_R16G16_FLOAT, true);
    if (!keyOutput_ || !keySource_ || !displacement_[0] || !displacement_[1] || !motion_) { close(); return false; }
    for (auto* r : {keyOutput_.Get(), keySource_.Get(), displacement_[0].Get(), displacement_[1].Get(), motion_.Get()})
        bytes_ += textureBytes(device, r);

    std::vector<uint8_t> shader;
    if (!compose_.loadShader("NrReuseCompose.dxil", shader) ||
        !compose_.create(device, shader, 8, 2, 2, 8, 0, true)) { close(); return false; }
    if (!warp_.loadShader("NrReuseWarp.dxil", shader) ||
        !warp_.create(device, shader, 16, 4, 1, kWarpConstants, 0, true)) { close(); return false; }
    const UINT adaptSlots = UINT(std::max<size_t>(1, targets_.size()) * 2);
    if (!adapt_.loadShader("FlowAdapt.dxil", shader) ||
        !adapt_.create(device, shader, adaptSlots, 1, 1)) { close(); return false; }

    DescriptorStager staging;
    if (!staging.initialize(device, 32)) { close(); return false; }
    // Ping-pong sets, staged once. Set i writes displacement_[i] and reads the
    // other one, so the slot mapping never changes between frames.
    for (unsigned i = 0; i < 2; ++i) {
        staging.stageSrv(flow, nullptr, compose_.heap.Get(), i * 4 + 0);
        staging.stageSrv(displacement_[1 - i].Get(), nullptr, compose_.heap.Get(), i * 4 + 1);
        makeUav(device, displacement_[i].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(compose_, i * 4 + 2));
        makeUav(device, motion_.Get(), DXGI_FORMAT_R16G16_FLOAT, cpuHandleOf(compose_, i * 4 + 3));
        // Warp set i reads the displacement compose set i wrote.
        ID3D12Resource* sources[] = {source, keyOutput_.Get(), keySource_.Get(), displacement_[i].Get()};
        for (unsigned j = 0; j < 4; ++j) staging.stageSrv(sources[j], nullptr, warp_.heap.Get(), i * 8 + j);
        makeUav(device, output, outputDesc.Format, cpuHandleOf(warp_, i * 8 + 4));
    }
    for (size_t t = 0; t < targets_.size(); ++t) {
        staging.stageSrv(motion_.Get(), nullptr, adapt_.heap.Get(), UINT(t * 2));
        makeUav(device, targets_[t].texture, DXGI_FORMAT_R16G16_FLOAT, cpuHandleOf(adapt_, UINT(t * 2 + 1)));
    }
    return true;
}

void NrReusePass::compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, bool chain)
{
    if (!compose_.pso) return;
    const unsigned i = latest_ ^ 1u;
    const float c[8] = {bits(flowWidth_), bits(flowHeight_), bits(chain ? 1u : 0u), 0, 0, 0, 0, 0};
    tracker.transition(list, flow_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, displacement_[latest_].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, displacement_[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    tracker.transition(list, motion_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    compose_.bind(list, c, gpuHandleOf(compose_, i * 4).ptr, gpuHandleOf(compose_, i * 4 + 2).ptr);
    list->Dispatch((flowWidth_ + 15) / 16, (flowHeight_ + 15) / 16, 1);
    tracker.uavBarrier(list, displacement_[i].Get());
    tracker.uavBarrier(list, motion_.Get());
    tracker.transition(list, displacement_[i].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, motion_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    latest_ = i;
}

void NrReusePass::writeMotionTargets(ID3D12GraphicsCommandList* list, StateTracker& tracker)
{
    if (!adapt_.pso) return;
    tracker.transition(list, motion_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t t = 0; t < targets_.size(); ++t) {
        const auto& target = targets_[t];
        tracker.transition(list, target.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float c[8] = {bits(flowWidth_), bits(flowHeight_), bits(target.width), bits(target.height), 0, 0, 0, 0};
        adapt_.bind(list, c, gpuHandleOf(adapt_, UINT(t * 2)).ptr, gpuHandleOf(adapt_, UINT(t * 2 + 1)).ptr);
        list->Dispatch((target.width + 15) / 16, (target.height + 15) / 16, 1);
        tracker.uavBarrier(list, target.texture);
        tracker.transition(list, target.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}

void NrReusePass::store(ID3D12GraphicsCommandList* list, StateTracker& tracker)
{
    if (!keyOutput_) return;
    auto copy = [&](ID3D12Resource* from, ID3D12Resource* to) {
        tracker.transition(list, from, D3D12_RESOURCE_STATE_COPY_SOURCE);
        tracker.transition(list, to, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(to, from);
        tracker.transition(list, to, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, from, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    copy(output_, keyOutput_.Get());
    copy(source_, keySource_.Get());
}

void NrReusePass::reuse(ID3D12GraphicsCommandList* list, StateTracker& tracker, const engine::ProtectionSettings& protection)
{
    if (!warp_.pso) return;
    const unsigned i = latest_;
    float c[kWarpConstants] = {bits(width_), bits(height_), bits(flowWidth_), bits(flowHeight_), kLow, kHigh, 0, 0,
                               protection.enabled ? 1.f : 0.f, protection.featherPixels, kDetailLow, kDetailHigh};
    for (size_t r = 0; r < 4; ++r) {
        const auto q = engine::protectionConstants(protection.regions[r]);
        for (size_t k = 0; k < 4; ++k) c[12 + r * 4 + k] = q[k];
    }
    for (auto* r : {source_, keyOutput_.Get(), keySource_.Get(), displacement_[i].Get()})
        tracker.transition(list, r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    warp_.bind(list, c, gpuHandleOf(warp_, i * 8).ptr, gpuHandleOf(warp_, i * 8 + 4).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, output_);
    tracker.transition(list, output_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void NrReusePass::close()
{
    compose_ = {}; warp_ = {}; adapt_ = {};
    for (auto& d : displacement_) d.Reset();
    motion_.Reset(); keyOutput_.Reset(); keySource_.Reset();
    targets_.clear();
    source_ = output_ = flow_ = nullptr;
    width_ = height_ = flowWidth_ = flowHeight_ = 0;
    latest_ = 0;
    bytes_ = 0;
}

} // namespace veyra::pipeline
