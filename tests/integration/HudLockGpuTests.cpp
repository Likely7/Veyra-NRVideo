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
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include "veyra/pipeline/HudLock.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <algorithm>
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

void setPixel(uint8_t* pixel, DXGI_FORMAT format, float r, float g, float b, float alpha) {
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        using DirectX::PackedVector::XMConvertFloatToHalf;
        const uint16_t half[] = {XMConvertFloatToHalf(r), XMConvertFloatToHalf(g),
                                 XMConvertFloatToHalf(b), XMConvertFloatToHalf(alpha)};
        std::memcpy(pixel, half, sizeof half);
    } else if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        const auto rgb10 = [](float value) { return uint32_t(std::lround(value * 1023)); };
        const uint32_t packed = rgb10(r) | (rgb10(g) << 10) | (rgb10(b) << 20) |
                                (uint32_t(std::lround(alpha * 3)) << 30);
        std::memcpy(pixel, &packed, sizeof packed);
    } else {
        pixel[0] = uint8_t(std::lround(r * 255));
        pixel[1] = uint8_t(std::lround(g * 255));
        pixel[2] = uint8_t(std::lround(b * 255));
        pixel[3] = uint8_t(std::lround(alpha * 255));
    }
}

struct DynamicHudFixture {
    Gpu& gpu;
    pipeline::HudLock lock;
    pipeline::StateTracker states;
    Plane real[2]{};
    Plane generated{};
    std::vector<uint8_t> placeholder;
    std::vector<uint8_t> uploaded[2];

    bool initialize(uint32_t w, uint32_t h, DXGI_FORMAT format, unsigned outputMode,
                    bool lockStatic = false, bool dynamic = true) {
        auto* device = gpu.device.Get();
        const uint32_t bpp = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u;
        for (auto* plane : {&real[0], &real[1], &generated}) {
            *plane = {pipeline::makeTexture(device, w, h, format, true), format, w, h, bpp};
            if (!plane->texture) return false;
        }
        if (!lock.initialize(device, w, h, format, outputMode, lockStatic, dynamic) ||
            lock.dynamicActive() != dynamic) return false;
        pipeline::DescriptorStager stager;
        if (!stager.initialize(device, 64)) return false;
        ID3D12Resource* reals[]{real[0].texture.Get(), real[1].texture.Get()};
        ID3D12Resource* gens[]{generated.texture.Get()};
        lock.createViews(device, stager, reals, gens, 1);
        placeholder.resize(size_t(w) * h * bpp);
        for (size_t k = 0; k < placeholder.size(); k += bpp)
            setPixel(placeholder.data() + k, format, 0.49f, 0.33f, 0.38f, 1.0f);
        return true;
    }

    // A changing meter along thin fixed rails over a translating picture.
    // Farther down, equally bright rails and matching insets surround moving
    // scenery that never exposes the inset colour.
    std::vector<uint8_t> picture(unsigned index, bool hud, bool vertical = false) const {
        const auto& p = real[0];
        std::vector<uint8_t> pixels(size_t(p.w) * p.h * p.bpp);
        const uint32_t cell = std::clamp((p.h + 269u) / 270u, 2u, 8u);
        for (uint32_t y = 0; y < p.h; ++y)
            for (uint32_t x = 0; x < p.w; ++x) {
                const uint32_t u = vertical ? y : x;
                const uint32_t v = vertical ? x : y;
                const uint32_t longSide = vertical ? p.h : p.w;
                const uint32_t shortSide = vertical ? p.w : p.h;
                const uint32_t start = shortSide / 8;
                const uint32_t end = start + 6 * cell;
                const uint32_t world = shortSide * 5 / 8;
                uint32_t hash = (x + 7 * index) * 374761393u + (y + 3 * index) * 668265263u;
                hash = (hash ^ (hash >> 13)) * 1274126177u;
                float r = 0.18f + float(hash >> 25) / 640.0f;
                float g = r + 0.06f, b = r + 0.08f;

                if (u >= longSide / 9 && u < longSide / 2) {
                    if (hud && v >= start && v <= end) {
                        const bool rail = v == start || v == end;
                        const bool fill = v >= start + 2 * cell && v <= start + 4 * cell &&
                            u >= longSide / 7 && u < longSide / (index % 2 ? 3 : 4);
                        r = rail ? 0.98f : fill ? 0.75f : 0.03f;
                        g = rail ? 0.98f : fill ? 0.14f : 0.03f;
                        b = rail ? 0.98f : 0.03f;
                    }
                    if (v == world || v == world + 6 * cell) {
                        r = g = b = 0.99f;
                    } else if (v > world && v < world + 6 * cell) {
                        if (v <= world + 2 * cell) r = g = b = 0.13f;
                        if (v >= world + 4 * cell) r = g = b = 0.13f;
                    }
                }
                setPixel(pixels.data() + (size_t(y) * p.w + x) * p.bpp, p.format, r, g, b, 1.0f);
            }
        return pixels;
    }

    bool update(unsigned index, bool hud, bool reset, bool vertical = false) {
        const auto parity = index & 1u;
        if (!gpu.begin()) return false;
        auto& plane = real[parity];
        uploaded[parity] = picture(index, hud, vertical);
        auto upload = recordFill(gpu, plane, uploaded[parity], states.get(plane.texture.Get()),
                                 D3D12_RESOURCE_STATE_COMMON);
        states.set(plane.texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        return upload && lock.update(gpu.list.Get(), states, parity, reset) && gpu.submitAndWait();
    }

    std::vector<uint8_t> compose(unsigned held, bool newer = false) {
        if (!gpu.begin()) return {};
        auto upload = recordFill(gpu, generated, placeholder, states.get(generated.texture.Get()),
                                 D3D12_RESOURCE_STATE_COMMON);
        states.set(generated.texture.Get(), D3D12_RESOURCE_STATE_COMMON);
        if (!upload || !lock.compose(gpu.list.Get(), states, 0, held, newer)) return {};
        auto result = recordRead(gpu, generated, states.get(generated.texture.Get()));
        if (!result || !gpu.submitAndWait()) return {};
        return mapRead(generated, result.Get());
    }
};

void testDynamicHud(Gpu& gpu, DXGI_FORMAT format, unsigned outputMode, bool vertical, const char* title) {
    constexpr uint32_t W = 257, H = 145;
    DynamicHudFixture fixture{gpu};
    const std::string name = std::string("dynamic HUD ") + title;
    const bool initialized = fixture.initialize(W, H, format, outputMode);
    check(initialized, name + ": passes created");
    if (!initialized) return;
    const bool first = fixture.update(0, true, true, vertical);
    check(first && fixture.compose(0) == fixture.placeholder, name + ": no edit before temporal evidence");
    if (!first) return;
    bool updated = true;
    for (unsigned i = 1; i < 64 && updated; ++i) updated = fixture.update(i, true, false, vertical);
    check(updated, name + ": learned from 64 original frames");
    if (!updated) return;

    const auto previous = fixture.compose(0);
    const auto newer = fixture.compose(0, true);
    const bool composed = previous.size() == fixture.placeholder.size() && newer.size() == previous.size();
    check(composed, name + ": read both interpolation phases");
    if (!composed) return;
    size_t changedHud = 0, matchingPrevious = 0, matchingNewer = 0, spill = 0;
    const auto bpp = fixture.generated.bpp;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const uint32_t u = vertical ? y : x, v = vertical ? x : y;
            const uint32_t minSide = vertical ? W : H, maxSide = vertical ? H : W;
            const uint32_t start = minSide / 8;
            const size_t at = (size_t(y) * W + x) * bpp;
            const auto equal = [at, bpp](const auto& a, const auto& b) {
                return std::memcmp(a.data() + at, b.data() + at, bpp) == 0;
            };
            if (v >= start + 4 && v <= start + 8 && u >= maxSide / 4 && u < maxSide / 3) {
                if (!equal(fixture.uploaded[0], fixture.uploaded[1])) {
                    ++changedHud;
                    matchingPrevious += equal(previous, fixture.uploaded[0]);
                    matchingNewer += equal(newer, fixture.uploaded[1]);
                }
            }
            if (u < maxSide / 9 || u >= maxSide / 2 || v <= start || v >= start + 12)
                spill += !equal(previous, fixture.placeholder) || !equal(newer, fixture.placeholder);
        }
    std::printf("INFO %s: changing pixels correctly selected %zu/%zu early, %zu/%zu late, spill=%zu\n",
                name.c_str(), matchingPrevious, changedHud, matchingNewer, changedHud, spill);
    check(changedHud > 0 && matchingPrevious == changedHud && matchingNewer == changedHud,
          name + ": nearest real colors selected across the change");
    check(spill == 0, name + ": moving scenery untouched, including between matching rails");
    check(fixture.update(64, false, false, vertical) && fixture.compose(1) == fixture.placeholder,
          name + ": HUD removal releases the mask immediately");
    check(fixture.update(65, true, true, vertical) && fixture.compose(1) == fixture.placeholder,
          name + ": reset clears the mask without changing the frame");
    check(!gpu.debugErrors(), name + ": no D3D12 debug errors");
}

bool benchmarkHud(Gpu& gpu, uint32_t width, uint32_t height, bool staticLock, bool dynamic) {
    constexpr unsigned warmup = 64, samples = 100, frames = warmup + samples;
    DynamicHudFixture fixture{gpu};
    if (!fixture.initialize(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0, staticLock, dynamic) ||
        !fixture.update(0, true, true) || !fixture.update(1, true, false)) return false;
    UINT64 frequency = 0;
    if (FAILED(gpu.queue->GetTimestampFrequency(&frequency)) || frequency == 0) return false;
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 3 * samples;
    ComPtr<ID3D12QueryHeap> timestamps;
    if (FAILED(gpu.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&timestamps)))) return false;
    auto result = makeBuffer(gpu.device.Get(), desc.Count * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK);
    if (!result || !gpu.begin()) return false;
    auto initial = recordFill(gpu, fixture.generated, fixture.placeholder,
                              D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
    if (!initial) return false;

    for (unsigned i = 0; i < frames; ++i) {
        const UINT first = i >= warmup ? 3 * (i - warmup) : 0;
        const unsigned parity = i & 1u;
        if (i >= warmup) gpu.list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first);
        if (!fixture.lock.update(gpu.list.Get(), fixture.states, parity, false)) return false;
        if (i >= warmup) gpu.list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + 1);
        if (!fixture.lock.compose(gpu.list.Get(), fixture.states, 0, 1u - parity, true)) return false;
        if (i >= warmup) gpu.list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + 2);
    }
    gpu.list->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, desc.Count, result.Get(), 0);
    if (!gpu.submitAndWait()) return false;
    UINT64* data = nullptr;
    if (FAILED(result->Map(0, nullptr, reinterpret_cast<void**>(&data)))) return false;
    std::array<double, samples> trackUpdate{}, trackCompose{}, trackBoth{};
    const double milliseconds = 1000.0 / double(frequency);
    for (unsigned i = 0; i < samples; ++i) {
        trackUpdate[i] = double(data[3 * i + 1] - data[3 * i]) * milliseconds;
        trackCompose[i] = double(data[3 * i + 2] - data[3 * i + 1]) * milliseconds;
        trackBoth[i] = double(data[3 * i + 2] - data[3 * i]) * milliseconds;
    }
    const D3D12_RANGE written{0, 0};
    result->Unmap(0, &written);
    std::sort(trackUpdate.begin(), trackUpdate.end());
    std::sort(trackCompose.begin(), trackCompose.end());
    std::sort(trackBoth.begin(), trackBoth.end());
    std::printf("BENCH %ux%u static=%d dynamic=%d samples=%u update_p50_ms=%.3f compose_p50_ms=%.3f total_p50_ms=%.3f total_p95_ms=%.3f\n",
                width, height, staticLock, dynamic, samples, trackUpdate[49], trackCompose[49], trackBoth[49], trackBoth[94]);
    return !gpu.debugErrors();
}

} // namespace

int main(int argc, char** argv) {
    Gpu gpu;
    if (!gpu.create()) { std::printf("FAIL D3D12 device\n"); return 1; }
    std::printf("adapter: %s%s\n", gpu.adapter.c_str(), gpu.debug ? " (debug layer)" : "");
    if (argc == 2 && std::string_view(argv[1]) == "--perf") {
        for (const uint32_t width : {1920u, 3840u}) {
            const uint32_t height = width * 9 / 16;
            if (!benchmarkHud(gpu, width, height, true, false) ||
                !benchmarkHud(gpu, width, height, false, true) ||
                !benchmarkHud(gpu, width, height, true, true)) return 1;
        }
        return 0;
    }
    if (argc != 1) return 2;
    testHudLock(gpu);
    testDynamicHud(gpu, DXGI_FORMAT_R8G8B8A8_UNORM, 0, false, "SDR");
    testDynamicHud(gpu, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, true, "scRGB");
    testDynamicHud(gpu, DXGI_FORMAT_R10G10B10A2_UNORM, 2, false, "HDR10 PQ");
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
