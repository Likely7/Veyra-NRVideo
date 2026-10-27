# FG stability execution evidence

## Scope and recoverable state

Worktree: E:/项目/Veyra/worktrees/playback-nr-20260920.
Branch: codex/fg-stability-20260921. Pre-work checkpoint d2ae8ee /
checkpoint/pre-fg-stability-20260921. Diagnostics checkpoint 0143baf.
Goal remains active. No release, package replacement or claim of completion.

## P0/P1: XeSS heavy-load timing candidate

Source-linked XeLL/Present diagnostics distinguish preparation cycle IDs from
actual source identities. Multiple queued sources can share a preparation
cycle; mismatch alone is not a failure. Provider output return records have
no source identity: they must not be attributed to a source by guessing.

Baseline source Present intervals alternate short/long around repeated epoch
resets. Within-burst generated spacing approaches 8.4ms despite consecutive
60fps source PTS; a 4X continuous pair corresponds to 4.167ms spacing.
The SDK scheduler waits are implicated, but exact internal causes remain open.

Candidate VEYRA_TEST_XESS_SOURCE_TIMING=1 supplies the actual consecutive source
PTS delta as frameRenderTime only when generation history is valid, identity
advances by exactly one, PTS increases, and delta is 0.125..500ms. Resets,
skips, repeated/unknown sources retain zero. It never feeds wall Present time
back, fills a fixed nominal period, adds waiting, disables effects or changes
the selected multiplier. Existing source correspondence checks still apply.
Default remains unchanged; this is a diagnostic candidate, not release approval.

Reference examined: Magpie 3841698348bfb246623d4acf791984c8b68a577b,
XeSSFGTiming.h, XeSSFGPresenter.cpp, XeSSFGPacing.h (clean checkout).
Magpie also has timestamp-deadline hooks absent from Veyra; its full timing
implementation has NOT been ported. This candidate uses existing source PTS
directly without copying the upstream median/fallback or timestamp hooks.

### Matched evidence

RTX5070, p001-derived1080.mp4 -> VideoSR quality3 4K + NR + XeSS4.
Same candidate executable, trace enabled, sequential 30s off/on runs:

| Metric | Off | On |
| --- | ---: | ---: |
| Retained source Present/s | 39.94 | 52.30 |
| NVML device GPU mean | 70.09% | 95.08% |
| Retained source interval P99 ms | 46.27 | 21.10 |
| Retained SDK return interval mean ms | 7.693 | 5.367 |
| Retained SDK return interval P99 ms | 23.25 | 18.58 |
| Retained SDK return maximum ms | 23.43 | 19.03 |
| Process age-to-Present-return P95 ms | 56.35 | 38.84 |
| SDK intervals under1ms / samples | 359/1566 | 399/1676 |
| SDK intervals over10ms / samples | 361/1566 | 401/1676 |

Both exit0, failed=false. The candidate improves throughput and long-gap
duration, but burst fractions do not improve. Short/long outputs remain;
this does NOT meet uniform4X acceptance. Source coverage remains below60.
No physical scanout, synchronized output-image quality comparison or glass-to-
glass latency measured. Ring tails differ in duration; raw counts are not
comparable rates. Original rings overwrite10697/16908 records respectively.

Earlier20s runs reproduce source39.98 ->52.35, native4K + NR XeSS4 remains
~60 source/s with candidate on. Neither pair establishes long-run stability.
Candidate-on20s2X smokes also maintain ~60 source/s for native4K+NR and
true1080p-to4K SR+NR, both exit0/failed=false. Evidence:
tests/fg-stability-20260921/source-timing-2x/{native-nr-xess2,sr-nr-xess2}.
These are source cadence/lifecycle checks, not2X visual or scanout acceptance.
The extra native SDK call instrumentation is opt-in and bounded, with no
per-frame file writes. It records return times, not physical display events.

### Commands and artifacts

Root for all artifacts: E:/项目/Veyra/.
Build: scripts/build-isolated.ps1 -Root . -BuildDirectory
E:/项目/Veyra/build/playback-nr-20260920 -DependencyCache
E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory
E:/项目/Veyra/tmp/fg-stability-20260921 -Targets veyra -DisplayVersion 1.4.4beta.
Build logs: logs/fg-stability-{source-timing,output-trace}-20260921.log; exit0.
Initial staging Copy-Item guessed a Release subdirectory and failed without
copying; corrected to the actual build-root veyra.exe.

Test harness: scripts/acceptance/fg-utilization-matrix.py with --exe
tests/fg-stability-20260921/app/veyra.exe, native user p001.mp4 and derived
tests/nr-fg-followup-20260921/p001-derived1080.mp4, --cases sr-nr-xess4
--seconds30, process TEMP/TMP tmp/fg-stability-20260921. Each process has
seconds+45 watchdog. Evidence roots:
tests/fg-stability-20260921/{timeline-a,source-timing-b,output-off,output-on}.
Per-case result.json records executable hash, environment, exact argv and
telemetry; trace.txt/app.log/stdout.log preserve raw evidence.
analyze-xess-timeline.py generated adjacent xess-timeline.json files.

Three synthetic analyzer tests pass (epoch boundaries, rebuild/revision,
missing/ambiguous joins); git diff --check passes. Initial analyzer omitted
epoch-boundary gaps: corrected before drawing conclusions or saving results.

## Whole-run and lifecycle follow-up

Added opt-in cumulative SDK-return histograms to XessPacing.cpp, bounded to
1001 quarter-millisecond bins per phase. First5s from first hooked return and
the remainder are separate. Percentiles are bin upper bounds (overflow uses
observed maximum). Totals include boundary gaps and are emitted on hook
release after provider context destruction, not per frame. They measure hook
lifetime, not application launch or scanout; 2X has no pacing hook coverage.

Same executable SHA256
6eafd66777b4ce307ccfc14abe70324a694b67f98e66f77327988d0f0419ad74:
heavy SR+NR+XeSS4, sequential120s on,30s off,30s on. Steady statistics:

| Metric | Off30s | On30s | On120s |
| --- | ---: | ---: | ---: |
| SDK interval samples | 2928 | 4198 | 20841 |
| Mean interval ms | 7.701 | 5.383 | 5.402 |
| P50 upper ms | 8.500 | 4.250 | 4.250 |
| P95 upper ms | 23.000 | 12.500 | 12.500 |
| P99 upper ms | 23.250 | 18.750 | 18.750 |
| Maximum ms | 41.124 | 43.733 | 45.954 |
| Intervals under1ms | 668 | 997 | 4940 |
| Intervals over10ms | 676 | 1008 | 4994 |
| Retained source presents/s | 40.03 | 52.23 | 52.09 |
| Process age-to-return P95 ms | 56.312 | 38.752 | 38.990 |

All exit0/failed=false. Sustained throughput improvement repeats, but burst
fraction and maximum gaps do not improve. Unequal run lengths make maxima
and raw event counts unsuitable as comparative rates. The matched30s pair
also has isolated >40ms gaps. This candidate is NOT uniform-cadence accepted.
Earlier retained-tail maxima understated whole-run spikes. Default stays off.

Evidence roots: tests/fg-stability-20260921/whole-{on120,off30,on30}.
Build log: logs/fg-stability-whole-output-20260921.log; build exit0.
Harness and fixed media/configuration are the same as above, with respective
--seconds120/30/30 and source timing enabled only for on runs.

ui-fg-backends.py EXE p001.mp4 OUTPUT --portable with timing candidate enabled
passes12 selector transactions: XeSS/DLSS round trips, XeSS2X/4X rebuilds,
DLSS6X to XeSS supported4X, and off. Three layout sizes checked. Evidence:
tests/fg-stability-20260921/source-timing-switch/result.json and app.log.
This script disables NR/SR and is a lifecycle check, not heavy-load visual,
seek or continuous-resize acceptance. Analyzer3 synthetic tests still pass.

## Next acceptance and remaining phases

### Scheduler wait attribution follow-up

Trace-only ProviderOutput now records the audited caller RVA and hook entry.
ProviderDeadline records native timestamp-calculation completion and remaining
deadline time without rewriting the result. QPC is converted to the native
nanosecond domain separately from the host trace clock. ABI/RVAs were checked
against pinned Magpie and the actual provider machine code. GPL attribution
is in THIRD_PARTY_NOTICES.md. No runtime file was changed.

Two20s source-timing-on runs in caller-on/{native-nr-xess4,sr-nr-xess4}
show light-source60/s and heavy52.19/s. In retained heavy output transitions:
2202ed->2202ed mean4.306ms;2202ed->220467 mean4.310ms;
220467->220db2 mean0.228ms;220db2->2202ed mean13.020ms,
of which12.366ms is inside the extra scheduler call. Native/light has
5.687ms in that scheduler position, versus2.241ms tail-to-next return.
These identify call sites, not guessed source IDs or physical image order.

Important correction: the native scheduler includes a fence-completion check
and possible event wait for index1 before computing its time deadline. It is
NOT a pure sleep-until function. A subsequent20s heavy run with read-only
deadline hook (deadline-on/sr-nr-xess4) separates350 retained first-index calls:
before-deadline mean8.375ms/P95 14.272ms, after-deadline mean3.883ms,
native deadline lead mean3.882ms. Throughput52.24/s, ageP95 39.034ms, exit0.
The pre-deadline segment includes fence wait, lookup and CPU scheduling;
it is not yet an isolated GPU duration. Blindly deleting it can violate
resource readiness. Timestamp rewrite alone cannot be credited with removing
this segment. Next isolate actual fence readiness/producer queue ownership
before selecting a scheduling change. Do not relabel all12ms as wasted time.

Build logs: logs/fg-stability-{caller,deadline}-trace-20260921.log, both exit0.
All artifacts relative to E:/项目/Veyra/. Runs use the same matrix harness and
media described above, --seconds20; source timing and trace enabled. Adjacent
xess-timeline.json is generated by analyze-xess-timeline.py. Four synthetic
tests cover gap decomposition, legacy missing fields and source joins. All
pass. Deadline hook remains diagnostic-only; dynamic hook rebuild test still
needed (previous switch regression predates this hook). No optimization has
been promoted, packaged or published by this follow-up.

Full-run interval aggregation, sustained run and selector switches now have
evidence above. Dynamic seek/continuous-resize and visual correspondence
remain required before enabling by
default. Do not call it an accepted complete repair. Investigate persistent
short/long burst boundaries independently; no extra queue or fixed wait.
P2 DLSS long gaps and P3-P6 remain as listed in the execution plan; this work
does not change or complete them. Independent denoising prerequisites remain
unresolved. No software performance claim is extended to other GPUs.

## Fence readiness attribution

### Bounded one-pending experiment: reverted

Temporarily limited XeSS enhancement admission to one pending source batch,
with source timing enabled. The first version blocked file startup (one
source, zero generated); startup requires two prepared batches. One revision
exempted startup, following the existing low-queue gate's contract.

Revised 20s run `one-pending-startup-on/sr-nr-xess4` completed: 867 sources,
2034 generated, failed=false. Retained source submission was49.48/s versus
52.25/s in fence-on. Process age P95 fell to22.444ms from38.815ms. Retained
SDK intervals: mean6.019ms, P95 14.354ms, P99 15.452ms, max35.101ms;
2/1412 intervals below1ms and91 above10ms. Fence entry-to-deadline mean
2.128ms, P95 2.909ms, all331 samples pending then ready.

The reduced fence waiting supports shared-queue backlog as a contributor,
but the source coverage regression and remaining uneven intervals fail the
acceptance contract. Both experimental code lines were removed. No default
change, no new wait, no accepted optimization. Do not repeat a one-batch gate
without new evidence addressing lost source throughput. These are CPU/SDK
timings, not scanout or physical display latency.

Build logs: `E:/项目/Veyra/logs/fg-stability-one-pending-startup-20260921.log`
and `fg-stability-one-pending-reverted-20260921.log`. Evidence directories
under `E:/项目/Veyra/tests/fg-stability-20260921/`: `one-pending-on` (startup
failure) and `one-pending-startup-on` (revised rejection). Existing matrix
command, sr-nr-xess4,20 seconds; trace/source-timing/one-pending environment
enabled. Analyzer output saved adjacent to trace. Next move to P2 DLSS;
XeSS queue separation still requires an explicit provider retirement contract.

Read-only tracing now samples the audited scheduler fence on entry and at
timestamp calculation, with the native lookup target. Only our synchronous
index1 scheduler calls are sampled; pointers are thread-local and cleared
on return. No resource lifetime, signal, wait or deadline is changed.
UINT64_MAX completion is classified as device removal, not readiness.

20s heavy source-timing-on run, fence-on/sr-nr-xess4: exit0, source52.25/s,
process age P95 38.815ms. All351 retained first-index samples had a pending
fence on entry and a completed fence at deadline calculation. None was
already ready or still pending. Entry-to-deadline mean8.465ms/P95 14.300ms;
subsequent deadline wait mean3.815ms. This proves a real pending dependency,
NOT that all8.465ms is GPU execution, nor that hardware alone is responsible.
Queue backlog and CPU wake time remain possible contributors. Do not delete
the wait. Retained SDK-return max47.473ms includes36.954ms in native Present,
so even the largest spike is not explained solely by this fence wait.

Static follow-up: VideoPresenter passes the shared direct queue to XeSS;
only DLSS has the separate presentation queue. Provider code at0x21F030
signals the sampled fence via a queue at context+0x10. Attribution of that
queue's identity and preceding submissions is the next bounded investigation;
do not simply enable the DLSS queue path for XeSS without resource retirement
and tagging contracts. No new optimization is accepted by this diagnostic.

Build log: E:/项目/Veyra/logs/fg-stability-fence-trace-20260921.log, exit0.
EXE SHA256:90b63920f3e30d06ac509779adba087baeb953e09267316f18e3444219825387.
Tests use existing matrix and timeline analyzer commands, cases sr-nr-xess4,
seconds20. Evidence under E:/项目/Veyra/tests/fg-stability-20260921/fence-on.
Five analyzer tests pass, including pending/ready/device-removed attribution.
deadline-hook-switch and fence-hook-switch both pass12 backend/multiplier
transactions plus3 layout sizes, exit0. This is lifecycle evidence, not visual
acceptance or physical multi-monitor testing. Default candidate remains off;
full goal and P2-P6 remain unfinished.
