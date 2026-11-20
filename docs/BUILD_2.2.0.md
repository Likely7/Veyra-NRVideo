# Veyra 2.2.0 构建与对应源码

Windows x64 使用 MSVC 2022、CMake/Ninja Release、Qt 6.8.3 和 Inno Setup 7.1.1。NVIDIA 与 AMD 安装器和便携版共用同一套 Veyra 程序、QML 与着色器。产品源码快照为 `41d1aeb6`，其中 `ecbe0543` 是此次引擎和界面代码。发行标签补齐安装器 `7ecb5d1d` 和文档，生产源码与编译快照一致。实际文件身份见各包的 `package-manifest.json` 及发行页 SHA-256 清单。

配置 `VEYRA_DISPLAY_VERSION=2.2.0`、`VEYRA_UPDATE_TEST_PORT=`。发行的 `VEYRA_UPDATE_PUBLIC_KEY` 为 `984321f24a445f642c368a3a0d218dff75820138766a0b8e4c1281f3b871daa2`。自行构建时可使用自己的更新信任根，默认只检查新版本并打开发布页。构建依赖与选项见 [开发入口](DEVELOPMENT.md) 和 [Windows 安装包](WINDOWS_INSTALLER.md)。在线更新清单是 Ed25519 签名，安装器尚未做 Windows Authenticode 签名。

## 对应源码

- [2.2.0 对应源码发行](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.2.0-sources)提供 Veyra 源码、Chiaki 补丁与 libdovi 固定源码、RX7000 网络源码及校验和。主线不带程序包里的 BGM、实验运行组件、SDK、运行模型和用户数据。
- [固定基础依赖源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA-256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。Chiaki 基于 `0e16950165f06e5c3291537c2eeba6e852be7120`，使用 [锁定版本和 0001–0004 补丁](../scripts/remoteplay/dependency-lock.json)。libdovi 3.4.0 对应 `d1abe0e27ff2c7ab3339614d06db9f8a058af6b2`。
- [FFmpeg 9.0.1 slices256、dav1d、zlib](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA-256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。[去交错模块](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip)，SHA-256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`。
- [lmxxf 0.44 对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0-sources/lmxxf-044-corresponding-source.zip)，SHA-256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`。RX7000 网络源码归档沿用已发布的原件，SHA-256 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`。

## 运行资产

两分版可选后端取决于机器的 GPU 和驱动。内附实验运行组件的来源、哈希和许可证保存在 `release-runtime-manifest.json`、`licenses` 与 [第三方说明](../THIRD_PARTY_NOTICES.md)。ANR Beta 运行资产不随包发行。实时翻译模型和 CPU 推理程序通过软件内按需下载。启动音乐仅随可运行包分发，来源与许可见 `media/startup-music-notice.txt`。
