# Released 1.4.3 scheduling comparison

## Finding

The tested current version is not uniformly faster than released 1.4.3.
Heavy Video SR + NR + XeSS 4X has substantially worse source continuity.
DLSS 4X/6X and native-input XeSS 4X are approximately unchanged in these
matched short runs. Neither version passes stable full-rate 6X with NR and
true 1080p-to-4K Video SR. A higher generated-frame counter is not a cadence
acceptance criterion.

## Controls and scope

- GPU: local RTX 5070, driver 616.56; video only, not capture/PS5 testing.
- Released executable: releases/1.4.3/final/Veyra-1.4.3-win64-portable/Veyra.exe,
  SHA256 F2A1E407D2FE136C32771BCF25068A6F595D51CC6B4F15766964B6A358B80676.
  Source tag v1.4.3 is 3b4570e1f3f7301fcce21b829003bb8ef7854e24.
- Current branch baseline: e71d715. The first current test executable is
  839316B4F64F5B66988A0BBA4C6E1867CAF1665BA93E4C121A12D0994F9BEE9C.
  It contains an opt-in overlap experiment, OFF in this comparison. Repeat
  using the already delivered beta executable is recorded below separately.
- The delivered beta SHA256 is
  318C6B0F44ADFC3BD8094DFD5BA39B1570AA56C5D01E9A95BC40430DFD28BDA8.
- All 21 compared runtime/dependency DLLs matched by SHA256. No DLL replacement.
- Native case: p001.mp4, 3840x2160 60fps, SR off, NR internal 1920x1080.
- True SR case: matched p001-derived1080.mp4, 1920x1080 60fps, RTX Video SR
  quality 3 to 3840x2160, NR internal 1920x1080. This is extra SR computation,
  unlike the native case; their FPS must not be directly compared as a regression.
- Temporal NR off, Performance flow, professional view, default window,
  output cap/pacing overrides off. Same options and dimensions in both logs.
- XeSS wraps a 2560x1440 swapchain; DLSS generates at 3840x2160. This is a
  same-provider version comparison, not an equal-pixel provider benchmark.
- Sequential 30-second runs. NVML sampled every 200ms, summarizing 10..29s.
  GPU percentages are device-wide activity, not SM occupancy or scanout.

Release 1.4.3 lacks the newer trace-file export. Common verbose logs are used
for both versions, not an absent old trace treated as zero output. Counter
rates use deltas in 10..29s; cadence excludes the first 10s after first Present.
The old lateness sampler repeatedly sampled stale observations, so its P95
lateness is NOT compared with the corrected current metric.

## Matched verbose results

| Same settings | 1.4.3 rate/s | Current rate/s | 1.4.3 gap P95/P99 ms | Current gap P95/P99 ms |
| --- | ---: | ---: | --- | --- |
| Native NR DLSS 6X | 272.03 | 268.23 | 16.418 / 16.934 | 16.397 / 16.899 |
| True SR + NR DLSS 4X | 135.44 | 134.42 | 16.885 / 17.063 | 16.905 / 17.099 |
| True SR + NR DLSS 6X | 139.67 | 138.05 | 16.933 / 17.125 | 16.936 / 17.127 |
| Native NR XeSS 4X, source | 60.00 | 60.00 | 17.305 / 17.598 | 17.232 / 17.550 |
| True SR + NR XeSS 4X, source | 58.51 | 39.84 | 21.447 / 30.279 | 39.637 / 40.921 |

DLSS rate and gaps describe application presentation submissions. XeSS rows
describe source processing and application Present-return gaps; the SDK's
intermediate presents are not individually represented in those gaps.
These observations cannot certify physical display smoothness or latency.

XeSS SDK-reported output in the heavy case is 96.39/s in 1.4.3 and 129.79/s
currently. That increased output coexists with worse source continuity.
Native XeSS reports 240/s in both. Mean GPU activity for heavy XeSS falls
from 79.74% to 72.73%; heavy DLSS remains approximately 92% in both versions.

A second old-version run without verbose logs reports native DLSS6 273.49/s,
SR+NR DLSS4 135.72/s, SR+NR DLSS6 137.19/s, native XeSS source60/output240,
heavy XeSS source58.72/output93.75. It supports the major findings, but does
not provide comparable per-frame gaps.

## Attribution and diagnostic isolation

1.4.3 repeatedly disables XeSS generation under load. Its heavy-case logs
contain 27 and 37 suppress/resume transitions across the two 30-second runs,
including enabled=0/framesPresented=1. It preserves source processing partly
by not doing the requested interpolation during those intervals. This is not
successful sustained 4X.

Commit dd69e1f removed XessGenerationGate. XessPresenter.cpp and
XessPacing.cpp have no source differences from v1.4.3. The gate removal and
observed enable/disable behavior explain an important workload difference;
a complete single-commit binary bisection has not been performed. Do not
revert the entire mixed UI/capture-fix commit or restore silent suppression
and describe it as a fixed 4X path.

Two additional current controls isolate the heavy XeSS path:

| Configuration | Source/s | SDK output/s | GPU mean | App gap P95/P99 ms |
| --- | ---: | ---: | ---: | --- |
| 2X, stock provider | 60.00 | 120.00 | 91.58% | 17.230 / 17.494 |
| 4X, normal pacing | 39.84 | 129.79 | 72.73% | 39.637 / 40.921 |
| 4X, diagnostic pacing disabled | 51.98 | 183.61 | 94.81% | 21.255 / 22.534 |

The last control uses the existing VEYRA_DISABLE_XESS_PACING hook. It is not
a shipping fix: unconstrained intermediate bursts are not accepted cadence.
It shows meaningful scheduling/backpressure cost, but does not demonstrate
that all remaining lost throughput is software overhead. No inference that
73% busy guarantees enough compute for full 4X is justified.

With normal pacing, the provider's scheduler is active (bypassed=0,
refused=0). Heavy-case in-burst mean gaps settle near 8.2ms, versus 4.17ms
for native NR XeSS4. The next focused investigation is the SDK scheduler's
period estimate and application Present backpressure during source skips.
Blindly setting frameRenderTime, removing pacing, adding fixed buffering,
or moving SDK work to another queue without resource-retirement proof is
not an accepted remedy.

## Evidence and reproduction

All artifacts below are rooted at E:/项目/Veyra/tests/fg-utilization-20260921:

- release143-a: original released binary, five loads, normal logs.
- release143-verbose-b: original released binary, same five loads, verbose.
- current-verbose-a: current baseline, same loads in reversed order, verbose.
- current-xess2-control and current-xess4-unpaced: isolation controls.
- release-comparison-final.json and xess-isolation.json: common-log summaries.

Commands: scripts/acceptance/fg-utilization-matrix.py with explicit --exe,
--native, --derived, --output, --temp and --cases. Set
VEYRA_VERBOSE_FRAME_LOGS=1 for the matched verbose runs. Analyze with
scripts/acceptance/compare-release-scheduling.py <run-directories> --output
<summary.json>. The result files preserve command arguments and EXE hashes.

No rollback, publication, capture acceptance, or fixed-high-multiplier
completion is claimed by this comparison.
