# Smart SR development notes (feature/smart-sr)

Goal: a source that upscaled its own render (Switch 2 TV mode sends a 1080p
game as 4K) is reduced back to its render extent, and RTX Video SR enlarges
that to 4K instead of the console's scaler.

## Evidence before implementation

- Prototype detector `res-probe/resprobe.py` (outside this repo): synthetic
  ground truth 11/11; GC573 live capture of Rune Factory (Switch 2 Edition,
  TV mode) 87/89 frames 1080p, 14/15 on 3D-only tiles. The console's scaler
  matches bilinear: exact least-squares inverse re-upscaled with bilinear gives
  45.85 dB PSNR against the capture.
- Still-frame A/B (`res-probe/ab/game`): RTX VSR quality 4 from the exact
  1080p inverse is clearly sharper than the console 4K (mean gradient 4.95 vs
  3.09). DLSS SR on a single frame aliases edges and text; smart SR uses VSR.

## Current state on main (feature/smart-sr-main, from c663312 / 2.2.0)

Upstream ships the three-step SR (SR_STAGE_PLAN_2026-10-28): since 2.2.0 the
reduction ("再次降低到") belongs to double SR and shrinks the first AI result by
exact area in linear light; the plan leaves render-resolution detection and
descale out. An area shrink of a console's bilinear upscale blurs it again (on
the Switch 2 frame the 1080p SR input has gradient 5.14 against 7.71 for the
descale), and after a first SR pass there is no bilinear upscale left to invert.
Smart SR is therefore its own switch on the single SR pass.

- Setting: `EnhancementSettings::smartSr` (default off), "智能超分" switch in
  `SrStageEditor.qml` (list card and SR node). Active only with SR on, the
  standard RTX Video SR family (quality 1..4, NGX), no double SR and no
  supersample shrink (`activeVideoSettings` clears it otherwise, so inactive
  edits do not rebuild). The bridge refuses it for other SR algorithms.
- Graph description: live video on NVIDIA only (no still image, no export).
  `desc.smartSr` keeps the SR stage for raw capture at the target size;
  `desc.smartSrNativeCopy` marks that case, where a frame with no render extent
  is copied as before (raw capture at the target skips SR). Compressed video at
  the target keeps its same-size repair, and an enlarging SR (1080p signal to a
  4K target) keeps enlarging the whole source until a render extent is found.
- EnhanceGraph: probes the linear source every 30 frames (96 rows, 22 candidate
  heights below the source height); the 22 x 96 stats are copied to a readback
  buffer and read at the start of a later frame once the ring fence has passed
  that frame, so nothing waits on the GPU and no pixels are read back. (Not when
  a ring slot comes round: optical flow, NR and the Video Effects SR split a
  frame over several slots, and the slot of the probe was then never a frame's
  first one, see the 2026-11-25 fixes.) An almost flat frame (coded luma
  deviation under 0.008: black, fade, loading screen) is no evidence and keeps
  the current extent; a weak fit at the height in use (dark picture) counts for
  it (`smartSrVote`). Three equal votes set a render extent, six votes for none
  drop it (`smartSrVotesNeeded`):
  SmartSrReduce descales rows (linear to coded) and columns (back to linear)
  into the top-left of the source-sized `srPrescaled_`, the
  encode blit and the VSR input rect follow it, and VSR writes the full output.
  Single-pass VSR is created without fixed dimensions, so the rect may change
  per evaluate; nothing is rebuilt when the render extent changes.
- HDR10 (PQ): the coded domain is chosen per frame (`smartSrCoding`,
  `shaders/SmartSrCoding.hlsli`): sRGB codes for SDR, PQ codes of BT.2020 with
  BT.2020 luma weights for an HDR10 source kept as HDR. HLG, linear float
  (scRGB) sources and HDR tone-mapped to SDR at ingest keep the ordinary path.
  HdrVideoSrRestore takes the extent of its base and proxy input from root
  constants [5]/[6] (0: whole texture, as every other caller binds), so the HDR
  restore samples only the render extent. If smart SR cannot run on a raw frame
  at the work size, the frame is copied as without smart SR.
- Order: SR has to read the source's own scaled picture. In list mode without
  NR anti-aliasing normally runs at source size ahead of SR; with smart SR it
  runs behind SR (`ChainExecutionRequest::smartSr`; the SR step then also exists
  at the same size), and the 处理顺序 strip shows it there. A denoise or an
  NR-first order in list mode, and any anti-aliasing, beauty, denoise or NR node
  ahead of SR in a node chain, block smart SR (`smartSrBlocked`): the graph is
  built as without it and the row says why. A colour grade stays (it is fused
  into the input conversion) and only weakens the detection.
- Status: `PlayerSnapshot::smartSr*` -> `QmlPlayerBridge::smartSrStatus`
  ("超分之前有会改动画面的处理…" / "正在检测…" / "检测到渲染分辨率 WxH…" /
  "画面不是放大输出…") under the switch.
- Field log: one `[smart-sr] probes N: in use=.. kept=.. none=.. other=.. flat=..`
  line per 40 probes (about 20 s at 60 fps).
- Storage (after merging upstream 2.2.1, which took presets v38/v39, library
  v23-v26 and session v18-v21): PresetStore v40 (field after the v38 layout),
  preset library v27 and mode session v22, each saved to a `.smart-sr` sidecar
  the way upstream saves `.video-denoise`, so the file it starts from keeps its
  bytes. Export worker ABI version 17 (16 is upstream's video denoise; the
  settings struct grew; exports do not run smart SR).
- i18n: 8 new strings (hint says "SDR 与 HDR10") in all 9 catalogue languages (`extract.py --check`:
  missing 0, placeholder problems 0).
- Tests: `veyra_effect_chain_tests` (math, verdict rules incl. unmeasured
  heights, normalization, rebuild, graph description),
  `veyra_repair_preset_tests` (v40 round trip, invalid value keeps the file,
  off stays v21), `veyra_preset_library_tests` (v27 / v22 sidecars).
- Build: main needs OpenSSL (portable update verification); libdovi is off in
  this reduced local build.
- Results on main with the switch (zh-TW UI, private data dir): turning it on
  rebuilds once (`smart-sr auto: 22 candidate heights ... whole-source SR`),
  the Switch 2 clip settles at `render 1920x1080 (knee x5.3 notch x3.4)` and the
  row under the switch reads "偵測到渲染解析度 1920x1080，依此還原後超解析".
  The session goes to `chain-session.v1.smart-sr` (v18 then; v22 now) with the original file
  unchanged; after a restart the switch is on and the clip settles again.
  Synthetic 1440p -> `render 2560x1440 (knee x21.7 notch x14.4)`; native 4K
  (compressed) -> `whole-source SR (knee x1.6)`, i.e. the same-size repair.
- Earlier env-var build on main, same engine path: output on the Switch 2 clip
  gradient 4.933 / high-frequency 0.374%, as the 2.1.1 build (4.941 / 0.373%).
- HDR evidence (Switch 2 TV mode, HDR on, GC573 P010 4K60 with HDR-PQ input,
  Veyra HDR screenshots = lossless scRGB FP16 JXR, analysed with
  `res-probe/hdr/hdr_probe.py` outside this repo; synthetic check: the domain
  that was upscaled fits at 145 dB, the other one fails to detect):
  - PQ codes: `spatial 1440 knee x32.6-35.1 notch x20.4-21.2`, descale and
    re-upscale fit 51.1-52.8 dB PQ PSNR; linear light: no detection; sRGB of a
    203-nit reference: weak (x8.9) and 46 dB. The console scales PQ codes.
  - Live with this build: `coding=PQ (HDR10)`, `render 2560x1440 (knee x35.1
    notch x21.2)` 1.3 s after the graph; output (PQ luma mean gradient /
    high-frequency share) 22.3-23.4e-3 / 3.0% against 16.4e-3 / 1.3% for the
    console's 4K; 60.00 fps, GPU ready P95 12.3 ms (SR stage 11.8 ms, 34
    one-second samples, RTX 5070 Ti).
  - A dimmed, almost black picture (peak 2-15 nits) voted pass-through and 1440
    in turn (knee x2.8-3.1) before `smartSrVote`; the unit tests replay those
    verdicts. Not re-run live on a dimmed picture.
  - SDR after the HDR change: clips unchanged (`render 1920x1080 knee x5.3`,
    `render 2560x1440 knee x21.7`, `whole-source SR knee x1.6`); the Switch 2
    clip screenshot is byte-identical to the one before.
- Unit tests here: effect chain 516 PASS / 0 FAIL, preset library 646 PASS /
  0 FAIL, ui i18n 0 failures; repair presets: every itemized line (smart SR v38, now v40
  included) is 1, but the overall line is 0 - the same overall 0 as upstream
  c663312 built from its own sources in this environment.

## Fixes from the first play test (2026-11-25)

Field log: Switch 2, GC573 P010 4K60 HDR-PQ, 20 minutes with a build from before
these fixes, list mode with a DLAA card on. Two findings, three causes:

1. No verdict while DLAA is on. The probe was copied to the readback of the ring
   slot current at that point, but collected only for the slot a frame starts
   with. Optical flow (DLAA here; also NR and frame generation with flow) submits
   mid-frame and takes another slot, so the probe was collected only when the
   shared slot cursor happened to change parity: 76 s without a verdict with
   DLAA, 1.4 s without it. Now collected by fence.
2. DLAA ahead of SR also weakens what is measured (still clip, fixed
   collection, DLAA ahead of / behind SR: 1080p knee x3.4 / x5.4, 1440p x4.5 /
   x21.4) and the descale is no longer the exact inverse. Anti-aliasing runs
   behind SR now.
3. A false 2048x1152 (knee x992.6, notch x1.1) on a 3 s almost flat screen between
   two cuts, held for 3 min 26 s (cause 1 fits: no later verdict was logged). Cutting
   the inverse at 28 taps left every row with a sum off one by a different amount
   per scale ratio (7e-3 at 576, 5e-4 at 1080, 3e-5 at 1152); on an almost flat
   picture that gain error was all there was to measure. A float32 replay of the
   probe gives knee x991-994, notch x1.1. Now: each tap row sums to one, an
   almost flat picture is no evidence, and a knee alone is not enough (notch x2
   wherever the next height up was measured; every true detection so far has
   x2.1 or more).

Checks: offline replay (float32, 28 taps), 22 heights x 2 pictures x 8/10 bit:
same detections as before (540 has no lower candidate and is not found, as
before); 126 almost flat pictures (6 levels, 8 / 10 bit and float, ramps and
glows): 66 false heights before, 0 now; Switch 2 SDR and HDR captures unchanged.
App, 4K60 H.264 clip 1080p game / almost flat / native / 1440p / almost flat:
with DLAA on no verdict in 31 s before, now `render 1920x1080 (knee x5.4 notch
x3.5)` after 2 s and `render 2560x1440 (knee x21.4 notch x14.3)`, the same as with
DLAA off and with DLAA + frame generation. 10-bit HEVC clip: before `render
2048x1152 (knee x67.9 notch x0.9)` on the almost flat part, now none. DLAA +
smart SR keeps 60.0 fps without skips (RTX 5070 Ti, SDR file; processing 9.7 ms at
1080p and 13.0 ms at 1440p, against 4.9 / 8.3 ms without DLAA). Unit tests: effect
chain 556 PASS / 0 FAIL, preset library 674 / 0, repair presets exit 0, ui i18n 0
failures, QML quick 256 passed / 0 failed / 1 skipped.

Second play test with these fixes (2026-11-26, Switch 2, GC573 P010 4K60 HDR-PQ,
RTX 5070 Ti, SR quality 4, list mode, about 10 minutes of log): 14 settled verdicts,
all 1920x1080, 2560x1440 or pass-through (knee x13.4 to x26.9, notch x7.9 to x15.7
for the render extents); in 960 probes (33 almost flat) no other height settled;
1080p <-> 1440p followed in both directions. Pass-through for the first 100 s (knee
x1.8 to x2.4): part of it has the signature of the missed HOME screen in the next
section (1872, knee x2.4, notch x0.9). The NR switch, an anti-aliasing switch and a
stage ahead of SR (`not running` logged) each rebuilt as designed. GPU ready P95 per
second:

| Setup | GPU ready P95 | SR / flow P95 | Rate |
|---|---|---|---|
| 1080p, no DLAA | 7.8 ms | 7.3 / - | 60 fps |
| 1080p + DLAA | 11.2 ms | 6.1 / 3.8 | 60 fps |
| 1440p + DLAA | 16.2 ms (54 s) | 10.3 / 3.8 | 60 fps, no drops |
| 1440p + DLAA, other scene | up to 18.1 ms (45 s) | 12.2 / 3.9 | 57-58 fps for the last 20 s, 50 frames dropped |

So 1440p -> 4K HDR with DLAA sits at the 16.7 ms limit on this card. Not checked:
the text of the blocked status on screen (unit tests and the log line only).

## Second scaler (2026-11-26)

Report from the same play test: after switching games (the signal drops), HOME is
1080p but nothing is found any more. Not a stuck detector: a restarted capture gave
the same result, and a HOME screen without a game settled 4 s after a signal loss.

Two HDR10 screenshots of HOME (Veyra F7, SR off), The Witcher 3 open in the
background / no game open:

- No game: bilinear 1080p as known (knee x16.0, notch x9.5).
- Game open: best bilinear height 1728, knee x2.5, notch x0.5, as in the log. It is
  still exactly 1080p -> 4K, through another scaler: even samples sit on the render
  samples and odd ones half way (bilinear has both a quarter off). Odd samples follow
  from the even ones within 0.33 ten-bit codes on both axes with the same
  coefficients, i.e. at the rounding noise; the reverse does not hold (2.4 codes),
  and neither does a period of 3 (a 1440p signal).
- Kernel from the same icons in both screenshots, in PQ codes (other domains fit
  worse): 0, 1, 2, 3 render samples away 0.7856, 0.1121, -0.0126, 0.0077; 0.5, 1.5,
  2.5, 3.5 away 0.5362, -0.0599, 0.0214, 0.0023. Whether the console or the capture
  card (GC573) scales is not known; the signal loss suggests a change of the HDMI mode.

Added: `SmartSrScaler::Aligned2x`, one more probe (half the source height, 28-tap
inverse, the kernel above in `SmartSrProbeResidual.hlsl`) behind the 22 candidates.
It is taken when its residual is at least 3 times below the bilinear ones of its own
and of the next height; votes, hold rule and descale work as before, the descale just
gets the other taps (exact inverse, error 4e-5, noise gain 1.09).

Checks: offline, gain x61 and more on the screenshots; at most x2.2 on everything else
(HDR10 and SDR captures, native, bilinear from all 22 heights at 8 / 10 bit, 126
almost flat pictures). Synthetic aligned 1080p pictures: x77 to x171 at 10 bit, x6 to
x16 at 8 bit; a picture dimmed to 30 % at 8 bit can miss it (x1.7 to x3.7). The 1080p picture
recovered from the game-open screenshot matches the one behind the other screenshot at
39.6-42.7 dB on the same icons (area shrink 30.4-39.6 dB). App, HDR10 10-bit clip of
the screenshots: before `whole-source SR (knee x2.5 notch x0.5)`, now `render 1920x1080
aligned 2x (knee x51.3 notch x53.8)`, then bilinear 1080p (no game), 1440p (game),
none (native) and aligned 2x again, each within 3 probes of the cut. Unit tests: effect
chain 573 PASS / 0 FAIL, preset library 0 FAIL, repair presets exit 0, ui i18n 0 failures.
Not checked: a game's own picture in that mode, other ratios (a 1440p signal) and other
capture cards, whose filters will differ.

## Thin text in HDR (2026-11-26)

Report from the play test: text gets thin with smart SR. Not the AI: the HDR restore
(`HdrVideoSrRestore.hlsl`) adds the proxy's difference to the HDR base, and enlarged that
base bilinearly in linear light, the proxy in sRGB codes. The gap between the two
brightens every edge, and where the proxy is compressed (above about 150 nits) the
base alone is shown. Dark strokes on a bright page get thin, bright ones on black fat.

With smart SR the base is now enlarged in PQ codes and the proxy is the one of that
base; highlights then show the console's own 4K picture. Other HDR enlarging keeps the
old code (the same gap is there, not changed here).

Measured with real RTX Video SR (quality 4, RTX 5070 Ti) on the two HOME screenshots,
against the console 4K, area past half way from the page to the stroke in PQ codes: dark
text 78-86 % before, 100-106 % now; white text 107-108 % before, 99.5-100 % now; gradient
of the dark text x0.99-1.10 before, x1.23-1.31 now. Synthetic 1080p strokes (2-3 px), both
scalers: dark 0.50 and bright 1.49 of the render's strokes before, 0.96 and 1.05 now
(`veyra_sr_quality_tests OUT smart-hdr`, new; fails with the old shader). SR stage 5.3 ms
before and now. `supersample` and `dual-restore` still pass. SDR does not restore a base
and is not changed.

The sections below record the work on the 2.1.1 base (feature/smart-sr), where
`VEYRA_TEST_SMART_SR_HEIGHT` was the test switch.

## M1 (done, local only)

- `include/veyra/pipeline/SmartSr.h`: render extent helper and the test switch
  `VEYRA_TEST_SMART_SR_HEIGHT` (render height; unset keeps the existing path).
- `GraphDescription.h`: with the switch set, raw capture keeps same-size VSR.
- `EnhanceGraph.cpp`: same-size VSR (SDR only) reduces the source into the
  top-left render extent of `videoSrInput_` with the existing ScaleBlit, and
  `VideoSrBackend::evaluate` reads only that region (`Rect.W/H`).
- Verified with a 4K H.264 loop of the captured frame: log shows
  `[smart-sr] render=1920x1080 source=3840x2160`, VSR create/evaluate succeed.

Result: the path works, but the ScaleBlit reduction (2-tap box at 2x) throws
away the detail VSR needs. Output gradient 3.58 vs 4.95 for VSR from the exact
inverse. Measured on the 1080p input itself: exact inverse 7.71 / box 5.17.

## M1.5 exact reduction (done, local only)

- `SmartSr.h` `smartSrInverseAxis`: per axis, the pseudo-inverse rows of the
  pixel-centre bilinear upscale (U^T U is tridiagonal, one Thomas solve per
  phase). 28 taps over source samples centre-14 .. centre+13, one row per
  phase (render index mod q; 2x: q=1, 1.5x: q=2). `smartSrSelfCheck` recovers
  a synthetic render signal from its upscale and is logged at graph creation.
- `shaders/SmartSrReduce.hlsl`: one separable pass per dispatch and phase,
  taps in root constants (40 values), coded (sRGB) domain as the console scaled.
- `EnhanceGraph`: the encode blit stays 1:1; rows reduce into `smartSrMid_`
  (render width x source height, RGBA16F), columns into the VSR input's
  top-left render extent; VSR reads that rect.
- Run (4K H.264 loop of the captured frame, test height 1080): log
  `phases x=2:1 y=2:1 inverse error x=3.31e-04 y=3.31e-04`; output gradient
  4.941 / high-frequency 0.373% against 4.946 / 0.369% for VSR from the
  offline exact inverse; crops match visually (`res-probe/ab/game/compare-smartsr-m15.png`).
- Live GC573 capture (3840x2160 60 fps YUY2, Switch 2 menus, RTX 5070 Ti),
  same build and settings, from the per-second `[capture-timing]` /
  `[frame-rate]` log lines (median of 20 one-second P95 samples):

  | run | graph | GPU ready P95 | callback -> present return P95 | fps | VRAM |
  |---|---|---|---|---|---|
  | smart SR, test height 1080 | `sr=1`, render 1920x1080 | 5.15 ms | 6.82 ms | 60.00 (6021 frames) | 1520 MiB |
  | baseline, switch unset | `sr=0` | 0.30 ms | 1.86 ms | 60.00 | 740 MiB |

  Smart SR (reduction + VSR 1080p->4K + decode) costs about 4.9 ms GPU and
  780 MiB per stream here. Software timings are not scanout or optical latency.
  Live output was not screenshot-compared (still-frame quality is above).

## M2 render detection (done, local only)

- `VEYRA_TEST_SMART_SR_HEIGHT=auto` turns on detection (a number still fixes
  the height). The prototype's 2D tile projections (~13 GMAC per probe) were
  replaced by a row form validated first in `res-probe/m2_validate.py`: per
  probed row, reduce with the inverse taps, re-upscale bilinearly, sum the
  residual. Synthetic 9/9 heights, Switch 2 captures 13/13 at 1080p.
- `shaders/SmartSrProbeReduce.hlsl` (one dispatch per candidate phase, taps in
  root constants) and `SmartSrProbeResidual.hlsl` (one group per row) write a
  22 x 96 stats buffer every 30 frames, copied to the ring slot's readback
  buffer; `collectSmartSrProbe` reads it when the ring hands the slot out again
  (as FaceTracker does), so nothing waits on the GPU.
- `smartSrDecide` (SmartSr.h): knee/notch rules of the prototype. Only a
  spatial upscale is re-rendered; three equal verdicts change the render
  extent between frames (row pass sized for the widest, VSR rect per
  evaluate, no graph rebuild). Until then, and for native or reconstructed
  frames, the source passes through with a 1:1 copy and no VSR.
- Clips (10 s, 30 fps H.264), log lines ~3 s after graph creation:
  Switch 2 1080p frame -> `render 1920x1080 (knee x5.2 notch x3.3)`;
  synthetic 1440p bilinear -> `render 2560x1440 (knee x19.4 notch x12.9)`;
  native 4K -> `pass-through (knee x1.6 notch x1.0)`.
- Live GC573 4K60 YUY2, Switch 2 *Ocarina of Time* remake in play:
  `render 2560x1440 (knee x5.6 notch x3.8)` 1.3 s after capture start
  (offline detector on a frame of the same scene: 1440, x4.7 / x3.0); 60.00
  fps; GPU ready P95 ~12.6 ms, measured while the user's own Veyra 2.1.0 was
  also open, so it may include GPU contention. Probe cost not isolated.

## Open points

- Dynamic-resolution games change their render height from scene to scene; the
  three-probe streak (~1.5 s at 60 fps) follows settled changes only.
- HLG and scRGB sources, HDR shown on an SDR display (tone-mapped at ingest),
  the Video Effects SR families (high bitrate / streaming), DLSS SR and FSR are
  not covered; neither are exports and still images.
- Not measured: the probe's GPU cost alone; live SDR GC573 capture on main (the
  2.1.1-based build was measured live).

Build: reduced local configuration, see `deps/build-veyra.cmd` outside this
repo (MSVC 14.44, Qt 6.8.3, vcpkg FFmpeg 9.0.1, DLSS SDK 310.7, NVOF 5.0.7).
