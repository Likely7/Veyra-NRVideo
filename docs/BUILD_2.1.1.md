# Veyra 2.1.1 对应源码 / Corresponding source

应用版本2.1.1，PE版本2.1.1.0。发布ZIP于2026-10-27重新构建，运行库与模型身份沿用2.1.0。源码与运行包分别分发。

## 应用源码

[Veyra-2.1.1-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.1-sources/Veyra-2.1.1-source.zip)

软件绘制与RTX视频超分默认项整合后重新构建。准确应用快照见源码ZIP内source-snapshot-manifest.json；构建提交、EXE哈希与对应源码哈希记录于运行包package-manifest.json及GitHub Release资产。

MSVC2022/C++20、CMake/Ninja Release、Qt6.8.3。通用构建入口scripts/acceptance/build-recording-handoff.py，构建目标veyra_qml_ui；本机2.1.1构建记录见docs/BUILD_2.1.1_REPORT_2026-10-26.md。SDK/模型/专有运行文件独立准备，不包含在源码Git中。

## 未变依赖的固定对应源码

以下依赖身份与发布运行组件相符，保留原固定资产链接及SHA256。

| 依赖源码 | SHA256 |
|---|---|
| [Veyra-2.0.6-dependency-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip) | `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9` |
| [FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip) | `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6` |
| [FFmpeg-9.0.1-deinterlace-module-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip) | `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152` |
| [lmxxf-044-corresponding-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0-sources/lmxxf-044-corresponding-source.zip) | `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b` |

原始源码、固定提交、修改说明、FFmpeg补丁与构建材料随相应源码档及仓库保留。Veyra自身为GPLv3；Chiaki相关组合遵循随附AGPLv3及OpenSSL例外，其他依赖以各自许可证和THIRD_PARTY_NOTICES为准。运行包中的专有/社区实验组件按manifest记录身份，许可证随运行包提供。

English: the application snapshot above matches the published executable code. Unchanged dependency sources remain at the fixed, digest-verified assets listed here. Release tags additionally contain publication documentation.
