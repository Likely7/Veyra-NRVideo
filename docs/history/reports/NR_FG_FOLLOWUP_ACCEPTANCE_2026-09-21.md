# NR and FG follow-up: local acceptance

Scope: `codex/playback-nr-20260920`, starting at `09b4ad7`, with the accepted
temporal correction/optimization at `ab7979c`. RTX 5070, video input only.
No main merge, publication, shutdown, NR layering or general 1.4.2 performance
investigation. This report records partial capability acceptance, not a claim
that all reported picture-quality and cadence issues are solved.

## Retained changes

- Correct lateness sampling: one observation per actual application Present,
  rather than repeating the last observation on every engine iteration.
  Reuse the bounded, cached TimingWindow quantile implementation instead of
  sorting 1200 entries each iteration. This is a diagnostic/CPU bookkeeping
  repair, not evidence of lower screen latency.
- Record Present entry and return separately, with signed media deviation,
  source/revision/epoch identity, queue depth and CPU-observed GPU readiness.
  The bounded trace is flushed after a smoke run, not written per frame.
- Add an offline natural-video NR probe and motion-compensated analysis.
  Readback and CPU waits are confined to the diagnostic executable.
- The sustained harness checks XeSS provider throughput, since application
  Present counts omit frames submitted inside the XeSS swapchain.
- Keep the existing temporal mask/time fixes and equivalent tiled shader.
  No new scheduler, delay, multiplier downgrade or resolution reduction.

## A. Timing and true SR

The previous 41.71/43.35 ms figure was an absolute deviation from the media
clock after Present, repeatedly sampled by the engine. It is not 42 ms of
added anti-flicker latency. The 80 ms shader constant weights past history;
it does not wait for future frames. Neither is screen-to-eye latency.

Four sequential 30-second UI tests on original 4K60 p001, NR on, SR off,
XeSS 4X, off/on/on/off:

| Measurement, P95 ms | Off A | On A | On B | Off B |
| --- | ---: | ---: | ---: | ---: |
| Present entry media deviation | .479 | .500 | .500 | .500 |
| Present return media deviation | 1.229 | 1.271 | 1.229 | 1.229 |
| Process to observed ready | 14.843 | 15.482 | 15.436 | 14.910 |
| Decode ready to Present return | 32.936 | 32.981 | 32.970 | 32.936 |

All source rates were approximately 60. These retained-window observations
do not reproduce an added 42 ms delay in the SR-off case.

Original 4K to target 4K bypasses SR. To test actual SR, derive a 1080p60
version of p001, then request RTX Video SR quality 3 to 4K, NR, XeSS 4X.
Clean 30-second repeats, with no simultaneous build:

| Measurement | Temporal off | Temporal on |
| --- | ---: | ---: |
| Retained application submissions/s | 54.205 | 39.878 |
| Present call P95 ms | 17.497 | 28.610 |
| Process to observed ready P95 ms | 21.857 | 29.430 |
| Observed ready to Present entry P95 ms | .0046 | .0068 |
| Decode ready to Present return P95 ms | 38.899 | 57.410 |
| Last reported residual GPU P95 ms | .269 | 1.129 |
| Last reported preview source skips | 154 | 542 |

This combined configuration does NOT pass full-rate acceptance. There is no
measured long CPU hold after readiness in these samples. Present/backpressure
and GPU readiness increase; XeSS internal GPU cost is not separately measured,
so this is not proof of GPU saturation or a proven fixable redundant wait.
One earlier on-run overlapped compilation and is excluded as performance
evidence; the clean repeat confirms the slowdown. Default anti-flicker remains
off. Do not present disabling it, reducing NR detail or lowering FG as a repair.

## B. Picture quality

The existing synthetic GPU tests pass protection/feather, zero-residual decay,
invalid interval, motion rejection, reset and signed HDR cases. All 41 FP16
fingerprints match the corrected non-tiled implementation; D3D12 debug clean.

Three real-video 120-frame samples (starts 0, 25, 85 seconds), same-size
1080p NR/NVOF, centered 640x360 linear FP16 crops, completed with zero D3D12
errors. Matched base/raw/filtered observations use the same source frames.

| Segment | Raw residual change | Filtered residual change | Raw edge energy | Filtered edge energy |
| --- | ---: | ---: | ---: | ---: |
| 0 | .00278450 | .00065995 | .02048084 | .02153499 |
| 25 | .00039777 | .00011532 | .00429858 | .00436498 |
| 85 | .00069813 | .00020498 | .00298797 | .00299468 |

Motion-compensated stable-source residual variation falls about 71-76%.
This measures enhancement stability, not independent source-noise removal.
Global edge energy in these crops does not decrease; that alone cannot prove
detail preservation. Inspected contact sheets show no obvious duplicate edges
in sampled frames. p001 is mainly screen/text material, including pixel-game
content. Fast-gameplay ghosting and complete temporal viewing remain unverified.

## C. FG and released 1.4.3 comparison

Sequential 30-second original-4K tests, NR1080, Performance flow, 4K target,
temporal off. SR bypasses at equal input/output size. Counts are software
submissions, not scanout. Trace metrics cover the final bounded window.

| Backend | Result | Retained submit/s | P95 gap ms | Max gap ms |
| --- | --- | ---: | ---: | ---: |
| DLSS 2X | Lifecycle and 95% throughput threshold pass | 120.002 | 8.745 | 9.077 |
| DLSS 4X | Lifecycle and 95% throughput threshold pass | 240.001 | 4.541 | 5.087 |
| DLSS 6X | Lifecycle only; uniform cadence fails | 317.056 | 4.200 | 17.157 |
| XeSS 2X | Provider throughput threshold pass | 120 provider / 60 app | Not measured inside SDK | Not measured inside SDK |
| XeSS 4X | Provider throughput threshold pass | 240 provider / 60 app | Not measured inside SDK | Not measured inside SDK |
| XeSS 6X | Unsupported, test fails as expected | N/A | N/A | N/A |

DLSS 6X retained 14 gaps exceeding 16.667 ms and 43 real-only rejected/warmup
batches. A zero minimum-throughput threshold was used for that lifecycle test;
exit 0 must not be advertised as successful 360 fps or uniform cadence.
The local XeSS runtime reports maxInterpolations=3 (4X); requested 6X is not
an implemented mode. No multiplier or proprietary binary was changed.

The actual released 1.4.3 application was also run on the derived1080 true-SR
configuration, temporal off. It reported 1597 source frames and 1599 generated
frames; current clean off reported 1493 and 3978. The old log repeatedly shows
`xess-fg-gate event=suppress/resume` and SDK `enabled=0, framesPresented=1`.
Its final source rate of 57 versus current 54 therefore does NOT demonstrate
better FG service: it repeatedly stops interpolation. The current gate removal
is retained. The provider pacing implementation remains unchanged. No reliable
capture of SDK-generated pixels was obtained, so XeSS jelly/visual superiority
over 1.4.3 is not accepted or claimed.

Two sequential 120-second tests, temporal on, original 4K60 (SR bypass),
NR1080 and Performance flow, both exit 0 at the 95% throughput threshold.
XeSS provider mean is 240; application mean is about 60. DLSS retained
submission rate is 240.018, gap P95 4.575 ms, maximum 5.584 ms, zero gaps
over 16.667 ms. These gap figures cover the last 6.20 seconds, not all 120.
XeSS internal generated-frame intervals are not observed by this trace.

Additional true-SR UI tests: derived 1080p60, RTX Video SR quality 3 to 4K,
NR, temporal off, identical professional window, 30 seconds each:

| DLSS multiplier | Retained submit/s | Gap P95 ms | Maximum gap ms | Gaps >16.667 ms |
| --- | ---: | ---: | ---: | ---: |
| 2X | 120.003 | 8.730 | 9.114 | 0 |
| 4X | 163.083 | 16.795 | 17.317 | 93 |
| 6X | 162.566 | 16.875 | 17.658 | 151 |

All exit normally and preserve source progress around 60 fps, but 4X and 6X
FAIL full-rate/uniform-cadence acceptance. An exit-zero smoke is only a
lifecycle result. This also prevents applying the SR-bypass 240/317 results
to real 4K upscaling. No speculative scheduling change was retained.

## D. Independent denoise prerequisite result

Investigated the official NVIDIA VFX SDK 1.2.0 Python package 0.1.0.1, cp311,
from https://pypi.nvidia.com/nvidia-vfx/. Wheel SHA256:
`FF7E3E65B4A6ACD507C3C9DDE0D8D881891B0643AF80C4ACC5B7D886463764BD`.
Local-only dependencies are under `E:/项目/Veyra/deps/nvidia-vfx-audit-20260921`.
No new VFX/CUDA DLL is included in Git or the player package.

CuPy sees the RTX 5070; SDK reports 1.2.0. All 17 bundled DLLs explicitly load.
`VideoSuperRes` creation fails with code -2, "The requested feature is not yet
implemented", before quality selection, model load or inference. A corrected
process-local DLL search directory and NV_VIDEO_EFFECTS_PATH did not change
the result. This does not establish that the GPU itself is unsupported.
No denoising output, D3D12/CUDA interop, quality or performance acceptance was
obtained. Stop this bounded attempt; there is no new working denoise switch.

Official denoise quality modes are 8..11. Modes 16..19 are high-bitrate
upscaling with artifact suppression skipped, not additional NR/denoise styles.
Magpie reference is commit 3841698348bfb246623d4acf791984c8b68a577b (GPLv3).
Its synchronous CUDA path and CPU 8-bit HDR fallback were not copied into live
playback. The new probe uses the public Python API only, not copied SDK source.

## Final lifecycle regression

Release build succeeds. CPU live-timing tests and UI contracts pass (384
layout cases). Temporal-on DLSS 4X transport and zoom smokes pass, including
seek, pause/resume and fullscreen focus. Protection smoke initially failed
because the watchdog launched a hidden parent while asserting child overlay
visibility. Its setup now shows the window; the unchanged rectangle, overlay,
clear/cancel and dirty-draft assertions pass. This is a test-setup correction,
not a product overlay change.

The actual professional selectors pass repeated DLSS/XeSS transitions,
2X/4X/6X selection, off, three window sizes, and injected XeSS initialization
failure recovery to DLSS. XeSS explicitly offers only its supported 2X..4X;
switching from DLSS 6X is not proof of XeSS 6X support. Evidence directories:
`final-backends`, `final-backends-reject`, and `ui-protection-temporal-retest.log`.
Final EXE SHA256:
`318C6B0F44ADFC3BD8094DFD5BA39B1570AA56C5D01E9A95BC40430DFD28BDA8`.
Optional PE stack inspection was not run because this build has no linker
map. Hardware capture and physical display latency are not tested here.

## Local package delivery

Retained source checkpoint: `f029707`, tag
`checkpoint/nr-fg-followup-verified-20260921`. Earlier `ab7979c` is preserved.
Local package, not a GitHub release:
`E:/项目/Veyra/test-packages/1.4.4beta-20260921-nr-followup/Veyra-1.4.4beta-win64-portable.zip`.
Size: 472379957 bytes. SHA256:
`A035A8C1956E1C539D39FEC917A273692A685B0A2184A026196700268B453ACB`.

The publisher packaging audit accepts the pinned runtimes and licenses;
no VFX runtime is added. The portable smoke suite passes all seven cases
with isolated PATH and publisher manifests temporarily absent: empty,
baseline, community NR/SR/FG, standard NR/SR/FG, Video SR/NR/FG,
fresh effects-off defaults, and Ampere NR runtime evaluation. The last case
is still on RTX 5070, not an RTX 30 hardware acceptance claim.
Results: `E:/项目/Veyra/verify/1.4.4beta-20260921-nr-followup/result.json`.
Actual packaged professional selectors also pass repeated backend/multiplier
switching and resize: the same directory's `backends/result.json`.

The ZIP was extracted and every manifest entry checked by size and SHA256.
All 124 payload files match; no unlisted/config/log/media/VFX payload.
The EXE and all 40 shaders match the build. Cleanup of the redundant extracted
copy was rejected by automatic approval review ("blocked by policy", no
specific reason supplied); the copy remains under the verification directory.
Final ZIP, runnable staging and evidence remain.
These checks accept packaging and tested lifecycle behavior, not the failed
true-SR cadence, independent denoise or unmeasured XeSS image-quality cases.

## Evidence and commands

- Source/build: `E:/项目/Veyra/worktrees/playback-nr-20260920` and
  `E:/项目/Veyra/build/playback-nr-20260920`.
- Evidence: `E:/项目/Veyra/tests/nr-fg-followup-20260921`, logs under the
  matching `E:/项目/Veyra/logs/nr-fg-followup-20260921` directory.
- Process-local TEMP/TMP: `E:/项目/Veyra/tmp/nr-fg-followup-20260921`.
- Build via vcvars64 and CMake --build, targets veyra,
  veyra_nr_video_quality_probe, veyra_fg_sustained_tests.
- Bounded launches use scripts/run-short-test.ps1; 30-second tests use 95-second
  watchdogs, 120-second tests use 180-second watchdogs.
- Natural probe: `<derived1080> <output> <start> 120`; analyze with
  scripts/acceptance/analyze-nr-natural.py.
- Trace analysis: scripts/acceptance/analyze-fg-cadence.py <trace> --trace
  --output <json>. GPU-ready stamps are CPU observations, not calibrated GPU
  timestamps. Never sum unrelated P95s as an end-to-end latency.
