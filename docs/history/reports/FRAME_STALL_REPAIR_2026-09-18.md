# Frame stall repair, 2026-09-18

Status: two scoped fixes implemented and validated. The NR runtime spike,
sporadic CPU-tail stall and fan's steady HDR stall remain open; not a full cure.
Baseline: c7464bb; checkpoint/pre-stall-repair-20260918.
Branch: codex/frame-pacing-20260918. No merge, publication or package.

## Scope and acceptance

1. Reduce the reproduced XeSS ResizeBuffers stall without changing capture
   format, source resolution, NR quality or requested FG multiplier. Preserve
   GPU lifetime waits and final window geometry.
2. Inspect expired generated-frame waits that can hold a ready original behind
   an obsolete output. Only preview scheduling may skip expired generated frames;
   preserve fences, leases, counters and chronological surviving output.
3. Verify with VC-007PRO 3840x2160 NV12 29.97fps, NR + XeSS2X/native and
   DLSS4X/realtime, using the existing repeatable resize probe and steady runs.
   SDK success is not proof of physical scanout or elimination of all stutters.
4. Record rejected experiments and remaining uncertainty. The supplied fan's
   1.4-second steady HDR stall is not reproduced locally and has no proven cause.

Outputs: E:/项目/Veyra/{tests,logs,tmp}/frame-stall-repair-20260918;
build: E:/项目/Veyra/build/frame-pacing-20260918.

## Implemented changes

- `VideoPresenter` retains XeSS buffers at least as large as the current monitor.
  Ordinary window resize does not shrink/rebuild provider resources. A larger
  client or monitor can still grow them with the existing GPU drains. This is
  intentionally XeSS-only. It consumes more output work/memory in small windows.
- `PresentationGeometry.h` maps the client's fit/zoom/pan rectangle into retained
  buffer coordinates, matching PresentBlit. Empty/offscreen regions cannot tag an
  out-of-bounds rectangle. Client geometry changes reset interpolation history.
- `PreviewFrameReadiness.h` rejects obsolete/suppressed/invalid generated preview
  output before waiting on its producer fence. Original frames still require
  their fence. Completion watchers retain GPU leases and resolve generation even
  when a presentation job has skipped its output; export is unchanged.
- Engine long-frame diagnostics separate capture metrics, GPU metrics, timestamp
  collection, flow aggregation, snapshot publication and periodic logging. The
  engine threshold is 45ms; other traces retain 80ms. Wall time includes OS
  descheduling. Capture age logs also report deadline, lateness, wait and Present.
- `EnhanceGraph` records CPU stages only for calls over30ms, separating upload,
  NVOF Execute, NR command acquisition/parameter setup/Evaluate and FG Evaluate.
  This is CPU call duration; GPU timestamps remain separate.

## Evidence so far

Same VC-007PRO 3840x2160 NV12 29.97Hz input, RTX5070 / driver616.56,
SDR2560x1440@100Hz, window1280x760, muted WASAPI, SR/sync off. Each run has
10s warmup. XeSS2X uses native4K NR; DLSS4X uses the same realtime NR policy as
the baseline. Different NR policies/backends are not an equal-cost comparison.

| Run directory | Formal seconds | Sample P50/P95 ms | Capture drops |
| --- | ---: | ---: | ---: |
| baseline `frame-stall-20260918/xess2-native-resize40` | 40 | 45.624 / 50.448 | 9 |
| `xess2-drain-experiment40` (rejected) | 40 | 46.261 / 50.740 | 9 |
| `xess2-retained40` | 40 | 48.372 / 54.984 | 0 |
| `xess2-final-resize40` | 40 | 49.907 / 57.260 | 0 |
| `dlss4-final120` | 120 | 40.223 / 40.653 | 10 |
| `dlss4-tail120` | 120 | 40.206 / 40.572 | 0 |
| `xess2-tail120` | 120 | 48.736 / 55.666 | 0 |
| `xess2-graph40` (resize) | 40 | 48.148 / 53.089 | 0 |
| `xess4-final-resize40` (realtime NR) | 40 | 35.097 / 35.544 | 0 |

The three resizes occur at formal10/20/30s. Baseline XeSS ResizeBuffers took
115.439/103.269/102.315ms while queue drain was only about0.04ms. A rejected
250ms debounce plus three disabled-generation frames still lost9 frames. That
experiment is removed from source. Retained-buffer runs had no ordinary
ResizeBuffers calls and no capture drops. Median latency increased2.748/4.283ms
against the resize baseline, so this is a stutter/resize fix, not a claim that
all XeSS latency decreased. Startup GPU/readiness spikes remain in the logs.

The first final DLSS run contained a separate390.077ms engine iteration at
source3087:387.874ms was in the previously unsplit CPU tail; GPU readiness was
observed at10.120ms, GPU span14.326ms, Present0.368ms, callback-to-return424.911ms.
It lost10 capture frames and expired3 generated outputs. The50ms snapshot max
was only42.961ms: it missed the transient. Never use that sampled maximum as the
all-frame maximum. Added diagnostics do not themselves fix this unknown stall.
The subsequent `dlss4-tail120` run completed120.049s,3597 received,zero dropped,
NR/FG continuously active, no45ms engine or80ms capture-age event. Non-recurrence
does not establish the cause or prove a fix.

XeSS native4K NR `xess2-tail120` completed120.047s,3597 received/processed,
zero capture drops,NR/FG continuously active. The startup spike was narrowed
with `xess2-graph40`, then `xess2-nr-trace10`: frame304 graph CPU50.215ms,
NR Evaluate49.060ms,NR acquisition0.030ms,parameters0.012ms,upload0.560ms,
NVOF Execute0.180ms. Graph GPU span66.868ms versus about26ms normally. This
identifies the synchronous NR runtime call, not its internal reason (which may
include driver work or OS preemption). Do not label it an XeSS pacing defect or
claim it is fixed. No matching304-frame/10s periodic work was found in the
investigated caller.
The short diagnostic run also completed cleanly with299 formal inputs/no drops.

The final XeSS4X resize run completed40.0165s,1198 inputs,no capture drops and
continuous NR/FG. It also recorded NR Evaluate49.802ms at frame304 in startup.
The final DLSS recovery run recorded48.491ms in the same NR call at frame304.
Thus this approximately49ms event occurs with both providers and both NR sizes;
it is not specific to native4K XeSS. No change to that runtime is claimed.

## Verification commands

Run from this worktree unless indicated otherwise. Each hardware run is serial,
with a200s process timeout (below the300s single-test limit).

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory 'E:/项目/Veyra/build/frame-pacing-20260918' -DependencyCache 'E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-repair-20260918' -DisplayVersion 1.4.2beta -Targets veyra_capture_latency_tests,veyra,veyra_preview_geometry_tests,veyra_presentation_worker_tests,veyra_presentation_pacing_tests
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name xess2-final-resize40 -Backend xess -Multiplier 2 -Nr native -Resize -Seconds 40 -Output 'E:/项目/Veyra/tests/frame-stall-repair-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-repair-20260918'
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name dlss4-tail120 -Output 'E:/项目/Veyra/tests/frame-stall-repair-20260918' -TempDirectory 'E:/项目/Veyra/tmp/frame-stall-repair-20260918'
```

Build logs: `build-final.log` initially failed on Windows min/max macros in the
expanded unit-test includes; target-local NOMINMAX/WIN32_LEAN_AND_MEAN fixed it.
`build-final-retry.log` and `build-tail.log` passed. Unit suite56 checks passed;
GPU geometry readback passed fit/zoom/pan/reference/reset and retained-buffer
square-client/zoom cases. Geometry readback verifies blit coordinates, not XeSS
interpolated image quality. Test logs: `unit.log`, `geometry.log` under the task
logs directory; images under the task tests directory `geometry`.

Subsequent diagnostic builds `build-graph-trace.log` and `build-nr-trace.log`
passed. All listed completed hardware runs returned0. NR CreateFeature18 and
DLSSG Create returned0x1,non-null handles,seh0; XeSS Create/Init/XeLL returned0.
Successful Evaluate is evidenced by growing NR/FG counters and no execution
failure/degradation; SDK success does not imply stutter-free rendering.

Final `build-verified.log` passed (exit0) after increasing CPU trace capacity to32
marks so6X subframe diagnostics retain the final stages. This last change only
affects trace storage; the hardware results above precede that capacity increase.
Final inspection found no running Veyra test processes and an empty task tmp
directory. Main's working tree is clean; fixes remain on the isolated branch.

Final recovery command from the build directory, per-process TEMP/TMP set to
the task tmp directory, via hidden Start-Process with a180s timeout:

```powershell
./veyra_presentation_pacing_tests.exe ignored 'E:/项目/Veyra/tests/frame-stall-repair-20260918/dlss4-recovery' -1 2 4 3 capture-nr-recovery
```

Exit0: baseline89 capture frames/no drops; injected80ms owner-thread work for3s
produced reported mailbox drops;12s recovery restored NR/FG without sustained
budget limiting or accumulated latency (P95 below80ms);pause/resume and2X/6X/4X
reconfiguration passed,clean stop. This tests actual DLSS GPU resource lifecycle
and recovery in addition to the unit decision tests. Full release/export gate
was not run; this is scoped preview acceptance, not a release certification.

Analysis:

```powershell
python scripts/acceptance/analyze-capture-version-comparison.py 'E:/项目/Veyra/tests/frame-stall-repair-20260918' xess2-drain-experiment40 xess2-retained40 xess2-final-resize40 dlss4-final120 dlss4-tail120 xess2-tail120 xess2-graph40 xess2-nr-trace10 xess4-final-resize40
```

`comparison.json` holds sampled statistics plus whole-run slow events separately.
The original logs are preserved, including failed experiments and the390ms
stall. Each run's `run.json` records arguments and test executable hash.

## Limits and next evidence

The fan's RTX5080 / ASUS4KPRO / P010 HLG-to-PQ native4K XeSS2X steady1.4s
present-wrapper stall has not been reproduced or fixed. That original wrapper
combines SDK markers, DXGI Present, buffer-index and SDK status calls; low GPU
utilization alone cannot identify its cause. The current build has split those
calls. Need a log from the original HDR setup to attribute the long call.

Local measurements are capture callback to original Present return, not HDMI
input to physical scanout. XeSS can present asynchronously after return. HDR,
multi-monitor, actual RTX30/40, FSR rendering, scanout cadence and visual XeSS
interpolation quality were not validated in this repair. No runtime changed,
no main merge, push, release, or beta package replacement.
