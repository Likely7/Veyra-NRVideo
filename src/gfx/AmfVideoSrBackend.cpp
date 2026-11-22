// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/gfx/AmfVideoSrBackend.h"
#include "veyra/gfx/AmfFrcBackend.h"
#include "veyra/Log.h"

#ifdef VEYRA_HAS_AMF_HQ_SCALER
#include "AmfDx12Interop.h"
#include "components/HQScaler.h"
#endif

namespace veyra::gfx {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
namespace {
using namespace amfDx12;

bool amfOk(const char* step, AMF_RESULT result) {
    if (result == AMF_OK) return true;
    log::error("amf-video-sr", std::format("{} failed result={}", step, int(result)));
    return false;
}

bool dxOk(const char* step, HRESULT result) {
    if (SUCCEEDED(result)) return true;
    log::error("amf-video-sr", std::format("{} failed hr=0x{:08X}", step, unsigned(result)));
    return false;
}
}
#endif

struct AmfVideoSrBackend::Impl {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    amf::AMFContext2Ptr context;
    amf::AMFComponentPtr scaler;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Resource> input;
    ComPtr<ID3D12Fence> inputFence;
    amf::AMFSurfacePtr output;
    ID3D12Resource* outputTexture = nullptr;
    ComPtr<ID3D12Fence> outputFence;
    uint64_t sequence = 0;
    unsigned width = 0, height = 0;
    amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_UNKNOWN;
    bool recordedOutput = false;

    ~Impl() {
        // The graph drains its queue before destruction. A failed evaluate may
        // still have left work on the scaler queue.
        output = nullptr;
        if (scaler) scaler->Terminate();
        if (inputFence) waitFence(inputFence.Get(), fenceValue(inputFence.Get()));
        if (context) context->Terminate();
    }
#endif
};

AmfVideoSrBackend::AmfVideoSrBackend() : p_(std::make_unique<Impl>()) {}
AmfVideoSrBackend::~AmfVideoSrBackend() = default;

bool AmfVideoSrBackend::available() {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    return AmfFrcBackend::runtimeInstalled();
#else
    return false;
#endif
}

bool AmfVideoSrBackend::initialize(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned inWidth, unsigned inHeight,
                                  unsigned outWidth, unsigned outHeight, bool version11, bool fromLinearToSrgb, DXGI_FORMAT format) {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    auto& p = *p_;
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: p.format = amf::AMF_SURFACE_RGBA_F16; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM: p.format = amf::AMF_SURFACE_RGBA; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM: p.format = amf::AMF_SURFACE_BGRA; break;
    default: return false;
    }
    const auto& rt = amfDx12::runtime();
    if (!rt.factory) return false;
    amf::AMFContextPtr context;
    if (!amfOk("CreateContext", rt.factory->CreateContext(&context))) return false;
    p.context = amf::AMFContext2Ptr(context);
    if (!p.context || !amfOk("InitDX12", p.context->InitDX12(device, amf::AMF_DX12))) return false;
    if (!amfOk("CreateComponent(HQScaler)", rt.factory->CreateComponent(context, AMFHQScaler, &p.scaler)) || !p.scaler)
        return false;
    const amf_int64 algorithm = version11 ? AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1 : AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_0;
    if (!amfOk("engine", p.scaler->SetProperty(AMF_HQ_SCALER_ENGINE_TYPE, amf_int64(amf::AMF_MEMORY_DX12))) ||
        !amfOk("algorithm", p.scaler->SetProperty(AMF_HQ_SCALER_ALGORITHM, algorithm)) ||
        !amfOk("output size", p.scaler->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, AMFConstructSize(outWidth, outHeight))) ||
        !amfOk("exact output extent", p.scaler->SetProperty(AMF_HQ_SCALER_KEEP_ASPECT_RATIO, false)) ||
        !amfOk("input transfer", p.scaler->SetProperty(AMF_HQ_SCALER_FROM_SRGB, fromLinearToSrgb)) ||
        !amfOk("Init", p.scaler->Init(p.format, inWidth, inHeight))) return false;
    amf_int64 selected = -1;
    if (!amfOk("read algorithm", p.scaler->GetProperty(AMF_HQ_SCALER_ALGORITHM, &selected)) || selected != algorithm) {
        log::error("amf-video-sr", "the driver did not retain the requested VideoSR algorithm");
        return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = inWidth;
    texture.Height = inHeight;
    texture.DepthOrArraySize = texture.MipLevels = 1;
    texture.Format = format;
    texture.SampleDesc.Count = 1;
    if (!dxOk("create input", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                 D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&p.input))) ||
        !dxOk("create input fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.inputFence))) ||
        !dxOk("attach input fence", p.input->SetPrivateDataInterface(AMFFenceGUID, p.inputFence.Get()))) return false;
    setResourceState(p.input.Get(), D3D12_RESOURCE_STATE_COMMON);
    setFenceValue(p.inputFence.Get(), 0);
    p.input->SetName(L"Veyra AMF VideoSR input");
    p.queue = queue;
    p.width = outWidth;
    p.height = outHeight;
    log::info("amf-video-sr", std::format("ready algorithm={} runtime={} {}x{} -> {}x{} dxgiFormat={} fromLinearToSrgb={}",
        version11 ? "1.1" : "1.0", rt.version, inWidth, inHeight, outWidth, outHeight, unsigned(format), fromLinearToSrgb));
    return true;
#else
    (void)device; (void)queue; (void)inWidth; (void)inHeight; (void)outWidth; (void)outHeight; (void)version11; (void)fromLinearToSrgb; (void)format;
    log::error("amf-video-sr", "this build has no AMD HQ Scaler headers");
    return false;
#endif
}

bool AmfVideoSrBackend::copyIn(ID3D12GraphicsCommandList* list, ID3D12Resource* input) {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    auto& p = *p_;
    const auto wait = fenceValue(p.inputFence.Get());
    if (wait && !dxOk("wait for input reuse", p.queue->Wait(p.inputFence.Get(), wait))) return false;
    barrier(list, p.input.Get(), resourceState(p.input.Get()), D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(p.input.Get(), input);
    barrier(list, p.input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    setResourceState(p.input.Get(), D3D12_RESOURCE_STATE_COMMON);
    return true;
#else
    (void)list; (void)input;
    return false;
#endif
}

bool AmfVideoSrBackend::evaluate() {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    auto& p = *p_;
    // The producer list holding copyIn has already been submitted.
    const auto written = fenceValue(p.inputFence.Get()) + 1;
    if (!dxOk("signal input copy", p.queue->Signal(p.inputFence.Get(), written))) return false;
    setFenceValue(p.inputFence.Get(), written);
    amf::AMFSurfacePtr surface;
    if (!amfOk("wrap input", p.context->CreateSurfaceFromDX12Native(p.input.Get(), &surface, nullptr)) || !surface)
        return false;
    surface->SetPts(++p.sequence);
    if (!amfOk("SubmitInput", p.scaler->SubmitInput(surface))) return false;
    amf::AMFDataPtr data;
    if (!amfOk("QueryOutput", p.scaler->QueryOutput(&data))) return false;
    p.output = amf::AMFSurfacePtr(data);
    if (!p.output || p.output->GetPts() != surface->GetPts() ||
        p.output->GetMemoryType() != amf::AMF_MEMORY_DX12 || p.output->GetFormat() != p.format) {
        log::error("amf-video-sr", "driver returned a different frame, memory type or format");
        return false;
    }
    auto* plane = p.output->GetPlaneAt(0);
    if (!plane || unsigned(plane->GetWidth()) != p.width || unsigned(plane->GetHeight()) != p.height) {
        log::error("amf-video-sr", "driver returned a different output extent");
        return false;
    }
    p.outputTexture = static_cast<ID3D12Resource*>(plane->GetNative());
    if (p.outputTexture) p.outputFence = resourceFence(p.outputTexture);
    if (!p.outputFence || !fenceValue(p.outputFence.Get())) {
        log::error("amf-video-sr", "driver returned no D3D12 output fence point");
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AmfVideoSrBackend::copyOut(ID3D12GraphicsCommandList* list, ID3D12Resource* output) {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    auto& p = *p_;
    if (!dxOk("wait for scaled frame", p.queue->Wait(p.outputFence.Get(), fenceValue(p.outputFence.Get()))))
        return false;
    const auto previous = resourceState(p.outputTexture);
    barrier(list, p.outputTexture, previous, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = p.outputTexture;
    dst.pResource = output;
    const D3D12_BOX box{0, 0, 0, p.width, p.height, 1};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    barrier(list, p.outputTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, previous);
    p.recordedOutput = true;
    return true;
#else
    (void)list; (void)output;
    return false;
#endif
}

bool AmfVideoSrBackend::retireSubmitted() {
#ifdef VEYRA_HAS_AMF_HQ_SCALER
    auto& p = *p_;
    if (!p.recordedOutput) return true;
    // Returning the surface to AMF before the consumer fence is submitted can
    // let the driver overwrite it before the graph's copy has completed.
    const auto copied = fenceValue(p.outputFence.Get()) + 1;
    if (!dxOk("signal output copy", p.queue->Signal(p.outputFence.Get(), copied))) return false;
    setFenceValue(p.outputFence.Get(), copied);
    p.output = nullptr;
    p.outputTexture = nullptr;
    p.outputFence.Reset();
    p.recordedOutput = false;
#endif
    return true;
}

} // namespace veyra::gfx
