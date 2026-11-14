# 2.1.6公开发布计划

用户明确要求整理2.1.5至今全部更新，尽量附数据并发布GitHub。本条覆盖此前本地构建的无push/Release限制；不包含关机或群聊代发。基线main6cb960b，隔离codex/release-2.1.6-20261113，源码沿用integrate-2.1.6-20261113，不动旧脏树与用户配置。

1. 对照已发布v2.1.5和完整Git差异，整理功能/修复/设置迁移/测量条件/已知限制，中英文Release正文、README与对应源码文档一致。数据只用已保存的验收原文，标明迭代A/B而非伪称所有数据均来自两正式包直接比较；软件Present不冒充光学延迟。
2. 继续使用已验收2.1.6产品EXE bf27ebed…，QML/词库保持最新；仅更新发行文档、元数据和载荷边界。ANR协作者2026-11-13于Issue9明确禁止第三方再分发，公开包不带其任何DLL/模型/配置/程序包，保留独立适配源码与禁用提示；本机完整实验包不改。其他运行原件沿用已批准身份，不改盘/重签名，不进源码Git。
3. 新产物统一E:/项目/Veyra/{releases,reports,logs,tmp,tests,verify,archives}/codex-release-2.1.6-20261113。公开NVIDIA/AMD便携ZIP、精确应用源码、RDNA3网络对应源码及SHA256SUMS。发布标签v2.1.6/v2.1.6-sources，应用设Latest、sources不抢Latest。
4. ZIP逐文件/88着色器/模型/运行库与PE导入核验；公开包无ANR载荷、正常播放/选择项回退/实际UI检查、最小PATH解压冷启动。保留2.1.5原资产与标签。源码仅常规快速前进推送nrvideo main，不推送origin或私人检查点。
5. 使用本机gh（连接器无Release上传能力）先创建可审查草稿并上传，核对服务端大小/SHA和正文，随后正式公开；核实Latest、标签、下载、两二维码各width220与实际图片字节、对应源码链接。固定支持区沿用10群与微信赞助/Patreon/Discord，不更换图片。

最终事实写docs/RELEASE_2.1.6_EXECUTION_2026-11-13.md、WORKLOG与reports同任务FINAL.json。单Agent，不声明独立Reviewer。
