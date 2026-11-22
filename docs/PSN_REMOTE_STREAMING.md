# PSN remote streaming in this fork

This fork adds a PSN route to Veyra's existing Remote Play source. The decoded
video, audio, controller input, HDR and enhancement pipeline keep using the
existing Veyra implementation. The Sony password is entered in the system
browser, not in Veyra.

## Connect

1. Open the PlayStation streaming dialog (the Home entry is named PS5 in the
   upstream interface) and choose **PSN 远程**.
2. Click **登录 PSN** and sign into the same PSN account used on the console.
3. Copy the complete final `https://remoteplay.dl.playstation.net/remoteplay/redirect?...`
   URL from the browser into **登录结果网址**, then submit it. The app validates
   the exact callback origin, one-time state and ten-minute login window.
4. Select a listed PS5, or **主 PS4**, choose video settings and click **连接**.
   The PSN handshake registers the session remotely; a previous LAN pairing
   and the eight-digit LAN pairing code are not required for this route.
5. To disconnect, reopen this dialog and click **断开**, or use Veyra's existing
   Stop action. Cancellation also interrupts in-progress PSN setup.

The console must already have Remote Play enabled and use the signed-in account.
Rest-mode access additionally needs its existing network/remote-wake settings.
PS4 connects to the account's primary PS4 and uses H.264. The primary-PS4 item is
an explicit route, not proof that the account owns an available PS4. PS5 entries
come from Sony's device list and must advertise Remote Play. NAT/firewall/service
conditions still determine whether an actual remote session can be established.
No new 1440p/4K console stream or higher source frame rate is requested; enhancement
and frame generation operate on the received video as they did before.

## Storage and reconnection

`%LOCALAPPDATA%/Veyra/remoteplay/psn-account.dat` contains the account ID, access
and refresh tokens encrypted with Windows DPAPI for the current Windows user.
No token is exposed as a QML property, logged, saved in settings.ini, or placed
on a command line. Token refresh is bounded and rotated tokens are saved atomically.
A failed account-switch save does not silently accept a different in-memory
account while leaving the old account on disk.

The nonsecret connection mode, selected console and PSN video preferences are
saved together in the existing atomic UI last-source record; a save failure stops
the new connection. The separate Remote Play settings.ini holds dialog defaults
only. Resuming uses the recorded transport and console, even if the dialog has
since changed. Missing or invalid resume information opens the dialog for manual
selection instead. After restart, refresh the PS5 list
before selecting a PS5; a failed resume opens the dialog rather than connecting
later without another user action. Signing out removes only the PSN account file,
not existing LAN pairing profiles or an already-running stream.

The saved PSN selection is also bound to the public account ID. Switching
accounts requires selecting the console again, including the primary-PS4 route
whose zero-filled DUID is shared across accounts. For an isolated test or portable
profile, set `VEYRA_REMOTEPLAY_PROFILE_DIR` to an absolute directory; without this
override the existing Windows-user storage location remains in use. `--data-dir`
alone does not redirect Remote Play storage.

## Source and build

Source checkout: `D:/Projects/Veyra-NRVideo`, branch `feat/psn-remote-streaming`.
Base Veyra commit: `fe8df5328a62849eb2d79053564b9574ab68aecd`.
Chiaki remains pinned to `0e16950165f06e5c3291537c2eeba6e852be7120` with the
patches listed in `scripts/remoteplay/dependency-lock.json`. RUDP is enabled.
The additional patches bound/cancel PSN setup, join an unestablished WebSocket
thread, explicitly transfer socket ownership and balance notification locks.
STUN socket waits are cancelable; a synchronous OS DNS lookup must return before
its calling thread can observe cancellation. `verify-chiaki-stage.py` compares
the complete staged source against the exact pin plus patches.

A native-only service/test build does not require the proprietary video SDKs:

```powershell
cmake -S services/remoteplay-probe -B <native-build> -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DVEYRA_RP_BUILD_NATIVE=ON `
  -DVEYRA_RP_BUILD_PSN_TESTS=ON `
  -DVEYRA_RP_CHIAKI_SOURCE_DIR=<patched-stage> `
  -DVEYRA_RP_CHIAKI_VERIFY_DIR=<clean-pinned-checkout> `
  -DCMAKE_PREFIX_PATH=<matching-compiler-dependencies-and-Qt>
cmake --build <native-build> --parallel 2
ctest --test-dir <native-build> --output-on-failure --timeout 60
```

Full Veyra additionally needs its original MSVC/Windows SDK, Qt, FFmpeg,
NVIDIA and other enabled backend dependencies. `VEYRA_DXC_EXECUTABLE` can select
DXC from a portable Windows SDK. `VEYRA_VIRTUALCAM_X86_PAYLOAD` can select the
32-bit camera DLL independently rebuilt from the same source with a portable
x86 compiler; the default still builds that payload with Visual Studio.
Do not mix MinGW and MSVC libraries, replace
patched FFmpeg with an unrelated binary, or disable the enhancement graph and
present that as a complete Veyra build.

## Local Windows build (2026-11-21)

The full `veyra_qml_ui.exe` compiled with portable MSVC 14.44, Windows SDK
10.0.26100 and Qt 6.8.3. The local package is
`D:/Projects/Veyra-NRVideo-work/package/Veyra-2.2.0-PSN`.
Its 91 shaders and x86/x64 camera payloads were rebuilt and checked against
the staged files. Existing, unmodified runtime DLLs came from the user's
installed Veyra 2.2.0; Qt dependencies were deployed from the matching SDK.

This PSN build enables DLSS NR/SR/FG, NVIDIA Optical Flow, the shared enhancement
graph, FSR/XeSS/AMF interfaces, deinterlacing and libdovi. Moonlight and Xbox
remain at their default disabled build options. The optional AMD Optical Flow
static backend and libass are not configured; the existing styled subtitle
renderer remains available. These build choices do not remove their source.

The machine-specific configuration and build scripts are retained under
`D:/Projects/Veyra-NRVideo-work/scripts`, with the exact CMake options in
`app-build-configuration.json`. This portable CMake distribution requires
`CMAKE_NINJA_CMCLDEPS_RC=OFF` because it does not include `cmcldeps.exe`.
The final C++ build retains `/EHsc` exception unwinding.

## Validation record (2026-11-21)

- Qt Quick dialog regression: 7 passed, 0 failed, isolated configuration and
  offscreen software rendering, repeated with the final Qt 6.8.3/MSVC runtime.
- Windows UCRT64 native Chiaki plus Qt PSN service compiled. 85 CTest cases
  passed, including 80 offline-core cases, native initialization, encrypted
  profile storage, synthetic HTTP OAuth/refresh tests, encrypted loopback
  feedback, WebSocket cleanup, STUN cancellation, socket ownership and
  notification-lock/wait regressions. Account-bound resume and private-directory
  override changes were subsequently rebuilt and their affected tests passed.
- The supplemental GCC build uses local `-Wno-error` options for existing
  indentation, aggregate-initializer, conversion and upstream-header shadow
  warnings; these options are not changes to the source's test policy.
- Full MSVC application compiled successfully. Its PSN authentication tests
  passed 81 assertions; DPAPI/profile, encrypted feedback and product-boundary
  tests also passed. The actual FFmpeg H.264/HEVC/HDR10 source test passed
  471 checks using synthetic media fixtures.
- Two full-application startup checks passed: a fresh profile selects PSN;
  an explicit saved LAN preference selects local mode. Both loaded the real
  bridge, PSN service and dialog in a hidden, isolated application instance.
  Test-only QML assertions were removed and original bytes verified afterward.
- Real PSN login, real PS4/PS5 first remote connection, NAT traversal and GPU
  enhancement on a live stream remain untested. No login, console discovery,
  wake or connection operation was triggered by these offline tests.

Development artifacts and logs are isolated under
`D:/Projects/Veyra-NRVideo-work`; no existing user application, pairing file,
console session, driver or installed runtime was modified for the tests.

## PS5 connection failure follow-up

The user's original-package log recorded `psn_session_start failed code=8` at
16:08:51 and 16:12:13 on 2026-11-21 (Asia/Shanghai). At this call site the pinned
library maps a missing console-member/custom-data notification to `HOST_DOWN`;
this does not establish that the console is powered off. The user confirmed the
PS5 was on and that the previous Chiaki-ng build could connect to it. Session
creation had succeeded; control-port traversal, registration and video had not
started. An audit found matching OAuth, device-ID mapping and start requests.

Patch 0009 backports the upstream notification ownership/session-deletion fix
from Chiaki-ng `1625cd640146fd25f2cce65eea3a33df79748757`, and makes a closed
notification channel report a network failure promptly instead of waiting and
misreporting it as an unresponsive console. The old logs cannot prove these
defects caused the observed failures.

New `remoteplay-psn` summaries contain only four phase durations, bounded numeric
HTTP/status/event fields and observed progress flags. Event IDs 1–10 mean:
create HTTP accepted, start HTTP accepted, console joined, custom data received,
WebSocket peer closed, WebSocket transport error, current session deleted,
other session deletion ignored, notification deadline, WebSocket finished.
The summary's event field is a bitset (bit 0 is event 1); bit 10 separately marks
the fixed upstream PONG-timeout error. No raw upstream strings, tokens, JSON,
console/account/session IDs or addresses are retained in these summaries.

The follow-up package is staged separately as `Veyra-2.2.0-PSN-fix1`. Its user
launcher retains the original Windows Remote Play account store and existing UI
preferences, and sends logs to
`D:/Projects/Veyra-NRVideo-work/diagnostics/psn-start-failure/fix1-user.log`.
Close the old Veyra window and disconnect other Remote Play clients before
retrying. Only a real retry can establish whether the connection problem is
resolved; the offline regressions cover the identified code defects.

Follow-up verification: exact nine-patch stage verification passed; 84 selected
UCRT native regressions passed, including the numeric diagnostic parser and
eight additional notification-lifecycle cases. The MSVC application and five
offline test groups passed, followed by both isolated full-application startup
checks. The final diagnostic summary is formatted only for startup results,
not for every controller update. Live retry with this package is still pending.

## Fix2: WebSocket handshake timeout retained after connection

The user's fix1 retry failed twice (2026-11-21 22:02:42 and 22:03:25,
Asia/Shanghai). Both summaries reported `code=6 events=547 state=55
last_http=202 ws_finished=1 ws_pong_timeout=0`, with no console-member or
custom-data notification. Sony accepted the start request, then the WebSocket
transport failed. These were not peer-close or session-deletion events.

Patch 0005 had changed the WebSocket's total CURL timeout from unlimited to ten
seconds to bound its HTTP upgrade, but did not clear it after the upgrade.
The pinned curl's Schannel sender checks `Curl_timeleft` on later writes as
well, including WebSocket heartbeats. Thus an established channel could fail
once this handshake deadline expired. The working Chiaki build uses Schannel
too, but retains the upstream unlimited WebSocket lifetime.

Patch 0010 clears only this total timeout immediately after a successful
upgrade, before publishing `ws_open`. The ten-second handshake limit, request
cancellation, heartbeat checks and other HTTP deadlines are retained. It also
adds strictly parsed numeric `ws_op`/`ws_curl` failure fields: operations 1–6
are handshake, active socket, PING send, receive, PONG send, and deadline reset.
No raw CURL error buffers, URLs or server data enter the application log.

A loopback WSS regression linked the exact product's static curl 8.11.0-DEV /
Schannel library. Both cases successfully sent and received before waiting;
after 11 seconds the old case returned CURL 55 with a Schannel send-timeout
error, while clearing the timeout sent and received successfully. The local
server received one versus two text frames, respectively. This confirms the
introduced transport defect; fix1 did not capture the underlying CURL code,
so the user's live connection still requires a retry to verify resolution.
The test uses only 127.0.0.1 and an ephemeral self-signed certificate with
verification disabled only in its test client, without changing trust stores.

Verification: exact ten-patch stage verification, MSVC product build, 84 UCRT
regressions, five MSVC offline test groups, and both isolated full-application
startup checks passed. The standalone WSS reproducer and receipts are under
`D:/Projects/Veyra-NRVideo-work/scripts/tests/ws-timeout`; the remaining logs
are `logs/fix2-*`, `fix2-test-results.json`, and
`tests/psn-fix2-smoke/20261121-221713-59d9e01b/result.json` in the same work root.
The separate package is `package/Veyra-2.2.0-PSN-fix2`. Its launcher preserves
the user's existing account/UI preferences and records new attempts in
`diagnostics/psn-start-failure/fix2-user.log`. No real-console connection was
initiated by these tests, and the running fix1 package was not replaced.

## Fix3: Connection-time PIN prompt and explicit stream exit

The user confirmed that fix2 connects and that entering the PIN in the same
application page works. The requested change is now interaction design:
remove manual PIN submission from settings, ask automatically when the console
requires it, and provide an obvious stream exit with an optional standby choice.

`Ps5LoginPinDialog` opens only for a live native PIN challenge. It focuses a
masked four-digit input, submits to the current connection without reconnecting,
displays rejection and permits another attempt, and closes when authentication
advances or the connection ends. Cancel stops that pending connection. Both
streaming and capture-card controller sessions use the prompt; the old PIN rows
are removed. An opaque key combines connection identity, attempt clock origin
and challenge number so an old dialog cannot submit to or cancel a newer run.
Canceling a capture-control login suspends that attempt without changing saved
settings. Its Reconnect button can explicitly start again.

The playback controls include an End stream button (see fix4 below for its
placement). It and the existing Disconnect action open a choice between
disconnecting while leaving the console on and requesting standby then
disconnecting. No standby operation happens merely by opening/canceling this
dialog, on errors, automatic retry, normal stop or destruction. The standby
choice is available only after the native session is connected.

Patch 0011 reports completion of the native control-message send, using fixed
payload-free markers. The source owner waits up to three seconds for the send
and briefly allows remote disconnect/retransmission before releasing transport.
A failed send keeps a live connection available for plain disconnect; an
unconfirmed timeout does not enqueue duplicate standby commands. A sent marker
means transport submission, not confirmation that the console has entered rest
mode. User-requested disconnect suppresses automatic reconnect, including when
the stream is paused or a transport retry is starting.

PIN submission now reports queue acceptance and distinct native progress.
Only the exact native login-success message claims console acceptance; entering
the data handshake alone does not. Leaving PIN entry refreshes the remaining
handshake's startup budget. `remoteplay-pin` logs contain only boolean/numeric
queue, status, stage and error fields, never PIN text. Status: None=0, Required=1,
Submitting=2, Sent=3, Incorrect=4, Accepted=5, SendFailed=6. Stage: request=1,
local submission=2, session forwarding=3, control send attempt=4, console
acceptance=5, console rejection=6. Sent is local API acceptance only.

Verification: exact eleven-patch verification, MSVC product build, 84 existing
UCRT checks plus six PIN state/ordering cases, five MSVC offline test groups
(including source queue, stale challenge and standby/disconnect boundaries),
25 Qt Quick checks and two isolated actual-application startup checks passed.
The real-app checks reject stale PIN submit/cancel and idle standby requests
without network actions. The two dialog renderings were inspected using Windows
fonts in the offscreen Qt test. A screenshot-harness attempt initially treated
Qt's void image-save result as a boolean; that harness error was corrected and
the failed log retained. No product failure was hidden by the correction.

Receipts under D:/Projects/Veyra-NRVideo-work: fix3-test-results.json,
fix3-pin-test-results.json, fix3-qml-result.json, and
tests/psn-fix3-smoke/20261121-230620-2c4fb7fd/result.json. The six new core tests
were also run explicitly because the existing CTest cache predates those names.
The package is package/Veyra-2.2.0-PSN-fix3. Its launcher preserves existing
login/preferences and logs to diagnostics/psn-start-failure/fix3-user.log.
Live use of the new automatic prompt and actual console standby remain for the
user to verify; these tests did not connect to or send power commands to a real
console. Existing running applications were not stopped or replaced.

## 2026-11-22 fix4: integrate End stream into existing controls

Removed the standalone top-right button. The existing cinema/fullscreen pill,
professional playback row and node playback row now show a small disconnect
icon beside play/pause, with an End stream tooltip. Cinema and node modes reuse
an existing seek slot while a console session is active; ordinary playback
retains its seek controls. The button stays visible but disabled while ending.
All three entries open the existing disconnect/standby choice dialog. PIN and
native transport/power behavior are unchanged.

The same build also corrects an omitted CUDA driver header configuration from
the earlier PSN packages. Their VFG DLLs were present, but an empty
`VEYRA_CUDA_DRIVER_INCLUDE_DIR` compiled both native NVIDIA VFG and Video Effects
SR as unavailable. DLSS FG and NVOF being enabled did not enable these backends.
The build now sets this CMake path to the existing Optical Flow SDK's
`NvOFFRUC/NvOFFRUCSample/cudart_header` directory (CUDA header version 11.4),
outside the source tree. No CUDA Toolkit installation or driver change is needed.
The seven files in `runtime/nvidia-vfg` are unchanged from the original local
NVIDIA runtime package. The actual build configuration is recorded in
`D:/Projects/Veyra-NRVideo-work/app-build-configuration.json`.

Verification: MSVC rebuild passed; 40 Qt Quick checks cover PIN/exit choices,
windowed/fullscreen stop routing, ending state and restored seeking. Initial UI
test failures came from clicking before the inner layout settled; the fixture
now settles after state changes and targets button centers. Failed logs are
retained as `logs/fix4-qml-first-run.log` and `fix4-qml-layout-attempt.log`.
The standalone control-bar rendering was also inspected.

On this machine's RTX 3070 Ti, the product VFG GPU test passed 2,425 assertions
at 640x360 with asynchronous submission, both tested pixel formats, six quality
modes, 2X–8X and scene changes. The VFG settings test passed 683 assertions.
This is synthetic interpolation validation, not a PS5/game-quality or sustained
1080p performance measurement; RTX 30 remains the project's experimental path.
Two isolated full-application checks also verified VFG is available and enabled
in its menu, Video Effects SR availability, and the real page-to-exit-dialog
routing. Receipts: `fix4-qml-result.json`, `fix4-vfg-result.json`, and
`tests/psn-fix4-smoke/20261122-102044-031f7db2/result.json` under the work root.
The independent package is `package/Veyra-2.2.0-PSN-fix4`; the user launcher
keeps existing login/preferences and logs to
`diagnostics/psn-start-failure/fix4-user.log`. No real console connection or
power operation was performed by these tests.

## 2026-11-22 fix5: home copy and startup music setting

The home card now reads "PS5 / PS4 串流" with "PSN 远程 / 局域网" as its
subtitle, including when a local host was previously saved. Moved the existing
saved startup-music switch from Appearance to the top of Startup and Window
(设置 → 启动与窗口), renamed it "启动时播放音乐", and explained its persistent
effect. The corner button still stops only the current launch; its tooltip now
points to the permanent setting. The existing `introMusic` preference, default
and native playback behavior are preserved.

MSVC product build and three isolated actual-application launches passed:
disable through the real UI handler and stop the native music state, restart
with the preference still off, re-enable and restart with it on. The silent
portable-update probe never plays audio, logs in or connects to a console.
Checks also confirm both home labels fit at 1280x900 and VFG remains available.
Receipt: `D:/Projects/Veyra-NRVideo-work/fix5-ui-result.json`. Independent package:
`package/Veyra-2.2.0-PSN-fix5`; the launcher retains existing user data and no
longer forces the PSN dialog open on every launch.
