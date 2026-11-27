// Builds the EDID the card shows to the HDMI source out of the one of the TV behind this PC, so that a
// console sees the luminance, the speakers and the audio formats of the real output chain while the
// video part stays what the card can receive (HDMI 2.0, 4K60 4:2:2 HDR10).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xcap {

struct AudioCaps { bool pcm8 = true, ac3 = false, eac3 = false, mat = false, dts = false, dtsHd = false; }; // what this PC can play out

struct TvEdid {
    std::wstring name;                  // as Windows shows the monitor
    std::vector<uint8_t> bytes;         // 128 * n
    std::wstring gdi;                   // \\.\DISPLAYn, empty without a desktop
    std::wstring path;                  // monitor device path: the same for the same monitor on the same connector, empty without a desktop
    bool primary = false;
};
std::vector<TvEdid> activeMonitors();   // primary display first

struct TvInfo {
    std::string name;
    bool hdr = false, bt2020 = false, atmosEac3 = false, atmosMat = false;
    unsigned maxLum = 0, avgLum = 0, minLum = 0;    // CTA-861.3 codes
    unsigned pcmChannels = 2;
    std::vector<std::array<uint8_t, 3>> sads;
    std::vector<uint8_t> speakers;
};
TvInfo parseTv(const std::vector<uint8_t>& edid);
// The TV among the monitors: the one whose name contains `name`, else the one with Dolby audio, else the primary display.
const TvEdid* chooseTv(const std::vector<TvEdid>& monitors, const std::wstring& name = {});

std::array<uint8_t, 256> composeEdid(const TvInfo& tv, const AudioCaps& pc);
uint8_t edidNumber(const std::array<uint8_t, 256>& edid);      // 1..255, changes with the contents

class Device;
// Writes the EDID and lets the firmware switch to it (one hot plug). False: old FPGA image, or refused.
// Nothing happens when the same EDID is already in use; `changed` tells which. "The same" is decided by
// the number the firmware reports together with a 16-bit check word of the contents that is kept in the
// otherwise unused bits 31:16 of XCAP_REG_HOST_CTRL (0 there: written by a library without the check word,
// then the number alone decides, as it used to).
bool applyEdid(Device& device, const std::array<uint8_t, 256>& edid, bool* changed = nullptr);

} // namespace xcap
