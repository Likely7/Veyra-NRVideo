#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

// IEC 61937 stores AC-3 in swapped 16-bit words. CRC-16/ANSI over the
// complete syncframe after the sync word has zero residue.
inline bool validAc3CarrierPayload(uint16_t bits, std::span<const uint8_t> payload) {
    if (payload.size() < 8 || bits != payload.size() * 8 ||
        payload[0] != 0x77 || payload[1] != 0x0b)
        return false;

    uint16_t crc = 0;
    for (size_t i = 2; i < payload.size(); ++i) {
        crc ^= uint16_t(payload[i ^ 1]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0));
    }
    return crc == 0;
}

} // namespace veyra
