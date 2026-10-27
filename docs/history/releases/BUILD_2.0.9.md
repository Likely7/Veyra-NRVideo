# Veyra 2.0.9 构建、运行组件与完整对应源码

正式发布原样晋升已交付2.0.9-test ZIP，只改外名。PE2.0.9.0，EXE SHA256 `7add56b9651d6b629433419fb0d2d9d077057d9a77595ebeabbf4d5fb3b80f80`；产品0ac9d39/745输入/52EXE，应用源码c9e779b。发布标签加入README/发布文档，产品Git内容不变；新checkout的LF/CRLF差异按归一化与Git对象核对，不冒称是旧构建树原始SHA。

| 对应材料 | SHA256 |
| --- | --- |
| [应用源Veyra-2.0.9-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/Veyra-2.0.9-source.zip) | `efc2463bf09bb2936e36e3537bbb23a5de11e252d17aeabee41d099ad68ab408` |
| [新增去隔行模块完整FFmpeg源](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip) | `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152` |
| [已有slices256/dav1d/zlib FFmpeg源](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip) | `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6` |
| [已有完整依赖/重链接材料](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip) | `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9` |

构建环境与开关见[2.0.9测试构建记录](BUILD_2.0.9_TEST.md)和[去隔行模块构建](BUILD_DEINTERLACE_2026-10-16.md)：MSVC2022 x64/C++20、CMake/Ninja、Python3.11、Qt6.8.3（Network）、既有外部SDK显式路径。对应发布EXE使用 `-DVEYRA_DISPLAY_VERSION=2.0.9-test` 与 `-DVEYRA_ENABLE_DEINTERLACE=ON`；新前缀头文件置前，保留原FFmpeg五DLL/dav1d。独立avfilter-12.dll SHA `a1b1db7d702f23fb2851b6733071c815da2f753995a7f7490768ab05e3c00a4d`，248832bytes，LGPL补丁/构建信息/许可证齐全。

NVIDIA NR新版SF310.9.16.0/276162560bytes/NotSigned/SHA `CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED`，原样复制用户提供且与作者RAR匹配的兼容构建；NVIDIA原版310.8.0.0/165840496bytes/Valid/SHA `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`保留。旧Lecram/SF-v2不在新包；AMD534运行组件行/文件沿用2.0.8。所有专有SDK/运行库/模型与源码分离，清单供发行审计而非用户替换锁。

Veyra原有代码GPL-3.0；Chiaki组合部分同时适用AGPL-3.0及上游OpenSSL例外。构建须保留完整依赖许可、对应源码/补丁及重链接材料，GitHub自动源码档不包含所有外部依赖。另见[第三方来源](../../../THIRD_PARTY_NOTICES.md)。
