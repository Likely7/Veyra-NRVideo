// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/engine/ConsoleAimReadback.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/pipeline/FrameBatch.h"
#include "veyra/remoteplay/AimTest.h"
#include <atomic>
#include <cstring>

namespace veyra::engine {
namespace {
using Microsoft::WRL::ComPtr;
using remoteplay::AimImage;
constexpr uint64_t kBytes = AimImage::width * AimImage::height;

bool allocate(ID3D12Device* device, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& output) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (heap == D3D12_HEAP_TYPE_DEFAULT) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_HEAP_PROPERTIES props{heap, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    const auto state = heap == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_DEST;
    const auto hr = device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&output));
    if (FAILED(hr)) log::warn("console-aim", std::format("allocate probe buffer hr=0x{:X}", unsigned(hr)));
    return SUCCEEDED(hr);
}
}

struct ConsoleAimReadback::State {
    struct Slot {
        ComPtr<ID3D12Resource> luma, readback;
        ComPtr<ID3D12Fence> fence;
        std::weak_ptr<remoteplay::AimTest> destination;
        uint64_t fenceValue = 0;
        int64_t presentUs = 0;
    };
    std::atomic<std::shared_ptr<remoteplay::AimTest>> probe;
    ComPtr<ID3D12Device> device;
    pipeline::ComputePass pass;
    pipeline::DescriptorStager stager;
    std::array<Slot, 3> slots;
    int recording = -1;
    int pending = 0;
    int64_t nextCapture = 0;
    std::weak_ptr<remoteplay::AimTest> previous;
    bool failed = false;

    bool open(ID3D12Device* d) {
        if (device.Get() == d) return !failed;
        if (std::any_of(slots.begin(), slots.end(), [](const auto& s) { return bool(s.fence); })) return false;
        device = d;
        slots = {};
        pass = {};
        std::vector<uint8_t> shader;
        failed = !pipeline::loadShaderBytes("ConsoleAimLuma.dxil", shader) ||
            !pass.create(d, shader, UINT(slots.size() * 2), 1, 1, 1, 0, true) ||
            !stager.initialize(d, 1);
        for (size_t i = 0; i < slots.size() && !failed; ++i) {
            auto& s = slots[i];
            failed = !allocate(d, D3D12_HEAP_TYPE_DEFAULT, s.luma) || !allocate(d, D3D12_HEAP_TYPE_READBACK, s.readback);
            if (failed) break;
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Format = DXGI_FORMAT_R32_UINT;
            view.Buffer.NumElements = UINT(kBytes / 4);
            const auto cpu = stager.heap()->GetCPUDescriptorHandleForHeapStart();
            d->CreateUnorderedAccessView(s.luma.Get(), nullptr, &view, cpu);
            d->CopyDescriptorsSimple(1, {pass.heap->GetCPUDescriptorHandleForHeapStart().ptr +
                (i * 2 + 1) * pass.increment}, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (failed) log::warn("console-aim", "luminance probe unavailable");
        else log::info("console-aim", "256x144 frame probe enabled; three nonblocking readback slots");
        return !failed;
    }

    void collect() {
        // Preserve presentation order even when more than one fence completes.
        std::array<Slot*, 3> readable{&slots[0], &slots[1], &slots[2]};
        std::sort(readable.begin(), readable.end(), [](const auto* a, const auto* b) { return a->presentUs < b->presentUs; });
        for (auto* slot : readable) {
            if (!slot->fence) continue;
            const auto completed = slot->fence->GetCompletedValue();
            if (completed < slot->fenceValue) continue;
            if (const auto destination = slot->destination.lock(); destination && slot->presentUs) {
                if (completed == UINT64_MAX) {
                    destination->cancel();
                    slot->presentUs = 0;
                }
            }
            if (const auto destination = slot->destination.lock(); destination && slot->presentUs) {
                void* pixels = nullptr;
                const D3D12_RANGE range{0, kBytes};
                const auto hr = slot->readback->Map(0, &range, &pixels);
                if (SUCCEEDED(hr) && pixels) {
                    AimImage image;
                    image.presentUs = slot->presentUs;
                    std::memcpy(image.luma.data(), pixels, kBytes);
                    const D3D12_RANGE written{};
                    slot->readback->Unmap(0, &written);
                    destination->frame(std::move(image));
                } else {
                    log::warn("console-aim", std::format("Map failed hr=0x{:X}", unsigned(hr)));
                    destination->cancel();
                }
            }
            slot->fence.Reset();
            slot->presentUs = 0;
            slot->destination.reset();
            --pending;
        }
    }
};

ConsoleAimReadback::ConsoleAimReadback() : p_(std::make_unique<State>()) {}
ConsoleAimReadback::~ConsoleAimReadback() = default;
void ConsoleAimReadback::setProbe(std::shared_ptr<remoteplay::AimTest> probe) { p_->probe.store(std::move(probe)); }

bool ConsoleAimReadback::record(ID3D12GraphicsCommandList* list, const Frame& frame) {
    auto& p = *p_;
    const auto probe = p.probe.load();
    if (p.pending) p.collect();
    if (!probe || !probe->needsFrames() || !frame.texture || frame.width < AimImage::width) return false;
    if (frame.generated && probe->mode() != remoteplay::AimTest::Mode::Latency) return false;
    const auto at = remoteplay::monotonic100ns() / 10;
    if (p.previous.lock() != probe) {
        p.previous = probe;
        p.nextCapture = 0;
    }
    if (at < p.nextCapture) return false;
    p.nextCapture = at + 12500; // Cap the probe at 80 images/s even when FG submits faster.
    ComPtr<ID3D12Device> device;
    if (FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&device))) || !p.open(device.Get())) {
        if (p.failed) probe->cancel();
        return false;
    }
    const auto found = std::find_if(p.slots.begin(), p.slots.end(), [](const auto& slot) { return !slot.fence; });
    if (found == p.slots.end()) return false;
    const auto i = size_t(found - p.slots.begin());
    auto& s = *found;
    const auto offset = i * 2 * p.pass.increment;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = frame.format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    p.stager.stageSrv(frame.texture, &view, p.pass.heap.Get(), UINT(i * 2));

    pipeline::StateTracker states;
    states.transition(list, frame.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    states.transition(list, s.luma.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float constants[]{float(frame.hdrMode)};
    const auto base = p.pass.heap->GetGPUDescriptorHandleForHeapStart().ptr + offset;
    p.pass.bind(list, constants, base, base + p.pass.increment);
    list->Dispatch(8, 18, 1);
    states.transition(list, s.luma.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(s.readback.Get(), 0, s.luma.Get(), 0, kBytes);
    states.transition(list, s.luma.Get(), D3D12_RESOURCE_STATE_COMMON);
    states.transition(list, frame.texture, D3D12_RESOURCE_STATE_COMMON);
    p.recording = int(i);
    s.destination = probe;
    return true;
}
void ConsoleAimReadback::submitted(ID3D12Fence* fence, uint64_t value) {
    if (p_->recording < 0) return;
    auto& slot = p_->slots[p_->recording];
    slot.fence = fence;
    slot.fenceValue = value;
    ++p_->pending;
}
void ConsoleAimReadback::presented(int64_t host100ns) {
    if (p_->recording < 0) return;
    p_->slots[p_->recording].presentUs = host100ns / 10;
    p_->recording = -1;
}
void ConsoleAimReadback::discarded() {
    if (p_->recording < 0) return;
    p_->slots[p_->recording].destination.reset();
    p_->recording = -1;
}
} // namespace veyra::engine
