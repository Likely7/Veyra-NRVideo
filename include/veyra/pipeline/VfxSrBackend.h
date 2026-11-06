#pragma once
#include <d3d12.h>
#include <memory>
#include <string>

namespace veyra::gfx { class D3D12DeviceContext; }

namespace veyra::pipeline {
// NVIDIA Video Effects "VideoSuperRes" (SDK 1.3) on the graph's RGBA8 SR
// textures, for the modes the RTX Video SDK path does not offer (high bitrate,
// streaming). It shares the VFG runtime directory and CUDA interop: the graph
// records copyIn(), submits, calls run() (queue signal -> CUDA wait, run,
// signal -> queue wait; no CPU wait) and records copyOut() in a new list.
class VfxSrBackend {
public:
    VfxSrBackend();
    ~VfxSrBackend();
    VfxSrBackend(const VfxSrBackend&)=delete;
    VfxSrBackend& operator=(const VfxSrBackend&)=delete;
    static bool runtimeAvailable(const std::wstring& runtimeRoot);
    bool initialize(gfx::D3D12DeviceContext&,const std::wstring& runtimeRoot,unsigned inputWidth,unsigned inputHeight,
                    unsigned outputWidth,unsigned outputHeight,unsigned qualityLevel);
    bool created() const;
    // The texture is RGBA8 in COPY_SOURCE.
    bool copyIn(ID3D12GraphicsCommandList*,ID3D12Resource* texture);
    // Call after the list holding copyIn() was submitted on this queue.
    bool run(ID3D12CommandQueue*);
    // The texture is RGBA8 in COPY_DEST.
    bool copyOut(ID3D12GraphicsCommandList*,ID3D12Resource* texture);
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
