# Veyra 2.0.0 build and corresponding source

The portable application is `veyra_qml_ui.exe`, C++20, Windows x64, Qt Quick 6.8.3 with native D3D12 video. Legacy Win32 sources remain for history/tools; they are not the new UI executable.

## Sources and licences

Download both `Veyra-2.0.0-source.zip` and `Veyra-2.0.0-dependency-source.zip` from the same Release. The first is the tagged application source; the second contains the unchanged, hash-verified patched FFmpeg/dav1d and Remote Play source archives previously supplied with 1.4.4, plus Moonlight and its recursive submodules, Xbox WebRTC dependencies/patches/ports, OpenSSL, Qt 6.8.3 source modules and SBOM, and FidelityFX 2.3.0 source. Historical archive version names identify provenance, not a request to build the old application. See each source manifest for hashes.

Veyra's original code is GPL-3.0. The combined application with Chiaki Remote Play is also subject to AGPL-3.0 and its OpenSSL exception; retain `licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt`. Qt is dynamically linked and replaceable; LGPL/GPL and bundled third-party notices are supplied. Xbox libdatachannel/libjuice use MPL-2.0; Moonlight is GPL-3.0. See THIRD_PARTY_NOTICES and the dependency archives, not this summary alone, for exact terms.

Qt sources are from https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ and verified against published SHA256 files. No Qt library patch was applied. The source modules include their configure/build scripts; shipped binary SBOMs record the Qt build configuration/provenance. Build an ABI-compatible shared Qt and replace the app-local DLLs/plugins for relinking/debugging modifications.

## Toolchain and dependencies

- Visual Studio 2022 x64 C++ build tools, Windows SDK, CMake/Ninja and Python for build-time source checks. Qt 6.8.3 MSVC2022 x64 (Qt Quick/Controls, Widgets, Network, SVG).
- Patched FFmpeg 9.0.1 with dav1d: follow `scripts/ffmpeg/README.md`, preserve the 32→256 H.264 slice patch and verify `veyra-local-build.json`. Do not substitute stock FFmpeg merely because DLL names match.
- Remote Play: `scripts/remoteplay/dependency-lock.json`, its stage/patch scripts and `cmake/VeyraRemotePlay.cmake`. Source archive includes pinned Chiaki with recursive dependencies and the required metadata/MSVC patches.
- Moonlight: `scripts/moonlight/dependency-lock.json`, `verify-moonlight-stage.py`, pinned moonlight-common-c plus ENet/nanors. Build shared OpenSSL dependency consistently with other static libraries.
- Xbox: `scripts/xbox/dependency-lock.json`, `scripts/xbox/vcpkg-overlay` including libjuice IPv4 wake-up patch, x64-windows-static; install command is in the lock. Dependency source includes both libjuice extracted variants; the overlay recipe/patch defines the intended patched build.
- Obtain NVIDIA/Intel/Magewell SDKs yourself under their terms. They are not part of source Git/archive. DLSS SDK 310.7 build interface, Optical Flow SDK 5.0.7, RTX Video SDK 1.1.0, nv-codec-headers, XeSS 3.0.2 and FidelityFX SDK 1.1.4/2.3.0 are selected by the existing CMake options. The shipped FG runtime is separately audited as 310.9.1.

## Configure and build

Use the `x64-release` preset with explicit dependency locations; adapt paths to your machine. Enable `VEYRA_BUILD_QML_UI`, `VEYRA_ENABLE_REMOTEPLAY`, `VEYRA_ENABLE_MOONLIGHT` and `VEYRA_ENABLE_XBOX`. Set `VEYRA_FFMPEG_ROOT`, `VEYRA_DLSS_SDK_ROOT`, `VEYRA_NVOF_SDK_ROOT`, `VEYRA_XESS_ROOT`, `VEYRA_FIDELITYFX_ROOT`, `VEYRA_FIDELITYFX2_ROOT`, `VEYRA_NVENC_HEADERS_ROOT`, `VEYRA_RP_CHIAKI_SOURCE_DIR`, `VEYRA_MOONLIGHT_SOURCE_DIR` and `CMAKE_PREFIX_PATH` (Qt + dependency install prefixes). Other optional backend requirements are checked by the CMake modules.

```powershell
cmake --preset x64-release -S . -B E:/build/veyra -DVEYRA_BUILD_QML_UI=ON -DVEYRA_ENABLE_REMOTEPLAY=ON -DVEYRA_ENABLE_MOONLIGHT=ON -DVEYRA_ENABLE_XBOX=ON
cmake --build E:/build/veyra --target veyra_qml_ui --parallel 4
```

The abbreviated command requires the dependency variables above to have been configured; it does not download SDKs. Use `windeployqt --release --qmldir qml` for Qt DLLs/QML plugins and keep the built QML/shader folders beside the executable. For the audited release layout, see `scripts/package-qml-release.py` and `docs/RELEASE_2.0.0_RUNTIME_LOCK.json`. These are publisher identity checks, not runtime DLL locks. New source builds do not require immutable acceptance guards tied to the author's historical checkout.

Run the QML data/quick/easing and i18n targets plus relevant engine/export/streaming tests. Software tests do not replace real hardware, physical HDR, controller or long-term stability checks.
