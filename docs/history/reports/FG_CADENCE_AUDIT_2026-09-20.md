# DLSS 6X cadence investigation, 2026-09-20

## Checkpoint and scope

User requested a Git checkpoint before continuing investigation of roughly 200 reported FPS feeling like 30-40 FPS. Saved prior work as `b2c3d0e`, tag `checkpoint/pre-fg-cadence-audit-20260920`; investigation branch `codex/fg-cadence-audit-20260920`. No product scheduling change, package replacement, push or release in this investigation.

## Reproduction

RTX5070, physical `capture:0:14:0:0`, NV12 2560x1440 nominal 60 Hz (59.94 actual), DLSS SR to 3840x2160, original NR at realtime 1920x1080, NVOF Performance, DLSS 6X at 4K, SR -> NR -> residual -> FG. Window 1795x816; pacing/HDR/VSync off, tearing on. The `live-1440p` harness disables injected stalls. User's main app was closed. Each test ran 45 seconds sequentially.

Build: `scripts/build-isolated.ps1 -Root <repo> -BuildDirectory E:/项目/Veyra/build/slider-reset-20260919 -DependencyCache E:/项目/Veyra/build/slider-reset-20260919/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/dlss-recovery-20260920 -Targets veyra_fg_sustained_tests -DisplayVersion 1.4.3`, succeeded. Log: `E:/项目/Veyra/logs/fg-cadence-audit-build-20260920.log`.

Run through `scripts/run-short-test.ps1`, exe in the build directory above, arguments `capture:0:14:0:0 <output> 6 on 45 on 0 live-1440p`, timeout 100 seconds. Output directories `E:/项目/Veyra/tests/fg-cadence-audit-20260920/{verbose,memory}`, matching `.stdout.log` / `.stderr.log` prefixes. CWD and process TEMP/TMP used existing E-drive dlss-recovery test/tmp directories. Both tests exited 0. Target ratio 0 means no throughput assertion; `throughput=1` is not proof of 360 FPS.

The verbose run enabled `VEYRA_VERBOSE_FRAME_LOGS=1` only in its child shell. The second run disabled verbose file logging and dumped the existing bounded 8192-event memory trace after stopping the engine. Thus disk logging is not necessary to reproduce the uneven submissions.

Analyze using `python scripts/acceptance/analyze-fg-cadence.py <input> --output <json>`, adding `--trace` for `memory/frame-trace.txt`. Inputs and JSON reports are under the test root above; verbose input is `verbose/engine.log`.

## Measurements

All times below describe application submission, not physical scanout or distinct visible images.

| Metric | Verbose, first 10 seconds excluded | Memory trace, retained final window |
| --- | ---: | ---: |
| Window | 35.17 s | 8.068 s |
| Submit FPS | 194.96 | 194.09 |
| Interval P50 | 2.116 ms | 2.169 ms |
| Interval P95 | 16.787 ms | 16.704 ms |
| Interval P99 | 19.324 ms | 19.117 ms |
| Maximum interval | 45.863 ms | 27.797 ms |
| Intervals below 1 ms | 735 | 156 |
| Intervals above 16.667 ms | 418 | 90 |
| Intervals above 25 ms | 46 | 5 |
| Present CPU P95 | 0.345 ms | 0.333 ms |

The memory window contains 478 interior presented batches (boundary batches excluded). 233 contained only the original frame: 114 were budget-rejected batches, 119 were single-call history warmups. Only 156 batches presented all six frames. Consecutive presented-batch transitions include 114 rejected -> warmup, 119 warmup -> steady, 113 steady -> rejected, and five steady -> warmup. The rejected/recovery cycle is directly observed, not inferred solely from an average counter.

## Supported mechanism and limits

1. `EnhanceGraph.cpp`: rejection sets `fgHistorySkipped_`; the next admitted batch resets/warms history. Repeated rejection therefore removes intermediate images from consecutive intervals. The earlier single-call warmup fix reduces wasted calls but does not remove this cycle.
2. `EngineController.cpp` presentation loop allows generated frames up to 10 ms late. With pacing off, already-due frames can be submitted consecutively. A 6X subframe interval is about 2.78 ms here, so this tolerance can contain several missed slots. Short bursts raise average submission FPS without repairing preceding gaps.
3. The graph and DLSS presentation share an owner thread. Verbose long-gap overlap with graph CPU submission is only about 24%; it would be incorrect to blame every gap on graph CPU work or infer GPU idleness from the rest. GPU readiness and scheduled waiting must also be distinguished.

Example in verbose trace: source 625 was skipped; source 626 reset and presented only its original. It was observed ready about 17.42 ms before presentation; the preceding submission gap was 27.73 ms. Source 627 then had a full generated group. This demonstrates a mixed reset/schedule event, not proof that all waiting can safely be removed.

PresentMon 2.5.1 failed to start ETW with access denied (exit 1). No privilege/group changes were made. Actual display cadence, distinct image count and the user's subjective 30-40 FPS remain unmeasured. Present CPU returning quickly does not prove frames appeared on screen. These measurements also do not prove sufficient GPU capacity for sustained 4K 6X.

## Next repair and acceptance

- Make admission and recovery decisions coherent across a complete warmup-plus-valid-pair sequence; inspect deadline phase and whole-batch completion estimates. Do not repeatedly admit warmup then reject useful work because the two have different costs.
- Separate original enhancement, per-subframe readiness and presentation opportunities in diagnostics before changing scheduler ownership. Any decoupling must preserve bounded queues, GPU resource lifetime and monotonic media time.
- Define an explicit late-frame policy that avoids catch-up bursts without silently enabling user-disabled pacing. Compare fewer evenly spaced valid frames against burst submission; do not replace them with repeated images to inflate FPS.
- Keep real resets and deadline protection. Previous same-parameter removal of admission reduced throughput and increased capture loss/expired generated frames; it is not a demonstrated fix.
- Re-run matched settings with verbose logging off. Acceptance must improve long-gap distribution and rejected/warmup cycling, not only mean FPS; also monitor source loss, original-frame latency, PTS ordering, resource lifetime and 2X/4X/6X recovery. Physical display validation remains separate and requires usable display-event capture or external measurement.

Current status: software cadence defect reproduced and narrowed down; not repaired or accepted as smooth playback.
