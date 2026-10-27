# XeSS / DLSS selector rollback repair

## Evidence and scope

User log: `E:/App/Veyra-1.4.2-test-win64-portable/logs/veyra-app.log`.
At 2026-09-19T14:20:21.900Z and repeatedly through 14:28:06.169Z, the selector accepts the choice but EngineController logs `xess multiplier gate: requested=4 maxInterpolatedFrames=1 applied=unchanged` (other attempts request 3X). The request never reaches provider initialization.

Baseline `43b8a4a`, checkpoint `checkpoint/pre-fg-backend-switch-20260919`, branch `codex/fg-backend-switch-20260919`. Includes preceding scheduling and seven-audit repairs. Local build only; no package, main merge, push or release.

## Root cause and changes

- EngineController treated the current XeSS context's ceiling as a permanent capability. Stock 2X intentionally does not apply MFG patches; leaving unlocked XeSS restores those patches and retains diagnostic state. Publishing that state as a 2X ceiling then blocked future 3X/4X requests before initialization. Removed this pre-initialization XeSS rejection. XessPresenter still verifies the unlock and queries the actual runtime ceiling before accepting the requested count. No unsupported multiplier is reported as active.
- Capability publication now reports only the active XeSS context; inactive means unknown (0). The UI's audited-provider choices remain distinct from the current context's count. DLSS retains its runtime capability checks and 6X option.
- AppShell now propagates requestSettings rejection, changes the saved draft only after acceptance, and restores the master toggle if enabling a feature was rejected. SettingsWindow no longer attributes every rejection to a pending master toggle.
- Failure injection exposed another behavior: a failed live switch silently disabled FG through startup degradation logic. A live backend/multiplier change from existing FG now uses the existing transaction rollback on FG initialization failure. Startup degradation and first-time enabling remain unchanged.

Further regression exposed `XeLL marker result=-4` when two source preparations advanced marker IDs ahead of the first presentation. A later attempted presentation-ID rebase also failed at sleep ID 33 because it left incomplete cycles. Final implementation in `src/gfx/XessPresenter.cpp` allocates one complete XeLL cycle per presentation: one sleep, simulation start/end, render start/end, present start/end. Queued source preparations share the open cycle; render-submit end stays immediately before presentation, covering all submissions in that cycle. A repaint or previously prepared source starts a fresh complete cycle when necessary. These timings are presentation-cycle instrumentation, not individual source-to-screen latency. Source queueing and frame generation remain asynchronous; no dragging suppression or duplicate output was added.

Product files: `src/engine/EngineController.cpp`, `apps/veyra/ui/AppShell.cpp`, `apps/veyra/SettingsWindow.cpp`, `src/gfx/XessPresenter.cpp`, `include/veyra/gfx/XessPresenter.h`.
Regression files: `tests/integration/FgSettingsTests.cpp`, `tests/integration/ExperimentalBackendTests.cpp`, `tests/integration/Yuy2ColorTests.cpp`, `scripts/acceptance/ui-fg-backends.py`.

## Verification

Build directory: `E:/项目/Veyra/build/slider-reset-20260919`.
Dependency cache: `E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt`.
Process TEMP/TMP: `E:/项目/Veyra/tmp/fg-backend-switch-20260919`.
Evidence: `E:/项目/Veyra/tests/fg-backend-switch-20260919` and `E:/项目/Veyra/logs/fg-backend-switch-20260919`.

Commands (PowerShell array syntax for Targets/Arguments):

```powershell
scripts/build-isolated.ps1 -Root . -BuildDirectory 'E:/项目/Veyra/build/slider-reset-20260919' -DependencyCache 'E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt' -TempDirectory 'E:/项目/Veyra/tmp/fg-backend-switch-20260919' -Targets @('veyra','veyra_fg_settings_tests')
scripts/run-short-test.ps1 -Exe 'E:/项目/Veyra/build/slider-reset-20260919/veyra_fg_settings_tests.exe' -Arguments @('E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4','E:/项目/Veyra/tests/fg-backend-switch-20260919/engine-final','backend-switch') -TimeoutSeconds 220 -LogPrefix 'E:/项目/Veyra/logs/fg-backend-switch-20260919/engine-final'
python scripts/acceptance/ui-fg-backends.py 'E:/项目/Veyra/build/slider-reset-20260919/veyra.exe' 'E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4' 'E:/项目/Veyra/tests/fg-backend-switch-20260919/ui-final'
python scripts/acceptance/ui-fg-backends.py 'E:/项目/Veyra/build/slider-reset-20260919/veyra.exe' 'E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4' 'E:/项目/Veyra/tests/fg-backend-switch-20260919/ui-reject-verified' --reject-xess
```

Test PATH includes the existing 1.4.2 portable root and `C:/veyra-deps/ffmpeg-ps5-dav1d-installed/bin`. GPU tests run sequentially. UI harness bounds each run to 90 seconds and now requires explicit media/output paths.

- Final 2026-09-20 build succeeded: `E:/项目/Veyra/logs/fg-backend-switch-build-verified.log`. Targets: veyra, veyra_fg_settings_tests, veyra_experimental_backend_tests, veyra_yuy2_color_tests, veyra_repair_contract_tests.
- Engine regression checks seven successive requests: XeSS4 -> DLSS4 -> XeSS4 -> XeSS2 -> XeSS4 -> DLSS6 -> XeSS4. Each must apply its revision/backend/count and increase real and generated frame counters; stop must drain cleanly.
- Final engine regression: `engine-verified`, exit 0, seven switches and clean stop.
- Final actual popup test passed: `ui-verified/result.json`, exit 0. Includes repeated backend changes, stock 2X to unlocked 4X, DLSS6 to XeSS4 clamp, FG off, and selector layout at three window sizes.
- Injected XeSS initialization failure passed: `ui-reject-cycle/result.json`, exit 0. Returns to DLSS and can subsequently disable FG.
- Final provider regressions: xess-recovery2/4, xess-pan2/4, xess-resize4, xess-yuy50-2/4; all `*-verified` runs exit 0, debugErrors=0. Each uses the experimental backend exe with mode and explicit output directory, through run-short-test.ps1 with TimeoutSeconds=60.
- `yuy-pixels-verified`: all four range/matrix combinations pass, source error <=1/255 and display error 0. Includes alternating single-pixel luma in the upper half and chroma/matrix patterns in the lower half; NR off, 1:1 presentation, synthetic 256x64 input.
- `contracts-verified`: repair contract tests exit 0. Both pixel/contract runs use run-short-test.ps1 with TimeoutSeconds=60 and the matching executable.
- Final application SHA256: `9E7FA94E527BE52FEE8CC875AE249CCA05645A8D7B97B1FC850B21422CE994FC`.

The command examples above describe earlier runs. For the final reruns substitute engine-verified, ui-verified and ui-reject-cycle respectively. Earlier engine-monotonic failed and is retained; engine-cycle and recovery4-cycle passed an intermediate version. Final verified runs supersede intermediate evidence.

Failures retained as evidence: first injected-failure run (`ui-reject`) exposed disabled FG instead of rollback. After repair, the old harness still timed out looking for warnings in hidden inspector label 401, although the engine and selector restored DLSS; professional mode routes warnings through its status sink. Corrected the assertion to check selector, applied-backend label and rollback diagnostics, then reran successfully. An attempted ffprobe command referenced an absent executable; it was not used as validation evidence.

Local GPU: RTX 5070. This verifies settings transactions, generated output and GUI state, not physical scanout, interpolation quality or all RTX 30/40/50 drivers. No new SDK/runtime binaries, no runtime files modified, no export/NR/color algorithm changes. Source diff review checks request rejection, asynchronous rollback and startup recovery separately.

## YUY2 50fps and capture clarity investigation

User clarified the sharpness comparison is against PotPlayer. Its input format, filter settings and display size remain unknown; no matched PotPlayer capture was supplied or measured. User capture stays running and was not interrupted for tests.

- GUID subtype `0x32595559` is YUY2. At 14:19:47.963Z, session 5 revision 15 reports capture 50fps, DLSS generated 150fps and total submission 200fps, no skipped evaluations or expired generated frames. At 14:22:45.964Z DLSS2 reports 50 generated and 100 total fps.
- At 14:22:49.442Z XeSS2 reports enabled=1, framesPresented=2 and generatedTotal=58. Common frame-flow GPU counters intentionally exclude provider-internal frames, while capture-timing and the UI already use separate provider counters. Added a separate provider-flow log with SDK count/rate and explicit not-scanout label; did not mix these into GPU completion metrics.
- Synthetic 640x360 YUY2 input with 50fps timestamps and paced arrival, 100 frames: XeSS2 generated 96 frames, XeSS4 generated 288, after test resets/resize. Both pass the generation threshold and D3D12 debug checks. This covers unpack -> graph -> provider, not the real capture driver or visible interpolation quality.
- Native YUY2 uses direct connection, no intermediate 4:2:0 converter. Shader loads each luma independently; chroma is shared between two pixels as required by 4:2:2. Pixel regression found no extra luma blur at 1:1 presentation.
- Relevant user intervals preserve 2560x1440 base/output while NR/flow operate at 1920x1080. NrResidualComposite retains the high-resolution base and upsamples the NR residual, so claiming the whole image is reduced to 1080p would be wrong. NR can still alter texture detail.
- Presentation uses bilinear sampling by default, optional bounded cubic magnification; this can differ from another renderer when scaled. Range/matrix fallback and YUY2 chroma subsampling can affect apparent contrast/colored text, but neither is proven as this user's cause.

Unresolved: perceived lack of smoothing and PotPlayer sharpness difference. Need matched input format/resolution, same frame at 1:1 with all effects off, then effects-on comparison; verify actual unique source cadence and display refresh/pacing separately. Existing logs cannot establish repeated pixel content or physical scanout. No speculative sharpening or input-format downgrade was introduced.
