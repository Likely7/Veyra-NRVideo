# NR quality and performance execution

Baseline: bcdbfbf, checkpoint/pre-nr-quality-perf-20260921.
Branch: codex/playback-nr-20260920. User authorized local implementation and
verification of research items 1/2/3/4/6, then 1.4.2 versus 1.4.3 performance.
NR layering is excluded. NVOF remains default. No publishing. User subsequently
requested shutdown after completion, evidence saved and test processes exited.

## Acceptance and bounded work

- Preserve existing effects, formats, user-selected multipliers and features.
- Do not trade image detail or fixed added latency for throughput numbers.
- Keep each accepted change in a separate Git checkpoint. Revert our own
  unsuccessful experiments and record evidence, never unrelated user changes.
- Per direction: establish baseline, one concrete implementation, targeted
  tests and one correction/retest if justified. Stop speculative tuning when
  evidence identifies a hardware limit or no benefit.
- Distinguish shader correctness, actual NVIDIA execution, software timing,
  visual acceptance and physical display latency. No claims across boundaries.

## Sequence

1. Audit current temporal NR, masks, timestamp/reset contract and upstream
   signed-residual stabilization. Implement bounded fixes with real GPU tests.
2. Investigate independent VFX denoise dependencies and GPU-only integration.
   Never substitute invented NGX quality values or CPU pixel readback. If the
   runtime/API prerequisites are unavailable, record the concrete limitation;
   do not add a nonfunctional UI or claim denoise is delivered.
3. Add regression cases for temporal output, detail, protection, HDR finite
   values, motion rejection and resets, using attributed upstream tests where
   applicable. Verify the actual product shader/pass, not only a CPU mirror.
4. Audit guidance frame identity and resource lifetime; repair demonstrated
   mismatches with focused tests, avoiding per-frame synchronous waits.
5. Evaluate source-based XeSS timing in isolation only if caller contracts
   support it. Compare against zero hint. Retain only demonstrated benefit;
   leave duplicate detection deferred unless required by a reproduced fault.
6. Compare verified 1.4.2/1.4.3 packages with identical video and parameters,
   matched runtimes where possible, source/working/output sizes, GPU timings,
   presentation intervals, skips and GPU power. Record confounders. Inspect
   source differences around any reproduced cost increase, then validate fixes.

## Evidence paths

Artifacts: E:/项目/Veyra/tests/nr-quality-perf-20260921/.
Logs: E:/项目/Veyra/logs/nr-quality-perf-20260921/.
Process temporary files: E:/项目/Veyra/tmp/nr-quality-perf-20260921/.
Build: E:/项目/Veyra/build/playback-nr-20260920/ (existing configured tree).
Video: E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4; PS5 is off.

## Progress

- Baseline clean and checkpoint created; goal active.
- Research basis: MAGPIE_NR_XESS_COMPARISON_2026-09-21.md.
- Temporal implementation is in the worktree; synthetic GPU regression log
  `temporal-gpu-retest.log` passes. Real-video/visual acceptance remains open.
- Existing release power CSVs analyzed in
  `NR_PERFORMANCE_POWER_REVIEW_2026-09-21.md`; no blanket performance
  regression demonstrated. No additional GPU run was needed for this analysis.
- Real-video 30-second temporal off/on runs exited 0, but enabled cadence
  regressed (58 versus 60 source FPS, lateness P95 46.54 versus 1.65 ms).
  Not accepted. Compare against the previous enabled implementation before
  attributing this to the new correction; retain evidence and bound tuning.

## Accepted bounded optimization

The original nine-tap temporal shader recomputed the same one-pixel-halo
observations for neighboring threads. A groupshared 10x10 tile now computes
each observation once and reuses it, preserving the original tap order,
thresholds, signed residuals and reset/protection behavior. This is a scheduling
optimization only; it adds no holdback or frame-rate cap.

Evidence: `tests/nr-quality-perf-20260921/temporal-gpu-accepted.log` passed the
product D3D12 pass with debug layer clean. The tiled shader and the prior shader
produced identical full-frame fingerprints for 41 regression frames, including
odd extents, fractional motion, resets, protection and signed HDR cases.
With the same staged application, video temporal-on improved from 58 source FPS,
46.48 ms lateness P95 and 2.014 ms residual GPU P95 to 60 FPS, 41.71 ms and
1.369 ms respectively; preview skips fell from 67 to 1. These are software
timings, not physical display latency. The run remains a bounded smoke test,
not a natural-video subjective quality acceptance.
