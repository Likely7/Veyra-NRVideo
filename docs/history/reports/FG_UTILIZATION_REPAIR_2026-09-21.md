# High-multiplier utilization investigation

> This experiment plan is historical. Further broad benchmark runs were stopped
> by the user. Follow [the current bounded repair plan](../plans/FG_STABILITY_COMPLETION_PLAN_2026-09-21.md)
> for subsequent work; existing experiments are not automatically accepted.

Authorized 2026-09-21: continue high-multiplier repair with different NR/SR/FG
combinations and GPU utilization evidence. Baseline e71d715, checkpoint
pre-fg-utilization-20260921; branch codex/fg-utilization-20260921 in the
existing playback-nr worktree. No publication or runtime replacement.

Use the local RTX 5070, p001 4K60 and its matched 1080p60 derivative.
Native 4K has SR off; derived1080 uses RTX Video SR quality 3 to 4K.
Cross NR off/on with DLSS 2X/4X/6X; compare XeSS 4X separately, because
the current provider supports at most 4X. Include no-FG controls where useful.
All cases use the same professional preview, default Performance flow,
temporal NR off initially, no frame cap/pacing override, 30-second sequential
runs with watchdogs. No compilation while GPU tests run.

Sample system NVML read-only at 200 ms: GPU busy percentage, memory-controller
busy percentage, power, clocks, temperature and VRAM. These are device-wide,
coarsely averaged indicators, not SM occupancy or proof of frame-scale idle.
Record host timestamps, skip startup in summaries and correlate with product
stage timings, admissions and the bounded per-frame trace. Never infer GPU
saturation from one utilization number or add unrelated stage P95s.

Investigate the reproduced limiting combination using measured per-input
dependencies. Read the failed-experiment record before modifications. Do not
repeat priority, speculative timing, guidance resolution, fixed holdback or
unsafe queue splits without new evidence. Keep output quality, resolution,
NR processing policy and multiplier unchanged in repair comparisons.
Save retained changes separately; revert experiments without repeatable gain.

Acceptance requires repeated cadence/coverage benefit, no extra fixed delay,
NR/SR still evaluated, no new dropped source coverage, lifecycle tests and
targeted GPU checks. Distinguish app submissions, XeSS provider throughput
and unmeasured physical scanout. A failed bounded experiment is not a fix.

Artifacts: E:/项目/Veyra/{tests,logs,tmp}/fg-utilization-20260921.
Reuse build/playback-nr-20260920 with explicit dependency/temp paths.
The baseline portable package stays unchanged.
