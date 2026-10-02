#include "veyra/pipeline/ColorDetailPass.h"
#include <algorithm>
#include <bit>

namespace veyra::pipeline {
namespace {
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
}

bool ColorDetailPass::prepare(ID3D12Device* device, ID3D12Resource* target)
{
    const auto desc = target->GetDesc();
    if (target == target_ && copy_ && uint32_t(desc.Width) == width_ && desc.Height == height_) return true;
    close();
    if (desc.Format != kFormat) {
        veyra::log::warn("color-detail", std::format("target format {} is not RGBA16F; texture/clarity/dehaze skipped", unsigned(desc.Format)));
        return false;
    }
    width_ = uint32_t(desc.Width);
    height_ = desc.Height;
    // About 135 block rows whatever the extent, so the clarity radius is a
    // fixed share of the picture (8 px blocks at 1080p, 16 px at 2160p).
    block_ = std::clamp<uint32_t>(height_ / 135u, 4u, 32u);
    lowWidth_ = (width_ + block_ - 1) / block_;
    lowHeight_ = (height_ + block_ - 1) / block_;
    copy_ = makeTexture(device, width_, height_, kFormat, false);
    lowA_ = makeTexture(device, lowWidth_, lowHeight_, kFormat, true);
    lowB_ = makeTexture(device, lowWidth_, lowHeight_, kFormat, true);
    if (!copy_ || !lowA_ || !lowB_) { close(); return false; }
    std::vector<uint8_t> cs;
    // reduce: t0 copy, u0 lowA. blur: (t0 lowA, u0 lowB) then (t0 lowB, u0 lowA).
    // apply: t0 copy, t1 lowA, u0 target.
    if (!reduce_.loadShader("ColorDetailReduce.dxil", cs) || !reduce_.create(device, cs, 2, 1, 1) ||
        !blur_.loadShader("ColorDetailBlur.dxil", cs) || !blur_.create(device, cs, 4, 1, 1) ||
        !apply_.loadShader("ColorDetailApply.dxil", cs) || !apply_.create(device, cs, 3, 2, 1, 12)) {
        close();
        return false;
    }
    makeSrv(device, copy_.Get(), kFormat, cpuHandleOf(reduce_, 0));
    makeUav(device, lowA_.Get(), kFormat, cpuHandleOf(reduce_, 1));
    makeSrv(device, lowA_.Get(), kFormat, cpuHandleOf(blur_, 0));
    makeUav(device, lowB_.Get(), kFormat, cpuHandleOf(blur_, 1));
    makeSrv(device, lowB_.Get(), kFormat, cpuHandleOf(blur_, 2));
    makeUav(device, lowA_.Get(), kFormat, cpuHandleOf(blur_, 3));
    makeSrv(device, copy_.Get(), kFormat, cpuHandleOf(apply_, 0));
    makeSrv(device, lowA_.Get(), kFormat, cpuHandleOf(apply_, 1));
    makeUav(device, target, kFormat, cpuHandleOf(apply_, 2));
    target_ = target;
    veyra::log::info("color-detail", std::format("texture/clarity/dehaze pass {}x{} block={} low={}x{}",
        width_, height_, block_, lowWidth_, lowHeight_));
    return true;
}

bool ColorDetailPass::run(ID3D12GraphicsCommandList* list, StateTracker& tracker, ID3D12Resource* target,
                          const engine::ColorSettings& settings, bool hdrWorking)
{
    if (!wanted(settings) || !list || !target || failed_) return true;
    ComPtr<ID3D12Device> device;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !prepare(device.Get(), target)) {
        // The grade itself is already written; the picture continues without the effects.
        failed_ = true;
        veyra::log::error("color-detail", "texture/clarity/dehaze pass unavailable; continuing without it");
        return false;
    }
    const float workingToScene = hdrWorking ? 80.0f / 203.0f : 1.0f;

    tracker.transition(list, target, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, copy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(copy_.Get(), target);
    tracker.transition(list, copy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    tracker.transition(list, lowA_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float reduce[8] = {std::bit_cast<float>(width_), std::bit_cast<float>(height_),
        std::bit_cast<float>(lowWidth_), std::bit_cast<float>(lowHeight_), std::bit_cast<float>(block_), workingToScene, 0, 0};
    reduce_.bind(list, reduce, gpuHandleOf(reduce_, 0).ptr, gpuHandleOf(reduce_, 1).ptr);
    list->Dispatch((lowWidth_ + 7) / 8, (lowHeight_ + 7) / 8, 1);
    tracker.uavBarrier(list, lowA_.Get());

    for (int pass = 0; pass < 2; ++pass) {
        auto* from = pass == 0 ? lowA_.Get() : lowB_.Get();
        auto* to = pass == 0 ? lowB_.Get() : lowA_.Get();
        tracker.transition(list, from, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, to, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float blur[8] = {std::bit_cast<float>(lowWidth_), std::bit_cast<float>(lowHeight_),
            std::bit_cast<float>(pass == 0 ? 1 : 0), std::bit_cast<float>(pass == 0 ? 0 : 1), 0, 0, 0, 0};
        blur_.bind(list, blur, gpuHandleOf(blur_, UINT(pass * 2)).ptr, gpuHandleOf(blur_, UINT(pass * 2 + 1)).ptr);
        list->Dispatch((lowWidth_ + 7) / 8, (lowHeight_ + 7) / 8, 1);
        tracker.uavBarrier(list, to);
    }

    tracker.transition(list, lowA_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float ring = float(std::max(1u, (height_ + 540u) / 1080u)) * 2.0f;
    const float apply[12] = {std::bit_cast<float>(width_), std::bit_cast<float>(height_),
        std::bit_cast<float>(lowWidth_), std::bit_cast<float>(lowHeight_), float(block_), workingToScene,
        std::clamp(settings.texture / 100.0f, -1.0f, 1.0f), std::clamp(settings.clarity / 100.0f, -1.0f, 1.0f),
        std::clamp(settings.dehaze / 100.0f, -1.0f, 1.0f), ring, 0, 0};
    apply_.bind(list, apply, gpuHandleOf(apply_, 0).ptr, gpuHandleOf(apply_, 2).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, target);
    return true;
}

void ColorDetailPass::close()
{
    reduce_ = {};
    blur_ = {};
    apply_ = {};
    copy_.Reset();
    lowA_.Reset();
    lowB_.Reset();
    target_ = nullptr;
    width_ = height_ = lowWidth_ = lowHeight_ = 0;
    failed_ = false;
}

} // namespace veyra::pipeline
