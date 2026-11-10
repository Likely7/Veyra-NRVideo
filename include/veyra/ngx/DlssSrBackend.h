#pragma once

// DLSS Super Resolution backend using the official NGX SDK 310.7 helpers
// (Playbook section 11). This is a standard NGX feature (not Feature 18);
// the runtime DLL nvngx_dlss.dll is loaded by NGX core from the PathList.

#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

#include <cstdint>

#include "veyra/Result.h"

namespace veyra::ngx {

class NgxCoreHost;

class DlssSrBackend {
public:
    DlssSrBackend() = default;
    ~DlssSrBackend();

    DlssSrBackend(const DlssSrBackend&) = delete;
    DlssSrBackend& operator=(const DlssSrBackend&) = delete;

    struct CreateDesc {
        uint32_t inputWidth;
        uint32_t inputHeight;
        uint32_t outputWidth;
        uint32_t outputHeight;
        int perfQuality; // NVSDK_NGX_PerfQuality_Value
        bool enableOutputSubrects;
        // engine::EnhancementSettings::dlssSrModel: 0 DLSS 4 transformer (preset K),
        // 1 DLSS 4.5 second-generation transformer (preset M), 2 DLSS 4.5 Ultra
        // Performance (preset L). Applied to every perf mode as a render preset hint.
        uint32_t model = 0;
        // DLAA: the feature runs at its own size (input == output) in the DLAA perf
        // mode instead of the 1:1 bypass below.
        bool nativeResolution = false;
    };

    struct EvalDesc {
        ID3D12Resource* color;       // linear input (render resolution)
        ID3D12Resource* output;      // output (target resolution)
        ID3D12Resource* depth;       // estimated depth or explicitly zero fallback
        ID3D12Resource* motionVectors; // current-to-previous, output pixels; zero on reset
        bool reset;
        float jitterOffsetX;
        float jitterOffsetY;
        float sharpness;
    };

    // Creates the SR feature through the core NGX API. The core host must be
    // initialized before calling this.
    bool create(NgxCoreHost& coreHost,
                ID3D12GraphicsCommandList* cmdList,
                NVSDK_NGX_Parameter* params,
                const CreateDesc& desc,
                Status& status);

    bool release();

    // Evaluates SR for one frame on the given command list.
    bool evaluate(ID3D12GraphicsCommandList* cmdList,
                  NVSDK_NGX_Parameter* params,
                  const EvalDesc& desc,
                  Status& status);

    bool created() const { return handle_ != nullptr; }
    uint64_t createResult() const { return createResult_; }
    uint64_t evaluateCount() const { return evaluateCount_; }
    uint64_t bypassCount() const { return bypassCount_; }
    bool isBypass() const; // true when input == output extent (never for DLAA)

    // 1:1 bypass: the entire SR backend is skipped (Playbook section 11).
    // Checks BOTH dimensions to handle non-uniform extents correctly.
    bool shouldBypass(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) const {
        return !nativeResolution_ && inW == outW && inH == outH;
    }
    // Convenience overload for width-only check (backward compat).
    bool shouldBypass(uint32_t inputWidth, uint32_t outputWidth) const {
        return !nativeResolution_ && inputWidth == outputWidth;
    }

private:
    NVSDK_NGX_Handle* handle_ = nullptr;
    uint64_t createResult_ = 0;
    uint64_t evaluateCount_ = 0;
    uint64_t bypassCount_ = 0;
    uint32_t inputWidth_ = 0;
    uint32_t inputHeight_ = 0;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;
    bool nativeResolution_ = false;
};

} // namespace veyra::ngx
