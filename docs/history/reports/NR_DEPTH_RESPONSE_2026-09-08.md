# Controlled NR depth response

Cycle75, basebfbfcbd. This experiment changes only a diagnostic target and build registration, not the player graph, default settings, dependencies or export integrity checks. Phase7/Goal remains in progress.

## Result and decision

On the pinned local Feature18 runtime, the tested depth variations produce **zero changed RGB channels in both raw NR RGBA8 output and final RGBA8 output**. This holds for a single256×256 frame and a12-frame moving sequence, with every frame compared. Changing NR intensity or temporal motion scale does produce large changes, and repeated baselines match exactly.

Do not add a per-frame depth model to the default NR path based on these results. They provide no measured NR benefit and do not establish that all other configurations ignore depth. They do not test SR or FG depth behavior. Depth provider remains unavailable; no Large model, download or inference dependency was introduced.

## Experiment

`tests/integration/DepthResponseTests.cpp` assembles the actual shared EnhanceGraph using the existing diagnostic-only `nrParameterProbe` hook. Each case creates a fresh graph, with same generated RGB source, source metadata, motion, reset sequence, model and residual settings. NR enabled, SR/FG processing disabled. Static cases have1NR/0NVOF; temporal cases12NR/11NVOF/11NR-motion frames. Exactly1reset per case, so the moving test does not accidentally reset every frame.

Modes:

| Mode | Changed variable |
|---|---|
| 0 | Default .5 depth, establishes per-frame baseline |
| 1 | Explicit replacement R32F texture filled .5 |
| 2–3 | Replacement0 /1 |
| 4 | Replacement horizontal gradient .01–.99 |
| 5 | Replacement checker .1/.9 |
| 6 | Gradient plus DepthInverted=0 instead of1 |
| 7 | Positive control: NR intensity0, default depth |
| 8 | Fresh repeated default baseline |
| 9–10, temporal only | Separate motion controls: MV scales0 /-1, default depth |
| 11–14, temporal only | Keep original depth resource pointer; after frame0 copy0 /1 /gradient /checker into that original texture |

Replacement bindings are verified with parameter Get and exact pointer equality. Resident modes capture the original pointer and validate R32F256×256; after frame0 they change its contents with SRV→COPY_DEST→SRV transitions. This checks whether replacing an Evaluate-time resource pointer alone explains the negative result. It cannot rule out every private runtime behavior or a feature flag that would be required to activate depth.

Raw NR readback restores graph resource states before the next frame. All readbacks and additional waits exist only in this diagnostic. RGB comparisons cover all frames; saved PNGs show the last frame. Alpha is excluded from changed-channel counts. No PSNR/perceptual quality claim is made.

## Actual evidence

Commands:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "$PWD" -Preset x64-release
out/build/x64-release/veyra_depth_response_tests.exe "<unique ignored output directory>"
```

Runner used Start-Process Hidden, retained Handle,275-second watchdog, WaitForExit/Refresh and explicit numeric exit code. Final build `logs/optimization-goal-20260908/build-cycle75-resident.log` exit0. Final run `logs/optimization-goal-20260908/depth-response-b8490565d1374d1c9a28a93ecc12b54d/`, exit0, log timestamps span39.416seconds. Every case debugErrors0, binding1, correct NR/NVOF/reset counts. Actual NR Create/Evaluate success0x1, SEH0 in `test.log`.

| Control | Raw changed RGB channels / max8bit difference | Final changed channels / max |
|---|---:|---:|
| Static intensity0 |191,293 /58|191,278 /58|
| Temporal intensity0 |2,306,633 /183|2,306,341 /183|
| Temporal MV scale0 |2,062,640 /147|2,060,339 /147|
| Temporal MV scale-1 |2,049,144 /164|2,047,395 /164|
| All depth modes including resident updates |0 /0|0 /0|
| Explicit .5 and repeated baseline |0 /0|0 /0|

Mode6 changes depth pattern and inversion together; mode4 independently covers that pattern, but this is not a full inversion×texture factorial. Resident updates start after frame0 and establish no creation-time depth-contract proof. The source is constructed, not natural footage or ground-truth3D. Motion control responses prove that these controls change this NR sequence, not that arbitrary estimated vectors improve it.

Initial build `build-cycle75.log` failed C1083 missing NGX include; using existing engine linkage and FFmpeg includes fixed the diagnostic target (`build-cycle75-fix.log`). Earlier successful runs191eb017… andfb2bd363… are retained; final run adds resident-content controls. No failures or prior variants were erased.

Independent gate/review is recorded separately after execution. Remaining work includes SR/FG depth-specific evaluation, natural SR reconstruction and motion/FG quality comparisons, plus RTX Video SDK availability. Physical capture, long stability and distribution remain separate.
