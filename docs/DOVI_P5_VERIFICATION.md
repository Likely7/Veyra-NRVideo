# Dolby Vision profile 5 — verification record

This file is the evidence behind the profile-5 base-layer conversion: what was
run, against which sample and reference decoder, and what came out. It exists so
the reported comparison can be re-checked without re-deriving the method.

Everything below was produced with `tools/dovi_p5_probe`, which runs the
**shipped** `YuvToLinearRgb.dxil` over a decoded frame with the same root
constants `EnhanceGraph::convertInput` packs, plus its `--scan` / `--history`
modes, which report the RPU-derived conversion parameters without touching the
GPU.

## Reproduce

This repository has no CI configuration, so every number below is a local run.
They are reproducible with the committed tooling — the probe, the comparison
helper and the test target:

```
cmake --build <build> --target veyra_dovi_p5_probe veyra_repair_contract_tests
<build>/veyra_repair_contract_tests.exe                                 # 249 checks, 0 failures

PROBE=<build>/veyra_dovi_p5_probe.exe
SAMPLE="<the profile-5 file, see below>"
$PROBE --input "$SAMPLE" --time 0   --scan 1500 --sequential            # 1.
$PROBE --input "$SAMPLE" --time 0.5 --later 600  --history              # 2.
$PROBE --input "$SAMPLE" --time 95  --later 1200 --history
$PROBE --input "$SAMPLE" --time 95  --out p5-on.raw                     # 3.
$PROBE --input "$SAMPLE" --time 95  --out p5-off.raw --no-dovi
ffmpeg -ss 95 -i "$SAMPLE" -frames:v 1 \
  -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
  -f rawvideo -pix_fmt rgb48le ref95.raw
python tools/dovi_p5_probe/compare_raw.py pq p5-on.raw  ref95.raw       # conversion
python tools/dovi_p5_probe/compare_raw.py pq p5-off.raw ref95.raw       # control
$PROBE --input "$SAMPLE" --time 95 --out p5-sdr-on.raw  --sdr           # 4.
$PROBE --input "$SAMPLE" --time 95 --out p5-sdr-off.raw --sdr --no-dovi
$PROBE --input "<HDR10-only file>" --time 95 --out hdr10-new.raw        # 5.
$PROBE --input "<HDR10-only file>" --time 95 --out hdr10-old.raw --shader <pre-change shader dir>
python tools/dovi_p5_probe/compare_raw.py f2 hdr10-new.raw hdr10-old.raw
```

`compare_raw.py`'s `pq` mode does the domain conversion the comparison needs: it
takes the probe's linear scRGB output (1.0 = 80 nits), goes back to BT.2020 with
the player's matrix and re-encodes PQ, so both sides live in one domain.

## Tests

| suite | result |
| --- | --- |
| `veyra_repair_contract_tests` | **249 checks, 0 failures** |
| `veyra_preset_library_tests` | 236 checks, 0 failures (untouched by this PR) |
| `veyra_effect_chain_tests` | 204 checks, 0 failures (untouched by this PR) |

The profile-5 checks this PR adds or updates:

```
PASS DV P5 base layer is pinned to the routed HDR10 container so the RPU conversion can run
PASS DV P5 does not report an RPU conversion before its parameters are actually in effect
PASS DV P5 reports the IPT-PQ-C2 conversion once the RPU parameters are in effect
PASS DV P5 conversion uses the RPU matrices and the per-frame reshaping curve
PASS DV P5 refuses an RPU whose colour block is empty
PASS DV P5 refuses a reshaping method it cannot reproduce instead of converting with the identity
PASS DV P5 flat mappings convert to the identity regardless of playback history
```

The first of those replaces the assertion that expected profile 5 to stay
`Unsupported`, which the new route made false.



## Sample

| | |
| --- | --- |
| File | `Wednesday S02E01 2022 2160p NF WEB-DL DDP5 1 Atmos DV H 265-HHWEB.mkv` (7.66 GiB) |
| Video | HEVC Main10, 3840x2160, yuv420p10le, full range, 23.976 fps |
| Dolby Vision | profile 5, level 6, `bl=1 el=0 rpuDeclared=1 compatibility=0` (no HDR10/SDR base layer) |
| Colour tags | matrix/transfer/primaries carry no usable VUI colour; the file is routed to the PQ/BT.2020 container and the colour comes from the RPU |

Second sample used for the non-P5 regression: `Spider-Man No Way Home.2021.2160p.BluRay.Remux.HEVC.HDR10…mkv`
(HEVC Main10 HDR10, **no** Dolby Vision metadata at all).

## Reference decoder

```
ffmpeg version n9.0.1-11-ge47273f4d9-20260831 (deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0)
libplacebo filter present; the reference command is
  ffmpeg -ss <t> -i <file> -frames:v 1 \
    -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
    -f rawvideo -pix_fmt rgb48le ref.raw
```

`--no-dovi` on the probe is the control: it clears the enable flag in the
constant block, so the shader takes the ordinary BT.2020 YCbCr path — the
"play it as HDR10" behaviour that renders profile 5 green.

## 1. Every frame carries usable parameters

```
veyra_dovi_p5_probe --input <file> --time 0 --scan 1500 --sequential
```

```
scan summary frames=1500 usable=1500 noSideData=0 missingColour=0 unsupported=0 placeholder=119
```

* 1500 consecutive frames (about 62 s) — **no frame without parameters, none
  unusable**, so the new refusal path never fires on this stream.
* 119 frames carry a *flat* (placeholder) reshaping: 0.0–1.0 s (the fade-in),
  9.3–12.3 s, 43.3–44.0 s and 48.3–48.4 s (genuinely black stretches). Those are
  converted with the identity for the affected components rather than with a
  mapping that would blank the frame.

## 2. The conversion does not depend on playback history

`--history` reaches the same frame three ways — a fresh seek, sequential
decoding from the start, and a backward seek after playing a later part — and
compares every matrix and curve value:

```
veyra_dovi_p5_probe --input <file> --time 0.5  --later 600  --history
veyra_dovi_p5_probe --input <file> --time 95   --later 1200 --history
```

```
# a flat frame
fresh-seek  pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
sequential  pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
seek-back   pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
history independent=1 target=0.500 later=600.000

# a shaped frame
fresh-seek  pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
sequential  pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
seek-back   pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
history independent=1 target=95.000 later=1200.000
```

The playback state is now cleared per frame (and on `open`/`seek`), so a
backward seek into the opening produces exactly what a fresh open produces.
`RepairContractTests` asserts the same property at the builder level
(`DV P5 flat mappings convert to the identity regardless of playback history`).

## 3. HDR10 output vs the reference decode

```
veyra_dovi_p5_probe --input <file> --time 95 --out p5-95-on.raw
veyra_dovi_p5_probe --input <file> --time 95 --out p5-95-off.raw --no-dovi
ffmpeg -ss 95 -i <file> -frames:v 1 -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
       -f rawvideo -pix_fmt rgb48le ref95.raw
```

The probe writes RGBA16F linear BT.709 scRGB (1.0 = 80 nits); the comparison
multiplies by 80, goes to BT.2020 with the player's own inverse matrix and
re-encodes PQ, then compares against the reference's 16-bit PQ values:

| run | mean abs | p50 | p95 | bright-area PQ RGB (probe / reference) |
| --- | --- | --- | --- | --- |
| conversion **on** | **0.00354** | 0.00255 | 0.01501 | 0.5210/0.5322/0.5308 vs 0.5210/0.5324/0.5311 |
| control (`--no-dovi`) | 0.05410 | 0.03779 | 0.15231 | 0.7482/0.8377/0.7822 vs 0.5210/0.5324/0.5311 |

The control is 15x worse and its bright area is the washed-out green picture,
so the comparison is measuring the conversion and not noise. The residual 0.0035
is the difference between libplacebo's exact piecewise reshaping polynomials and
the three-term `{1, sqrt(x), x}` projection the shader can afford (the exact
curves score 0.0007 on the same metric, applying no reshaping at all scores
0.041, and skipping the matrices entirely — the control above — 0.054).

## 4. SDR output route

The same conversion feeds the pre-existing BT.2390 SDR tone map when the display
is SDR (`--sdr` clears the scRGB bit):

| run | mean | max | clipped at 1.0 | near black |
| --- | --- | --- | --- | --- |
| conversion on | 0.0209 | 0.9893 | **0.000 %** | 26.6 % |
| control | 0.0630 | 1.0000 | 0.81 % | 40.8 % |

Mean absolute difference between the two: 0.0339. So the conversion is applied on
that route as well, and the tone-mapped output stays inside range where the
control clips. (This is a range/consistency check; the colour accuracy claim
above is the PQ-domain reference comparison, which is where the two decoders can
be compared in the same domain.)

## 5. Non-P5 regression

An HDR10-only file (no Dolby Vision metadata, so the profile-5 block stays
disabled) decoded at t=95 with the shipped shader and with the shader from
before this change:

```
veyra_dovi_p5_probe --input <HDR10 file> --time 95 --out hdr10-new.raw
veyra_dovi_p5_probe --input <HDR10 file> --time 95 --out hdr10-old.raw --shader <pre-change shader dir>
```

```
samples=33177600 meanAbs=2.151e-09 max=4.883e-04 frac_equal=0.999972 frac_le_1step=1.000000
```

99.9972 % of samples are bit-identical and every sample is within one FP16 step
(max 4.9e-04, mean 2.2e-09): the added constant block and branch do not disturb
any path that is not profile 5.

## 6. Hardware versus software decoding

`veyra_dovi_p5_probe` decodes in software on purpose: the comparison needs the
frame in system memory next to an independent reference decode. The hardware
(D3D11VA) path was verified in the player itself, on the same file, with the
same shader:

```
[source-dovi] profile=5 level=6 compatibility=0 bl=true el=false rpuDeclared=true route=2 nativeOutput=0 rpuApplied=0
[source-dovi] profile 5 base layer converted as IPT-PQ-C2: RPU reshaping (per-frame, projected on {1,sqrt(x),x}) -> …
[source-dovi] profile 5 constants ycc_to_rgb=[1.000000,0.097534,0.205200|1.000000,-0.113892,0.133179|1.000000,0.032593,-0.676880] lms_to_rgb=[3.238759,-2.325560,0.086741|…]
[source-dovi] profile 5 RPU carries a flat (placeholder) reshaping on this frame; the affected components use the identity
[resolution]  source=3840x2160 base=3840x2160 nr=1920x1080 flow=1920x1080 fg=3840x2160 output=3840x2160
```

The matrices the player packed on the hardware path are the same values the
probe reports for the same frame, playback ran for the full 20 s window with no
refusal message, and frames were presented throughout.

## What this record does not establish

* The exact-piecewise-polynomial accuracy (0.0007) is not what ships: the shader
  uses the three-term projection described above.
* Non-polynomial (MMR) reshaping is not implemented; such a frame is now refused
  instead of being converted with the identity.
* The probe has no hardware-decode mode, and the libplacebo comparison is a
  software-decode comparison.
