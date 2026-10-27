# Playback, subtitles, HDR and NR: investigation and proposed work

> Historical proposal and implementation notes. Use
> [current plan](FG_STABILITY_COMPLETION_PLAN_2026-09-21.md) and
> [corrective audit](../reports/CORRECTIVE_AUDIT_2026-09-21.md) for current scope.
> Output cap now defaults OFF; XeSS/FSR support is incomplete. The experimental
> accepted-Present timing hint described below was reverted. Its description
> as improved timing is not current acceptance. Dolby Vision work is deferred.

Date: 2026-09-20. Status: proposal only; no implementation authorized in this request.

## Evidence and limits

- Veyra baseline: be3e2e6, branch codex/capture-color-144beta-20260920.
  Existing uncommitted capture discontinuity repair is preserved.
- Read-only inspection of C:/Users/123/Desktop/033. No package executable,
  installer, injection script, addon or runtime was executed or loaded.
- Magpie experimental source: https://github.com/SAOG0721/Magpie/tree/3841698348bfb246623d4acf791984c8b68a577b
  Download: E:/项目/Veyra/downloads/magpie-six-issue-audit-20260920.
  GPLv3 source reuse is permitted subject to attribution, fixed revision,
  modifications and corresponding source notices. No source port performed.
- No projector, affected MKV cue, controlled XeSS image comparison or physical
  HDR output measurement was available for this investigation. Code mechanisms
  below are distinguished from unverified explanations of user observations.

## 1. Independent output frame-rate limit

Current settings offer FG multipliers 1/2/3/4/6, not a custom final output cap.
Capture input limits are a different control. The existing frame-pacing plan
excluded an extra limiter to avoid competing clocks; this is a new proposal.

A 60 Hz projector cannot display 120 distinct complete frames each second.
Submitting 120 FPS may affect latency, tearing, frame selection or device
processing, but is not equivalent to 120 Hz output and is not always smoother.
VSync at 60 Hz also cannot promise 120 visible complete frames.

Proposed UI: output limit Off / Follow display / Custom numeric FPS, independent
of VSync. Preserve existing default behavior (Off), allow fractional rates such
as 59.94/119.88, persist settings and expose requested versus effective rates.
Do not silently change the requested FG multiplier or media/audio playback speed.

Integrate the cap into the existing presentation owner, not a second Sleep loop
on source/processing threads. DLSS self-presentation and XeSS provider-owned
pacing require separate integration checks; verify the provider's actual cap
capability before promising identical behavior. Bound queued work and avoid
generating work that cannot be presented where the provider contract permits.
Do not add a fixed latency cushion. A cap still necessarily schedules frames
against target time slots; it cannot be described as zero waiting.

Selecting 120 frames out of 144 generated temporal samples does not yield
uniform content phases automatically. Evenly spacing Present calls alone does
not fix that discrepancy. Keep frame selection and actual interpolation phase
quality as distinct acceptance criteria; do not falsify timestamps to hide it.

Acceptance: 23.976/24/25/30/60 input, 59.94/60/120 Hz displays, FG off/DLSS/XeSS,
VSync on/off, seek, cap changes and cross-monitor movement. Measure generated,
submitted and displayed rates separately, interval distribution, queue residence
and audio continuity. Physical latency requires separate instrumentation.

## 2. Subtitle size changes

Confirmed mechanism: apps/veyra/ui/SubtitleOverlay.cpp:128-134 shrinks text by
10 percent per attempt to fit targetLines; another fit step shrinks stacked
subtitles to available height. Default targetLines is two. This explains a
possible one-line/two-line size change, but the exact user's cue was not tested.
ASS-authored font differences and bitmap subtitles must be distinguished.

Proposal: preserve selected/authored font size by default, respect explicit line
breaks and grow the block upward. Treat preferred line count as a layout setting,
not implicit permission to shrink. Offer explicit Fit-to-area mode separately.
Handle long cues, bilingual overlap and safe-area overflow without clipping;
do not apply text font rules to PGS/VobSub bitmap scaling.

Acceptance: SRT/ASS embedded cues with one/two/three lines, narrow/wide windows,
DPI changes, bilingual text, styled/positioned ASS and bitmap subtitles.

## 3. Fullscreen lock and HDR color changes

Confirmed: AppShell.cpp pointerActivity reveals the controls on mouse motion;
there is no fullscreen lock. No direct HDR toggle was found in that handler.
The video viewport remains fullscreen while separate control windows are shown.
EngineController periodically checks display HDR state and may rebuild the graph
when output state changes; PresentSink manages the swapchain color space.

Hypothesis requiring measurement: an overlapping SDR control window changes
DWM composition/independent-flip/MPO behavior, affecting HDR appearance. Do not
report this as a confirmed RTX HDR disable/enable cycle.

Proposal: a lock icon in fullscreen controls; locking hides controls and ignores
incidental mouse motion. Provide explicit unlock action/shortcut; Esc/F11 must
always allow exit, without requiring a hidden control. Default session unlocked.
Log control visibility, display HDR state, swapchain format/color space, TrueHDR
active state and graph revision together. If overlay composition is responsible,
integrate HDR-aware UI composition after FG with a controlled UI white level.
Showing controls must not itself recreate the enhancement graph or toggle HDR.
The lock improves usability but does not replace fixing a confirmed color defect.

Acceptance: SDR/HDR displays, HDR input and SDR+TrueHDR, controls shown/hidden,
locked playback, keyboard escape, subtitles and cross-monitor transitions.

## 4. Dolby Vision compatibility

RTX Video HDR converts SDR to HDR; it is not a Dolby Vision decoder or converter.
Existing include/veyra/source/DolbyVision.h and MediaFileSource.cpp already route
compatible base layers: P7 to HDR10; P8/P10 compatibility 1 to HDR10, 2 to SDR,
4 to HLG. Unsupported profiles, absent base layers and incompatible color
descriptions are rejected. RPU/enhancement layers are not applied.

For a compatible HDR10 base layer, use that HDR directly. Do not round-trip
HDR through SDR and TrueHDR and call it Dolby Vision compatibility.

P5 and other non-compatible sources need a separate implementation project:
evaluate FFmpeg metadata extraction and libplacebo Dolby Vision color/reshape
support, then map to the shared linear processing/output contract. Reference:
https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/colorspace.h
(research reference only; pin and audit a revision before reuse).
Verify D3D12 interoperability, GPU-only processing cost and dependency licensing.
Do not claim full P7 FEL reconstruction, original dynamic metadata preservation
or licensed native DV output. Acceptance requires real profile-specific samples
and a reference renderer, not merely successful playback initialization.

## 5. XeSS 4x jelly compared with Magpie

The sharpening explanation is unproven. Magpie's XeSS marker shader is a point
copy, not an independent sharpening pass. Its defaults select AMD optical flow,
whereas Veyra defaults to NVOF. Magpie also exposes duplicate-frame filtering.
Both provide estimated motion and simplified depth; exact settings/runtime and
source content must match before comparing algorithms.

References in pinned upstream: src/Effects/XeSSFG/XeSS_FrameGeneration.hlsl and
src/Magpie.Core/XeSSFGPresenter.cpp. Veyra: src/gfx/XessPresenter.cpp. Veyra's
FSR SR sharpening is disabled in the inspected dispatch; DLSS SR has a sharpness
parameter, which does not establish the user's actual runtime setting.

Proposal: first compare identical clips/crops/resolution with NR and SR off,
same XeSS runtime/multiplier and input cadence. Then isolate optical-flow choice,
NR, SR and sharpening individually. Check current/previous frame identity,
motion direction/units/scaling, occlusion rejection, duplicate detection and
history resets. Zero motion is a diagnostic control, not a quality solution.
Measure warping/occlusion artifacts and repeated/uneven frames as well as FPS.
Port a demonstrated improvement only after the controlled comparison; do not
globally soften the image or disable effects to hide the symptom.

## 6. NR controls, styles and temporal stabilization

NR here is neural rendering/enhancement, not simply a conventional denoiser.
Veyra already sends NGX style IDs 0/1/2 plus intensity, tone, structure and skin
parameters. SettingsWindow currently labels style buttons 0/1/2. Magpie names
those IDs Default/Natural/Cinematic; they are not three newly discovered models.
Veyra clamps intensity/tone/structure to 0..1; inspected Magpie allows 0..2.
Range expansion needs tests against each supported NR runtime and consistent
UI/config validation. Categorical style IDs are not arbitrary user-trained styles.

Useful open-source implementation: Magpie DLSSNRTemporalShader.h and
DLSSNRTemporal.cpp stabilize the NR residual (Raw minus Base), with motion
reprojection, history validity, neighborhood clamping and rejection. A low-
frequency mode stabilizes broad residual changes while retaining current detail.
This is substantially different from blurring the entire image. It uses history,
not an additional future frame, but adds GPU work and can introduce ghosting.
Upstream also has 1..3 NR passes; multipass increases cost and should not be
bundled into the initial stabilization proposal.

033 inspection limits:
- Core NR implementation is a binary engine DLL; no matching core C++ source
  was found. No binary was loaded, extracted into Veyra or modified.
- Configuration exposes style, intensity, structure, tone, skin, sharpen,
  pregrade and stabilization-related fields. Values such as 200 have unknown
  exact runtime mapping without core source. Inspected seeds use sharpen=0.
- Readable DLSS5_Feed.fx prepares optical-flow/depth guidance and validity
  rejection. Game-injected depth access is not available for normal video input.
- notices/InsaneShaders033/SOURCE.md describes CC0 BilateralComic-derived
  log-luma/chroma color quantization and bounded HDR/skin handling, explicitly
  without bilateral smoothing or temporal filtering. Natural/cinematic recipes
  are described as numeric color recipes. These are not evidence of new trained
  NR models. The notice's source-inclusion statement was not confirmed by the
  inspected files. Preserve this distinction before reuse.
- Package NR runtime differs from Veyra's approved runtime identity. This audit
  grants no replacement or redistribution approval and did not verify its hash.

Proposal: name/explain the existing three styles; add named user NR presets for
model parameters and optional residual/temporal settings. Keep color presets
explicitly distinguishable from model styles. If reproducing pre-NR color recipes,
design their exact processing stage and HDR contract rather than blindly using
post-processing values. Evaluate an optional, default-off port of Magpie's
temporal residual stabilization into the shared D3D12 graph with full provenance.
Exact 033 core reproduction requires corresponding source and license evidence;
do not add ReShade injection to the product.

Acceptance: fixed scenes, motion, faces, dark gradients, HDR highlights, cuts,
seek, pause/resume, resize and provider switching; compare NR-off/base/temporal
variants for detail, trails/flicker and GPU time. Persist presets without breaking
old configurations. Reject a port that merely trades all flicker for ghosting.

## Proposed order and delivery gates

1. Fixed-size subtitle layout and fullscreen lock; instrument HDR control changes.
2. Independent output cap, with provider feasibility and cadence checks first.
3. Controlled XeSS/flow comparison; implement only evidence-backed differences.
4. Named NR presets and optional temporal residual stabilization, separately gated.
5. Non-HDR10-compatible Dolby Vision processing as a distinct larger project.

Each implementation step requires a reversible checkpoint, scoped changes,
build and relevant regression results. No automatic FG downshift, fixed latency
cushion, NR multipass default, runtime substitution or global color adjustment is
part of this proposal. User hardware acceptance remains separate from local tests.
This audit performed no application build or functional acceptance test.

## Implementation status in codex/playback-nr-20260920

The user subsequently authorized implementation. The current isolated worktree
contains the first reversible slice:

- Subtitles now preserve the selected font size by default. The old fit loop is
  behind an explicit `自动缩小字号以适应目标行数` option and is covered by the
  overlay/panel tests.
- Fullscreen has an explicit lock button and Ctrl+L toggle. Mouse activity is
  ignored while locked; Esc/F11 remain exit paths. The lock does not alter HDR
  state or rebuild the graph.
- Presentation settings now persist an independent output-rate mode and custom
  FPS (the default is Follow-display). The custom cap is enforced in the existing cadence owner, without a
  fixed latency cushion. Follow-display resolves the active Windows monitor
  mode and uses that same cap; generated candidates that are already stale are
  skipped to prevent queue growth. XeSS/FSR provider-owned swapchains report
  the cap as unsupported instead of throttling their real-frame input.
- NR has an opt-in temporal residual stabilization switch. It keeps a bounded
  two-texture history, reprojects it with the existing motion field, rejects
  inconsistent 3x3 patches and resets on discontinuity. It is a small Veyra
  integration adapted from Magpie's open motion route, not a claim of
  source-identical behavior. It is not enabled by default and has no real RTX
  image-quality acceptance yet.
- NR now also has a separate named preset file (`nr-presets.v1`). Save/apply/
  delete are exposed beside the model controls. Applying a preset copies only
  model, residual, exclusion-zone and temporal-NR fields, so it cannot silently
  change SR, FG, pacing, colour, audio or capture options.
- XeFG now receives a bounded accepted-present interval as `frameRenderTime`
  (zero on first/reset frame), matching the timing shape observed in the
  inspected Magpie route. It is a hint only: no sleep, queue growth or fixed
  latency cushion was added. This is an implementation change, but the jelly
  symptom still requires an affected-GPU A/B capture before it can be called
  solved.

Deferred by explicit user decision: Dolby Vision P5/RPU reconstruction. The
current code still supports only the already-audited compatible base-layer
routes; no decoder or colour conversion was changed. XeSS remains at the
controlled-A/B stage for the visual jelly symptom: timing input is improved,
but sharpening/flow defaults were not changed without a real affected-GPU
comparison. No runtime DLL was replaced or added.
