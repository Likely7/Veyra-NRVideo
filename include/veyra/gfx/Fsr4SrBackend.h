#pragma once

// AMD FSR4 ML upscaling backend via the driver DLL (amdxcffx64.dll), the
// OptiScaler-compatible path. The DLL is user-supplied (never redistributed):
// runtime_local/amd/fidelityfx/amdxcffx64.dll beside the signed FFX SDK
// loader/upscaler DLLs. On non-AMD adapters the INT8 Lite model is selected by
// patching the ffxProvider_*::IsSupported vtable entries (RTTI scan), the same
// gate bypass OptiScaler's master branch uses; no code bytes are modified.
//
// Interface mirrors FsrSrBackend so the graph's temporal SR harness (motion,
// depth, reset, HDR restore) drives it unchanged. Local evidence:
// tools/fsr4_driver_probe (NVIDIA smoke passed; Intel/Arc pending hardware).

#include <d3d12.h>

#include <cstdint>
#include <memory>

namespace veyra::gfx {

class Fsr4SrBackend {
public:
    Fsr4SrBackend();
    ~Fsr4SrBackend();

    Fsr4SrBackend(const Fsr4SrBackend&) = delete;
    Fsr4SrBackend& operator=(const Fsr4SrBackend&) = delete;

    // True when the user-supplied driver DLL and the signed SDK loader are
    // both present beside the FidelityFX runtime directory.
    static bool runtimeAvailable();

    bool initialize(ID3D12Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
                    uint32_t maxUpscaleWidth, uint32_t maxUpscaleHeight, bool hdr);

    // Same contract as FsrSrBackend::evaluate: color/motion/depth compute-read,
    // output UAV; motion is render-extent pixel motion (current->previous).
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
