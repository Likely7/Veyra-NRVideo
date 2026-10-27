# Veyra 1.4.4 Runtime Components

All runtime identities are unchanged from 1.4.3. No SDK or model is added.
Per-file origins, hashes, versions, sizes and signatures are recorded in the
three release-runtime-manifest.json files under runtime/experimental,
runtime_local/intel/experimental and runtime_local/amd/fidelityfx.
Community/Ampere NR modules retain their declared HashMismatch status.
RTX Video HDR retains NVIDIA RTX Video SDK 1.1.0 TrueHDR, SHA256
9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B.
Licenses are included under licenses/. Patched FFmpeg and dav1d are unchanged;
licenses/FFMPEG-VEYRA-BUILD.json records binary provenance.
Publisher identity auditing does not restrict user runtime replacement.
