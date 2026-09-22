# AJA KONA HDMI capture plan (2026-09-22)

User request: add native AJA capture to Veyra and verify real PS5 video on the local KONA HDMI. User explicitly selected Desktop for this task's build/dependency/test artifacts, overriding the historical E-drive rule. Artifact root: C:/Users/charll/Desktop/Veyra-AJA-work. Existing portable package stays intact. No publication or driver change.

Current evidence: DirectShow enumeration does not expose AJA; native driver reads succeed. PS5 HDCP disabled; HDMI1 locked at UHD59.94. AJA Control Room recorded 240 real frames after user changed erroneous HDMI4 selection to Auto.

Implementation: optional MIT AJA NTV2 SDK backend behind existing CaptureCardSource facade and capture2 stable device path. Enumerate KONA HDMI inputs separately; detect actual input format; use AutoCirculate DMA, bounded latest-frame mailbox, existing FramePacket/GPU ingress and audio session. Never emulate a device merely by listing it. Fail explicitly on busy, absent/unsupported input, format changes and unsupported color/audio modes. Restore device configuration and release ownership on close.

Validation: compile backend + application, offline path checks, real card capture with Control Room released, verify decoded non-black frames and sustained preview. Record exact supported formats and remaining limitations. Build agent handles toolchain and dependencies while main agent implements capture.

Source acquisition: github.com/Likely7/Veyra-NRVideo main at task start; AJA SDK github.com/aja-video/ntv2 local checkout. Record pinned commit and MIT attribution when integrating. No SDK/runtime bytes tracked in Veyra.

## Review and validation

The native backend is opt-in (`VEYRA_ENABLE_AJA=ON`) and requires an external
MIT NTV2 checkout supplied with `VEYRA_AJA_SDK_ROOT`. Default DirectShow builds
remain independent of the AJA SDK. Tested SDK commit:
`007fb92b5328c01da85bd170dc5cfc9ede3cfe91`.

Supported scope: Windows KONA HDMI, progressive SDR UYVY up to 3840x2160 at 60 Hz,
HDMI ports enumerated separately, optional 48 kHz linear PCM. HDR, compressed
HDMI audio, other AJA models and surround speaker mapping are not validated.
Close other applications that own the device before connecting.

Review fixes: declare the eight-channel PCM speaker layout explicitly; preserve
embedded audio routing separately when restoring an HDMI source; propagate
worker failures to the capture facade; use a rolling input-rate window; report
actual audio availability; contain worker exceptions. Removed the temporary
unrelated missing-NVOF build fallback after the real SDK became available.

Hardware evidence before the final review changes: 718 frames in 12 seconds,
59.926 fps; full application AJA + NR + NVOF smoke passed at approximately 60 fps.
PCM diagnostic buffers contained real audio and the user confirmed audible
playback. Final review hardware rerun reported HDMI no signal and is not a pass.
No claim of end-to-end display latency, precise game FPS or surround validation.

Hardware test usage: `veyra_aja_capture_tests.exe` lists device paths. Supply a
listed path and an output raw-frame filename to capture for 12 seconds. Append
`--audio` to exercise embedded PCM initialization and delivery as well. A live,
moving, non-black source is needed for the video acceptance checks.
