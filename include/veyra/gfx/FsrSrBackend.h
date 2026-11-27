#pragma once

// AMD FidelityFX Super Resolution upscaling backend (FidelityFX API, not NGX).
//
// The engine feeds it the linear working color at render extent plus the
// guidance motion in render-extent pixels; the provider writes the upscaled
// result directly into the caller's output resource, so the graph needs no
// extra blit. Motion convention: the FSR3 upscaler reprojects with
// "previous = current + motion" (ffx_fsr3upscaler_reproject.h:
// fReprojectedHrUv = fHrUv + fMotionVector), which already matches Veyra's
// current->previous guidance, so the stored pixels are passed unchanged.
//
// Local evidence for this path: tools/fsr_upscale_probe ->
// logs/fsr/upscale-probe.log, and docs/FSR_UPSCALING_PLAN_2026-09-16.md.

#include <d3d12.h>

#include <cstdint>
#include <memory>

namespace veyra::gfx {

class FsrSrBackend {
public:
    FsrSrBackend();
    ~FsrSrBackend();

    FsrSrBackend(const FsrSrBackend&) = delete;
    FsrSrBackend& operator=(const FsrSrBackend&) = delete;

    // maxRender* is the largest render (source) extent, maxUpscale* the largest
    // output extent. hdr selects the high-dynamic-range input contract.
    bool initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                    uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr);

    // Records the upscale on the caller's command list. Resources must be in
    // the state declared here (color/motion/depth readable by compute, output
    // as UAV); `motion` is render-extent pixel motion, `depth` render extent.
    bool evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* motion,
                  ID3D12Resource* depth, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight,
                  uint32_t upscaleWidth, uint32_t upscaleHeight, bool reset, float frameTimeMs,
                  float sharpness = 0.0f);

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
