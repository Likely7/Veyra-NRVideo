# Capture VRR report: automatic audio synchronization audit

## Original audit (before repair)

User reports that some players experience seconds of audio delay with PS5 VRR
and automatic audio compensation; disabling compensation restores normal sound.
User believes the affected input is a capture card. The affected application
version, card, actual negotiated HDMI/USB cadence and incident log are unknown.

This audit establishes a software failure mechanism, not a physical VRR root
cause. Ordinary variable frame arrival with consistent timestamps did not
produce excessive buffering in the synthetic test. Video-only timestamp offset
did: the current production audio session interpreted it as additional video
delay and buffered over one second of real PCM. The same input with compensation
disabled returned to a short queue. No product synchronization policy changed.

## Code findings

- `src/sink/CaptureAudioSession.cpp` computes automatic delay as
  `videoHost - videoPts - audioIngressMapping`, clamped to 0..1500 ms.
  Raw/converting/PCM storage has a separate 2000 ms budget. These limits are not
  additive measurements of end-to-end latency.
- Freshness checks whether video presentation was reported in the last 500 ms;
  they do not validate that video and audio PTS origins/rates remain comparable.
- A correction error over 60 ms, after the reset cooldown, resets the output
  anchor. Subsequent startup waits for the calculated mapping. Consequently,
  an erroneous target can cause real waiting and PCM accumulation, rather than
  merely displaying an incorrect number.
- DirectShow capture forwards video and audio sample timestamps. Sharing one
  graph/reference clock does not by itself validate driver timestamp behavior.
  The WASAPI input path added locally uses a separate video-ingress mapping;
  this audit did not exercise that adapter or conclude it has identical behavior.
- The relevant formula and 1500/2000 ms budgets are also present in tag
  `v1.2.0`. Recent audio waveform fixes retained this synchronization policy.
- `ebba6f1` reanchors live FG deadlines, but does not rewrite the source PTS
  passed to audio synchronization. The prior FG repair therefore does not
  establish that this audio problem is fixed.

Given only the present-time/PTS difference, true upstream video delay and an
invalid video timestamp offset can look identical. A robust repair needs input
arrival/clock evidence, not an arbitrary smaller delay constant.

## Reproduction

Added optional `--sync-clock-audit` to
`tests/integration/CaptureAudioTests.cpp`, using the production
`CaptureAudioSession`, synthetic continuous 48 kHz PCM and real WASAPI output at
gain zero. No microphone, capture device, PS5 connection or loopback was opened.
Video observations were synthetic; there was no actual video/GPU processing.
The fixture's video residence was 35 ms with changing observation intervals.

| Scenario | Max compensation | Max software PCM queue | Result |
| --- | ---: | ---: | --- |
| Aligned timestamps, variable observation cadence | 35.50 ms | 13.75 ms | PASS |
| Video PTS shifted back 1200 ms, same synthetic residence | 1235.51 ms | 1214.56 ms | FAIL |
| Same shifted PTS, compensation disabled | 0 ms | 10 ms | PASS |

Output endpoint capacity was 22 ms. During the offset scenario its queue was
22 ms while the software held about 1.2 seconds of PCM. No overflow occurred.
The auto-mode skew eventually looked small (about -15 ms) despite the wrong
target: internal timestamp agreement is not proof of physical A/V agreement.

The failure is intentional evidence of an unresolved product behavior; the
diagnostic returns exit 1 and is not registered as an always-passing gate.
It is neither physical VRR reproduction nor a speaker latency measurement.
It does not establish the user's exact multi-second duration; automatic target
alone is capped at 1.5 seconds, and other delays require incident evidence.

Commands executed:

```powershell
cmd.exe /c out\build\veyra-build-x64-release.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/scheduler-short-test.ps1 -Name capture-sync-clock-audit-20260915 -Exe out/build/audio-continuity-repair-20260915/veyra_capture_audio_tests.exe -TestArgs '--sync-clock-audit'
git diff --check
```

Build exit 0, 2/2 incremental targets. Diagnostic exit 1 in 11.60 seconds.
Evidence: `logs/scheduler-repair-20260910/capture-sync-clock-audit-20260915.stdout.log`
and `.result.json`; test EXE SHA256
`3AFEE6EAAA704CD40754513BD6DEE8A494C97CD4FA1D7A5B6C6E29292C93F668`.
Only the test executable was rebuilt. No runtime/SDK changed, no RTX
Create/Evaluate, physical capture or audible acceptance ran, and no publication.

## Proposed repair and acceptance

1. Carry matching video ingress/presentation observations, source epoch and
   original/generated frame identity into the shared synchronization estimator.
   Record clock validity, raw requested delay, accepted target, actual PCM and
   endpoint queues, input PTS/arrival trends and reasons for reanchoring.
2. Accept cross-stream PTS compensation only with a validated clock relationship.
   When the relationship becomes inconsistent, stop increasing compensation
   from that evidence and use a bounded fallback based on observed local video
   residence. A fallback cannot infer unmeasured capture hardware A/V offset;
   expose the limitation and preserve manual adjustment. Keep real frame times
   and A/B interpolation lineage; do not replace VRR timestamps by frame count.
3. Separate audio device rate correction from changes in the video delay target.
   Apply stable target estimation and explicit discontinuity recovery rather
   than treating a single presentation outlier as oscillator drift. Preserve
   continuous resampling history and explicit queue/recovery accounting.
4. Make this diagnostic pass through the production estimator with consistent
   provenance. Also test genuine 80/400/900 ms video residence (not offsets),
   independent video clock drift, common A/V offsets, correct variable PTS,
   unmarked/marked timestamp jumps, FG toggles and stale video. Preserve the
   existing audio continuity and +/-1000 ppm tests. Test adapters independently;
   PS5 network streaming and files must not inherit capture-only assumptions.
5. Verify the affected card in the same scene with VRR on/off and automatic
   compensation on/off. Obtain the version and incident log before claiming
   VRR causality or the user's exact symptom fixed.

Temporary workaround: select compensation off or a small user-calibrated manual
offset. This removes automatic video-delay following; it does not guarantee
perfect long-term synchronization and is not the final fix.

## Authorized implementation

The user subsequently authorized the local repair. The failure and unchanged
policy described above refer to the before-repair executable, not the candidate.

- `CaptureSyncTarget` validates cross-stream PTS against the matching original
  frame's host arrival-to-presentation residence. A residual above 80 ms or an
  invalid observation latches fallback to that local residence. Recovery needs
  residual within 40 ms for two seconds of fresh observations. Five actual
  observations provide median filtering; repeated audio-thread polls do not
  become extra video samples. Real 400/900 ms local processing delay is retained.
- The 80 ms threshold is an explicit confidence policy, not a measurement of
  capture-card latency. Unknown upstream A/V offset larger than that can be
  rejected even if physically real. Fallback estimates local synchronization;
  it cannot calibrate HDMI/card buffering or acoustic output. Manual adjustment
  remains available. The 1500 ms automatic and 2000 ms queue bounds remain.
- Engine presentation reports matching arrival only for real, non-cached frames;
  generated frames cannot shift the synchronization anchor. Existing scheduler
  cancellation/reset owns stale jobs. Video-only reset invalidates observations
  while holding the last audio target; it does not reset healthy PCM. Actual
  audio discontinuities, endpoint recreation and explicit mode changes reset
  the estimator. No independent source-epoch protocol was introduced.
- DirectShow retains its PTS with this validation. The separate WASAPI input
  adapter now uses the matching original arrival on the host axis, replacing
  the latest unrelated callback's video mapping. The PS5 no-arrival overload
  retains its separate clock contract; file audio does not use this estimator.
- The detail panel explicitly labels timestamp fallback and the estimated
  skew. Logs retain raw requested delay/skew, local residence, accepted target,
  freshness, matching input arrival, PCM/endpoint queues and fallback changes.
- Audio oscillator correction retains the existing continuous resampler and
  bounded controller. The repair filters video targets and rejects inconsistent
  clock evidence; it is not a new independent oscillator-estimation algorithm.

### Additional startup backlog defect

The first post-repair suite exposed a separate, genuine failure: WASAPI opening
took about 680 ms, but the session started after converting only roughly 40 ms
of the raw input backlog. Startup expiry could see only that small converted
portion. The remaining raw backlog then kept sound about 600 ms behind even
after repeated reanchors. Initial jitter regression failed at P95 641.452 ms.

A deterministic `--slow-start` case delays this session's own initial endpoint
open by 650 ms. Before the backlog fix it failed at P95 646.558 ms with four
additional reanchors. Evidence remains in
`logs/scheduler-repair-20260910/capture-slow-start-before-20260915.*`;
test hash `A9958825429C8F2CBF2FA18FA0AD24FFDF05223E3A44976975AA92A75AE77A28`.

Before starting or reanchoring, the session now converts its bounded pending
input before applying the existing expired-PCM policy. Healthy running playback
retains the existing fill policy. Recovery discards are counted and logged as
`capture-audio-reanchor` and exposed separately in details. This is startup/live
recovery accounting, not steady playback sample dropping to disguise sync.

## Repair verification

Final candidate: `out/build/audio-continuity-repair-20260915/veyra.exe`,
11,463,168 bytes, SHA256
`F060238E39311D9E2D3D8B8CB4EA50B0CBB6E4BD24E67BC10D90A249F417BACC`.
Audio test SHA256:
`F2DC850D603DE23964C02864AA603704856560DB8A859D70D76DF42C0282CED1`.

Commands executed:

```powershell
cmd.exe /c out\build\veyra-build-x64-release.cmd
out/build/audio-continuity-repair-20260915/veyra_repair_contract_tests.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -LogDirectory logs/capture-sync-repair-20260915/final
scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra.exe -Arguments @('--smoke-empty','--smoke-seconds','2','--no-nr','--no-sr','--no-fg') -TimeoutSeconds 30 -LogPrefix logs/capture-sync-repair-20260915/gui-smoke
git diff --check
```

- Build succeeded (initial repair 60/60; final incremental 4/4), RemotePlay ON,
  patched FFmpeg/dav1d unchanged. Final output:
  `logs/capture-sync-repair-20260915/build-final.log`. During the added slow-start
  test, compilation failed twice because its assertion referenced `kAudioRate`
  without the defining header. Replaced with the fixture's 24,000-frame
  half-second threshold, then rebuilt successfully. Failures were not test passes.
- CPU contract suite: 123 checks, zero failures; output `contracts.log` in the
  same evidence directory. Covers ten-minute simulated independent clock drift,
  fallback/recovery, real residence, invalid observations and latency lineage.
- Final audio short suite: 16 processes, all exit 0; `final/results.json`.
  This includes 47 DSP checks, 18 WASAPI input-clock checks, stereo/5.1 jitter,
  real output-endpoint recovery, multichannel, file timeline, manual/off modes,
  common input offsets, graph reset and queue bounds. The no-arrival legacy
  clock contract ran separately and passed; no physical PS5 was connected.
- Final timestamp-offset test: aligned max target 35.4393 ms, queue 23.625 ms;
  1200 ms video-only offset max target 35 ms, queue 10 ms; compensation off
  target 0 ms, queue 10 ms. Offset scenario has only the startup reset.
- Deterministic slow-start fix: P95 skew 20.1321 ms vs 646.558 ms before;
  zero additional resets/underruns. Logs show 648.396 ms expired PCM removed at
  startup. Evidence: `capture-slow-start-fixed-20260915.*` under
  `logs/scheduler-repair-20260910/`; final full-suite slow-start also passed.
- GUI empty-start smoke exit 0, `failed=false`, zero processed/generated frames.
  This checks startup only, not playback or GPU enhancement success.
- `git diff --check` passed (existing CRLF notices); no tracked DLL/LIB/EXE/model
  or PYC files. No runtime replacement, version change, push or Release.

Two 120-second real-WASAPI/synthetic-input clock-drift runs completed with exit 0
using the same script with `-DriftOnly`. At input speed 1.001, P95 skew was
4.22906 ms; at 0.999, it was 4.23319 ms. Both reported missing=0, resets=1
(startup only), and queue high water 89.6458 ms. Evidence:
`logs/capture-sync-repair-20260915/final/drift-results.json` and the adjacent
`drift-fast.stdout.log` / `drift-slow.stdout.log` files.
All audio output tests are muted; no acoustic acceptance is
claimed. No physical capture/WASAPI input recording, VRR on/off comparison,
RTX Create/Evaluate or full GPU delivery gate ran in this audio-focused repair.
Next acceptance: affected user's same card/scene with automatic sync and VRR
on/off, together with the new target/queue logs. Genuine upstream hardware
offset and actual screen/speaker timing remain unmeasured.
