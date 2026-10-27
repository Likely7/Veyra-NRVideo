# Remote Play build and corresponding source / 串流构建与对应源码

The 1.3.1 portable package enables `VEYRA_ENABLE_REMOTEPLAY`. The default build without this option does not include PS5 Remote Play.

Application source: https://github.com/Likely7/Veyra-NRVideo/tree/v1.3.1

Chiaki is pinned to `0e16950165f06e5c3291537c2eeba6e852be7120` from https://github.com/streetpea/chiaki-ng, with the reviewed patches in `scripts/remoteplay/patches/`. The combined program is subject to Chiaki's AGPL-3.0-only license and OpenSSL exception as well as retained dependency notices; original Veyra source remains GPLv3.

`Veyra-1.3.1-RemotePlay-source.zip` supplies the clean pinned Chiaki tree and recursive submodule files, reviewed patches, dependency source and vcpkg port provenance for json-c, libevent, miniupnpc, OpenSSL, Opus, and SDL3. It also contains the open-source FidelityFX source used by the static optical-flow backend. FFmpeg corresponding source is a separate asset. No NVIDIA or Intel SDK/runtime, credentials, local configuration, logs, or test media are included.

To build: use Visual Studio 2022 x64 tools, CMake, Ninja, Python, protobuf tooling, the dependency paths recorded in the source archive, and separately licensed enhancement SDKs described in `BUILD.md`. Build the pinned Chiaki checkout with `scripts/remoteplay/build-native.ps1`, then run `scripts/build.ps1 -Root . -Preset x64-release -RemotePlay` with the same dependency paths. The release uses static Chiaki/SDL/dependencies and the existing five dynamic FFmpeg DLLs; Windows networking, WinHTTP, DPAPI, WASAPI, DirectX, and GPU driver components are not redistributed.

The PS5 connection and normal USB controller path have user test evidence. Offline tests do not prove Sony authorization, all Bluetooth controllers, physical HDR output, 5.1 transport, or uninterrupted long sessions. PS5 Remote Play remains limited to the upstream stereo audio path; Veyra does not claim Dolby/Atmos transport.

AV1 playback adds app-local dav1d 1.5.4 (SHA256 `38E09F960822A081FC46FC296FB3EF5F841D1A6C15A39F20684C2F9D88A9FC52`). FFmpeg is rebuilt with `--enable-libdav1d` while retaining the PS5 slice patch; avcodec SHA256 `22D1321B8D31161E7CB0920EC08C43B51884436A066E62C70A0C564ACD4C813E`. The portable package carries dav1d notices and provenance; the FFmpeg source asset includes dav1d source and its vcpkg port. All eight enhancement runtimes keep the previously approved identities.
