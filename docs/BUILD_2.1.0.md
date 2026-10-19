# Veyra 2.1.0 构建与对应源码

本次发行不重新构建或封包。用户验收的 `bilibili-2.1.0-20261019` 两ZIP原样晋升，仅外部文件名去掉-test；包内标签/原目录/清单不变，PE2.1.0.0。

- 应用精确源码快照：`66b321051bef9343fa2bcc6556a53a551ccc809c`，2,187文件；`Veyra-2.1.0-source.zip` SHA256 `d3ed0860cebef310eeceb5fafd790b553f78cecdf2d393e9f4c4d6ca87b32559`。之后到本次发布标签仅文档、README、新群图和发布校验脚本，不改产品输入。
- 两包EXE SHA256：`5b8a8920c4c67a4e1760e26b6c4f197ed532d429cca8effd59526642d7a03a65`。
- [原整合配方](BUILD_2.1.0_TEST.md)、[整合回归](RELEASE_2.1.0_TEST_2026-10-19.md)、[B站链接修正/最终包验证](BILIBILI_2.1.0_FIX_2026-10-19.md)。MSVC14.44/C++20、Qt6.8.3、Ninja；FFmpeg保留slices256/dav1d/zlib及独立去隔行模块。实际构建命令、依赖路径、开关与打包命令见上述原记录。
- NVIDIA包继续SF310.9.16社区原件和NVIDIA原版；AMD包提供lmxxf0.44的Small/Original两个本地源码构建模块及对应原样资产。美颜提供YuNet与MediaPipe权重及各自许可证；源码仓库不携带模型、HIP模块、SDK或专有DLL。

## 完整源码材料

1. [本版应用源码及lmxxf0.44对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0-sources)。后者SHA256 `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`，含两个AMD模块的固定上游源码、Veyra补丁、构建配方及许可证，不含衍生权重。
2. [保留slices256/dav1d/zlib的FFmpeg源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。
3. [独立LGPL去隔行模块对应源](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9-sources/FFmpeg-9.0.1-deinterlace-module-source.zip)，SHA256 `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`。
4. [保留的依赖及重链接材料](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。

GitHub自动生成源码不包含全部独立依赖源；请按以上材料和包内许可证重建。既有三个依赖资产未变化，引用固定链接而不重复上传。模型下载来源、哈希和AA/美颜来源见THIRD_PARTY_NOTICES.md。发行者manifest仅用于审计，不锁定用户替换DLL。Veyra为GPL-3.0，Chiaki组合同时适用AGPL-3.0及上游例外；专有权重不因外围MIT源码变成MIT。
