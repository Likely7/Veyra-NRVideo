// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/remoteplay/MouseStick.h"
#include "veyra/remoteplay/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace veyra::remoteplay {

// Primary keys are Win32 virtual keys (left/right modifiers are distinct).
// The upper bits optionally hold Shift, Ctrl and Alt. Mouse keys follow VKs.
class KeyboardMousePad {
public:
    enum Action {
        Cross, Circle, Square, Triangle, Left, Right, Up, Down,
        L1, R1, L3, R3, Options, Share, Touchpad, PS, L2, R2,
        LeftUp, LeftDown, LeftLeft, LeftRight, RightUp, RightDown, RightLeft, RightRight,
        ActionCount
    };
    static constexpr int MouseLeft = 257, MouseMiddle = 258, MouseRight = 259;
    static constexpr int MouseBack = 260, MouseForward = 261;
    static constexpr int WheelUp = 262, WheelDown = 263, WheelLeft = 264, WheelRight = 265;
    static constexpr int Shift = 1, Ctrl = 2, Alt = 4;
    static constexpr int KeyMask = 511;
    static constexpr std::array<int, ActionCount> Defaults = {
        0x20, 'C', 'E', 'F', 0x25, 0x27, 0x26, 0x28, 'Q', 'R', 0xA0, MouseMiddle,
        0x0D, 0x08, 'T', 'P', MouseRight, MouseLeft, 'W', 'S', 'A', 'D', 0, 0, 0, 0
    };

    using Tuning = MouseStick::Tuning;

    static int modifier(int key) {
        if (key == 0xA0 || key == 0xA1) return Shift;
        if (key == 0xA2 || key == 0xA3) return Ctrl;
        if (key == 0xA4 || key == 0xA5) return Alt;
        return 0;
    }
    static bool validBinding(int binding) {
        if (!binding) return true;
        const int key = binding & KeyMask;
        return binding >= 0 && (binding >> 9) <= 7 &&
            ((key >= 1 && key <= 254 && key != 0x1B && key != 0x5B && key != 0x5C &&
                (key < 0x76 || key > 0x79)) || (key >= MouseLeft && key <= WheelRight));
    }
    static int encode(int key, int modifiers) { return key | ((modifiers & ~modifier(key) & 7) << 9); }

    const auto& bindings() const { return bindings_; }
    const Tuning& tuning() const { return tuning_; }
    bool setBinding(int action, int binding) {
        if (action < 0 || action >= ActionCount || !validBinding(binding)) return false;
        for (int i = 0; binding && i < ActionCount; ++i)
            if (i != action && bindings_[i] == binding) bindings_[i] = 0;
        bindings_[action] = binding;
        reset();
        return true;
    }
    void resetBindings() { bindings_ = Defaults; reset(); }
    void setTuning(Tuning t) { tuning_ = t; }

    int modifiers() const {
        int mask = 0;
        for (int k = 0xA0; k <= 0xA5; ++k)
            if (held_[k]) mask |= modifier(k);
        return mask;
    }

    // A primary key selects its action on press; release never changes it to
    // a different action, even if a shortcut modifier has already been lifted.
    bool digital(int key, bool down) {
        if (key < 1 || key > WheelRight || held_[key] == down) return false;
        held_[key] = down;
        const int currentModifiers = modifiers();
        if (down) {
            const int chord = encode(key, currentModifiers);
            for (int action = 0; action < ActionCount; ++action) {
                const int binding = bindings_[action];
                if (binding == chord) {
                    active_[key] = action;
                    return true;
                }
                if (binding == key) active_[key] = action;
            }
            return active_[key] >= 0;
        }
        bool changed = active_[key] >= 0;
        active_[key] = -1;
        if (modifier(key)) {
            for (int& action : active_) {
                if (action >= 0 && ((bindings_[action] >> 9) & currentModifiers) != (bindings_[action] >> 9)) {
                    action = -1;
                    changed = true;
                }
            }
        }
        return changed;
    }

    ControllerState state(bool* mouseControlsRightStick = nullptr) const {
        std::array<bool, ActionCount> on{};
        for (const int a : active_) if (a >= 0) on[a] = true;
        ControllerState pad;
        pad.inputActive = true;
        for (int i = 0; i < 16; ++i) if (on[i]) pad.buttons |= uint32_t(1) << i;
        pad.l2 = on[L2] ? 255 : 0;
        pad.r2 = on[R2] ? 255 : 0;
        setStick(pad.leftX, pad.leftY, int(on[LeftRight]) - int(on[LeftLeft]),
                  int(on[LeftDown]) - int(on[LeftUp]));
        const int rightX = int(on[RightRight]) - int(on[RightLeft]);
        const int rightY = int(on[RightDown]) - int(on[RightUp]);
        const bool rightKeys = on[RightRight] || on[RightLeft] || on[RightUp] || on[RightDown];
        setStick(pad.rightX, pad.rightY, rightX, rightY);
        if (mouseControlsRightStick) *mouseControlsRightStick = tuning_.mouseLook && !rightKeys;
        return pad;
    }

    void reset() {
        held_.fill(false);
        active_.fill(-1);
    }
    KeyboardMousePad() { reset(); }

private:
    static void setStick(int16_t& xAxis, int16_t& yAxis, double x, double y) {
        const double scale = 32767.0 / std::max(1.0, std::hypot(x, y));
        xAxis = int16_t(std::lround(x * scale));
        yAxis = int16_t(std::lround(y * scale));
    }
    std::array<int, ActionCount> bindings_ = Defaults;
    std::array<bool, WheelRight + 1> held_{};
    std::array<int, WheelRight + 1> active_{};
    Tuning tuning_{};
};

} // namespace veyra::remoteplay
