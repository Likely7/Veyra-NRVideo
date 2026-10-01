#pragma once

// FFmpeg software video decoder (Phase 3A baseline; Playbook section 13.1).
// D3D12VA arrives in P3.3 as a separate configuration of the same surface.

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11DeviceContext4;
struct ID3D11Fence;
struct ID3D11Texture2D;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12Resource;
struct ID3D12CommandQueue;

namespace veyra::media {

struct DecoderStats {
    uint64_t framesDecoded = 0;
    uint64_t framesSubmitted = 0;
    uint64_t packetRetries = 0;
    int64_t firstPts = 0;
    int64_t lastPts = 0;
    uint64_t ptsNonMonotonicCount = 0;
    uint64_t pixelReadbackCount = 0; // software decode uploads only; always 0 on the GPU path
};

enum class DecodeReceiveStatus { NeedInput, Frame, EndOfStream, Error };

// Decoded surface handed to the graph ingress for paths whose texture does not
// travel inside the AVFrame (D3D11VA). Pointers are borrowed: the decoder owns
// the texture view and the D3D12 fence and keeps both alive until close().
struct HardwareSurfaceView {
    ID3D12Resource* texture = nullptr;
    uint32_t subresourceIndex = 0;
    ID3D12Fence* waitFence = nullptr;   // D3D12 view of the D3D11 fence
    uint64_t waitValue = 0;             // signalled after this frame's decode
    uint32_t textureWidth = 0;          // includes decoder allocation padding
    uint32_t textureHeight = 0;
};

class FFmpegVideoDecoder {
public:
    FFmpegVideoDecoder() = default;
    ~FFmpegVideoDecoder();

    FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
    FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

    // Copies the stream's codec parameters into the codec context; the
    // demuxer stays alive independently of this decoder. `streamTimeBaseNum/
    // Den` is the DEMUXER stream time base: decoder frame PTS pass through in
    // that base when the codec context itself carries none.
    bool openSoftware(const AVCodecParameters* codecParameters,
        int streamTimeBaseNum = 0, int streamTimeBaseDen = 0, unsigned softwareThreads = 1,
        bool lowLatency = false);

    // D3D12VA hardware decode on the SHARED Veyra device (Playbook 13.2):
    // creates an AV_HWDEVICE_TYPE_D3D12VA context wrapping `device`/`queue`,
    // installs a get_format callback that only selects AV_PIX_FMT_D3D12, and
    // records GPU queue waits on each frame's sync fence.
    bool openD3D12VA(const AVCodecParameters* codecParameters,
        int streamTimeBaseNum, int streamTimeBaseDen,
        ID3D12Device* device, ID3D12CommandQueue* queue, bool lowLatency = false);

    // D3D11VA hardware decode on a private D3D11 device created on the same
    // adapter as `d3d12Device`. The decoder surfaces are created as SHARED_NTHANDLE
    // textures, opened on the D3D12 side once per texture and handed to the graph
    // through HardwareSurfaceView with a D3D11 fence signalled after each frame's
    // decode (the graph issues one GPU-side queue wait, never a CPU wait).
    // Rationale: HEVC D3D12VA faults the shared D3D12 device on some drivers
    // (RTX 5070 / 616.56, see docs/DIAG_HEVC_D3D12VA_AND_EXPORT_CFR_2026-09-17.md);
    // D3D11VA cannot take the render device down with it.
    bool openD3D11VA(const AVCodecParameters* codecParameters,
        int streamTimeBaseNum, int streamTimeBaseDen,
        uint64_t adapterLuid, ID3D12Device* d3d12Device, bool lowLatency = false);
    void close();

    // This FFmpeg build registers libdav1d ahead of the native AV1 decoder, and
    // libdav1d has no hardware path: avcodec_find_decoder(AV1) silently gives a
    // software decoder to openD3D12VA/openD3D11VA. Streaming sources set this so
    // the hardware opens use the native "av1" decoder (the one with the hwaccels).
    void setPreferNativeAv1Hardware(bool prefer) { preferNativeAv1_ = prefer; }

    bool opened() const { return context_ != nullptr; }
    int width() const;
    int height() const;
    int pixelFormat() const; // AVPixelFormat as int to keep this header light

    // True means accepted (or an already completed nullptr drain). EAGAIN
    // drains into a bounded output queue and retries this same packet.
    bool sendPacket(const AVPacket* packet);

    // Receive one decoded frame; false when more input is needed or at EOF.
    // The frame stays owned by the decoder and is valid until the next call.
    const AVFrame* receiveFrame();
    // nullptr can mean pending input, normal EOF, or a hard decode error.
    DecodeReceiveStatus receiveStatus() const { return receiveStatus_; }

    void flushBuffers(); // seek boundary (Playbook 13.4)
    void recoverAtKeyframe(); // compressed input loss: discard until a new key frame
    const DecoderStats& stats() const { return stats_; }

    // True when the decoder is producing AV_PIX_FMT_D3D12 frames.
    // Time base the decoded frames' PTS are expressed in (the demuxer stream
    // time base, applied when the codec context carries none).
    int frameTimeBaseNum() const { return frameTimeBaseNum_; }
    int frameTimeBaseDen() const { return frameTimeBaseDen_; }

    bool usingD3D12Frames() const { return hwAccelActive_ && context_ != nullptr && lastFrameFormat_ != -1; }
    bool usingD3D11Frames() const {
        return hwAccelActive_ && context_ != nullptr && lastFrameFormat_ != -1
            && hwAccelKind_ == HardwareDecodeKind::D3D11VA;
    }
    bool hardwareActive() const { return hwAccelActive_ && context_ != nullptr; }
    // Decode path actually in use, for telemetry/UI labels: "d3d12va",
    // "d3d11va" or "software". Do not label a session with a path it is not on.
    const char* decodePathName() const {
        if (!hwAccelActive_) { return "software"; }
        return hwAccelKind_ == HardwareDecodeKind::D3D11VA ? "d3d11va" : "d3d12va";
    }
    // Latest decoded surface for the D3D11VA path; only valid after
    // receiveFrame() returned a frame and receiveStatus() == Frame.
    const HardwareSurfaceView& hardwareSurface() const { return hardwareSurface_; }
    // The product ingress imports only 2D NV12/P010 surfaces. Validate that
    // contract before handing the first frame to the graph so an unsupported
    // hardware surface can fall back cleanly.
    bool hardwareFrameImportable() const;
    int lastFrameFormat() const { return lastFrameFormat_; }
    uint64_t gpuQueueWaitCount() const { return gpuQueueWaitCount_; }

private:
    enum class HardwareDecodeKind { None, D3D12VA, D3D11VA };

    // Decoded slices are copied into these D3D11 textures, which are shared to
    // D3D12 as NT handles. The ring is oversized for the app's six command
    // slots, so a slot is only rewritten well after the graph finished reading
    // it (see the comment in receiveFrame()).
    static constexpr uint32_t kInteropSlotCount = 8;
    struct InteropSlot {
        ID3D11Texture2D* texture = nullptr;
        ID3D12Resource* resource = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t lastFenceValue = 0;
    };

    ID3D12Resource* importD3D11Texture(ID3D11Texture2D* texture);
    bool createInteropSlots(const void* sourceDesc);
    bool createInteropFence(ID3D12Device* d3d12Device);
    void releaseInterop();

    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    std::deque<AVFrame*> bufferedFrames_;
    DecodeReceiveStatus receiveStatus_ = DecodeReceiveStatus::NeedInput;
    int frameTimeBaseNum_ = 0;
    int frameTimeBaseDen_ = 0;
    DecoderStats stats_{};
    bool hwAccelActive_ = false;
    bool preferNativeAv1_ = false;
    HardwareDecodeKind hwAccelKind_ = HardwareDecodeKind::None;
    int lastFrameFormat_ = -1;
    uint64_t gpuQueueWaitCount_ = 0;

    // D3D11VA interop state. FFmpeg owns the device context; this class owns
    // the extra device reference, the fence pair and the opened texture views.
    AVBufferRef* d3d11DeviceRef_ = nullptr;
    ID3D11Device* d3d11Device_ = nullptr;             // our reference (FFmpeg holds its own)
    ID3D11DeviceContext* d3d11Context_ = nullptr;     // borrowed from the hw device context
    ID3D11Fence* d3d11Fence_ = nullptr;
    ID3D11DeviceContext4* d3d11Context4_ = nullptr;   // owned QueryInterface reference
    ID3D12Device* d3d12Device_ = nullptr;             // borrowed from the caller
    ID3D12Fence* d3d12Fence_ = nullptr;
    uint64_t interopFenceValue_ = 0;
    uint64_t interopFrameIndex_ = 0;
    uint32_t interopSlotIndex_ = 0;
    InteropSlot interopSlots_[kInteropSlotCount];
    HardwareSurfaceView hardwareSurface_{};
    struct D3D11TextureImport { ID3D12Resource* resource = nullptr; bool logged = false; };
    // One decoder pool is a single texture array, so this map stays tiny; the
    // cap only guards against a driver that rebuilds pools repeatedly.
    std::unordered_map<ID3D11Texture2D*, D3D11TextureImport> d3d11Imports_;
};

} // namespace veyra::media
