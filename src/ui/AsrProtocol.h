// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <cstdint>

namespace veyra::ui::asr {
// The two ends are built from the same x64 Windows executable. All integers
// and PCM samples use native little-endian byte order.
enum class FrameType : uint32_t { Config = 1, Reset, Audio, Quit, Gap };
inline constexpr uint32_t frameMagic = 0x52534156; // 'VASR'
inline constexpr uint32_t maxPayload = 8192;
inline constexpr uint32_t audioCreditBytes = 8192; // 128 ms of 16 kHz float PCM.
inline constexpr int64_t maxAudioAgeUs = 500000;
inline constexpr int64_t maxPendingSegmentAgeUs = 1600000;
inline constexpr int64_t maxResultAgeUs = 4000000;
struct FrameHeader {
    uint32_t magic = frameMagic;
    FrameType type = FrameType::Audio;
    uint64_t epoch = 0;
    int64_t playoutUs = 0;
    uint32_t bytes = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(FrameHeader) == 32);

inline QByteArray frame(FrameType type, uint64_t epoch, int64_t playoutUs, QByteArrayView data = {}) {
    if (data.size() > maxPayload) return {};
    FrameHeader header{frameMagic, type, epoch, playoutUs, uint32_t(data.size()), 0};
    QByteArray result(reinterpret_cast<const char*>(&header), sizeof(header));
    result.append(data.data(), data.size());
    return result;
}
}
