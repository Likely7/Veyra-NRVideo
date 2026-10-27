# Veyra 1.4.3 Release Build

The application source is pinned by tag v1.4.3. It includes the retained cadence
repairs and window/settings follow-up through 139db25. Rejected cadence
experiments remain reverted. Publication evidence is in RELEASE_1.4.3_EXECUTION.md.

Remote Play remains enabled. Chiaki is pinned to streetpea/chiaki-ng commit
0e16950165f06e5c3291537c2eeba6e852be7120; local patches are under scripts/remoteplay/patches.
The combined program is subject to Chiaki AGPL-3.0-only with OpenSSL exception;
Veyra source is GPLv3. Notices are included under licenses/remoteplay.

FFmpeg preserves the existing PS5 H.264 32-to-256 slice patch and dav1d build.
Dependency sources and notices are unchanged from 1.4.2. The combined release source
ZIP includes the verified FFmpeg and RemotePlay source archives originally built
for 1.4.1, including patches, notices and build records. Their names retain the
original version to identify reuse. Binary provenance is in licenses/FFMPEG-VEYRA-BUILD.json.

Build with VS2022 x64, CMake/Ninja and authorized local SDKs, using scripts/build-isolated.ps1
with DisplayVersion=1.4.3 and explicit E:/项目/Veyra build/temp directories. See BUILD.md.
No SDK, runtime, credentials or private media are added to source Git.
