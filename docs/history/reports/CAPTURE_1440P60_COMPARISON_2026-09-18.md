# 1440p60 / 4K30 capture latency comparison

User requested 2560x1440 60fps against the existing 4K capture path. This is a
measurement task; product scheduling and capture implementation are unchanged.
Branch: codex/frame-pacing-20260918, product baseline 88ed81c.

## Method

VC-007PRO, NV12, RTX5070 / 616.56; NR realtime (1920x1080), DLSS4X, SR off,
frame sync off, WASAPI input muted, window1280x760, SDR2560x1440@100Hz.
Each run warms up10s then measures120s. Both runs use the same executable hash
recorded in run.json. Capture format negotiation and actual callback rate are
checked; no automatic substitute pixel format or resolution is allowed.

1440p input uses1440p FG/output; 4K input uses4K FG/output. Both NR and NVOF
run at1920x1080. This compares two requested configurations, not resolution
alone: source cadence changes from33.37ms to16.68ms as well. Display submission
rate is not physical refresh rate, and callback cadence does not prove the HDMI
source contains a different image on every frame.

Latency is capture callback to original-frame Present return. Card firmware,
upstream HDMI timing, USB/driver buffering before callback, compositor and
physical scanout are not measured. NV12 is uncompressed: no software video
codec decode step executes. GPU color conversion remains measured separately.
CSV snapshots are sampled every50ms and deduplicated by real Present counter;
P50/P95/P99 and maximum are sampled statistics, not exhaustive frame statistics.
Whole-run slow-event logs are preserved separately, including warmup.

GPU rows report the median of rolling stage means. Enhancement time merges
same-frame GPU intervals and excludes color/presentation. CPU readiness and
presentation scheduling intervals overlap GPU work and refer to different
output opportunities; they must not be added to reconstruct total latency.

## Two-minute results

Both runs exited0, retained NR/DLSS4X, passed actual callback-rate validation,
and stopped cleanly. No ERROR lines. NR CreateFeature18 and DLSSG Create
returned0x1 with non-null handles and seh0; Evaluate counters grew throughout.
Build succeeded. Shared probe SHA256:
73494BA0AE154E7C45DEF4ECBF290618297F3AD0F6FCBEED418BE86C9D27B4C3.

| Measurement | 1440p60 NV12 | 4K30 NV12 |
| --- | ---: | ---: |
| Actual input fps over120s | 59.969 | 29.9846 |
| Received / dropped during formal window | 7199 / 0 | 3599 / 0 |
| Generated during formal window | 21597 | 10797 |
| Callback to original return P50 ms | 25.355 | 40.095 |
| P95 ms | 25.707 | 40.974 |
| P99 ms | 26.002 | 41.989 |
| GPU color ms | 0.083 | 0.198 |
| GPU NVOF ms | 1.033 | 1.060 |
| GPU NR ms | 6.314 | 6.264 |
| GPU residual ms | 0.138 | 0.231 |
| GPU FG batch ms | 4.147 | 5.705 |
| GPU enhancement intervals ms | 11.632 | 13.268 |

P50 reduction14.739ms (36.76%). Enhancement intervals fall only1.636ms;
the source cadence/presentation timeline matters substantially. This does not
isolate resolution from fps, and does not measure physical screen latency.
No formal-window skipped/expired generated outputs or slot waits in either run.

Warmup is not fault-free: 1440p run lost2 capture frames, with NR Evaluate46.835ms
at frame603; 4K run recorded45.941ms at frame303 and original age81.808ms.
Both are about10s after capture starts, shifting frame number with input rate;
this supports investigating time-triggered work, not claiming an established
cause. The limited flag remained for the first0.91/0.76s of the formal windows
without new skipped outputs. All warmup evidence remains in engine.log.

## Commands and artifacts

From the isolated worktree:

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory 'E:/项目/Veyra/build/frame-pacing-20260918' -DependencyCache 'E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt' -TempDirectory 'E:/项目/Veyra/tmp/capture-1440p60-20260918' -DisplayVersion 1.4.2beta -Targets veyra_capture_latency_tests
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name dlss4-1440p60-120 -CaptureProfile 1440p60 -Output 'E:/项目/Veyra/tests/capture-1440p60-20260918' -TempDirectory 'E:/项目/Veyra/tmp/capture-1440p60-20260918'
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name dlss4-4k30-120 -CaptureProfile 4k30 -Output 'E:/项目/Veyra/tests/capture-1440p60-20260918' -TempDirectory 'E:/项目/Veyra/tmp/capture-1440p60-20260918'
python scripts/acceptance/analyze-capture-version-comparison.py 'E:/项目/Veyra/tests/capture-1440p60-20260918' dlss4-1440p60-120 dlss4-4k30-120
```

Build log: E:/项目/Veyra/logs/capture-1440p60-20260918/build.log.
Evidence: E:/项目/Veyra/tests/capture-1440p60-20260918, including per-run
engine.log, snapshots.csv, run.json, result.txt and combined comparison.json.
Temp: E:/项目/Veyra/tmp/capture-1440p60-20260918.

Only the comparison probe, its PowerShell wrapper and analysis script changed:
optional exact capture profile, allocator policy, existing verbose trace toggle,
formal-window markers, format diagnostics and actual callback-rate acceptance.
The analyzer splits original-frame timing using existing capture-present-sample
records within the markers. Existing historical two-argument invocation remains supported.
No runtime, main merge, package or release change.

## Device-buffer short tests

User then requested the specific original-ready-to-presentation interval and
whether the device-buffer selector affects it. Reconnected for each setting;
same1440p60 NV12/NR realtime/DLSS4X,10s warmup and20s formal test. Enabled
existing verbose timestamps; product code unchanged. CPU fence observation is
later than or equal to actual GPU completion, so ready-to-present is based on
observed readiness, not an exact GPU-to-host clock correlation.

| 1440p60 policy | Requested / actual allocator buffers | Callback to observed ready P50 ms | Ready to present begin P50 ms | Present call P50 ms | Callback to present end P50 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Auto | 3 / 10 | 9.331 | 15.918 | 0.196 | 25.458 |
| Minimum | 1 / 10 | 9.267 | 15.907 | 0.196 | 25.383 |
| Driver default | no suggestion / 10 | 9.534 | 15.828 | 0.209 | 25.579 |

Matched20s 4K30 auto trace:600 original records, callback-to-observed-ready
P50=10.664ms, observed-ready-to-present-begin P50=29.653ms/P95=30.447ms,
Present call P50=0.247ms, callback-to-end P50=40.606ms. 1440p60 auto wait
P95=16.611ms. All four trace runs contain no invalid readiness timestamps or
ERROR lines. 4K formal window also had zero capture drops and continuous NR/FG.
Most of the measured configuration difference is after original readiness;
fps and resolution still co-vary, so this is not an isolated fps experiment.

The4X provider generates A/B intermediate frames that precede original B.
Original B can be ready while the paced intermediate frames are still being
presented. Shorter source intervals compress that output timeline. This is
present even with the optional frame-sync feature disabled; it is not proof
that GPU idle time is waste or that presenting B immediately preserves cadence.

All three formal windows had zero capture drops, skipped/expired generation or
slot waits; NR/FG remained active. All exited0 and stopped cleanly. Auto/minimum
were not honored by the driver; the actual allocator remained10 in all modes.
Allocator capacity is not queued-frame occupancy and does not prove10 frames
of delay. Differences of0.09ms in ready-to-present across these single short
runs are not evidence of a useful policy effect. Startup NR stalls remain.

The selector negotiates DirectShow video-pin allocator capacity at connect;
it does not set Veyra's FG timeline or its capacity-one input mailbox. A device
that honors the request may change upstream latency or callback jitter. Fixed
delay before callback is excluded from this software metric; jitter can still
indirectly affect readiness and adaptive scheduling. No physical HDMI-to-screen
buffer-latency claim is possible from these timestamps.

Additional commands (same output/build/tmp arguments as above):

```powershell
./scripts/test-capture-version-comparison.ps1 -Build 'E:/项目/Veyra/build/frame-pacing-20260918' -Name buffer-auto-1440p60-20 -CaptureProfile 1440p60 -BufferMode auto -TraceFrames -Seconds 20 -Output 'E:/项目/Veyra/tests/capture-1440p60-20260918' -TempDirectory 'E:/项目/Veyra/tmp/capture-1440p60-20260918'
# Repeated with -BufferMode minimum / driver and corresponding unique names.
# trace-auto-4k30-20 uses -CaptureProfile 4k30 -BufferMode auto -TraceFrames -Seconds 20.
```

Build log: build-buffer.log. Per-frame trace count can differ by a few frames
from snapshot deltas because the50ms snapshot may lag the explicit window markers.
Final analysis command includes all six run names listed above, producing
comparison.json and logs/capture-1440p60-20260918/analysis-final.json. All test
processes exited, and the task temporary directory is empty. No new package.
Final process inspection found a user-launched portable Veyra process from
E:/App/Veyra-1.4.2beta-win64-portable, started23:02:52 local before the buffer
short tests. It was left running; its workload was not instrumented. These are
local short-run measurements, not an isolated-system microbenchmark.
