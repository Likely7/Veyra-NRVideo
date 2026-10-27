# Scheduling Chain Audit, 2026-09-18

Scope: user requested a full-chain scheduling audit while testing the r1
package. Work stays on `codex/post140-field-repair-20260917`, preserving the
existing repairs. The r1 package and the user's running player are unchanged.
No merge, push, release, runtime replacement, or hardware compatibility claim.

Changed in this audit: `src/engine/EngineController.cpp`,
`include/veyra/engine/VideoPresenter.h`, `src/engine/VideoExportJob.cpp`,
`tests/integration/SchedulingChainTests.cpp`,
`tests/integration/FgPresentationTests.cpp`, `CMakeLists.txt`, this report and
`docs/WORKLOG.md`. Earlier field repairs in these files are preserved.

## Confirmed Defects

1. Silent-file master clock reanchored after every successful presentation in
   `EngineController.cpp`. This erased accumulated lateness. A real 60 fps
   silent-video regression with 60 ms injected processing work advanced only
   0.750 media seconds in 3.009 wall seconds (0.249x), with zero skipped
   opportunities. Keep the clock origin until an explicit transport reanchor.
   Fixed run: 2.850/3.010 seconds (0.947x), 129 skipped opportunities, then
   catches up. Pause, paused seek and resume passed.
2. DLSS presentation now uses a private queue/ring, but the admission service
   calculation subtracted waits from the shared producer ring. Read allocator
   wait deltas from the ring actually used by `VideoPresenter`. CPU service
   must not charge a GPU dependency wait a second time. This establishes the
   accounting defect, not its contribution on the user's RTX30/40 machines.
3. Export's scope guard captured `encoder.get()` before the encoder opened,
   leaving it null. Cancellation/errors could destroy callback captures before
   encoder teardown drained pending packets. The guard now owns a reference
   to the encoder owner and resets it while all writer captures remain alive.
   Real NVENC cancellation after 1, 3 and 5 submitted source frames passed;
   subsequent normal 12-frame export decoded and validated successfully.
   No pre-fix crash or sanitizer result is claimed for this lifetime defect.

## Chain Review

| Area | Finding |
| --- | --- |
| File clock / preview | Silent clock defect fixed; audio clock has a separate exhausted-stream wall-clock handoff and endpoint recovery freeze. Preview skips retain real PTS and reset history. |
| File demux / decode | Still synchronous on the GPU owner. Slow read/decode can block advancement of queued presentation. Architectural risk; no measured slow-disk reproduction in this audit. |
| Physical capture | Nonblocking latest-frame read; compressed decode has a separate worker, bounded payload queue and leased frame pool. No new proven persistent throttle found. |
| PS5 | Separate decode and PCM feeding workers; capacity-one decoded mailbox; interruptible reconnect waits. Actual console/network session not tested in this audit. |
| Flow / NR / SR / color | Creation waits and teardown drains are lifecycle operations; steady processing uses command slots and GPU fence dependencies. Actual serial GPU cost remains real, even if aggregate GPU utilization looks low. |
| DLSS presentation | Producer/consumer fences and private ring are explicit. Service accounting fixed. `fgTiming` uses the shared fence but current callers only invoke it for XeSS/FSR, which use that fence; no cross-fence bug claimed for this call path. |
| XeSS | Actual overload/recovery test passed: 60 ms injected work suppresses generation; reducing work to 10 ms restores it within a 5-second observation, with 233 additional generated frames and 1.033x media pace. |
| File / capture audio | Separate WASAPI owner; capture backlog bounded; transport reanchors distinct from steady playback. Fresh physical-capture video observations use matching original arrival. |
| Video export | Full frame processing, bounded four-slot NVENC output, CFR holds explicitly counted. Per-batch generation resolution still serializes export with GPU completion, including non-FG jobs. This is throughput headroom, not evidence of persistent adaptive throttling. |
| AMD/Intel export | MF encoder copies Y/UV to CPU and waits each frame. Existing implementation conflicts with the no-readback performance contract; not a zero-copy path. Needs a separate D3D11/DXGI surface handoff implementation and actual adapter validation. |
| Export policy | Playback-priority option intentionally waits 50 ms between source frames. Final export verification decodes the completed file; both can lower visible utilization without being an admission bug. |
| Images / paused view | No media pacing or preview frame dropping. GPU completion waits protect a requested full result. |

## Evidence

Fixture command:

```powershell
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=640x360:rate=60 -t 24 -an -c:v libx264 -preset ultrafast -y out/scheduling-silent-20260918.mp4
```

New test: `tests/integration/SchedulingChainTests.cpp` (playback, XeSS and
export modes); private-ring accounting regression extends
`tests/integration/FgPresentationTests.cpp`.

Logs:
- `out/logs/scheduling-before-playback.stdout.log`: reproduces silent slow motion.
- `out/logs/scheduling-fixed-playback.stdout.log`: fixed timing and transport pass.
- `out/logs/scheduling-fixed-export.stdout.log`: pending cancellation and export pass.
- `out/logs/scheduling-build-before.log`: initial test build failed on wrong
  ExportCounts field names, corrected to source/encoded.
- `out/logs/scheduling-build-before2.log`: baseline test build passed.
- `out/logs/scheduling-build-fixed.log`: libraries/tests built; executable link
  failed LNK1104 because the user is running that executable. Do not stop it.
- `out/logs/scheduling-isolated-build.log`: separate complete build passed (561 steps).
- `out/logs/scheduling-isolated-build-final.log`: final presentation-test rebuild passed.
- `out/logs/scheduling-fixed-export-audio.stdout.log`: the supplied MKV with audio
  passed cancellation at 1/3/5 source frames and subsequent 12-frame export.
- `out/logs/scheduling-xess-before.stdout.log`: process launch failed with
  -1073741515, before main, because the new build directory lacked dependency
  DLLs. This was a test environment failure, not a XeSS result.
- `out/logs/scheduling-xess-before2.stdout.log`: dependency PATH corrected;
  XeSS recovery, silent clock and transport checks passed. Despite the log
  filename, this run includes the silent-clock fix.
- `out/logs/scheduling-private-presentation.stdout.log`: producer allocator wait
  54.8609 ms, private-ring wait 0 ms; 4X/6X/4X generated 114/190/114 frames;
  presented texture max pixel error 0, D3D12 errors 0. This verifies texture
  presentation, not the visual quality or temporal position of interpolation.
- `out/logs/scheduling-dlss4.stdout.log` and `scheduling-dlss6.stdout.log`:
  NR + DLSS 4X/6X, 30 seconds each, injected 55 ms processing work at seconds
  15-17, both recovered and exited 0. Recovered mean submissions 142.75/203.375
  per second. These runs replay the MKV as a simulated live source: its actual
  processing cadence exceeds the nominal 24 fps, and expired generated frames
  remain (including after recovery). Neither the submission counts nor the
  nominal-target assertion prove correct 24 fps pacing, full delivery of all
  generated frames, display scanout rate, or real USB/PS5 capture throughput.
- `out/logs/scheduling-audio-underrate.stdout.log`: simulated 51/60 video
  underrate, wall 4008.89 ms / audio 3988.90 ms, no additional audio pauses,
  underruns or PCM overruns; passed.
- `out/logs/scheduling-veyra_presentation_worker_tests.log`,
  `scheduling-veyra_realtime_preview_tests.log`, and
  `scheduling-veyra_repair_contract_tests.log`: passed; repair contracts
  205 checks, zero failures.
- `logs/delivery/62df9939d564489fb42ca7b24292fd08/result.json`: fresh delivery
  gate passed, 26 checks, 62.4984434 seconds. Includes actual NR/NVOF, native
  4K correctness, realtime-profile playback, transport, color page, images,
  native 4K H.264/HEVC exports with audio, cancellation and compressed capture
  decoder hardware/software parity (ALL PASS, not skipped). The decoder test
  uses files, not a physical capture device. Quality probe: 60 NR evaluations,
  zero failures, D3D diagnostics errors and normal-path readbacks.
- `out/logs/scheduling-dlss6/engine.log`: NR Feature 18 Create `0x1`, DLSSG
  Create `0x1`, FG warm-up Evaluate `0x1`, maxGeneratedFrames=5.
- Independent EXE SHA256:
  `BD9C8B275560E4DDBD0BDB20DCE868818311E6FED54678BFF1550B4D178A2249`.
- `git diff --check` passed; no proprietary runtime, SDK or test media was
  added to source tracking. Existing LF/CRLF conversion warnings remain.

Build command: `cmd.exe /c out\build\scheduling-audit-build.cmd`.
The independent build retains patched FFmpeg at
`C:/veyra-deps/ffmpeg-ps5-dav1d-installed`, the existing approved runtime paths,
and RemotePlay dependencies. It is a developer build, not a new portable pack.

Commands for the isolated tests (each run limited to at most 80 seconds):

```powershell
$env:PATH=(Resolve-Path out/build/post140-hdr-preflight-20260918).Path+';'+$env:PATH
& scripts/run-short-test.ps1 -Exe out/build/scheduling-audit-20260918/veyra_scheduling_chain_tests.exe -Arguments @('out/scheduling-silent-20260918.mp4','out/logs/scheduling-xess-before2','xess') -TimeoutSeconds 60 -LogPrefix out/logs/scheduling-xess-before2
& scripts/run-short-test.ps1 -Exe out/build/scheduling-audit-20260918/veyra_fg_presentation_tests.exe -Arguments @('out/logs/scheduling-private-presentation') -TimeoutSeconds 60 -LogPrefix out/logs/scheduling-private-presentation
foreach($multiplier in @('4','6')) {
    & scripts/run-short-test.ps1 -Exe out/build/scheduling-audit-20260918/veyra_fg_sustained_tests.exe -Arguments @('C:/Users/123/Desktop/Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv',"out/logs/scheduling-dlss$multiplier",$multiplier,'on','30','nr','0.95') -TimeoutSeconds 80 -LogPrefix "out/logs/scheduling-dlss$multiplier"
}
& scripts/run-short-test.ps1 -Exe out/build/scheduling-audit-20260918/veyra_audio_timeline_tests.exe -Arguments @('out/logs/scheduling-audio-underrate','--underrate') -TimeoutSeconds 30 -LogPrefix out/logs/scheduling-audio-underrate
& scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/scheduling-audit-20260918
```

Completed test directories intentionally reject overwriting export files; use
fresh output paths when rerunning export modes.

## Remaining Boundaries

Hardware: RTX5070, driver 616.56. RTX30/40/5090, physical capture, AMD/Intel
export and a real PS5 session were not exercised in this audit. The previous
4K full NR+SR throughput failures and interpolation-position mismatch remain
open; short lifecycle checks do not supersede those failures.

Next acceptance task: collect the user's r1 RTX30/40 results and full logs,
then reproduce any remaining limiter state with the same settings. The three
additional fixes here are only in the branch/developer build, not in r1.
