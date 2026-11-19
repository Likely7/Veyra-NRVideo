// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra::remoteplay {
// DualSense haptics over Bluetooth: HID output report 0x32.
//
// Over USB the pad exposes a four-channel audio endpoint and haptic audio is played into
// channels 3/4. Over Bluetooth there is no audio endpoint; the same audio travels in an
// output report as signed 8-bit stereo at 3000 Hz, 32 frames per report (10.667 ms).
//
//   [0]        0x32
//   [1]        sequence in the high nibble
//   [2..10]    sub-packet 0x11 (control), 7 bytes: flags 0xFE, four bytes left at zero, the
//              pad's buffer depth, a running counter
//   [11..76]   sub-packet 0x12 (samples), 64 bytes: 32 frames of left, right
//   [77..137]  zero
//   [138..141] CRC-32 (reflected 0xEDB88320, initial and final inversion) of the byte 0xA2
//              followed by bytes 0..137, little endian
//
// The manufacturer does not document the report. Where the facts come from (no code was
// taken from these projects):
//   - layout: first published by SAxense, https://github.com/egormanga/SAxense (MPL-2.0);
//     read here as dualsense-companion and AnyPS5 issue 2056 describe it
//   - on Windows the report is 142 bytes with its id, as the pad's Bluetooth descriptor
//     declares; the buffer depth byte and its measured effect; 32-frame blocks:
//     dualsense-companion, https://github.com/vingt-2/dualsense-companion (MIT)
//   - a second HID handle next to SDL's own takes the report on Windows:
//     https://github.com/boykopovar/AnyPS5/issues/2056
//   - CRC seed byte 0xA2 of Bluetooth output reports: SDL (SDL_hidapi_ps5.c) and the Linux
//     hid-playstation driver compute report 0x31 the same way
inline constexpr std::size_t kDualSenseHapticsFrames = 32;
inline constexpr std::size_t kDualSenseHapticsReportSize = 142;
// How far ahead the pad buffers before it plays, roughly in milliseconds. 0x40 measured
// about 70 ms from write to vibration in dualsense-companion and rode out Bluetooth link
// stalls (0-1 dropouts in 10 s of a continuous tone, 4-7 with 0x20); 0x04 plays nothing.
inline constexpr std::uint8_t kDualSenseHapticsBufferDepth = 0x40;

inline std::uint32_t dualSenseCrc32(std::uint32_t crc, const std::uint8_t* data, std::size_t size) noexcept {
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}
// 16-bit sample to the pad's 8 bits, rounded half away from zero. Truncation loses quiet
// effects: everything below one step would go silent.
inline std::int8_t dualSenseNarrowSample(std::int16_t value) noexcept {
    const int rounded = (value >= 0 ? int(value) + 128 : int(value) - 128) / 256;
    return static_cast<std::int8_t>(std::clamp(rounded, -128, 127));
}
// Above the noise floor: more than one 8-bit step from zero. The console sends noise of one
// step between effects.
inline bool dualSenseAudible(std::int16_t value) noexcept {
    const auto sample = dualSenseNarrowSample(value);
    return sample > 1 || sample < -1;
}
// `frames`: 32 interleaved left/right 16-bit frames at 3000 Hz. Returns true when any sample
// of the report is audible.
inline bool buildDualSenseHapticsReport(std::array<std::uint8_t, kDualSenseHapticsReportSize>& out,
    const std::int16_t* frames, std::uint8_t sequence, std::uint8_t counter,
    std::uint8_t bufferDepth = kDualSenseHapticsBufferDepth) noexcept {
    out.fill(0);
    out[0] = 0x32; out[1] = static_cast<std::uint8_t>((sequence & 0x0f) << 4);
    out[2] = 0x80 | 0x11; out[3] = 7; out[4] = 0xfe; out[9] = bufferDepth; out[10] = counter;
    out[11] = 0x80 | 0x12; out[12] = static_cast<std::uint8_t>(kDualSenseHapticsFrames * 2);
    bool loud = false;
    for (std::size_t i = 0; i < kDualSenseHapticsFrames * 2; ++i) {
        const auto sample = dualSenseNarrowSample(frames[i]);
        out[13 + i] = static_cast<std::uint8_t>(sample);
        loud = loud || dualSenseAudible(frames[i]);
    }
    const std::uint8_t seed = 0xa2;
    const std::uint32_t crc = ~dualSenseCrc32(dualSenseCrc32(0xffffffffu, &seed, 1), out.data(), 138);
    out[138] = static_cast<std::uint8_t>(crc); out[139] = static_cast<std::uint8_t>(crc >> 8);
    out[140] = static_cast<std::uint8_t>(crc >> 16); out[141] = static_cast<std::uint8_t>(crc >> 24);
    return loud;
}
} // namespace veyra::remoteplay
