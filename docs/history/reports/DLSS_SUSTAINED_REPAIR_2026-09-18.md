# Sustained DLSS 4X / 6X repair, 2026-09-18

Branch: `codex/post140-field-repair-20260917`. No merge, push or release.
Local hardware: RTX 5070 / driver 616.56 / physical USB3 capture, 1080p60 YUY2.
User reports both 4X and 6X becoming limited without full total GPU usage.
The physical RTX 30/40/5090 acceptance limits in the other repair documents remain.

## Findings and implementation

- Preserved application log: `out/logs/dlss-sustained-user-20260918.log`, SHA256
  `65A775AECEC8C73BC96B8AC5FDB838EF2B0C05A7ED9689A7DB628C59C198D641`.
  Its capture configuration includes SR to 4K, realtime NR at 1080p, FG at 4K
  and a 2191x1187 window. Many evaluated generated frames expire before Present.
- `FrameMetrics.h` omitted Fg4/Fg5. 6X therefore overwrote FgBatch and Blit
  timestamps. Added separate stages, static assertions and regression checks.
- Shared presentation/enhancement allocators caused approximately 6ms waits in
  Present although DXGI service was approximately 0.1-0.2ms. Admission counted
  this GPU waiting again as presentation service. DLSS now uses a dedicated
  DIRECT presentation queue, fence and three command slots. The enhancement
  ring stays at six slots. XeSS/FSR keep their existing provider queues.
- Producer/consumer GPU fence handoffs protect each output parity. A real
  texture waits for the full FG batch because FG still reads it. A generated
  texture waits its own ready fence. Reuse and teardown wait for consumer
  completion. Test-only readback drains the correct queue.
- Live capture accepts at most one pending enhancement batch while continuing
  to service its bounded presentation queue. It reads the latest mailbox frame
  after capacity is available, avoiding accumulating stale enhanced batches.
- Live interpolation deadlines now include measured base processing after B
  arrives, capped at one source interval and sampled before processing. PTS
  stay unchanged. This adds bounded live latency; it is not free lookahead.
- Admission never discounts future FG by CPU elapsed time. CPU progress is
  capped at measured base cost. Transient rejection no longer imposes a fixed
  250ms cooldown, which otherwise loses 15 healthy 60Hz pairs; every pair still
  checks its real deadline, and recovery requires consecutive admissions.
- Presentation service samples expire after one second. Soft history resets
  retain revision-scoped observations instead of starving completion metrics.

## Experiments and failures

All rates below are submission rates, not physical scanout FPS. Each test is
bounded below 300 seconds. Admission-off runs are diagnostic controls only.
Product settings do not enable that override.

| Capture configuration / experiment | Post-recovery mean submissions/s |
| --- | ---: |
| Original 6X NR+SR | about 60 / target 360 |
| Enum fix only, 4X NR+SR | 69.615 / target 240 |
| Deadline allowance, 4X / 6X NR+SR | 90.290 / 90.261 |
| 6X effects off | 359.923 / target 360 |
| Dedicated queue, 4X NR+SR | 96.913 |
| Dedicated queue + 12 enhancement slots, 4X NR+SR | 93.615 |
| Dedicated queue + one pending GPU batch, old cooldown, 4X NR+SR | 131.615 |
| Same, admission off, 4X NR+SR | 179.000; still expires frames |
| Dedicated queue + backpressure, old cooldown, NR-only 6X, 60 seconds | 359.868 |

The 12-slot experiment was removed; it did not reliably improve throughput.
Full NR+SR local GPU work was about 17-18ms per source at 4X and 20ms at 6X,
above the 16.67ms budget of 60Hz. Low total utilization cannot prove that this
serial processing path fits its deadline. No claim of full 4K 360fps is made.

Build6 failed on `.at()` used with a C array; corrected to bounds-checked
indexing. Builds7-10 passed. Logs: `out/logs/fg-sustained-buildN-20260918.log`.
First presentation regression passed exact pixels for 4X (18 generated) and
6X (30 generated), with zero D3D12 errors. Its repeated 4X cycle failed because
the test attempted to save the same PNG twice; fixed with cycle-specific names.
Original result: `out/logs/fg-presentation-final-result-20260918.log`.
Pixel equality checks presentation against actual DLSS output, not interpolation
quality. The earlier content-dependent synthetic motion failures remain open.

## Final local validation

Build directory: `out/build/post140-hdr-preflight-20260918`.
`veyra_presentation_worker_tests.exe`: passed, including deadline recovery and
expiry tests; log `out/logs/fg-scheduler-final-20260918.log`.
`veyra_repair_contract_tests.exe`: 205 checks, zero failures.

Build11 passed (44 targets): `out/logs/fg-sustained-build11-20260918.log`.
Final executable: `out/build/post140-hdr-preflight-20260918/veyra.exe`.
SHA256: `9F63750E652C8FBE2CE6185D3D21CFBD7E7CAA19BB7A66E2DDFB297D875B7251`.

| Final configuration | Duration | Recovery mean submissions/s | Result |
| --- | ---: | ---: | --- |
| 1080p60 capture, NR, DLSS 4X | 180 s | 239.981 / 240 | PASS |
| 1080p60 capture, NR, DLSS 6X | 180 s | 359.943 / 360 | PASS |
| 1080p60 capture, NR + SR to 4K, DLSS 4X at 4K | 45 s | 177.609 / 240 | FAIL throughput; lifecycle PASS |
| 1080p60 capture, NR + SR to 4K, DLSS 6X at 4K | 45 s | 191.130 / 360 | FAIL throughput; lifecycle PASS |

Each run injected 55 ms CPU work per source frame from t=15 to t=17 seconds.
The recovery mean starts at t=22. Both 180-second runs have 158 recovery
samples with zero limited samples: 4X ranges 239-241 submissions/s, 6X 359-361.
Expired frames remain at 3 and 7 respectively throughout recovery. Total
generated frames are 32049 and 53410. The 6X runtime log records Create and
Release `0x1` and 53440 Evaluate calls. These are actual local runtime calls,
not RTX30/40 acceptance or display scanout measurements.
Results: `out/logs/fg-final-{nr4,nr6,full4,full6}-result-20260918.log`;
runtime logs: corresponding `out/logs/fg-final-*-20260918/engine.log`.
The NR 4X run used build10; build11 only added test-readback queue draining
and corrected the nonlive consumer-fence diagnostic after that run.

Presentation regression passed 4X -> 6X -> 4X, 40 source frames per cycle,
114/190/114 generated frames, reset, queued presentation, 640 -> 320 -> 640
resize, consumer-fence reuse protection and producer-first teardown.
Maximum pixel error against each corresponding DLSS texture: 0; D3D12 errors: 0.
Log: `out/logs/fg-presentation-verified-20260918.stdout.log` (exit 0).

The supplied MKV settings regression passed 1 -> 2 -> 6 -> 1 -> 6, paused
seek/resume and shutdown: 117 source frames, 172 generated, maximum generated
count 5. Log: `out/logs/fg-final-settings-20260918.stdout.log` (exit 0).

Final delivery gate passed in 62.5811217 seconds (26 checks):
`logs/delivery/4a5b5a11d376481184994f93e376659f/result.json`.
Its executable hash matches the hash above. This gate does not supersede
the failed full-enhancement throughput controls or hardware acceptance limits.

Commands (run from repository root):

```powershell
& cmd.exe /c 'out\build\veyra-build-x64-release.cmd'
& out/build/post140-hdr-preflight-20260918/veyra_presentation_worker_tests.exe
& out/build/post140-hdr-preflight-20260918/veyra_repair_contract_tests.exe
& out/build/post140-hdr-preflight-20260918/veyra_fg_sustained_tests.exe 'capture:0:0:-1:0' out/logs/fg-final-nr4-20260918 4 on 180 nr 0.95
& out/build/post140-hdr-preflight-20260918/veyra_fg_sustained_tests.exe 'capture:0:0:-1:0' out/logs/fg-final-nr6-20260918 6 on 180 nr 0.95
& out/build/post140-hdr-preflight-20260918/veyra_fg_sustained_tests.exe 'capture:0:0:-1:0' out/logs/fg-final-full4-20260918 4 on 45 on 0.95
& out/build/post140-hdr-preflight-20260918/veyra_fg_sustained_tests.exe 'capture:0:0:-1:0' out/logs/fg-final-full6-20260918 6 on 45 on 0.95
& scripts/run-short-test.ps1 -Exe out/build/post140-hdr-preflight-20260918/veyra_fg_presentation_tests.exe -Arguments @('out/logs/fg-presentation-verified-20260918') -TimeoutSeconds 60 -LogPrefix out/logs/fg-presentation-verified-20260918
& scripts/run-short-test.ps1 -Exe out/build/post140-hdr-preflight-20260918/veyra_fg_settings_tests.exe -Arguments @('C:/Users/123/Desktop/Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv','out/logs/fg-final-settings-20260918') -TimeoutSeconds 240 -LogPrefix out/logs/fg-final-settings-20260918
& scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/post140-hdr-preflight-20260918
```

## Changed files and remaining acceptance

This sustained repair changes `include/veyra/diagnostics/FrameMetrics.h`,
`include/veyra/engine/{FgRecoveryBudget,TimingWindow,VideoPresenter}.h`,
`src/engine/{EngineController,VideoPresenter}.cpp`,
`include/veyra/pipeline/EnhanceGraph.h`, `src/pipeline/EnhanceGraph.cpp`,
`tests/unit/{LiveGpuSchedulerTests,RepairContractTests}.cpp`, `CMakeLists.txt`,
and adds `tests/integration/{FgSustainedTests,FgPresentationTests}.cpp`.
Evidence is recorded here and in `docs/WORKLOG.md`; earlier repair documents
also correct their DRED description. Other existing branch edits are retained.

Full 4K enhancement still exceeds the local 60 Hz source budget (approximately
17.3 ms at 4X and 20.8 ms at 6X). Neither a driver update nor low total GPU
utilization proves these serial stages meet their deadlines. This remains an
open performance limit; no multiplier reduction is presented as a fix.

RTX30/40/5090 physical tests were not executed. The affected 5090 NR -> XeSS
device removal (`0x887A002B`) remains unreproduced locally; its useful DRED
evidence and the original export-worker first-error log remain unavailable.
Content-dependent synthetic interpolation-position failures also remain open.
No all-issues-complete claim is made.

Next acceptance task: run this exact build with the affected user's unchanged
4X/6X configuration and capture application/engine logs spanning the first
limited transition, so remaining serial GPU cost can be separated from a
recurring scheduling failure on that hardware.
