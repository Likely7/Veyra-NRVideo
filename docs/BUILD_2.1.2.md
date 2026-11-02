# Veyra 2.1.2 构建与对应源码

应用版本 2.1.2，PE 2.1.2.0。产品源码提交 `358be8cfe82a1563cc05fdeca34f684a0e95b6fc`；主 EXE SHA256 `33d8e615666365ba84f1cfebbc6ffef4bb6c2dbaf08119bdad6648e7d9cefa44`。

沿用 Claude 2026-11-02 最新去重修复包内已验程序及组件。公开发布仅替换说明、二维码和发行元数据；未重新编译或修改程序、QML、shader、DLL、模型。对应原构建与验证见 [本地构建记录](BUILD_2.1.2_TEST_2026-11-02.md)。发布目录 `E:/项目/Veyra/releases/codex-release-2.1.2-20261102`，机器回执 `E:/项目/Veyra/reports/codex-release-2.1.2-20261102`。

## 本版应用与新增网络源码

- [应用源码 Veyra-2.1.2-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.2-sources/Veyra-2.1.2-source.zip)：发布整理提交的 Git 快照；其中产品目录与上述构建提交无差异，额外包含更新后的发行文档、二维码、打包脚本。摘要见该 Release 的 SHA256SUMS.txt 及运行包 package-manifest.json。
- [RX 7000 网络对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.2-sources/Veyra-2.1.2-rdna3-network-source.zip)：SHA256 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`。RedDuke/ZLUDA 固定提交、许可证与修改说明见 THIRD_PARTY_NOTICES、AMD 包中的来源清单及源码内 source-manifest.json。原 Veyra 原生 worker 源码在应用档中。

MSVC2022/C++20、CMake/Ninja Release、Qt6.8.3。构建入口 `scripts/acceptance/build-recording-handoff.py`，目标 `veyra_qml_ui`、`veyra_amd_nr_worker`；实际构建回执 `E:/项目/Veyra/logs/claude-build-2.1.2-dedup-20261102/build-20261102-041414/receipt.json`。源码 Git 不含专有运行组件、模型或 SDK，开发依赖另行准备。

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
