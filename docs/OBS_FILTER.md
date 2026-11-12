# Veyra enhancement as an OBS filter

Veyra's Windows runtime is a D3D12 application whose headline features are
NGX-bound. On Linux the useful, portable part is the per-frame enhancement
chain, and OBS already owns everything else (capture, decode, composition,
encode). This module is that chain as an OBS video filter.

The Windows build is untouched: the root `CMakeLists.txt` takes the Windows
branch and only `add_subdirectory(linux/obs)` on other platforms.

## What the filter does

Filter id `veyra_filter`, shown as **Veyra Enhancement**. Add it to any source:
Video Capture Device, Screen/Window Capture, Media Source, capture card.

```
source frame -> 3D LUT -> colour grade -> FXAA -> sharpen -> output
```

| Control | Range | Notes |
| --- | --- | --- |
| 3D LUT (.cube) | file | `LUT_3D_SIZE` 2…64, `DOMAIN_MIN/MAX` honoured; red varies fastest |
| LUT strength | 0…100 % | blend against the ungraded colour |
| Exposure | −2…2 EV | applied in display space |
| Contrast | 0.5…2 | pivot 0.5 |
| Saturation | 0…2 | Rec.601 luma |
| Sharpen | 0…1 | 5-tap unsharp cross, after AA |
| FXAA | on/off | luma-directional, 5-tap cross + 2 diagonal taps |

AA runs on the graded picture (edge detection needs the final contrast), then
sharpen works on the antialiased result — sharpening first would hand FXAA
edges it then softens back. The Windows chain's colour→AA ordering
(`src/pipeline/EnhanceGraph.cpp`) is preserved.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

Needs `libobs` headers (Arch: `obs-studio`). Install layout:

```
/usr/lib/obs-plugins/veyra-filter.so
/usr/share/obs/obs-plugins/veyra-filter/veyra.effect
/usr/share/obs/obs-plugins/veyra-filter/locale/en-US.ini
```

`~/.config/obs-studio/logs/` shows `[veyra] filter registered`, `[veyra] LUT
loaded: 33^3 from …`, or `[veyra] effect load failed: …` on a shader error.

## Verified

Recorded 1080p30 through OBS 32.2.2 (RTX 3090, EGL/Wayland) and sampled the
recorded mkv pixels. Source images are synthetic, so every expected value is
computable:

| Filter state | Sample | Recorded | Expected |
| --- | --- | --- | --- |
| filter not loaded | red source, centre | `251, 0, 0` | passthrough |
| identity 33³ LUT | red source, centre | `254, 0, 0` | passthrough ±NV12 quantisation |
| invert 33³ LUT | red source, centre | `0, 253, 254` | red → cyan |
| saturation 0 | red source, centre | `75, 75, 75` | Rec.601 luma of red (0.299 × 251) |
| exposure −1 EV | red source, centre | `124, 0, 0` | 251 × 0.5 |
| sharpen 1.0 | 1 px stripes 96/166 | `61 / 201` | overshoot away from the mean |
| FXAA on | diagonal edge, x=700 | `171` (was `214`) | edge blended toward the dark side |

FXAA is measured on a diagonal edge on purpose: on a perfectly axis-aligned
step its direction vector is perpendicular to the edge and the blend is a
no-op, which is the algorithm behaving as designed rather than a missing pass.

## Not ported, and why

| Windows feature | Linux |
| --- | --- |
| DLSS SR/FG, RTX Video SR/HDR, DeepDVC, optical flow | NGX has no public Linux SDK — not portable |
| Neural NR (`src/ngx/DlssNrRuntimeAdapter.cpp`) | same NGX dependency |
| SMAA/MLAA | portable shader maths, not implemented here |
| FSR/XeSS super resolution | needs an upscaling stage; as a same-size filter it would only soften |
| Frame generation | not meaningful inside a filter |
| Capture, record/export, subtitles, streaming, node graph, presets | OBS owns these |
