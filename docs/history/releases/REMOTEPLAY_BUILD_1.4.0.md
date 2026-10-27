# Remote Play build and corresponding source / 串流构建与对应源码

The 1.4.0 portable package enables `VEYRA_ENABLE_REMOTEPLAY`. The default build without this option does
not include PS5 Remote Play. This package is built from `main` (the isolated colour/AverMedia/export
branches were merged before the build); the `v1.4.0` tag will be created when the build is released.

Application source: https://github.com/Likely7/Veyra-NRVideo (tag `v1.4.0` at release time)

Chiaki is pinned to `0e16950165f06e5c3291537c2eeba6e852be7120` from https://github.com/streetpea/chiaki-ng,
with the reviewed patches in `scripts/remoteplay/patches/`. The combined program is subject to Chiaki's
AGPL-3.0-only license and OpenSSL exception as well as retained dependency notices; original Veyra source
remains GPLv3.

`Veyra-1.4.0-RemotePlay-source.zip` supplies the clean pinned Chiaki tree and recursive submodule files,
reviewed patches, dependency source and vcpkg port provenance for json-c, libevent, miniupnpc, OpenSSL,
Opus, and SDL3. It also contains the open-source FidelityFX source used by the static optical-flow backend.
FFmpeg corresponding source is a separate asset. No NVIDIA or Intel SDK/runtime, credentials, local
configuration, logs, or test media are included.

To build: use Visual Studio 2022 x64 tools, CMake, Ninja, Python, protobuf tooling, the dependency paths
recorded in the source archive, and separately licensed enhancement SDKs described in `BUILD.md`. Build the
pinned Chiaki checkout with `scripts/remoteplay/build-native.ps1`, then run
`scripts/build.ps1 -Root . -Preset x64-release -RemotePlay` with the same dependency paths. The release uses
static Chiaki/SDL/dependencies and the existing dynamic FFmpeg DLLs; Windows networking, WinHTTP, DPAPI,
WASAPI, DirectX, and GPU driver components are not redistributed.
