# Veyra 2.0.8 构建与对应源码

2.0.8按用户要求直接晋升已验测试ZIP，只改外部发布文件名。包内显示2.0.8-test，PE2.0.8.0，EXE SHA256 `a4676e9a5bf08738dcc85c6c6909045f0593ebadcf857e6108904e90d4df2432`；产品构建99435ae，应用源码归档80a3d7c。发布标签随后加入README与Release文档，产品679构建输入没有变化。

- [应用源码 Veyra-2.0.8-source.zip](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.8-sources/Veyra-2.0.8-source.zip)：SHA256 `4ca39a70797c6080ef02646e1d2aec0a392346ea3594206c8809f188a64d3d99`，原样复用测试源码归档，不含运行库/SDK/模型/私有配置或媒体。
- [patched FFmpeg 9.0.1 / dav1d / zlib对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)：SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`，与本版运行文件对应，保留PS5 slices256和压缩字幕所需zlib；原资产不重复上传。
- [完整依赖源码与重链接材料](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)：SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`，与已发布归档逐字节相同。

遵循[2.0.8测试构建说明](BUILD_2.0.8_TEST.md)、[2.0.7说明](BUILD_2.0.7.md)与[基础构建说明](BUILD_2.0.0.md)：MSVC2022 x64/C++20、CMake/Ninja、Python3.11、Qt6.8.3（含Network）、外部运行组件/SDK路径显式指定。对应已发布EXE使用 `-DVEYRA_DISPLAY_VERSION=2.0.8-test`。Windows中文终端先设UTF-8代码页再构建，避免/showIncludes前缀编码不匹配。

Veyra原有代码GPL-3.0，Chiaki组合部分适用AGPL-3.0与上游OpenSSL例外。许可、源码、补丁与重链接材料属于完整对应材料，不能只下载GitHub自动生成的仓库源码就认为包含所有外部依赖。专有运行组件仅随用户包提供并保留原身份/许可证，不进入源码Git。
