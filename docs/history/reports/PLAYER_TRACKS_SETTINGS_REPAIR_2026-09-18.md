# Player field repairs, 2026-09-18

Branch: `codex/post140-field-repair-20260917`. This extends the post-1.4.0
repair without merging, publishing, or adding local runtime/media to Git.

## Initial evidence

- RTX 4060 Ti log: community NR Feature 18 creates successfully before restart;
  subsequent original NR selection fails with `0xFFFFFFFFBAD00001`. WM_CLOSE
  unconditionally logs a corrupt-preferences warning and never calls save.
- NR then XeSS log: first Present fails with `0x887A0005`, device removal
  reason `0x887A002B`, e.g. 2026-09-17T13:51:11.608Z. Initialization succeeds.
  Establish the resource/queue failure with a local sequence test; do not
  label this insufficient GPU performance or claim an untested fix.
- Subtitle popup returns an index correctly. Existing flat menu mixes 32 main
  and 32 secondary tracks with settings; selecting a track does not enable
  subtitles. Async listing currently withholds cue updates until a whole-file
  scan completes. Improve selection and incremental availability.
- File audio decoder selects only av_find_best_stream. Add enumeration and
  explicit selection, preserving position, pause, gain and channel layout.
- Measure open and seek against the supplied MKV, including audio/probing and
  subtitle work. Retain truthful timestamps and complete export processing.
- Prevent idle display/system sleep and screen saver only while playing.

## Validation contract

Build, preference restart tests (including bypass and NR runtime choice),
subtitle scrolling/selection with 32 tracks, selected audio PCM/timestamps,
open/seek timings, NR/XeSS in both orders, playback power lifecycle and delivery
gate. Hardware here is RTX 5070: RTX 30/40/5090 remains explicitly unverified.
Record commands, results and remaining limits below and in WORKLOG.

## Implemented

- `AppShell.cpp` splits the subtitle popup into audio, primary subtitle and
  secondary subtitle selectors. Choosing a subtitle enables its display.
  Audio rows map to container stream IDs, not popup indices. Selection uses
  the current session ID so a stale request cannot affect a different file.
- `Subtitles.cpp` publishes cues after 250 ms and subsequently once per second
  during its bounded, cancellable single demux scan. Opening playback does not
  wait for the full subtitle scan. User choices, offsets and external tracks
  survive progressive updates. Existing text/ASS support is not a claim of
  newly implemented bitmap subtitle support.
- `AudioTrack.h`, `WasapiAudioSink`, `EngineController` enumerate audio language,
  title, codec and channel count. Switching stops the decoder worker, opens the
  candidate decoder before replacing the current one, restarts the endpoint
  with the selected channel layout and restores position/pause/volume. Invalid
  stream IDs are rejected. MKV audio seeks now use the container video index
  followed by PCM timestamp trimming, avoiding a long scan from a sparse audio
  index. `SeekPreview.h` avoids submitting the same final seek twice.
- `ExportJobManager`, worker IPC v2 and `VideoExportJob` carry and validate the
  selected audio stream. The legacy `EngineController::startExport` path also
  preserves it across settings conversion. Export retains selected audio
  metadata; tests verify decoded content, not only the language tag.
- `UiPreferenceStore.h` adds a v5 master-enabled field and migrates older
  preferences. `AppShell.cpp` now actually saves on normal close. Successful
  applied settings are saved when available; otherwise the configured draft
  is saved, including while master enhancement is off. Removed the stale
  last-successful cache that could restore an older source's settings.
- `PlaybackPowerGuard.h` holds an execution-state request on the UI thread
  while video is playing. Pause, stop, failure and close release it. The main
  window suppresses screen saver/monitor-off system commands while playing.
- `PresentSink.cpp` acquires the proxy swapchain's current buffer index once;
  `VideoPresenter.cpp` uses that same index for the resource barriers, RTV and
  recorded last buffer. This repairs an inconsistent resource/descriptor
  selection contract. It is NOT established as the cause of the user's
  `DXGI_ERROR_ACCESS_DENIED` device removal.

## Actual validation

All tests below passed on RTX 5070 / driver 616.56 with patched FFmpeg at
`C:/veyra-deps/ffmpeg-ps5-dav1d-installed`. Each test ran under 300 seconds.
Build directory: `out/build/post140-hdr-preflight-20260918`.
EXE SHA256 at this validation checkpoint (superseded by the sustained-FG repair):
`5B35FDAD4291168A03A1EA0E9D911B952123DFD800C57237A08CB07F6DB1529D`.

| Check | Actual evidence |
| --- | --- |
| Full build | `out/logs/player-tracks-build3-20260918.log`, exit 0 |
| Supplied 4.27 GB MKV | `out/logs/player-tracks2-result-20260918.log`: 17 tracks switched in both playing and paused states, stale session/invalid ID rejected; first frame 367.05 ms, seeks to 30/1200/90 seconds 31.3501/30.8197/30.6269 ms. Warm cache; earlier open was 974 ms. These are local measurements, not all-machine limits. |
| Audio content and export | `out/logs/audio-track-output-result-20260918.log`: 440/880 Hz tracks switched twice, each 24000 decoded PCM frames at 1000 ms; both worker and legacy exports contain the selected 880 Hz English track, 240 encoded video frames. Worker log `logs/export-worker-33780.log`. AAC skipped-sample warnings on output decode did not fail PCM/timestamp assertions. |
| Progressive subtitles | `out/logs/subtitle32-progressive-result-20260918.log`: intermediate cue publication observed, all 32 tracks usable, 15720 cues, background scan 5753.49 ms; cancellation 15.1261 ms. FFmpeg emitted an incomplete-probe warning during cancellation. |
| Actual normal GUI | `out/logs/player-shell-result-20260918.log`: normal close/restart retained NR, SR, community runtime choice and bypass draft; selected 32nd subtitle (stream 49), audio stream 1, fullscreen/pause/resume/close power lifecycle passed. Sandbox `out/tests/player-shell-d033d14c6d80468c95313d82508df4a6`, including four application logs and separate writable preferences. User preferences were not overwritten. |
| Popup and preferences | `out/logs/popup33-result-20260918.log`, `veyra_ui_contract_tests.exe out/tests/ui-preferences-20260918-a`: 33-row scrolling/keyboard selection, disk round trip, master off/on and corrupt-original protection passed. |
| NR / XeSS order | `out/logs/player-xess-exact-window-result-20260918.log`: 4K/50fps capture replay, AMD optical flow, exact 1155x741 client size; NR-first and XeSS-first each repeated twice, 501 processed frames, each combined stage 26-36 actual generated frames. NR Create `0x1`, XeSS initialization `0`; no device removal. Logs in `out/logs/player-xess-exact-window-20260918`. This is replay on 5070, not the user's capture card or 5090. No pixel-quality comparison in this order test. |
| Latest delivery gate | `logs/delivery/81a8d951f689467a868456f023550dbd/result.json`: PASS, 61.575757 s, matching EXE hash. Covers actual NR/NVOF, native 4K correctness, playback, controls, image output, H.264/HEVC decode/audio/frame accounting and cancellation. Not hardware acceptance or endurance testing. |

Commands executed (from the repository root; fixture paths are ignored):

```powershell
$build = 'out/build/post140-hdr-preflight-20260918'
$media = 'C:/Users/123/Desktop/Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv'
& cmd.exe /c 'out\build\veyra-build-x64-release.cmd' *> out/logs/player-tracks-build3-20260918.log
& "$build/veyra_player_tracks_tests.exe" $media out/logs/player-tracks2-20260918 *> out/logs/player-tracks2-result-20260918.log
& "$build/veyra_audio_track_output_tests.exe" out/audio-tracks-fixture.mkv out/logs/audio-track-output-20260918 *> out/logs/audio-track-output-result-20260918.log
& "$build/veyra_subtitle_loading_tests.exe" $media 32 *> out/logs/subtitle32-progressive-result-20260918.log
& "$build/veyra_player_tracks_tests.exe" out/xess-live-amd-4k50.mp4 out/logs/player-xess-exact-window-20260918 xess-live-amd *> out/logs/player-xess-exact-window-result-20260918.log
& tests/integration/PlayerShellTests.ps1 -BuildDirectory $build -Media $media *> out/logs/player-shell-result-20260918.log
& scripts/gates/delivery.ps1 -Root . -BuildDirectory $build *> out/logs/player-tracks-delivery-20260918.log
```

The audio fixture is an 8-second 640x360/30 H.264 video with two AAC 48 kHz
sine tracks (440 Hz / spa, 880 Hz / eng). The order fixture is 4K/50 H.264.
Neither fixture nor any runtime/SDK is added to source Git.

## Remaining evidence limits and next task

- The RTX 4060 Ti report contains successful community NR creation followed
  by original NR failure after restart. Saving the runtime preference repairs
  that concrete failure chain. No physical RTX 40 retest was performed here.
- The 5090 NR-first failure remains unreproduced. Both orders pass locally,
  but that cannot establish a fix for error `0x887A002B`. Device removal may
  precede its first visible Present failure. `PresentSink.cpp` already queries
  DRED breadcrumbs and page faults after device failure. Forced collection
  enablement and useful evidence from the affected machine are not established.
- Playback power checks exercised successful Windows requests, their release
  and the screen-saver message path. They did not wait through a physical
  sleep timeout or test every enterprise power policy.
- Earlier RTX 30/40 6X work and the content-dependent synthetic interpolation
  position failures remain recorded in the post-1.4.0 plan. No physical RTX
  30/40/5090 test was performed, and the affected export user's original worker
  log is still missing. This patch is not an all-issues-complete claim.
- Next task: run this exact build on affected RTX 30/40/5090 hardware, retain
  the application/probe/export-worker logs and obtain the first failing call
  for any remaining NR/XeSS or export failure. No merge, push or release.
