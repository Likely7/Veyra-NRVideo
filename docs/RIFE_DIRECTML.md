# RIFE frame generation (ONNX Runtime DirectML)

`FrameGenerationBackend::Rife` interpolates with RIFE (Practical-RIFE, MIT) through
ONNX Runtime's DirectML provider on the graph's own D3D12 queue. It is meant for
GPUs without NVIDIA VFG (AMD, Intel): it estimates motion from the two pictures
only, like VFG and AMD FRC, and offers 2X and 3X.

## Runtime (not bundled)

The menu entry is available when `runtime/rife` next to `veyra_qml_ui.exe` holds:

| file | source |
|---|---|
| `onnxruntime.dll`, `onnxruntime_providers_shared.dll` | NuGet Microsoft.ML.OnnxRuntime.DirectML 1.24.4, `runtimes/win-x64/native` |
| `DirectML.dll` | NuGet Microsoft.AI.DirectML 1.15.4, `bin/x64-win` |
| `rife_v4.26_fp16.onnx`, `rife_v4.22_lite_fp16.onnx` | `tools/rife_models/make_rife_models.py` from vs-mlrt's external-models `rife_v4.26.7z` and `rife_v4.22_lite.7z` |

`DirectML.dll` is loaded from that folder before ONNX Runtime, so the older copy in
System32 is never used. Building needs only the headers: `VEYRA_ORT_DML_ROOT`
points at the two extracted NuGet packages (`ort/build/native/include`,
`dml/include`); without them the backend compiles to an unavailable stub.

## Behaviour

- Per real frame: copy of the enhanced colour frame, conversion to the model's
  planar fp32 input (padded to a multiple of 64), shot-change test; per subframe
  one network run with timestep k/n and a conversion back. All on one queue in
  submission order; ONNX Runtime only records and submits (0.3 ms of CPU per run),
  no CPU fence waits.
- 2X runs RIFE 4.26; 3X runs 4.22 lite, which leaves room in a 30 fps pair.
  `VEYRA_TEST_RIFE_MODEL` overrides the model for tests.
- Shot changes: a motion-compensated block match on a 128x72 luma picture
  (median best-match error > 0.07). Across a cut the subframes show the new frame.
  The histogram/SAD test of VFG did not separate cuts inside one game location
  from fast camera turns on console capture.
- SDR output only (R8G8B8A8); other formats fail initialization and turn FG off.

## Measurements (RX 9070 XT, Adrenalin, PS5 1080p through Elgato 4K S, 2026-11-22)

Quality: a 30 fps GTA capture with every other frame dropped (15 fps in), each
generated frame compared with the real frame it replaces (960x540 luma PSNR).

| | PSNR mean | worst 5 % | top-25 % motion | 50/50 blend frames in motion |
|---|---|---|---|---|
| AMD FRC (SUPER profile) | 31.9 | 23.8 | 25.9 | 4.7 % |
| RIFE 4.26 through Veyra export | 33.4 | 25.8 | 29.3 | 0 % |

Model choice (same test, fp16 DirectML, one frame at 1920x1088): 4.26 10.5 ms,
4.22 lite 9.5 ms; 4.22, 4.23, 4.24, 4.25, 4.25 lite/heavy and 4.26 heavy were not
better on the worst 5 % and top-25 % motion, and 4.26 heavy (16.9 ms) does not fit
3X. 4.26 at 3X live: 7.3 % of pairs rejected by admission; 4.22 lite: none.

Live, capture arrival to Present of the real frame, per-frame logs:

| | p50 | p95 | generated frames shown per real frame |
|---|---|---|---|
| AMD FRC 2X | 21.8 ms | 41.3 ms | 0.95 |
| RIFE 2X | 37-40 ms | 39-43 ms | 0.97-0.99 |
| RIFE 3X | 40.3 ms | 50.4 ms | 1.98 |

RIFE costs about 18 ms more than FRC: the real frame waits for the network and
then one output interval. 3X costs no more than 2X. GPU time of the FG batch at 3X:
p95 25 ms of the 33 ms pair. 4X needs about 36 ms per pair in the live graph and is
not offered.

Not evaluated: NVIDIA (VFG is the better choice there: 2026-11-05 WORKLOG measured
RIFE at +32 ms per source frame at 4K against +13 ms for VFG high), Intel, 4K and
HDR. At 4K the network alone would take about 40 ms per frame on this GPU.
