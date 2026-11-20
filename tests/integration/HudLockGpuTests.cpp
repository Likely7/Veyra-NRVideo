// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS tests/integration/PushGpuTests.cpp (commit 3be641ca,
// the harness and testHudLock only) and adapted: renamed, standalone executable.
//
// Live HUD Lock (pipeline/HudLock.h) on a real D3D12 device (hardware, or WARP when no graphics
// card is present): after a second of a still HUD over a moving picture, a generated frame takes
// the HUD pixels from the held real frame and keeps its own everywhere else; a HUD that
// disappears is released on that frame; a reset starts over. VEYRA_HUD_LOCK_GPU_DEBUG=1 enables
// the D3D12 debug layer (when installed) and fails on its errors.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "veyra/pipeline/HudLock.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <format>
#include <vector>

using namespace veyra;
using Microsoft::WRL::ComPtr;

namespace {
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t value = 0;
    bool debug = false;
    std::string adapter;

    bool create() {
        if (GetEnvironmentVariableW(L"VEYRA_HUD_LOCK_GPU_DEBUG", nullptr, 0) > 0) {
            ComPtr<ID3D12Debug> d;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) { d->EnableDebugLayer(); debug = true; }
            else std::printf("NOTE D3D12 debug layer not installed; running without it\n");
        }
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        for (UINT i = 0; !device; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) adapter = narrow(d.Description);
        }
        if (!device) {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return false;
            adapter = "WARP";
        }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        return SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) &&
               SUCCEEDED(list->Close()) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) &&
               (event = CreateEventW(nullptr, FALSE, FALSE, nullptr)) != nullptr;
    }
    static std::string narrow(const wchar_t* w) {
        std::string s; for (; *w; ++w) s.push_back(*w < 128 ? char(*w) : '?'); return s;
    }
    bool begin() { return SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(), nullptr)); }
    bool submit() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        return SUCCEEDED(queue->Signal(fence.Get(), ++value));
    }
    bool wait() {
        if (fence->GetCompletedValue() < value) { fence->SetEventOnCompletion(value, event); WaitForSingleObject(event, 60000); }
        return fence->GetCompletedValue() >= value && SUCCEEDED(device->GetDeviceRemovedReason());
    }
    bool submitAndWait() { return submit() && wait(); }
    bool debugErrors() {
        if (!debug) return false;
        ComPtr<ID3D12InfoQueue> info;
        if (FAILED(device.As(&info))) return false;
        bool errors = false;
        for (UINT64 i = 0, n = info->GetNumStoredMessages(); i < n; ++i) {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
            std::vector<char> buffer(size); auto* m = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (SUCCEEDED(info->GetMessage(i, m, &size)) && m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::printf("D3D12 %s\n", m->pDescription); errors = true;
            }
        }
        info->ClearStoredMessages();
        return errors;
    }
};

ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = type;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    const auto state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

// A texture plane: bytes per pixel `bpp`, filled or read through a buffer.
struct Plane {
    ComPtr<ID3D12Resource> texture;
    DXGI_FORMAT format;
    uint32_t w, h, bpp;
    UINT pitch() const { return (w * bpp + 255) & ~255u; }
};

// Records an upload of `pixels` (tight rows) into the plane; the texture ends in `after`.
ComPtr<ID3D12Resource> recordFill(Gpu& gpu, const Plane& p, const std::vector<uint8_t>& pixels, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    auto upload = makeBuffer(gpu.device.Get(), uint64_t(p.pitch()) * p.h, D3D12_HEAP_TYPE_UPLOAD);
    if (!upload) return nullptr;
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < p.h; ++y) std::memcpy(mapped + size_t(y) * p.pitch(), pixels.data() + size_t(y) * p.w * p.bpp, size_t(p.w) * p.bpp);
    upload->Unmap(0, nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = p.texture.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (before != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = before; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; gpu.list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint = {p.format, p.w, p.h, 1, p.pitch()};
    to.pResource = p.texture.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (after != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = after; gpu.list->ResourceBarrier(1, &b);
    }
    return upload;
}

// Records a readback of the plane (currently in `state`, left there) into a new buffer.
ComPtr<ID3D12Resource> recordRead(Gpu& gpu, const Plane& p, D3D12_RESOURCE_STATES state) {
    auto readback = makeBuffer(gpu.device.Get(), uint64_t(p.pitch()) * p.h, D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = p.texture.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = state; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; gpu.list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = p.texture.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint = {p.format, p.w, p.h, 1, p.pitch()};
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter); gpu.list->ResourceBarrier(1, &b);
    }
    return readback;
}

std::vector<uint8_t> mapRead(const Plane& p, ID3D12Resource* readback) {
    std::vector<uint8_t> out(size_t(p.w) * p.h * p.bpp);
    uint8_t* mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return {};
    for (uint32_t y = 0; y < p.h; ++y) std::memcpy(out.data() + size_t(y) * p.w * p.bpp, mapped + size_t(y) * p.pitch(), size_t(p.w) * p.bpp);
    const D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return out;
}

void testHudLock(Gpu& gpu) {
    constexpr uint32_t W = 256, H = 144;
    constexpr DXGI_FORMAT F = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto* device = gpu.device.Get();
    Plane real[2]{{pipeline::makeTexture(device, W, H, F, true), F, W, H, 4}, {pipeline::makeTexture(device, W, H, F, true), F, W, H, 4}};
    Plane generated{pipeline::makeTexture(device, W, H, F, true), F, W, H, 4};
    check(real[0].texture && real[1].texture && generated.texture, "hud lock: textures created");
    pipeline::HudLock lock;
    check(lock.initialize(device, W, H, F, 0), "hud lock: created");
    pipeline::DescriptorStager stager;
    check(stager.initialize(device, 64), "hud lock: staging heap");
    ID3D12Resource* reals[2] = {real[0].texture.Get(), real[1].texture.Get()};
    ID3D12Resource* gens[1] = {generated.texture.Get()};
    lock.createViews(device, stager, reals, gens, 1);
    // HUD: x 20..100, y 10..40, high-contrast stripes that never move. Everything else moves.
    auto hudAt = [](uint32_t x, uint32_t y) { return x >= 20 && x < 100 && y >= 10 && y < 40; };
    auto picture = [&](int shift, bool hud) {
        std::vector<uint8_t> px(size_t(W) * H * 4);
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t v;
                if (hud && hudAt(x, y)) v = ((x / 3) % 2) ? 230 : 25;
                else {
                    uint32_t h = uint32_t(int(x) + shift) * 374761393u + uint32_t(int(y) + shift / 3) * 668265263u;
                    h = (h ^ (h >> 13)) * 1274126177u;
                    v = uint8_t(40 + (h >> 25));
                }
                uint8_t* p = px.data() + (size_t(y) * W + x) * 4;
                p[0] = p[1] = p[2] = v; p[3] = 255;
            }
        return px;
    };
    pipeline::StateTracker states;
    std::vector<uint8_t> last;
    auto realFrame = [&](int i, bool hud, bool reset) {
        const unsigned parity = unsigned(i) & 1u;
        gpu.begin();
        last = picture(i * 5, hud);
        auto up = recordFill(gpu, real[parity], last, states.get(real[parity].texture.Get()), D3D12_RESOURCE_STATE_COMMON);
        states.set(real[parity].texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        const bool ok = lock.update(gpu.list.Get(), states, parity, reset);
        return ok && gpu.submitAndWait();
    };
    auto generatedFrame = [&](unsigned held, std::vector<uint8_t>& out) {
        gpu.begin();
        auto up = recordFill(gpu, generated, std::vector<uint8_t>(size_t(W) * H * 4, 128), states.get(generated.texture.Get()), D3D12_RESOURCE_STATE_COMMON);
        states.set(generated.texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        const bool ok = lock.compose(gpu.list.Get(), states, 0, held);
        auto rb = recordRead(gpu, generated, states.get(generated.texture.Get()));
        if (!ok || !gpu.submitAndWait()) return false;
        out = mapRead(generated, rb.Get());
        return true;
    };
    bool ran = true;
    int i = 0;
    for (; i < 60; ++i) ran = ran && realFrame(i, true, i == 0);
    check(ran, "hud lock: 60 real frames update the state");
    std::vector<uint8_t> out;
    check(generatedFrame(unsigned(i - 1) & 1u, out), "hud lock: a generated frame is composed");
    int hudHeld = 0, hudTotal = 0, bgKept = 0, bgTotal = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const size_t k = (size_t(y) * W + x) * 4;
            if (hudAt(x, y)) { ++hudTotal; hudHeld += out[k] == last[k]; }
            else if (x >= 110 || y >= 50) { ++bgTotal; bgKept += out[k] == 128; }
        }
    std::printf("INFO hud lock: HUD pixels from the real frame %d/%d, moving pixels kept %d/%d\n", hudHeld, hudTotal, bgKept, bgTotal);
    check(hudHeld >= hudTotal * 95 / 100, "hud lock: the HUD comes from the held real frame");
    check(bgKept == bgTotal, "hud lock: the moving picture keeps the generated pixels");
    // The HUD goes away (a menu closes): released on that very frame.
    check(realFrame(i, false, false), "hud lock: a frame without the HUD runs");
    check(generatedFrame(unsigned(i) & 1u, out), "hud lock: composed after the HUD went away");
    int released = 0;
    for (uint32_t y = 12; y < 38; ++y) for (uint32_t x = 22; x < 98; ++x) released += out[(size_t(y) * W + x) * 4] == 128;
    check(released >= 26 * 76 * 95 / 100, "hud lock: a HUD that moves or disappears is released at once (" + std::to_string(released) + "/1976)");
    // A reset starts over: nothing is locked right after it.
    ++i;
    check(realFrame(i, true, true), "hud lock: reset runs");
    check(generatedFrame(unsigned(i) & 1u, out), "hud lock: composed after a reset");
    int afterReset = 0;
    for (size_t k = 0; k < out.size(); k += 4) afterReset += out[k] != 128;
    check(afterReset == 0, "hud lock: nothing is locked right after a reset");
    check(!gpu.debugErrors(), "hud lock: no D3D12 debug errors");
}

} // namespace

int main() {
    Gpu gpu;
    if (!gpu.create()) { std::printf("FAIL D3D12 device\n"); return 1; }
    std::printf("adapter: %s%s\n", gpu.adapter.c_str(), gpu.debug ? " (debug layer)" : "");
    testHudLock(gpu);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
