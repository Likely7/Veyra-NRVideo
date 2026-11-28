#pragma once
#include <string_view>
#include "veyra/source/CapturePixelFormat.h"

namespace veyra::source {
// N3: format-list ordering and cost labels (plan 2026-09-16, section 8 N3).
// Lower rank sorts first. Plan order: NV12/P010 < YUY2 < RGB24/RGB32 <
// repack-heavy packed formats < compressed (needs a decoder).
enum class CaptureFormatTier { Low = 0, Medium = 1, High = 2, Decoded = 3 };

constexpr int captureFormatRank(CapturePacking packing) {
    switch (packing) {
    case CapturePacking::Nv12: return 0;
    case CapturePacking::P010: return 1;
    case CapturePacking::Nv21: return 2;
    case CapturePacking::I420: return 3;
    case CapturePacking::Yv12: return 4;
    case CapturePacking::P016: return 5;
    case CapturePacking::Yuy2: return 10;
    case CapturePacking::Bgr24: return 20;  // RGB24 DIB
    case CapturePacking::Bgr32: return 21;  // RGB32 DIB
    case CapturePacking::Bgra32: return 22; // ARGB32 DIB
    case CapturePacking::P210: return 23;
    case CapturePacking::Y210: return 24;
    case CapturePacking::Uyvy: return 30;
    case CapturePacking::Yvyu: return 31;
    case CapturePacking::Rgb555: return 32;
    case CapturePacking::Rgb565: return 33;
    case CapturePacking::Unknown: break;
    }
    return 100; // compressed or unknown: needs a decoder / system conversion
}

constexpr CaptureFormatTier captureFormatTier(CapturePacking packing) {
    switch (captureFormatRank(packing)) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 10:
        return CaptureFormatTier::Low;
    case 20: case 21: case 22: case 23: case 24:
        return CaptureFormatTier::Medium;
    case 30: case 31: case 32: case 33:
        return CaptureFormatTier::High;
    default: break;
    }
    return CaptureFormatTier::Decoded;
}

constexpr std::wstring_view captureFormatTierLabel(CaptureFormatTier tier) {
    switch (tier) {
    case CaptureFormatTier::Low: return L"低延迟";
    case CaptureFormatTier::Medium: return L"中延迟";
    case CaptureFormatTier::High: return L"高延迟·需 CPU 拆包";
    case CaptureFormatTier::Decoded: break;
    }
    return L"需系统解码·较高延迟";
}

// One-time hint when the user selects a high-cost or decoder-backed format.
constexpr bool captureFormatNeedsCostHint(CaptureFormatTier tier) {
    return tier >= CaptureFormatTier::High;
}
}
