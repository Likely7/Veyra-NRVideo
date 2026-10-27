# Frame-generation pipeline reassessment

> Superseded execution order: [full-chain review, 2026-09-22](../plans/FULL_CHAIN_REGRESSION_REPAIR_PLAN_2026-09-22.md).
> This document preserves historical evidence. Its overlap/owner-splitting suggestions
> are not current work items; rejected experiments must not be repeated as new fixes.

## Scope

User cancelled further Magpie binary benchmarking and requested architectural
diagnosis and remedies. This is source review using existing evidence, not
implementation or new performance/physical-display acceptance.

Veyra: e71d715, codex/fg-utilization-20260921, worktree
E:/项目/Veyra/worktrees/playback-nr-20260920. Existing dirty overlap experiments
are preserved and are not accepted fixes. The desktop checkout is older.
Magpie: clean SAOG0721/Magpie checkout at
3841698348bfb246623d4acf791984c8b68a577b under
E:/项目/Veyra/downloads/magpie-six-issue-audit-20260920. Equivalence with the
user's installed executable has not been established.

## First principles and Magpie findings

Smooth output requires correct temporal image positions, regular delivery,
and continuous fresh source coverage. Average FPS alone proves none of these.
60 FPS input gives a 16.667 ms source period; ideal 4X/6X output spacing is
4.167/2.778 ms. Bursts separated by holes can report high average throughput.
Five DLSS evaluations consume real work. Sustained critical-path cost above
the source period cannot be fixed just by adjusting deadlines. Coarse GPU busy
percentage neither proves spare critical resources nor excludes stalls.

Magpie DLSS is D3D11 captured input/effects -> shared input/fence -> D3D12
NGX evaluations -> shared output slots -> frontend FIFO/deadlines -> present.
DLSSFrameGenerator waits on a CPU fence per generated image before reuse;
Renderer also waits for shared-copy completion. It is not wait-free or a single
inexpensive 4X call. Frontend retries deadlines/capacity and re-anchors after
long stalls. A full ring backpressures capture, potentially costing source
acceptance and frame age. Stable low latency is not proved.

FramePacingOptions::ResolvePresentationFrameRate uses refreshRate/multiplier
when the requested limit is zero; Renderer uses it as a base limit when sync
is enabled. At 100 Hz and 4X this branch yields 25 base FPS. This is a possible
explanation for a 25 FPS observation, not proof of the user's active settings.

Magpie's patched XeSS path estimates accepted source cadence from source IDs
and timestamps, a nine-sample median, discontinuity resets, and a fallback
subtracting measured extra wait. This distinction is worth investigating;
it is not proof that Magpie output is stable or superior.

## Problems and remedies

### 1. Heavy XeSS 4X regression: confirmed

Existing matched true 1080p-to-4K Video SR + NR comparison: source58.51 ->
39.84 FPS versus 1.4.3; SDK output96.39 ->129.79 FPS. The old version repeatedly
suppressed generation. Restoring suppression is not a successful 4X repair.
Native-input XeSS4 sustained source60/SDK240 in both versions.

Current diagnostic pacing bypass reached source51.98/SDK183.61 versus normal
39.84/129.79, GPU busy94.81% versus72.73%. Pacing/backpressure has meaningful
cost, but bypass is unaccepted because delivery remains unproved. Veyra passes
frameRenderTime=0 to avoid feeding its own wait back; zero alone is not an API
violation. Next, align source ID/PTS, XeLL/Present entry/exit, resource
retirement and all subframe/boundary intervals. Verify whether blocked input
delivery inflates the provider's period estimate and which clock its parameter
represents. Only then try source-aware timing with resets. Do not feed nominal
16.667 ms across source skips or blindly remove pacing.

### 2. DLSS high output still has long holes: confirmed

Native NR DLSS6 submitted272.03 ->268.23 FPS old/current, with gap P99 about
16.9 ms in both. True SR + NR currently submitted about134/138 FPS at4X/6X.
These counters describe application submissions, not physical scanout.

FgRecoveryBudget::admitFile checks first and last deadlines for a group.
EnhanceGraph tracks skipped FG history and may reseed with one evaluation.
Admission/recovery can leave interpolation holes; their measured share versus
late discard/GPU waits still needs per-group attribution. Link every hole to
rejection reason, estimated/actual GPU cost, fence readiness and warmup
outcome. Fix demonstrated estimation/rejection errors; preserve discontinuity
resets and per-output validity/fence ownership. Do not force late groups into
the queue or silently reduce the multiplier.

### 3. Input and presentation coupling: structural risk, not proven root cause

EngineController services source reads and presentation progress on the same
owner thread with bounded occupancy/resource gates. Blocking reads or provider
Present can delay the other responsibility. Trace the actual blocked boundary
before moving it to bounded asynchronous handoff. Seek, resize, provider switch
and shutdown must retire resources correctly. Do not add unbounded queues.

### 4. Real GPU cost and overlap: limited remaining opportunity

Historical serial measurements were17.5-17.7 ms/input, including about9.73 ms
for five FG evaluations in those runs. Removing roughly0.083 ms inter-call gaps
cannot recover the whole deficit. A separate DLSS present queue exists. The
opt-in NVOF/Video SR overlap experiment is off and unaccepted. Only overlap
independent work with proved resource/fence lifetimes. Larger rings, priority,
blind queue splitting and fixed20/35/50 ms holds were tried; consult
FG_NON_NR_EXPERIMENTS_2026-09-20.md before repeating failed directions.

### 5. Motion quality and diagnostic coverage

Regular submissions cannot cure wrong motion scale, frame identity, history or
occlusion. Check source/previous texture identity, motion extent and temporal
position before blaming sharpening for XeSS wobble. Acceptance must include
source coverage, frame age and whole-stream interval tails. Hook-return
intervals are not GPU execution or scanout; in-burst averages omit boundary
holes.

### 6. NR anti-flicker is not independent denoising

Temporal stabilization targets NR residual changes and defaults off. It is not
a completed independent denoiser. The old42 ms statistic is not added
anti-flicker delay; see NR_FG_FOLLOWUP_ACCEPTANCE_2026-09-21.md. The attempted
NvVFX path was unavailable in the tested package. A working GPU-only provider
and quality/ghosting validation remain necessary. Keep this separate from FG
scheduling. NR multilayer remains deferred.

## Order and acceptance

1. XeSS heavy-load timing/backpressure attribution, one isolated remedy.
2. DLSS per-group hole attribution and demonstrated estimation/continuity fixes.
3. CPU blocking isolation or independent GPU overlap only if evidence warrants.
4. Motion-quality validation and independent denoising as separate work.

Checkpoint before implementation. Keep media, resolution, model quality and
multiplier fixed. Require repeatable whole-stream cadence/source-coverage
improvement without increased frame age, visual regression or lifecycle
failure. Revert failed experiments and record them. No artificial delay,
hidden fallback, or endless attempts to exceed sustained hardware capacity.

Existing numerical evidence and artifact locations are documented in
RELEASE_143_SCHEDULING_COMPARISON_2026-09-21.md.
