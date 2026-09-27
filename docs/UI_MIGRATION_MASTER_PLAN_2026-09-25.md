# Veyra 2.0.0 QML 界面迁移总方案（活动版）
日期：2026-09-27
状态：用户纠偏后重新生效，只允许 UI 迁移。
分支：codex/ui-qml-migration-20260925
起点：main df41580，审计时 HEAD babebbb。

本文件取代 2026-09-25 旧版“QML 新界面 + 引擎改造”施工指令。旧版内容保留在 Git 历史中供追溯，不再是活动任务。审计见 UI_MIGRATION_SCOPE_AUDIT_2026-09-27.md，进度见 UI_MIGRATION_EXECUTION_2026-09-25.md。

## 1. 目标与边界
2.0.0 当前目标只有一件事：把已批准的设计稿迁移为 QML 界面，并让它消费现有产品能力。迁移后，用户可以在 QML 入口中使用已经接入 bridge 的播放、采集、设置、预设、导出和对话框入口；尚未接入的能力必须明确显示为接口缺口，不能用假控件或后端改造掩盖。功能语义继续由现有引擎提供，界面不重新定义处理链。

### 1.1 边界的第一性原理

UI 迁移只改变呈现层和输入适配层。已有的 source、engine、pipeline、sink、media、shader、运行库、时间戳、队列和输出语义都是本轮的外部合同。QML 要的数据先从现有 snapshot/facade/bridge 查找；查不到就显示“未接入/未测”并登记，不得为了填一个控件而增加后端字段或改处理链。任何把 UI 缺口转化为 engine/pipeline 改动的行为都算范围失败，必须停工并另开后端任务。

当前分支已经包含一批历史越界改动，所以本轮 QML 运行结果只能作为“该隔离分支的 UI 入口证据”，不能宣称原链路等价、R5.3 完成或性能无回归。越界内容由 scope baseline 冻结，不回滚、不继续改、不计入 2.0.0 完成度。完整原因和错误证据见 `docs/UI_MIGRATION_SCOPE_AUDIT_2026-09-27.md`。

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

## 2. 已有越界改动
当前分支已包含 GraphDescription、source metadata、EffectChain、Video HDR、Preset Library、NR 多实例、trim-aware export、export queue/rate control 和 R5.3 颜色链等提交。它们不回滚、不继续修改，也不计入 2.0.0 UI 完成度；UI_MIGRATION_SCOPE_BASELINE_2026-09-27.json 记录并冻结其当前状态。

这些改动没有得到新的链路验收。旧 R5.3 性能、delivery、完整 unit 或真实采集结果只能作为历史证据，不能作为 QML 迁移通过证明。

## 3. UI-only 阶段
### U0 审计与隔离
先记录 repo root、当前分支、HEAD、main 指针、`git worktree list --porcelain` 和 dirty paths，再运行 scope guard 并核对 baseline。当前目录是主 checkout 的隔离分支，不得写成 managed linked worktree。guard 只认固定仓库、固定分支 `codex/ui-qml-migration-20260925`、固定 `main=df41580f7fa0d2b26718f355640470e8cb94b324` 和固定 baseline；baseline 与 guard 必须已经纳入 Git 且工作树清洁，不能通过参数替换证据源。任一身份、指针、控制文件状态或冻结 hash 不一致即停，不运行后端 gate。

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
QML staging 只含 veyra_qml_ui.exe；旧 Win32 UI 暂不删除。保存构建、entry、unit、smoke、截图和 scope guard 证据并更新执行文档。用户明确验收后，才另行讨论删除旧 UI、切换默认入口、合并 main、push 或 Release。

## 4. 无人值守硬规则
每轮先运行并保存原始输出：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-migration-scope-guard.ps1

scope guard 失败必须停止并报告路径、hash 和原因。不得修改 guard、baseline、阈值或测试入口来制造通过；guard 和 baseline 自身必须保持 Git tracked + clean，不能用另一份文件绕过。构建、staging、TEMP/TMP、unit、smoke、截图和日志都必须使用本轮唯一的 E:\项目\Veyra 目录；禁止复用旧 staging 或固定 run 目录。不得运行 `scripts/gates/delivery.ps1`、`scripts/perf-ui-migration.ps1`、R5.3 性能/采集/导出 gate 来推进 UI 进度。每个阶段只认当前 run 目录的证据，旧日志不能替代本轮结果。任何阶段发现需要修改冻结路径、缺少真实接口、入口不一致、测试缺失或 fixture 缺失时，状态只能是 `blocked`/`not-run`，无人值守流程必须停在该阶段。

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
