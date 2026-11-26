#pragma once

// Intel XeSS super-resolution upscaling backend (xess D3D12 API, dynamic load).
//
// The engine feeds it the linear working color (scRGB FP16, 1 = 80 nits) at
// render extent plus current->previous pixel motion and a depth texture; the
// provider writes the upscaled result into the caller's output UAV. XeSS-SR is
// a temporal (jitter-trained) upscaler; on video Veyra supplies zero jitter and
// estimated optical flow, so quality must be validated per content (probe-first).
// On Intel Arc the XMX path is used; other vendors fall back to DP4a.
//
// Integration mirrors FsrSrBackend (see EnhanceGraph::initFsrSr / runSr).

#include <d3d12.h>

#include <cstdint>
#include <memory>

namespace veyra::gfx {

class XessSrBackend {
public:
    XessSrBackend();
    ~XessSrBackend();

    XessSrBackend(const XessSrBackend&) = delete;
    XessSrBackend& operator=(const XessSrBackend&) = delete;

    bool initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                    uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr);

    // Records the upscale on the caller's command list. color/motion/depth must
    // be NON_PIXEL_SHADER_RESOURCE, output UNORDERED_ACCESS. motion is
    // render-extent current->previous pixels; depth render extent (constant far
    // depth is acceptable). reset clears temporal history.
    bool evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                  ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                  uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs);

    void release();
    bool created() const;
    uint64_t evaluateCount() const;
    uint64_t failureCount() const;
    const char* providerVersion() const;

    struct Impl;

private:
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::gfx