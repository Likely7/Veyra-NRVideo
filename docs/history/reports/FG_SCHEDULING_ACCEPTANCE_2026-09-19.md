# DLSS / XeSS scheduling repair, 2026-09-19

## Scope and evidence

User requested both DLSS and XeSS investigation, including sustained 4X/6X admission failures, irregular work, and GC573 RGB input cadence. Checkpoint: `5a1931b`; branch: `codex/fg-scheduling-repair-20260919`. No publication or runtime replacement.

Local hardware: RTX 5070, SDR 2560x1440 at 100 Hz. Window capture tests use an independently animated child process through the real Windows capture and engine paths, configured at 30 fps. They are not GC573, PS5, 4K NR, or physical scanout measurements.

## Implemented fixes

- `FgRecoveryBudget.h` / `EngineController.cpp`: warmup FG timestamps now follow the matching completion identity and stay out of steady-state admission cost. Previously warmup graph cost was excluded but its FG stage still entered the steady budget. Repeated resets could repeatedly poison recovery.
- Admission now charges measured GPU blit cost instead of CPU Present duration. DXGI/driver blocking remains real backpressure in the existing bounded scheduler; it is no longer charged again as predicted GPU work. Genuine expensive steady GPU work still fails admission.
- Live present-sink generation receives the current real frame without the graph-FG source-interval delay. XeSS retains ownership of its subframe pacing. Graph DLSS A/B lookahead, file/replay timing, resource leases, reset rules and queue bounds remain in place.
- Window capture uses its configured frame interval for presentation phase, preserving actual A/B timestamps. Alternating compositor packet durations previously moved consecutive batches' deadlines across one another. Physical capture continues to honor sample duration.
- Bounded admission diagnostics now distinguish GPU blit and CPU Present and retain rejection samples separately from periodic successful admissions.
- Capture callback diagnostics now include arrival interval, driver PTS interval and sample duration next to lock/copy time. This is diagnosis support, not a claimed GC573 throughput fix.

## Verification

Artifacts: `E:/项目/Veyra/logs/xess-gc573-20260919/` and `E:/项目/Veyra/tests/xess-gc573-20260919/`. Build: `E:/项目/Veyra/build/slider-reset-20260919/`. Process TEMP/TMP: `E:/项目/Veyra/tmp/xess-gc573-20260919/`.

Build command: `scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/slider-reset-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/xess-gc573-20260919 -Targets veyra,veyra_presentation_pacing_tests,veyra_experimental_backend_tests,veyra_live_timing_tests,veyra_presentation_worker_tests,veyra_capture_color_tests` (PowerShell array used for Targets). `build5.log`: exit 0.

- `veyra_live_timing_tests`: exit 0; alternating compositor duration deadline ordering for 2X/4X/6X, sample-duration precedence, existing timing contracts.
- `veyra_presentation_worker_tests`: exit 0; warmup contamination with and without graph timestamps, repeated recovery at 2X/4X/6X, real GPU overload still rejected, existing bounded scheduler tests.
- `screen-dlss6-final`: 120.003 seconds, media speed 0.999152, 3033 processed frames. Three 80 ms injected owner-work episodes followed by valid-generation recovery all passed; clean stop passed. Stable epoch: zero skipped-before-evaluation, 15190 ready-valid generated frames, 14456 generated submissions, 734 expired, queue high-water 2, zero allocator waits. These counters are not scanout FPS.
- Before the compositor-phase fix, `screen-dlss6-quiet` had 441 expired / 1925 valid (22.9%); the final longer stable epoch had 734 / 15190 (4.8%). Different durations and ambient compositor load prevent treating this as a controlled performance claim. Expiration and source mailbox overwrites remain; this is not a zero-drop claim.
- `dlss6` file lifecycle regression passed before the final compositor-only change: pause, seek, resume, three sync-mode changes, resize, disable, clean stop.

- `screen-xess4-final`: 120.044 seconds, media speed 0.999484, 3050 processed frames, generation remains active; three injected-stall recovery cycles and clean stop passed. Steady capture-timing samples show schedulingWaitP95Ms=0; provider Present still takes time and is not counted as zero-cost generation.
- `screen-dlss4-vsync`: despite its directory name, mode=-1 completely disabled synchronization; this is an off-mode test. 120.012 seconds, speed 0.999586, 3025 processed; three recovery cycles passed. Final Present flags verified sync=0.
- `screen-dlss4-vsync-enabled`: mode=0/display=1, final Present sync=1/flags=0. 120.050 seconds, speed 0.999264, 3023 processed; three recovery cycles passed. Stable epoch: 9075 valid generated, 8203 submitted, 872 expired, zero skipped-before-evaluation, queue high-water 2. A 100 Hz screen cannot physically display all frames of 30x4 output.
- `xess-pan4`, `xess-recovery2`, `xess-recovery4`, `xess-resize4`: exit 0 with D3D12 debug layer, zero debug errors. Checks actual provider submissions through pan, reset/recovery and resize; not visual-quality or scanout certification.
- `veyra_capture_color_tests`: exit 0. First invocation omitted FFmpeg runtime PATH and failed to start (0xC0000135); rerun with the same dependency PATH as integration tests passed.
- `dlss2-lifecycle-final`: final build, 6-second file playback followed by pause/seek/resume, three mode changes and resize/disable; exit 0, speed 0.987080, 142 processed frames in measurement.
- `screen-xess4-vsync`: 15.047 seconds plus three stall/recovery cycles, exit 0, speed 0.994925, 374 processed. Verified backend=XeSS/sync=1/flags=0; provider remains pacing owner.

Integration command pattern: `scripts/run-short-test.ps1 -Exe <build>/veyra_presentation_pacing_tests.exe -Arguments @('unused','<tests>/<case>','-1','0','6','120','screen-dlss') -TimeoutSeconds 220 -LogPrefix <logs>/<case>`. XeSS uses factor 4 / `screen-xess`; enabled VSync uses mode 0 / display 1. File lifecycle uses the existing `E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4`, 6 seconds and `lifecycle`. Backend regression command: `veyra_experimental_backend_tests.exe <case> <tests>/<case>`; each took only a few seconds. GPU tests ran sequentially.

## Failures and limits

The first screen test attempted to capture its own process and failed with 0x80070057, as designed. The harness was corrected to use an owned child process; the product's self-capture protection was retained. Both verbose and quiet DLSS tests showed expired generated frames before the compositor-phase fix, ruling out per-frame logging as the sole explanation.

GC573 logs show real input around 53.49 fps with effects off. The packed RGB path already uses a bounded copy and GPU unpack; the earlier suspicion of mandatory CPU RGB expansion is not supported for that log. Without the card and matched OBS format/driver timing, no confirmed throughput fix can be claimed. The new diagnostics allow the next user run to distinguish driver cadence, lock contention and copy cost.

No 5080 power trace, GC551/GC573 hardware, PS5 session, HDR/VRR display, RTX 30/40 regression or external display-latency measurement was obtained. GPU utilization alone does not establish scheduling failure; DLSS 4X/6X executes three/five evaluations and is not inherently free. Present CPU duration is not a measurement of GPU idle time. No external reviewer was used.

The broad legacy delivery suite was not rerun; this turn built the application and ran the targeted scheduling, capture-layout and backend regressions listed above. No new export/color/NR algorithm behavior is claimed. Source review confirmed no additional presentation thread, unbounded queue, removal of discontinuity resets or mutation of runtime binaries. `git diff --check` passed.
