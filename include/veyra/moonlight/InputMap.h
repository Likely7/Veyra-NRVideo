// SPDX-License-Identifier: GPL-3.0-only
// Pure input translation for a Moonlight session: SDL gamepad state to the
// GameStream pad frame, Win32 key messages to the codes the host expects, and the
// reserved hotkeys that are never forwarded. No library calls here, so it can be
// tested offline; the session source static_asserts the constants against
// moonlight-common-c.
#pragma once
#include <algorithm>
#include <cstdint>

#include "veyra/remoteplay/Types.h"

namespace veyra::moonlight {

// Button flags of LiSendMultiControllerEvent (Xbox layout).
constexpr uint32_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
constexpr uint32_t kPadStart = 0x0010, kPadBack = 0x0020, kPadLeftStick = 0x0040, kPadRightStick = 0x0080;
constexpr uint32_t kPadLeftBumper = 0x0100, kPadRightBumper = 0x0200, kPadGuide = 0x0400;
constexpr uint32_t kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000;
constexpr uint32_t kPadTouchpad = 0x100000, kPadMisc = 0x200000;

struct PadFrame {
    uint32_t buttons = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    bool operator==(const PadFrame&) const = default;
};

// GameStream sticks are "up is positive"; SDL's are "down is positive".
inline int16_t flipAxis(int16_t value) { return value == INT16_MIN ? INT16_MAX : int16_t(-value); }

// The positional layout is kept: the south face button is A whatever is printed on it.
// A frame from an unfocused or lost device is all zeros, which releases everything.
inline PadFrame mapPad(const remoteplay::ControllerState& state) {
    PadFrame frame;
    if (!state.inputActive) return frame;
    using B = remoteplay::ControllerState;
    struct Pair { uint32_t from, to; };
    static constexpr Pair table[] = {
        {B::Cross, kPadA}, {B::Circle, kPadB}, {B::Square, kPadX}, {B::Triangle, kPadY},
        {B::Left, kPadLeft}, {B::Right, kPadRight}, {B::Up, kPadUp}, {B::Down, kPadDown},
        {B::L1, kPadLeftBumper}, {B::R1, kPadRightBumper}, {B::L3, kPadLeftStick}, {B::R3, kPadRightStick},
        {B::Options, kPadStart}, {B::Share, kPadBack}, {B::PS, kPadGuide}, {B::Touchpad, kPadTouchpad},
    };
    for (const auto& pair : table)
        if (state.buttons & pair.from) frame.buttons |= pair.to;
    frame.leftTrigger = state.l2;
    frame.rightTrigger = state.r2;
    frame.leftX = state.leftX;
    frame.leftY = flipAxis(state.leftY);
    frame.rightX = state.rightX;
    frame.rightY = flipAxis(state.rightY);
    return frame;
}

// --- keyboard ---------------------------------------------------------------------------

constexpr uint8_t kModShift = 0x01, kModCtrl = 0x02, kModAlt = 0x04, kModMeta = 0x08;

// The host takes Windows virtual-key codes with bit 15 set. The plain VK_SHIFT / VK_CONTROL /
// VK_MENU that WM_KEYDOWN reports are resolved to their left and right keys, and the keypad
// keys that a Num Lock-off keyboard reports as navigation keys are sent as the keypad keys
// they are (extended navigation keys stay navigation keys).
// Bit 15 marks a Windows virtual-key code.
constexpr int16_t hostCode(uint32_t vk) { return int16_t(uint16_t(0x8000u | vk)); }

struct HostKey {
    int16_t code = 0;
    uint8_t modifier = 0;   // which modifier this key is, if any
};

inline HostKey hostKey(uint32_t vk, uint32_t scanCode, bool extended) {
    switch (vk) {
    case 0x10: return scanCode == 0x36 ? HostKey{hostCode(0xA1), kModShift} : HostKey{hostCode(0xA0), kModShift};
    case 0xA0: return {hostCode(0xA0), kModShift};
    case 0xA1: return {hostCode(0xA1), kModShift};
    case 0x11: return extended ? HostKey{hostCode(0xA3), kModCtrl} : HostKey{hostCode(0xA2), kModCtrl};
    case 0xA2: return {hostCode(0xA2), kModCtrl};
    case 0xA3: return {hostCode(0xA3), kModCtrl};
    case 0x12: return extended ? HostKey{hostCode(0xA5), kModAlt} : HostKey{hostCode(0xA4), kModAlt};
    case 0xA4: return {hostCode(0xA4), kModAlt};
    case 0xA5: return {hostCode(0xA5), kModAlt};
    case 0x5B: return {hostCode(0x5B), kModMeta};
    case 0x5C: return {hostCode(0x5C), kModMeta};
    default: break;
    }
    if (!extended) {
        switch (vk) {
        case 0x2D: return {hostCode(0x60), 0};   // Insert  -> Numpad0
        case 0x23: return {hostCode(0x61), 0};   // End     -> Numpad1
        case 0x28: return {hostCode(0x62), 0};   // Down    -> Numpad2
        case 0x22: return {hostCode(0x63), 0};   // PageDn  -> Numpad3
        case 0x25: return {hostCode(0x64), 0};   // Left    -> Numpad4
        case 0x0C: return {hostCode(0x65), 0};   // Clear   -> Numpad5
        case 0x27: return {hostCode(0x66), 0};   // Right   -> Numpad6
        case 0x24: return {hostCode(0x67), 0};   // Home    -> Numpad7
        case 0x26: return {hostCode(0x68), 0};   // Up      -> Numpad8
        case 0x21: return {hostCode(0x69), 0};   // PageUp  -> Numpad9
        case 0x2E: return {hostCode(0x6E), 0};   // Delete  -> Decimal
        default: break;
        }
    }
    return {hostCode((vk & 0xFF)), 0};
}

// Keys held on the host, so they can all be released when capture ends.
class HeldKeys {
public:
    // Returns false for a repeat of a key that is already down (auto-repeat is the host's job).
    bool press(int16_t code, uint8_t modifier) {
        for (int i = 0; i < count_; ++i)
            if (codes_[i] == code) return false;
        if (count_ < kMax) { codes_[count_] = code; mods_[count_] = modifier; ++count_; }
        if (modifier) held_ |= modifier;
        return true;
    }
    bool release(int16_t code) {
        for (int i = 0; i < count_; ++i) {
            if (codes_[i] != code) continue;
            codes_[i] = codes_[count_ - 1]; mods_[i] = mods_[count_ - 1]; --count_;
            held_ = 0;
            for (int k = 0; k < count_; ++k) held_ |= mods_[k];
            return true;
        }
        return false;
    }
    uint8_t modifiers() const { return held_; }
    int count() const { return count_; }
    int16_t at(int index) const { return codes_[index]; }
    void clear() { count_ = 0; held_ = 0; }
private:
    static constexpr int kMax = 32;
    int16_t codes_[kMax]{};
    uint8_t mods_[kMax]{};
    int count_ = 0;
    uint8_t held_ = 0;
};

// Ctrl+Alt+Shift+<key> is handled by the app and never sent to the host.
enum class Reserved { None, ReleaseCapture, Quit, ToggleStats };

inline Reserved reservedHotkey(uint32_t vk, uint8_t modifiers) {
    constexpr uint8_t all = kModShift | kModCtrl | kModAlt;
    if ((modifiers & all) != all) return Reserved::None;
    switch (vk) {
    case 'Z': return Reserved::ReleaseCapture;
    case 'Q': return Reserved::Quit;
    case 'S': return Reserved::ToggleStats;
    default: return Reserved::None;
    }
}

// A raw mouse delta can exceed what one host event carries; split it.
inline int16_t clampDelta(long value) { return int16_t(std::clamp<long>(value, -32767, 32767)); }

} // namespace veyra::moonlight
