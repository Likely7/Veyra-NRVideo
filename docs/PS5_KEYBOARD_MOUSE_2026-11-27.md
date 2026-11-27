# PS5 Remote Play: keyboard and mouse as the controller

Branch `feature/ghoststick-core`, based on main `55ebc7b0`. Off by default.

## What it does

With **键盘鼠标模拟手柄** switched on in the PS5 dialog, a click on the picture of a running PS5 stream hands the
keyboard and mouse to the console as a DualSense. `Ctrl+Alt+Shift+Z` gives them back; so does losing the focus
(Alt+Tab) or the end of the session. A real pad keeps working at the same time.

No virtual pad and no driver: Veyra already sends a `ControllerState` to the console for the real pad, and the
keyboard and mouse are added to that same state. Nothing changes on the path to the PS5.

| Input | Pad |
|---|---|
| W A S D (physical keys: Z Q S D on AZERTY) | left stick |
| mouse movement | right stick |
| left / right / middle click | R2 / L2 / R3 |
| Space, C, R, F | Cross, Circle, Square, Triangle |
| Q, E, X | L1, R1, L3 |
| arrows | D-pad |
| Enter, Backspace, Tab, Home | Options, Create, touchpad click, PS |

No modifier key is mapped, so the release chord presses nothing on the console.

The right stick takes the direction of the mouse movement and a deflection from its speed. It starts at 15 % as
soon as the mouse moves (to get past the game's own deadzone), is fully over at 8 counts/ms at sensitivity 3, and
returns to the centre after two pad samples without movement. **鼠标灵敏度** 1 to 5 scales the speed by
0.5 / 0.75 / 1 / 1.5 / 2.25.

## Where

- `include/veyra/remoteplay/KeyboardMousePad.h`: the translation. Pure state, no Win32, no Qt.
- `MoonlightInputCapture`: the existing raw keyboard and mouse capture gets `setTarget()`, so a second instance
  feeds the PS5 pad instead of the Moonlight host. Its Moonlight behaviour is unchanged when no target is set.
- `src/ui/bridge/RemotePlay.cpp`: the PS5 pad timer adds the keyboard and mouse to the polled state while they are
  captured; `ps5KeyboardMouse(bool)` starts and stops the capture; preferences `ps5KeyboardMouse` and
  `ps5MouseSensitivity`.
- `Ps5SourceDialog.qml` (switch and sensitivity), `Main.qml` (the click on the picture), four catalog strings.

## Limits

- PS5 stream only. Not wired to PS5 手柄控制 (capture card picture), Xbox or PC streaming.
- The capture class is built under `VEYRA_ENABLE_MOONLIGHT`, so the feature needs that option together with
  `VEYRA_ENABLE_REMOTEPLAY` (both are on in product builds).
- Fixed key mapping, one anti-deadzone and a linear curve for every game. No per-game profile, no mouse wheel.
- A stick is a speed and a mouse is a position: aiming through a stick never feels like a native mouse, and the
  Remote Play latency is on top of it.
- Using a mouse in online competitive games may be against their rules. That is the player's responsibility.

## Verification

- `tests/remoteplay/KeyboardMousePadTests.cpp` (new target `veyra_remoteplay_keyboard_mouse_pad_tests`): compiled
  alone with MSVC 14.51 (`cl /std:c++20 /W4 /EHsc /utf-8 /I include`), no warning, 18 checks passed.
- `python -B scripts/i18n/extract.py --check`: 3337 entries, nothing missing, no placeholder problem.
- **Not done:** the application was not built (no Qt, FFmpeg, SDL or Chiaki dependencies on the machine used), so
  the bridge, capture and QML changes are uncompiled and were never run. Nothing was tested against a PS5.

## Credit

Contributed by [3Geeks](https://3geeks.fr), free and just for fun. Built with
[Geeks Code](https://3geeks.fr/realisations/geeks-code), the 3Geeks code editor.
