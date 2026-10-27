# Non-NR fixed6 experiments, 2026-09-20

Scope: RTX5070, p001.mp4 (native 3840x2160/60), existing NR1080,
4K output, DLSS requested/effective6, pacing off. Native4K source bypasses
SR; this is not a measured SR workload. No extra delay or multiplier fallback.
Baseline source: 7a3dfae. No runtime changes, package, push or release.

Every run below is30s, sequential, with a95s watchdog. Metrics are the final
bounded retained trace window, NOT the whole run or physical scanout.
Exit0 with minimumTargetRatio0 means lifecycle only, not cadence acceptance.

## Paired whole-group cost: reverted

Hypothesis: adding independent base and FG P95 values overestimates the
actual group P95. Experiment used measured group totals for non-warmup
file admission and queued work, with expiry and reset tests. Capture stayed
unchanged. Worker tests and build succeeded. Optional hook:
VEYRA_TEST_FG_PAIRED_COST; removed after comparison, including experiment tests.

| Run | Retained seconds | Submit/s | P95/P99/max ms | Gaps >10ms | Rejected groups |
| --- | ---: | ---: | --- | ---: | ---: |
| restored-normal6 (baseline) | 5.274 | 294.676 | 4.894/16.846/17.259 | 69 | 69 |
| paired-cost6a | 5.227 | 297.699 | 4.470/16.831/17.229 | 65 | 64 |
| paired-control6b | 5.170 | 302.916 | 4.231/16.821/17.165 | 59 | 59 |
| paired-cost6b | 5.172 | 301.988 | 4.448/16.782/17.144 | 60 | 60 |

No repeatable throughput or cadence improvement. All retained windows have
zero discarded generated subframes; missing interpolation groups remain.
This rejects this estimator as a current fix, not all admission research.

## Redundant presentation clear: reverted

PresentBlit covers the entire buffer, returns black outside the image,
has no discard, and uses a PSO with blending/depth disabled and full RGBA
write mask. Tested omitting ClearRenderTargetView before that draw, with
all fences and timing unchanged. Optional hook VEYRA_TEST_PRESENT_SKIP_CLEAR
was removed after comparison.

| Run | Retained seconds | Submit/s | P95/P99/max ms | Gaps >10ms |
| --- | ---: | ---: | --- | ---: |
| skip-clear6a | 5.171 | 303.055 | 4.459/16.877/17.383 | 59 |
| clear-control6a | 5.230 | 297.891 | 4.482/16.777/17.158 | 65 |

The5.2/s difference is within observed baseline variation; P99/max did not
improve and the recurring source-period holes remain. No performance claim
or production change retained. This is not proof that the clear costs zero.

## Flow history copy: reverted

Only the same-size BGRA8 B-to-A flow history shader copy is replaced with
CopyResource. The existing shader uses a direct texel load with encode and
dither disabled for this operation. Both resource descriptions match.
Producer queue ordering, state transitions, NVOF waits and history resets
are preserved. NR is unchanged. Optional VEYRA_TEST_COPY_FLOW_HISTORY removed.

| Run | Retained seconds | Submit/s | P95/P99/max ms | Gaps >10ms |
| --- | ---: | ---: | --- | ---: |
| copy-flow6a | 5.233 | 297.892 | 4.495/16.878/17.352 | 65 |
| flow-control6a | 5.216 | 298.695 | 4.479/16.858/17.221 | 64 |

No measured benefit; both have zero retained discarded generated frames.
No product change retained. This does not rule out other optical-flow work.

## Current conclusion

### Native flow extent as DLSSG render input: reverted

Temporary VEYRA_TEST_FG_NATIVE_FLOW selected existing1920x1080 flowTex
instead of its nearest-neighbour3840x2160 baseFlow expansion. Create
RenderWidth/Height and motion/depth subrects were1920x1080; backbuffer and
generated output stayed3840x2160. Motion normalization used native flow
dimensions. Constant depth and zero-motion fallback used matching subrects
of their existing larger textures. NR, NVOF computation, count/index/frame
ID and scheduling were unchanged. Kept the original baseFlow adaptation
dispatch to isolate SDK input behavior first; this experiment therefore
does not measure savings from eliminating that dispatch.

Create succeeded with logged internal1920x1080. Sequential30s runs exited0
(minimumTargetRatio0: lifecycle only), watchdog95s:

| Run | Retained seconds | Submit/s | P95/P99/max ms | Gaps >10ms | Discards |
| --- | ---: | ---: | --- | ---: | ---: |
| native-flow6 | 5.631 | 268.319 | 15.650/16.951/18.129 | 103 | 1 |
| native-flow-control6 | 5.268 | 295.364 | 4.779/16.861/17.263 | 68 | 0 |

FG evaluation sum averaged9.9624 versus9.6768ms. This run supplies no
performance benefit; reverted all four source/header edits and the hook.
No visual equivalence claim: SDK acceptance and generation validity alone
do not prove motion/occlusion quality. No image quality acceptance was run
because performance already failed. The cause of the regression is not
isolated from run-to-run GPU variation; do not claim low-resolution guidance
is universally slower. Product behavior remains the prior full-size input.

Evidence under tests/fg-cadence-repair-20260920/native-flow{6,-control6},
adjacent JSON/stdout/stderr; native-flow{-restored}-build.log under the
matching logs directory. Rebuilt the sustained executable after rollback.

### Graph queue priority: reverted

Tested D3D12_COMMAND_QUEUE_PRIORITY_HIGH on the graph direct queue only,
with a temporary VEYRA_TEST_GRAPH_HIGH_PRIORITY hook. The independent
presentation queue stayed unchanged. No NR, resource lifetime, admission,
multiplier, output quality or waiting policy changed. The high-run log
confirms the requested priority and successful queue creation.

Sequential30s same-config runs, watchdog95s, lifecycle threshold0:

| Run | Retained seconds | Submit/s | P95/P99/max ms | Gaps >10ms | Discards |
| --- | ---: | ---: | --- | ---: | ---: |
| priority-high6 | 5.211 | 299.550 | 4.489/16.837/17.319 | 63 | 0 |
| priority-normal6 | 5.223 | 298.664 | 4.453/16.880/17.362 | 64 | 0 |

Paired stage sum averages17.5890/17.6117ms. No meaningful throughput or
cadence improvement. Removed the hook and restored NORMAL. This rejects
priority elevation as a fix in this workload, not all possible contention
under other applications. Both tests exit0 proves lifecycle only; fixed6
cadence remains unaccepted. Evidence: tests/fg-cadence-repair-20260920/
priority-{high6,normal6}/{engine.log,frame-trace.txt}, adjacent JSON and
stdout/stderr files. Build logs queue-priority-build.log and
queue-priority-restored-build.log use the existing isolated build path.

### Same-input serial work audit

Reanalyzed existing retained traces; no new GPU run or product change.
The analyzer now sums Color + Flow + NR + Residual + FgBatch for the SAME
input, including measured SR/HDR if present, never adding nested Fg1..5
again. This configuration has SR bypassed and HDR off. Missing base stages
exclude a sample. These sums omit uninstrumented gaps and presentation;
they are not end-to-end latency or a limit on a future concurrent design.

| Baseline | Full groups | Mean / P95 stage sum ms | Groups over 16.667ms |
| --- | ---: | --- | ---: |
| restored-normal6 | 248 | 17.7113 / 18.5745 | 237 |
| paired-control6b | 251 | 17.5221 / 18.3311 | 226 |

In restored-normal6, FG evaluations average9.7288ms and their inter-call
gaps total0.0830ms. Eliminating those gaps alone cannot cover the measured
1.04ms average stage-budget deficit, even before omitted costs. The current
serial path has measured throughput pressure; admission overestimation alone
is not an adequate explanation. This does NOT prove hardware saturation or
exclude better dependencies/provider execution. Prior naive queue splits
remain failed experiments, not a solution. NR stays unchanged.

Evidence: adjacent restored-normal6-cost-audit.json and
paired-control6b-cost-audit.json in tests/fg-cadence-repair-20260920.
Synthetic analyzer checks passed: nested FG timing is counted once, and
missing NR samples are excluded. Fixed6 acceptance remains open.

These three experiments did not establish a cadence fix. Product code is
restored to7a3dfae. Its explicit multiplier selection and safe per-subframe
status readback remain. Fixed6 still fails the342/s and no-source-period-hole
criteria. Do not call this completion or promise physically smooth output.

Next work should target measured critical-path overlap or provider execution
cost, first proving resource ownership and queue dependencies. A separate
presentation queue already exists; proposing it again is not a new direction.
Prior naive FG queue splits failed. Any revisit needs a different, explicit
resource/fence design and GPU timing evidence before promotion. No NR changes,
extra hold latency or automatic multiplier fallback are authorized.

## Reproduction and evidence

Final restored build non-nr-final-build.log succeeded for main, sustained,
worker and UI targets. non-nr-final-cpu.log and non-nr-final-ui.log exit0.
Thirty-second regressions with minimumTargetRatio0.95 both exit0:

| Run | Retained seconds | Submit/s | P95/P99/max ms | Retained discards |
| --- | ---: | ---: | --- | ---: |
| non-nr-final2 | 9.753 | 119.861 | 8.765/8.949/19.017 | 1 |
| non-nr-final4 | 6.200 | 239.983 | 4.580/4.721/5.027 | 0 |

2X includes one warmup and one gap over16.667ms, so its near-target average
does not prove absence of stalls.4X final window has no gap over10ms.
Earlier30s totals include startup/recovery; do not extend final-window
claims to the entire run. No test hooks are present in the restored code.

All artifacts use E:/项目/Veyra/:

- Build: build/slider-reset-20260919, reused dependency CMakeCache.txt.
- Logs: logs/fg-cadence-repair-20260920/{paired-cost,skip-clear,copy-flow}-build.log.
- CPU: logs/fg-cadence-repair-20260920/paired-cost-cpu.log.
- Tests: tests/fg-cadence-repair-20260920/<run>/{engine.log,frame-trace.txt}
  and adjacent <run>.json, <run>.stdout.log, <run>.stderr.log.
- Process TEMP/TMP: tmp/fg-cadence-repair-20260920.

Commands: scripts/build-isolated.ps1 with explicit build/dependency/temp
paths, DisplayVersion1.4.3, target veyra_fg_sustained_tests (worker target
also built for paired/clear); scripts/run-short-test.ps1 with arguments
p001.mp4,<run-directory>,6,on,30,on,0,file-4k; VEYRA_TEST_TRACE_SUBFRAMES=1
and only the relevant experiment hook. Analysis:
python scripts/acceptance/analyze-fg-cadence.py <run>/frame-trace.txt --trace
--output <run>.json. No build overlaps any GPU test.
