# Screen capture implementation, 2026-09-19

User approved a professional-mode screen input alongside the existing sources.
Branch: `codex/screen-capture-20260919`, base `12238df`. Existing uncommitted
P010 HDR fixes and XeSS pan regressions are retained, not reverted.

## Contract

- Window and monitor WGC capture; monitor DXGI duplication compatibility mode.
- Same EngineController / EnhanceGraph / presenter as other live inputs.
- Bounded GPU texture pool, latest available frame, real QPC timestamps.
- Shared D3D11/D3D12 textures and producer fence; AVFrame leases prevent reuse
  until graph consumers and cached frames have released the source texture.
- Explicit SDR sRGB and HDR scRGB contracts, no GPU pixel readback in playback.
- Target selection, preview/start/switch/stop, cursor, rate, crop and display fit.
- Video only. The source application continues playing its own audio; no
  loopback capture, audio monitoring controls or extra audio playback.
- Closed/minimized targets, source resize, display changes and errors surfaced.
- No game injection, input forwarding or claims of exclusive-fullscreen support.

## Upstream

`robmikh/Win32CaptureSample`, MIT, commit
`49fefe79fd9b11025f0b5eb91783a98888516070`.
Adapt SimpleCapture's free-threaded WGC pool, session lifecycle and resize
pattern; replace its swapchain with Veyra's leased GPU ingress. Keep attribution
in source and THIRD_PARTY_NOTICES.md.

## Verification / Current State

Local implementation and targeted verification completed; not merged or released.
The sidebar uses Lucide `app-window`, distinct from the capture-card `monitor`.
Audio controls and the provisional loopback implementation were removed at the
user's request. Original application audio is neither captured nor delayed;
enhancement can therefore make the displayed picture lag behind that audio.

Build: `scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/screen-capture-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/screen-capture-20260919 -DisplayVersion 1.4.2beta -Targets veyra_screen_capture_tests,veyra`.
Latest `build9.log` linked both targets successfully. `build8.log` also built
the HDR color, capture color contract and existing WASAPI test targets.

Tests used `scripts/run-short-test.ps1` with explicit LogPrefix and task-local
TEMP/TMP. `veyra_screen_capture_tests.exe --engine` used a 120-second timeout;
`--base` used 60 seconds. Logs are under the artifact log directory below.

- `screen-test3.stdout.log`: failures=0; actual RGB stripe pixels via WGC window,
  WGC monitor and DXGI monitor; monotonic timestamps, bounded pool leases,
  minimize/restore, resize, crop, close/reopen and engine stop/drain.
  Four-second producer sample: 138 delivered, 139 received, one dropped.
- RTX 5070 NR + DLSS 4X: Feature 18 Create `0x1`, SEH=0; 652 source frames,
  1956 FG evaluations, 1665 generated presentations, 287 generated frames expired
  after evaluation; slotWaitCount=0. This proves actual execution, not perfect
  pacing, physical refresh rate or absence of drops.
- `screen-test4.stdout.log`: latest source, failures=0; additionally checked
  monitor capture excludes existing own windows and stop restores their original
  display affinity for both WGC and DXGI.
- `hdr-test.log`: four SCREEN_SCRGB cases passed (native/tone-mapped output,
  exposure 0/1), alongside existing HDR GPU cases. Physical HDR not tested.
- `veyra_wasapi_input_tests.exe --offline`: 18 checks, zero failures
  (`wasapi-offline.stdout.log`); `veyra_capture_color_tests.exe`:
  failures=0 (`capture-color.stdout.log`). No new screen audio code remains.
- GUI preview observed with readable controls, distinct icon and no audio row.
  Interactive crop/start/stop were not independently exercised through UI;
  integration tests exercise the underlying capture and engine lifecycle.

Earlier failures: mixed int/LONG compilation corrected; an initial test wrongly
required 60 FPS from a slower GDI producer, and an immediate resize assertion
did not allow the queued old frame to drain. Both assertions now test the actual
contract. The removed loopback experiment had a timestamp failure; it is not
part of this implementation. Initial GUI launch lacked FFmpeg DLL lookup;
existing approved portable root DLLs were staged beside the build EXE.

Known limits: no game injection, exclusive-fullscreen guarantee or input
forwarding; physical HDR, protected content, multi-GPU and real games untested.
DXGI has no cursor and rejects rotated displays. Own windows created after a
monitor session starts are not automatically excluded. Full release/file/export
regressions were not run. Existing teardown logs include an unsubmitted-command
list discard HRESULT `0x80004005`; no content test failed, but it is not being
claimed as fixed. Next validation is user testing with their actual target app.

Artifacts: `E:/项目/Veyra/{build,tests,logs,tmp,deps}/screen-capture-20260919/`.
Upstream checkout: `E:/项目/Veyra/deps/screen-capture-20260919/Win32CaptureSample`.

## User feedback: static 0 FPS / moving 50 FPS

### Follow display refresh (default)

The rate selector now defaults to following the captured target's display.
Window capture uses MonitorFromWindow; monitor capture uses the selected monitor.
DisplayConfig preserves fractional refresh rates, with EnumDisplaySettings as
fallback. A failed initial query uses 60 FPS with a warning; transient failures
retain the last known rate. The existing one-second display check updates the
limit and flags a temporal discontinuity when the rate changes. Manual
30/60/120/144/240 limits remain. Preview now obeys this setting too.

UI preference `ScreenCapture/RateMode` starts at automatic (0); the previous
experimental `Fps` index is not reused because its index meaning has changed.
Subsequent explicit selections persist. This is still a ceiling, not constant
output resampling or a claim that capture runs at the monitor's refresh rate.

`build10.log`: application and capture tests built successfully with the command
above. `screen-test5.stdout.log` (`--base`, timeout 60 seconds): failures=0;
automatic default/URI, target monitor rate and first-frame duration passed.
Actual local rate was 100/1 Hz, independently compared with display settings.
WGC window, WGC monitor and DXGI monitor all produced verified RGB pixels in
automatic mode. Manual 60 FPS regression delivered 131 frames in four seconds,
131 received and zero dropped, reflecting this test producer's cadence.
Physical fractional modes, moving between different-refresh displays and live
display-mode changes have not been exercised. NR/FG were not rerun this revision.
GUI screenshot and accessibility tree confirmed the automatic default is selected
and its full label fits beside the cursor toggle. The test GUI remains open.

The current option is a rate ceiling, not fixed-rate resampling. WGC/DXGI
deliver changed frames; no frame means Waiting, and no cached frame is emitted
with a fabricated capture timestamp. The user requested clarification about
fixed 30/60 output. No fixed-rate mode has been implemented in this revision.

Read-only verification of `gui.log` found 300 delivered / 300 received over
six seconds (04:20:16.353 to 04:20:22.353 UTC), with zero drops. Later intervals
also show 300 frames in six seconds and an unchanged dropped counter. In this
sample, WGC delivery itself is 50 FPS, not a 60 FPS source reduced to 50 by our
limiter. Win32_VideoController reports the RTX 5070 display at 2560x1440, 100 Hz.
Half-refresh content is plausible, but neither browser video cadence nor DWM
behavior was independently measured; the 100 Hz value alone does not prove it.

Fixed output would require a separate output clock and reuse of the latest
image when no new one arrives. Repeats must be counted separately from new
capture and AI-generated frames, retaining original capture timestamps. It
would stabilize presentation submissions, not invent missing motion or ensure
60 physical refreshes on a 100 Hz screen. Uniform 60 Hz physical cadence would
also require a suitable display refresh/VRR configuration.
