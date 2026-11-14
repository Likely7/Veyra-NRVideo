#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>
#include "veyra/pipeline/FrameBatch.h"

namespace veyra::gfx {
class XessPresenter {
public:
    XessPresenter();
    ~XessPresenter();
    // fgMultiplier is the requested output multiplier (2 = one generated frame,
    // 3 = two, 4 = three). >2 requires the audited provider unlock.
    // lowLatencySleep: XeLL bLowLatencyMode. File playback already paces its
    // inputs by the media clock (an application frame limiter, which Intel's
    // XeLL guide says should not run together with XeLL sleep); keep the XeLL
    // markers and sleep call but let pass-through mode make the sleep trivial.
    bool initialize(ID3D12Device*,ID3D12CommandQueue*,IDXGIFactory2*,HWND,const DXGI_SWAP_CHAIN_DESC1&,IDXGISwapChain3**,uint32_t fgMultiplier=1,bool lowLatencySleep=true);
    uint32_t beginInput();
    bool beginProcessing(uint32_t frameId);
    bool endProcessing(uint32_t frameId);
    bool beginFrame(uint32_t preparedFrameId=0,pipeline::FrameIdentity identity={});
    bool tag(ID3D12GraphicsCommandList*,ID3D12Resource* color,ID3D12Resource* motion,ID3D12Resource* depth,
             RECT region,bool enabled,bool reset,float elapsedMs,ID3D12Resource* ui=nullptr,ID3D12Resource* hudless=nullptr);
    bool beforePresent();
    bool afterPresent();
    uint64_t generatedCount() const;
    uint64_t presentedCount() const;
    // Runtime-reported ceiling of generated frames (1 = 2X) after the provider
    // capability query; 1 until the context exists.
    uint32_t maxInterpolatedFrames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
