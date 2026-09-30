#pragma once

// Pipeline FramePacket - the product-grade frame contract (Playbook R2).
// Fixes over the veyra::core draft: exact rational timestamps that can express
// negative and unknown PTS, full color description with assumed flags, ten
// frame event flags, and GPU textures that carry their own lifetime contract
// (owner slot, expected state, ready fence value, extent, format).
#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

namespace veyra::pipeline {

// Exact rational timestamp. den > 0. known=false means the container/source
// did not provide a timestamp (never fabricate one; downstream must handle).
struct Rational {
    int64_t num = 0;
    int32_t den = 1;
    bool known = true;

    static Rational unknown() { return Rational{0, 1, false}; }
    bool isUnknown() const { return !known; }
    bool isNegative() const { return known && num < 0; }
    double toDouble() const { return den == 0 ? 0.0 : static_cast<double>(num) / den; }
    // Rounded conversion to 100 ns units for logging/interop.
    int64_t to100ns() const {
        if (den == 0) { return 0; }
        return static_cast<int64_t>(static_cast<long double>(num) * 10000000.0L
            / static_cast<long double>(den) + (num >= 0 ? 0.5L : -0.5L));
    }
    bool equals(const Rational& o) const {
        if (known != o.known) { return false; }
        if (!known) { return true; }
        // Cross-multiply to avoid requiring a canonical form.
        return static_cast<long double>(num) * o.den == static_cast<long double>(o.num) * den;
    }
};

enum class SourceKind : uint8_t {
    Unknown = 0,
    File,         // FFmpeg demux/decode (player, export)
    CaptureCard,  // physical DirectShow/UVC capture
    RemotePlay,   // PS5 Remote Play stream (Chiaki transport)
    Image,        // WIC single image
    TestPattern,  // synthetic harness input
    ScreenCapture,
    Moonlight,    // Sunshine / GameStream host stream (moonlight-common-c transport)
};

inline const char* sourceKindName(SourceKind k) {
    switch (k) {
    case SourceKind::File: return "File";
    case SourceKind::CaptureCard: return "CaptureCard";
    case SourceKind::RemotePlay: return "RemotePlay";
    case SourceKind::Image: return "Image";
    case SourceKind::TestPattern: return "TestPattern";
    case SourceKind::ScreenCapture: return "ScreenCapture";
    case SourceKind::Moonlight: return "Moonlight";
    default: return "Unknown";
    }
}

enum class SourcePixelFormat : uint8_t {
    Unknown = 0,
    NV12,      // SDR limited 8-bit planar
    P010,      // 10-bit HDR path - V1 must fail closed, not misread as SDR
    Yuv420P,   // software decode planar
    Yuy2,      // packed capture format
    Bgra8,     // 8-bit BGRA
    Rgba16F,   // canonical linear working format
    P016,      // 16-bit capture storage; does not imply HDR transfer
    // N1 packed capture formats: the driver's bytes reach the GPU 1:1 and the
    // upload shader unpacks them (see EnhanceGraphDesc::packedInput).
    Bgr24,     // RGB24 DIB, BGR byte order
    Rgb555,    // 16-bit 5:5:5 DIB
    Rgb565,    // 16-bit 5:6:5 DIB
    Uyvy,      // UYVY 4:2:2
    Yvyu,      // YVYU 4:2:2
};
// Value carried in EnhanceGraphDesc::packedInput; 0 means "not a packed input".
constexpr uint32_t packedInputCode(SourcePixelFormat format) {
    switch (format) {
    case SourcePixelFormat::Bgr24: return 1;
    case SourcePixelFormat::Rgb555: return 2;
    case SourcePixelFormat::Rgb565: return 3;
    case SourcePixelFormat::Uyvy: return 4;
    case SourcePixelFormat::Yvyu: return 5;
    default: break;
    }
    return 0;
}

enum class ColorRange : uint8_t { Unknown = 0, Limited, Full };
enum class YuvMatrix : uint8_t { Unknown = 0, BT601, BT709, BT2020NCL, BT2020CL };
enum class TransferFunction : uint8_t { Unknown = 0, SRGB, BT709, BT2020_10, Linear, PQ, HLG };
enum class ColorPrimaries : uint8_t { Unknown = 0, BT601_525, BT601_625, BT709, BT2020 };
enum class ChromaLocation : uint8_t { Unknown = 0, Left, Center, TopLeft, Top, BottomLeft, Bottom };

struct SampleAspectRatio {
    uint32_t num = 1;
    uint32_t den = 1;
};

// Full ingress color metadata. Assumed flags must be set when a value was
// derived from a documented default (SD->601, HD SDR->709) rather than the
// container; the UI surfaces these.
struct ColorDescription {
    SourcePixelFormat pixelFormat = SourcePixelFormat::Unknown;
    ColorRange range = ColorRange::Unknown;
    YuvMatrix matrix = YuvMatrix::Unknown;
    TransferFunction transfer = TransferFunction::Unknown;
    ColorPrimaries primaries = ColorPrimaries::Unknown;
    uint16_t rotationDegrees = 0;       // 0 / 90 / 180 / 270
    SampleAspectRatio sar;

    // Display intent is separate from source VUI. PS5 SDR is displayed with
    // BT.1886 (ideal black), not inverse camera OETF, before sRGB presentation.
    bool displayReferred709 = false;
    // Desktop SDR media uses a reversible sRGB working representation so
    // no-effects output retains the post-matrix RGB code values. This is a
    // display policy, not a rewrite of the source transfer metadata. Keep
    // legacy PS5 reference-display intent separate until independently tested.
    bool preserveSdrCodeValues = false;
    bool rangeAssumed = false;
    bool matrixAssumed = false;
    bool transferAssumed = false;
    bool primariesAssumed = false;
    // PS5 opt-in reconstruction; legacy file/capture/export sampling is unchanged.
    ChromaLocation chromaLocation = ChromaLocation::Unknown;
    bool reconstructChroma = false;
    // Static HDR metadata in cd/m2; zero means absent/unspecified. These are
    // declarations, not measured frame peaks or dynamic Dolby Vision metadata.
    float hdrMaxCllNits = 0, hdrMaxFallNits = 0, hdrMasteringPeakNits = 0;
    bool scRgb = false; // Explicit WGC FP16 linear BT.709, 1.0 = 80 nits.

    bool isHdrPath() const {
        return scRgb || transfer == TransferFunction::PQ || transfer == TransferFunction::HLG;
    }
};

enum class FrameFlagBits : uint32_t {
    None            = 0,
    Open            = 1u << 0,   // first frame after open/start
    Seek            = 1u << 1,
    Cut             = 1u << 2,   // detected scene cut
    Drop            = 1u << 3,   // capture mailbox dropped stale frame(s)
    Duplicate       = 1u << 4,   // repeated source frame
    Resize          = 1u << 5,   // extent change
    Discontinuity   = 1u << 6,   // PTS jump / sequence gap
    PauseResume     = 1u << 7,
    DeviceLost      = 1u << 8,
    Eos             = 1u << 9,
};
using FrameFlags = uint32_t;

constexpr bool hasFrameFlag(FrameFlags flags, FrameFlagBits bit) {
    return (flags & static_cast<uint32_t>(bit)) != 0;
}
// Any flag that invalidates temporal history (consumers must reset).
constexpr FrameFlags historyBreakingFlags() {
    return static_cast<uint32_t>(FrameFlagBits::Seek)
        | static_cast<uint32_t>(FrameFlagBits::Cut)
        | static_cast<uint32_t>(FrameFlagBits::Drop)
        | static_cast<uint32_t>(FrameFlagBits::Resize)
        | static_cast<uint32_t>(FrameFlagBits::Discontinuity)
        | static_cast<uint32_t>(FrameFlagBits::PauseResume)
        | static_cast<uint32_t>(FrameFlagBits::DeviceLost);
}
constexpr bool breaksHistory(FrameFlags flags) {
    return (flags & historyBreakingFlags()) != 0;
}

// GPU texture with an explicit lifetime contract. The packet never owns the
// resource; it names who owns the transition (command slot), the state the
// content is in, and the fence value after which it is safe to consume.
struct GpuTextureHandle {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES expectedState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t ownerSlot = 0;           // command slot responsible for writes/transitions
    uint64_t readyFenceValue = 0;     // content ready at this fence value (0 = CPU/CPU-visible)
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    bool present() const {
        return resource != nullptr && width > 0 && height > 0
            && format != DXGI_FORMAT_UNKNOWN;
    }
};

// Decoded hardware surface for the graph ingress, filled by sources whose
// decoder owns the surface outside FFmpeg's AVFrame (today: the D3D11VA HEVC
// path, whose textures are shared to D3D12 as NT handles). The packet never
// owns the resource or the fence: the decoder keeps both alive for at least as
// long as the frame it belongs to. The graph issues one GPU-side queue wait on
// `waitFence`/`waitValue` and then samples the texture by texel index inside
// the visible extent, so decoder allocation padding is never read.
struct HardwareSurfaceInput {
    ID3D12Resource* texture = nullptr;
    uint32_t subresourceIndex = 0;
    ID3D12Fence* waitFence = nullptr;
    uint64_t waitValue = 0;
    uint32_t textureWidth = 0;        // includes decoder padding
    uint32_t textureHeight = 0;

    bool present() const { return texture != nullptr; }
};

struct FramePacket {
    uint64_t sequence = 0;        // monotonic per source (0 reserved as invalid)
    Rational pts;
    Rational duration=Rational::unknown();
    SourceKind sourceKind = SourceKind::Unknown;
    FrameFlags flags = 0;
    ColorDescription colorInfo;
    uint64_t sourceEpoch = 1;     // ResetCoordinator epoch at frame boundary
    int64_t arrivalHost100ns = 0; // capture callback steady_clock; 0 when unavailable
    int64_t decodedHost100ns = 0; // actual remote decoder output; same local clock

    GpuTextureHandle color;       // canonical linear RGBA16F working texture
    HardwareSurfaceInput hardwareSurface; // decoded NV12/P010 surface when the
                                          // codec path does not expose it through
                                          // the AVFrame (see the struct comment)

    bool valid() const {
        return sequence > 0 && color.present() && !pts.isUnknown();
    }
};

} // namespace veyra::pipeline
