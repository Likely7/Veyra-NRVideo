#pragma once

// IFrameSource - the unified input contract (Playbook R2/R7). Sources
// produce pipeline::FramePacket metadata plus the decoded frame view; the
// graph's ingress stage owns the single color conversion into the canonical
// linear RGBA16F working texture (Product Spec 7).
#include <cstdint>
#include <string>

#include "veyra/pipeline/FramePacket.h"
#include "veyra/source/DolbyVision.h"

struct AVFrame;

namespace veyra::source {

namespace pipeline = veyra::pipeline;

struct SourceOpenDesc {
    std::wstring path;
    bool preferHardwareDecode = true; // hardware first, software fallback explicit
    // User setting "强制硬解": a file whose hardware decoder cannot open fails
    // with a clear error instead of falling back to software decode.
    bool requireHardwareDecode = false;
    void* d3d12Device = nullptr;      // ID3D12Device* when hardware decode is wanted
    void* d3d12Queue = nullptr;       // ID3D12CommandQueue*
    // Adapter the D3D12 device lives on. The D3D11VA path creates its own
    // D3D11 device on the same adapter so decoded surfaces can be shared.
    uint64_t d3d12AdapterLuid = 0;
    bool legacyCaptureRgbForDiagnostic = false; // explicit A/B only; never set by the player
};

struct SourceInfo {
    bool opened = false;
    pipeline::SourceKind kind = pipeline::SourceKind::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    // Display aspect ratio (width/height) after the container's sample aspect
    // ratio and rotation are applied. 0 = unknown, use width/height. Live
    // sources report their format's ratio; files read it from the stream.
    double displayAspect = 0.0;
    // Rotation from the container display matrix, in degrees (0/90/180/270).
    // Informational: the decoder does not rotate, the UI reports it.
    int rotationDegrees = 0;
    pipeline::Rational duration;
    double averageFps = 0.0;         // informational only; the pipeline is PTS-driven
    int nominalRateNum = 0, nominalRateDen = 0; // candidate, not proof of CFR
    double timestampQuantum = 0.0;
    bool hardwareDecodeActive = false;
    // Which decode path actually produced the frames: "d3d12va", "d3d11va" or
    // "software". Descriptive only; the UI/telemetry must not claim a path the
    // session is not using.
    std::string videoDecodePath;
    // File diagnostics. These are descriptive capability results, not a
    // promise that every profile of the codec is supported.
    std::string containerName;
    std::string videoCodecName;
    std::string videoPixelFormatName;
    DolbyVisionInfo dolbyVision;
    pipeline::ColorDescription color;
};

enum class SourceReadStatus : uint8_t {
    Frame = 0,  // packet metadata + decoded frame valid
    Eos,        // source fully drained
    Error,
    Waiting,   // live capture has no new sample yet; poll cancellation then retry
};

class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    virtual bool open(const SourceOpenDesc& desc) = 0;
    virtual const SourceInfo& info() const = 0;

    // Reads the next frame. On Frame, `out` carries the full packet metadata
    // and `decodedFrame` points at the FFmpeg frame (owned by the source,
    // valid until the next read). The ingress stage converts it.
    virtual SourceReadStatus read(pipeline::FramePacket& out, const AVFrame** decodedFrame) = 0;

    // Unsupported for capture/image sources. Atomic: demuxer seek + decoder
    // flush; the next read carries the Seek flag and a fresh epoch.
    virtual bool seek(const pipeline::Rational& targetSeconds) = 0;

    virtual void close() noexcept = 0;
};

} // namespace veyra::source
