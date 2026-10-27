# Magpie NR research and XeSS regression comparison

## Scope and decision

User requested NR anti-flicker/denoising research, useful upstream candidates,
and current XeSS versus released 1.4.3. Keep NVOF as the existing default.
No new runtime, package, publication or feature implementation in this task.
Current source is dd69e1f on codex/playback-nr-20260920.

The four software runs do not demonstrate a current XeSS throughput regression.
Keep the current implementation. This is not a visual jelly/ghosting acceptance:
no synchronized generated-frame image comparison or physical scanout was measured.
The speculative frameRenderTime feedback was already reverted in dd69e1f.
Do not roll back unrelated capture, HDR, subtitle or UI repairs.

## Upstream identity

https://github.com/SAOG0721/Magpie/tree/3841698348bfb246623d4acf791984c8b68a577b

Local and remote experimental HEAD were checked at that commit. GPLv3.
Local source: E:/项目/Veyra/downloads/magpie-six-issue-audit-20260920.
Any future port must record source files, fixed commit and changes in notices.

## Prioritized candidates

1. NR residual anti-flicker (highest priority). DLSSNRTemporalShader.h,
   DLSSNRTemporal.cpp, include/DLSSNRTemporalState.h and DLSSNR_AI_Filter.hlsl.
   Smooth signed NR correction rather than indiscriminately blurring the picture.
   Modes include static, optical-flow, optical-flow-plus, and low-frequency
   reconstruction. These are temporal modes, NOT extra NR model styles 3-6.
   The ordinary EMA uses 80 ms past history and source time; no future-frame
   hold is required, but visual response lag and GPU cost still exist.
   Plus mode tracks observation support, 60 ms attack/180 ms release, and
   rejects opposite corrections. Low-frequency mode filters half-resolution
   residuals while retaining the current high-frequency complement: useful for
   details, not proof of high-frequency noise removal. Motion discontinuities,
   invalid taps, cuts, duplicates and timestamp gaps have explicit treatment.
   Veyra currently implements only the simpler flow mode. NrTemporal.hlsl's
   any(abs(current)>1e-7) gate bypasses history on zero correction; ordinary
   missing correction and intentionally protected pixels need separate masks.
   NrTemporalPass clamps frameMs to 1..250 rather than independently rejecting
   invalid/gapped timestamps; caller reset coverage needs adversarial testing.

2. Independent source denoising (high priority, conditional implementation).
   RTXVideoDenoiser.cpp uses NvVFX_CreateEffect("VideoSuperRes") and denoise
   quality 8..11 (also accepts 16..19), with same-resolution output. The effect
   HLSL is a marker, not the denoiser implementation. Veyra uses direct NGX
   VSR quality 1..4; these quality namespaces must not be conflated.
   Upstream adds VFX/NvCV/CUDA dependencies, synchronizes CUDA per frame, and
   its HDR path reads U8 pixels back to CPU before upload. Do not copy that
   path into the D3D12 graph. Investigate GPU-only interop, identity/licenses,
   HDR precision, detail loss and GPU time first. Source noise and unstable
   NR-generated detail are different problems; handle them separately.

3. Shader quality regression suite (high priority, alongside 1).
   tests/DLSSNRTemporalTests.cpp checks signed residuals, on/off variance,
   motion/reset/rejection, tap validation, HDR finite values, detail retention
   and odd extents. Port relevant acceptance cases with provenance; extend
   them to our protection masks and D3D12 execution, not just preset storage.

4. Guidance identity validation (medium priority). FrameGuidanceTypes.h ties
   frame ID, sequence, resource generation, time, extent, valid region, fence,
   vector direction and units together. Veyra already has identity/reset logic;
   add missing boundary assertions/diagnostics, do not replace the whole graph.

5. NR layering (future, lower priority). DLSSNRMultiPass.h supports independent
   features/history and intermediate textures for 1-3 passes. Useful for the
   planned layers, but not extra model styles. GPU cost, memory and latency
   must be measured; single-pass stability comes first.

6. XeSS timing estimator (isolated experiment only). XeSSFGTiming.h prefers
   source timestamps with a nine-sample median; submit fallback subtracts its
   own measured waits. XeSSFGPresenter uses it on the compatibility-patched
   path, not universally. Capture timestamps may still include backpressure.
   Current Veyra keeps the zero hint matching 1.4.3; do not reintroduce guessed
   timing feedback without an independently measured benefit.

7. Duplicate detection and HDR contracts (conditional). FrameSourceBase's
   duplicate detector copies and synchronously maps a GPU result every frame.
   Useful concept for repeated source cadence, but needs asynchronous handling
   and preservation of real PTS/static scenes. HDR boundary/format tests are
   useful; they are not a reason to globally adjust normal users' colors.

Do not adopt HalfResOpticalFlow as a default replacement: its small local search
and simple upsampling are not evidence of better fast-motion vectors than NVOF.
No evidence here establishes that changing sharpness alone fixes XeSS jelly.

## Controlled software comparison

Actual release executable:
E:/项目/Veyra/releases/1.4.3/Veyra-1.4.3-win64-portable/Veyra.exe.
Release package manifest: zero file hash mismatches. Release ZIP SHA256:
A44B86E34BAEE1F4E7FEAC83ECE9279B5C1AE25193862FEA9F02225AD8223FA6.
Common runtime DLLs and PresentMotion.dxil match current test staging.
Four color conversion shader binaries differ due to the shared hue-wrap fix.
XessPresenter.cpp, XessPacing.cpp and PresentMotion.hlsl have no source diff
against v1.4.3. Optional temporal NR and cap are off in these tests.

Input: E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4.
Arguments: --smoke-seconds 60 --smoke-view pro --nr --no-sr --fg-xess
--fg-multiplier 4. Same NVOF/default settings, no UI interaction.
Sequential ABBA, 90-second watchdog each, Start-Process -WindowStyle Hidden
for both versions. All four exited 0, failed=false, no gate toggles.

| Run | NR evaluations | SDK presentations | Final SDK fps | Preview skips* | Media lateness P95 ms | Mean return interval ms | Worst return gap ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1.4.3 A1 | 3438 | 13711 | 240 | 0 | 1.48 | 4.178 | 35.179 |
| Current B1 | 3445 | 13739 | 240 | 0 | 1.48 | 4.178 | 36.924 |
| Current B2 | 3447 | 13750 | 240 | 0 | 1.35 | 4.177 | 40.306 |
| 1.4.3 A2 | 3150 | 12505 | 240 | 132 | 12.48 | 4.594 | 1994.025 |

*Last player-timing sample; not necessarily an exact shutdown total.
Mean interval is the mean of logged 240-return-window means, NOT per-frame P95.
Final SDK rate is not the whole-run average or display refresh rate.
The old second run has a large unexplained interruption and repeated preview
skips. Retain it in results, but do not attribute it to old code without a trace
or reproduction. The isolated new worst gaps do not prove a regression either.
No whole-software stability, capture gameplay, 6x, or other GPU acceptance claimed.

## Reproducibility and verification

Artifacts: E:/项目/Veyra/tests/corrective-audit-20260921/.
compare-xess.ps1, summarize-xess.ps1 and ab-{old,current}-{1,2}.log preserve
commands and evidence. Current app/ is test staging, not a distributable package.
Process TEMP/TMP: E:/项目/Veyra/tmp/corrective-audit-20260921.

Upstream temporal tests compiled with MSVC C++20 /MT /O2, assertions enabled,
and linked with d3d11/d3dcompiler. WARP tests passed and D3D11 debug layer
reported no resource/API warnings. This validates upstream shaders, NOT Veyra's
GPU integration or real video picture quality. Executable/object are under
tests/corrective-audit-20260921/magpie-temporal/.
Initial combined compilation failed to create a linker response file; split
compile/link exposed a non-ASCII TEMP path issue. Linking from the designated
E:/.../tmp directory using relative TEMP/TMP and relative paths succeeded.
No global environment or drive mapping changed. No product code edited here.
