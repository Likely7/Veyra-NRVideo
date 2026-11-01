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
    enum class SubmissionState { Pending, Succeeded, Failed };
    VfgBackend();
    ~VfgBackend();
    VfgBackend(const VfgBackend&)=delete;
    VfgBackend& operator=(const VfgBackend&)=delete;
    static std::wstring runtimeDirectory(const std::wstring& runtimeRoot);
    static bool runtimeAvailable(const std::wstring& runtimeRoot);
    // quality 0-5 as engine::EnhancementSettings::vfgQuality; a mixed tier
    // loads a second effect with the second model on the same CUDA stream.
    bool initialize(gfx::D3D12DeviceContext&,const std::wstring& runtimeRoot,
                    unsigned width,unsigned height,DXGI_FORMAT,unsigned multiplier,unsigned quality,
                    bool asynchronousSubmission=false,bool fastFallback=false);
    bool created() const;
    // Caller transitions encoded texture to COPY_SOURCE and back to COMMON.
    // Submit this list before generate(). Normal frames never CPU-wait here;
    // teardown and exceptional SDK rejection drain work before releasing it.
    // The same list also runs the GPU shot-change test of this pair against
    // the other parity's input (comparePrevious=false when the pair has no
    // valid A, e.g. after a reset); copyOutput() of the pair's subframes is
    // predicated on its result and shows the current input on a cut.
    bool capture(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned parity,bool comparePrevious=true);
    // Signals an OWNED producer fence and queues a GPU wait before copyOutput.
    // Preview offloads the serial CUDA wait/run/signal to a bounded worker;
    // synchronous/export callers retain the immediate SDK rejection verdict.
    // fast: this pair uses the second effect, see fastReady().
    bool generate(unsigned parity,unsigned subframe,unsigned multiplier,bool reset,bool fast=false);
    // A second effect is loaded: a mixed tier's second model, or with fastFallback
    // the next faster model, for pairs the chosen one would finish too late for.
    bool fastReady() const;
    // Poll after the corresponding consumer fence. A submitted copy is not
    // proof that the SDK accepted the generated frame.
    SubmissionState submissionState(unsigned slot) const;
    bool copyOutput(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned slot);
    // Test diagnostics only: the shot-change result buffer (null when the SDK's
    // automatic detection is used instead). Between captures it is in
    // D3D12_RESOURCE_STATE_PREDICATION; uint32 layout: cut flag per parity at
    // 0/2, last SAD/histogram/compare per parity at 4/8, cut count at 12.
    ID3D12Resource* shotChangeResultsForTest() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
