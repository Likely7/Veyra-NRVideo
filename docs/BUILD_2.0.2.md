# Veyra 2.0.2 — corresponding source and build

The release has one runnable download, `Veyra-2.0.2-win64-portable.zip`. The following source downloads are for rebuilding; they are not needed to run Veyra:

- [Exact Veyra v2.0.2 source](https://github.com/Likely7/Veyra-NRVideo/archive/refs/tags/v2.0.2.zip). Includes the original SoundTouch 2.4.1 sources, its LGPL-2.1 licence, Veyra's static-library integration, build scripts and patches.
- [Matching dependency source bundle](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.0/Veyra-2.0.0-dependency-source.zip), unchanged from 2.0.0: 538838710 bytes, SHA256 `a293552434381a71169c1c27658f07202d67243ca82e748936b9db15077ce0de`. Contains patched FFmpeg/dav1d, Chiaki/Remote Play, Moonlight, Xbox/libdatachannel dependencies, OpenSSL, Qt 6.8.3 source modules/SBOM and FidelityFX source/build materials. Keep the existing 2.0.0 asset available with this release.

Use both downloads. The dependency versions/build instructions in [BUILD_2.0.0.md](BUILD_2.0.0.md) still apply. The application version is now 2.0.2; no FFmpeg, Qt or WebRTC dependency binary was replaced. Keep FFmpeg's PS5 H.264 32→256 slice patch and its build manifest. SoundTouch is built from `third_party/soundtouch`; modify it and rebuild the full application to relink it. The complete application source is available for that purpose.

Use Visual Studio 2022 C++20 x64, CMake/Ninja and the dependency paths described in the earlier build guide. Configure the `x64-release` preset with QML, Remote Play, Moonlight and Xbox enabled, then build `veyra_qml_ui`. Required checks for this release include `veyra_preset_library_tests`, `veyra_repair_preset_tests`, `veyra_effect_chain_tests`, `veyra_xbox_tests`, `veyra_live_timing_tests`, `veyra_audio_playback_rate_tests`, `veyra_ui_contract_tests`, `veyra_qml_data_tests`, `veyra_ui_i18n_tests` and `veyra_qml_quick_tests`.

The package is assembled with `scripts/package-qml-release.py --label 2.0.2 --release`. Supply explicit `--build`, `--runtime-source`, `--original-nr`, `--legacy-licenses`, `--qt`, `--qt-licenses` and `--output` paths. The publisher lock is `RELEASE_2.0.2_RUNTIME_LOCK.json`: it keeps every 2.0.0 identity and adds the approved NVIDIA original NR in its own folder. Runtime/SDK/model binaries are excluded from source Git. Obtain proprietary development SDKs under their own terms; no unknown DLL download is part of the build.

The original Veyra code is GPL-3.0; the combined Chiaki application also follows AGPL-3.0 with the documented OpenSSL exception. Qt remains dynamically linked and replaceable. SoundTouch is LGPL-2.1; its corresponding source and build integration are in the tagged application source. See the packaged `licenses/` and `THIRD_PARTY_NOTICES.md` for all third-party notices. These links provide source without adding separate downloadable assets to the 2.0.2 release.

## 中文

运行软件只需下载一个便携压缩包。需要重编译时，下载上面的 **v2.0.2 应用源码** 和 **2.0.0 依赖源码包**，后者未改动，哈希如上。SoundTouch 2.4.1 的完整源码、许可证和静态构建接入已放入应用源码，可修改后重新链接整个应用。原有 Qt、patched FFmpeg、Chiaki、Moonlight、Xbox 等构建材料沿用；必须保留 PS5 H.264 slice 补丁。具体工具链和依赖路径见 2.0.0 构建说明。

NR 新增 NVIDIA 原版独立目录，其余两版及运行组件不变。发行 manifest 是发布者核验清单，不阻止用户替换 DLL。实验运行组件不代表厂商认证或完整官方 DLSS 5 集成。
