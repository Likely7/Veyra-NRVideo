#pragma once
#include "veyra/engine/ColorSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {
// The colour panel's 效果 group: 纹理 (texture), 清晰度 (clarity) and 去朦胧
// (dehaze). They need the neighbourhood of a pixel, which the per-pixel grade
// (ColorGrade.hlsli, fused into input conversion or ColorGradePass) cannot see,
// so they run as a separate in-place pass right after the grade. Until
// 2026-10-02 the three sliders were stored but nothing read them.
//
// Clarity and positive dehaze use downsampled block statistics and a separable
// blur. Texture and negative dehaze can run the full-size apply directly from
// a copy of the grade. The first non-zero value prepares the resources so
// switching between these modes needs no graph rebuild.
class ColorDetailPass {
public:
    static bool wanted(const engine::ColorSettings& settings) {
        return settings.enabled && (settings.texture != 0 || settings.clarity != 0 || settings.dehaze != 0);
    }
    // `target` is an RGBA16F texture the grade just wrote; it is read and
    // rewritten in place. `hdrWorking` selects scRGB working units.
    bool run(ID3D12GraphicsCommandList* list, StateTracker& tracker, ID3D12Resource* target,
             const engine::ColorSettings& settings, bool hdrWorking);
    void close();

private:
    bool prepare(ID3D12Device* device, ID3D12Resource* target);
    ComputePass reduce_, blur_, apply_;
    ComPtr<ID3D12Resource> copy_, lowA_, lowB_;
    ID3D12Resource* target_ = nullptr;
    uint32_t width_ = 0, height_ = 0, lowWidth_ = 0, lowHeight_ = 0, block_ = 8;
    bool failed_ = false;   // one log line, then no retries until close()
};
}
