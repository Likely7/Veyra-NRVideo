# Corrective audit, 2026-09-21

This record supersedes completion claims in the six-issue plan where they
conflict with the evidence below. This is not whole-product acceptance.

## Confirmed defects and corrections

- The new output cadence comparison used an initial host anchor on unpaced
  capture. An earlier capture log recorded 202 processed frames but only two
  presentations. Unpaced cadence now uses current host time; the cap comparison
  can recover from late deadlines. Regression tests cover 30/60 Hz, caps and reset.
- Removed the speculative two-second transaction first-sample timeout. Existing
  source timeout handling remains; a short timeout is not a starvation fix.
- Settings UI used inconsistent absolute offsets after adding controls. Enhancement
  controls now use sequential layout, collapsed combo heights and responsive rows.
  Restored body compositing/immediate invalidation. Static 1280x800 scroll views
  were inspected; fast motion, multiple monitors and all DPI settings are not
  visually accepted by this test.
- Restored NR enhancement terminology and numeric model styles 0/1/2. Named
  presets remain. Presets are parameter collections, not new model styles.
- Reverted the recent XeSS frameRenderTime hint derived from accepted Present
  intervals: these include provider waits and are not independent rendering
  time. Intel permits zero. This rollback alone did not fix file playback.
- Video testing found repeated XeSS generation suppression/resumption under the
  audio-master lateness gate. Removed that automatic generation gate from file
  presentation, consistent with the user's no-automatic-downgrade requirement.
  Audio continuity and bounded preview skipping remain. No fixed delay added.
- Included five temporal RGBA16F textures in graph memory estimates (40 bytes
  per residual pixel). Previous estimates omitted roughly 316 MiB at 4K.
- Fixed short-test runner: Start-Process must omit an empty ArgumentList.

## Scope corrections

- The readable 033 CC0 BilateralComic shader applies luminance/chroma shaping
  and skin protection; it is not evidence of additional NGX model style IDs.
  The binary engine supplies no auditable source for adding styles 3-6. Do not
  invent IDs or relabel existing models as new ones.
- Magpie reference commit: 3841698348bfb246623d4acf791984c8b68a577b.
  Its NR style choices are also 0/1/2. Residual temporal stabilization is not
  equivalent to generic input-video denoising. Veyra temporal NR remains optional
  and off by default; GPU quality and export regression acceptance are pending.
- Output cap is default off, but VideoPresenter still disables it for XeSS/FSR.
  The requested all-provider behavior is NOT complete. A 60 Hz display cannot
  show 120 complete distinct frames per second.
- Multiline subtitle fitting defaults off; total-screen overflow protection
  remains. Bitmap subtitles retain authored geometry. Fullscreen lock prevents
  mouse-triggered controls; it does not prove HDR overlay color issues solved.
- Capture driver discontinuity filtering and Elgato HDR changes were reviewed;
  they were not broadly reverted. Real affected-device acceptance remains needed.

## Evidence

Artifacts: E:/项目/Veyra/tests/corrective-audit-20260921/.
Build: E:/项目/Veyra/build/playback-nr-20260920/.
Process TEMP/TMP: E:/项目/Veyra/tmp/corrective-audit-20260921/.

VS x64 CMake build targets veyra, live timing, UI contract, control paint,
capture color, repair preset, subtitle panel, subtitle overlay and bitmap
subtitle tests passed. UI contract was rerun with an isolated directory to
actually exercise persistence. Preset and bitmap tests used required arguments.
Initial empty-argument runner failures were corrected, not counted as passes.

Capture baseline, 15 seconds: 415 received/processed/presented, zero expiry,
29-30 submissions/s. Complete-runtime NR + XeSS4x capture, 20 seconds:
988 NR evaluations, 2952 generated frames, 3939 provider presentations.
The PS5 was shutting down/static: this is not gameplay image-quality acceptance.
An earlier test from the build directory lacked the runtime and recovered to
ordinary presentation; it is explicitly NOT a XeSS pass.

Per user steering, subsequent tests use
E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4, not the powered-off PS5.
video-xess.log: 120-second NR + XeSS4x test, 7031 NR evaluations, 11046 generated,
18075 provider presentations; repeated suppress/resume explains large rate
variation. No claim of stable XeSS playback follows from that run.

Runtime staging app/ reuses an existing verified portable runtime with the new
executable. It is a test directory, not a new release package or updated manifest.
No runtime binary was edited or added to source control; no publication occurred.

## No-gate video comparison

Rebuilt veyra successfully after removing the file generation gate, then ran
the same p001.mp4 with --smoke-seconds 60 --smoke-view pro --nr --no-sr
--fg-xess --fg-multiplier 4 through scripts/run-short-test.ps1 (90-second
watchdog). Exit 0; log video-xess-no-gate.log. 3428 NR evaluations, 10221
generated frames, 13648 provider presentations. Final source processing was
60 fps and provider submission rate 240 fps. No gate toggles remained.
The final timing windows retained 17 preview skips; absolute media-clock
lateness P95 was 43.79 ms, versus 29.00 ms in the earlier gated run.
This is NOT input-to-photon latency. The runs differ in duration and UI
interaction, so neither establishes a controlled latency improvement.
Steady 240 submissions at the end do not prove artifact-free motion.

UI contract persistence rerun with the isolated prefs-full directory passed.
git diff --check passed. Test processes exited; no new portable package made.

## Remaining acceptance

Record the no-gate video comparison separately. Check sustained media-clock
lateness and preview skips, not only provider FPS. Temporal NR quality, actual
fast UI scrolling/multi-monitor motion, provider output caps and new NR style
availability remain open. No physical scanout or input-to-photon measurement
was performed. Whole-software stability has not been established.
