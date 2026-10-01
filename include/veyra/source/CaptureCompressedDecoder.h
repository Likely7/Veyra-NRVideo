#pragma once

// Compressed capture payload decoder (MPEG chain stage 3). Owns the FFmpeg
// decoder that turns the direct-connect capture payloads (MJPEG / H.264 /
// HEVC / AV1 / VP9) into frames the capture ingress understands. It is used
// from the single capture decode worker thread and is not thread safe.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "veyra/source/CaptureCodec.h"

struct AVFrame;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::pipeline { struct HardwareSurfaceInput; }

namespace veyra::source {

class CaptureCompressedDecoder {
public:
    CaptureCompressedDecoder();
    ~CaptureCompressedDecoder();

    CaptureCompressedDecoder(const CaptureCompressedDecoder&) = delete;
    CaptureCompressedDecoder& operator=(const CaptureCompressedDecoder&) = delete;

    // Backend order: MJPEG always decodes in software (drivers disagree on
    // 4:2:0 vs 4:2:2 and there is no 4:2:2 hardware path); everything else
    // tries D3D12VA on the shared Veyra device first. A hardware decoder whose
    // first frame cannot be imported by the graph is closed and replaced by
    // the software decoder, which converts into `nv12Target` instead.
    //
    // `extradata` (SPS/PPS from MPEG2VIDEOINFO) may be empty; the bitstream's
    // in-band parameter sets are then required.
    // Streaming sources (PC and Xbox streaming) call this before open(): AV1
    // then uses the native decoder with D3D12VA (the libdav1d decoder FFmpeg
    // would pick has no hardware path), HEVC decodes through D3D11VA on its own
    // device (HEVC D3D12VA fails at 4K and can remove the shared device on
    // NVIDIA drivers, see FFmpegVideoDecoder::openD3D11VA), a hardware decoder
    // that fails before its first picture is replaced by the software one, and
    // the software decoder runs several low-delay threads. Capture cards keep
    // the original behaviour.
    void setStreamProfile(bool enabled);

    bool open(CaptureCodec codec, unsigned width, unsigned height,
        const uint8_t* extradata, size_t extradataBytes,
        ID3D12Device* device, ID3D12CommandQueue* queue);

    // Decodes one payload; data=nullptr, bytes=0 receives remaining output
    // without submitting input or EOS. Drain this way before the next payload.
    // On success `*out` is a frame whose lifetime the
    // caller must not extend past the next decode():
    //  - hardware=true:  decoder-owned D3D12 surface (AV_PIX_FMT_D3D12);
    //  - hardware=false: converted into `nv12Target` (owned by the caller).
    // Returns false when the payload produced no frame (pending input) or the
    // decoder reported an error; see lastError()/waitingForInput().
    bool decode(const uint8_t* data, size_t bytes, int64_t pts100ns,
        AVFrame* nv12Target, AVFrame** out, bool& hardware);

    // For D3D11VA frames (AV_PIX_FMT_D3D11) the texture the graph reads is not
    // inside the AVFrame: this fills the borrowed view of the last frame.
    bool hardwareSurface(pipeline::HardwareSurfaceInput& out) const;

    bool opened() const;
    bool hardwareActive() const;
    bool waitingForInput() const;
    void recoverAtKeyframe();
    const char* backendName() const;
    const std::string& lastError() const;
    uint64_t framesDecoded() const;
    uint64_t decodeFailures() const;
    uint64_t hardwareFallbacks() const;
    // Running averages of the two halves of a software decode() call.
    double decodeMsAverage() const;
    double convertMsAverage() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::source
