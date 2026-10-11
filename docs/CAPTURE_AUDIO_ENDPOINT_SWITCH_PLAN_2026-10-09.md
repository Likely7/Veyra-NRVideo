# Capture audio output switching

Scope: the user reported that changing the audio output in the 2.0.6 Windows
capture session only takes effect after restarting Veyra. The user authorized
a local repair and an upstream pull request after successful verification.

Baseline: main `96a7c8d`; isolated branch `fix/capture-audio-output-switch`.
The official portable installation and its runtime files remain intact.
Build dependencies, original source copies, test output and candidate binaries
are kept outside Git in a dedicated local development directory.

The preference setter increments the render endpoint generation, and
`AudioRenderer::lastError()` exposes a generation mismatch as device
invalidation. File playback polls that state, but `CaptureAudioSession` does
not. The capture owner must observe the same state and reuse its existing
bounded endpoint recovery path. It must not restart the capture graph or
recreate the video engine.

Changes are limited to the capture audio owner, the existing capture audio
integration test, and this plan/work log. Runtime DLLs, shaders, drivers, GPU
algorithms, display settings and unrelated source modules are outside scope.

Validation:

- Feed continuous muted PCM through the real WASAPI capture audio session.
- Select two distinct active endpoints, switch A -> B -> A in the same
  session, and assert the actual opened endpoint and continued input/output.
- Assert selecting the same endpoint again does not rebuild the renderer.
- Select system default in the same session and verify the actual default
  endpoint. For a live capture, change the Windows default while that mode
  is selected and verify the notification reopens the new default.
- Show the unmodified owner fails the switch regression, then run the fixed
  owner and existing relevant capture audio recovery checks.
- Build and launch a side-by-side local candidate and verify a live capture
  output change without restarting the process before claiming a local fix.
- Keep each test within 300 seconds and each build within 900 seconds.

Submit only source changes and summarized validation after successful local
verification. No capture media, user endpoint IDs, local logs, proprietary
SDKs or runtime binaries are included in the public contribution.

Results: the unmodified capture owner failed A -> B with the original output
still active. The fixed real WASAPI test passed A -> B -> A, the same-endpoint
no-op and selection of system default. Existing endpoint-loss, slow-start and
44.1 kHz transient conversion checks also passed.

A side-by-side MSVC/Qt candidate was verified with live NS2 3840x2160/60
capture. Explicit output changes reopened in 0.526 and 0.515 seconds. The user
then selected system default and changed the Windows output; the existing
default-device notification was consumed and the renderer followed the new
default in 0.517 seconds. The process stayed unchanged, with one capture
configuration and one input start. Video stayed at 60 real frames/s with
XeSS reporting 240 submissions/s (not measured panel scanout). The user
initially reported not hearing the USB speaker, then reported that a manual
retry seemed to work; no microphone or physical speaker measurement was made.

The local candidate enables the NVIDIA capture enhancement chain but was
built without PS5/Moonlight/Xbox source modules or libass. Those paths were
not tested. The original complete official portable installation is retained.
