// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/gfx/RifeDmlBackend.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef VEYRA_HAS_RIFE_DML
#include <DirectML.h>
#include <onnxruntime_c_api.h>
#include <dml_provider_factory.h>
#endif

namespace veyra::gfx {
using Microsoft::WRL::ComPtr;

namespace {
std::filesystem::path runtimeDirectory() { return runtime::applicationRoot() / "runtime" / "rife"; }
// 2X takes the full RIFE 4.26; 3X the lite 4.22, which leaves more of the
// 33 ms of a 30 fps pair for the rest of the graph (RX 9070 XT, 2026-11-22:
// 11.4 vs 8.9 ms per frame at 1088x1920 fp16).
std::string defaultModel(unsigned multiplier) { return multiplier > 2 ? "rife_v4.22_lite" : "rife_v4.26"; }
std::string selectedModel(unsigned multiplier) {
    wchar_t value[64]{};
    if (GetEnvironmentVariableW(L"VEYRA_TEST_RIFE_MODEL", value, 64)) {
        std::string name;
        for (const wchar_t* c = value; *c; ++c) name.push_back(char(*c));
        return name;
    }
    return defaultModel(multiplier);
}
// Model tensors are padded to a multiple of 64 (the network downsamples 16x
// and its flow pyramid needs whole blocks).
uint32_t padded(uint32_t v) { return (v + 63u) / 64u * 64u; }
}

#ifdef VEYRA_HAS_RIFE_DML
namespace {
struct Runtime {
    const OrtApi* api = nullptr;
    const OrtDmlApi* dml = nullptr;
    OrtEnv* env = nullptr;
    decltype(&DMLCreateDevice) createDevice = nullptr;
    std::string error;
};
const Runtime& runtime() {
    static Runtime r;
    static std::once_flag once;
    std::call_once(once, [] {
        const auto dir = runtimeDirectory();
        // DirectML first, from runtime\rife by absolute path: onnxruntime.dll then
        // binds the already loaded module, never the older System32 DirectML.dll.
        HMODULE directMl = LoadLibraryExW((dir / L"DirectML.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE ort = directMl ? LoadLibraryExW((dir / L"onnxruntime.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) : nullptr;
        if (!directMl || !ort) { r.error = std::format("runtime\\rife DLLs not loaded err={}", GetLastError()); return; }
        r.createDevice = reinterpret_cast<decltype(&DMLCreateDevice)>(GetProcAddress(directMl, "DMLCreateDevice"));
        auto base = reinterpret_cast<const OrtApiBase*(ORT_API_CALL*)()>(GetProcAddress(ort, "OrtGetApiBase"));
        r.api = base ? base()->GetApi(ORT_API_VERSION) : nullptr;
        if (!r.createDevice || !r.api) { r.error = "DMLCreateDevice / OrtGetApiBase missing or ONNX Runtime too old"; r.api = nullptr; return; }
        if (auto* s = r.api->GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&r.dml))) {
            r.error = r.api->GetErrorMessage(s); r.api->ReleaseStatus(s); r.api = nullptr; return;
        }
        if (auto* s = r.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "veyra-rife", &r.env)) {
            r.error = r.api->GetErrorMessage(s); r.api->ReleaseStatus(s); r.api = nullptr; return;
        }
        log::info("rife", std::format("runtime loaded dir={} ort={}", dir.string(), base()->GetVersionString()));
    });
    return r;
}
}

struct RifeDmlBackend::Impl {
    const Runtime* rt = nullptr;
    ID3D12Device* device = nullptr;
    ComPtr<IDMLDevice> dmlDevice;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    uint32_t width = 0, height = 0, pw = 0, ph = 0;
    unsigned multiplier = 2;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::string model;
    // planes[2]: the two most recent frames. times/outputs per timestep: k/multiplier
    // for k = 1..multiplier-1, then 1/2 at 3X for pairs the owner reduces to 2X.
    struct Tensor { ComPtr<ID3D12Resource> buffer; void* allocation = nullptr; OrtValue* value = nullptr; };
    std::array<Tensor, 2> planes;
    std::vector<Tensor> times, outputs;
    // bindings[current * steps + step]: img0 = planes[1-current], img1 = planes[current].
    std::vector<OrtIoBinding*> bindings;
    ComPtr<ID3D12Resource> input;   // copy of the colour frame (SRV for the pack pass)
    D3D12_RESOURCE_STATES inputState = D3D12_RESOURCE_STATE_COPY_DEST;
    pipeline::ComputePass pack, unpack, shot;
    // Shot-change test (RifeShotChange.hlsl): work holds two 128x72 luma pictures and
    // 32 block errors; cut holds the 64-bit flag of the last pair at offset 0.
    ComPtr<ID3D12Resource> work, cut;
    D3D12_RESOURCE_STATES cutState = D3D12_RESOURCE_STATE_COMMON;
    std::unordered_map<ID3D12Resource*, UINT> outputSlots;   // unpack UAV per owner texture
    unsigned current = 1;      // planes index written by the last capture
    bool haveFrame = false;    // a capture since the last reset
    bool havePrevious = false; // ... and one before it: planes hold a real pair
    bool timesFilled = false, ok = false;

    unsigned steps() const { return multiplier > 2 ? multiplier : 1u; }
    int step(unsigned sub, unsigned of) const {
        if (sub < 1 || sub >= of) return -1;
        if (of == multiplier) return int(sub - 1);
        if (of == 2 && multiplier > 2) return int(multiplier - 1);
        return -1;
    }
    float time(unsigned step) const { return step + 1 < multiplier ? float(step + 1) / float(multiplier) : 0.5f; }
    bool check(OrtStatus* s, const char* what) {
        if (!s) return true;
        log::error("rife", std::format("{} failed: {}", what, rt->api->GetErrorMessage(s)));
        rt->api->ReleaseStatus(s);
        return false;
    }
    bool makeTensor(Tensor& t, int64_t channels) {
        const uint64_t bytes = uint64_t(channels) * pw * ph * 4;
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, bytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&t.buffer)))) {
            log::error("rife", std::format("tensor buffer {} bytes failed", bytes));
            return false;
        }
        if (!check(rt->dml->CreateGPUAllocationFromD3DResource(t.buffer.Get(), &t.allocation), "CreateGPUAllocationFromD3DResource")) return false;
        const int64_t shape[4] = {1, channels, int64_t(ph), int64_t(pw)};
        return check(rt->api->CreateTensorWithDataAsOrtValue(memory, t.allocation, size_t(bytes), shape, 4,
                                                             ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &t.value), "CreateTensor");
    }
    void release(Tensor& t) {
        if (t.value) rt->api->ReleaseValue(t.value);
        if (t.allocation) rt->dml->FreeGPUAllocation(t.allocation);
        t = {};
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(const pipeline::ComputePass& pass, UINT slot) const {
        return {pass.heap->GetGPUDescriptorHandleForHeapStart().ptr + uint64_t(slot) * pass.increment};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(const pipeline::ComputePass& pass, UINT slot) const {
        return {pass.heap->GetCPUDescriptorHandleForHeapStart().ptr + uint64_t(slot) * pass.increment};
    }
    void rawUav(ID3D12Resource* buffer, uint64_t channels, D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R32_TYPELESS;
        u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements = UINT(channels * pw * ph);
        u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(buffer, nullptr, &u, handle);
    }
    static void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES to) {
        if (state == to) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r; b.Transition.StateBefore = state; b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        state = to;
    }
    void dispatch(ID3D12GraphicsCommandList* list, const pipeline::ComputePass& pass, const uint32_t (&constants)[8],
                  D3D12_GPU_DESCRIPTOR_HANDLE srv, D3D12_GPU_DESCRIPTOR_HANDLE uav, uint32_t w, uint32_t h) const {
        pass.bind(list, reinterpret_cast<const float*>(constants), srv.ptr, uav.ptr);
        list->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
    }
};

// Pack heap: [0] input SRV, [1..2] planes UAV, [3] null SRV, [4..] time UAVs.
// Unpack heap: [0] null SRV, then per (sub, owner texture): output UAV, texture UAV.
RifeDmlBackend::RifeDmlBackend() : p_(std::make_unique<Impl>()) {}
RifeDmlBackend::~RifeDmlBackend() { shutdown(); }

bool RifeDmlBackend::runtimeInstalled() {
    const auto dir = runtimeDirectory();
    std::error_code ec;
    return std::filesystem::exists(dir / L"onnxruntime.dll", ec) && std::filesystem::exists(dir / L"DirectML.dll", ec) &&
           std::filesystem::exists(dir / (defaultModel(2) + "_fp16.onnx"), ec) && std::filesystem::exists(dir / (defaultModel(3) + "_fp16.onnx"), ec);
}

bool RifeDmlBackend::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t width, uint32_t height, DXGI_FORMAT format, unsigned multiplier) {
    shutdown();
    auto& p = *p_;
    p.rt = &runtime();
    if (!p.rt->api) { log::error("rife", "runtime unavailable: " + p.rt->error); return false; }
    if (format != DXGI_FORMAT_R8G8B8A8_UNORM) { log::error("rife", std::format("SDR colour only (format {})", int(format))); return false; }
    if (multiplier < 2 || multiplier > 3) { log::error("rife", std::format("multiplier {} not supported (2X or 3X)", multiplier)); return false; }
    p.device = device; p.width = width; p.height = height; p.pw = padded(width); p.ph = padded(height);
    p.multiplier = multiplier; p.format = format; p.model = selectedModel(multiplier);
    const auto modelPath = runtimeDirectory() / (p.model + "_fp16.onnx");
    if (FAILED(p.rt->createDevice(device, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&p.dmlDevice)))) { log::error("rife", "DMLCreateDevice failed"); return false; }
    const auto* api = p.rt->api;
    if (!p.check(api->CreateSessionOptions(&p.options), "CreateSessionOptions") ||
        !p.check(api->DisableMemPattern(p.options), "DisableMemPattern") ||
        !p.check(api->SetSessionExecutionMode(p.options, ORT_SEQUENTIAL), "SetSessionExecutionMode") ||
        !p.check(api->SetSessionGraphOptimizationLevel(p.options, ORT_ENABLE_ALL), "SetSessionGraphOptimizationLevel") ||
        !p.check(api->AddFreeDimensionOverrideByName(p.options, "height", int64_t(p.ph)), "height override") ||
        !p.check(api->AddFreeDimensionOverrideByName(p.options, "width", int64_t(p.pw)), "width override") ||
        // The graph's own queue: ONNX Runtime records and submits there, so queue
        // order places each Run between our pack and unpack lists.
        !p.check(p.rt->dml->SessionOptionsAppendExecutionProvider_DML1(p.options, p.dmlDevice.Get(), queue), "AppendExecutionProvider_DML1"))
        return false;
    const auto started = std::chrono::steady_clock::now();
    if (!p.check(api->CreateSession(p.rt->env, modelPath.c_str(), p.options, &p.session), "CreateSession")) return false;
    const double sessionMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (!p.check(api->CreateMemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault, &p.memory), "CreateMemoryInfo")) return false;
    for (auto& t : p.planes) if (!p.makeTensor(t, 3)) return false;
    p.times.resize(p.steps()); p.outputs.resize(p.steps());
    for (auto& t : p.times) if (!p.makeTensor(t, 1)) return false;
    for (auto& t : p.outputs) if (!p.makeTensor(t, 3)) return false;
    for (unsigned cur = 0; cur < 2; ++cur)
        for (unsigned s = 0; s < p.steps(); ++s) {
            OrtIoBinding* b = nullptr;
            if (!p.check(api->CreateIoBinding(p.session, &b), "CreateIoBinding")) return false;
            p.bindings.push_back(b);
            if (!p.check(api->BindInput(b, "img0", p.planes[1 - cur].value), "BindInput img0") ||
                !p.check(api->BindInput(b, "img1", p.planes[cur].value), "BindInput img1") ||
                !p.check(api->BindInput(b, "timestep", p.times[s].value), "BindInput timestep") ||
                !p.check(api->BindOutput(b, "output", p.outputs[s].value), "BindOutput"))
                return false;
        }
    p.input = pipeline::makeTexture(device, width, height, format, false);
    if (!p.input) return false;
    p.inputState = D3D12_RESOURCE_STATE_COMMON;
    std::vector<uint8_t> cs;
    if (!p.pack.loadShader("RifePack.dxil", cs) || !p.pack.create(device, cs, 4 + p.steps(), 1, 1) ||
        !p.unpack.loadShader("RifeUnpack.dxil", cs) || !p.unpack.create(device, cs, 1 + 2 * 3 * 8, 1, 2) ||
        !p.shot.loadShader("RifeShotChange.dxil", cs) || !p.shot.create(device, cs, 7, 1, 3, 8))
        return false;
    {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                               D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&p.cut)))) return false;
        p.cutState = D3D12_RESOURCE_STATE_COMMON;
        constexpr UINT workWords = 2 * 128 * 72 + 32;
        rd.Width = workWords * 4;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&p.work)))) return false;
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        // Shot heap: [0] null SRV, then per planes index: that frame's planes, work, flags.
        for (unsigned cur = 0; cur < 2; ++cur) {
            p.rawUav(p.planes[cur].buffer.Get(), 3, p.cpu(p.shot, 1 + cur * 3));
            u.Buffer.NumElements = workWords;
            device->CreateUnorderedAccessView(p.work.Get(), nullptr, &u, p.cpu(p.shot, 2 + cur * 3));
            u.Buffer.NumElements = 64;
            device->CreateUnorderedAccessView(p.cut.Get(), nullptr, &u, p.cpu(p.shot, 3 + cur * 3));
        }
    }
    pipeline::makeSrv(device, p.input.Get(), format, p.cpu(p.pack, 0));
    p.rawUav(p.planes[0].buffer.Get(), 3, p.cpu(p.pack, 1));
    p.rawUav(p.planes[1].buffer.Get(), 3, p.cpu(p.pack, 2));
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = format; nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; nullSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &nullSrv, p.cpu(p.pack, 3));
    device->CreateShaderResourceView(nullptr, &nullSrv, p.cpu(p.unpack, 0));
    device->CreateShaderResourceView(nullptr, &nullSrv, p.cpu(p.shot, 0));
    for (unsigned i = 0; i < p.times.size(); ++i) p.rawUav(p.times[i].buffer.Get(), 1, p.cpu(p.pack, 4 + i));
    p.ok = true;
    log::info("rife", std::format("ready model={} {}x{} tensor {}x{} {}X session={:.0f} ms", p.model, width, height, p.pw, p.ph, multiplier, sessionMs));
    return true;
}

bool RifeDmlBackend::capture(ID3D12GraphicsCommandList* list, ID3D12Resource* color) {
    auto& p = *p_;
    if (!p.ok) return false;
    auto barrier = [&](D3D12_RESOURCE_STATES to) {
        if (p.inputState == to) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = p.input.Get(); b.Transition.StateBefore = p.inputState; b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        p.inputState = to;
    };
    barrier(D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(p.input.Get(), color);
    barrier(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const unsigned next = 1 - p.current;
    const uint32_t c[8] = {p.width, p.height, p.pw, p.ph, 0, 0, 0, 0};
    p.dispatch(list, p.pack, c, p.gpu(p.pack, 0), p.gpu(p.pack, 1 + next), p.pw, p.ph);
    if (!p.timesFilled) {
        for (unsigned s = 0; s < p.steps(); ++s) {
            const float t = p.time(s);
            uint32_t f[8] = {p.width, p.height, p.pw, p.ph, 1, 0, 1, 0};
            std::memcpy(&f[5], &t, 4);
            p.dispatch(list, p.pack, f, p.gpu(p.pack, 3), p.gpu(p.pack, 4 + s), p.pw, p.ph);
        }
        p.timesFilled = true;
    }
    // Shot change between the previous and this frame, from the planes just written.
    D3D12_RESOURCE_BARRIER planesWritten{};
    planesWritten.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; planesWritten.UAV.pResource = p.planes[next].buffer.Get();
    list->ResourceBarrier(1, &planesWritten);
    Impl::transition(list, p.cut.Get(), p.cutState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // Median best-match error above 0.07: continuous 30 fps GTA pairs stay below
    // 0.043, cuts reach 0.12-0.13 (RifeShotChange.hlsl).
    const float threshold = 0.07f;
    D3D12_RESOURCE_BARRIER workWritten{};
    workWritten.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; workWritten.UAV.pResource = p.work.Get();
    for (uint32_t mode = 0; mode < 3; ++mode) {
        uint32_t sc[8] = {p.width, p.height, p.pw, p.ph, mode, next, p.haveFrame ? 1u : 0u, 0};
        std::memcpy(&sc[7], &threshold, 4);
        p.shot.bind(list, reinterpret_cast<const float*>(sc), p.gpu(p.shot, 0).ptr, p.gpu(p.shot, 1 + next * 3).ptr);
        // mode 1 needs both luma pictures; on a first frame it still runs, compare=0 discards it.
        list->Dispatch(mode == 0 ? (128 * 72 + 255) / 256 : mode == 1 ? 32 : 1, 1, 1);
        list->ResourceBarrier(1, &workWritten);
    }
    Impl::transition(list, p.cut.Get(), p.cutState, D3D12_RESOURCE_STATE_PREDICATION);
    p.current = next;
    p.havePrevious = p.haveFrame;
    p.haveFrame = true;
    return true;
}

bool RifeDmlBackend::generate(unsigned sub, unsigned of) {
    auto& p = *p_;
    const int s = p.step(sub, of);
    if (!p.ok || !p.havePrevious || s < 0) return false;
    return p.check(p.rt->api->RunWithBinding(p.session, nullptr, p.bindings[p.current * p.steps() + unsigned(s)]), "RunWithBinding");
}

bool RifeDmlBackend::copyOutput(ID3D12GraphicsCommandList* list, ID3D12Resource* output, unsigned sub, unsigned of) {
    auto& p = *p_;
    const int s = p.step(sub, of);
    if (!p.ok || s < 0) return false;
    const auto key = output;
    auto it = p.outputSlots.find(key);
    UINT slot = 0;
    if (it == p.outputSlots.end()) {
        // Owner textures are fixed for the backend's life; each gets one pair of
        // descriptors per subframe, written once (never while the GPU reads them).
        const UINT index = UINT(p.outputSlots.size());
        if (index >= 8) { log::error("rife", "too many output textures"); return false; }
        p.outputSlots.emplace(key, index);
        slot = index;
        for (unsigned k = 0; k < p.steps(); ++k) {
            const UINT base = 1 + (slot * 3 + k) * 2;
            p.rawUav(p.outputs[k].buffer.Get(), 3, p.cpu(p.unpack, base));
            pipeline::makeUav(p.device, output, p.format, p.cpu(p.unpack, base + 1));
        }
    } else slot = it->second;
    const UINT base = 1 + (slot * 3 + unsigned(s)) * 2;
    const uint32_t c[8] = {p.width, p.height, p.pw, p.ph, 0, 0, 0, 0};
    // Across a shot change the network would morph two unrelated pictures: the
    // conversion is skipped and the subframe shows the new frame instead.
    list->SetPredication(p.cut.Get(), 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);   // skipped on a cut
    p.dispatch(list, p.unpack, c, p.gpu(p.unpack, 0), p.gpu(p.unpack, base), p.width, p.height);
    D3D12_RESOURCE_STATES outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Impl::transition(list, output, outputState, D3D12_RESOURCE_STATE_COPY_DEST);
    Impl::transition(list, p.input.Get(), p.inputState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->SetPredication(p.cut.Get(), 0, D3D12_PREDICATION_OP_EQUAL_ZERO);       // skipped unless a cut
    list->CopyResource(output, p.input.Get());
    list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    Impl::transition(list, p.input.Get(), p.inputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Impl::transition(list, output, outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}

void RifeDmlBackend::reset() { p_->haveFrame = p_->havePrevious = false; }
bool RifeDmlBackend::hasPair() const { return p_->havePrevious; }
bool RifeDmlBackend::ready() const { return p_->ok; }
const std::string& RifeDmlBackend::modelName() const { return p_->model; }

void RifeDmlBackend::shutdown() {
    auto& p = *p_;
    if (!p.rt || !p.rt->api) { p = Impl{}; return; }
    const auto* api = p.rt->api;
    for (auto* b : p.bindings) api->ReleaseIoBinding(b);
    p.bindings.clear();
    for (auto& t : p.planes) p.release(t);
    for (auto& t : p.times) p.release(t);
    for (auto& t : p.outputs) p.release(t);
    if (p.session) api->ReleaseSession(p.session);
    if (p.options) api->ReleaseSessionOptions(p.options);
    if (p.memory) api->ReleaseMemoryInfo(p.memory);
    const Runtime* rt = p.rt;
    p = Impl{};
    p.rt = rt;
}
#else
struct RifeDmlBackend::Impl { std::string model; };
RifeDmlBackend::RifeDmlBackend() : p_(std::make_unique<Impl>()) {}
RifeDmlBackend::~RifeDmlBackend() = default;
bool RifeDmlBackend::runtimeInstalled() { return false; }
bool RifeDmlBackend::initialize(ID3D12Device*, ID3D12CommandQueue*, uint32_t, uint32_t, DXGI_FORMAT, unsigned) {
    log::error("rife", "this build has no ONNX Runtime DirectML headers");
    return false;
}
bool RifeDmlBackend::capture(ID3D12GraphicsCommandList*, ID3D12Resource*) { return false; }
bool RifeDmlBackend::generate(unsigned, unsigned) { return false; }
bool RifeDmlBackend::copyOutput(ID3D12GraphicsCommandList*, ID3D12Resource*, unsigned, unsigned) { return false; }
void RifeDmlBackend::reset() {}
bool RifeDmlBackend::hasPair() const { return false; }
bool RifeDmlBackend::ready() const { return false; }
const std::string& RifeDmlBackend::modelName() const { return p_->model; }
void RifeDmlBackend::shutdown() {}
#endif
}
