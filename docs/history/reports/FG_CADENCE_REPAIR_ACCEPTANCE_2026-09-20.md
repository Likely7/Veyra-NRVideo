# FG cadence: partial acceptance, fixed 6X still open

Latest non-NR continuation: paired whole-group prediction, removing redundant
presentation clear, and CopyResource flow history experiments were reverted.
None established a cadence gain; fixed6 remains approximately298-303/s in the
latest retained windows with17ms holes. See FG_NON_NR_EXPERIMENTS_2026-09-20.md
for paired measurements, commands and limitations. NR stays unchanged.

## Fixed6 workload controls

Two30s diagnostic tests select EXISTING NR900/720 policies, keeping
fixed6 and4K FG/output. These change NR/flow resolution and are not the
requested original-quality acceptance. No defaults or product code changed.

| NR policy | Retained seconds | Submissions/s | P99 / max gap ms | Gaps over10ms | Expired subframes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 900p | 4.945 | 319.299 | 15.361 / 15.816 | 37 | 17 |
| 720p | 4.564 | 353.874 | 3.993 / 4.559 | 0 | 28 |

900p's >=95% throughput assertion fails;720p passes that assertion but
still drops individual generated outputs. Neither result establishes
physical display smoothness or fixes NR1080+4K+6X. Evidence folders:
nr900-fixed6 / nr720-fixed6 under the existing task test root. See WORKLOG
for commands and GPU costs. The proposed new NR performance UI is redundant
with existing options and has not been implemented.

## XeSS subframe evidence

Existing fence-xess4 provider-hook logs include all Present returns across
burst boundaries. After excluding10 seconds from the first window,19
windows cover4560 intervals:mean4.166579ms,max6.909ms. No recurrent16ms
DLSS-style hole appears in those steady4X records. The first startup window
has max159.027ms and is not accepted as smooth. This evidence is stronger
than SDK FPS alone but still not physical scanout, not a6X result, and not
proof for other GPUs or user workloads. XeSS2X's earlier counter test does
not provide the same hooked-subframe measurement.

## Full-group cost follow-up

Matched GPU stages by complete input identity, excluding seed-only groups.
fence-fixed6 full-group mean: NR6.424ms, flow0.992ms, FG9.747ms; five FG
Evaluate intervals account for9.661ms of that batch. The remaining0.087ms
is between-evaluation elapsed time, not the whole pipeline overhead.
Do not infer runtime-internal engine utilization from queue timestamps.

Disabling redundant command-slot timestamps for one bounded experiment
gave291.568 submissions/s versus288.946 in the subsequent control, but
73/76 gaps over10ms remain in5.320/5.347s retained windows. Both runs keep
fixed6, NR,4K, real PTS, graph timestamps and interpolation validity checks.
The test hook was removed; no performance claim or fix promoted. Analyzer
now reports full6GpuCosts separately from warmup/seed samples. See WORKLOG
for commands and artifacts. Fixed6 acceptance remains open.

## Latest continuation: seed on rejected input

Rejected full groups may now perform one affordable reset evaluation on the
same real input. The next adjacent input can generate immediately, avoiding
an additional real-only warmup group. The admission callback returns an
explicit Skip/Evaluate/Seed decision exactly once. Seed affordability does
not advance budget recovery, and seed-only output remains budget-limited.

Current sequential RTX5070 runs, same media/settings as below:

| Run | Duration | Retained seconds | Submit FPS | P95 / max gap ms |
| --- | ---: | ---: | ---: | ---: |
| reseed-default6, NR on fixed 6X | 45s | 5.483 | 279.593 | 15.105 / 16.747 |
| reseed-default2, NR on 2X | 30s | 9.742 | 119.998 | 8.782 / 9.039 |

The fixed6 retained window contains 240 full groups and 88 seed-only groups;
rejection and warmup counters refer to the same 88 inputs, not 176 inputs.
It has one sub-ms gap, one gap over 16.667ms and no generated discards.
This is an improvement, **not fixed6 uniform-cadence acceptance**. No physical
scanout or screen latency measurement. Prior rollback is ec0f57d.

CPU suite: 126 PASS. reseed-content2/content6 validate reset, seed, true skip,
single callback, dynamic multipliers, PTS and accounting, with zero D3D12
errors. reseed-lifecycle passes pause/seek/resume/mode changes/resize/stop.
reseed-smoke passes main startup and exit. reseed-xess2 runs 30s, app60/s,
SDK120/s in final samples, no admission skips or expirations; not scanout.
All targets built in reseed-metrics-build.log. Evidence under
E:/项目/Veyra/{tests,logs}/fg-cadence-repair-20260920.
The temporary double-callback experiment switch was removed before these
final runs. The historical results below describe the prior checkpoint.

Gap attribution: analyze-fg-cadence.py now joins consecutive Present events
with their media PTS, admission state and CPU-observed ready time. In
reseed-default6 all88 gaps over10ms have a16.667ms media step and correspond
to the88 rejected groups. These gaps lack intermediate generated samples;
moving a ready real frame earlier cannot recreate that missing motion.
reseed-default2 has no gap over10ms. Files reseed-gap-causes.json and
reseed2-gap-causes.json retain the analysis. This does not identify every
GPU scheduling cause of rejection or prove a hardware limit.

## Scope and result

Branch: `codex/fg-cadence-audit-20260920`. RTX5070, user p001.mp4,
3840x2160 60fps, NR realtime 1080, 4K output target, DLSS 6X/2X,
pacing setting off. The source already matches the 4K target, so SR does
not execute. No package, push or release. No physical scanout measurement.

The requested NR-on fixed 6X uniform cadence has **not passed**. Promoted
prefix admission and pacing reduce the original roughly 80 submissions/s
collapse to about 265, but repeated admission rejection and history warmup
still create 15-17ms holes. Automatic 4X fallback is separately successful;
it must not be used as evidence that fixed 6X was solved.

## Changes under test

- File DLSS admission checks first-output cost against its deadline and whole
  group cost against the last deadline. Account for older queued GPU work,
  retaining predicted start when an earlier completion watch retires.
- Normal file DLSS playback enforces the existing 90% minimum output spacing
  after the audio-clock deadline, preventing dense catch-up submissions.
  XeSS/FSR provider pacing and capture admission are not changed by this rule.
- Sixteen command allocators cover two bounded jobs, each with up to three
  enhancement lists and five MFG lists. The two-job scheduler and texture
  history capacities remain unchanged. This is a deliberate expansion from
  the earlier six-slot allocator budget, not a sixteen-frame queue.
- Logs distinguish requested/effective multipliers, first deadline and queued
  GPU cost. Failed admission/spacing experiment switches were removed.
- Earlier checkpoint `1be28a4` preserves valid prior generated leases across
  soft drops, while still resetting processing history and invalidating on
  hard boundaries. Earlier fallback checkpoint: `0050870`.

## Current continuation correction

Automatic multiplier selection has been removed at the user's request. Current
playback preserves the selected multiplier; it may still skip work or expire
generated frames when deadlines cannot be met. Historical adaptive4X results
below describe a superseded implementation, not the current playback policy.

The status-readback experiment88915a3 was reverted as55e0bdd: an early subframe
could read the final status before its producer fence, and Map/Unmap used
different resources. The308.1/s result is withdrawn as optimization evidence.
The restored normal6 run (no fixed-multiplier test override) preserved6X for
30s:294.676 submissions/s in5.274s retained, P99/max16.846/17.259ms,
69 rejected groups, zero retained generated discards. This remains below
fixed6X acceptance. UI/worker tests and restored-content6 exited0; content
test covers producer fences, PTS, resets and dynamic multipliers.

## Measured results

Each trace is the **final retained window** of a bounded in-memory trace,
not the entire run. FPS below means application submissions, not display.
All GPU runs were sequential; startup, window size and media were matched.

| Run | Duration | Retained seconds | FPS | P95 / max gap ms | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| prefix2, NR on 2X | 30s | 11.367 | 120.001 | 8.751 / 9.061 | throughput assertion >=95% passed |
| prefix6, NR on fixed 6X | 45s | 5.760 | 265.445 | 15.428 / 17.169 | lifecycle only; cadence fails |
| prefix-adaptive6, NR on request 6X, actual 4X | 30s | 6.204 | 240.007 | 4.514 / 5.560 | fallback only |
| prefix-headroom6, effects off fixed 6X | 30s | 4.869 | 360.004 | 3.198 / 3.592 | diagnostic; throughput assertion >=95% passed |

No sub-ms gaps in these retained windows. prefix6 retains 236 complete
six-frame batches, 54 rejected pairs and 54 warmups, with no generated
discards in that window (16 over the whole run). Its 19 gaps over16.667ms
are why the higher average FPS is insufficient for acceptance.

The NR-on serial stage measurements are approximately NR6.8ms, flow1.2ms,
FG9.8-10ms, plus remaining work, against a 16.667ms source interval.
This establishes an over-budget **current serial implementation**; it does
not prove the hardware cannot benefit from a different implementation.
Removing admission entirely previously caused ~59fps and 1620 expired
generated frames in the retained trace. More buffering cannot cure a
sustained service-time deficit without growing latency.

XeSS `prefix-xess4`: 30s, lifecycle passes, app60/s and SDK240/s in the
final reported samples, zero admission skips/expiration. SDK counters do
not establish subframe scanout timing. Earlier XeSS2X evidence remains in
`xess-final2`; no claim of a new 2X run after the allocator change.

CPU policy suite: 121 PASS, exit0, including first/last deadlines, queued
cost, CPU time not erasing future FG work and history warmup. GPU
`prefix-content6`: dynamic2/4/6/default, real PTS, reset/recovery,40 NR
frames,144 FG evaluations,140 valid outputs, debugErrors=0, exit0.

`prefix-lifecycle`: paused seek, resume, live pacing-mode changes, resize,
off-mode playback and clean stop pass, exit0. `prefix-smoke`: main program
`--smoke-empty --smoke-seconds 5`, exit0. These validate lifecycle, not
NR-on fixed6 cadence. The final rebuild only added diagnostic log fields
after prefix2/6/adaptive6/headroom6; scheduling behavior is identical.

## Reproduction and evidence

Outputs: `E:/项目/Veyra/tests/fg-cadence-repair-20260920/<run>` and
`<run>.{stdout,stderr}.log`, JSON from `scripts/acceptance/analyze-fg-cadence.py`.
Build/log root: `E:/项目/Veyra/logs/fg-cadence-repair-20260920`.
Temporary root: `E:/项目/Veyra/tmp/fg-cadence-repair-20260920`.
Current local build: `E:/项目/Veyra/build/slider-reset-20260919/veyra.exe`.

Build via `scripts/build-isolated.ps1`, existing build/cache, display1.4.3,
targets `veyra`, `veyra_fg_sustained_tests`, `veyra_presentation_worker_tests`,
`veyra_fg_admission_tests`, `veyra_presentation_pacing_tests`; logs
`prefix-final-build.log`, `prefix-lifecycle-build.log`, `prefix-final-cpu.log`.
Run via `scripts/run-short-test.ps1`,95s watchdog (admission150s), working
directory `E:/项目/Veyra/tests/dlss-recovery-20260920`, process TEMP/TMP set
to the temporary root. Sustained arguments: media,output,multiplier,on,
seconds,on,minimumRatio,file-4k; headroom uses effects off, XeSS uses
file-xess-4k. Fixed6 diagnostics set `VEYRA_TEST_FIXED_FG_MULTIPLIER=1`;
per-subframe analysis sets `VEYRA_TEST_TRACE_SUBFRAMES=1`.

## Remaining acceptance

NR-on fixed6X smoothness remains open. Investigate serial enhancement/FG
cost and reject/warmup recovery rather than further arbitrary admission
tuning. A second enhancement queue requires per-pair motion/depth and
independent fence ownership; do not share the current monotonically
signaled fence across unordered queues or assume concurrency is faster.
Neither disabling NR, changing the 4K contract nor actual4X is fixed6X
acceptance. The active goal remains open; shutdown is not yet performed.
