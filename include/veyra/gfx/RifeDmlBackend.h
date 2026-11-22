// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>

namespace veyra::gfx {
// RIFE video frame interpolation (Practical-RIFE, MIT) run by ONNX Runtime with
// the DirectML execution provider on the graph's own D3D12 queue. The runtime
// (onnxruntime.dll, DirectML.dll) and the fp16 models live in runtime\rife next
// to the application and are loaded by absolute path; nothing is linked.
//
// Per real frame: capture() records a copy of the colour frame and its
// conversion into the model's planar input, the owner submits that list, then
// for each subframe generate() queues the network on the same queue (no CPU
// wait: ONNX Runtime only records and submits) and copyOutput() records the
// conversion of its result into the owner's texture. Queue order alone orders
// the three steps. 2X and 3X: subframe k of n is the frame at time k/n.
class RifeDmlBackend {
public:
    RifeDmlBackend();
    ~RifeDmlBackend();
    RifeDmlBackend(const RifeDmlBackend&) = delete;
    RifeDmlBackend& operator=(const RifeDmlBackend&) = delete;
    // runtime\rife holds the runtime DLLs and the model (file checks only).
    static bool runtimeInstalled();
    // SDR colour only (R8G8B8A8_UNORM); multiplier 2 or 3.
    bool initialize(ID3D12Device*, ID3D12CommandQueue*, uint32_t width, uint32_t height, DXGI_FORMAT, unsigned multiplier);
    // `color` is in COPY_SOURCE; the list runs on the queue given to initialize().
    bool capture(ID3D12GraphicsCommandList*, ID3D12Resource* color);
    // After the list holding capture() was submitted: queues subframe `sub` of
    // `of` (time sub/of) between the previous and this frame. `of` is the
    // multiplier, or 2 for a pair the owner reduced to 2X. False on failure.
    bool generate(unsigned sub, unsigned of);
    // Records the conversion of that subframe into `output` (UNORDERED_ACCESS,
    // same format as initialize()).
    bool copyOutput(ID3D12GraphicsCommandList*, ID3D12Resource* output, unsigned sub, unsigned of);
    // Drops the previous frame: the next capture starts a new pair.
    void reset();
    // A previous frame exists, so generate() has a real pair.
    bool hasPair() const;
    bool ready() const;
    const std::string& modelName() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
