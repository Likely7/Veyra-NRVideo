# Fixed-media DLSS cadence repair

本轮继续施工前的深度排查合同见
[`FG_CADENCE_DEEP_INVESTIGATION_PLAN_2026-09-20.md`](FG_CADENCE_DEEP_INVESTIGATION_PLAN_2026-09-20.md)。
该计划把固定 6X 的计算服务时间、admission/recovery、呈现队头阻塞和资源等待分开验收；
自动降档只保留为明确标注的 fallback，不构成固定 6X 完成。

Continuation: affordable same-input reseeding removes a second lost group
after budget rejection. Fixed6 now measures about280 submissions/s, still
with15ms holes; acceptance remains open. Fallback is not the requested fix.

## Next experiment: overlap enhancement and interpolation

2026-09-20 result: producer-fence prerequisite saved as1b955b5. The explicit
independent-queue feasibility run async6 failed performance acceptance
(58.764 retained submissions/s,1629 expired generated frames). Its code was
removed; serial processing remains. See WORKLOG for implementation boundaries
and timings. Do not repeat this same queue split as an untested proposal.

The retained trace attributes every >10ms gap to a rejected interpolation
group. Test whether NR for input N+1 can overlap FG for input N, instead of
assuming the current serial stage sum is an immutable hardware limit.

1. Carry producer fence identity with each lease and with non-published
   warmup completion. Poll and presentation must use that object's timeline.
   Keep current single graph queue until this prerequisite passes regression.
2. An isolated FG queue needs its own ring/fence/event and drain lifecycle.
   Snapshot motion per parity before handing off; depth is currently immutable.
   No queue may signal another queue's monotonic completion fence.
   The first experiment must be explicitly requested by the realtime engine,
   not enabled globally from inside the graph: player_probe and existing
   export callers still consume numeric fence values on the original queue.
3. Protect video input and generated/status output reuse, including seed-only
   evaluations with no generated leases. Real-frame presentation must wait
   for FG's read of its texture before changing that texture's state.
4. Split timestamp collection by producer queue. Do not subtract timestamps
   from different clocks or treat overlapping sums as elapsed execution time.
   File budget and queued-work prediction need a dependency model before
   promotion; keep any initial feasibility path explicitly experimental.
5. Compare fixed6 same-media cadence, actual GPU overlap and latency against
   checkpoint/fg-same-input-seed-20260920. Validate reset/seek/resize/stop,
   2X/4X/6X, seeds, exports and non-DLSS paths before enabling normally.
   Separate queues alone do not prove concurrent execution or a speedup.

Latest continuation: prefix admission, carried queue prediction and minimum
file output spacing are now enabled normally; command allocator budget16,
two-job capacity unchanged. Experimental admission/spacing flags removed.
The historical default6/opt-in statements below describe earlier experiments.
See FG_CADENCE_REPAIR_ACCEPTANCE_2026-09-20.md for current measured results
and remaining fixed6X acceptance. No claim of a completed fixed6X repair.

User authorized repair, checkpointing validated improvements, testing p001.mp4 with NR / 4K target / DLSS 6X and comparison to 2X, then shutdown after reporting. Starting checkpoint: dad189a, tag checkpoint/pre-fg-cadence-repair-20260920. Work remains on codex/fg-cadence-audit-20260920; no publish authorization.

Media: E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4. ffprobe reports H.264 yuv420p 3840x2160 60/1, AAC, 333.764 seconds. A 4K target does not upscale an already 4K source; report actual graph sizes and enabled stages rather than claim SR execution from its UI flag.

1. Baseline matched file playback 2X/6X and bounded live replay as needed. Use original NR realtime policy and fixed 4K target, same display size, pacing disabled. Never interpret software FPS as scanout.
2. Inspect submission, temporal recovery and deadline models; fix evidenced scheduling defects. Preserve real resets, bounded queues, audio speed and export behavior. Investigate increasing command slots or scheduling changes only with measured evidence and resource lifetime checks.
3. Compare stable cadence, source continuity, valid generated frames, PTS, latency and CPU/GPU timing, not only average FPS. If 6X exceeds compute capacity, prioritize sustainable even output and retain requested quality/multiplier semantics honestly.
4. Build main program and affected tests; run CPU policy tests, GPU reset/admission regression and fixed-media playback. Each run <=300 seconds. Commit verified improvements individually. Record failures and reverted experiments.
5. Write final report with rollback points, available build path and unmeasured display/hardware boundaries, then request Windows shutdown as authorized.

New outputs: E:/项目/Veyra/tests/fg-cadence-repair-20260920, logs/fg-cadence-repair-20260920, tmp/fg-cadence-repair-20260920. Reuse build/slider-reset-20260919; no source-tree artifacts.

## Implementation and rejected experiments

- File presentation now resolves each frame's own fence and interpolation status, as capture already does, instead of waiting for the entire generated batch.
- Moving DLSS preview measures base GPU work and cost per generated frame over 60 samples. After 30 samples, persistent overload lowers the effective multiplier (15 confirmations); sustained headroom restores it (120 confirmations). Down/up headroom thresholds are 90%/82% of the source interval. The saved request remains unchanged and UI reports the scheduled multiplier separately. Unknown cost preserves the request. This is a measured-cost heuristic, not a guarantee of scanout cadence.
- The graph uses the effective number for NGX evaluations and interpolation PTS. Queued batches keep their own size for pacing. Export/default graph calls retain the configured full multiplier. Provider-owned XeSS/FSR pacing is excluded from this DLSS controller.
- Combining all MFG evaluations into a single command list reduced measured output to about 72 FPS; reverted. Servicing older presentations during producer slot waits reached only about 89 FPS; reverted. No CommandSlotRing changes retained.
- XeSS is included in the active goal. Test the same media/effects at 2X and 4X, checking SDK generation counters separately from application Present calls; provider-owned subframe timing needs separate evidence.
- Final acceptance: sustained matched DLSS 6X/2X; XeSS 2X/4X; dynamic multiplier GPU validity/PTS and recovery tests; CPU policy and application lifecycle/startup. Results belong in FG_CADENCE_REPAIR_ACCEPTANCE_2026-09-20.md.

## Fixed 6X follow-up: not yet accepted

User explicitly requires fixed 6X cadence repair in addition to the fallback checkpoint 0050870. Tests below bypass the capacity controller; multiplier remains 6. None proves NR-on fixed 6X repaired.

| Run | Settings / duration | Retained submission FPS | P95 interval ms | Complete six-frame batches |
| --- | --- | --- | --- | --- |
| fixed6-slots16 | NR on, 16 command slots, 45s | 61.196 | 18.0236 | 0 |
| fixed6-nr-off | NR/SR off, original 6 slots, 30s | 360.002 | 3.3488 | 371 |
| barrier6 | NR on, avoid repeated input/depth COMMON transitions, 45s | 77.230 | 17.7609 | 0 |
| phase20-slots16 | NR on, 16 slots, file presentation phase delayed 20ms, 45s | 62.609 | 17.7917 | 0 |

Each trace is only the final bounded retention window, not the entire test. NR-off retained 6.206s, max interval 4.1553ms, no interval over16.667ms, PTS steps 2.7778ms. This demonstrates valid full6X submission with headroom; disabling NR is diagnostic, not a proposed user solution. NR-on measured GPU work is about17-18ms per60Hz input versus16.667ms budget. Enlarging the ring removed CPU slot waits but did not repair deadline losses. This does not prove all observed stalls are unavoidable compute limits.

Input/depth barrier experiment and fixed20ms phase experiment were reverted for lack of benefit. The latter also added video phase lag and is not an audio synchronization fix. Default command ring remains6. Two bounded test hooks remain: VEYRA_TEST_COMMAND_SLOTS (6..24), VEYRA_TEST_FIXED_FG_MULTIPLIER (presence bypasses automatic multiplier selection). Normal runs do not set them.

Evidence: E:/项目/Veyra/tests/fg-cadence-repair-20260920/{fixed6-slots16,fixed6-nr-off,barrier6,phase20-slots16}, corresponding stdout/stderr/JSON files; build logs slots-build.log, barrier-build.log, phase-build.log in E:/项目/Veyra/logs/fg-cadence-repair-20260920. All bounded harness runs exited0 for lifecycle only: minimumTargetRatio=0 deliberately disables a target-throughput assertion. No scanout measurement or subjective smoothness claim follows from these numbers.

Next investigation must address the overload equilibrium: admitting work by the last generated deadline permits earlier subframes to expire, and repeated reject/warmup transitions remove additional temporal coverage. A fixed phase shift cannot cure a sustained throughput deficit. Any replacement must preserve original media time/audio speed, bounded queue/latency, accurate effective multiplier reporting and reset/resource lifetime correctness. XeSS final regression and lifecycle acceptance remain pending. Goal remains active; no shutdown until final work/report is complete.

## Queue and prefix deadline experiments

All below are 30-second NR-on fixed6X runs. Metrics cover only each final retained trace window. Test-only environment switches isolate changes from normal playback. Exit0 checks lifecycle, not throughput (minimumTargetRatio=0).

| Run | Submission FPS | P95 gap ms | Complete 6-frame batches | Discarded generated frames |
| --- | ---: | ---: | ---: | ---: |
| subframe16 | 62.663 | 17.926 | 0 | 1566 |
| queue16 | 66.883 | 17.854 | 0 | 1538 |
| first16 | 203.170 | 16.799 | 114 | 663 |
| reserve16 | 101.126 | 17.046 | 88 | 0 |
| profile16 | 276.589 | 14.363 | 168 | 244 |
| carry16 | 246.569 | 16.603 | 227 | 0 |
| spacing16 | 268.191 | 15.329 | 237 | 0 |
| spacing6 | 216.506 | 15.377 | 201 | 9 |
| remaining16 | 274.567 | 15.357 | 240 | 0 |
| always16 | 58.551 | 17.947 | 0 | 1620 |

Sixteen command slots unless named spacing6 (default6). Per-frame CPU fence observations show most baseline discards precede observed readiness; this is not exact GPU completion timing. A last-subframe-only deadline admits batches whose early frames are already infeasible. Using the first deadline with the whole batch cost over-rejects; measured first-FG stage cost improves admission. Carrying predicted GPU start across retired completion watches prevents underestimating queued work. Applying 90% minimum output spacing removes most catch-up bursts.

These are not accepted fixes: spacing16 still has 52 rejected pairs followed by 52 warmups in 5.709s, and spacing6 has 94 of each in 6.651s. Both retain periodic15-17ms holes. Command slot enlargement alone was previously ineffective. Pending experiment subtracts fence-completed stages from the queued-work prediction to test whether conservative accumulated predictions cause unnecessary rejection.

Hooks: VEYRA_TEST_TRACE_SUBFRAMES, ADMISSION_QUEUE, ADMISSION_FIRST, ADMISSION_RESERVE, ADMISSION_PROFILE, SUBFRAME_SPACING and ADMISSION_REMAINING (all admission/spacing names also prefixed VEYRA_TEST_). Failed alternatives must be removed before production enablement. Artifacts use tests/fg-cadence-repair-20260920/<run> and <run>.json; builds use corresponding logs/fg-cadence-repair-20260920/*-build.log. No physical scanout or subjective smoothness result is claimed.

remaining16 deducts stages whose completion fences passed: 48 rejected/warmup pairs remain in5.612s. always16 bypasses admission entirely (VEYRA_TEST_ADMISSION_ALWAYS), retains fixed6X and minimum spacing, and demonstrates unbounded lateness is not a solution: almost all generated frames expire before observed completion. Each60Hz input requires approximately NR6.8ms + flow1.2ms + FG9.8ms plus other work on this RTX5070. These serial GPU stages exceed16.667ms, independently of CPU completion polling. This is evidence of a compute deficit at the tested quality, not an excuse for the baseline80fps collapse.

A separate code defect was found at soft-drop boundaries: incrementing presentationGeneration invalidated earlier leased A/B output when a later input was dropped. Keep resetting processing history, but increment the presentation generation only on hard boundaries. Add regression checks for drop-only, explicit-reset-plus-drop, and resize-plus-drop. This does not override expiry, resource fences, or comparison suppression; GPU regression is pending.
