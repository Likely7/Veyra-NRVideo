# 当前工作日志

完整旧日志：[截至本轮整理前的记录](history/worklog/WORKLOG_before_maintenance_2026-10-27.md)。当前状态见[CURRENT_STATUS](CURRENT_STATUS.md)，历史授权见[历史索引](history/INDEX.md)。

## 2026-10-27 项目整理1–4

用户授权执行全部四项：清退休产物、收拢分支/工作树、文档归档、按职责整理桥接与QML并验证。分支codex/project-maintenance-20261027，main基线7bf02d4，当前正式版仍2.1.1；不push/Release，Lecram暂停。

- 保全：全部609refs、101工作树状态；6脏树516状态原字节ZIP逐SHA核验（70,537,460字节），完整Git bundle 77,589,296字节通过verify且覆盖原引用。
- 产物：43退休目录/54,827文件，保留12,160份脚本/配置/日志/源码及应用EXE；按SHA对象去重的证据ZIP341,177,909字节，复核通过。先记录并剪枝旧runtime junction，再只unlink其入口，不递归目标。
- 执行：cleanup_native.ps1核对绝对路径/父链/进程、所有文件ID/mtime/size和链接目标；删除43旧目录。E卷空闲增加24,123,240,448字节（22.47GiB），与本轮归档成本分开报告。
- Git：retire_native.ps1重新核对HEAD/状态/ignored及bundle恢复tip，安全移除89干净工作树，122分支引用归入已验证bundle。6脏树/暂停线保留；远端refs、发行标签和共同Git库不动。
- 文档：旧日历式AGENTS/CURRENT_STATUS/WORKLOG移入history，历史计划/报告/发行过程分类并重写Markdown相对链接，旧路径映射可查；历史guard/start不改。恢复用户2026-10-20的NV/AMD纠偏，不拿旧状态当待办。

本轮全部原始证据：`E:/项目/Veyra/reports/project-maintenance-execution-20261027`；恢复归档：`E:/项目/Veyra/archives/project-maintenance-execution-20261027`。

代码整理提交`d4b1034`：桥接主文件9233→640行，14个领域CPP与私有状态头；对话框2277→134行；专业页3213→2144行，独立音频/显示/性能组件。公开头和引擎/管线/采集/音频/着色器未改；681个方法体、QML文案/objectName、2681条翻译内容一致。

构建52.7秒、后续10目标23.2秒通过；16组检查通过，QtQuick117/0/1跳过。四次真实软件/GPU/兼容启动、两种渲染器下十对话框/五页签/37%预设往返通过；软件/GPU文件播放各290实际呈现帧。View首次共享GDI初始化声明缺口已修为唯一实例；自动迁移误改三条对象名被结构检查拦下并恢复，最终复验通过；Playlist初次夹具参数错误exit2保留JSON，按合同改用新目录后通过。

611历史文档/规则归档并修正相对链接；当前和历史Markdown链接目标检查无缺失。新开发入口docs/README.md、DEVELOPMENT.md、ARCHITECTURE.md；历史guard/start保持，旧运行产物链接标注退休路径。

收尾保护检查：516条脏状态对应文件原始字节全部一致，保留树HEAD/status一致，2.1.1正式两包/源码SHA一致，两个正式运行供体全文件一致，测试运行组件179文件身份保持；461发行/检查点标签和远端refs未改。E卷空闲相对开工前净增约28.14GiB，最终值以FINAL.json为准。未重复真实采集或全硬件推理/性能验收。

最后本地整合main并关闭施工分支；不push、不Release，不修改真实配置、驱动、SDK/模型原件或暂停的Lecram。

## 2026-10-27 GitHub交流群二维码更新

用户明确授权更新GitHub群码。仅发布新群7截图、README.md/README_CN.md及RELEASE_SUPPORT中的图片地址与有效期，2.1.1 Release正文同步替换为2026-11-03前有效；赞助图、两图width220、其他发布说明与正式资产保持。README_EN.md为指向英文首页的入口，无需改动。

远端main原9520946，二维码提交a983c18；连接器上传后逐SHA/PNG解码发现图片损坏，改用原文件字节通过Git修复提交26480b2，最终远端原图77616字节、SHA256 AEDA3DF086F9C0983055A4F73EB2BD9F422C03E5C1910AF603A6ED0E79F2BDF9，与用户截图完全一致。两次远端提交均基于远端main，只含二维码范围，未推送本地项目整理提交。

GitHub连接器用于文件树/提交/引用更新，缺少Release编辑接口时用gh release edit --notes-file；随后合并远端二维码改动回本地main。verify.py核对远端三文档、原图字节与PNG解码、赞助图访问、两图宽度、Release正文仅群码/日期变化，全部通过；不涉及产品代码，无需重建。证据、原正文与最终回执：E:/项目/Veyra/reports/github-group-qr-20261027/。真实配置、依赖和正式包未改。

## 2026-10-28 单层RTX三段式超分接手完成

用户要求接手Claude额度用尽前未完成的功能。阅读原会话确认最后范围为单层RTX视频超分“先降到→超分到→再缩到”，沿用claude/sr-stage-20261028，基线7b0b64b；不合并main、不push、不Release，不把独立capture-fps-audio分支混入。保全33个改动文件/patch至archives/codex-sr-stage-handoff-20261028并逐SHA验证，起点标签checkpoint/pre-codex-sr-stage-handoff-20261028。

复查新增真实GPU边界测试，先复现4K原生NR先行→1080p先降→2K输出被旧NR≤work边界错误拒绝；修为按NR实际所在阶段nrFullExtent校验，保留逐层校验，修复后6帧处理通过。独立导出共享参数布局改变，协议11→12，补两个实际worker/NVENC任务验证新字段、执行尺寸与完整帧数。

构建：复用tmp/claude-sr-stage-20261028/build/build.py，MSVC/Ninja并发2，产品/相关测试目标均构建通过；主要增量26.8秒、后续6.9秒/3.1秒。运行目录复用tests/claude-sr-stage-20261028/app，通过新文件替换EXE/QML，不写穿供体链接。

本轮run.py记录：六组unit/contract/QML退出0（RepairContract272项0失败）；GPU stage-boundaries、stage、stage-capabilities、原单步SR四组退出0，新增24个实际VSR组合全部成功。GUI验收覆盖列表15项与节点4项，退出0、无QML错误；首次节点夹具未向空图添加SR而等待，停止本轮测试进程、保留失败记录，修正夹具后通过。两个实际导出各4源帧/4编码帧，ffprobe核对H.264、2560×1440及3840×2160。i18n extract --check缺失0、占位符0，git diff --check通过。

GPU首次runner的stdout路径重名，前组stdout被覆盖；各组exit/末尾标记仍在gpu.json，各组独立engine事件日志保留；runner已改用序号。未借机重复整轮测试。未做main二进制逐像素对照、长时动态画质/显存峰值或实卡采集；Claude原图片A/B与本轮定向验收分开陈述。

详细范围、命令、失败与验证见docs/SR_STAGE_HANDOFF_2026-10-28.md；本轮JSON/日志：reports与logs/codex-sr-stage-handoff-20261028。正式2.1.1包/SDK/模型/用户配置不改，Lecram继续暂停。

## 2026-10-28 三段式SR整机卡死后续防护

用户报告1080p先降/8K输出导致整机卡住后重启。新分支codex/sr-stage-freeze-20261028从cf84762开始，checkpoint/pre-sr-stage-freeze-20261028；旧测试构建不再推荐。只读保全实际hang dump、相关系统事件与会话参数，真实配置未改，不重跑原危险组合。

配置解码确认质量2 RTX视频超分、先降1080p、8K最终输出/不再缩、NR Ampere开启、VFG 2X开启。系统41证明非正常重启；重启后处理的旧日期141报告不能冒充本次证据。EXE无PDB，离线同OBJ/LIB重链接MAP的.text与旧构建逐字节一致，原始栈候选映射指向DXGI/PresentSink resize与VideoPresenter；线程当前IP在系统驱动查询/BitBlt。没有完整调用栈、当时显存值或对应内核dump，不把显存压力怀疑写成已证实根因。

防护：新三段式SR同时开NR/FG时，最终处理尺寸限制4K像素预算，保留8K内部评估。列表/节点参数在同一事务补齐再缩4K；旧会话恢复复制不可变节点文档后修正，明确提示；底层validateFixedExecutionPlan在分配/SDK前拒绝绕过UI的危险组合。首轮构建因写入const节点文档失败，改为复制后通过。原单步与纯SR规则保持。

复用既有MSVC/Ninja构建并发2，修复后产品和相关测试52.3秒构建通过。checks.py四组通过；EffectChain新增7项防护检查，QtQuick117/0/1跳过，i18n缺失/占位符0。sr-stage-safety-ui.py复制用户三个会话文件到私有profile，禁止自动恢复/预热，只开本地4K文件；7项断言通过，原会话SHA不变，NR/SR/VFG真实全开、内部8K/最终4K，日志无8K工作图，出帧约12秒后正常退出。未占用采集卡，也未复测原8K NR/VFG危险组合或声称长时稳定/底层驱动问题根治。

当前EXE在tests/claude-sr-stage-20261028/app原位置已更新；测试注入Main.qml恢复，97个运行依赖与正式供体逐SHA一致。诊断、离线工具和最终回执在reports/sr-stage-freeze-20261028，测试日志在logs同名目录；源文档docs/SR_STAGE_FREEZE_2026-10-28.md。无main合并、push、Release，不改SDK/驱动/模型/真实用户配置。
