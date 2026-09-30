#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>

namespace veyra::gfx {
// AMD SDK frame interpolation into application-owned GPU textures. The
// historical class name is retained, but no HWND / swapchain / presentation
// worker is created. FrameBatch and VideoPresenter own timing/presentation.
class FsrFgPresenter {
public:
    FsrFgPresenter();
    ~FsrFgPresenter();
    FsrFgPresenter(const FsrFgPresenter&) = delete;
    FsrFgPresenter& operator=(const FsrFgPresenter&) = delete;
    bool initializeIndependent(ID3D12Device*, uint32_t width, uint32_t height,
                               DXGI_FORMAT, bool requireMl);
    // All resources COMMON before/after; motion current->previous in display
    // pixels, depth R32F. The owner drains GPU/consumer work before shutdown.
    bool generate(ID3D12GraphicsCommandList*, ID3D12Resource* color,
                  ID3D12Resource* motion, ID3D12Resource* depth, ID3D12Resource* output,
                  bool reset, float elapsedMs);
    uint32_t maxGeneratedFrames() const;
    bool available() const;
    bool failed() const;
    const char* providerVersion() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
