#pragma once

// Product video encoder abstraction for the export job. Exactly one encoder is
// active per export; the backend is chosen from adapter capability, never from
// a build-time switch:
//   * NVIDIA adapter      -> NVENC (D3D12, zero-copy, the fastest path)
//   * any other adapter   -> Media Foundation hardware MFT (AMD/Intel/NVIDIA
//                            driver encoders exposed through the OS)
// The Media Foundation path is 8-bit 4:2:0 only, so HDR (HEVC Main10) export
// still requires NVENC; the export job states that instead of silently
// producing an SDR file.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }

namespace veyra::sink {

enum class ExportRateControl : uint32_t { Cbr = 0, Vbr = 1, Cq = 2 };
constexpr std::string_view exportRateControlName(ExportRateControl mode) {
    switch (mode) {
    case ExportRateControl::Cbr: return "CBR";
    case ExportRateControl::Vbr: return "VBR";
    case ExportRateControl::Cq: return "CQ";
    }
    return "unknown";
}

// (bitstream, bytes, output frame index, keyframe) -> accepted?
using PacketWriter=std::function<bool(const uint8_t*,size_t,int64_t,bool)>;

enum class EncoderBackend { None, Nvenc, MediaFoundation };

constexpr std::string_view encoderBackendName(EncoderBackend backend) {
    switch(backend) {
    case EncoderBackend::Nvenc: return "NVIDIA-NVENC";
    case EncoderBackend::MediaFoundation: return "MediaFoundation-MFT";
    case EncoderBackend::None: break;
    }
    return "none";
}

struct EncoderConfig {
    bool hevc=false;
    unsigned fpsNum=0;
    unsigned fpsDen=1;
    // Export target bitrate in Mbps; 0 keeps the backend's constant-quality
    // default (NVENC CONSTQP / MFT quality mode).
    uint32_t bitrateMbps=0;
    ExportRateControl rateControl=ExportRateControl::Cq;
    // Active adapter identity. A machine can have several vendors' hardware
    // MFTs registered at once (an AMD driver's MFT is visible on an NVIDIA
    // host), so the Media Foundation path must prefer the MFT of the GPU that
    // actually renders the frames. 0x10DE/0x1002/0x8086 = NVIDIA/AMD/Intel.
    uint32_t adapterVendorId=0;
};

class VideoEncoder {
public:
    virtual ~VideoEncoder()=default;
    virtual bool open(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,const EncoderConfig&,PacketWriter)=0;
    virtual std::vector<uint8_t> headers() const=0;
    virtual bool encode(unsigned frameSlot,bool generated,int64_t pts)=0;
    virtual bool finish()=0;
    virtual void close()=0;
    virtual EncoderBackend backend() const=0;
    // Human-readable identity for logs and the export status line, e.g.
    // "NVIDIA NVENC H.264 (D3D12)" or "Media Foundation HEVC (NVIDIA ... MFT)".
    virtual std::wstring describe() const=0;
    virtual std::wstring lastError() const=0;
};

// Capability-driven selection. Returns nullptr when no usable encoder exists
// for the requested codec and fills `detail` with the reason (used verbatim in
// the user-facing failure message).
std::unique_ptr<VideoEncoder> openVideoEncoder(gfx::D3D12DeviceContext&,gfx::CommandSlotRing&,pipeline::EnhanceGraph&,
                                              const EncoderConfig&,PacketWriter,std::wstring& detail);

// Read-only capability probe for logs/tests: is a hardware Media Foundation
// encoder registered for the requested codec? Never opens a session.
struct EncoderCapability {
    bool mediaFoundation=false;      // a hardware MFT was found
    std::wstring mediaFoundationName;// friendly name of that MFT
};
EncoderCapability queryMediaFoundationEncoder(bool hevc);

} // namespace veyra::sink
