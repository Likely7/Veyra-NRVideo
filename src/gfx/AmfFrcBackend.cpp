// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/gfx/AmfFrcBackend.h"

#include "veyra/Log.h"

#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>

#ifdef VEYRA_HAS_AMF
#include "AmfDx12Interop.h"
#include "components/FRC.h"
#endif

namespace veyra::gfx {
using Microsoft::WRL::ComPtr;

#ifdef VEYRA_HAS_AMF
namespace {
using namespace amfDx12;
amf::AMF_SURFACE_FORMAT surfaceFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return amf::AMF_SURFACE_RGBA;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return amf::AMF_SURFACE_BGRA;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return amf::AMF_SURFACE_R10G10B10A2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return amf::AMF_SURFACE_RGBA_F16;
    default: return amf::AMF_SURFACE_UNKNOWN;
    }
}
}
#endif

struct AmfFrcBackend::Impl {
#ifdef VEYRA_HAS_AMF
    static constexpr int kInputs = 4;
    amf::AMFContextPtr context;
    amf::AMFComponentPtr frc;
    ComPtr<ID3D12CommandQueue> queue;
    std::array<ComPtr<ID3D12Resource>, kInputs> input;
    std::array<ComPtr<ID3D12Fence>, kInputs> inputFence;
    int next = 0, captured = -1;
    // The frame AMF returned for the latest input, then the one being copied out.
    amf::AMFSurfacePtr out, held;
    ID3D12Resource* outTexture = nullptr;
    ComPtr<ID3D12Fence> outFence, heldFence;
    D3D12_RESOURCE_STATES outState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width = 0, height = 0;
    uint64_t inputs = 0, outputs = 0;
#endif
    bool ready = false;
    std::string version;
};

AmfFrcBackend::AmfFrcBackend() : p_(std::make_unique<Impl>()) {}
AmfFrcBackend::~AmfFrcBackend() { shutdown(); }

bool AmfFrcBackend::runtimeInstalled() {
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(system) / L"amfrt64.dll", ec);
}

bool AmfFrcBackend::ready() const { return p_->ready; }
bool AmfFrcBackend::warmingUp() const {
#ifdef VEYRA_HAS_AMF
    return p_->inputs <= 2; // the first input has no pair; the second is the warm-up repeat
#else
    return true;
#endif
}
const std::string& AmfFrcBackend::runtimeVersion() const { return p_->version; }

bool AmfFrcBackend::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width, uint32_t height, DXGI_FORMAT format, bool lowPower) {
#ifdef VEYRA_HAS_AMF
    shutdown();
    auto& p = *p_;
    const auto& rt = runtime();
    p.version = rt.version;
    if (!rt.factory) { log::error("amf-frc", "AMF runtime unavailable; AMD FRC needs an AMD GPU driver"); return false; }
    const auto amfFormat = surfaceFormat(format);
    if (amfFormat == amf::AMF_SURFACE_UNKNOWN) { log::error("amf-frc", std::format("unsupported texture format {}", int(format))); return false; }
    const auto start = std::chrono::steady_clock::now();
    AMF_RESULT result = rt.factory->CreateContext(&p.context);
    amf::AMFContext2Ptr context2(p.context);
    if (result != AMF_OK || !context2) { log::error("amf-frc", std::format("CreateContext failed result={}", int(result))); shutdown(); return false; }
    // Fails on a device of another vendor: the AMF runtime drives AMD GPUs only.
    result = context2->InitDX12(device, amf::AMF_DX12);
    if (result != AMF_OK) { log::error("amf-frc", std::format("InitDX12 failed result={} (AMD GPU required)", int(result))); shutdown(); return false; }
    result = rt.factory->CreateComponent(p.context, AMFFRC, &p.frc);
    if (result != AMF_OK || !p.frc) { log::error("amf-frc", std::format("CreateComponent(AMFFRC) failed result={} runtime={}", int(result), p.version)); shutdown(); return false; }
    // AMD's guidance: the SUPER motion-search profile from 1440p up, HIGH below.
    // Dedicated GPUs also take SUPER below 1440p: on 1080p console capture HIGH
    // loses fast camera pans and falls back to a two-frame blend (double image);
    // SUPER tracks them for about 0.3 ms more GPU time (RX 9070 XT, 2026-11-20).
    // Low-memory GPUs (lowPower, under 2 GiB) keep HIGH below 1440p.
    const bool large = uint64_t(width) * height > uint64_t(2560) * 1440;
    const bool superProfile = large || !lowPower;
    // The downscaled (performance) search returned the previous frame unchanged at
    // 640x360 while it interpolated from 960x540 up (probe on the Radeon iGPU,
    // 2026-11-14); small frames are cheap with the full-resolution search anyway.
    lowPower = lowPower && uint64_t(width) * height >= uint64_t(1280) * 720;
    p.frc->SetProperty(AMF_FRC_ENGINE_TYPE, amf_int64(FRC_ENGINE_DX12));
    p.frc->SetProperty(AMF_FRC_MODE, amf_int64(FRC_ONLY_INTERPOLATED));
    p.frc->SetProperty(AMF_FRC_ENABLE_FALLBACK, true); // blend, not repeat, where the motion is unsure
    p.frc->SetProperty(AMF_FRC_INDICATOR, false);
    p.frc->SetProperty(AMF_FRC_PROFILE, amf_int64(superProfile ? FRC_PROFILE_SUPER : FRC_PROFILE_HIGH));
    p.frc->SetProperty(AMF_FRC_MV_SEARCH_MODE, amf_int64(lowPower ? FRC_MV_SEARCH_PERFORMANCE : FRC_MV_SEARCH_NATIVE));
    // The future frame would add one more frame of delay and shift the output a pair later.
    p.frc->SetProperty(AMF_FRC_USE_FUTURE_FRAME, false);
    result = p.frc->Init(amfFormat, amf_int32(width), amf_int32(height));
    if (result != AMF_OK) { log::error("amf-frc", std::format("Init failed result={} {}x{} format={}", int(result), width, height, int(format))); shutdown(); return false; }
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = format; desc.SampleDesc.Count = 1;
    for (int i = 0; i < Impl::kInputs; ++i) {
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&p.input[i]))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.inputFence[i])))) {
            log::error("amf-frc", "input texture/fence allocation failed"); shutdown(); return false;
        }
        p.input[i]->SetName(std::format(L"Veyra AMF FRC input {}", i).c_str());
        const UINT state = D3D12_RESOURCE_STATE_COMMON;
        p.input[i]->SetPrivateData(AMFResourceStateGUID, sizeof(state), &state);
        setFenceValue(p.inputFence[i].Get(), 0);
        p.input[i]->SetPrivateDataInterface(AMFFenceGUID, p.inputFence[i].Get());
    }
    p.queue = queue; p.width = width; p.height = height; p.ready = true;
    log::info("amf-frc", std::format("ready runtime={} {}x{} format={} profile={} motionSearch={} futureFrame=0 mode=only-interpolated initMs={:.1f}",
        p.version, width, height, int(format), superProfile ? "super" : "high", lowPower ? "performance" : "native",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()));
    return true;
#else
    (void)device; (void)queue; (void)width; (void)height; (void)format; (void)lowPower;
    log::error("amf-frc", "this build has no AMF SDK headers");
    return false;
#endif
}

bool AmfFrcBackend::capture(ID3D12GraphicsCommandList* list, ID3D12Resource* color) {
#ifdef VEYRA_HAS_AMF
    auto& p = *p_;
    if (!p.ready || !list || !color) return false;
    const int slot = p.next;
    p.next = (p.next + 1) % Impl::kInputs;
    auto* fence = p.inputFence[slot].Get();
    // AMF raises this fence when it no longer reads the texture; the copy waits for it.
    if (const UINT64 wait = fenceValue(fence)) p.queue->Wait(fence, wait);
    // AMF leaves the texture in the state it read it in (shader resource) and records
    // that state on the texture; start from it and record the state left behind.
    auto* texture = p.input[slot].Get();
    barrier(list, texture, resourceState(texture), D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(texture, color);
    barrier(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    setResourceState(texture, D3D12_RESOURCE_STATE_COMMON);
    p.captured = slot;
    return true;
#else
    (void)list; (void)color; return false;
#endif
}

bool AmfFrcBackend::generate(int64_t pts100ns, bool reset) {
#ifdef VEYRA_HAS_AMF
    auto& p = *p_;
    if (!p.ready || p.captured < 0) return false;
    const int slot = p.captured;
    p.captured = -1;
    // The list holding the copy is submitted: AMF may read once this signals.
    auto* fence = p.inputFence[slot].Get();
    const UINT64 written = fenceValue(fence) + 1;
    setFenceValue(fence, written);
    if (FAILED(p.queue->Signal(fence, written))) { log::error("amf-frc", "input fence signal failed"); return false; }
    p.out = nullptr; p.outTexture = nullptr; p.outFence.Reset();
    // Without a valid previous frame AMF must not blend across the cut.
    if (reset && p.inputs) p.frc->Flush();
    amf::AMFContext2Ptr context2(p.context);
    amf::AMFSurfacePtr surface;
    AMF_RESULT result = context2 ? context2->CreateSurfaceFromDX12Native(p.input[slot].Get(), &surface, nullptr) : AMF_FAIL;
    if (result != AMF_OK || !surface) { log::error("amf-frc", std::format("CreateSurfaceFromDX12Native failed result={}", int(result))); return false; }
    surface->SetPts(pts100ns);
    result = p.frc->SubmitInput(surface);
    for (int retry = 0; result == AMF_INPUT_FULL && retry < 4; ++retry) {
        // Outputs left from earlier inputs are dropped; they were never shown.
        amf::AMFDataPtr stale;
        p.frc->QueryOutput(&stale);
        result = p.frc->SubmitInput(surface);
    }
    if (result != AMF_OK && result != AMF_NEED_MORE_INPUT) { log::error("amf-frc", std::format("SubmitInput failed result={}", int(result))); return false; }
    ++p.inputs;
    for (int i = 0; i < 4; ++i) {
        amf::AMFDataPtr data;
        result = p.frc->QueryOutput(&data);
        if (!data) break;
        p.out = amf::AMFSurfacePtr(data); // the latest output belongs to this input
    }
    if (p.out && p.out->GetPlaneAt(0)) {
        p.outTexture = static_cast<ID3D12Resource*>(p.out->GetPlaneAt(0)->GetNative());
        p.outFence = resourceFence(p.outTexture);
        p.outState = resourceState(p.outTexture);
    }
    if (p.inputs <= 3 || p.inputs % 600 == 0)
        log::info("amf-frc", std::format("input={} reset={} output={} result={}", p.inputs, reset, p.outTexture != nullptr, int(result)));
    return true;
#else
    (void)pts100ns; (void)reset; return false;
#endif
}

bool AmfFrcBackend::copyOutput(ID3D12GraphicsCommandList* list, ID3D12Resource* output) {
#ifdef VEYRA_HAS_AMF
    auto& p = *p_;
    if (!p.ready || !p.out || !p.outTexture || !list || !output) return false;
    if (p.outFence) {
        if (const UINT64 wait = fenceValue(p.outFence.Get())) p.queue->Wait(p.outFence.Get(), wait);
    }
    barrier(list, p.outTexture, p.outState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = output; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = p.outTexture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const D3D12_BOX box{0, 0, 0, p.width, p.height, 1};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    barrier(list, p.outTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, p.outState);
    p.held = std::move(p.out); p.heldFence = std::move(p.outFence); p.outTexture = nullptr;
    ++p.outputs;
    return true;
#else
    (void)list; (void)output; return false;
#endif
}

void AmfFrcBackend::submitted() {
#ifdef VEYRA_HAS_AMF
    auto& p = *p_;
    if (!p.held) return;
    // AMF may write this frame again once our copy is done.
    if (p.heldFence) {
        const UINT64 value = fenceValue(p.heldFence.Get()) + 1;
        setFenceValue(p.heldFence.Get(), value);
        p.queue->Signal(p.heldFence.Get(), value);
    }
    p.held = nullptr; p.heldFence.Reset();
#endif
}

void AmfFrcBackend::shutdown() {
#ifdef VEYRA_HAS_AMF
    auto& p = *p_;
    if (p.ready) log::info("amf-frc", std::format("shutdown inputs={} outputs={}", p.inputs, p.outputs));
    if (p.held && p.heldFence) waitFence(p.heldFence.Get(), fenceValue(p.heldFence.Get()));
    p.held = nullptr; p.heldFence.Reset();
    p.out = nullptr; p.outTexture = nullptr; p.outFence.Reset();
    if (p.frc) { p.frc->Terminate(); p.frc = nullptr; }
    for (int i = 0; i < Impl::kInputs; ++i) {
        if (p.inputFence[i]) waitFence(p.inputFence[i].Get(), fenceValue(p.inputFence[i].Get()));
        p.input[i].Reset(); p.inputFence[i].Reset();
    }
    if (p.context) { p.context->Terminate(); p.context = nullptr; }
    p.queue.Reset();
    p.next = 0; p.captured = -1; p.inputs = p.outputs = 0;
#endif
    p_->ready = false;
}
}
