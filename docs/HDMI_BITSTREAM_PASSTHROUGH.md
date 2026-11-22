# HDMI bitstream passthrough

Veyra takes a copy of the original IEC 61937 payload at the AVerMedia carrier callback, immediately before FFmpeg decodes it. A dedicated WASAPI render thread sends AC-3 and DTS core to any compatible Windows render endpoint using the Windows IEC 61937 encoded subtypes. It rebuilds the transport padding and keeps the original IEC header words and compressed payload. `CaptureCompressedDecoder.cpp` handles video codecs and is outside this audio path. `AverMediaAudioSwitch.cpp` continues to handle the existing vendor input setup.

The capture session still decodes PCM for Veyra recording. During passthrough, the bitstream takes exclusive ownership of the selected audio output. Windows loopback capture on that output loses Veyra’s PCM monitor audio. The HDMI queue has a 40 ms priming target and a 125 ms backlog limit at zero added delay. During underruns it emits IEC 61937 pause bursts for AC-3 and zero carrier words for DTS. It drops whole queued bursts if independent clocks or scheduling fill the queue. Passthrough runs directly from capture to its own queue. Audio/Video Sync applies to the decoded PCM path. A failed format query leaves shared PCM playback in place. The player restores shared PCM when bitstream ownership ends.

## Build and use

1. Configure the standard Veyra Windows build with the existing SDK and runtime dependencies in [Development](DEVELOPMENT.md). Build the QML player using `cmake --build <configured-build> --config Release --target veyra_qml_ui --parallel 2`. Stage the player with the matching runtime and shaders using the repository's normal build workflow.
2. Install the AVerMedia Multichannel Audio component. In Veyra, select the GC553G2 capture and leave **Capture audio ingress** on **Auto** so the vendor graph can expose the compressed carrier. Reopen capture once if it was already using an explicitly selected legacy bitstream route.
3. Set the PS5 to **Dolby Audio** first. On the B9, select **Bitstream** for the GPU's HDMI input, **HDMI ARC** for sound output, and **Pass Through** for digital sound output. Select ARC on the S60TR. [PlayStation audio settings](https://www.playstation.com/en-gb/support/hardware/ps5-change-audio-output/) · [LG B9 support](https://www.lg.com/us/support/product/lg-OLED65B9PUA.AUSD?tab=1)
4. In Veyra's **Audio Settings**, choose **LG TV (NVIDIA High Definition Audio)** as the main output and leave **Stereo Downmix** off. Turn on **HDMI bitstream passthrough** to use the exclusive receiver output when a supported stream arrives. The default is decoded PCM. When the driver accepts the captured stream, the normal output status shows **AC-3 passthrough** or **DTS passthrough**.
5. Confirm that the soundbar plays surround in sync with the picture. Use Veyra recording to verify the decoded PCM path. Turn **Stereo Downmix** on to restore stereo PCM monitoring and Windows loopback audio for OBS.

## Regression tests

The independent Windows transport tests require MSVC and the Windows SDK.

```powershell
cmake -S tests/hdmi-bitstream -B <test-build> -A x64
cmake --build <test-build> --config Release --parallel 2
ctest --test-dir <test-build> -C Release --output-on-failure
```

The full build also registers `veyra_avermedia_carrier` for real encoded AC-3/DTS carrier fragmentation, byte comparison, codec transitions, and simultaneous PCM decoding. `veyra_capture_audio_hdmi_branch` checks stereo PCM and recording while the configured HDMI endpoint is unavailable. `veyra_capture_audio_bitstream_reservation` verifies that recording continues when bitstream takes the main output and that PCM playback resumes after release.

## Local results

- Windows 11, MSVC 19.44, Windows SDK 10.0.26100. The six transport tests passed. The separate carrier suite passed 39,592 assertions using FFmpeg 8.0 for local test binaries.
- RTX 5090 and LG B9. Exclusive-mode format queries accepted 48 kHz AC-3 and DTS. The render backend ran 12-second generated 5.1 tests in both formats and the listener confirmed both played over ARC on the S60TR.
- PS5 Pro and GC553G2. A diagnostic linked against the production vendor callback, splitter, decoder and render backend observed 263 original AC-3 bursts and 403,968 decoded PCM sample frames while the LG HDMI stream was active.
- On subsequent runs with the PS5 set to DTS, the capture callback continued to deliver AC-3. The latest retry observed 274 AC-3 bursts and 420,864 decoded PCM sample frames while the HDMI stream was active. Live PS5 DTS capture and an on-device codec transition remain unverified.
- The PCM integration tests passed with the configured endpoint unavailable and during main-output reservation. The reservation test observed recording-only PCM, monotonic timestamps and resumed shared PCM after release. All nine audio tests passed against the staged patched FFmpeg runtime.
- The full Windows Release player built with MSVC, Qt 6.8.3, NVIDIA Optical Flow SDK 5.0.7, the published patched FFmpeg 9.0.1 DLLs and generated import libraries, and libdovi 3.4.0. The portable test stage is `E:/VeyraHdmiBuild/tests/hdmi-bitstream/veyra-player`. The launch smoke test opened Audio Settings and exited cleanly using an isolated profile. The final player was rebuilt with `VEYRA_CUDA_DRIVER_INCLUDE_DIR` pointing at the CUDA driver headers supplied in the Optical Flow SDK, enabling native NVIDIA VFG. All 89 built shaders matched the staged files by SHA-256. Live capture and OBS in the full app remain to be tested.
