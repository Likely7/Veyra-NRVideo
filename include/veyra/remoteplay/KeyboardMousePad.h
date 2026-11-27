// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/remoteplay/MouseStick.h"
#include "veyra/remoteplay/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <chrono>
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
        LeftSlow, MenuStick, SwipeUp, SwipeDown, SwipeLeft, SwipeRight, StickClickBoth, HalfPedal,
        StickInwardBoth, ActionCount
    };
    static constexpr int MouseLeft = 257, MouseMiddle = 258, MouseRight = 259;
    static constexpr int MouseBack = 260, MouseForward = 261;
    static constexpr int WheelUp = 262, WheelDown = 263, WheelLeft = 264, WheelRight = 265;
    static constexpr int Shift = 1, Ctrl = 2, Alt = 4;
    static constexpr int KeyMask = 511;
    using Bindings = std::array<int, ActionCount>;
    static constexpr int SlotCount = 2;
    static constexpr int DefaultDriveKey = 0x75; // F6
    static constexpr Bindings Defaults = {
        0x20, 0xA2, 'R', 'F', 0x25, 0x27, 0x26, 0x28, 0x09, 'Q', 0xA0, 'C',
        0x1B, 0x08, 'M', 'P', MouseRight, MouseLeft, 'W', 'S', 'A', 'D', 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 'V', 0, 'T'
    };
    static constexpr Bindings DrivingDefaults = {
        0x20, 0xA2, 'R', 'F', 0x25, 0x27, 0x26, 0x28, 0x09, 'Q', 0xA0, 'C',
        0x1B, 0x08, 'M', 'P', 'S', 'W', 0, 0, 'A', 'D', 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 'V', 0, 'T'
    };

    using Tuning = MouseStick::Tuning;
    static constexpr Tuning defaultTuning() {
        Tuning tuning;
        tuning.horizontal = tuning.vertical = 3.4;
        tuning.dpi = 2449;
        tuning.adsScale = 1.7;
        tuning.unlock = 3; // F1 leaves Escape available to the game.
        return tuning;
    }

    static int modifier(int key) {
        if (key == 0xA0 || key == 0xA1) return Shift;
        if (key == 0xA2 || key == 0xA3) return Ctrl;
        if (key == 0xA4 || key == 0xA5) return Alt;
        return 0;
    }
    static bool validBinding(int binding, bool allowEscape=false) {
        if (!binding) return true;
        const int key = binding & KeyMask;
        return binding >= 0 && (binding >> 9) <= 7 &&
            ((key >= 1 && key <= 254 && (key != 0x1B || allowEscape) && key != 0xe5 && key != 0x5B && key != 0x5C &&
                (key < 0x76 || key > 0x79)) || (key >= MouseLeft && key <= WheelRight));
    }
    static int encode(int key, int modifiers) { return key | ((modifiers & ~modifier(key) & 7) << 9); }

    const Bindings& bindings(bool driving = false, int slot = 0) const {
        return (driving ? drivingBindings_ : bindings_)[slot];
    }
    bool driving() const { return driving_; }
    int driveKey() const { return driveKey_; }
    void selectDriving(bool driving) {
        if (driving_ == driving) return;
        driving_ = driving;
        clearActions();
        const int mods = modifiers();
        for (int key = 1; key < WheelUp; ++key) {
            if (!held_[key] || encode(key, mods) == driveKey_) continue;
            const auto [action, matchedModifiers] = matchAction(key, mods);
            if (action < 0 || (action >= SwipeUp && action <= SwipeRight) ||
                (activeToggles() & (uint64_t{1} << action))) continue;
            active_[key] = action;
            activeModifiers_[key] = matchedModifiers;
        }
    }
    bool setDriveKey(int binding) {
        if (!validBinding(binding, tuning_.unlock != 0)) return false;
        if (binding && (hasBinding(binding, false) || hasBinding(binding, true))) return false;
        driveKey_ = binding;
        return true;
    }
    bool hasBinding(int binding, bool driving) const {
        const auto& layout = driving ? drivingBindings_ : bindings_;
        for (const auto& slot : layout)
            if (std::find(slot.begin(), slot.end(), binding) != slot.end()) return true;
        return false;
    }
    void resetLegacyDrivingBindings() {
        drivingBindings_ = bindings_;
        for (auto& slot : drivingBindings_) {
            for (auto& key : slot) if (key == 'W' || key == 'S') key = 0;
            slot[LeftUp] = slot[LeftDown] = 0;
        }
        drivingBindings_[0][R2] = 'W';
        drivingBindings_[0][L2] = 'S';
        reset();
    }
    const Tuning& tuning() const { return tuning_; }
    bool setBinding(int action, int binding, bool driving = false, int slot = 0) {
        if (action < 0 || action >= ActionCount || slot < 0 || slot >= SlotCount ||
            !validBinding(binding, tuning_.unlock != 0)) return false;
        if (binding && binding == driveKey_) return false;
        auto& layout = driving ? drivingBindings_ : bindings_;
        if (binding) {
            for (auto& keys : layout)
                for (auto& key : keys) if (key == binding) key = 0;
        }
        layout[slot][action] = binding;
        reset();
        return true;
    }
    void resetBindings(bool driving = false) {
        auto& layout = driving ? drivingBindings_ : bindings_;
        layout = {driving ? DrivingDefaults : Defaults, {}};
        if (tuning_.unlock == 0) layout[0][Options] = 0;
        reset();
    }
    void setTuning(Tuning t) { tuning_ = t; }

    int modifiers() const {
        int mask = 0;
        for (int k = 0xA0; k <= 0xA5; ++k)
            if (held_[k]) mask |= modifier(k);
        return mask;
    }

    // A primary key selects its action on press; release never changes it to
    // a different action, even if a shortcut modifier has already been lifted.
    bool digital(int key, bool down, int64_t now = clockUs()) {
        if (key < 1 || key > WheelRight || held_[key] == down) return false;
        held_[key] = down;
        const int currentModifiers = modifiers();
        if (down) {
            const int chord = encode(key, currentModifiers);
            if (driveKey_ && chord == driveKey_) {
                selectDriving(!driving_);
                return true;
            }
            const auto [matchedAction, matchedModifiers] = matchAction(key, currentModifiers);
            if (matchedAction < 0) return false;
            const auto bit = uint64_t{1} << matchedAction;
            const bool toggle = (activeToggles() & bit) != 0;
            const bool swipe = matchedAction >= SwipeUp && matchedAction <= SwipeRight;
            // Either slot can hold an action. Pressing its other key while it is
            // already held must not toggle it or restart a touchpad gesture.
            if ((toggle || swipe) && std::find(active_.begin(), active_.end(), matchedAction) == active_.end()) {
                if (toggle) toggled_ ^= bit;
                if (swipe) {
                    swipeAction_ = matchedAction;
                    swipeStart_ = now;
                    touchId_ = (touchId_ + 1) % 127;
                }
            }
            active_[key] = matchedAction;
            activeModifiers_[key] = matchedModifiers;
            return true;
        }
        bool changed = active_[key] >= 0;
        active_[key] = -1;
        if (modifier(key)) {
            for (int k = 1; k <= WheelRight; ++k) {
                const int mods = activeModifiers_[k];
                if (active_[k] >= 0 && (mods & currentModifiers) != mods) {
                    active_[k] = -1;
                    changed = true;
                }
            }
        }
        return changed;
    }

    static int64_t clockUs(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    uint64_t toggleMask() const{return toggleMask_;}
    uint64_t activeToggles() const {
        // Inward sticks are momentary. L2 is also held in the driving layout.
        const auto mask = toggleMask_ & ~(uint64_t{1} << StickInwardBoth);
        return driving_ ? mask & ~(uint64_t{1} << L2) : mask;
    }
    void setToggleMask(uint64_t mask){toggleMask_=mask;reset();}
    bool menuActive()const{return (toggleMask_&(uint64_t{1}<<MenuStick))?(toggled_&(uint64_t{1}<<MenuStick))!=0:std::find(active_.begin(),active_.end(),MenuStick)!=active_.end();}
    ControllerState state(bool* mouseControlsRightStick = nullptr, int64_t now = clockUs(),
                          bool* inwardSticksHeld = nullptr) const {
        std::array<bool, ActionCount> on{};
        for (const int a : active_) if (a >= 0) on[a] = true;
        const auto toggles = activeToggles();
        for(int a=0;a<ActionCount;++a)if(toggles&(uint64_t{1}<<a))on[a]=(toggled_&(uint64_t{1}<<a))!=0;
        ControllerState pad;
        pad.inputActive = true;
        for (int i = 0; i < 16; ++i) if (on[i]) pad.buttons |= uint32_t(1) << i;
        if (on[StickClickBoth]) pad.buttons |= ControllerState::L3 | ControllerState::R3;
        pad.l2 = on[L2] ? 255 : 0;
        pad.r2 = on[R2] ? 255 : 0;
        setStick(pad.leftX, pad.leftY, int(on[LeftRight]) - int(on[LeftLeft]),
                  int(on[LeftDown]) - int(on[LeftUp]));
        if (driving_) {
            const double dtMs = steerAt_ ? std::clamp(double(now - steerAt_) / 1000., 0., 300.) : 0;
            steerAt_ = now;
            const double target = pad.leftX / 32767.;
            if (tuning_.graduatedSteering) updateSteering(target, dtMs);
            else {
                const double dt = std::min(1., dtMs / 80.);
                if (target * steer_ < 0.) steer_ = 0.;
                steer_ += std::clamp(target - steer_, -dt, dt);
            }
            pad.leftX = int16_t(std::lround(steer_ * 32767.));
            const double limit = on[HalfPedal] ? 128. : 255.;
            pad.l2 = rampPedal(l2_, on[L2] ? limit : 0., dtMs);
            pad.r2 = rampPedal(r2_, on[R2] ? limit : 0., dtMs);
        }
        if(on[LeftSlow]){pad.leftX=int16_t(std::lround(pad.leftX*tuning_.walkScale));pad.leftY=int16_t(std::lround(pad.leftY*tuning_.walkScale));}
        if(swipeAction_>=SwipeUp){
            const double t=double(now-swipeStart_)/90000.;
            if(t>=0 && t<1.){const double d=(std::min(1.,t*1.25)-.5)*.75;
                pad.touches[0]={int8_t(touchId_),uint16_t(960+1920*d*(int(swipeAction_==SwipeRight)-int(swipeAction_==SwipeLeft))),
                    uint16_t(464+928*d*(int(swipeAction_==SwipeDown)-int(swipeAction_==SwipeUp)))};
            }
        }
        const int rightX = int(on[RightRight]) - int(on[RightLeft]);
        const int rightY = int(on[RightDown]) - int(on[RightUp]);
        const bool rightKeys = on[RightRight] || on[RightLeft] || on[RightUp] || on[RightDown];
        setStick(pad.rightX, pad.rightY, rightX, rightY);
        const bool inward = on[StickInwardBoth];
        if (inward) {
            pad.leftX = 32767;
            pad.rightX = -32767;
            pad.leftY = pad.rightY = 0;
        }
        if (inwardSticksHeld) *inwardSticksHeld = inward;
        if (mouseControlsRightStick) *mouseControlsRightStick = tuning_.mouseLook && !rightKeys && !inward;
        return pad;
    }

    void reset() {
        held_.fill(false);
        clearActions();
    }
    KeyboardMousePad() { reset(); }

private:
    std::pair<int, int> matchAction(int key, int modifiers) const {
        const int chordModifiers = encode(key, modifiers) >> 9;
        const auto& layout = driving_ ? drivingBindings_ : bindings_;
        int bestRank = -1, result = -1, required = 0;
        for (const auto& slot : layout) {
            for (int action = 0; action < ActionCount; ++action) {
                const int binding = slot[action], mods = binding >> 9;
                if ((binding & KeyMask) != key || (mods & chordModifiers) != mods) continue;
                const int rank = std::popcount(unsigned(mods));
                if (rank <= bestRank) continue;
                bestRank = rank;
                result = action;
                required = mods;
            }
        }
        return {result, required};
    }
    void updateSteering(double target, double ms) const {
        if (target == 0. || target * steer_ < 0.) {
            const double toCenter = std::abs(steer_) * tuning_.steeringReturnMs;
            steer_ = std::copysign(std::max(0., std::abs(steer_) - ms / tuning_.steeringReturnMs), steer_);
            ms = std::max(0., ms - toCenter);
            if (target == 0. || ms == 0.) return;
        }
        double amount = std::abs(steer_);
        const double initial = tuning_.steeringInitial;
        if (amount < initial) {
            const double toInitial = (initial - amount) * (40. / initial);
            amount += std::min(ms, toInitial) * (initial / 40.);
            ms = std::max(0., ms - toInitial);
        }
        amount += ms * (1. - initial) / tuning_.steeringFullMs;
        steer_ = std::copysign(std::min(amount, std::abs(target)), target);
    }
    uint8_t rampPedal(double& value, double target, double ms) const {
        value = tuning_.pedalRiseMs > 0 && value < target
            ? std::min(target, value + 255. * ms / tuning_.pedalRiseMs) : target;
        return uint8_t(std::lround(value));
    }
    void clearActions() {
        active_.fill(-1);
        toggled_ = 0;
        swipeAction_ = -1;
        steer_ = l2_ = r2_ = 0;
        steerAt_ = 0;
    }
    static void setStick(int16_t& xAxis, int16_t& yAxis, double x, double y) {
        const double scale = 32767.0 / std::max(1.0, std::hypot(x, y));
        xAxis = int16_t(std::lround(x * scale));
        yAxis = int16_t(std::lround(y * scale));
    }
    std::array<Bindings, SlotCount> bindings_{Defaults, {}};
    std::array<Bindings, SlotCount> drivingBindings_{DrivingDefaults, {}};
    int driveKey_ = DefaultDriveKey;
    bool driving_ = false;
    mutable double steer_ = 0, l2_ = 0, r2_ = 0;
    mutable int64_t steerAt_ = 0;
    std::array<bool, WheelRight + 1> held_{};
    std::array<int, WheelRight + 1> active_{};
    std::array<int, WheelRight + 1> activeModifiers_{};
    uint64_t toggleMask_=0,toggled_=0;
    int swipeAction_=-1,touchId_=0;int64_t swipeStart_=0;
    Tuning tuning_ = defaultTuning();
};

} // namespace veyra::remoteplay
