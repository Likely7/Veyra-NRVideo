# Elgato MK.2 HDR repair

## Evidence and scope

User's `veyra-app(30).log` has native P010 3840x2160/60, no converters,
VIDEOINFOHEADER2 colorInfoPresent=false. Final manual PQ path correctly uses
BT2020-NCL limited -> scRGB FP16 without SDR tone mapping. Windows HDR is on.
The comparison is Veyra versus a PNG of Elgato Studio viewed in Photos, not
a controlled live pixel comparison. Neither the image nor this log proves
the capture card's internal tone mapping state.

Nitlink commit 4f8a723406f63c6e92832488a6022fcf25233871 reads the vendor HDR
packet and disables internal tone mapping. Official Elgato support commit
fe9630974d47f51bf54826e72fb8b654e620aa93 confirms properties 720/721 (two
16-byte HDMI HDR packet halves), 722 (DWORD: 0 native HDR, 1 tone-mapped SDR),
and P010 for DirectShow HDR. Veyra had none of those property calls.
MK.2 limited-chroma decoding in Nitlink agrees with Veyra's existing 896-span
formula. Do not change shaders or compensate red globally.

## Implementation

1. Checkpoint pre-elgato-hdr-control-20260920 at c84eb72; retain isolated beta branch.
2. Only exact MK.2 aliases and negotiated P010 enter the helper. Use the
   capture graph's existing device; no second device open, vendor DLL or worker.
3. Read and validate both packet halves, header, length, checksum, EOTF and
   static metadata descriptor. Empty packet denotes SDR; failures/unknown
   EOTF never establish HDR. PQ/HLG are supported, traditional HDR gamma is not.
4. Auto can fill missing HDR interpretation from InfoFrame. Preserve explicit
   driver matrix, primaries and range. Missing gamut retains labelled BT2100
   assumptions. Explicit user color/range remains authoritative.
5. Native HDR disables card tone mapping; explicit Rec709 enables it. No
   device write just because a format has ten bits. Writes occur on stopped,
   connected graph before Run, never during active frame callbacks.
6. Read original state and verify successful Set when readable. Set failure
   or contradictory readback fails connection rather than mislabelling data.
   Read-only QuerySupported is diagnostic, as the official sample uses Set
   directly. Write-only drivers are accepted but logged as unverified readback;
   their unknown original value cannot safely be restored and is not invented.
7. Session close restores known original state after Stop; configure failures
   release the local RAII helper. No per-frame polling or extra queue latency.

## Acceptance and boundaries

Build application and capture-color tests using existing isolated beta build.
Mock actual IKsPropertySet: corrupt/short packets, unknown EOTF, PQ/HLG/SDR,
explicit override, unrelated device/non-P010, rejected/ignored writes,
write-only firmware, original-state restoration and preserved color metadata.
Existing raw capture layout/native sink/audio contract cases must still pass.
Package audit and baseline smoke use existing release scripts.

No MK.2 is attached locally. Software/mock success is not actual color approval.
Device should already receive HDR before Connect. Live HDMI SDR/HDR changes
without reconnect are not implemented by this repair. Test P010, auto color,
auto range first; compare same scene with enhancements/grading disabled.
If auto packet unavailable, select PQ explicitly and reconnect. Do not force
full range to compensate color. Request the new capture-elgato log if wrong.
If property changes pass but color stays wrong, compare actual source samples
and renderer output against Studio/Nitlink; do not claim metadata was the only cause.

Artifacts: build/color-mixer-hue-20260920; logs/elgato-hdr-20260920;
tmp/elgato-hdr-20260920; downloads/{nitlink-hdr,elgato-capture-support}-20260920;
test-packages/1.4.4beta-elgato-20260920 under E:/项目/Veyra.
