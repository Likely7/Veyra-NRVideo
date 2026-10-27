# Veyra 1.4.3 Runtime Components

This release preserves the runtime identities used in 1.4.2.
No new runtime, SDK, model or driver binary is introduced. NVIDIA FSR4 INT8 remains withdrawn.

The authoritative per-file names, sizes, versions, SHA256 hashes, signature status and origin categories are in:

- runtime/experimental/release-runtime-manifest.json
- runtime_local/intel/experimental/release-runtime-manifest.json
- runtime_local/amd/fidelityfx/release-runtime-manifest.json

NVIDIA NR community and Ampere variants retain their recorded HashMismatch signatures;
they are explicitly experimental, not validly signed NVIDIA originals.
The remaining enhancement components must pass the publisher's fixed identity audit.
RTX Video HDR uses the unchanged RTX Video SDK 1.1.0 nvngx_truehdr.dll:
SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B,
3955752 bytes, version 1.1.0.0, Valid NVIDIA signature.

Applicable licenses are included under licenses/, including NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf.
The package includes patched FFmpeg with PS5 H.264 slice capacity 256 and dav1d.
licenses/FFMPEG-VEYRA-BUILD.json records the actual FFmpeg binary provenance.
Drivers supply nvEncodeAPI64.dll and nvofapi64.dll; these are not redistributed.

Manifests audit the publisher package; they do not prevent user DLL replacement.
ABI, API, hardware and driver compatibility still apply. These experimental integrations
are not vendor certification or endorsement. Source Git contains no proprietary runtimes.
