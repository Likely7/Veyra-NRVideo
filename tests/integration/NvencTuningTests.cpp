// NVENC recording tuning (2026-11-19; the options come from PushFPS e6d3e5c8, GPL-3.0-or-later):
// the live encoder opens with two-pass at quarter and full resolution and with psycho-visual
// adaptive quantization, H.264 and HEVC, and every variant returns 30 frames with a key frame
// and real bitstream data. On a machine without an NVIDIA adapter the test reports SKIP.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "veyra/sink/NvencLiveEncoder.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using veyra::sink::NvencLiveEncoder;

namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?');
    return s;
}

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t value = 0;
    std::string adapter;
    bool nvidia = false;

    bool create() {
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        ComPtr<IDXGIAdapter1> a;
        if (FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)))) return false;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        adapter = narrow(d.Description);
        nvidia = d.VendorId == 0x10DE;
        if (!nvidia) return true;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        return SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))) &&
               SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) &&
               (event = CreateEventW(nullptr, FALSE, FALSE, nullptr)) != nullptr;
    }
    bool submitAndWait() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (FAILED(queue->Signal(fence.Get(), ++value))) return false;
        if (fence->GetCompletedValue() < value) {
            fence->SetEventOnCompletion(value, event);
            WaitForSingleObject(event, 60000);
        }
        return fence->GetCompletedValue() >= value && SUCCEEDED(allocator->Reset()) &&
               SUCCEEDED(list->Reset(allocator.Get(), nullptr));
    }
};

ComPtr<ID3D12Resource> uploadBuffer(ID3D12Device* device, uint64_t bytes) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

// A moving diagonal gradient with neutral chroma, so the encoder sees motion between inputs.
bool fillNv12(Gpu& gpu, ID3D12Resource* texture, uint32_t w, uint32_t h, uint32_t phase, std::vector<ComPtr<ID3D12Resource>>& keep) {
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]{};
    UINT rows[2]{};
    UINT64 rowBytes[2]{}, total = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 2, 0, fp, rows, rowBytes, &total);
    auto staging = uploadBuffer(gpu.device.Get(), total);
    if (!staging) return false;
    uint8_t* mapped = nullptr;
    if (FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return false;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = mapped + fp[0].Offset + size_t(y) * fp[0].Footprint.RowPitch;
        for (uint32_t x = 0; x < w; ++x) row[x] = uint8_t(16 + ((x + y + phase * 8) % 220));
    }
    for (uint32_t y = 0; y < h / 2; ++y) std::memset(mapped + fp[1].Offset + size_t(y) * fp[1].Footprint.RowPitch, 128, w);
    staging->Unmap(0, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = texture;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    gpu.list->ResourceBarrier(1, &b);
    for (UINT plane = 0; plane < 2; ++plane) {
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = texture;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = plane;
        from.pResource = staging.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = fp[plane];
        gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    gpu.list->ResourceBarrier(1, &b);
    keep.push_back(staging);
    return true;
}

struct Variant {
    const char* name;
    bool hevc;
    uint32_t multipass;
    bool psychoAq;
};
} // namespace

int main() {
    Gpu gpu;
    if (!gpu.create()) { std::printf("FAIL D3D12 device\n"); return 1; }
    std::printf("adapter: %s\n", gpu.adapter.c_str());
    if (!gpu.nvidia) { std::printf("SKIP no NVIDIA adapter: NVENC tuning applies to NVIDIA only\n"); return 0; }
    constexpr uint32_t W = 1920, H = 1080, Inputs = 4, Frames = 30;
    std::vector<ComPtr<ID3D12Resource>> textures, staging;
    std::vector<ID3D12Resource*> inputs;
    for (uint32_t i = 0; i < Inputs; ++i) {
        auto t = veyra::pipeline::makeTexture(gpu.device.Get(), W, H, DXGI_FORMAT_NV12, false);
        if (!t || !fillNv12(gpu, t.Get(), W, H, i, staging)) { std::printf("FAIL NV12 input %u\n", i); return 1; }
        textures.push_back(t);
        inputs.push_back(t.Get());
    }
    check(gpu.submitAndWait(), "NV12 inputs uploaded");
    const Variant variants[] = {
        {"H.264 standard", false, 0, false},
        {"H.264 two-pass quarter", false, 1, false},
        {"H.264 two-pass full", false, 2, false},
        {"H.264 psycho-visual AQ", false, 0, true},
        {"H.264 two-pass full + AQ", false, 2, true},
        {"HEVC two-pass full + AQ", true, 2, true},
    };
    for (const auto& v : variants) {
        NvencLiveEncoder encoder;
        NvencLiveEncoder::Config config;
        config.width = W;
        config.height = H;
        config.hevc = v.hevc;
        config.fps = 60;
        config.bitrateMbps = 20;
        config.multipass = v.multipass;
        config.psychoAq = v.psychoAq;
        const bool opened = encoder.open(gpu.device.Get(), config, inputs);
        check(opened, std::string(v.name) + ": encoder opens");
        if (!opened) { std::printf("  error: %s\n", narrow(encoder.lastError()).c_str()); continue; }
        uint32_t submitted = 0, received = 0, keys = 0;
        uint64_t bytes = 0;
        bool ok = true;
        auto take = [&](bool wait) {
            NvencLiveEncoder::Packet packet;
            bool ready = false;
            if (!encoder.takeOldest(packet, wait, 5000, ready) || !ready) return false;
            ++received;
            bytes += packet.data.size();
            keys += packet.key ? 1 : 0;
            return true;
        };
        while (ok && submitted < Frames) {
            if (encoder.submit(submitted % Inputs, gpu.fence.Get(), gpu.value, int64_t(submitted) * 16667, submitted == 0)) { ++submitted; continue; }
            ok = take(true);  // every output buffer pending: take one first
        }
        while (ok && received < submitted) ok = take(true);
        std::printf("  %s: frames %u/%u, key frames %u, %llu bytes, headers %zu bytes\n", v.name, received, submitted, keys,
                    static_cast<unsigned long long>(bytes), encoder.headers().size());
        check(ok && received == Frames && keys >= 1 && bytes > Frames * 100, std::string(v.name) + ": 30 frames with a key frame and data");
        encoder.close();
    }
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
