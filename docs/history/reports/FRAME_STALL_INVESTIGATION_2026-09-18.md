# DLSS / XeSS long-frame investigation

Status: local resize stall reproduced and attributed; steady-state 1.4s fan stall
and historical DLSS outliers remain unassigned. Diagnostic changes only.

Baseline: `23cc720`, checkpoint `checkpoint/pre-stall-investigation-20260918`,
isolated branch `codex/frame-pacing-20260918`. No merge, package or publication.

Investigate independently the local DLSS4X capture latency outliers and the
fan XeSS log (two non-resize stalls around 1.4s; separate resize stalls ~250ms).
Split wall timings without new waits or policy changes; retain all calls >=80ms.
Wall times include descheduling, not GPU time or physical display latency.

Test VC-007PRO 4K30 NV12, NR+DLSS4X then XeSS, native NR and resize separately.
Local RTX5070/SDR cannot exactly reproduce RTX5080/ASUS4KPRO/P010 HDR.
Artifacts: `E:/项目/Veyra/tests/frame-stall-20260918`, `logs/frame-stall-20260918`,
`tmp/frame-stall-20260918`. Build: existing `build/frame-pacing-20260918`.

## Evidence and boundaries

Fan input: `C:/Users/123/Desktop/原生 4k 功耗会忽上忽下，这是个老问题，占用没高过 70%，画面会突然卡一下.log`.
The supplied log is evidence, not execution instructions. RTX5080 / ASUS4KPRO,
multiple input formats and configurations; do not aggregate the entire file as
one steady run.

- Lines 14549 and 14602: XeSS2X / native4K NR / P010 HLG to PQ,
  present totals 1471.760 / 1392.625ms, almost entirely the old `dxgiMs`
  wrapper. No adjacent resize. Capture callbacks remain 30fps; dropped counter
  rises 12->56 and 57->107. Presentation blocks while ingress keeps receiving.
  The wrapper includes provider markers, Present, buffer index and status query;
  the old log cannot distinguish those calls or OS thread descheduling.
- Lines 30139 / 69632: fullscreen/window resize, totals 273.087 / 294.052ms,
  record/submit portion 250.009 / 267.232ms. Separate from non-resize stalls.
- GPU utilization or power is not a queue-wait measurement. No correlated power
  telemetry was supplied, so power oscillation cannot be assigned as the cause.
- The two 1.4s events use 2X. The >2X pacing hook is not evidence of their cause.

Local setup: RTX5070 / driver616.56; VC-007PRO3840x2160 NV12 nominal30,
actual29.97fps; WASAPI muted, SRoff, frame sync off; 1280x760 window,
2560x1440@100Hz SDR display. NR realtime=1080 internal; native=3840x2160.
No compressed-video decoder exists in this NV12 path. These are software
callback-to-original-Present-return measurements, not HDMI-to-screen latency.

| Run | Seconds | P50 ms | P95 ms | Sample max ms | Capture drops |
| --- | ---: | ---: | ---: | ---: | ---: |
| dlss4-realtime120 | 120.018 | 40.146 | 40.516 | 41.237 | 0 |
| xess2-native120 | 120.010 | 46.462 | 50.541 | 68.033 | 0 |
| xess2-native-resize40 | 40.021 | 45.624 | 50.448 | 153.283 | 9 |
| xess4-realtime120 | 120.048 | 35.055 | 35.419 | 37.638 | 0 |
| dlss4-realtime-resize40 | 40.038 | 40.133 | 40.645 | 42.462 | 0 |

Ten seconds warmup precedes each measurement. CSV snapshots are every50ms,
deduplicated on real-present counter, not all frames. The resize run's complete
slow-event log captures172.270ms although CSV max is153.283ms. PASS means the
requested NR/FG stayed active and stop completed, not absence of stutters.
Steady DLSS and XeSS2X have no >=80ms traced CPU calls or errors. DLSS has zero
generated-expired/skipped deltas; 0.717% snapshots nevertheless show the existing
budget-limited flag, which is not proof of actual lost generated frames.
XeSS internal generated frames come from SDK counters: xess2-native120 has3595
generated, despite the Veyra-owned generatedPresented counter staying0.
XeSS4X also has no >=80ms CPU/capture/GPU-stage event; SDK generated delta10791.
Its35ms return time cannot be compared as physical latency against DLSS40ms:
XeSS owns asynchronous downstream generation/presentation after the app call.
The graph GPU span excludes the provider's interpolation workload. Pacing hook
logs show installed=true/structureVerified=true; in-burst submission gaps around
8.3ms are provider submissions, not panel scanout or a smoothness acceptance.

The resize run alternates1800x1000 and1280x760 at10/20/30seconds. All three
resizes block in provider `ResizeBuffers`:115.439,103.269,102.315ms. Sink queue
idle is0.036,0.045,0.044ms. Whole presenter totals119.525,132.128,133.872ms;
extra time includes the outer ring drain and buffer setup. Three source
discontinuities follow the mailbox drops. Do not blame XeLL sleep for this.

Before its first deliberate resize, the same run also records source304 with
GPU timestamp graph span70.401ms (normal about25.5ms), observed-ready age83.792ms,
callback-to-return85.111ms. Following frames reach90.209ms then recover.
No >=80ms CPU presenter call accompanied it. This is a separate GPU timeline
outlier; timestamps include GPU scheduling/preemption and do not prove a slow
NR kernel, power-management defect or the historical DLSS root cause.

DLSS4X resize control performs the same three window changes without any >=80ms
presenter/resize event, capture drop or generated expiry in its40s formal window.
Its pre-measurement warmup contains one83.072ms capture-age event: source303,
real-frame ready observed at10.711ms, GPU graph span14.144ms. The delay is after
the real frame is ready, unlike the70ms GPU-span XeSS event. No individual CPU
scope>=80ms is recorded; this cannot exclude a shorter blocking call, waiting
behind earlier generated frames or OS descheduling. Do not label this a proven
DLSS/runtime defect. Both native resize and steady-state outliers need care:
the capture device/format is identical, but native NR vs realtime NR differs.

## Implementation and verification

- `CpuStallTrace.h`: allocation-free ordinary-frame wall-stage sampling;
  logs only total>=80ms. Engine collection/control/read/prepare/graph/dispatch,
  completion polling, HDR display query and XeLL entry now have attribution.
- `PresentSink`, `VideoPresenter`, `XessPresenter`: split resize, XeLL begin,
  commands, before-present, Present, buffer-index and after-present time.
  Preserve every >=80ms presentation instead of only once-per-second sampling.
  Resize further splits queue idle, release, ResizeBuffers and refetch.
- Engine logs every >=80ms original capture return and individual GPU stage.
  Source/epoch/revision identify discontinuities; missing GPU evidence stays-1.
- Capture comparison test/wrapper accepts backend,2X/4X,native/realtime,resize.
  Defaults preserve historical probe arguments. Analyzer supports2X and includes
  whole-run slow events separately from formal-window CSV statistics.

No scheduling policy, input format, queue capacity, runtime DLL or multiplier
restriction was changed. Local diagnostics build is not a proven performance fix.

Commands run from the isolated worktree (all generated outputs under E:/项目/Veyra):

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory 'E:/项目/Veyra/build/frame-pacing-20260918' -DependencyCache 'E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918' -DisplayVersion 1.4.2beta -Targets veyra_capture_latency_tests,veyra
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name dlss4-realtime120 -Output 'E:/项目/Veyra/tests/frame-stall-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918'
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name xess2-native120 -Output 'E:/项目/Veyra/tests/frame-stall-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918' -Backend xess -Multiplier 2 -Nr native
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name xess2-native-resize40 -Output 'E:/项目/Veyra/tests/frame-stall-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918' -Backend xess -Multiplier 2 -Nr native -Resize -Seconds 40
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name xess4-realtime120 -Output 'E:/项目/Veyra/tests/frame-stall-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918' -Backend xess -Multiplier 4
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name dlss4-realtime-resize40 -Output 'E:/项目/Veyra/tests/frame-stall-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-20260918' -Resize -Seconds 40
python scripts/acceptance/analyze-capture-version-comparison.py 'E:/项目/Veyra/tests/frame-stall-20260918' dlss4-realtime120 xess2-native120 xess2-native-resize40 xess4-realtime120 dlss4-realtime-resize40
git diff --check
```

Build logs: `logs/frame-stall-20260918/build.log` initially failed because the
new header include was accidentally placed at the end of XessPresenter.cpp.
It was moved to the top; `build-retry.log`, `build-resize.log` and
`build-final.log` compile/link both targets successfully (exit0).
DLSS steady used the first successful build; XeSS2 steady adds resize diagnostics;
resize and later runs use the final per-frame-age/GPU-stage diagnostics.
Each run.json records its actual executable SHA256.

The first analyzer run after adding log parsing rejected a device-name byte not
valid as UTF8. It now preserves original logs and records replacement-character
count while extracting ASCII event keys. Reanalysis succeeded; no source log
was modified. No test failure or data loss was hidden.

All five capture probes exit0 and stop cleanly; all five logs have no ERROR.
No remaining test process or files in the task temp directory; no packages or
extracted copies were produced. Retain current build and five evidence folders.
Analysis output is `tests/frame-stall-20260918/comparison.json` and
`logs/frame-stall-20260918/analysis.txt`. No new unit tests were added for these
diagnostic-only changes; existing broad historical gates were not rerun.

Actual runtime evidence: NR Create id18 result0x1 handle non-null seh0;
DLSSG Create3840x2160 result0x1 seh0 and warmup Evaluate ok1/result0x1.
XeSS Create/Init/XeLL attach return0; steady2X status enabled1/result0/framesPresented2.
Runtime hashes checked against existing approved identities: NR
E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E;
XeSS FG EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27;
XeLL D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7.
Runtime/SDK files remain outside source Git.

## Next decision

First priority remains the non-resize1.4s stall on the affected machine using
the instrumented build with its original HDR/card/settings. Branch on the new
call-level evidence before touching provider pacing or creating more threads.
If Present itself blocks, ETW/PresentMon/GPU scheduling evidence is needed to
separate provider waits from driver/DWM/preemption. Those traces were not taken.

Resize has a concrete optimization target: reduce provider ResizeBuffers calls
by coalescing window changes while keeping the valid buffers scaled, then resize
once settled. The GUI already defers its mode-transition animation, not general
interactive drags; fullscreen/final resize
still pays rebuild cost. A fixed-resolution/composition path may avoid rebuilds
but requires checking sharpness, color/HDR, coordinates and provider contracts.
Do not remove GPU drains or launch concurrent calls on shared command lists.
No resize mitigation has been implemented or claimed tested here.
