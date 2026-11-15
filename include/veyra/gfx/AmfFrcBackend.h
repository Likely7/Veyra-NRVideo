// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>

namespace veyra::gfx {
// AMD AMF FRC: the video frame interpolation in the AMF runtime that AMD drivers
// install (System32\amfrt64.dll; built against the AMF SDK headers, MIT). It runs
// on its own queues of the same D3D12 device. Textures cross queues through AMF's
// fence protocol (core/D3D12AMF.h): each texture names a fence, the fence carries
// the last value to wait for, and a queue that used the texture raises that value
// by one and signals it. Each input yields the frame halfway between the previous
// input and this one (FRC_ONLY_INTERPOLATED without the future frame): 2X, one
// generated frame per pair, motion estimated by AMF from the two pictures.
//
// Per real frame: capture() records a copy into an input texture, the owner
// submits that list, generate() hands the frame to AMF, copyOutput() makes the
// queue wait for AMF and records the copy of the result, the owner submits that
// list and calls submitted(). No CPU waits on the GPU while frames flow.
class AmfFrcBackend {
public:
    AmfFrcBackend();
    ~AmfFrcBackend();
    AmfFrcBackend(const AmfFrcBackend&) = delete;
    AmfFrcBackend& operator=(const AmfFrcBackend&) = delete;
    // An AMD driver's AMF runtime is installed (file check only, nothing loaded).
    static bool runtimeInstalled();
    // lowPower: the downscaled motion search AMD suggests for APUs and low-end GPUs.
    bool initialize(ID3D12Device*, ID3D12CommandQueue*, uint32_t width, uint32_t height, DXGI_FORMAT, bool lowPower);
    // `color` is in COPY_SOURCE; the list runs on the queue given to initialize().
    bool capture(ID3D12GraphicsCommandList*, ID3D12Resource* color);
    // After the list holding capture() was submitted. reset drops AMF's history
    // first; the frame AMF then returns is not an interpolation and is not shown.
    bool generate(int64_t pts100ns, bool reset);
    // Queue wait for AMF, then the copy into `output` (COPY_DEST). False when AMF
    // returned no frame for this input.
    bool copyOutput(ID3D12GraphicsCommandList*, ID3D12Resource* output);
    // After the list holding copyOutput() was submitted: hands AMF's frame back.
    void submitted();
    // The first interpolation after initialize() repeats the previous frame (AMF
    // warm-up, seen on the Radeon iGPU, 2026-11-14); the owner does not show it.
    bool warmingUp() const;
    bool ready() const;
    const std::string& runtimeVersion() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
