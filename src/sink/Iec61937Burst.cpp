#include "veyra/sink/HdmiBitstreamOutput.h"

#include <algorithm>

namespace veyra::sink {

unsigned iecBurstFrames(uint16_t pc) {
    switch (pc & 0x1f) {
    case 0x01: return 1536; // AC-3
    case 0x0b: return 512;  // DTS type I
    case 0x0c: return 1024; // DTS type II
    case 0x0d: return 2048; // DTS type III
    default: return 0;
    }
}

std::vector<uint8_t> makeIecBurst(uint16_t pc, uint16_t pd, std::span<const uint8_t> payload) {
    const size_t bytes = iecBurstFrames(pc) * 4;
    const size_t stored = (size_t(pd) + 15) / 16 * 2;
    if (!pd || (pc & 0x80) || bytes < 8 || stored > bytes - 8 || payload.size() != stored)
        return {};

    std::vector<uint8_t> burst(bytes);
    burst[0] = 0x72;
    burst[1] = 0xf8;
    burst[2] = 0x1f;
    burst[3] = 0x4e;
    burst[4] = uint8_t(pc);
    burst[5] = uint8_t(pc >> 8);
    burst[6] = uint8_t(pd);
    burst[7] = uint8_t(pd >> 8);
    std::copy(payload.begin(), payload.end(), burst.begin() + 8);
    return burst;
}

void writeAc3PauseBursts(std::span<uint8_t> gap) {
    // IEC 61937-3 uses three stereo carrier frames per AC-3 pause burst.
    // The first burst announces the gap duration in decoded audio samples.
    constexpr size_t pauseBytes = 12;
    constexpr uint8_t header[]{0x72, 0xf8, 0x1f, 0x4e, 0x03, 0, 32, 0};
    std::ranges::fill(gap, uint8_t(0));
    if (gap.size() < pauseBytes) return;
    const auto samples = std::min<size_t>(gap.size() / 4, 0xffff);
    for (size_t offset = 0; offset + pauseBytes <= gap.size(); offset += pauseBytes)
        std::ranges::copy(header, gap.begin() + offset);
    gap[8] = uint8_t(samples);
    gap[9] = uint8_t(samples >> 8);
}

} // namespace veyra::sink
