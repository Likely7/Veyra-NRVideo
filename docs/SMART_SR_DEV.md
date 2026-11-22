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
  heights below the source height); per ring slot the 22 x 96 stats are copied
  to a readback buffer and read when the ring hands the slot out again, so
  nothing waits on the GPU and no pixels are read back. A flat frame (black,
  fade) is no evidence and keeps the current extent; with a render extent in
  use, a verdict without a knee that is not clearly smooth (dark, low detail)
  keeps it too (`smartSrVote`). Three equal votes set the render extent:
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
- Status: `PlayerSnapshot::smartSr*` -> `QmlPlayerBridge::smartSrStatus`
  ("正在检测…" / "检测到渲染分辨率 WxH…" / "画面不是放大输出…") under the switch.
- Storage: PresetStore v38 (field after the v37 layout), preset library v23 and
  mode session v18, each in a `.smart-sr` sidecar so older files keep their
  bytes (as `.sr-stage` / `.dual-sr` do). Export worker ABI version 16 (the
  settings struct grew; exports do not run smart SR).
- i18n: 7 new strings (hint says "SDR 与 HDR10") in all 9 catalogue languages (`extract.py --check`:
  missing 0, placeholder problems 0).
- Tests: `veyra_effect_chain_tests` (math, verdict rules incl. unmeasured
  heights, normalization, rebuild, graph description),
  `veyra_repair_preset_tests` (v38 round trip, invalid value keeps the file,
  off stays v21), `veyra_preset_library_tests` (v23 / v18 sidecars).
- Build: main needs OpenSSL (portable update verification); libdovi is off in
  this reduced local build.
- Results on main with the switch (zh-TW UI, private data dir): turning it on
  rebuilds once (`smart-sr auto: 22 candidate heights ... whole-source SR`),
  the Switch 2 clip settles at `render 1920x1080 (knee x5.3 notch x3.4)` and the
  row under the switch reads "偵測到渲染解析度 1920x1080，依此還原後超解析".
  The session goes to `chain-session.v1.smart-sr` (v18) with the original file
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
  0 FAIL, ui i18n 0 failures; repair presets: every itemized line (smart SR v38
  included) is 1, but the overall line is 0 - the same overall 0 as upstream
  c663312 built from its own sources in this environment.

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
