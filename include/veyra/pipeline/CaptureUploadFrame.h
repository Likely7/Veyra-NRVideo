#pragma once
// Capture frames that live in a D3D12 upload buffer (latency work 2026-09-30).
//
// A native capture frame used to be copied twice before the GPU saw it: driver
// sample -> owned AVFrame in the capture callback, then AVFrame -> upload heap
// in the graph. On the test machine each copy is memory-bandwidth bound (4K
// NV12: 1.3 ms + 0.9 ms of a 3.3 ms capture-to-present path with all effects
// off), and splitting it over threads gained nothing. Here the capture source's
// own frames ARE upload buffers, so the callback's copy is the only one and the
// graph records its CopyTextureRegion straight from the frame.
//
// Layouts: semiplanar NV12/P010/P016 (two planes in one buffer) and the packed
// single-plane formats the graph converts on the GPU (YUY2, UYVY, YVYU, BGR24,
// RGB555, RGB565). Planar I420/YV12 is interleaved into an NV12 frame by the
// callback (interleaveCaptureI420), which replaces the graph's swscale pass.
//
// Rules:
//  - The frame keeps normal AVFrame semantics (data/linesize, ref-counted), so
//    any consumer that does not know this header still works; it just reads
//    write-combined memory, which is slow.
//  - The graph stores the fence of the list that reads the buffer. While that
//    fence is pending the callback must not write the buffer, so it fills the
//    frame's CPU planes instead (direct=false) and the graph copies as before.
//  - The 64x36 points the graph analyses are sampled in the callback from the
//    driver sample (raw pixel bytes, same packing as the frame), never read
//    back from the upload buffer.
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <mutex>
extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

namespace veyra::pipeline {

struct CaptureUploadFrame {
    static constexpr uint64_t kMagic = 0x5645595255504652ull;
    static constexpr unsigned kProxyWidth = 64, kProxyHeight = 36, kMaxPixelBytes = 4;
    uint64_t magic = kMagic;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint8_t* mapped = nullptr;      // plane 0 at 0, semiplanar chroma at chromaOffset
    uint8_t* cpu = nullptr;         // same layout; allocated on first fallback
    uint64_t chromaOffset = 0, bytes = 0;
    uint32_t pitch = 0, width = 0, height = 0;
    uint32_t planes = 1;            // 2 = semiplanar
    uint32_t pixelBytes = 1;        // bytes per pixel of plane 0 (NV12 1, P010/YUY2/RGB565 2, BGR24 3)
    bool direct = false;            // pixels of the current frame are in the upload buffer
    bool proxyValid = false;
    // kProxyWidth x kProxyHeight pixels of plane 0, pixelBytes each, row pitch
    // proxyPitch(): a tiny frame with the same packing as the real one.
    uint8_t proxy[kProxyWidth * kProxyHeight * kMaxPixelBytes] = {};
    int proxyPitch() const { return int(kProxyWidth * pixelBytes); }
    uint64_t fallbacks = 0;

    // Last GPU list that reads the upload buffer (set by the graph).
    void gpuRead(ID3D12Fence* object, uint64_t value) { std::lock_guard lock(mutex_); fence_ = object; fenceValue_ = value; }
    bool gpuBusy() { std::lock_guard lock(mutex_); return fence_ && fence_->GetCompletedValue() < fenceValue_; }

    ~CaptureUploadFrame() { if (resource && mapped) resource->Unmap(0, nullptr); av_free(cpu); }

private:
    std::mutex mutex_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    uint64_t fenceValue_ = 0;
};

inline CaptureUploadFrame* captureUploadFrame(const AVFrame* frame) {
    if (!frame || !frame->opaque_ref || frame->opaque_ref->size != sizeof(CaptureUploadFrame)) return nullptr;
    auto* info = reinterpret_cast<CaptureUploadFrame*>(frame->opaque_ref->data);
    return info->magic == CaptureUploadFrame::kMagic ? info : nullptr;
}

// Bytes per plane-0 pixel for a format this header can back; 0 = unsupported.
inline uint32_t captureUploadPixelBytes(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_NV12: return 1;
    case AV_PIX_FMT_P010: case AV_PIX_FMT_P016: return 2;
    case AV_PIX_FMT_YUYV422: case AV_PIX_FMT_UYVY422: case AV_PIX_FMT_YVYU422: return 2;
    case AV_PIX_FMT_RGB555LE: case AV_PIX_FMT_RGB565LE: return 2;
    case AV_PIX_FMT_BGR24: return 3;
    default: return 0;
    }
}

// Returns nullptr when the format is not supported or the buffer cannot be
// created; the caller then allocates an ordinary frame.
inline AVFrame* allocCaptureUploadFrame(ID3D12Device* device, AVPixelFormat format, unsigned width, unsigned height) {
    const uint32_t pixelBytes = captureUploadPixelBytes(format);
    if (!device || !width || !height || !pixelBytes) return nullptr;
    const bool semiplanar = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010 || format == AV_PIX_FMT_P016;
    // Packed rows are uploaded as RGBA8 texels, so a row is a whole number of texels.
    const uint32_t rowBytes = (width * pixelBytes + 3u) & ~3u;
    const uint32_t pitch = (rowBytes + 255u) & ~255u;                              // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
    const uint64_t chromaOffset = semiplanar ? (uint64_t(pitch) * height + 511u) & ~uint64_t(511) : 0; // ..._PLACEMENT_ALIGNMENT
    const uint64_t bytes = semiplanar ? chromaOffset + uint64_t(pitch) * ((height + 1) / 2) : uint64_t(pitch) * height;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto* info = new CaptureUploadFrame;
    void* mapped = nullptr; const D3D12_RANGE noRead{0, 0};
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&info->resource))) ||
        FAILED(info->resource->Map(0, &noRead, &mapped)) || !mapped) { delete info; return nullptr; }
    info->device = device; info->mapped = static_cast<uint8_t*>(mapped); info->chromaOffset = chromaOffset; info->bytes = bytes;
    info->pitch = pitch; info->width = width; info->height = height; info->planes = semiplanar ? 2u : 1u; info->pixelBytes = pixelBytes;
    // The packed texel upload reads up to 3 bytes past a BGR24 row: keep them defined.
    std::memset(info->mapped, 0, size_t(bytes));
    AVBufferRef* infoRef = av_buffer_create(reinterpret_cast<uint8_t*>(info), sizeof(CaptureUploadFrame),
        [](void*, uint8_t* data) { delete reinterpret_cast<CaptureUploadFrame*>(data); }, nullptr, 0);
    if (!infoRef) { delete info; return nullptr; }
    // The pixel buffer keeps the info (and so the mapped resource) alive.
    AVBufferRef* keep = av_buffer_ref(infoRef);
    AVBufferRef* pixels = keep ? av_buffer_create(info->mapped, size_t(bytes),
        [](void* opaque, uint8_t*) { auto* ref = static_cast<AVBufferRef*>(opaque); av_buffer_unref(&ref); }, keep, 0) : nullptr;
    AVFrame* frame = pixels ? av_frame_alloc() : nullptr;
    if (!frame) { if (pixels) av_buffer_unref(&pixels); else if (keep) av_buffer_unref(&keep); av_buffer_unref(&infoRef); return nullptr; }
    frame->format = format; frame->width = int(width); frame->height = int(height);
    frame->buf[0] = pixels; frame->opaque_ref = infoRef;
    frame->data[0] = info->mapped; frame->linesize[0] = int(pitch);
    if (semiplanar) { frame->data[1] = info->mapped + chromaOffset; frame->linesize[1] = int(pitch); }
    return frame;
}

// Capture callback, before it writes a frame: point the planes at the upload
// buffer, or at the CPU planes while the GPU still reads the buffer.
inline void selectCaptureUploadTarget(AVFrame& frame) {
    auto* info = captureUploadFrame(&frame);
    if (!info) return;
    uint8_t* base = info->mapped;
    info->direct = true; info->proxyValid = false;
    if (info->gpuBusy()) {
        if (!info->cpu) { info->cpu = static_cast<uint8_t*>(av_malloc(size_t(info->bytes))); if (info->cpu) std::memset(info->cpu, 0, size_t(info->bytes)); }
        if (info->cpu) { base = info->cpu; info->direct = false; ++info->fallbacks; }
    }
    frame.data[0] = base;
    if (info->planes == 2) frame.data[1] = base + info->chromaOffset;
}

// A writer that cannot supply the analysis pixels (generic swscale output)
// keeps the frame on its CPU planes; the graph then copies it as before.
inline void useCaptureUploadCpuPlanes(AVFrame& frame) {
    auto* info = captureUploadFrame(&frame);
    if (!info) return;
    if (!info->cpu) { info->cpu = static_cast<uint8_t*>(av_malloc(size_t(info->bytes))); if (info->cpu) std::memset(info->cpu, 0, size_t(info->bytes)); }
    if (!info->cpu) return;
    info->direct = false; info->proxyValid = false;
    frame.data[0] = info->cpu;
    if (info->planes == 2) frame.data[1] = info->cpu + info->chromaOffset;
}

// Capture callback, after the copy: the pixels the graph analyses, read from
// plane 0 of the driver sample. flip: destination row r came from source row
// height-1-r.
inline void sampleCaptureUploadProxy(AVFrame& frame, const uint8_t* source, size_t sourcePitch, bool flip) {
    auto* info = captureUploadFrame(&frame);
    if (!info || !source) return;
    const unsigned n = info->pixelBytes;
    uint8_t* out = info->proxy;
    for (unsigned y = 0; y < CaptureUploadFrame::kProxyHeight; ++y) {
        const unsigned row = y * info->height / CaptureUploadFrame::kProxyHeight;
        const uint8_t* line = source + size_t(flip ? info->height - 1 - row : row) * sourcePitch;
        for (unsigned x = 0; x < CaptureUploadFrame::kProxyWidth; ++x) {
            const uint8_t* pixel = line + size_t(x * info->width / CaptureUploadFrame::kProxyWidth) * n;
            for (unsigned k = 0; k < n; ++k) *out++ = pixel[k];
        }
    }
    info->proxyValid = true;
}

// Planar 4:2:0 (I420 / YV12) driver sample -> NV12 frame. u/v point at the
// first source row of each chroma plane; flip reverses the rows of every plane.
inline void interleaveCaptureI420(AVFrame& dst, const uint8_t* luma, size_t lumaPitch, const uint8_t* u, const uint8_t* v, size_t chromaPitch,
                                  unsigned width, unsigned height, bool flip) {
    for (unsigned row = 0; row < height; ++row)
        std::memcpy(dst.data[0] + ptrdiff_t(flip ? height - 1 - row : row) * dst.linesize[0], luma + size_t(row) * lumaPitch, width);
    const unsigned chromaRows = height / 2, chromaWidth = width / 2;
    for (unsigned row = 0; row < chromaRows; ++row) {
        uint8_t* out = dst.data[1] + ptrdiff_t(flip ? chromaRows - 1 - row : row) * dst.linesize[1];
        const uint8_t* a = u + size_t(row) * chromaPitch; const uint8_t* b = v + size_t(row) * chromaPitch;
        unsigned x = 0;
        for (; x + 16 <= chromaWidth; x += 16) {
            const __m128i uu = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + x)), vv = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + x));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 2 * x), _mm_unpacklo_epi8(uu, vv));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 2 * x + 16), _mm_unpackhi_epi8(uu, vv));
        }
        for (; x < chromaWidth; ++x) { out[2 * x] = a[x]; out[2 * x + 1] = b[x]; }
    }
}

} // namespace veyra::pipeline
