# NR / frame generation: next repair plan

> Historical execution plan. Current scope and remaining work are tracked in
> [FG stability completion plan](FG_STABILITY_COMPLETION_PLAN_2026-09-21.md).
> The goal-active statement below describes that earlier task, not a new goal.
> Latest matched heavy-XeSS regression evidence supersedes earlier broad comparisons.

## Scope and baseline

User decision: stop investigating the general 1.4.3 versus 1.4.2 overhead
report. Use the local RTX 5070 as the engineering acceptance machine; the user
will organize other-device group testing. Do not block local completion on
unavailable hardware. Local success is not a universal hardware guarantee.

Baseline: ab7979c, checkpoint/nr-temporal-verified-20260921, isolated branch
codex/playback-nr-20260920. Implementation authorized on 2026-09-21;
goal mode is active. Local changes, builds and bounded acceptance are in scope.
No publication or shutdown is requested in this turn.
NR layers remain deferred. Preserve detail, functionality, selected multiplier,
NVOF default and existing HDR/capture/UI repairs. No hidden downgrade, extra
fixed presentation holdback, resolution reduction or blur as a performance fix.

## Findings verified in code

- EngineController.cpp records lastFilePresentLateness after presenter.present
  returns as nowMs()-itemPtsMs. The loop samples abs(lateness) into a 1200-entry
  window. It is an absolute media-time deviation sampled by the engine, not
  per-generated-frame scanout latency. It can include Present backpressure;
  sampling outside the didPresent branch also needs duplicate-sample auditing.
  The recorded 41.71/43.35 ms is not proof of 42 ms added filter latency.
- NrTemporalPass.cpp uses exp(-frameMs/80). This is past-history weighting,
  not an 80 ms future wait. It can nevertheless delay changing enhancement.
- NrTemporal.hlsl now reuses neighborhood observations. Existing GPU tests
  prove equivalence on 41 synthetic frames, not natural-video quality.
- Magpie at 3841698348bfb246623d4acf791984c8b68a577b uses NvVFX
  VideoSuperRes. Official denoise quality is 8..11; 16..19 is high-bitrate
  upscaling without artifact suppression, not denoise. SDR uses D3D11/CUDA
  interop followed by stream synchronization; HDR falls back to CPU U8 staging.
  These are not NGX VSR quality values and not a suitable direct HDR port.
- Current XeSS timing/motion code previously matched released 1.4.3. The
  experimental timing feedback was reverted in dd69e1f. Throughput evidence
  does not settle jelly, warping or generated-image quality.

## A. Explain and reduce temporal-on delay first

1. Add bounded in-memory diagnostic records keyed by source ID, epoch,
   settings revision, pair/subframe and PTS. Flush after each short run.
   Record decode ready, graph submit, GPU completion observation, deadline,
   queue entry/exit and Present entry/return. GPU timestamps stay in their
   own clock domain unless explicitly calibrated. No synchronous pixel reads
   or per-pass CPU waits in playback, and no per-frame disk logging.
2. Split signed media deviation at Present entry from deviation at return;
   sample each actually submitted frame once. Keep existing counters labelled
   for historical comparison. A lower corrected statistic is not an actual
   latency improvement. Do not sum unrelated P95s to invent a total.
3. Run matched off/on/on/off tests on p001.mp4, same window, output mode,
   synchronization, NR size and XeSS 4X. Separate startup, scene cuts and
   steady sections. First repeat the SR-off case, then the user's NR+4K SR
   configuration. Test processes sequentially, 30-60 seconds per diagnostic.
4. Locate the dominant cost: late GPU completion, redundant synchronization,
   queue residence, early/late deadline, or SDK Present blocking. Only repair
   a demonstrated duplicate wait/stale deadline/avoidable dependency. Do not
   move the media clock, pause audio or drop more frames to improve a metric.
5. Retain a change only when source coverage and generated coverage do not
   regress, delay/long gaps improve on matched repeats, and reset/lifecycle
   tests pass. If GPU service time exceeds the budget, document the measured
   limit and stop that path. Do not promise zero overhead for temporal NR.

## B. Validate anti-flicker picture quality

Build a diagnostic offline export using the product pass and identical input
frames/motion. Offline readback is permitted only in this diagnostic path.
Compare temporal-off, pre-correction, corrected non-tiled and current tiled
where relevant. Keep initial histories, PTS and scene boundaries identical.

Use static fine texture, slow pan, fast reversal, occlusion, scene cuts,
protected/feathered regions and signed HDR highlights. Measure flicker after
motion compensation, edge/detail preservation and residual trail duration;
inspect synchronized crops/video alongside metrics. Less variance alone can
mean blur, so it is not sufficient evidence. Clearly label synthetic versus
natural frames, and do not compare unmatched playback screenshots.

Only if the evidence shows history contamination, fix rejection/reset/motion
validation. If the 80 ms EMA causes excessive persistence, evaluate one
confidence-dependent history change with the same clips and bounded test.
Do not globally increase smoothing or sharpen to hide ghosts. Keep the feature
default-off until motion/detail evidence supports acceptance; expose any
eventual history-strength control under anti-flicker, not new NR model styles.

## C. XeSS jelly and 4X/6X presentation

Treat image interpolation and scheduling as separate problems.

- Compare released 1.4.3 and current using verified runtimes and identical
  source/settings. Obtain generated output through a supported diagnostic
  route; verify that the capture actually includes generated frames. If no
  reliable capture is available, label visual comparison incomplete rather
  than use base-frame screenshots as evidence.
- Use NR and sharpening on/off only as diagnostic factors. Examine whether
  artifacts already exist before FG, and check motion sign, units, extent,
  source/pair identity, discontinuity/reset and resource lifetime. Never ship
  disabling NR or detail as the fix. Do not assume sharpening is the cause.
- For timing, compare 2X/4X/6X at the same quality. Collect interval histogram,
  P95/P99/max gaps, bursts, per-pair generated/presented/expired counts, reset
  and warmup counts, GPU ready time versus due time. SDK submission is not
  scanout; use display-event tracing if available and label limitations.
- Investigate one reproduced scheduling defect at a time. Keep XeSS provider
  scheduling separate from DLSS batch scheduling. Add source timestamps only
  if the provider contract and measurements show the hint is needed.
- Read FG_NON_NR_EXPERIMENTS_2026-09-20.md and the cadence worklog before
  experiments. Do not repeat queue HIGH priority, native-guidance resolution,
  speculative wall-clock hints, fixed 35/50 ms holdback or unsafe async queue
  experiments without new evidence explaining the previous failure.

## D. Independent input denoise, conditional implementation

This is a new feature, not a fix already delivered by temporal NR.

1. Verify official VFX/NvCV/CUDA APIs, supported runtime/model set, provenance
   and licenses. Reuse the GPL-compatible upstream wrapper where suitable,
   with pinned attribution. Do not guess support from the upstream enum.
2. Make a standalone product-library probe for same-size SDR GPU input/output.
   Prefer supported D3D12/CUDA external memory and semaphore interop; otherwise
   assess supported shared D3D11 resources/fences. These are candidates, not
   established SDK capabilities. Prove ownership and completion semantics
   before connecting the live graph; no guessed removal of CUDA synchronization.
3. Measure quality and total interop/inference cost on local RTX 5070. Separate
   source noise from desired grain/texture. Use still detail plus motion to
   detect blur/trails. Default off and independently controllable if accepted.
4. HDR requires a GPU-only precision/color contract. Do not use upstream CPU
   U8 fallback or claim SDR processing is native HDR. If SDR passes but HDR
   cannot preserve quality, offer only explicitly supported SDR scope; preserve
   existing HDR playback unchanged, and clearly report the limitation.
5. Stop after prerequisite audit and one feasible interop prototype if no
   supported path exists. No fake switch, arbitrary NGX quality values or
   unverified runtime distribution. New runtime packaging needs its own audit.

## Acceptance, rollback and delivery

Order: A diagnostics and repair, B quality, C FG comparison/repair, D denoise
prototype, then a clean local test package. D must not delay verified A-C fixes
indefinitely; the report must identify any unavailable new capability.

Each direction gets a baseline, one evidence-based change and one justified
correction/retest at most. Save separate Git checkpoints for retained changes;
revert failed experiments and record why. Preserve ab7979c as a rollback point.
No refactoring unrelated capture/UI/export paths.

Local final checks: p001 video with NR + 4K SR at selected 2X/4X/6X where the
provider supports it; anti-flicker off/on; sustained 120-second selected cases;
seek, pause/resume, resize, stop, provider/settings switches and protection;
targeted screenshot/export/HDR regression where the changes touch shared graph.
One test maximum 300 seconds. Use capture only with a valid live source;
do not turn on PS5 or declare absent input a completed capture test.

Build/package only accepted source, regenerate manifests and verify EXE/shader
hashes in the final extracted package. Do not redistribute the stale-manifest
diagnostic staging. Deliver a clearly identified local package and report;
other-device group validation belongs to the user. No GitHub publication or
main merge is part of this planning request.

New artifacts: E:/项目/Veyra/{tests,logs,tmp,build}/nr-fg-followup-20260921;
packages under E:/项目/Veyra/test-packages/ with explicit task/version label.
Commands, raw evidence, failed trials and accepted commit IDs go in WORKLOG.
