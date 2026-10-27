# RTX30 startup and capture interruption follow-up

Scope: local isolated branch `codex/post140-field-repair-20260917`, preserving
the user's successful RTX40 result and the native RTX50 path. No publication.
Deliver a complete r2 portable test package with matching source.

## Evidence and Plan

- New RTX3060 logs in `C:/Users/123/Desktop/30系列，无法开启` fail both 2X and
  6X before evaluation: cached FeatureInitResult and Create are 0xBAD0000B.
  Driver update is not requested. Provider identity and SM86 patch preparation
  succeeded. Actual RTX30 hardware remains unavailable locally.
- Upstream RTX40MFG-Unlock commit 33b41835dc39c5d8ab1ef93efb2449be31139c09
  applies metadata compatibility during startup, including NGX initialization.
  Veyra only covered requirements/capabilities/Create. Extend the verified
  Ampere metadata scope to Init, retaining verified restoration on failure.
- Capture screenshots report intermittent simultaneous audio/video stalls;
  no accompanying capture log proves the specific incident's cause.
  CaptureAudioSession::push currently stops counting arriving input when the
  output endpoint reports an error. After 3 seconds AudioInputRecovery then
  restarts the shared A/V graph despite continuing input. Reproduce with a
  four-second owned output outage, fix input progress accounting, and verify
  real missing input still triggers recovery.
- Build, run targeted lifecycle/output-loss regressions, physical capture if
  available, and delivery gate. Package the earlier three scheduling fixes too.

## Status

Implemented both fixes and the startup-seek fix discovered during package smoke.
RTX40 success is user-reported, not a local RTX40 test.
Local adapter is RTX5070 / 616.56. RTX3060 initialization remains a candidate
repair until the same user's hardware runs it; forced routes cannot reproduce
Ampere's native rejection. The child preflight still requires actual successful
Create/Evaluate and does not silently lower the requested multiplier.

## Verification

Build: `cmd.exe /c out\build\scheduling-audit-build.cmd`, directory
`out/build/scheduling-audit-20260918`. Final build logs:
`out/logs/rtx30-capture-build3.log`, `rtx30-capture-build4.log`, exit 0;
final startup-seek build `out/logs/rtx30-startup-fix-build.log`, exit 0.
Build 2 encountered LNK1104 because a test executable was running while linking;
the test exited normally, then build 3 completed. No user process was stopped.
Final EXE SHA256: `656E671F62F005C95F4883AD232482917B2936BD50B2550F80E4461AC6FFEBF8`.
Earlier lifecycle/capture checks used the pre-seek-fix EXE
`0E03B9946997347DAAB4E029183A95B1EAB8BB36C7FC33BF4BBE1369F0FD149F`.

Tests use `scripts/run-short-test.ps1`, timeout 25-100 seconds per test, with
`out/build/post140-hdr-preflight-20260918` added to PATH for dependency DLLs.
All following log prefixes are under `out/logs/`:

| Test | Result |
| --- | --- |
| capture audio `--long-endpoint-loss`, before fix | FAIL: 403 callbacks during outage, only 247/650 counted, 2 false input-recovery decisions |
| same test after fix | PASS: 650/650 counted, zero false decisions, real input-stall policy still fires; `rtx30-capture-outage-fixed` |
| physical USB3 source `--output-outage` | PASS: 421 video frames in 7 s, max read gap 16.833 ms, 703 audio blocks, endpoint recovers without restarting graph; `rtx30-capture-physical-outage` |
| forced Ampere lifecycle | PASS 6,2,5,3,4,6X with actual generation and reset/shutdown; `rtx30-init-ampere-lifecycle` |
| forced Ampere `init-failure` | PASS injected Init failure restores metadata/session, followed by all six successful lifecycles; `rtx30-init-failure-lifecycle` |
| forced Ada lifecycle | PASS; `rtx30-ada-regression` |
| native RTX50 lifecycle | PASS; no compatibility session/patch log; `rtx30-native-regression` |
| forced Ampere `mfg6-exact-rgb-planar-detail` | PASS 55/55 generated images meet hash, non-blend and displacement checks; retained slot overwrite rejected; `rtx30-content6` |
| stereo / 5.1 `--jitter` | PASS 500 blocks each, no additional resets/underruns, P95 skew 23.970/23.001 ms; `rtx30-audio-jitter`, `rtx30-audio-jitter51` |

The long output outage is injected into the owned audio renderer, not a physical
speaker unplug. Physical capture test uses the real USB3 card and the product's
recoverAudio call. It proves this internal restart defect is fixed, not that the
user's screenshot has the same cause or that all intermittent stalls are gone.
Output failure still discards unavailable audio as before; no growing recovery
queue or stale audio replay is introduced. Fatal audio worker errors retain the
existing input recovery path.

GitHub rechecked with `gh api repos/<repo>/commits/HEAD`: RTX40MFG-Unlock
`b77e6e55c9211faf58932fc67eaaad9feb9df934` preserves GPU payload/provider policy;
dlssg_for_sm86 `5e79459c2d521f8c3276ce9ee02342c0e2686982` changes README wording.
The port remains pinned and attributed to the audited MIT commit above.
No runtime was replaced or patched on disk. DLSSG SHA256 remains
`135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F`.

## Additional Startup Seek Deadlock

The first package smoke failed in `community6`: NR startup overlapped the scripted
pause/seek/resume. Audio's initial prefill was interrupted by seek. The subsequent
seek anchored real PCM successfully, but left `endpointRecovering_` true. Video
waited for audio recovery, while audio waited for the first presented video frame.
Both original and repeat package logs show no presented frame. Failures remain in
`out/logs/rtx30-package-smoke.log`, `rtx30-package-community-repro.log` and their
`package-post140-smoke-20260918-r2-final*` directories.

`src/sink/WasapiAudioSink.cpp` now clears recovery after a successful seek anchor,
or a successful exhausted-audio seek with a healthy endpoint. Failed anchoring
still enters recovery. `tests/integration/AudioTimelineTests.cpp --startup-seek`
opens real muted WASAPI three times and seeks immediately during initialization.
Before the fix all three attempts failed the recovery-state assertion; after the
fix all three pass, including actual PCM anchoring, held clock, release by video
and bounded queue. Full audio timeline regression also passes.

Commands (all through `scripts/run-short-test.ps1`):

- `-Exe out/build/scheduling-audit-20260918/veyra_audio_timeline_tests.exe -Arguments @('out/logs/rtx30-startup-seek-before','--startup-seek') -TimeoutSeconds 25 -LogPrefix out/logs/rtx30-startup-seek-before`: exit 1 before product fix.
- Same command with prefix/argument `rtx30-startup-seek-fixed`: exit 0 after fix.
- Same executable, `-Arguments @('out/logs/rtx30-audio-timeline-final') -TimeoutSeconds 60 -LogPrefix out/logs/rtx30-audio-timeline-final`: exit 0.

Final delivery: `scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/scheduling-audit-20260918`
passes all 26 checks in 62.7948212 s. Evidence:
`logs/delivery/b7425358ff0342339bca643c7483a5a8/result.json` and
`out/logs/rtx30-final-delivery.log`. The previous gate on the earlier binary remains
in `logs/delivery/fa45435aa440435dbb7dff31ac9cb57d/result.json`.

## Changed Files

- `include/veyra/ngx/FgCompatibilitySession.h`, `src/ngx/FgCompatibilitySession.cpp`,
  `src/ngx/NgxCoreHost.cpp`, `src/pipeline/EnhanceGraph.cpp`,
  `tests/integration/FgLifecycleTests.cpp`, `THIRD_PARTY_NOTICES.md`: Init scope,
  restoration, injection regression and attribution.
- `include/veyra/sink/CaptureAudioSession.h`, `src/sink/CaptureAudioSession.cpp`,
  `tests/integration/CaptureAudioTests.cpp`, `tests/integration/CaptureSourceTests.cpp`:
  input liveness during output recovery and synthetic/physical regressions.
- `src/sink/WasapiAudioSink.cpp`, `tests/integration/AudioTimelineTests.cpp`:
  startup seek recovery publication and real endpoint regression.
- This document and `docs/WORKLOG.md`: evidence and delivery state.

## Portable Package Verification

Command: `scripts/package-portable.ps1 -Root . -Version 1.4.0 -Label '-test-20260918-r2' -OutputDirectory C:/veyra-test-packages/post140-20260918-r2 -BuildDirectory out/build/scheduling-audit-20260918`.
Final build log: `out/logs/rtx30-package-build-final.log`. The first attempt failed
for missing FFmpeg dependency DLLs in the build directory. These were copied from
the actual patched FFmpeg installation, with provenance hashes checked by the
packager. No older/unpatched replacement was used. Earlier incomplete and
pre-startup-fix packages are preserved in clearly named subdirectories.

`out/tmp/finalize-post140-package.ps1 -SkipSource` adds the test instructions and
HDE/RTX40MFG license texts, updates the manifest, checks all archive file hashes,
rejects SDK/debug/private media/configuration payloads, and extracts an independent
verification directory. Result: 113 entries, audit PASS.
Portable archive: `C:/veyra-test-packages/post140-20260918-r2/Veyra-1.4.0-test-20260918-r2-win64-portable.zip`.
SHA256: `BE603292C5D776D88D5FE33E02C3B97E95A4306C57D491B845E708CEE9BE8E1B`.

`out/tmp/smoke-post140-package.ps1 -OutputSuffix '-verified'` launches the extracted
EXE with only Windows directories in PATH and TEMP as the working directory.
All FFmpeg/VC runtime and selected NR/FG modules must resolve inside the package.
The original MKV pause/seek/resume timing is unchanged. All seven cases pass
process, real NR/FG work, module-path and saved-frame checks:

| Case | Decoded frames | Generated frames |
| --- | ---: | ---: |
| Baseline | 188 | 0 |
| Original NR + 6X | 120 | 470 |
| Community NR + 6X | 113 | 435 |
| Ampere NR + 6X | 118 | 460 |
| Forced Ampere 2X | 81 | 55 |
| Forced Ampere 6X | 63 | 185 |
| Forced Ada 6X | 64 | 190 |

Evidence: `out/logs/rtx30-package-smoke-final.log` and
`out/logs/package-post140-smoke-20260918-r2-final-verified/result.json`.
Forced Ampere 6X logs show Init `0x1`, restored metadata, DLSSG Create `0x1`,
warm-up Evaluate `0x1` and actual generated presentation. This is RTX5070 evidence.

Visual inspection found the enhanced MKV snapshots entirely black at startup;
the baseline snapshot shows the intro logo. The source at approximately one second
is almost black. These snapshots cannot establish correct enhanced image content,
so a separate moving-color fixture with real audio was also tested below.
No product/test timing was changed to avoid the
startup-seek regression.

Fixture command: `ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=1920x1080:rate=24' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 12 -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -c:a ac3 -ac 2 out/logs/rtx30-package-motion-fixture.mkv`.
This diagnostic fixture is not included in any delivered archive.
Run: `out/tmp/smoke-post140-package.ps1 -OutputSuffix '-motion' -MediaOverride 'C:/Users/123/Desktop/Veyra DLSS Video Player/out/logs/rtx30-package-motion-fixture.mkv' -RequireNonblank`.
All seven cases PASS, including the same transport operations and module checks.
Each screenshot has 8160/8160 sampled pixels above the nonblack threshold, with
RGB-sum ranges spanning 423 or more levels. Forced Ampere 6X and community 6X
screenshots were visually inspected: distinct test patterns and time labels are
visible. This proves meaningful saved output, not interpolation accuracy.
Generated counts in case order above: 0,565,580,560,63,325,455.
Evidence: `out/logs/rtx30-package-motion-smoke.log` and
`out/logs/package-post140-smoke-20260918-r2-final-motion/result.json`.

Unchanged dependency corresponding-source archives were copied and hash-verified:

- `Veyra-1.4.0-test-20260918-r1-FFmpeg-source.zip`:
  `A83293849960802E9AFD0E685E7E094B986E5FED19FA8371410E7E2E6F0DAAF7`.
  The r1 filename is retained because the patched FFmpeg dependency is unchanged.
- `Veyra-1.4.0-RemotePlay-source.zip`:
  `1D69DDA5BB1AFA3B78E917569BF396338A338BF2184D4458C2F1E1D88CE881DF`.
  The new r2 application snapshot supersedes any old application tag guidance.

Local repair/build/package checks are complete. The matching application source
snapshot is `Veyra-1.4.0-test-20260918-r2-source.zip`, generated from the actual
worktree after this record, with SHA256 sidecars alongside all archives. Source
verification hashes each entry against the worktree and rejects binary/media
payloads. Packaging evidence and hashes are recorded beside the archives.
`git diff --check` passes; no runtime/SDK/test media was added to source Git.
No merge, push or publication was performed. Target-hardware acceptance remains
open, so the broader repair goal is not marked fully achieved.
Next user acceptance: RTX3060 2X then 6X and prolonged capture playback. Retain
the main log plus the matching fg-probe log if initialization still fails.
