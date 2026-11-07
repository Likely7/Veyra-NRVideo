# 2.1.3 发布方案与执行

用户2026-11-06明确授权发布GitHub，详细汇总2.1.2以来更新并公开所提供超分对比原图。目标Likely7/Veyra-NRVideo，两个显卡运行ZIP与对应源码Release，保留交流群8/赞助两图各width220。沿用已验重构建不可变ZIP作供体；不读取用户运行目录，产品字节不改。

正常fetch已确认远端main为祖先，不覆盖远端；提交发布准备，git archive归档，脚本release-2.1.3.py逐文件核验供体并仅更新文档/发行元数据。推送main及v2.1.3/v2.1.3-sources，草稿上传核对服务端摘要后先公开源码，再将运行版设Latest。校验远端资产SHA/大小、匿名下载、图片字节、二维码宽度、标签与Latest。实际结果以reports/codex-release-2.1.3-20261106/FINAL.json为准。群公告仅交付文字，不代发。
