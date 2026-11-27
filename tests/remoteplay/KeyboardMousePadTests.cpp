// Standalone checks for the keyboard and mouse pad of a PS5 session. No SDL, console or Chiaki.
#include "veyra/remoteplay/KeyboardMousePad.h"
#include <cstdio>
#include <cstdlib>

using veyra::remoteplay::ControllerState;
using veyra::remoteplay::HostTime;
using veyra::remoteplay::KeyboardMousePad;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
constexpr HostTime ms(int v) { return HostTime(v) * 10000; }
// One pad sample, 8 ms after the previous one, on top of `base`.
ControllerState sample(KeyboardMousePad& pad, int& tick, ControllerState base = {}) {
    pad.apply(base, ms(++tick * 8));
    return base;
}
}

int main() {
    {   // Keys: physical WASD is the left stick, a diagonal has the length of one direction.
        KeyboardMousePad pad; int tick = 0;
        pad.key(0x11, false, true);
        auto s = sample(pad, tick);
        check(s.inputActive && s.leftX == 0 && s.leftY == -32767, "W is the left stick fully up");
        pad.key(0x20, false, true);
        s = sample(pad, tick);
        check(s.leftX == 23169 && s.leftY == -23169, "W+D is a diagonal of the same length");
        pad.key(0x11, false, false); pad.key(0x20, false, false);
        s = sample(pad, tick);
        check(s.leftX == 0 && s.leftY == 0, "released keys centre the stick");
        pad.key(0x1e, false, true); pad.key(0x20, false, true);
        check(sample(pad, tick).leftX == 0, "opposite keys cancel");
    }
    {   // Buttons, triggers and the extended flag.
        KeyboardMousePad pad; int tick = 0;
        pad.key(0x39, false, true); pad.key(0x148, true, true); pad.key(0x48, false, true);
        pad.mouseButton(1, true); pad.mouseButton(3, true); pad.mouseButton(2, true);
        auto s = sample(pad, tick);
        check(s.buttons == (ControllerState::Cross | ControllerState::Up | ControllerState::R3), "Space, arrow up and middle click; numpad 8 is not the arrow");
        check(s.l2 == 255 && s.r2 == 255, "right click is L2, left click is R2");
        pad.mouseButton(1, false); pad.key(0x39, false, false);
        s = sample(pad, tick);
        check(s.r2 == 0 && s.l2 == 255 && !(s.buttons & ControllerState::Cross), "a release is a release");
        pad.key(0x1d, false, true); pad.key(0x2a, false, true); pad.key(0x38, false, true); pad.key(0x2c, false, true);
        check(sample(pad, tick).buttons == (ControllerState::Up | ControllerState::R3), "the release chord Ctrl+Alt+Shift+Z presses nothing");
        pad.reset();
        s = sample(pad, tick);
        check(s.buttons == 0 && s.l2 == 0 && s.inputActive, "reset releases everything");
    }
    {   // Mouse: direction and speed become the right stick, and it stops when the mouse does.
        KeyboardMousePad pad; int tick = 0;
        check(sample(pad, tick).rightX == 0, "a mouse at rest leaves the stick centred");
        pad.mouseMove(2, 0);
        auto s = sample(pad, tick);
        const int slow = s.rightX;
        check(slow > int(32767 * KeyboardMousePad::kDeadzone) && slow < 16000 && s.rightY == 0, "a slow movement starts past the deadzone");
        for (int i = 0; i < 4; ++i) { pad.mouseMove(16, 0); s = sample(pad, tick); }
        check(s.rightX > slow && s.rightX < 32767, "a faster movement deflects more");
        for (int i = 0; i < 4; ++i) { pad.mouseMove(-4000, 4000); s = sample(pad, tick); }
        check(s.rightX < -23000 && s.rightY > 23000 && s.rightX * double(s.rightX) + s.rightY * double(s.rightY) <= 32767.0 * 32767.0,
              "a flick saturates on the circle, up-left stays up-left");
        s = sample(pad, tick);
        check(s.rightX != 0, "one empty sample is not a stop (slow mouse report rate)");
        s = sample(pad, tick);
        check(s.rightX == 0 && s.rightY == 0, "two empty samples are");
        KeyboardMousePad fast; fast.setSensitivity(2.0); int t2 = 0;
        fast.mouseMove(2, 0);
        check(sample(fast, t2).rightX > slow, "a higher sensitivity deflects more for the same movement");
    }
    {   // A real pad keeps what the keyboard and mouse do not touch.
        KeyboardMousePad pad; int tick = 0;
        ControllerState base; base.buttons = ControllerState::Square; base.leftX = 1000; base.rightY = -2000; base.l2 = 90;
        auto s = sample(pad, tick, base);
        check(s.buttons == ControllerState::Square && s.leftX == 1000 && s.rightY == -2000 && s.l2 == 90, "an idle keyboard and mouse leave the pad as it is");
        pad.key(0x1f, false, true); pad.key(0x13, false, false);
        s = sample(pad, tick, base);
        check(s.leftX == 0 && s.leftY == 32767 && (s.buttons & ControllerState::Square), "keys take the left stick; the pad's own button stays down");
    }
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
