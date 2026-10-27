# Veyra 1.4.4beta Build

Local beta on codex/capture-color-144beta-20260920, based on main 05998da.
Build using scripts/build-isolated.ps1 with DisplayVersion=1.4.4beta.
Corresponding source archive records the exact Git commit.

Remote Play is enabled, unchanged from 1.4.3. Chiaki-ng commit
0e16950165f06e5c3291537c2eeba6e852be7120 and local patches under
scripts/remoteplay/patches remain in use. Its AGPL-3.0-only with OpenSSL
exception applies to the combined program; notices are under licenses/remoteplay.
FFmpeg retains the PS5 H.264 256-slice patch and dav1d.
Verified dependency source archives originally supplied for 1.4.1 are reused
in the corresponding-source package. No proprietary SDK/runtime is in Git.
