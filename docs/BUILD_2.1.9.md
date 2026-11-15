# Veyra 2.1.9 构建与对应源码

实际程序产品提交`b430d986c2adace6eae6cd2893fbb7a491473aaa`，PE2.1.9.0，EXE SHA256 `c8a53dc778331c696f5a488400885ef842adb849e1b22a9fbea07520a9ecb8f0`。两个显卡包使用同一EXE；发行沿用已验程序，不因发布文档重编译。MSVC2022/C++20、CMake/Ninja Release、Qt6.8.3、D3D12；本地构建命令和依赖入口见BUILD_2.1.9_2026-11-15.md及STARTUP_MUSIC_RESTART_2026-11-15.md。历史“未发布”只描述对应阶段，不覆盖本次正式发行。

发行标签与`Veyra-2.1.9-source.zip`包含本次README/发布文档/二维码；生产代码、QML、i18n、着色器和构建文件与实际产品提交相同，发布前用git diff核对。manifest同时记录built product、publication commit和源码ZIP SHA，SHA256SUMS与GitHub资产digest可核对。专有运行库/SDK、BGM、用户配置/凭据/测试媒体不进入Git或对应源码ZIP。

## 对应源码与固定依赖

- [应用与RX7000网络源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.9-sources)：`Veyra-2.1.9-source.zip`及`Veyra-2.1.9-rdna3-network-source.zip`。RX7000网络与2.1.6相同，归档SHA256 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`，应用归档摘要见SHA256SUMS。
- [Veyra-2.0.6-dependency-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。
- [FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。
- [FFmpeg-9.0.1-deinterlace-module-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip)，SHA256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`。
- [lmxxf-044-corresponding-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0-sources/lmxxf-044-corresponding-source.zip)，SHA256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`。

保留patched FFmpeg的PS5 slices256/dav1d/zlib及去交错模块，旧依赖源码未替换。VB-CABLE Pack46保持原SHA/许可；实际运行组件身份和签名以各包manifest为准，manifest不是阻止用户替换DLL的加载锁。

## AMF、音乐和ANR

AMF SDK v1.5.3、提交57ee0cfb0e1a08d9041f77435653794c1b012be0，MIT公共头文件仅供构建，路径通过VEYRA_AMF_SDK_ROOT传入；不随包携带SDK或amfrt64.dll，运行时从用户AMD驱动系统目录加载。自写适配和来源记录见THIRD_PARTY_NOTICES。

用户提供BGM.MP3并明确确认运行包分发，原件1847011字节、SHA256 `3005aa601f06a97d3968fd9fa171e870fceb4ec47586f1e950c9ed3c7875fb6a`。仅放运行包media/startup-music.mp3，独立notice/manifest，不进入源码Git/ZIP，也不纳入源代码GPL许可；不额外带音频SDK。

ANR Beta上游[明确禁止第三方再分发](https://github.com/apex-compute/ANR/issues/9#issuecomment-6857895225)，本次公开包无其运行库、模型、配置、程序包或生成loader；不自动下载/镜像。Veyra自写适配源码保留。本次其余运行DLL与已验本地2.1.9原字节相同，NVIDIA/AMD/Intel实验组件不等于厂商官方认证。
