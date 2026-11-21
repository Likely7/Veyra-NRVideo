// SPDX-License-Identifier: GPL-3.0-only
#include "NisScaler.h"
#include "veyra/gfx/CommandSlotRing.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include "../../third_party/nis/NIS_Config.h"

namespace veyra::pipeline {
namespace {
constexpr DXGI_FORMAT kWorking = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kWeights = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr float kSharpness = 0.25f;
constexpr unsigned kBlockWidth = 32;
constexpr unsigned kBlockHeight = 24;
}

Extent NisScaler::inputExtent(Extent input, Extent output) {
    // NVScaler accepts 1x through 2x on each axis. Bring larger enlargements
    // into that range with the same linear resize used by the base graph.
    return {std::clamp(input.width, (output.width + 1) / 2, output.width),
            std::clamp(input.height, (output.height + 1) / 2, output.height)};
}

bool NisScaler::initialize(ID3D12Device* device, gfx::CommandSlotRing& ring, Extent source, Extent output, bool hdr) {
    device_ = device;
    source_ = source;
    output_ = output;
    input_ = inputExtent(source_, output_);
    hdr_ = hdr;
    NISConfig config{};
    if (!NVScalerUpdateConfig(config, kSharpness, 0, 0, input_.width, input_.height, input_.width, input_.height,
                             0, 0, output_.width, output_.height, output_.width, output_.height,
                             hdr_ ? NISHDRMode::Linear : NISHDRMode::None)) return false;
    static_assert(offsetof(NISConfig, reserved1) + sizeof(float) == sizeof(constants_));
    std::memcpy(constants_.data(), &config, sizeof(constants_));

    const unsigned slots = ring.slotCount();
    std::vector<uint8_t> bytes;
    const char* full = hdr_ ? "NisHdr.dxil" : "NisSdr.dxil";
    const char* half = hdr_ ? "NisHdrFp16.dxil" : "NisSdrFp16.dxil";
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options{};
    const bool supportsFp16 = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options))) &&
                              options.Native16BitShaderOpsSupported;
    const bool fp16 = supportsFp16 && scaler_.loadShader(half, bytes) &&
                      scaler_.create(device, bytes, slots * 4, 3, 1, unsigned(constants_.size()), 0, true);
    if (!fp16 && (!scaler_.loadShader(full, bytes) ||
                  !scaler_.create(device, bytes, slots * 4, 3, 1, unsigned(constants_.size()), 0, true))) return false;
    if (!descriptors_.initialize(device, 4)) return false;
    if (!hdr_ || source_ != input_) {
        prepared_ = makeTexture(device, input_.width, input_.height, kWorking, true);
        if (!prepared_ || !prepare_.loadShader("ScaleBlit.dxil", bytes) ||
            !prepare_.create(device, bytes, slots * 2, 1)) return false;
    }
    if (!uploadCoefficients(ring)) return false;
    for (unsigned slot = 0; slot < slots; ++slot) {
        for (unsigned index = 0; index < coefficients_.size(); ++index)
            descriptors_.stageSrv(coefficients_[index].Get(), nullptr, scaler_.heap.Get(), slot * 4 + 1 + index);
    }
    log::info("nis", std::format("ready {}x{} (filter input {}x{}) -> {}x{} {} {} sharpness={:.2f}",
        source_.width, source_.height, input_.width, input_.height, output_.width, output_.height,
        hdr_ ? "linear HDR" : "SDR", fp16 ? "FP16" : "FP32", kSharpness));
    return true;
}

bool NisScaler::uploadCoefficients(gfx::CommandSlotRing& ring) {
    // Each phase is eight floats split across two RGBA32F texels.
    constexpr unsigned rowBytes = 2 * 4 * sizeof(float);
    constexpr unsigned pitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    constexpr unsigned tableBytes = pitch * unsigned(kPhaseCount);
    for (auto& texture : coefficients_) {
        texture = makeTexture(device_, 2, unsigned(kPhaseCount), kWeights, false);
        if (!texture) return false;
    }
    upload_ = makeUploadBuffer(device_, tableBytes * coefficients_.size());
    if (!upload_) return false;
    D3D12_RANGE noRead{};
    void* mapped = nullptr;
    if (FAILED(upload_->Map(0, &noRead, &mapped))) return false;
    const void* tables[] = {coef_scale, coef_usm};
    for (unsigned index = 0; index < coefficients_.size(); ++index) {
        for (unsigned row = 0; row < kPhaseCount; ++row) {
            std::memcpy(static_cast<char*>(mapped) + index * tableBytes + row * pitch,
                        static_cast<const char*>(tables[index]) + row * rowBytes, rowBytes);
        }
    }
    upload_->Unmap(0, nullptr);

    Status status = Status::Ok;
    unsigned slot = 0;
    auto* list = ring.acquireNext(slot, status);
    if (!list) return false;
    StateTracker tracker;
    for (unsigned index = 0; index < coefficients_.size(); ++index) {
        auto* texture = coefficients_[index].Get();
        tracker.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = texture;
        src.pResource = upload_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = index * tableBytes;
        src.PlacedFootprint.Footprint = {kWeights, 2, unsigned(kPhaseCount), 1, pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.transition(list, texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
    upload_.Reset();
    return true;
}

void NisScaler::dispatch(ID3D12GraphicsCommandList* list, uint32_t slot, StateTracker& tracker,
                          ID3D12Resource* input, ID3D12Resource* output) {
    tracker.transition(list, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (prepared_) {
        const auto base = slot * 2;
        descriptors_.stageSrv(input, nullptr, prepare_.heap.Get(), base);
        makeUav(device_, prepared_.Get(), kWorking, cpuHandleOf(prepare_, base + 1));
        const float resize[8] = {std::bit_cast<float>(source_.width), std::bit_cast<float>(source_.height),
            std::bit_cast<float>(input_.width), std::bit_cast<float>(input_.height), hdr_ ? 0.0f : 1.0f, 0, 0, 0};
        tracker.transition(list, prepared_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        prepare_.bind(list, resize, gpuHandleOf(prepare_, base).ptr, gpuHandleOf(prepare_, base + 1).ptr);
        list->Dispatch((input_.width + 15) / 16, (input_.height + 15) / 16, 1);
        input = prepared_.Get();
        tracker.transition(list, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    const auto base = slot * 4;
    descriptors_.stageSrv(input, nullptr, scaler_.heap.Get(), base);
    makeUav(device_, output, kWorking, cpuHandleOf(scaler_, base + 3));
    tracker.transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    scaler_.bind(list, constants_.data(), gpuHandleOf(scaler_, base).ptr, gpuHandleOf(scaler_, base + 3).ptr);
    list->Dispatch((output_.width + kBlockWidth - 1) / kBlockWidth,
                   (output_.height + kBlockHeight - 1) / kBlockHeight, 1);
    tracker.transition(list, output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

} // namespace veyra::pipeline
