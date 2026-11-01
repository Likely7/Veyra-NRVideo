#pragma once
// Live recording (LiveRecorder) reads the presented frame inside the
// presentation command list, after the presenter's own reads. The consumer
// fence the graph waits on before reusing the texture then covers the
// recording too, and no second queue ever touches the graph's textures.
// Engine thread only. Kept free of the presenter's headers: EngineController.h
// is included after Qt, whose `slots` macro breaks diagnostics/GpuTimer.h.
#include <d3d12.h>
#include <cstdint>

namespace veyra::engine {

class PresentCapture {
public:
    struct Frame {
        ID3D12Resource* texture=nullptr; // graph output; COMMON on entry, must be left in COMMON
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        uint32_t width=0,height=0;
        uint32_t hdrMode=0;              // 0 SDR, 1 scRGB (linear BT.709), 2 HDR10 (PQ BT.2020)
        float ditherStep=0;              // graph output dither step (RgbToNv12 reserved.y)
        bool generated=false;
    };
    virtual ~PresentCapture()=default;
    // true: commands were recorded into `list`; submitted() or discarded() follows.
    virtual bool record(ID3D12GraphicsCommandList* list,const Frame& frame)=0;
    // The list was submitted; this fence point covers the recorded commands.
    virtual void submitted(ID3D12Fence* fence,uint64_t value)=0;
    // The list was not submitted (failed present); nothing will execute.
    virtual void discarded()=0;
};

} // namespace veyra::engine
