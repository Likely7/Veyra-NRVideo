// SPDX-License-Identifier: GPL-3.0-only
// The data-channel protocol of Xbox home streaming, as Greenlight's player speaks it
// (unknownskl/greenlight packages/player, MIT): the JSON handshake and messages on "message" and
// "control", and the little-endian binary reports on "input". Pure functions only, tested offline.
#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "veyra/remoteplay/Types.h"

namespace veyra::xbox {

enum ReportType : uint16_t {
    ReportMetadata = 1, ReportGamepad = 2, ReportPointer = 4, ReportClientMetadata = 8, ReportServerMetadata = 16,
    ReportMouse = 32, ReportKeyboard = 64, ReportVibration = 128,
};

// Gamepad buttons as the input report carries them.
enum GamepadButton : uint16_t {
    BtnNexus = 2, BtnMenu = 4, BtnView = 8, BtnA = 16, BtnB = 32, BtnX = 64, BtnY = 128,
    BtnUp = 256, BtnDown = 512, BtnLeft = 1024, BtnRight = 2048,
    BtnLeftShoulder = 4096, BtnRightShoulder = 8192, BtnLeftThumb = 16384, BtnRightThumb = 32768,
};

// Which inputs the player is physically touching, sent with every pad. Games that read input through
// GameInput ignore buttons and sticks whose physicality bit is clear, so a fixed value leaves only the
// system-handled Xbox button working in game. Bit layout from the Xbox streaming client
// (Geocld/XStreaming abc02ff "fix: some games input issue"; kirtarget/jadebox vendored player).
enum GamepadPhysicality : uint32_t {
    PhysDPadUp = 0x1, PhysDPadDown = 0x2, PhysDPadLeft = 0x4, PhysDPadRight = 0x8,
    PhysMenu = 0x10, PhysView = 0x20, PhysLeftThumb = 0x40, PhysRightThumb = 0x80,
    PhysLeftShoulder = 0x100, PhysRightShoulder = 0x200, PhysNexus = 0x400,
    PhysA = 0x1000, PhysB = 0x2000, PhysX = 0x4000, PhysY = 0x8000,
    PhysLeftTrigger = 0x10000, PhysRightTrigger = 0x20000,
    PhysLeftThumbX = 0x40000, PhysLeftThumbY = 0x80000, PhysRightThumbX = 0x100000, PhysRightThumbY = 0x200000,
};

struct GamepadFrame {
    uint8_t index = 0;
    uint16_t buttons = 0;
    int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;   // up is positive (the report's convention)
    uint16_t leftTrigger = 0, rightTrigger = 0;             // 0..65535
    bool operator==(const GamepadFrame&) const = default;
};

struct Vibration {
    uint8_t gamepad = 0;
    uint8_t leftMotor = 0, rightMotor = 0, leftTrigger = 0, rightTrigger = 0;   // percent 0..100
    uint16_t durationMs = 0, delayMs = 0;
    uint8_t repeat = 0;
};

namespace detail {
inline void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { b[at] = uint8_t(v); b[at + 1] = uint8_t(v >> 8); }
inline void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i)); }
inline void putDouble(std::vector<uint8_t>& b, size_t at, double v) { uint64_t u; std::memcpy(&u, &v, 8); for (int i = 0; i < 8; ++i) b[at + size_t(i)] = uint8_t(u >> (8 * i)); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
}

// Header: report type (u16), sequence (u32), timestamp in ms (f64) = 14 bytes.
// maxTouchPoints 0: a client that reports touch gets the touch-controls layout, which games such as
// GTA Online then prefer over the gamepad.
inline std::vector<uint8_t> clientMetadataReport(uint32_t sequence, double timeMs, uint8_t maxTouchPoints = 0) {
    std::vector<uint8_t> b(15, 0);
    detail::put16(b, 0, ReportClientMetadata);
    detail::put32(b, 2, sequence);
    detail::putDouble(b, 6, timeMs);
    b[14] = maxTouchPoints;
    return b;
}

// Greenlight 58e832a1: input/packet.ts _writeMetadataData. Preserve the actual
// RTP key and seven u32 fields; times share the session's monotonic origin.
struct VideoFeedback {
    uint32_t rtpTimestamp=0, arrivalMs=0, submittedMs=0, decodedMs=0, renderedMs=0;
};
inline std::vector<uint8_t> videoFeedbackReport(uint32_t sequence,double timeMs,const VideoFeedback& f){
    std::vector<uint8_t> b(43,0);
    detail::put16(b,0,ReportMetadata);detail::put32(b,2,sequence);detail::putDouble(b,6,timeMs);b[14]=1;
    const auto sent=uint32_t(uint64_t(timeMs));
    const uint32_t values[]={f.rtpTimestamp,f.arrivalMs,f.submittedMs,f.decodedMs,f.renderedMs,sent,sent};
    for(size_t i=0;i<7;++i)detail::put32(b,15+4*i,values[i]);
    return b;
}

inline uint32_t gamepadPhysicality(const GamepadFrame& p) {
    struct Pair { uint16_t button; uint32_t bit; };
    static constexpr Pair map[] = {
        {BtnUp, PhysDPadUp}, {BtnDown, PhysDPadDown}, {BtnLeft, PhysDPadLeft}, {BtnRight, PhysDPadRight},
        {BtnMenu, PhysMenu}, {BtnView, PhysView}, {BtnLeftThumb, PhysLeftThumb}, {BtnRightThumb, PhysRightThumb},
        {BtnLeftShoulder, PhysLeftShoulder}, {BtnRightShoulder, PhysRightShoulder}, {BtnNexus, PhysNexus},
        {BtnA, PhysA}, {BtnB, PhysB}, {BtnX, PhysX}, {BtnY, PhysY},
    };
    uint32_t bits = 0;
    for (const auto& m : map) if (p.buttons & m.button) bits |= m.bit;
    if (p.leftTrigger) bits |= PhysLeftTrigger;
    if (p.rightTrigger) bits |= PhysRightTrigger;
    if (p.leftX || p.leftY) bits |= PhysLeftThumbX | PhysLeftThumbY;
    if (p.rightX || p.rightY) bits |= PhysRightThumbX | PhysRightThumbY;
    return bits;
}

// One report with up to a few gamepads: count (u8), then per pad 23 bytes: index, buttons, 4 axes,
// 2 triggers, physicality (u32 LE, the inputs in use) and virtual physicality (u32 LE 0).
inline std::vector<uint8_t> gamepadReport(uint32_t sequence, double timeMs, const std::vector<GamepadFrame>& pads) {
    std::vector<uint8_t> b(14 + 1 + 23 * pads.size(), 0);
    detail::put16(b, 0, ReportGamepad);
    detail::put32(b, 2, sequence);
    detail::putDouble(b, 6, timeMs);
    size_t at = 14;
    b[at++] = uint8_t(pads.size());
    for (const auto& p : pads) {
        b[at++] = p.index;
        detail::put16(b, at, p.buttons);
        detail::put16(b, at + 2, uint16_t(p.leftX));
        detail::put16(b, at + 4, uint16_t(p.leftY));
        detail::put16(b, at + 6, uint16_t(p.rightX));
        detail::put16(b, at + 8, uint16_t(p.rightY));
        detail::put16(b, at + 10, p.leftTrigger);
        detail::put16(b, at + 12, p.rightTrigger);
        detail::put32(b, at + 14, gamepadPhysicality(p));
        detail::put32(b, at + 18, 0);
        at += 22;
    }
    return b;
}

// SDL state (down-positive sticks, 0..255 triggers) to the report's frame.
inline GamepadFrame gamepadFromController(const remoteplay::ControllerState& s, uint8_t index = 0) {
    GamepadFrame f;
    f.index = index;
    if (!s.inputActive) return f;
    using B = remoteplay::ControllerState;
    struct Pair { uint32_t from; uint16_t to; };
    static constexpr Pair map[] = {
        {B::Cross, BtnA}, {B::Circle, BtnB}, {B::Square, BtnX}, {B::Triangle, BtnY},
        {B::Up, BtnUp}, {B::Down, BtnDown}, {B::Left, BtnLeft}, {B::Right, BtnRight},
        {B::L1, BtnLeftShoulder}, {B::R1, BtnRightShoulder}, {B::L3, BtnLeftThumb}, {B::R3, BtnRightThumb},
        {B::Options, BtnMenu}, {B::Share, BtnView}, {B::PS, BtnNexus},
    };
    for (const auto& m : map) if (s.buttons & m.from) f.buttons = uint16_t(f.buttons | m.to);
    const auto flip = [](int16_t v) { return v == INT16_MIN ? int16_t(INT16_MAX) : int16_t(-v); };
    const auto clamp = [](int16_t v) { return v < -32767 ? int16_t(-32767) : v; };
    f.leftX = clamp(s.leftX);
    f.leftY = flip(s.leftY);
    f.rightX = clamp(s.rightX);
    f.rightY = flip(s.rightY);
    f.leftTrigger = uint16_t(s.l2 * 257u);
    f.rightTrigger = uint16_t(s.r2 * 257u);
    return f;
}

inline std::optional<Vibration> parseVibration(const uint8_t* p, size_t n) {
    if (n < 13 || p[0] != ReportVibration) return std::nullopt;
    Vibration v;
    v.gamepad = p[3];
    v.leftMotor = p[4]; v.rightMotor = p[5]; v.leftTrigger = p[6]; v.rightTrigger = p[7];
    v.durationMs = detail::get16(p + 8);
    v.delayMs = detail::get16(p + 10);
    v.repeat = p[12];
    return v;
}

// Server metadata: height then width (u32 LE each) after the 2-byte header.
inline bool parseServerMetadata(const uint8_t* p, size_t n, uint32_t* width, uint32_t* height) {
    if (n < 10 || p[0] != ReportServerMetadata) return false;
    *height = detail::get32(p + 2);
    *width = detail::get32(p + 6);
    return true;
}

} // namespace veyra::xbox
