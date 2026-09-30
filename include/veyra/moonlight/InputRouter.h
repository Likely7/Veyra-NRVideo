// SPDX-License-Identifier: GPL-3.0-only
// Turns the Win32 input messages of a captured window into host input events. No Qt and no
// library calls: events go to an InputSink, so the whole translation (key codes, modifier
// tracking, reserved hotkeys, buttons, wheel) is tested with synthesized messages.
#pragma once
#include <windows.h>
#include <windowsx.h>

#include <cstdint>

#include "veyra/moonlight/InputMap.h"

namespace veyra::moonlight {

class InputSink {
public:
    virtual ~InputSink() = default;
    virtual void key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) = 0;
    virtual void mouseMove(int dx, int dy) = 0;
    virtual void mouseButton(int button, bool down) = 0;   // 1 left, 2 middle, 3 right, 4 X1, 5 X2
    virtual void scroll(int delta, bool horizontal) = 0;
};

class InputRouter {
public:
    explicit InputRouter(InputSink& sink) : sink_(sink) {}

    // True when the message was consumed (it must not reach the UI underneath).
    bool message(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN: return key(wParam, lParam, true);
        case WM_KEYUP: case WM_SYSKEYUP: return key(wParam, lParam, false);
        case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: case WM_UNICHAR: case WM_IME_CHAR:
            return true;   // typed text reaches the host as key events, not as characters
        case WM_SYSCOMMAND:
            return (wParam & 0xFFF0) == SC_KEYMENU;   // Alt or Alt+Space must not open the window menu
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: return button(1, true);
        case WM_LBUTTONUP: return button(1, false);
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: return button(2, true);
        case WM_MBUTTONUP: return button(2, false);
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: return button(3, true);
        case WM_RBUTTONUP: return button(3, false);
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: return button(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 4 : 5, true);
        case WM_XBUTTONUP: return button(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 4 : 5, false);
        case WM_MOUSEWHEEL: sink_.scroll(GET_WHEEL_DELTA_WPARAM(wParam), false); return true;
        case WM_MOUSEHWHEEL: sink_.scroll(GET_WHEEL_DELTA_WPARAM(wParam), true); return true;
        case WM_MOUSEMOVE: return true;   // movement comes from raw input (relative, unaccelerated by the window)
        default: return false;
        }
    }

    // Relative movement from a WM_INPUT mouse report.
    void rawMouse(long dx, long dy) { sink_.mouseMove(int(dx), int(dy)); }

    // Set when the last key press was one of the app's own hotkeys; cleared by taking it.
    Reserved takeReserved() {
        const Reserved r = reserved_;
        reserved_ = Reserved::None;
        return r;
    }

    // Forget what is held (capture ended; the session releases the host side itself).
    void reset() { held_.clear(); reserved_ = Reserved::None; }

private:
    bool key(WPARAM wParam, LPARAM lParam, bool down) {
        const uint32_t vk = uint32_t(wParam);
        const uint32_t scan = uint32_t((lParam >> 16) & 0xFF);
        const bool extended = ((lParam >> 24) & 1) != 0;
        const HostKey host = hostKey(vk, scan, extended);
        if (down) {
            if (vk != 0x10 && vk != 0x11 && vk != 0x12) {   // a modifier alone is never a hotkey
                const Reserved r = reservedHotkey(vk, held_.modifiers());
                if (r != Reserved::None) { reserved_ = r; swallowedUp_ = vk; return true; }
            }
            held_.press(host.code, host.modifier);
        } else {
            if (swallowedUp_ == vk) { swallowedUp_ = 0; return true; }   // the matching key-up of a hotkey
            held_.release(host.code);
        }
        sink_.key(vk, scan, extended, down);
        return true;
    }

    bool button(int which, bool down) {
        sink_.mouseButton(which, down);
        return true;
    }

    InputSink& sink_;
    HeldKeys held_;
    Reserved reserved_ = Reserved::None;
    uint32_t swallowedUp_ = 0;
};

} // namespace veyra::moonlight
