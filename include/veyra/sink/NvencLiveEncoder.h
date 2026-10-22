#pragma once
// D3D12 NVENC session for the live recorder. It encodes NV12 (SDR) or P010
// (HDR10) textures the caller owns: each submit names an input texture and
// the caller's fence point after which the texture holds the frame, so NVENC
// waits on the GPU and the caller never blocks. Bitstreams come back in
// submission order. Every call must come from one thread (the recorder's
// encoder thread); open() registers the inputs once for the session.
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::sink {

class NvencLiveEncoder {
public:
    struct Config {
        uint32_t width=0,height=0;
        bool hevc=false;
        bool hdr=false;          // HEVC Main10 from P010 inputs, BT.2020 PQ signalling
        uint32_t fps=60;         // rate-control frame rate
        uint32_t bitrateMbps=20; // VBR target
    };
    struct Packet {
        std::vector<uint8_t> data;
        int64_t ptsUs=0;
        bool key=false;
        unsigned input=0;        // the input texture this frame was read from (free again now)
    };

    NvencLiveEncoder();
    ~NvencLiveEncoder();
    NvencLiveEncoder(const NvencLiveEncoder&)=delete;
    NvencLiveEncoder& operator=(const NvencLiveEncoder&)=delete;

    bool open(ID3D12Device* device,const Config& config,const std::vector<ID3D12Resource*>& inputs);
    // Annex B parameter sets (SPS/PPS, plus VPS for HEVC).
    std::vector<uint8_t> headers() const;
    // Encode input `index` once `fence` reaches `value`. Fails when every
    // output buffer is still pending: take a packet first.
    bool submit(unsigned index,ID3D12Fence* fence,uint64_t value,int64_t ptsUs,bool forceKey=false);
    // The oldest submitted frame. Without `wait`, false when it is still
    // encoding (ready stays false); with it, waits up to timeoutMs.
    bool takeOldest(Packet& packet,bool wait,uint32_t timeoutMs,bool& ready);
    size_t inFlight() const;
    size_t capacity() const;
    void close();
    std::wstring lastError() const;
    std::wstring describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::sink
