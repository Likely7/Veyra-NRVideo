# Veyra 2.0.7 对应源码与复现

2.0.7原样发布已验2.0.7-test-r2，显示标识仍为该值，PE为2.0.7.0。两运行ZIP的字节未修改；源码、依赖与原构建相同。发布标签增加README/报告/六群图，不修改任何产品源文件。应用源码归档commit a94a1ce与main67ab696完整树相同；最终EXE构建输入668项在原验收回执逐项绑定，之后仅脚本/文档更新。

## 下载对应源码

[独立源码归档（不占主版本两项下载）](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7-sources)：

- [应用源码test-r2](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/Veyra-2.0.7-test-r2-source.zip)，SHA256 `52a01c540fec394596730ae9601e0a081360f9d81b91e60086567d329c915ac3`。
- [patched FFmpeg9.0.1 / dav1d / zlib源](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7-sources/FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip)，SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`。保留PS5 slices256、启用zlib；旧2.0.6依赖源中的FFmpeg不能替代当前配置。
- [未改变的完整依赖源/重链接材料](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/Veyra-2.0.6-dependency-source.zip)，SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`。不重复上传598.7MB相同文件。

两运行包中历史相对源码路径仍为原测试交付布局；从这里下载上述三份源即为完整对应材料。当前发布文档可从v2.0.7源码标签取得，产品实现与应用归档一致；GitHub自动Source code ZIP只含应用仓库，不代替依赖源。

## 构建

按[原完整构建说明](BUILD_2.0.7_TEST.md)配置外部Qt、FFmpeg、串流、DeckLink/Magewell及合法取得的厂商SDK。工具脚本的本机路径是可复现输入记录，换机器需显式调整，不将SDK/模型/运行库当开源对应源码上传。

```text
python -X utf8 -B -I scripts/acceptance/test-2.0.7-build.py --task test-2.0.7-refresh-20261012 --label 2.0.7-test-r2
```

显示覆盖为`-DVEYRA_DISPLAY_VERSION=2.0.7-test-r2`，数值版本为2.0.7.0。原实际构建548步/27目标，141.9秒；本轮没有重新构建来伪造一个与用户已验包不同的二进制。最终EXE SHA256 `6fd35109d3c668f51fd7ce1609c8176e164227e2510473ef5a6276a34b1d713d`。

源码保留GPL/AGPL/LGPL及第三方归因；专有组件来源/版本/签名/哈希在包内运行清单，清单不作为用户替换DLL的固定哈希锁。Open-source source archives exclude proprietary SDKs/runtimes/models. Download the application, current patched FFmpeg source and unchanged dependency source above; the original build instructions preserve the exact test-r2 display override and external SDK configuration.
