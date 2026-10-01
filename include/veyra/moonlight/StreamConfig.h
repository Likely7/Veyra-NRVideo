// SPDX-License-Identifier: GPL-3.0-only
// Pure decisions about a stream: the default bitrate and which video formats to
// offer the host. Kept free of the C library so they can be tested offline.
// The bitrate table is moonlight-qt's (StreamingPreferences::getDefaultBitrate).
#pragma once
#include <algorithm>
#include <cmath>

#include "veyra/moonlight/Types.h"

namespace veyra::moonlight {

// VIDEO_FORMAT_* bits of moonlight-common-c (the session source static_asserts
// that these still match Limelight.h).
constexpr int kFormatH264 = 0x0001;
constexpr int kFormatH265 = 0x0100;
constexpr int kFormatH265Main10 = 0x0200;
constexpr int kFormatAv1Main8 = 0x1000;
constexpr int kFormatAv1Main10 = 0x2000;
constexpr int kFormatMask10Bit = kFormatH265Main10 | kFormatAv1Main10;

enum class CodecChoice { Auto, H264, Hevc, Av1 };

// Default video bitrate in kbit/s. The resolution factor is interpolated between
// fixed points (Shield's long standing defaults); above 60 fps the frame rate
// factor grows with the square root, not linearly.
inline int defaultBitrateKbps(int width, int height, int fps, bool yuv444 = false) {
    const float frameRateFactor = (fps <= 60 ? float(fps) : std::sqrt(float(fps) / 60.f) * 60.f) / 30.f;
    struct Point { int pixels; int factor; };
    static constexpr Point table[] = {
        {640 * 360, 1}, {854 * 480, 2}, {1280 * 720, 5}, {1920 * 1080, 10}, {2560 * 1440, 20}, {3840 * 2160, 40},
    };
    constexpr int count = int(sizeof(table) / sizeof(table[0]));
    const int pixels = width * height;
    float resolutionFactor = float(table[count - 1].factor);
    for (int i = 0; i < count; ++i) {
        if (pixels == table[i].pixels) { resolutionFactor = float(table[i].factor); break; }
        if (pixels < table[i].pixels) {
            resolutionFactor = i == 0 ? float(table[0].factor)
                : float(pixels - table[i - 1].pixels) / float(table[i].pixels - table[i - 1].pixels) * float(table[i].factor - table[i - 1].factor) + float(table[i - 1].factor);
            break;
        }
    }
    if (yuv444) resolutionFactor *= 2;
    return int(std::lround(resolutionFactor * frameRateFactor)) * 1000;
}

// The VIDEO_FORMAT_* mask to offer: what the user asked for, limited to what the
// host reports it can encode (`serverCodecModeSupport`, SCM_* bits). H.264 is
// always available. AV1 is only offered automatically when the caller knows it can
// decode it in hardware. 0 means the host cannot do what was asked.
inline int chooseVideoFormats(CodecChoice choice, bool hdr, int serverCodecModeSupport, bool av1HardwareDecode) {
    const bool hevc = (serverCodecModeSupport & kServerHevc) != 0;
    const bool hevc10 = (serverCodecModeSupport & kServerHevcMain10) != 0;
    const bool av1 = (serverCodecModeSupport & kServerAv1Main8) != 0;
    const bool av110 = (serverCodecModeSupport & kServerAv1Main10) != 0;
    int mask = 0;
    const auto add = [&](bool allowed, int bit) { if (allowed) mask |= bit; };
    if (hdr) {
        // HDR needs a 10-bit format: H.264 cannot carry it here.
        if (choice == CodecChoice::Auto || choice == CodecChoice::Hevc) add(hevc10, kFormatH265Main10);
        if (choice == CodecChoice::Av1 || (choice == CodecChoice::Auto && av1HardwareDecode)) add(av110, kFormatAv1Main10);
    } else {
        if (choice == CodecChoice::Auto || choice == CodecChoice::H264) mask |= kFormatH264;
        if (choice == CodecChoice::Auto || choice == CodecChoice::Hevc) add(hevc, kFormatH265);
        if (choice == CodecChoice::Av1 || (choice == CodecChoice::Auto && av1HardwareDecode)) add(av1, kFormatAv1Main8);
    }
    return mask;
}

} // namespace veyra::moonlight
