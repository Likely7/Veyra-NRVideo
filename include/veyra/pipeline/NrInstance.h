// NR stacking: several Feature-18 instances on one snippet session, chained.
//
// Mirrors Magpie's multi-pass structure (SAOG0721/Magpie, GPL-3.0, commit
// 3841698348bfb246623d4acf791984c8b68a577b, src/Magpie.Core/DLSSNRMultiPass.h):
// one shared runtime session and caller-compatibility shim, with a per-instance
// feature handle, parameter block, textures and history. Verified on this
// machine with tools/nr_probe: up to eight concurrent handles create, evaluate
// and release.
#pragma once
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/pipeline/NrTemporalPass.h"
#include "veyra/engine/EnhancementSettings.h"

#include <memory>
#include <vector>

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

namespace veyra::pipeline {
// One NR layer, owning everything that must not be shared with another layer:
// its textures (encode proxy, neural output, decoded output, residual scratch,
// its own input copy) and its temporal history.
class NrInstance {
public:
    // `zeroMotion` / `zeroDepth` are shared constant guidance textures owned by
    // the graph; the layer only borrows them. `fullWidth`/`fullHeight` are the
    // composite extent, which differs from the NR extent in the realtime path.
    // `borrowedBase` is the image this layer composite onto: the working texture
    // for layer 0, the previous layer's output beyond that.
    // `borrowedInput` lets a layer read a texture the graph already owns. The
    // single-layer product path uses it when the NR extent equals the stage
    // input extent: the original graph aliased the working texture instead of
    // downsampling, and that byte-exact behaviour must survive. Null means the
    // layer allocates its own NR-extent input and the downsample runs.
    bool create(ID3D12Device* device, uint32_t width, uint32_t height,
                uint32_t fullWidth, uint32_t fullHeight,
                ID3D12Resource* zeroMotion, ID3D12Resource* zeroDepth,
                ID3D12Resource* borrowedInput = nullptr);
    void close();

    bool created() const { return proxy_ != nullptr; }
    NVSDK_NGX_Handle* handle() const { return handle_; }
    void setHandle(NVSDK_NGX_Handle* handle) { handle_ = handle; }
    NVSDK_NGX_Parameter* parameters() const { return parameters_; }
    void setParameters(NVSDK_NGX_Parameter* parameters) { parameters_ = parameters; }

    NrTemporalPass& temporal() { return temporal_; }
    ID3D12Resource* input() const { return input_.Get(); }
    ID3D12Resource* proxy() const { return proxy_.Get(); }
    ID3D12Resource* neural() const { return neural_.Get(); }
    ID3D12Resource* finalRgba() const { return finalRgba_.Get(); }
    ID3D12Resource* residual() const { return residual_.Get(); }
    ID3D12Resource* zeroMotion() const { return zeroMotion_; }
    // The full-extent image this layer composites onto, and the full-extent
    // result it produces (except for the last layer, which the graph owns).
    ID3D12Resource* baseFull() const { return baseFull_; }
    void setBaseFull(ID3D12Resource* resource) { baseFull_ = resource; }
    ID3D12Resource* outputFull() const { return outputFull_.Get(); }
    uint32_t fullWidth() const { return fullWidth_; }
    uint32_t fullHeight() const { return fullHeight_; }
    ID3D12Resource* zeroDepth() const { return zeroDepth_; }
    // True when input() is a texture the graph owns: no downsample is recorded
    // for this layer, exactly like the original single-layer aliasing path.
    bool inputIsBorrowed() const { return inputIsBorrowed_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    // Per-layer settings, copied out of EnhancementSettings when the graph is
    // built or re-applied.
    engine::NrSettings model{};
    engine::ResidualSettings residualSettings{};
    engine::ProtectionSettings protection{};
    bool temporalEnabled = false;
    bool enabled = true;
    // Bumped when an upstream layer's settings or output change. A layer whose
    // input revision moved drops its history, the same contract Magpie's
    // inputRevision enforces between passes.
    uint64_t inputRevision = 1;
    bool historyValid = false;

private:
    ComPtr<ID3D12Resource> input_, proxy_, neural_, finalRgba_, residual_, outputFull_;
    ID3D12Resource* baseFull_ = nullptr;
    uint32_t fullWidth_ = 0, fullHeight_ = 0;
    bool inputIsBorrowed_ = false;
    ID3D12Resource* zeroMotion_ = nullptr;
    ID3D12Resource* zeroDepth_ = nullptr;
    NVSDK_NGX_Handle* handle_ = nullptr;
    NVSDK_NGX_Parameter* parameters_ = nullptr;
    NrTemporalPass temporal_;
    uint32_t width_ = 0, height_ = 0;
};
}
