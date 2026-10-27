# Veyra 2.0.8 发布计划（2026-10-15）

用户实机测试通过，授权将现有两个测试包仅改外部文件名后发布GitHub 2.0.8。内容优先级：延迟优化、窗口捕获游戏模式、可变刷新、采集卡画面与手柄转发及第三方远程软件配合、其余双方修复。此次没有重构、重编译或重新封包需求；不代发群消息、不关机、不关闭远端issue。

固定输入：本地main `3107e0076a0a8ad3cfdee767a0ea36152ed402a4`；源ZIP封存提交 `80a3d7c`；产品构建 `99435ae`，679输入、35EXE。NVIDIA原ZIP SHA256 `e89972d234fc79167c08b2183b42347c9ace606663b9ac363e2d09a547afe0ba` / 718608815字节；AMD `1740d7dbc6c8307d0dd8deefb9aaafac4398ed0e40022b1e40e66a86092ecda9` / 355816850字节；两包相同EXE `a4676e9a5bf08738dcc85c6c6909045f0593ebadcf857e6108904e90d4df2432`。`test-packages/claude-release-2.0.8-20261015/DELIVERY.json`和四场最终ZIP启动回执为原验证证据。

1. 复用E盘release-2.0.8-20261015工作树，切新分支codex/release-2.0.8-20261015，原handoff分支和所有旧分支不动。archives/release-2.0.8-20261015独立固定77工作树/509脏文件、旧包及原2.0.7 Release API回执，增量源码bundle verify。
2. 对照v2.0.7..3107e00的全部非merge提交、最终产品diff、双方验收/现场文档建立变更覆盖索引。公开说明只讲当前行为和本次改进，不叙述用户要求省略的旧显示同步设定；保留历史技术证据，不改写已有日志。区分撤回的自动降档与最终只提示方案；不把理论延迟、CPU提交返回或模拟VRR称为光学端到端收益。
3. 更新中英README的版本/新增内容/下载/构建入口，保留其它功能说明及六群、赞助、Discord、Ko-fi、知识库。完整Release分重点功能、用法、全部修复、实测条件和边界。PS5手柄专用会话与第三方远程电脑游戏回传分别讲清。
4. 将原ZIP/应用源码ZIP以硬链接复用为正式外部文件名，不修改任何包内字节。逐包SHA/ZIP CRC/payload manifest/runtime清单/EXE与四场冷启动证据绑定；679构建输入仍一致。内部2.0.8-test、PE2.0.8.0和测试manifest状态保留并说明。没有产品变化，不重做GPU性能测试。
5. 主v2.0.8只有两个用户包；v2.0.8-sources为非latest源码归档，上传原样应用源码。patched FFmpeg/dav1d/zlib源沿用v2.0.7-sources既有固定资产，完整依赖/重链接材料沿用v2.0.6源，校验SHA并互链，不重复上传未变的600MB依赖。
6. 干净发布分支快进main，普通atomic推送仅nrvideo/main和两个新标签，不移动旧标签。先建草稿/上传/核对服务器digest、size和逐字正文，再发布源码归档非latest、主版latest。保留旧Release/其它分支与本地旧包。
7. 发布后核对远端main/标签/latest、恰两主资产、来源源码、文档链接和二维码实际可访问性。WORKLOG/报告写实际回执，最终保护复核和bundle完成；收尾仅记录文档，不改变已验ZIP和产品输入。

所有研发产物在E:/项目/Veyra/{archives,logs,tmp,reports,releases}/release-2.0.8-20261015。已有验证副本递归清理被自动审批拒绝，本轮不重试该删除；不让清理阻塞发布。
