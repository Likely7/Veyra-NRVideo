# PS5 Remote Play: keyboard and mouse as the controller

Branch `feature/ghoststick-core`, based on main `55ebc7b0`. Off by default.

## What it does

With **键盘鼠标模拟手柄** switched on, a click on the picture hands the keyboard and mouse to the console as a
DualSense. It works in both PS5 modes:

- a PS5 Remote Play stream (the switch is in the PS5 dialog);
- **PS5 手柄控制**: the picture and sound come from a capture card and Remote Play only carries the pad (the switch
  is also in the capture card dialog, once PS5 手柄控制 is on). The click works once that session is connected.

`Ctrl+Alt+Shift+Z` gives the keyboard and mouse back; so does losing the focus (Alt+Tab) or the end of the
session. A real pad keeps working at the same time.

No virtual pad and no driver: Veyra already sends a `ControllerState` to the console for the real pad, and the
keyboard and mouse are added to that same state. Nothing changes on the path to the PS5.

## Keys

Every key can be changed in **按键设置…** (next to 鼠标灵敏度): each pad action lists its keys, `+` waits for a
key, a mouse button or a wheel notch, and clicking a key removes it. One input does one action; binding it again
moves it. **恢复 GTA 6 默认** restores the defaults. Keys are saved by position (scan code), and the menu shows
their names on the PC's own layout.

The defaults follow the GTA VI default control scheme on PS5 (sprint on L3, jump on Cross, shoot R2, aim L2), with
the keys a GTA V PC player expects where they fit:

| Input (QWERTY position) | Pad | GTA VI (default scheme) |
|---|---|---|
| W A S D (Z Q S D on AZERTY) | left stick | move, steer |
| mouse movement | right stick | camera, aim |
| left / right / middle click | R2 / L2 / R3 | shoot / aim / zoom while aiming |
| side buttons 1 / 2 | L1 / R1 | holster and loadout / cover |
| Space | Cross | jump, climb, handbrake |
| Left Shift | L3 | run, horn and nitrous |
| Left Ctrl | Circle | crouch, duck in a car |
| R | Square | melee, shove |
| E, F | Triangle | interact, enter and leave a vehicle |
| Q | R1 | cover, quick throw, dive, shoot from a vehicle |
| Tab | L1 | holster (tap), loadout (hold) |
| C | R3 | look behind |
| B | L3 + R3 | Focus |
| arrows | D-pad | phone, objective and minimap, radio and vehicle menu, partner menu |
| Esc, M, Backspace, Home | Options, touchpad, Create, PS | pause, map, share, PS |
| X | (vehicle layer) | on foot / in a vehicle |

Holds, taps and double taps go through as they are (hold Tab for the loadout, double-tap E to jump out of a
moving car). Combinations work as on the pad (hold the right arrow and press Q to switch character).

**Vehicle layer.** In a GTA VI vehicle R2 accelerates and L2 brakes, which a left stick cannot do. X switches to
the vehicle layer: the forward key becomes R2, the back key L2, the shoot button (R2) becomes R1 (shoot from the
vehicle), the side keys still steer. X again is on foot. The screen says which layer is on. The game state is
not read, so the layer does not follow the player into a car by itself.

**Release chord.** While Ctrl and Alt are both held (the start of `Ctrl+Alt+Shift+Z`) the modifier keys press
nothing, so releasing the capture does not crouch or run in the game.

The right stick takes the direction of the mouse movement and a deflection from its speed. It starts at 15 % as
soon as the mouse moves (to get past the game's own deadzone), is fully over at 8 counts/ms at sensitivity 3, and
returns to the centre after two pad samples without movement. **鼠标灵敏度** 1 to 5 scales the speed by
0.5 / 0.75 / 1 / 1.5 / 2.25.

Sources for the GTA VI mapping (they disagree on a few details, e.g. tap or hold on the D-pad while aiming, so
check the game's own control screen): [GTABase](https://www.gtabase.com/articles/gta-6/gta-6-controls-guide-ps5-xbox-button-layout),
[Shacknews](https://www.shacknews.com/article/151396/gta6-ps5-controls), [allthings.how](https://allthings.how/gta-6-controls/).

## Where

- `include/veyra/remoteplay/KeyboardMousePad.h`: the translation, the actions, the GTA VI defaults, the vehicle
  layer and the saved form of the bindings. Pure state, no Win32, no Qt.
- `MoonlightInputCapture`: the existing raw keyboard and mouse capture gets `setTarget()`, so a second instance
  feeds the PS5 pad instead of the Moonlight host. Its Moonlight behaviour is unchanged when no target is set.
- `src/ui/bridge/RemotePlay.cpp`: both PS5 pad timers (stream and PS5 手柄控制) add the keyboard and mouse to the
  polled state while they are captured; `ps5KeyboardMouse(bool)` starts and stops the capture; `ps5Set`
  `bindKey` / `resetKeys` edit the bindings; preferences `ps5KeyboardMouse`, `ps5MouseSensitivity`,
  `ps5KeyBindings` (`code:action,…`, hexadecimal codes).
- `Ps5KeysDialog.qml` (the key menu), `Ps5SourceDialog.qml` and `CaptureSourceDialog.qml` (switch, sensitivity,
  key menu), `Main.qml` (the click on the picture), catalog strings in all nine languages.

## Limits

- Not wired to Xbox or PC streaming.
- The capture class is built under `VEYRA_ENABLE_MOONLIGHT`, so the feature needs that option together with
  `VEYRA_ENABLE_REMOTEPLAY` (both are on in product builds; `package-qml-release.py` requires them).
- One anti-deadzone and a linear curve for every game, no per-game profile, no automatic vehicle detection.
- A stick is a speed and a mouse is a position: aiming through a stick never feels like a native mouse, and the
  Remote Play latency is on top of it.
- Using a mouse in online competitive games may be against their rules. That is the player's responsibility.

## Verification

- `tests/remoteplay/KeyboardMousePadTests.cpp` (target `veyra_remoteplay_keyboard_mouse_pad_tests`): compiled alone
  with MSVC 14.51 (`cl /std:c++20 /W4 /WX /EHsc /utf-8 /I include`), 37 checks passed (keys, mouse, GTA VI
  defaults, release chord, vehicle layer, rebinding, wheel, saved bindings).
- `python -B scripts/i18n/extract.py --check`: 3380 entries, nothing missing, no placeholder problem.
- **Not done:** the application was not built (no Qt, FFmpeg, SDL or Chiaki dependencies on the machine used), so
  the bridge, capture and QML changes are uncompiled and were never run. Nothing was tested against a PS5 or in
  GTA VI. The key menu reads the scan code from Qt's `nativeScanCode`, which needs a check on a real build.

## Credit

Contributed by [3Geeks](https://3geeks.fr), free and just for fun. Built with
[Geeks Code](https://3geeks.fr/realisations/geeks-code), the 3Geeks code editor.
