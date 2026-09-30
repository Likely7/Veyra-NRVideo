# Veyra QML 界面恢复子方案（历史范围保留）
日期：2026-09-27
状态：完整 2.0.0 路线图已由 [完整升级与恢复总方案](UI_V2_0_0_MASTER_PLAN_2026-09-27.md) 接管。本文件保留 UI 止损阶段的范围、事实与缺口，不再定义全部 2.0.0。
分支：codex/ui-qml-migration-20260925
起点：main `df41580f7fa0d2b26718f355640470e8cb94b324`；scope baseline 的初始快照为 `babebbb24ab1eaaf677d78c17ed3d63b8b9fbaec`；当前控制提交为 `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`。

2026-09-27 最新需求已重新确认完整 UI 重做与新增能力，原 R5.3 必须优先收口，不能以 UI-only 代替完整交付。统一执行顺序和需求台账见新总方案；进度仍只记在 `UI_MIGRATION_EXECUTION_2026-09-25.md`。旧 S/R/U 队列均不得直接自动重启。

**当前施工边界仍未解冻：** 本轮仅写方案；AGENTS、scope guard 和 baseline 原样保留。在取得代码开工确认并完成新方案 P0 的显式控制合同迁移前，后端冻结继续执行。以下 UI-only 条目是该保护阶段的记录，不是永久取消 2.0.0 新功能。

当前阶段状态（2026-09-27 恢复性修复）：U0/U1 保留已有证据；本轮 U2 **通过**（全新 QML 构建与真实 staging），U3 **通过**（入口合同 7/7、三个 QML 测试程序通过、Quick 用例 9/9）。U4 **仅自动 smoke 8/8 通过**，完整人工交互验收未完成；U5/U6 未执行。证据见 `docs/UI_MIGRATION_REPAIR_2026-09-27.md`。本轮到此报告结束，不自动推进剩余阶段或恢复旧无人值守 Goal。

## 1. 目标与边界
当前可执行的 UI 迁移子目标是：把已批准的设计稿迁移为 QML 界面，并让它消费现有产品能力。它不是完整 2.0.0 功能升级清单；用户本轮再次确认升级仍包含新增和改造功能，需求保留在第 2.1 节，不因暂停后端施工而取消。迁移后，用户可以在 QML 入口中使用已经接入 bridge 的播放、采集、设置、预设、导出和对话框入口；尚未接入的能力必须明确显示为接口缺口，不能用假控件或后端改造掩盖。功能语义继续由现有引擎提供，界面不重新定义处理链。

### 1.1 边界的第一性原理

UI 迁移只改变呈现层和输入适配层。已有的 source、engine、pipeline、sink、media、shader、运行库、时间戳、队列和输出语义都是本轮的外部合同。QML 要的数据先从现有 snapshot/facade/bridge 查找；查不到就显示“未接入/未测”并登记，不得为了填一个控件而增加后端字段或改处理链。任何把 UI 缺口转化为 engine/pipeline 改动的行为都算范围失败，必须停工并另开后端任务。

当前分支已经包含一批历史后端改动，所以本轮 QML 运行结果只能作为“该分支的 UI 入口证据”，不能宣称原链路等价、R5.3 完成或性能无回归。这些内容由 scope baseline 冻结，不回滚、不继续改、不计入 UI 完成度。历史授权须按发生时的用户指令判断，不能用 2026-09-27 收紧后的边界追溯认定所有 09-25 后端改动均未经授权。范围审计见 `docs/UI_MIGRATION_SCOPE_AUDIT_2026-09-27.md`。

允许修改：
- qml/**
- apps/veyra-qml/**
- QML 适配层：src/ui/Qml*.cpp、include/veyra/ui/Qml*.h、ThumbnailProvider.*
- tests/qml/**、tests/unit/Qml*、tools/qt_probe/**
- 已批准设计稿 prototypes/ui-redesign-2026-09-25/**
- QML 迁移专用构建、测试、验收脚本
- docs/UI*、docs/WORKLOG.md

禁止修改：
- src/engine、src/pipeline、src/source、src/sink、src/media、src/gfx、src/ngx、shaders
- include/veyra/engine、include/veyra/pipeline、include/veyra/source、include/veyra/sink、include/veyra/media
- CMakeLists.txt、旧 Win32 apps/veyra/ui/AppShell.cpp
- 导出、采集、解码、音频、补帧、NR、颜色处理链的新功能或重构
- R5.3 多实例调色、R5.4、R5.5，以及任何新的后端能力

若 QML 页面发现现有接口缺数据，先记录“接口缺口”并停止该点；不得为让页面通过而修改引擎。后端能力必须另开分支、另取授权、另做验收。

## 2. 已有后端改动与冻结范围
当前分支已包含 GraphDescription、source metadata、EffectChain、Video HDR、Preset Library、NR 多实例、trim-aware export、export queue/rate control 和 R5.3 颜色链等提交。它们不回滚、不继续修改，也不计入 2.0.0 UI 完成度；UI_MIGRATION_SCOPE_BASELINE_2026-09-27.json 记录并冻结其当前状态。

这些改动没有得到新的链路验收。旧 R5.3 性能、delivery、完整 unit 或真实采集结果只能作为历史证据，不能作为 QML 迁移通过证明。

### 2.1 2.0.0 原定新增/改造需求台账（保留需求，不恢复旧自动执行队列）

来源：Git `5f307fc:docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md` 第 0、S2、S4、6 节，以及本轮用户对功能升级范围的再次确认。旧 UI-only 纠偏文档将后端施工移出活动队列，但不能把这一动作等同于撤销原定产品需求。下表是需求与缺口追踪，不是新后端开工授权或完成证明。

| 原定需求 | 已有证据/缺口 | 后续验收边界 |
|---|---|---|
| NR 叠层、实例独立、顺序调整、防闪烁修复 | 分支有历史 NR 多实例修改，当前冻结；防闪烁和完整升级效果不能据此判定完成 | 单/多层正确性、运行时兼容、历史 reset、低延迟与旧路径回归，单独验收 |
| 节点编辑与真实执行，列表/节点预设分开保存 | 已有页面和部分预设能力；节点预设导出明确尚未接入 | 不以画布/保存通过替代执行与导出通过；补帧必须最后，HDR 后只接补帧 |
| 独立多实例调色/LUT、调色按组还原 | R5.3 有实现与部分窄验证，整体未验收；不能写成已解决 | 单一前置调色保持原融合路径；多实例正确性与性能分别过门槛 |
| 自定义导出分辨率、剪辑范围、多文件队列、CBR/VBR/恒定质量、预设导出与剩余时间 | 历史有部分后端实现，不等于整组完成；节点预设导出缺口，MF 实卡验收未完成 | 逐项核对分辨率、时长、音画同步、码率控制、取消/失败恢复；NVENC 与 MF 分开记录 |
| 字幕功能及无/细/中/粗四档描边 | 当前 bridge 审计记录字幕相关接口缺口 | 不能以禁用控件或静态窗口作为功能完成 |
| 音频输出设备选择、立体声下混 | 当前缺 bridge 接口，跟随系统默认设备 | 设备切换、下混与同步须有真实证据，不改完 UI 就计完成 |
| 屏幕捕获缩略图 | 原方案明确要求，本轮未完成专项验收 | 缩略图必须关联真实窗口/目标，不能用占位图通过 |
| 设置页、播放位置记忆、默认解码/截图目录、统一预设及管理 | 已有部分 UI/持久化能力；完整交互仍待 U4，不按整组标记完成 | 保存、恢复、旧配置迁移逐项验证；采集会话恢复仍是已知缺口 |

保留原方案最终决定：只显示提交 FPS，不另做物理显示 FPS 功能；不能把先前的 displayFps 占位字段误列为必须新增的需求。旧 UI 删除、合并、push、Release 的授权限制不变。

**R5.3 当前状态：未解决、冻结，不能以本轮 QML 自动测试通过替代。** `docs/WORKLOG.md` 2026-09-27 的历史记录显示：TrueHDR 与六独立 LUT 窄测试已有通过，1080p120 NV12/720p60 MJPEG 采集短测已有通过；但固定性能四轮均未过，全量 unit 仍有失败/跳过，4K MJPEG 短测 frames=0/exit=1，MF 实卡导出等验收未收口。这是历史证据摘要，本轮没有重新跑 R5.3，也不据软件计数推断屏幕端到端延迟或给所有失败归因。

后续必须将 UI 迁移与功能升级分开排期。功能升级开工前逐项明确现有实现、最小改动白名单、依赖、隔离/回退点和验收命令；需要改冻结后端的项目仍停在当前边界，另行取得明确施工授权。禁止自动修改 guard/baseline、降低阈值或复活旧 R5.3 无限重试。上述逐项功能施工方案尚未完成，不能把需求台账声称为完整可执行的升级计划。

## 3. UI-only 阶段
### U0 审计与隔离
先记录 repo root、当前分支、HEAD、main 指针、`git worktree list --porcelain` 和完整 dirty paths，再运行 scope guard 并核对 baseline。当前目录是主 checkout 的隔离分支，不得写成 managed linked worktree；“分支隔离成立”不等于“工作树干净”或“后端链路正确”。guard 只认固定仓库、固定分支 `codex/ui-qml-migration-20260925`、固定 `main=df41580f7fa0d2b26718f355640470e8cb94b324` 和固定 baseline；baseline 与 guard 必须已经纳入 Git 且工作树清洁，不能通过参数替换证据源。当前最终 baseline 记录 582 条变更、冻结 474 条越界路径。任一身份、指针、控制文件状态或冻结 hash 不一致即停，不运行后端 gate。

### U1 接口只读盘点
列出 QML 页面使用的 bridge/facade 方法和状态字段，标出缺口，产物固定为 `docs/UI_MIGRATION_BRIDGE_AUDIT_2026-09-27.md`。只补 QML 适配层的信号、序列化、线程和生命周期问题，不改变 engine/pipeline 行为；如果页面缺少后端字段，U1 记录缺口并停止该交互点。

### U2 QML 正确构建
使用现有 CMake 配置，以 `VEYRA_BUILD_QML_UI=ON` 同时构建 `veyra_qml_ui`、`veyra_qml_data_tests`、`veyra_qml_easing_tests` 和 `veyra_qml_quick_tests`。每轮必须显式提供全新的 E:\项目\Veyra build、日志和 TEMP 目录；目录已有 CMake cache 或任何文件时不得当作新一轮。构建前后各重新运行一次 scope guard；任一冻结 hash、分支、main 或控制文件状态变化都立即停止。构建前后不得修改 `CMakeLists.txt` 或任何冻结后端路径；配置/编译失败就记录阻塞并停止。legacy `veyra.exe` 即使存在，也不得进入 QML staging。`build-ui-migration.ps1` 只接受 `-UiTarget qml`，并拒绝复用非空 build/TEMP/log；stage 脚本的 `-Build`、`-App`、`-Release` 均必须显式提供，且三个路径都在 E 盘。

### U3 QML-only 自动测试
先运行 entry contract，并把结果写入本轮唯一的 run 目录；entry contract 后重新运行 scope guard；随后只从同一个 QML staging 执行 `veyra_qml_data_tests.exe`、`veyra_qml_easing_tests.exe`、`veyra_qml_quick_tests.exe`。三个测试必须存在于该 staging，且 entry JSON 中的 executable hash、branch、commit、QML cache 开关必须与当前运行一致。缺少测试、超时或失败均阻止下一步；不得扫描或执行全部 `veyra_*_tests.exe`，不得从另一份 staging 借测试通过。U3 结束后再次运行 scope guard。

### U4 QML smoke 与交互
通过 `qml-ui-smoke.ps1` 验证 home、minimal、professional、node、export、settings、capture dialog、退出和可用的文件打开路径。脚本只能接受 QML 入口；`FixtureRoot` 不再有旧路径默认值，未显式提供或文件不存在时默认失败，只有显式 `-AllowMissingPlaybackFixture` 才能记录为 skipped，且 skipped 仍使进程退出 1。U3 结束后、smoke 开始前和结束后都重新运行 scope guard；任一次失败立即停止。输出和 TEMP 每轮自动创建带 UTC+随机后缀的空 run 目录。交互验证单独记录页面切换、菜单/弹窗、拖放、播放控制、设置保存、全屏和 DPI；只使用 QML 程序实际支持的命令行参数，保存 stdout/stderr/QML 日志。

### U5 设计一致性
只在 U4 运行合同通过后，逐一对照 `prototypes/ui-redesign-2026-09-25/` 的 17 个 frame，验证页面结构、窗口层级、比例、静态布局、动效、菜单、弹窗、全屏和 DPI 行为。每张 frame 单独记录截图、状态（pass/fail/not-run）和差异元素；差异必须定位到 frame/元素，不以“看起来差不多”通过。U5 不得反向要求修改 engine/pipeline；设计稿与已有 bridge 冲突时记录接口/设计缺口并停在 UI 边界。

### U6 用户验收前封存
验收 staging 的唯一 **UI 入口**是 `veyra_qml_ui.exe`，同时必须保留 U3 所需的 `veyra_qml_data_tests.exe`、`veyra_qml_easing_tests.exe` 和 `veyra_qml_quick_tests.exe`；并非整个目录只能有一个 exe。Qt 插件、QML 模块、依赖 DLL 与已批准运行组件仍须齐全。不得混入 `veyra.exe`、后端测试或 probe；旧 Win32 源码暂不删除。保存构建、entry、unit、smoke、截图和 scope guard 证据并更新执行文档。用户明确验收后，才另行讨论删除旧 UI、切换默认入口、合并 main、push 或 Release。

## 4. 无人值守硬规则

### 本轮恢复性修复的收口条件（2026-09-27）
- 当前授权是修复已证实的问题，不是自动执行 U2–U6 全部阶段或恢复旧 Goal。本轮完成针对性修复及验证后必须给出报告并结束。
- 审计发现必须指向具体文件、行为或失败证据；不得把“再审一次”当作无限新增待办，也不得为了 guard 通过重写 baseline/guard、重记 hash 或创建控制提交。
- 同一失败只有在提出新的、可验证的原因并做出最小修改后才可重试；两次修复仍失败则记录阻塞并停止，禁止重复原命令消耗额度。单项测试仍最多 300 秒。
- 后端冻结继续有效。若真实构建失败定位到冻结路径，保留日志并报告；不以笼统的“修好/恢复”指令自行回退整批后端改动。
- 本节是执行约束，不是外部计费熔断器；没有配置服务端限额之前，不宣称已经防止超额消费，不自动启动新一轮无人值守。

每轮先运行并保存原始输出：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-migration-scope-guard.ps1

scope guard 失败必须停止并报告路径、hash 和原因。不得修改 guard、baseline、阈值或测试入口来制造通过；guard 和 baseline 自身必须保持 Git tracked + clean，不能用另一份文件绕过。每个阶段只认当前 run 目录的 JSON 和日志；旧日志不能替代本轮结果，脚本退出码为 0 但结果 JSON 缺失、字段不匹配或包含 `skipped` 时也不能推进。构建、staging、TEMP/TMP、unit、smoke、截图和日志都必须使用本轮唯一的 E:\项目\Veyra 目录；禁止复用旧 staging 或固定 run 目录。不得运行 `scripts/gates/delivery.ps1`、`scripts/perf-ui-migration.ps1`、R5.3 性能/采集/导出 gate 来推进 UI 进度。任何阶段发现需要修改冻结路径、缺少真实接口、入口不一致、测试缺失或 fixture 缺失时，状态只能是 `blocked`/`not-run`，无人值守流程必须停在该阶段。

### 4.1 推荐的单轮目录合同

令 `<run>` 为 UTC 时间加随机后缀，并在 WORKLOG 中记录完整路径：

```text
E:\项目\Veyra\build\ui-qml-migration-20260927\<run>
E:\项目\Veyra\tests\ui-qml-migration-20260927\<run>\app
E:\项目\Veyra\logs\ui-qml-migration-20260927\<run>
E:\项目\Veyra\tmp\ui-qml-migration-20260927\<run>
```

U2–U4 的 `build`, `staging`, `unit`, `smoke` 和 entry contract 必须互相引用这一组目录；entry contract 的 `-QmlBuild` 必须显式指向本轮 build，且拒绝 `qt-probe-*`；任何参数仍指向 `app\veyra.exe`、旧 `qml-app`、旧固定 staging 或历史 `goal\r5.3` 时，视为入口错误并停工。smoke 实际证据目录会在其 output/TEMP 根下再生成唯一 `run-*` 子目录，结果 JSON 必须记录实际目录。

## 5. 完成定义
仅当以下条件全部满足，才能把 2.0.0 QML UI migration 标为完成：
1. U0–U6 当前证据齐全且产物在 E:\项目\Veyra；
2. scope guard 通过，冻结后端路径没有变化；
3. QML 构建、QML-only 测试、entry contract、smoke 和设计一致性验收通过；
4. 未把后端新功能、旧 Win32 测试或提交 FPS 当作 UI 通过依据；
5. 用户明确验收后才执行任何删除、合并、推送或发布。
