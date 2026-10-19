# Veyra 2.1.0 发布计划

用户于2026-10-19明确验收 `test-packages/bilibili-2.1.0-20261019` 两包并要求直接发布，不重新打包；完整整理2.0.9以来Claude/Codex合入内容，重点AMD、抗锯齿、美颜、采集格式选择，替换提供的6群二维码，最后拟群公告。授权本轮发布所需main快进、普通push、新标签和GitHub Release；不代发群消息、不关机、不继续Lecram。两个Lecram实验分支保持，不进入本版。

起点main d91ec9f，应用对应源66b3210，EXE SHA256 `5b8a8920c4c67a4e1760e26b6c4f197ed532d429cca8effd59526642d7a03a65`。存档标签 `checkpoint/pre-codex-publish-2.1.0-20261019`（原同名pre-release标签属于Claude整合前，保持不动）。施工分支 `codex/release-2.1.0-20261019`；仅发行文档/README/新群图/发布校验脚本，不改产品或运行文件，不重建、不重压缩已验包。

1. 核对原DELIVERY、冷启动、原ZIP大小/SHA/CRC与组件清单；硬链接为正式外名，内部2.1.0-test和原文件夹/清单保留并明确说明。
2. 按v2.0.9..d91ec9f全部提交/路径生成变更索引；人工整合产品行为、验证及未验证项。新群图原样复制，赞助码/Discord/Ko-fi/知识库保持。
3. README中替换版本更新区并纠正本版AMD模型说明/下载/构建链接，不重写无关教程。主Release仅NVIDIA、AMD两个ZIP；应用原样源码与AMD 0.44对应源码另放v2.1.0-sources（非latest），不变FFmpeg/依赖源引用既有固定资产并核对digest。
4. 校验所有改动、两包与原交付哈希相同、发布正文完整支持区块、图像可访问。正常快进main，普通atomic push main与两个新标签。先草稿上传、核对服务器size/digest/state，发布源码后发布主版latest；再次核对latest、二维码及仅两个运行资产。
5. 报告实际发布SHA、资产/URL/结果，不将本机5070或fake-provider回归写成AMD/圆刚全型号实卡验收，不将旧SF速度数据当成本次新增收益。

全部新产物统一 `E:/项目/Veyra/{archives,releases,reports,logs,tmp}/release-2.1.0-20261019`，不复制整工程、不派Agent、不修改用户配置/驱动/原运行库，不覆盖其他Agent施工。
