# Remote Play build and corresponding source / 串流构建与对应源码

1.0.1 portable enables VEYRA_ENABLE_REMOTEPLAY. The default build without that option does not include PS5.

## Sources and licenses

Application: https://github.com/Likely7/Veyra-NRVideo/tree/v1.0.1
Chiaki: https://github.com/streetpea/chiaki-ng at 0e16950165f06e5c3291537c2eeba6e852be7120, with scripts/remoteplay/patches/0001 and 0002. AGPL-3.0-only with upstream OpenSSL exception. The combined program is subject to this license as well as the retained component notices. Original Veyra source remains GPLv3.

The Release's RemotePlay-source ZIP supplies the clean pinned Chiaki tree and recursive submodule files, reviewed patches, installed dependency SPDX records, patched source directories and vcpkg port recipes for json-c, libevent, miniupnpc, OpenSSL, Opus and SDL3. It also includes the local FidelityFX source used by the existing static optical flow backend. No NVIDIA or Intel SDK, runtime, model, private configuration or credential is included. FFmpeg source is a separate asset.

licenses/remoteplay/ contains the retained notices. source-manifest.json records source hashes; dependency pins and port provenance are also retained. The source bundle is not required by ordinary users.

## Build

Use Visual Studio 2022 x64 tools, CMake, Ninja, Python and protobuf tooling. FFmpeg and enhancement SDK requirements remain in BUILD.md.

1. Clone Veyra at v1.0.1.
2. Clone Chiaki recursively and checkout the above pin, then update submodules recursively. The supplied clean source and submodule manifest provide an offline reference of the exact source.
3. Prepare static dependencies with the supplied vcpkg port versions and x64-windows-static triplet. The release used C:/veyra-deps/remoteplay-installed/x64-windows-static. The source archive's installed-status.txt and port recipes record versions; adapt paths to your machine.
4. Run scripts/remoteplay/build-native.ps1 with -ChiakiCheckout, -StageDirectory, -PrefixPath, -ProtocPath and -PkgConfigPath. This creates a separate stage, applies both patches and verifies its complete contents.
5. Run scripts/build.ps1 -Root . -Preset x64-release -RemotePlay -BuildDirectory out/build/release-1.0.1 with the same -ChiakiCheckout, -ChiakiStage, -RemotePlayPrefixPath, -ProtocPath and -PkgConfigPath. This explicitly enables the real backend and compiles it from source.

The 1.0.1 build uses static MSVC runtime and static Chiaki/SDL/dependencies, with the existing five dynamic FFmpeg libraries. System networking, WinHTTP, DPAPI, WASAPI, DirectX and driver libraries are not redistributed.

The source bundle's vcpkg-port-scripts directory includes the port helper sources and x64-windows-static triplet. Source builds still require ordinary compiler/build tools and separately licensed enhancement SDKs; no proprietary SDK is included in corresponding-source assets.

## Scope

The PS5 connection and normal USB controller path have user test evidence. Build/offline GPU tests do not prove Sony authorization, physical HDR output, Bluetooth support on all devices or uninterrupted long sessions. README and release notes preserve these limits.
