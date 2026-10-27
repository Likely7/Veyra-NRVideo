# Veyra 2.0.7 发布计划

用户明确授权将已经交付的 2.0.7-test-r2 包直接发布 GitHub，六群替换五群，在中英文 README 和 Release 添加知识库，完整梳理从 2.0.6 起的变化，主下载区仅 NVIDIA / AMD 两个包。允许普通推送 main / 标签和新 Release；不关机、不代发群消息、不派 Agent。

## 固定输入

- 起点本地 main `67ab6960ead21c2917a7b05ad6676a8bb0d17f20`；隔离分支 `codex/release-2.0.7-20261013`，工作树 `E:/项目/Veyra/worktrees/release-2.0.7-20261013`。
- 已验完整包 `E:/项目/Veyra/test-packages/test-2.0.7-refresh-20261012/`。NVIDIA SHA256 `3105d62545a76b6e971c1e32f094bb312d5af22332e310c93c19f28550fd9036`；AMD `24ea0ed5f8ce6a65d3769e946a870c2eda3383c30864ead612fc3af673b46a2d`；应用 `6fd35109d3c668f51fd7ce1609c8176e164227e2510473ef5a6276a34b1d713d`。
- 原样晋级，只改外部下载文件名为 `Veyra-2.0.7-{NVIDIA,AMD}-win64-portable.zip`，ZIP 字节及全部包内文件保持。内部显示 2.0.7-test-r2、PE 2.0.7.0 在发布说明明示，不重新编译、修改 manifest 或假称包内为另一个版本。
- 新六群原图逐字节复制 `docs/images/2.0.7/community-group.png`，SHA256 `a237d642efbe3e83294b607cf2ae2937f51f63549eddd356f819481606647844`；图示 10 月 19 日前有效。原微信赞助图、Discord、Ko-fi 和两图 width220 保留。软件内支持入口原本打开在线 README，因此不需修改软件。
- 知识库使用用户给定完整飞书 URL，描述来自用户提供的“四个群聊汇总”。web 工具不可访问该页面，不冒称读过库内文档。

## 执行

1. 独立 `archives/release-2.0.7-20261013/start.json` 固定 69 旧工作树 / 509 脏文件、分支 tip、原交付 ZIP 和原群图；增量 bundle 已 verify；旧 v2.0.6 Release API 回执保留。
2. 对照 `v2.0.6..67ab696` 所有非 merge 提交、最终产品 diff、对应验收文档整理双语说明。区分最终保留、被撤回、仍未解决和历史测试，不重复宣传 2.0.6 已发布收益。VFG 混合是本项目最新研究的分配策略，底层仍为 NVIDIA 模型、逐子帧推理；XeSS 改善有限、有成本。
3. README 更新当前版本、下载、最新变化、知识库及六群，其余内容尽量保持。追加本版报告、构建/对应源码入口和 WORKLOG，不改产品实现、UI频率、优先级、DLL/模型/驱动或用户配置。
4. 校验原 ZIP SHA/CRC/全 payload manifest、运行组件清单与已通过四次真实解压启动回执相符；原应用源码/FFmpeg源哈希和版本边界保持。无产品修改，不重跑无根据的性能测试。
5. 主 Release `v2.0.7` 只有两个用户包。单独非 latest 源码归档 `v2.0.7-sources` 提供原样应用源码及新的 zlib-enabled patched FFmpeg 源；未改的完整依赖源码直接链接 v2.0.6 既有资产。保留 GPL/AGPL/LGPL 对应源码、构建和重链接材料访问，不把 SDK/runtime/模型上传源码 Git。源码归档说明与主正文互链。
6. 干净发布分支合入本地 main，确保产品树与已验起点相同。普通 atomic 推送仅 nrvideo/main、v2.0.7、v2.0.7-sources；标签绑定发布文档提交。先草稿上传、服务器大小/digest/正文核对，通过后正式发布主版本为 latest，源码归档非 latest。
7. 复核远端 main/标签、主资产恰两项、所有下载/六群/赞助/知识库入口与原 QR SHA。不强行称飞书登录页可读。保全旧 release / 旧包 / 其它工作树；生成真实发布回执和 <=1000 字可供用户转发群公告。

## 不能包装成完成

NV 长期显存增长未根治；BMD 黑屏与用户 HDR 闪黑候选需实机复测；边缘神盾四肢/栏杆残差；AMD/Intel 实卡和屏幕延迟未全面验收。扩展 NR+DLSS 热切换回放仍超时，原 2.0.7 同场也复现；定向回归通过不等于全部检查全绿。内部测试标识保留是“直接这个包”的实际结果。

本轮所有新增产物在 `E:/项目/Veyra/{archives,logs,tmp,reports,releases}/release-2.0.7-20261013/`。实际远端回执以该任务 logs / reports 为准，完成前不称已发布。
