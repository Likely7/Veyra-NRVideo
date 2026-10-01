#include "veyra/media/FFmpegVideoDecoder.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#include <format>
#include <algorithm>

#include "veyra/Log.h"

namespace veyra::media {

namespace {

using Microsoft::WRL::ComPtr;

std::string hrText(HRESULT hr)
{
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}

// get_format: only accept D3D12 (Playbook 13.2 get_format contract).
enum AVPixelFormat SelectD3D12Format(struct AVCodecContext* /*ctx*/, const enum AVPixelFormat* pixFmts)
{
    for (const enum AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D12) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

// get_format for the D3D11VA path: only D3D11 surfaces are importable.
enum AVPixelFormat SelectD3D11Format(struct AVCodecContext* /*ctx*/, const enum AVPixelFormat* pixFmts)
{
    for (const enum AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D11) {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    close();
}

bool FFmpegVideoDecoder::openSoftware(const AVCodecParameters* codecParameters,
    int streamTimeBaseNum, int streamTimeBaseDen, unsigned softwareThreads, bool lowLatency)
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        close();
    }
    if (codecParameters == nullptr) {
        return false;
    }
    frameTimeBaseNum_ = streamTimeBaseNum;
    frameTimeBaseDen_ = streamTimeBaseDen;

    // The FFmpeg native AV1 decoder is hardware-path oriented in the
    // pinned Windows build.  When the optional LGPL-compatible dav1d
    // backend is present, prefer it for software playback so AV1 files do
    // not fail after the demuxer has already accepted them.  Keep the
    // native decoder as a fallback for installations that do not ship
    // dav1d (and let the caller report the actual decode error).
    const AVCodec* codec = nullptr;
    if (codecParameters->codec_id == AV_CODEC_ID_AV1) {
        codec = avcodec_find_decoder_by_name("libdav1d");
    }
    if (codec == nullptr) {
        codec = avcodec_find_decoder(codecParameters->codec_id);
    }
    if (codec == nullptr) {
        log::error("media", std::format("decoder: no software decoder for codecId={}", static_cast<int>(codecParameters->codec_id)));
        return false;
    }

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        return false;
    }
    if (avcodec_parameters_to_context(context_, codecParameters) < 0) {
        log::error("media", "decoder: avcodec_parameters_to_context failed");
        avcodec_free_context(&context_);
        return false;
    }
    // Legacy bounded packet pumps keep the default single thread. The file
    // source can supply compressed lookahead until receiveFrame succeeds and
    // explicitly opts into at most four codec workers; no application frame
    // queue and no capture lookahead are introduced. Streaming fallbacks ask
    // for up to eight low-delay workers (dav1d then threads inside one frame).
    context_->thread_count = int(std::clamp(softwareThreads,1u,8u));
    context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (lowLatency) {
        // Live capture: never trade latency for throughput. Frame threading
        // buffers whole frames before returning any output, so slice-only
        // execution plus the codec's low-delay flag keeps every decoded frame
        // available as soon as it is complete.
        context_->thread_type = FF_THREAD_SLICE;
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    if (avcodec_open2(context_, codec, nullptr) < 0) {
        log::error("media", "decoder: avcodec_open2 failed");
        avcodec_free_context(&context_);
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        avcodec_free_context(&context_);
        return false;
    }

    stats_ = DecoderStats{};
    log::info("media", std::format("decoder: software decoder opened codec={} {}x{} pixFmt={} threads={} activeThreadType={} lowLatency={}",
        codec->name, context_->width, context_->height, static_cast<int>(context_->pix_fmt),context_->thread_count,context_->active_thread_type,lowLatency));
    return true;
}

bool FFmpegVideoDecoder::openD3D12VA(const AVCodecParameters* codecParameters,
    int streamTimeBaseNum, int streamTimeBaseDen,
    ID3D12Device* device, ID3D12CommandQueue* queue, bool lowLatency)
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        close();
    }
    if (codecParameters == nullptr || device == nullptr || queue == nullptr) {
        return false;
    }
    frameTimeBaseNum_ = streamTimeBaseNum;
    frameTimeBaseDen_ = streamTimeBaseDen;

    const AVCodec* codec = preferNativeAv1_ && codecParameters->codec_id == AV_CODEC_ID_AV1 ? avcodec_find_decoder_by_name("av1") : nullptr;
    if (codec == nullptr) codec = avcodec_find_decoder(codecParameters->codec_id);
    if (codec == nullptr) {
        log::error("media", "decoder: no decoder for d3d12va codec");
        return false;
    }

    // Shared Veyra device wrapped in a D3D12VA hw device context
    // (Playbook 13.2). FFmpeg owns one reference via the buffer.
    AVBufferRef* hwDeviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D12VA);
    if (hwDeviceRef == nullptr) {
        log::error("media", "decoder: av_hwdevice_ctx_alloc(D3D12VA) failed");
        return false;
    }
    auto* hwDevice = reinterpret_cast<AVHWDeviceContext*>(hwDeviceRef->data);
    auto* hwctx = reinterpret_cast<AVD3D12VADeviceContext*>(hwDevice->hwctx);
    hwctx->device = device;
    device->AddRef(); // FFmpeg context owns/releases this interface
    const int initResult = av_hwdevice_ctx_init(hwDeviceRef);
    if (initResult < 0) {
        log::error("media", std::format("decoder: av_hwdevice_ctx_init failed code={}", initResult));
        av_buffer_unref(&hwDeviceRef);
        return false;
    }

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        av_buffer_unref(&hwDeviceRef);
        return false;
    }
    if (avcodec_parameters_to_context(context_, codecParameters) < 0) {
        avcodec_free_context(&context_);
        av_buffer_unref(&hwDeviceRef);
        return false;
    }
    context_->thread_count = 1;
    context_->get_format = SelectD3D12Format;
    context_->hw_device_ctx = av_buffer_ref(hwDeviceRef);
    if (lowLatency) {
        // Live capture must not wait for the decoder's reorder window.
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    av_buffer_unref(&hwDeviceRef); // codec ctx holds its own reference now

    if (avcodec_open2(context_, codec, nullptr) < 0) {
        log::error("media", "decoder: avcodec_open2 failed for d3d12va");
        avcodec_free_context(&context_);
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        avcodec_free_context(&context_);
        return false;
    }
    stats_ = DecoderStats{};
    hwAccelActive_ = true;
    gpuQueueWaitCount_ = 0;
    log::info("media", std::format("decoder: d3d12va decoder opened codec={} {}x{} lowLatency={} (shared Veyra device)",
        codec->name, context_->width, context_->height, lowLatency));
    return true;
}

void FFmpegVideoDecoder::releaseInterop()
{
    for (auto& entry : d3d11Imports_) {
        if (entry.second.resource != nullptr) {
            entry.second.resource->Release();
        }
    }
    d3d11Imports_.clear();
    for (auto& slot : interopSlots_) {
        if (slot.resource != nullptr) {
            slot.resource->Release();
            slot.resource = nullptr;
        }
        if (slot.texture != nullptr) {
            slot.texture->Release();
            slot.texture = nullptr;
        }
        slot.lastFenceValue = 0;
        slot.width = slot.height = 0;
    }
    interopFrameIndex_ = 0;
    interopSlotIndex_ = 0;
    hardwareSurface_ = HardwareSurfaceView{};
    if (d3d12Fence_ != nullptr) {
        d3d12Fence_->Release();
        d3d12Fence_ = nullptr;
    }
    if (d3d11Fence_ != nullptr) {
        d3d11Fence_->Release();
        d3d11Fence_ = nullptr;
    }
    if (d3d11Context4_) { d3d11Context4_->Release(); d3d11Context4_ = nullptr; }
    d3d11Context_ = nullptr;  // borrowed from the FFmpeg device context
    d3d12Device_ = nullptr;   // borrowed from the caller
    interopFenceValue_ = 0;
    if (d3d11DeviceRef_ != nullptr) {
        // Releases the FFmpeg device context (and its reference on our device).
        av_buffer_unref(&d3d11DeviceRef_);
    }
    if (d3d11Device_ != nullptr) {
        d3d11Device_->Release();
        d3d11Device_ = nullptr;
    }
}

bool FFmpegVideoDecoder::createInteropFence(ID3D12Device* d3d12Device)
{
    if (d3d11DeviceRef_ == nullptr || d3d11Device_ == nullptr || d3d12Device == nullptr) {
        return false;
    }
    auto* hwDevice = reinterpret_cast<AVHWDeviceContext*>(d3d11DeviceRef_->data);
    auto* hwctx = reinterpret_cast<AVD3D11VADeviceContext*>(hwDevice->hwctx);
    if (hwctx->device_context == nullptr) {
        log::error("media", "decoder: d3d11va device context missing; cannot create the interop fence");
        return false;
    }
    d3d12Device_ = d3d12Device;

    ComPtr<ID3D11Device5> device5;
    HRESULT hr = d3d11Device_->QueryInterface(IID_PPV_ARGS(&device5));
    if (FAILED(hr) || !device5) {
        log::error("media", std::format("decoder: ID3D11Device5 unavailable hr={} (Windows 10 1703+ required for shared fences)", hrText(hr)));
        return false;
    }
    ComPtr<ID3D11Fence> fence11;
    hr = device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence11));
    if (FAILED(hr) || !fence11) {
        log::error("media", std::format("decoder: ID3D11Device5::CreateFence(shared) failed hr={}", hrText(hr)));
        return false;
    }
    HANDLE shared = nullptr;
    hr = fence11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared);
    if (FAILED(hr) || shared == nullptr) {
        log::error("media", std::format("decoder: ID3D11Fence::CreateSharedHandle failed hr={}", hrText(hr)));
        return false;
    }
    ComPtr<ID3D12Fence> fence12;
    const HRESULT openHr = d3d12Device->OpenSharedHandle(shared, IID_PPV_ARGS(&fence12));
    CloseHandle(shared);
    if (FAILED(openHr) || !fence12) {
        log::error("media", std::format("decoder: D3D12 OpenSharedHandle(fence) failed hr={}", hrText(openHr)));
        return false;
    }
    ComPtr<ID3D11DeviceContext4> context4;
    hr = hwctx->device_context->QueryInterface(IID_PPV_ARGS(&context4));
    if (FAILED(hr) || !context4) {
        log::error("media", std::format("decoder: ID3D11DeviceContext4 unavailable hr={}", hrText(hr)));
        return false;
    }
    d3d11Context_ = hwctx->device_context; // borrowed, FFmpeg owns it
    d3d11Fence_ = fence11.Detach();
    d3d12Fence_ = fence12.Detach();
    d3d11Context4_ = context4.Detach();
    log::info("media", "decoder: d3d11va interop fence ready (D3D11 signal -> D3D12 queue wait, no CPU wait)");
    return true;
}

bool FFmpegVideoDecoder::createInteropSlots(const void* sourceDescVoid)
{
    if (sourceDescVoid == nullptr || d3d11Device_ == nullptr || d3d12Device_ == nullptr) {
        return false;
    }
    const auto& sourceDesc = *static_cast<const D3D11_TEXTURE2D_DESC*>(sourceDescVoid);
    for (auto& slot : interopSlots_) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = sourceDesc.Width;
        desc.Height = sourceDesc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sourceDesc.Format;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // NT-handle sharing requires the legacy SHARED flag as well; the
        // keyed mutex is not used because ordering is done with the fence pair.
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        const HRESULT createHr = d3d11Device_->CreateTexture2D(&desc, nullptr, &slot.texture);
        if (FAILED(createHr) || slot.texture == nullptr) {
            log::error("media", std::format("decoder: shared copy texture {}x{} format={} failed hr={}",
                desc.Width, desc.Height, static_cast<int>(desc.Format), hrText(createHr)));
            return false;
        }
        ComPtr<IDXGIResource1> dxgiResource;
        HRESULT hr = slot.texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));
        HANDLE shared = nullptr;
        if (SUCCEEDED(hr) && dxgiResource) {
            hr = dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &shared);
        }
        if (FAILED(hr) || shared == nullptr) {
            log::error("media", std::format("decoder: shared copy texture handle failed hr={}", hrText(hr)));
            return false;
        }
        ComPtr<ID3D12Resource> resource;
        const HRESULT openHr = d3d12Device_->OpenSharedHandle(shared, IID_PPV_ARGS(&resource));
        CloseHandle(shared);
        if (FAILED(openHr) || !resource) {
            log::error("media", std::format("decoder: D3D12 open of the shared copy texture failed hr={}", hrText(openHr)));
            return false;
        }
        slot.resource = resource.Detach();
        slot.width = desc.Width;
        slot.height = desc.Height;
    }
    log::info("media", std::format("decoder: d3d11va interop ring ready slots={} surface={}x{} format={}",
        kInteropSlotCount, sourceDesc.Width, sourceDesc.Height, static_cast<int>(sourceDesc.Format)));
    return true;
}

bool FFmpegVideoDecoder::openD3D11VA(const AVCodecParameters* codecParameters,
    int streamTimeBaseNum, int streamTimeBaseDen,
    uint64_t adapterLuid, ID3D12Device* d3d12Device, bool lowLatency)
{
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        close();
    }
    if (codecParameters == nullptr || d3d12Device == nullptr) {
        return false;
    }
    frameTimeBaseNum_ = streamTimeBaseNum;
    frameTimeBaseDen_ = streamTimeBaseDen;

    const AVCodec* codec = preferNativeAv1_ && codecParameters->codec_id == AV_CODEC_ID_AV1 ? avcodec_find_decoder_by_name("av1") : nullptr;
    if (codec == nullptr) codec = avcodec_find_decoder(codecParameters->codec_id);
    if (codec == nullptr) {
        log::error("media", "decoder: no decoder for d3d11va codec");
        return false;
    }

    // The D3D11 device has to live on the same adapter as the Veyra D3D12
    // device, otherwise the decoded surfaces cannot be opened by D3D12.
    const LUID luid{ static_cast<DWORD>(adapterLuid & 0xFFFFFFFFull),
        static_cast<LONG>(adapterLuid >> 32) };
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(hr) && factory) {
        hr = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    }
    if (FAILED(hr) || !adapter) {
        log::error("media", std::format("decoder: cannot resolve the D3D12 adapter LUID 0x{:X}:0x{:X} for D3D11VA hr={}",
            uint32_t(adapterLuid >> 32), uint32_t(adapterLuid & 0xFFFFFFFFull), hrText(hr)));
        return false;
    }

    // D3D11_CREATE_DEVICE_VIDEO_SUPPORT is the documented flag for decode
    // devices; some adapters refuse it, so retry without it before giving up.
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel{};
    ComPtr<ID3D11DeviceContext> immediate;
    hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, UINT(sizeof(levels)/sizeof(levels[0])), D3D11_SDK_VERSION,
        &d3d11Device_, &featureLevel, &immediate);
    if (FAILED(hr)) {
        log::warn("media", std::format("decoder: D3D11CreateDevice(video support) failed hr={}; retrying without the flag", hrText(hr)));
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            0, levels, UINT(sizeof(levels)/sizeof(levels[0])), D3D11_SDK_VERSION,
            &d3d11Device_, &featureLevel, &immediate);
    }
    if (FAILED(hr) || d3d11Device_ == nullptr) {
        log::error("media", std::format("decoder: D3D11CreateDevice failed hr={}", hrText(hr)));
        d3d11Device_ = nullptr;
        return false;
    }
    d3d11Device_->AddRef(); // FFmpeg's device context owns one reference
    log::info("media", std::format("decoder: d3d11va device created featureLevel=0x{:X} luid=0x{:X}",
        static_cast<unsigned>(featureLevel), adapterLuid));

    AVBufferRef* deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (deviceRef == nullptr) {
        log::error("media", "decoder: av_hwdevice_ctx_alloc(D3D11VA) failed");
        d3d11Device_->Release();
        d3d11Device_ = nullptr;
        return false;
    }
    {
        auto* hwDevice = reinterpret_cast<AVHWDeviceContext*>(deviceRef->data);
        auto* hwctx = reinterpret_cast<AVD3D11VADeviceContext*>(hwDevice->hwctx);
        hwctx->device = d3d11Device_;
        // The decoder pool itself is left exactly as FFmpeg builds it: the
        // NVIDIA driver rejects SHARED_NTHANDLE on DXVA decoder output textures
        // (CreateTexture2D -> E_INVALIDARG, 0x80070057 on RTX 5070/616.56).
        // Decoded slices are copied into our own shared textures instead; see
        // receiveFrame().
        if (av_hwdevice_ctx_init(deviceRef) < 0) {
            log::error("media", "decoder: av_hwdevice_ctx_init(D3D11VA) failed");
            av_buffer_unref(&deviceRef);
            d3d11Device_->Release();
            d3d11Device_ = nullptr;
            return false;
        }
    }
    log::info("media", "decoder: d3d11va hw device context initialized");
    d3d11DeviceRef_ = deviceRef;

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        releaseInterop();
        return false;
    }
    if (avcodec_parameters_to_context(context_, codecParameters) < 0) {
        log::error("media", "decoder: avcodec_parameters_to_context failed (d3d11va)");
        avcodec_free_context(&context_);
        releaseInterop();
        return false;
    }
    context_->thread_count = 1;
    if (lowLatency) {
        context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    // FFmpeg's D3D11VA frame parameters pick P010 only when the software format
    // is already declared; without this a 10-bit stream would be handed NV12
    // surfaces. Set the same value ff_get_format would have chosen.
    if (context_->sw_pix_fmt == AV_PIX_FMT_NONE) {
        const bool tenBit = codecParameters->format == AV_PIX_FMT_YUV420P10
            || codecParameters->format == AV_PIX_FMT_YUV420P12
            || codecParameters->bits_per_raw_sample > 8;
        context_->sw_pix_fmt = tenBit ? AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;
    }

    context_->get_format = SelectD3D11Format;
    context_->hw_device_ctx = av_buffer_ref(d3d11DeviceRef_);

    if (avcodec_open2(context_, codec, nullptr) < 0) {
        log::error("media", "decoder: avcodec_open2 failed for d3d11va");
        avcodec_free_context(&context_);
        releaseInterop();
        return false;
    }
    // The hardware frames context is created lazily by the decoder on the first
    // frame (ff_get_format), so the pooled surface is validated there instead of
    // here; see the D3D11VA branch of receiveFrame().
    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        avcodec_free_context(&context_);
        releaseInterop();
        return false;
    }

    stats_ = DecoderStats{};
    hwAccelActive_ = true;
    hwAccelKind_ = HardwareDecodeKind::D3D11VA;
    gpuQueueWaitCount_ = 0;
    if (!createInteropFence(d3d12Device)) {
        close();
        return false;
    }
    log::info("media", std::format("decoder: d3d11va decoder opened codec={} {}x{} lowLatency={} (private device, shared surfaces)",
        codec->name, context_->width, context_->height, lowLatency));
    return true;
}

ID3D12Resource* FFmpegVideoDecoder::importD3D11Texture(ID3D11Texture2D* texture)
{
    if (texture == nullptr || d3d12Device_ == nullptr) {
        return nullptr;
    }
    const auto cached = d3d11Imports_.find(texture);
    if (cached != d3d11Imports_.end()) {
        return cached->second.resource;
    }
    if (d3d11Imports_.size() >= 32) {
        // A decoder pool is a single array texture, so this only happens when a
        // driver rebuilds pools repeatedly; drop the views and re-open lazily.
        for (auto& entry : d3d11Imports_) {
            if (entry.second.resource != nullptr) {
                entry.second.resource->Release();
            }
        }
        d3d11Imports_.clear();
    }

    ComPtr<IDXGIResource1> dxgiResource;
    HRESULT hr = texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));
    if (FAILED(hr) || !dxgiResource) {
        log::error("media", std::format("decoder: decoded texture has no IDXGIResource1 hr={}", hrText(hr)));
        return nullptr;
    }
    HANDLE shared = nullptr;
    hr = dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &shared);
    if (FAILED(hr) || shared == nullptr) {
        log::error("media", std::format("decoder: CreateSharedHandle(decoded texture) failed hr={}", hrText(hr)));
        return nullptr;
    }
    ComPtr<ID3D12Resource> resource;
    const HRESULT openHr = d3d12Device_->OpenSharedHandle(shared, IID_PPV_ARGS(&resource));
    CloseHandle(shared);
    if (FAILED(openHr) || !resource) {
        log::error("media", std::format("decoder: D3D12 OpenSharedHandle(decoded texture) failed hr={}", hrText(openHr)));
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const auto d3d12Desc = resource->GetDesc();
    log::info("media", std::format(
        "decoder: imported d3d11va surface {}x{} array={} format={} -> d3d12 format={} arraySize={}",
        desc.Width, desc.Height, desc.ArraySize, static_cast<int>(desc.Format),
        static_cast<int>(d3d12Desc.Format), d3d12Desc.DepthOrArraySize));

    ID3D12Resource* raw = resource.Detach();
    d3d11Imports_.emplace(texture, D3D11TextureImport{ raw, true });
    return raw;
}

void FFmpegVideoDecoder::close()
{
    for (auto* buffered : bufferedFrames_) av_frame_free(&buffered);
    bufferedFrames_.clear();
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }
    if (context_ != nullptr) {
        avcodec_free_context(&context_);
    }
    releaseInterop();
    hwAccelActive_ = false;
    hwAccelKind_ = HardwareDecodeKind::None;
    lastFrameFormat_ = -1;
    gpuQueueWaitCount_ = 0;
}

bool FFmpegVideoDecoder::hardwareFrameImportable() const
{
    if (!hardwareActive() || frame_ == nullptr) {
        return false;
    }
    if (hwAccelKind_ == HardwareDecodeKind::D3D11VA) {
        if (frame_->format != AV_PIX_FMT_D3D11 || hardwareSurface_.texture == nullptr) {
            return false;
        }
        auto* decoded = reinterpret_cast<ID3D11Texture2D*>(frame_->data[0]);
        if (decoded == nullptr) {
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        decoded->GetDesc(&desc);
        if (desc.MipLevels != 1 || desc.SampleDesc.Count != 1 || desc.ArraySize == 0) {
            return false;
        }
        if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) {
            return false;
        }
        // The shared copy keeps the decoder's aligned extent (128 px for HEVC),
        // which legitimately exceeds the visible size: the ingress samples by
        // texel index and never reads the pad.
        return context_ != nullptr
            && hardwareSurface_.textureWidth >= static_cast<uint32_t>(context_->width)
            && hardwareSurface_.textureHeight >= static_cast<uint32_t>(context_->height);
    }
    if (frame_->format != AV_PIX_FMT_D3D12) {
        return false;
    }
    auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame_->data[0]);
    if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
        return false;
    }
    const auto desc = d3dFrame->texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize == 0 ||
        desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
        return false;
    }
    if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) {
        return false;
    }
    return context_ != nullptr && desc.Width == static_cast<UINT64>(context_->width) &&
        desc.Height == static_cast<UINT>(context_->height);
}

int FFmpegVideoDecoder::width() const
{
    return context_ != nullptr ? context_->width : 0;
}

int FFmpegVideoDecoder::height() const
{
    return context_ != nullptr ? context_->height : 0;
}

int FFmpegVideoDecoder::pixelFormat() const
{
    return context_ != nullptr ? static_cast<int>(context_->pix_fmt) : -1;
}

bool FFmpegVideoDecoder::sendPacket(const AVPacket* packet)
{
    if (context_ == nullptr) {
        return false;
    }
    int result = avcodec_send_packet(context_, packet);
    while (result == AVERROR(EAGAIN)) {
        ++stats_.packetRetries;
        // Bound: each buffered frame pins a decoder pool surface on the
        // hardware paths (pool ~20); keep well below it (sweep 2026-09-22 C9).
        if (bufferedFrames_.size() >= (hardwareActive() ? 8u : 16u)) {
            log::error("media", "decoder: output queue full; caller must consume frames before sending more input");
            return false;
        }
        AVFrame* buffered = av_frame_alloc();
        if (!buffered) return false;
        const int received = avcodec_receive_frame(context_, buffered);
        if (received < 0) {
            av_frame_free(&buffered);
            log::error("media", std::format("decoder: send EAGAIN without receive progress code={}", received));
            return false;
        }
        bufferedFrames_.push_back(buffered);
        result = avcodec_send_packet(context_, packet);
    }
    if (result == AVERROR_EOF && packet == nullptr) return true;
    if (result < 0) {
        char errorText[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, errorText, sizeof(errorText));
        log::error("media", std::format("decoder: send_packet failed code={} text={}", result, errorText));
        return false;
    }
    if (packet != nullptr) {
        ++stats_.framesSubmitted;
    }
    return true;
}

const AVFrame* FFmpegVideoDecoder::receiveFrame()
{
    if (context_ == nullptr) {
        receiveStatus_ = DecodeReceiveStatus::Error;
        return nullptr;
    }
    int result = 0;
    if (!bufferedFrames_.empty()) {
        av_frame_unref(frame_);
        AVFrame* buffered = bufferedFrames_.front();
        bufferedFrames_.pop_front();
        av_frame_move_ref(frame_, buffered);
        av_frame_free(&buffered);
    } else result = avcodec_receive_frame(context_, frame_);
    if (result < 0) {
        if (result == AVERROR(EAGAIN)) receiveStatus_ = DecodeReceiveStatus::NeedInput;
        else if (result == AVERROR_EOF) receiveStatus_ = DecodeReceiveStatus::EndOfStream;
        else {
            receiveStatus_ = DecodeReceiveStatus::Error;
            char errorText[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(result, errorText, sizeof(errorText));
            log::error("media", std::format("decoder: receive_frame failed code={} text={}", result, errorText));
        }
        return nullptr;
    }
    receiveStatus_ = DecodeReceiveStatus::Frame;
    if (frame_->flags & AV_FRAME_FLAG_KEY) context_->skip_frame = AVDISCARD_DEFAULT;
    ++stats_.framesDecoded;
    // Frame timestamps are in the CODEC context time_base; AVFrame.time_base
    // is not reliably populated by every decoder path.
    const int64_t stamp = frame_->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame_->best_effort_timestamp
        : (frame_->pts != AV_NOPTS_VALUE ? frame_->pts : frame_->pkt_dts);
    // Prefer the frame's own base; fall back to the codec context base, then
    // to the demuxer stream base passed at open (observed: this FFmpeg passes
    // stream-base PTS through while the codec context base stays {0,1}).
    AVRational base = frame_->time_base;
    if (base.num == 0 || base.den == 0) {
        base = context_->time_base;
    }
    if (base.num == 0 || base.den == 0) {
        base.num = frameTimeBaseNum_;
        base.den = frameTimeBaseDen_;
    }
    const int64_t ptsUs = base.den > 0 ? av_rescale_q(stamp, base, { 1, 1000000 }) : 0;
    lastFrameFormat_ = frame_->format;
    if (hwAccelActive_ && frame_->format == AV_PIX_FMT_D3D12) {
        // GPU queue wait on the frame's sync fence (Playbook 13.2: a GPU-side
        // wait, never a CPU WaitForSingleObject per frame).
        auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame_->data[0]);
        if (d3dFrame != nullptr && d3dFrame->sync_ctx.fence != nullptr) {
            // Note: the wait target is the Veyra direct queue when the frame
            // consumer runs there; for the probe we count the wait and rely
            // on the next GPU operation to serialize. The real pipeline (P3.4+)
            // issues queue->Wait before touching the texture.
            ++gpuQueueWaitCount_;
        }
    } else if (hwAccelKind_ == HardwareDecodeKind::D3D11VA && frame_->format == AV_PIX_FMT_D3D11) {
        // The decoded slice is copied into our own shared texture (the driver
        // refuses NT-handle sharing on DXVA decoder output textures), then the
        // D3D11 fence is signalled so the graph can enqueue one GPU-side wait.
        // Nothing here waits on the CPU.
        hardwareSurface_ = HardwareSurfaceView{};
        auto* decoded = reinterpret_cast<ID3D11Texture2D*>(frame_->data[0]);
        if (decoded == nullptr) {
            log::error("media", "decoder: d3d11va frame without a texture");
            return frame_;
        }
        D3D11_TEXTURE2D_DESC desc{};
        decoded->GetDesc(&desc);
        if (stats_.framesDecoded == 1) {
            log::info("media", std::format(
                "decoder: d3d11va decoded surface {}x{} visible={}x{} array={} slice={} format={} bind=0x{:X} misc=0x{:X}",
                desc.Width, desc.Height, frame_->width, frame_->height, desc.ArraySize,
                static_cast<uint32_t>(reinterpret_cast<intptr_t>(frame_->data[1])),
                static_cast<int>(desc.Format), desc.BindFlags, desc.MiscFlags));
            if (!createInteropSlots(&desc)) {
                log::error("media", "decoder: d3d11va interop ring creation failed");
                return frame_;
            }
        }
        if (d3d11Context_ == nullptr || d3d11Context4_ == nullptr || d3d11Fence_ == nullptr || d3d12Fence_ == nullptr) {
            log::error("media", "decoder: d3d11va interop state missing; frame cannot be ordered");
            return frame_;
        }
        // Ring reuse is safe by construction: the app submits one frame per
        // read() and its six command slots force the GPU to finish frame N-6
        // before frame N is submitted, so a slot rewritten eight frames later
        // can no longer be in flight on the D3D12 queue. The previous copy into
        // this slot is also ordered by a GPU-side fence wait on the D3D11 side.
        interopSlotIndex_ = static_cast<uint32_t>(interopFrameIndex_ % kInteropSlotCount);
        auto& slot = interopSlots_[interopSlotIndex_];
        ++interopFrameIndex_;
        if (slot.texture == nullptr) {
            log::error("media", "decoder: d3d11va interop slot missing");
            return frame_;
        }
        if (slot.lastFenceValue != 0) {
            d3d11Context4_->Wait(d3d11Fence_, slot.lastFenceValue);
        }
        const UINT slice = static_cast<UINT>(reinterpret_cast<intptr_t>(frame_->data[1]));
        d3d11Context_->CopySubresourceRegion(slot.texture, 0, 0, 0, 0, decoded, slice, nullptr);
        ++interopFenceValue_;
        const HRESULT hr = d3d11Context4_->Signal(d3d11Fence_, interopFenceValue_);
        if (SUCCEEDED(hr)) {
            d3d11Context4_->Flush();
            slot.lastFenceValue = interopFenceValue_;
            hardwareSurface_.waitFence = d3d12Fence_;
            hardwareSurface_.waitValue = interopFenceValue_;
            ++gpuQueueWaitCount_;
        } else {
            log::error("media", std::format("decoder: d3d11va fence signal failed hr={}", hrText(hr)));
        }
        hardwareSurface_.texture = slot.resource;
        hardwareSurface_.subresourceIndex = 0;
        hardwareSurface_.textureWidth = slot.width;
        hardwareSurface_.textureHeight = slot.height;
        if (stats_.framesDecoded <= 3) {
            log::info("media", std::format("decoder: d3d11va copy slot={} fence={} sourceSlice={} extent={}x{}",
                interopSlotIndex_, hardwareSurface_.waitValue, slice, slot.width, slot.height));
        }
        if (hardwareSurface_.texture == nullptr) {
            log::error("media", "decoder: d3d11va surface import failed");
        }
    }
    if (stats_.framesDecoded <= 3) {
        log::info("media", std::format("decoder: frame#{} stamp={} tb={}/{} -> ptsUs={} format={}",
            stats_.framesDecoded, stamp, base.num, base.den, ptsUs, frame_->format));
    }
    if (stats_.framesDecoded == 1) {
        stats_.firstPts = ptsUs;
    }
    else if (ptsUs < stats_.lastPts) {
        ++stats_.ptsNonMonotonicCount;
    }
    stats_.lastPts = ptsUs;
    return frame_;
}

void FFmpegVideoDecoder::flushBuffers()
{
    for (auto* buffered : bufferedFrames_) av_frame_free(&buffered);
    bufferedFrames_.clear();
    receiveStatus_ = DecodeReceiveStatus::NeedInput;
    if (context_ != nullptr) {
        avcodec_flush_buffers(context_);
        context_->skip_frame = AVDISCARD_DEFAULT;
    }
}

void FFmpegVideoDecoder::recoverAtKeyframe()
{
    flushBuffers();
    if (context_) context_->skip_frame = AVDISCARD_NONKEY;
}

} // namespace veyra::media
