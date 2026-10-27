# RTX5090 P010 capture / FG investigation

## Scope and checkpoint

- Pre-change checkpoint: `3953bff`; branch `codex/5090-capture-fg-20260921`.
- User log: `C:/Users/123/Desktop/logs-4knr+dlss质量光流X2，跑不满120，GPU占用60，5090(1).log`.
- Investigate DLSS/XeSS throughput, intermittent flicker and reported 15-second hitches. No quality reduction, automatic multiplier downgrade or fixed extra delay.
- Existing rejected experiments remain rejected; see `FG_EXPERIMENT_INDEX_2026-09-21.md`.

## Evidence from the supplied log

- RTX5090; 3840x2160 P010 capture at 60 Hz, native NR, DLSS 2X, SR off, manual PQ/BT2020 HDR with HDR output.
- Before the quality-flow switch: source 60 / submission approximately 120 FPS, flow P95 3.36 ms, NR 8.56 ms, GPU readiness 13.35 ms.
- At 14:07:07.584 a settings rebuild changes NVOF SDK performance level from 10 to 5. Rebuild lasts 1618 ms (create 1448 ms), accounting for about 95 mailbox overwrites. These are not steady-state losses.
- Afterwards: processed source about 56-58 FPS, submission 106-114 FPS, flow P95 5.27-5.45 ms, GPU readiness 14.7-15.3 ms, process CPU about 3.3-3.46 ms. Percentiles must not be summed as exact frame duration.
- Admission: 999 accepted / 1 rejected. Revision 4: 735 evaluated, 45 warmup, 690 valid, 0 invalid; 689 generated presented, 1 expired. This is not primarily mass FG admission rejection or generated-frame expiry.
- Repeated mailbox overwrites coincide with repeated history resets after rebuild. Dropped source frames must still reset temporal history. Reducing processing overhead may prevent some drops; removing the resets is not a valid fix.
- Low-queue mode starts after throughput already falls. Aggregate GPU utilization is insufficient to prove available capacity on the dependent critical path.
- Capture recording lasts only about 20 seconds and includes settings changes. It cannot establish a recurring 15-second hitch. Flicker mechanism remains unproven.

## Bounded implementation / validation

1. Measure and remove the redundant same-format CPU P010/P016 staging copy. Preserve bit depth, metadata, row pitch, negative stride, odd chroma dimensions and CPU luma analysis. No reads from write-combined upload memory.
2. Compare real GPU color/precision output before and after, then run focused HDR and live FG regressions. Hardware decode/import remains separate.
3. Inspect provider/history/presentation lifetimes and run bounded sustained playback for periodic stalls. Do not label unobserved user symptoms fixed.

Generated artifacts: `E:/项目/Veyra/tests/5090-capture-fg-20260921/`, `E:/项目/Veyra/tmp/5090-capture-fg-20260921/`. Existing build directory reused: `E:/项目/Veyra/build/1.4.4-xess-current-20260921-r1/`.

## Accepted change

`src/pipeline/EnhanceGraph.cpp` now copies matching CPU NV12/P010/P016 planes
directly into the already-fenced mapped upload slot. P010/P016 no longer pass
through a same-format swscale staging buffer followed by another full-frame
copy. Signed source pitch, padded rows, odd chroma dimensions and all input code
values are preserved. Scene/cadence analysis reads ordinary source CPU memory,
not write-combined upload memory. Hardware decode/import is unchanged.

No quality, optical-flow setting, FG multiplier, queue depth or presentation
delay was changed. This is a CPU upload improvement, not a rewritten scheduler.

## Verification on local RTX5070

Built `veyra`, `veyra_cpu_yuv_upload_tests`, `veyra_hdr_color_tests`,
`veyra_fg_sustained_tests`, `veyra_live_presentation_tests` and
`veyra_fg_presentation_tests` successfully in the existing isolated build.
The latter accepts an optional absolute runtime directory instead of requiring
runtime files in the source tree. Compilation alone is not test execution.

### Same-input CPU upload A/B

The user closed the running app before these measurements. Each format uses
240 4K frames, excludes 40 warmup frames, and measures CPU upload separately
from the GPU drain. Two baseline/fixed rounds, all exit 0:

| Format | Baseline P50/P95 ms, round 1 | Fixed P50/P95, round 1 | Baseline P50/P95, round 2 | Fixed P50/P95, round 2 |
| --- | --- | --- | --- | --- |
| NV12 | 0.4656 / 0.5279 | 0.4636 / 0.5420 | 0.4614 / 0.5177 | 0.4504 / 0.5075 |
| P010 | 2.3926 / 2.6036 | 0.9804 / 1.0819 | 2.4355 / 2.6131 | 0.9289 / 1.0011 |
| P016 | 2.0466 / 2.3053 | 0.9752 / 1.1339 | 2.1069 / 2.5139 | 0.9775 / 1.1005 |

P010 saves approximately 1.4-1.5 ms per CPU upload (59-62% of this stage's
median). This is not a measured end-to-end capture or RTX5090 FPS improvement.
NV12 shows no material change. `baseline/concurrent.stdout.log` was recorded
while another Veyra instance was running and is excluded from acceptance.

`CpuYuvUploadTests` checks actual GPU linear-output FP16 hashes for 18 cases:
NV12/P010/P016, widths 64/65/66, height 35, padded pitch, positive/negative stride.
All hashes match baseline exactly in both rounds. The existing no-argument HDR
color test also passes (PQ/HLG, ranges, formats, tone mapping and grading);
it does not exercise a real HDR NR model loop.

### Sustained playback and presentation

Three 60-second UI runs with `p001.mp4`, realtime 1080-internal NR and balanced
flow all exit 0. This file/hardware-decode setup bypasses the changed CPU upload
path and is a regression check, not the user's native4K quality-flow capture.
The bounded trace retains only the final window; its FPS is not a whole-run
average or physical scanout measurement.

| Mode | Retained window | App submit FPS | Interval P95/P99/max ms | Device GPU mean |
| --- | --- | --- | --- | --- |
| DLSS 2X | 9.74 s | 120.00 | 8.74 / 8.89 / 9.03 | 60.94% |
| DLSS 6X | 5.23 s | 297.88 | 4.62 / 16.80 / 17.44 | 91.24% |
| XeSS 4X | 13.65 s | 60.00 real inputs | 17.22 / 17.39 / 17.83, real inputs only | 88.35% |

XeSS's per-second SDK records report 60 real plus 180 generated submissions
per second in steady state (`framesPresented=4`); app Present intervals do not
measure its internally generated-frame cadence. Whole-run counters are DLSS2
3411 real / 3408 generated; DLSS6 3455 / 13145; XeSS4 3452 / 10338. DLSS6 still
does not meet uniform 360 FPS. Do not market the retained 298 FPS as a fix.

Both DLSS6 and XeSS4 recorded a wall-time NR Evaluate stall near source frame
605, approximately 10 seconds after initialization: 30.565 and 30.750 ms. A
35-second NR-only control had no graph wall-time event above the 30 ms logging
threshold and maintained 60 source FPS. The wrapper directly calls the runtime
with a null progress callback; no matching 600-frame/15-second timer was found
in that path. This does not identify the runtime/driver/descheduling cause,
prove repeated 15-second stalls, or measure 31 ms of GPU computation.

`veyra_fg_presentation_tests` passed DLSS 4X -> 6X -> 4X, generated counts
114/190/114, maximum checked pixel error 0, debug-layer errors 0. It exercises
history reset, window resize, output parity reuse and consumer-fence retirement.
This focused SDR test does not reproduce a user's HDR under-target flicker.

## Remaining issues and next evidence

- RTX5090 P010 capture acceptance is pending; this machine has RTX5070. Re-run
  the affected user's exact quality-flow/HDR/NR settings and compare source FPS,
  mailbox overwrites, reset frequency and generated submissions.
- The supplied capture interval is too short to establish 15-second periodicity.
  Need a continuous affected session with settings unchanged. Preserve the
  frame-605 observation as a separate unresolved lead.
- No proven bad resource reuse or mismatched original/generated color format
  was found in this audit. Under-target flicker remains unresolved; do not
  remove required history resets or add fixed delay to conceal it.
- No failed admission, overlap, queue-depth or forced-delay experiment was
  restored. No merge, push, release or distributable package was created.

Evidence root: `E:/项目/Veyra/tests/5090-capture-fg-20260921/`.
Upload logs are `baseline-{1,2}.stdout.log` and `fixed-{1,2}.stdout.log`;
HDR logs are `hdr.*.log`; sustained cases are under `sustained/`; the control
is under `nr-only/`; presentation logs are `presentation.*.log`.
The isolated runnable app is `app/veyra.exe`, dependent on local runtime/shader
junctions. Do not distribute this staging folder as a portable package.
