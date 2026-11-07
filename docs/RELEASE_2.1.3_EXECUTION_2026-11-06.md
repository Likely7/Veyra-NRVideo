# 2.1.3 发布方案与执行

用户2026-11-06明确授权发布GitHub，详细汇总2.1.2以来更新并公开所提供超分对比原图。目标Likely7/Veyra-NRVideo，两个显卡运行ZIP与对应源码Release，保留交流群8/赞助两图各width220。沿用已验重构建不可变ZIP作供体；不读取用户运行目录，产品字节不改。

正常fetch已确认远端main为祖先，不覆盖远端；提交发布准备，git archive归档，脚本release-2.1.3.py逐文件核验供体并仅更新文档/发行元数据。推送main及v2.1.3/v2.1.3-sources，草稿上传核对服务端摘要后先公开源码，再将运行版设Latest。校验远端资产SHA/大小、匿名下载、图片字节、二维码宽度、标签与Latest。实际结果以reports/codex-release-2.1.3-20261106/FINAL.json为准。群公告仅交付文字，不代发。

## 实际公开与远端核验完成

发行准备0a09d376a7d0a185bdb1d1a54b1b666dc25e7a60，产品7a2d15d9222a98fac69e5343c01b443e1d7060be。main与v2.1.3/v2.1.3-sources已原子推送；两个Release均为公开正式发布，运行版为Latest。7个资产服务端SHA256/大小/状态及匿名下载HEAD200、群8/赞助各width220、超分原图字节、远端README/标签/Latest核验通过；4份固定依赖对应源码摘要和可访问性核验通过。

Veyra-2.1.3-NVIDIA-win64-portable.zip：726570478 bytes，SHA256 `464a46ac1874acf36e43f7d3f53db336c3e7a0c59f156237d8226d94e63f7b7f`。

Veyra-2.1.3-AMD-win64-portable.zip：717577602 bytes，SHA256 `dc1223f0aff0feb397f2c40706a3f608b9e147d5390892d7631295759e17a692`。

应用源码SHA256 `52e73bfa40422238f3fe35f7d3fbb0188c2ff759671cb6bda90a782824161d5b`。实际执行scripts/acceptance/release-2.1.3.py；git push --atomic nrvideo main refs/tags/v2.1.3 refs/tags/v2.1.3-sources；reports同任务publish.py body/drafts/sources/packages/publish/verify。AMD首次上传被原300秒脚本上限中断，服务端无残留；延长至1800秒重试，既有NVIDIA资产按摘要跳过。每个上传先核验远端摘要，再公开；源码先公开，运行版后设Latest。产品字节与供体ZIP逐文件一致，不重复占用采集卡；新产物仅在E盘。全部ZIP清单与SOURCE校验通过，无专有二进制/SDK混入源码Git，无用户配置进入运行包。群公告810字符，文件reports同任务GROUP_ANNOUNCEMENT.txt，未代发群消息。
