#pragma once
#include <d3d12.h>
#include <memory>
#include <string>

namespace veyra::gfx { class D3D12DeviceContext; }
namespace veyra::pipeline {
// Public NvVFX 1.3 API. SDK metadata is allocated by NVCVImage.dll; no SDK
// headers, models or implementation are vendored into the source repository.
// Borrowed encoded textures stay in the graph's normal FrameLease pool.
class VfgBackend {
public:
    VfgBackend();
    ~VfgBackend();
    VfgBackend(const VfgBackend&)=delete;
    VfgBackend& operator=(const VfgBackend&)=delete;
    static std::wstring runtimeDirectory(const std::wstring& runtimeRoot);
    static bool runtimeAvailable(const std::wstring& runtimeRoot);
    bool initialize(gfx::D3D12DeviceContext&,const std::wstring& runtimeRoot,
                    unsigned width,unsigned height,DXGI_FORMAT,unsigned multiplier,unsigned quality);
    bool created() const;
    // Caller transitions encoded texture to COPY_SOURCE and back to COMMON.
    // Submit this list before generate(). All CPU waits are confined to teardown.
    bool capture(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned parity);
    // Signals an OWNED producer fence, waits/runs/signals on the CUDA stream,
    // and queues a GPU wait before the caller records copyOutput().
    bool generate(unsigned parity,unsigned subframe,unsigned multiplier,bool reset);
    bool copyOutput(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned slot);
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
