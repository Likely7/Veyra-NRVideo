# Veyra 2.1.3 构建与对应源码
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

产品源码 `7a2d15d9222a98fac69e5343c01b443e1d7060be`，PE2.1.3.0，EXE SHA256 `76c4a44ee582dcd4150152c4e32e5e45f61fee1a35a285818dc7ee1464940f7a`。沿用已验2.1.3重构建ZIP原程序，发布仅更新文档、对比原图和发行元数据，不改程序/QML/shader/DLL/模型。

[应用及RX7000网络源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.3-sources)。源码包为发布准备Git快照，产品目录与上述构建提交相同。构建MSVC2022/C++20、CMake/Ninja Release、Qt6.8.3，入口scripts/acceptance/build-recording-handoff.py；[本轮构建验证](REBUILD_2.1.3_DEFAULTS_2026-11-06.md)、[主线验收](BUILD_2.1.3_TEST_2026-11-06.md)。AMD包带NVAPI LUID与HIP选卡修复，NVIDIA包带已核验的两VFX原件；来源/许可见manifest及THIRD_PARTY_NOTICES。

网络源码SHA256 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`，worker源码在应用档中。发行目录E:/项目/Veyra/releases/codex-release-2.1.3-20261106，报告reports同任务。

## 未变依赖的固定对应源码

| 依赖源码 | SHA256 |
|---|---|
| [Veyra-2.0.6-dependency-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip) | `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9` |
| [FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip) | `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6` |
| [FFmpeg-9.0.1-deinterlace-module-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip) | `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152` |
| [lmxxf-044-corresponding-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0-sources/lmxxf-044-corresponding-source.zip) | `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b` |

原始源码、固定提交、修改说明、FFmpeg补丁与构建材料随相应源码档及仓库保留。Veyra自身为GPLv3；Chiaki相关组合遵循随附AGPLv3及OpenSSL例外，其他依赖以各自许可证和THIRD_PARTY_NOTICES为准。运行包中的专有/社区实验组件按manifest记录身份，许可证随运行包提供。

English: the application snapshot above matches the published executable code. Unchanged dependency sources remain at the fixed, digest-verified assets listed here. Release tags additionally contain publication documentation.


English: the source archive is the publication commit; product directories are identical to the exact executable build commit above. Original dependency archives, licences, build instructions and FFmpeg slices256/dav1d/zlib patches remain available at their fixed links.
