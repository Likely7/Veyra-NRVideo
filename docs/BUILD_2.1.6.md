# Veyra 2.1.6 构建与对应源码

应用精确源码提交 `f46df7f7cec4e8d2df3b8fc7bcbc9b2e9d7ad54e`，PE2.1.6.0。EXE SHA256 `bf27ebed5d81a79814441671e933c7a8ef6be584c784f75b8ec80493642c10f3`，沿用已验收界面修订后的同一二进制；后续发行只改文档和公开载荷元数据。NVIDIA/AMD包使用同一个EXE，MSVC2022、CMake/Ninja Release、Qt6.8.3。

应用源码归档SHA256 `f1d365fe9560d566574dbf8c5e337b6427631f0858a857524dcb113f8c2851c9`；RX7000网络对应源码SHA256 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`。精确源码ZIP基于实际产品提交；GitHub自动标签源码另外包含发布资料。包内manifest列明产品/发行提交、逐文件身份及源码链接。

## 固定依赖源码

- [Veyra-2.0.6-dependency-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA-256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。
- [FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA-256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。
- [FFmpeg-9.0.1-deinterlace-module-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip)，SHA-256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`。
- [lmxxf-044-corresponding-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0-sources/lmxxf-044-corresponding-source.zip)，SHA-256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`。

[应用与RX7000网络源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.6-sources)。FFmpeg保留PS5 slices256/dav1d/zlib补丁和去交错模块，其他已发布依赖没有替换。VB-CABLE Pack46原包继续按原SHA/许可嵌入，未修改驱动。运行库/模型/SDK/凭据/配置/测试媒体不在源码Git。

## 公开载荷

从已审核最新2.1.6本地完整包读取已批准的运行原件，公开包移除ANR全部19个运行文件与本地实验说明，明确无ANR核心/模型再分发。上游协作者[2026-11-13禁止当前Beta第三方再分发](https://github.com/apex-compute/ANR/issues/9#issuecomment-6857895225)。保留Veyra自写适配源码，外部组件缺失时选项不可用；不自动下载或镜像。原本机实验包和用户配置保留。NVIDIA/Intel/AMD组件继续标注实验身份，不宣称厂商认证；清单不作为用户替换DLL的哈希锁。

公开包检查包括ZIP逐SHA、PE依赖/许可/模型、全部88个着色器、无ANR载荷、无用户数据；Windows最小PATH重新解压启动/播放与ANR不可用路径实测。实际结果见发布执行记录，性能条件见[验收汇总](RELEASE_VALIDATION_2.1.6.md)。
