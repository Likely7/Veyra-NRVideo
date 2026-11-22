// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS src/pipeline/PushHudLock.cpp (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
#include "veyra/pipeline/HudLock.h"
#include <bit>
#include <format>

namespace veyra::pipeline {
namespace {
constexpr DXGI_FORMAT kStateFormat = DXGI_FORMAT_R16G16_FLOAT;  // x: stillness 0..1, y: held contrast
constexpr DXGI_FORMAT kMaskFormat = DXGI_FORMAT_R8_UNORM;
// Descriptor layouts. update: per parity { t0 current, t1 previous, u0 state }.
// dynamic: per parity { t0 current, t1 state, t2 previous, u0 mask }.
// compose: per (slot, held parity) { t0 generated, t1 held real, t2 state, t3 mask,
//                                   t4 other real, u0 scratch }.
constexpr UINT kUpdateStride = 3, kDynamicStride = 4, kComposeStride = 6;
}

std::string HudLock::describe() const {
    return std::format("HUD lock {}x{} mode={} static={} dynamic={} generated={}",
                       width_, height_, outputMode_, staticLock_, dynamicActive(), generatedCount_);
}

bool HudLock::initialize(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                         unsigned outputMode, bool staticLock, bool dynamicProtection) {
    if (!device || !width || !height || format == DXGI_FORMAT_UNKNOWN) return false;
    width_ = width; height_ = height; format_ = format; outputMode_ = outputMode;
    staticLock_ = staticLock;
    std::vector<uint8_t> cs;
    if (dynamicProtection) {
        cellSize_ = std::clamp((height_ + 269u) / 270u, 2u, 8u);
        maskWidth_ = (width_ + cellSize_ - 1) / cellSize_;
        maskHeight_ = (height_ + cellSize_ - 1) / cellSize_;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT caps{kMaskFormat};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &caps, sizeof caps)) &&
            (caps.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) &&
            dynamic_.loadShader("HudLockDynamic.dxil", cs) &&
            dynamic_.create(device, cs, 2 * kDynamicStride, 3, 1, kConstants) &&
            fill_.loadShader("HudLockFill.dxil", cs) && fill_.create(device, cs, 2, 1, 1, 4)) {
            dynamicMask_ = makeTexture(device, maskWidth_, maskHeight_, kMaskFormat, true);
            if (dynamicMask_) protectionMask_ = makeTexture(device, maskWidth_, maskHeight_, kMaskFormat, true);
        }
        if (!dynamicActive()) {
            dynamicMask_.Reset();
            veyra::log::warn("hud-lock", "dynamic HUD mask unavailable");
        }
    }
    if (!staticLock_ && !dynamicActive()) return false;
    state_ = makeTexture(device, width_, height_, kStateFormat, true);
    scratch_ = makeTexture(device, width_, height_, format_, true);
    if (!state_ || !scratch_) return false;
    if (!update_.loadShader("HudLockUpdate.dxil", cs) || !update_.create(device, cs, 2 * kUpdateStride, 2, 1, kConstants)) return false;
    if (!compose_.loadShader("HudLockCompose.dxil", cs) ||
        !compose_.create(device, cs, kMaxGenerated * 2 * kComposeStride, 5, 1, kConstants)) return false;
    veyra::log::info("hud-lock", std::format("created {}", describe()));
    return true;
}

void HudLock::createViews(ID3D12Device* device, DescriptorStager& stager, ID3D12Resource* const real[2],
                              ID3D12Resource* const* generated, unsigned generatedCount) {
    real_[0] = real[0]; real_[1] = real[1];
    generatedCount_ = std::min<unsigned>(generatedCount, kMaxGenerated);
    for (unsigned i = 0; i < generatedCount_; ++i) generated_[i] = generated[i];
    D3D12_SHADER_RESOURCE_VIEW_DESC maskSrv{};
    maskSrv.Format = kMaskFormat;
    maskSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    maskSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    maskSrv.Texture2D.MipLevels = 1;
    if (dynamicActive()) {
        stager.stageSrv(dynamicMask_.Get(), &maskSrv, fill_.heap.Get(), 0);
        makeUav(device, protectionMask_.Get(), kMaskFormat, cpuHandleOf(fill_, 1));
    }
    for (unsigned parity = 0; parity < 2; ++parity) {
        const UINT base = parity * kUpdateStride;
        stager.stageSrv(real_[parity], nullptr, update_.heap.Get(), base);
        stager.stageSrv(real_[1 - parity], nullptr, update_.heap.Get(), base + 1);
        makeUav(device, state_.Get(), kStateFormat, cpuHandleOf(update_, base + 2));
        if (dynamicActive()) {
            const UINT maskBase = parity * kDynamicStride;
            stager.stageSrv(real_[parity], nullptr, dynamic_.heap.Get(), maskBase);
            stager.stageSrv(state_.Get(), nullptr, dynamic_.heap.Get(), maskBase + 1);
            stager.stageSrv(real_[1 - parity], nullptr, dynamic_.heap.Get(), maskBase + 2);
            makeUav(device, dynamicMask_.Get(), kMaskFormat, cpuHandleOf(dynamic_, maskBase + 3));
        }
    }
    for (unsigned slot = 0; slot < generatedCount_; ++slot)
        for (unsigned held = 0; held < 2; ++held) {
            const UINT base = (slot * 2 + held) * kComposeStride;
            stager.stageSrv(generated_[slot], nullptr, compose_.heap.Get(), base);
            stager.stageSrv(real_[held], nullptr, compose_.heap.Get(), base + 1);
            stager.stageSrv(state_.Get(), nullptr, compose_.heap.Get(), base + 2);
            stager.stageSrv(protectionMask_.Get(), &maskSrv, compose_.heap.Get(), base + 3);
            stager.stageSrv(real_[1 - held], nullptr, compose_.heap.Get(), base + 4);
            makeUav(device, scratch_.Get(), format_, cpuHandleOf(compose_, base + 5));
        }
}

bool HudLock::update(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned parity, bool reset) {
    if (!list || parity > 1 || !real_[0] || !real_[1]) return false;
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(width_); c[1] = std::bit_cast<float>(height_);
    c[2] = std::bit_cast<float>(uint32_t(reset ? 1 : 0)); c[3] = std::bit_cast<float>(uint32_t(outputMode_));
    tracker.transition(list, real_[parity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, real_[1 - parity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT base = parity * kUpdateStride;
    update_.bind(list, c, gpuHandleOf(update_, base).ptr, gpuHandleOf(update_, base + 2).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, state_.Get());
    if (dynamicActive()) {
        c[4] = std::bit_cast<float>(cellSize_);
        c[5] = std::bit_cast<float>(maskWidth_);
        c[6] = std::bit_cast<float>(maskHeight_);
        tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, dynamicMask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const UINT maskBase = parity * kDynamicStride;
        dynamic_.bind(list, c, gpuHandleOf(dynamic_, maskBase).ptr, gpuHandleOf(dynamic_, maskBase + 3).ptr);
        list->Dispatch((maskWidth_ + 7) / 8, (maskHeight_ + 7) / 8, 1);
        tracker.uavBarrier(list, dynamicMask_.Get());
        tracker.transition(list, dynamicMask_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list, protectionMask_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float fillConstants[] = {std::bit_cast<float>(maskWidth_), std::bit_cast<float>(maskHeight_), c[2], 0};
        fill_.bind(list, fillConstants, gpuHandleOf(fill_, 0).ptr, gpuHandleOf(fill_, 1).ptr);
        list->Dispatch((maskWidth_ + 7) / 8, (maskHeight_ + 7) / 8, 1);
        tracker.uavBarrier(list, protectionMask_.Get());
        tracker.transition(list, dynamicMask_.Get(), D3D12_RESOURCE_STATE_COMMON);
        tracker.transition(list, protectionMask_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[parity], D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[1 - parity], D3D12_RESOURCE_STATE_COMMON);
    return true;
}

bool HudLock::compose(ID3D12GraphicsCommandList* list, StateTracker& tracker, unsigned slot, unsigned heldParity,
                      bool useNewerHud) {
    if (!list || slot >= generatedCount_ || heldParity > 1 || !generated_[slot] || !real_[heldParity]) return false;
    auto* generated = generated_[slot];
    float c[kConstants]{};
    c[0] = std::bit_cast<float>(width_); c[1] = std::bit_cast<float>(height_);
    c[2] = std::bit_cast<float>(uint32_t(outputMode_));
    c[3] = std::bit_cast<float>(uint32_t(staticLock_));
    c[4] = std::bit_cast<float>(dynamicActive() ? cellSize_ : 0u);
    const bool useNewer = dynamicActive() && useNewerHud;
    c[5] = std::bit_cast<float>(uint32_t(useNewer));
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (dynamicActive()) tracker.transition(list, protectionMask_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (useNewer) tracker.transition(list, real_[1 - heldParity], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT base = (slot * 2 + heldParity) * kComposeStride;
    compose_.bind(list, c, gpuHandleOf(compose_, base).ptr, gpuHandleOf(compose_, base + 5).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, scratch_.Get());
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(generated, scratch_.Get());
    tracker.transition(list, generated, D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, scratch_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_COMMON);
    tracker.transition(list, real_[heldParity], D3D12_RESOURCE_STATE_COMMON);
    if (dynamicActive()) tracker.transition(list, protectionMask_.Get(), D3D12_RESOURCE_STATE_COMMON);
    if (useNewer) tracker.transition(list, real_[1 - heldParity], D3D12_RESOURCE_STATE_COMMON);
    return true;
}
}
