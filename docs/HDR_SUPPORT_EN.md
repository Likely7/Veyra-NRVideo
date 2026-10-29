# HDR Support: Dolby Vision, HDR10+, HDR Vivid

This work makes Veyra a correct player for three dynamic-metadata HDR formats. It
does not add a Dolby-licensed display pipeline: every format is decoded to
HDR10/PQ (or SDR, for a profile 8.2 base layer) and handed to the existing tone
mapping and presentation path.

The Chinese documents in `docs/` remain the deep reference, including the traps
and the reasoning behind each decision. This file is the entry point for readers
who want to know what works, how it was checked, and what is deliberately not
implemented.

## What's supported

### Dolby Vision

| Profile | Playback | Dynamic metadata | Verified |
|---|---|---|---|
| **5** (IPT-PQ-C2) | ✅ converted from the RPU | ✅ conversion parameters | ✅ Netflix WEB-DL sample |
| **7** single-track dual-layer, FEL | ✅ base + enhancement composited | ✅ L1 | ✅ real UHD remux |
| **7** single-track dual-layer, MEL | ✅ falls back to base layer | ✅ L1 | ⚠️ no MEL sample |
| **8.1** (PQ base) | ✅ | — (compatibility playback) | ✅ |
| **8.2** (SDR base) | ✅ SDR passthrough | — | ⚠️ routing only, no sample |
| **8.4** (HLG base) | ✅ HLG decode path | — | ✅ |
| **10** (AV1) | ✅ | ✅ L1 | ✅ Dolby's own test signal |
| **7** dual-track (EL in a second track) | ⬜ not supported | — | — |

The enhancement layer is split with FFmpeg's `dovi_split` bitstream filter,
decoded on a second software decoder, paired by PTS, and reconstructed on the GPU
in three components (NLQ residual + MMR inverse mapping, single-piece).

### HDR10+ (SMPTE ST 2094-40)

Scene metadata is read from `AV_FRAME_DATA_DYNAMIC_HDR_PLUS`. `maxscl[]` and
`average_maxrgb` (linear, normalised to 10000 nits) are converted to PQ and feed
the same curve-follow logic the Dolby L1 path uses.

### HDR Vivid

Dynamic metadata is identified and logged (window count, brightness statistics).
It is **not applied**: the format has no published reference implementation and
the samples available carry `tone_mapping_param_num = 0`, i.e. statistics but no
mapping parameters to apply.

## Dynamic metadata application

One switch drives both Dolby Vision L1 and HDR10+ scene metadata: *follow dynamic
metadata*, exposed under HDR brightness curve. It moves only the highlight
roll-off start; shadow lift, mid-gray and roll-off strength stay as the user set
them.

Three defects were found and fixed while making this actually reach the picture
(all were invisible while the switch was off, which is the lesson):

1. the peak smoothing was a sliding window whose overflow step halved an odd
   count, so the average drifted above every input it had seen (measured 1.11
   with inputs capped at 1.0 - a "52000 nit" peak);
2. frames claiming more than 4000 nits were followed; no mastering standard
   produces those, and following one pinned the curve at its ceiling for a frame;
3. `maxscl` / `average_maxrgb` are `AVRational`, and `av_q2d` divides by `den`
   unguarded - streams contain `{num, 0}`, producing `+inf` and a phantom
   10000-nit frame every 10 s.

## How it was verified

Nine local samples cover all five formats. Their paths, expected log lines and
per-format judgement criteria are in `docs/TEST_ASSETS.md` (Chinese).

Two worth calling out here:

- **FF Pictures' one-minute HDR10+ system test** is one looped scene with extreme
  metadata swapped every 10 s. The judgement is simply whether the picture
  changes every 10 s at all; the measured peak per block (395.6 / 980.5 / 175.5
  nits) and the resulting curve start are recorded in `docs/HDR10PLUS_2026-10-29.md`.
- **Dolby's P10 test signal** (AV1 10-bit, 47 s) is the only real profile 10
  sample. It exposed a routing bug: `dv_bl_signal_compatibility_id = 0` was mapped
  to "unsupported", while Dolby's bitstream table gives profile 10's CCID 0 base
  layer the VUI `1/9/16/15/1`, i.e. a PQ base layer.

Unit tests: `veyra_repair_contract_tests` includes a CPU-reference parity check
for the profile 7 reconstruction (306 checks, 0 failures). Luma must match the
ETSI reference exactly; chroma MMR is allowed rounding, since the reference uses
20-bit fixed point and the shader uses floats.

## Known limitations

### Upstream (FFmpeg)

MP4 files using the `dav1` sample entry (AV1-based Dolby Vision) are not
demuxable: `libavformat/isom_tags.c` maps `dva1`/`dvav` (AVC-based DV) and `av01`,
but not `dav1`. The payload is `av01` plus a `dvvC` box, so rewriting the fourcc
in the init segment is enough. `av01`-wrapped profile 10 files work today,
including their RPU.

### Not implemented, with reasons

| Item | Why |
|---|---|
| Multi-piece reshaping curves | 1.5% of frames across three samples; 300-400 lines for that |
| Dolby L2 / L8 trim passes | No open implementation, and every trim is bound to a target display index a player-led renderer can only guess |
| Dolby Display Management | Licensed and unpublished; L1-driven approximation is the ceiling |
| L5 active-area mask/crop | No benefit under a PQ output (letterbox is already black) |
| HDR Vivid tone mapping | No reference implementation; samples carry no parameters |
| Native DV output (TV-Led) | No public API on Windows |

### Unverified

| Path | Missing |
|---|---|
| Profile 7 MEL | A MEL sample (the `disable_residual_flag` logic has never run) |
| Profile 8.1 / 8.2 | Real samples (routing was verified with forged containers) |
| Dual-track profile 7 | Any sample |
| 4K 60 fps profile 7 | A high-frame-rate FEL source (EL decoding is 4-8 ms/frame on CPU) |
| Real HDR10+ feature films | Only test material has been run; real discs carry smooth per-scene metadata |

## Where the details are

| Document | Contents |
|---|---|
| `docs/DOLBY_VISION.md` | profile routing, profile 7 reconstruction, P5 conversion, metadata levels, every "decided not to" with its measurement |
| `docs/HDR10PLUS_2026-10-29.md` | HDR10+ implementation, the three defects, the FF Pictures verification method |
| `docs/HDR_VIVID_2026-10-28.md` | Vivid identification, format labels and the start-up badge, profile 8.4, why graphical subtitle colour splitting does not apply here |
| `docs/TEST_ASSETS.md` | the nine samples, what each proved, and the log lines to check on a regression run |
