# Export Gate Removal, 2026-09-18

## Request and Evidence

The user explicitly requested removing export restrictions and validation,
including the full decode of the completed output. This supersedes older
requirements for product-side CFR rejection and output integrity verification.
Changes remain on `codex/post140-field-repair-20260917`; no publication or merge.

User log: `E:/wechat/xwechat_files/wxid_kcmlgkgv70mv22_d585/msg/file/2026-09/export-worker-17508(1).log`.
At 19:37:28.533 it reports 51,248 source/output frames and successful NVENC calls.
At 19:37:28.765 it starts reopening the HEVC output using a single-thread software
decoder. At 19:48:43.861 verification reports 51,248/51,248 frames, EOF and PASS.
This redundant stage took approximately 675 seconds. The log does not contain
the reported GUI failure and does not establish a driver failure.

## Implementation

- `src/engine/VideoExportJob.cpp`: remove the 120-frame CFR qualification scan,
  source reopen, per-source strict CFR rejection, child FG compatibility probe,
  audio codec-query rejection and corrupt-packet flag rejection. The actual graph,
  encoder, decoder and mux operations still return their real errors.
- Remove all post-export reopen/decode/dimension/color/timestamp/count checks.
  Flush the encoder, write the trailer, close the file, then rename the partial.
- The encoder still receives sequential frame IDs; a small in-flight timestamp
  queue translates completed packets into source-relative microseconds. A single
  pending compressed packet obtains its duration from the next packet. Irregular
  frame intervals and long gaps retain their real duration without dropping any
  decoded source frame, or generating an unbounded run of duplicate frames.
  FG timestamps subdivide each actual source interval. Missing/backward source
  timestamps are repaired using the nominal interval, counted and logged.
  Their original timing cannot be reconstructed when the source is malformed.
- HDR plus an H.264 request selects HEVC Main10 with an explicit outcome note.
  XeSS/FSR export retains the existing explicit DLSS substitution.
- `src/sink/NvencD3D12Encoder.cpp`: API version and maximum-dimension/10-bit
  capability queries are diagnostic only. Real CreateInstance/Open/Initialize
  results select NVENC or the existing system-encoder fallback. ABI structures
  remain the audited 13.0 layout; no guessed version-field rewriting.
- `apps/veyra/ui/AppShell.cpp`: exporting no longer requires a successful preview
  frame or a completed preview effect transaction. Freeze the requested effective
  UI settings for the independent worker.
- `src/engine/ExportJobManager.cpp`: remove verification claims and preserve
  the worker's success notes, including codec/backend substitutions.

Bounds needed to allocate planes or parse shared memory, existing-file protection,
actual encoding/I/O/mux failures and cancellation remain. Removing those would
cause memory errors, data loss or false success, rather than enable an export.
An unavailable requested enhancement still reports its actual initialization
failure; the requested multiplier is not silently reduced.

## Verification

Hardware: RTX5070, driver 616.56. No RTX30/40 hardware execution in this change.
All test processes are bounded to 300 seconds or less. Independent test decoding
is intentionally retained outside the application.

Build: `cmd.exe /c out\build\scheduling-audit-build.cmd`, exit 0.
Final build log: `out/logs/export-relaxed-build-final-20260918.log`.

`tests/integration/ExportTimingTests.ps1 -BuildDirectory out/build/scheduling-audit-20260918 -OutputDirectory out/logs/export-relaxed-timing-final-20260918`:
PASS H.264 NVENC, HEVC DLSS 6X, forced Media Foundation, and injected NVENC failure
with successful Media Foundation fallback. The fixture changes from 30 to 15 fps.
Every source frame remains at its original relative time (measured error zero),
generated frames subdivide the actual intervals, sample durations are contiguous,
AC-3 audio remains present, and independent FFmpeg decoding succeeds. Real output
creation failure remains exit 1. Results: 60/360/60/60 frames; 6X consists of
60 source + 295 generated + 5 explicitly counted tail holds. DLSSG Create is
`0x1`, NVENC EncodePicture/LockBitstream status 0. Logs beside `result.json`.
The initial test script failed because `$error` is a reserved PowerShell variable;
renamed to `$timestampError` before the successful full run. The first failed
script run remains in `out/logs/export-relaxed-timing-20260918.log`.

All following executables run from `out/build/scheduling-audit-20260918`, through
`scripts/run-short-test.ps1`:

- `veyra_scheduling_chain_tests.exe out/logs/rtx30-package-motion-fixture.mkv out/logs/export-relaxed-cancel-20260918 export`, limit 90s: PASS cancellation after
  1/3/5 source frames, partial retained, no false completion; subsequent 12-frame
  job succeeds. Initial shell invocation incorrectly passed an array through
  `pwsh -File`; corrected to invoke the script with `&` before execution.
- `veyra_export_worker_failure_tests.exe probe out/logs/rtx30-package-motion-fixture.mkv out/logs/export-relaxed-probe-bypass.mp4`, limit 40s: PASS. A forced
  compatibility-probe rejection no longer blocks the worker. Real 6X exports
  72 frames from 12 sources; `logs/export-worker-16804.log`.
- `veyra.exe out/hdr-audio-fixtures/pq-tagged-51.mp4 --export-out out/logs/export-relaxed-hdr-auto.mp4 --no-nr --no-sr --no-fg --max-frames 12`, limit 60s:
  PASS without `--hevc`. Independent ffprobe confirms HEVC, yuv420p10le,
  BT2020/PQ and AAC audio.
- `veyra_export_worker_failure_tests.exe encoder out/hdr-audio-fixtures/pq-tagged-51.mp4 out/logs/export-relaxed-real-encoder-error.mp4`, limit 40s: PASS actual
  initialization-failure reporting. Injected NVENC status 15 plus unavailable HDR
  system fallback produces failure with the worker-log path, not false success.
- `veyra_audio_track_output_tests.exe out/audio-tracks-fixture.mkv out/logs/export-relaxed-audio-tracks`, limit 120s: PASS selected English audio, measured 880 Hz,
  through both the independent worker and controller export paths.

## Delivery Status

`scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/scheduling-audit-20260918`:
PASS 26 checks, 60.406 seconds. Evidence:
`logs/delivery/35f686779486445f9b4e0e51e9cbe49c/result.json`.
This includes actual NR/NVOF, native 4K H.264/HEVC 2X encoding, independent
decode/frame-count/audio checks, cancellation and source switching.

EXE SHA256: `38E7A3ED8B5156A4962FF227391698D6CD71BFCBED4DA98807B03A6F6E5E7599`.

Packaging command: `scripts/package-portable.ps1 -Root . -Version 1.4.0 -Label '-test-20260918-r3' -OutputDirectory C:/veyra-test-packages/post140-20260918-r3 -BuildDirectory out/build/scheduling-audit-20260918`.
`out/tmp/finalize-post140-package.ps1 -SkipSource -Revision r3` adds the testing
instructions and existing third-party license texts, audits all 113 archive
entries against their manifest hashes and rejects debug/SDK/private-media data.
Evidence: `out/logs/export-relaxed-package-r3-audit.log`.

Portable: `C:/veyra-test-packages/post140-20260918-r3/Veyra-1.4.0-test-20260918-r3-win64-portable.zip`.
SHA256: `BEC301B6BF19419246E04FD8D0849A461FC73EEFE168532E500161C635898215`.

`out/tmp/smoke-r3-export.ps1` runs the independently extracted executable from
TEMP with only Windows directories in PATH. PASS actual HEVC 6X export, packaged
FFmpeg/FG module paths, 60 source + 295 generated + 5 holds = 360 frames, no
post-export decode, and independent FFmpeg decode of the completed output.
Evidence: `out/logs/export-relaxed-packaged-6x.json` and
`out/logs/export-relaxed-packaged-smoke.log`.

The matching worktree source snapshot and unchanged FFmpeg/RemotePlay source
archives are provided alongside the portable archive, with SHA256 sidecars and
`delivery-verification.json`. r2 is unchanged. Runtime identities and patched
FFmpeg are unchanged. No merge, push or public release. `git diff --check` passes.
Next acceptance: reproduce the user's original export with r3; its exact GUI
failure was not present in the supplied worker log. Target RTX30 hardware
acceptance from the preceding repair remains open.
