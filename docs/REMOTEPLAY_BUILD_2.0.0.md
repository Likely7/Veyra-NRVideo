# Veyra 2.0.0 Beta Build

Local beta built from this worktree. The corresponding-source archive for this
package records the exact Git commit; nothing in this package is committed,
pushed or released by the agent.

Remote Play is enabled (`VEYRA_ENABLE_REMOTEPLAY:BOOL=ON`) and unchanged from
1.4.4. Chiaki-ng commit `0e16950165f06e5c3291537c2eeba6e852be7120` and the local
patches under `scripts/remoteplay/patches` remain in use. Its AGPL-3.0-only with
OpenSSL exception applies to the combined program; notices are under
`licenses/remoteplay`. FFmpeg retains the PS5 H.264 256-slice patch and dav1d.

Frame generation moves to the official NVIDIA DLSS SDK 310.9.1 provider, patched
in process memory only; the upscaling provider stays at 310.7. The community
optimized kernel files under `runtime/experimental/dlssg-kernels/` are runtime
data, listed per file in the package manifest, and carry no licence from either
author (see `THIRD_PARTY_NOTICES.md`).

Verified dependency source archives originally supplied for 1.4.1 are reused in
the corresponding-source package. No proprietary SDK/runtime is in Git.
