# 2.0.0 QML 迁移范围审计

审计日期：2026-09-27
审计分支：`codex/ui-qml-migration-20260925`
审计 HEAD：`53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`
`main`：`df41580f7fa0d2b26718f355640470e8cb94b324`

## 结论

任务确实跑偏了。原任务是替换 UI，却把“界面需要展示/操作的数据”错误地解释成“必须先改造引擎链路”。这不是 UI 迁移的必要条件，而是范围判断错误。当前没有证据证明所有原链路已经回归通过，因此不能把这些改动说成安全，也不能把 R5.3 结果说成 2.0.0 进度。

本次不做破坏性回滚：先把已经进入分支和工作树的越界内容按当前 hash 冻结，保留证据；从现在开始，2.0.0 只做 QML 页面、QML bridge 和 UI 验收。scope guard 会阻止新的后端路径，也会阻止已存在的越界文件继续变化。

## 隔离事实

- 当前目录是主 checkout，但分支为 `codex/ui-qml-migration-20260925`；它不是 Codex managed linked worktree。
- `git worktree list --porcelain` 显示其他历史 worktree，但当前目录没有 `main` checkout；当前分支与 `main` 仍通过 Git 分支隔离。
- 当前没有 merge、push 或 Release；旧 Win32 UI 没有删除。
- 工作树存在大量未提交改动。它们不会被本审计擅自清理；baseline 会保存状态和 hash。

## 已确认的越界提交

| 提交 | 实际内容 | 为什么越界 |
|---|---|---|
| `1530844` | GraphDescription、EngineController、VideoExportJob | UI 不需要重写图描述和导出路径 |
| `7e2b04d` | source metadata、duration、poster frame、FFmpeg/source 改动 | 这是 source/engine 能力，不是 QML 视图 |
| `3710107` | EffectChain 及单元测试 | 新的链路数据模型，不是 UI 适配 |
| `2aacf37` | Video HDR 在链路中的位置 | 改变处理顺序，属于后端行为 |
| `74415d2` | rebuild decision、export chain、Preset Library | 同时改变 engine、export 和 preset 存储 |
| `8cbf052` | Feature-18 多句柄 probe | NR runtime 研究，不是 UI |
| `5d3a105`、`7a2be4b` | NR 多实例及黑帧修复 | 直接改变 EnhanceGraph 和 NR 历史 |
| `7444a2d`、`babebbb` | trim-aware export、export queue/rate control | 新增导出语义和编码路径 |

这些提交可以被追溯，但从现在起不再作为 2.0.0 UI 任务继续推进。它们的代码和历史提交不代表已经通过链路验收。

## 未提交越界内容

当前 dirty paths 包含 `CMakeLists.txt`、旧 `apps/veyra/ui/AppShell.cpp`、`include/veyra/engine/**`、`include/veyra/pipeline/**`、`src/engine/**`、`src/pipeline/**`、`shaders/**`、颜色/导出/采集/音频测试及 R5.3 脚本；`src/source`、`src/sink`、`src/media` 在当前工作树没有未提交修改，它们属于分支中已经提交的越界差异。另有源码目录下的 `veyra_preset_library_tests/`、`veyra_qml_easing_tests/`、`veyra_qml_quick_tests` 生成残留。

这些路径现在不删除、不再编辑。`UI_MIGRATION_SCOPE_BASELINE_2026-09-27.json` 会记录当前分支差异、工作树状态、HEAD blob 和 SHA-256；后续 hash 变化直接失败。源码目录生成残留属于历史证据，不被当作新的 QML 源码；第五次审计已移除 scope guard 对这三个目录的整体忽略，当前 405 个文件按越界路径冻结，后续新增或变化都会停止无人值守流程。

## 证据错误

此前有一轮“UI”测试实际使用：

- `E:\项目\Veyra\build\ui-qml-migration-20260925`；
- `VEYRA_BUILD_QML_UI=OFF`；
- `veyra.exe`。

这只能证明 legacy Win32 入口的结果，不能证明 QML。该构建目录还同时存在 `veyra_qml_ui.exe`，导致后续 staging/测试有混淆风险。已把构建脚本改为显式 `-UiTarget qml|legacy`、目标级构建，并要求 staging 只含对应入口；后续只认 `veyra_qml_ui.exe` 的独立 QML staging。

此前 unit runner 扫描全部 `veyra_*_tests.exe`，会把后端、采集、导出和历史 fixture 混进 UI 证据；现改为只执行三个 QML 测试，并要求缺失直接失败。旧 Win32、delivery、R5.3 性能和真实采集结果不再计入 UI gate。

## R5.3 处理

R5.3 没有被本审计“攻克”。它仍有历史阻塞和未完成的后端验收，而且它本来就不应是 UI 迁移的前置条件。当前动作是把 R5.3 从 2.0.0 活动队列移出并冻结，不通过改文档把它伪装成完成，也不继续消耗 UI 迁移时间。

## 新的完成口径

2.0.0 UI 只看 U0–U6：scope/隔离、只读接口盘点、正确 QML 构建、QML-only 测试、QML smoke、17 个设计 frame 对照、用户验收前封存。QML 如果缺少后端数据，只登记接口缺口并停在 UI 边界；需要新能力必须另行授权。

每次无人值守执行必须先运行 `scripts/acceptance/ui-migration-scope-guard.ps1`。guard 失败、入口不一致、QML 测试缺失/失败或设计差异未定位时，立即停工。不能修改 guard、baseline、阈值、旧链路或测试选择来制造通过。

## 当前剩余 UI 任务

1. 生成并核对 scope baseline；
2. 只读盘点 QML bridge 与现有接口，登记缺口；
3. 用 `VEYRA_BUILD_QML_UI=ON` 构建并独立 staging；
4. entry contract、三个 QML-only tests、QML smoke；
5. 17 个设计 frame 的截图和交互对照；
6. 整理 E 盘证据，交用户验收。

在用户验收前，不删除旧 UI，不合并，不 push，不发布。

## 二次审计：无人值守验收的防误报修正

首次审计后的脚本仍存在三个会让证据失真的点，已在本轮修正：

1. `qml-ui-smoke.ps1` 原来允许 `legacy` 参数，并在播放样本不存在时直接少跑一个案例。现在参数只允许 `qml`；`test_av_1080p.mp4` 缺失默认直接失败，只有显式 `-AllowMissingPlaybackFixture` 才能生成带 `status=skipped` 的记录，不能把 skipped 算作 playback 通过。
2. `run-unit-ui-migration.ps1` 原来接受 `legacy|qml`，且 `-App` 可以指向另一份目录。现在只允许 `qml`，并要求 `-App`（若提供）与 `-StagingDirectory` 是同一目录；三个测试和 QML entry 必须来自同一 staging，staging 中出现 `veyra.exe` 直接失败。
3. `test-ui-migration-entry-contract.ps1` 原来固定复用输出目录。现在把 `-OutputDirectory` 当作根目录，每次创建带 UTC 时间和随机后缀的唯一 `run-*` 目录，并在 `result.json` 写明 `runDirectory`，旧 evidence 不会成为本次结果。

这些修正仍属于 QML 验收脚本，不触碰冻结后端路径。修正后的脚本必须先通过 PowerShell 解析检查和 scope guard，再允许 U1/U2 继续；脚本或 guard 失败时无人值守流程停止。

## 三次审计：旧默认输入和运行目录再封堵

二次审计后逐文件复查发现仍有两个会绕过合同的默认值：entry contract 可以无参数复用 `qt-probe-20260926`，smoke 可以无参数读取旧 `loop\local\fixed_clips`，并把输出/TEMP 写入固定目录。已再次修正：

1. `test-ui-migration-entry-contract.ps1` 的 `-QmlBuild` 现在是必填参数，必须位于 `E:\项目\Veyra`，并拒绝名称匹配 `qt-probe-*` 的历史 build；输出仍由唯一 UTC+随机 `run-*` 目录承载。
2. `qml-ui-smoke.ps1` 不再有旧 fixture 默认值；未传 `-FixtureRoot` 或文件不存在时默认直接失败，显式 `-AllowMissingPlaybackFixture` 只产生 `skipped` 并以退出码 1 结束。输出和 TEMP 根目录都在 E 盘，每次自动生成唯一 `run-*` 子目录并写入 `result.json`。
3. `build-ui-migration.ps1` 的 TEMP 也必须为空；`stage-ui-migration.ps1` 的 `-Release` 改为必填且必须位于 E 盘，避免把旧发布包的运行依赖静默带入当前证据。

本次修改后已完成 PowerShell AST 解析检查和 `git diff --check`；scope guard 必须在文档和脚本修改完成后重新执行。U2–U6 仍未执行，不能写成通过。

## 第一性原理复核：为什么换 UI 不应改变链路

UI 的职责是把已有状态呈现出来，把用户操作转换成已有命令；处理链的职责是解码、增强、调度、编码和输出。两者之间的边界应该是已有 snapshot/facade/bridge 合同。只要旧 UI 已经能完成一项处理，替换 UI 就不需要改变该项处理的图描述、资源生命周期、颜色顺序、时间戳、队列、运行库或输出编码。

“新界面需要展示更多数据”也不能推导出“先改引擎”。正确顺序是：先查已有 snapshot 和 facade；已有字段就接到 QML；没有字段就显示未知或未接入并登记缺口。只有用户另行要求新增能力时，才建立独立后端计划和独立验收。把 GraphDescription、EffectChain、HDR 排序、NR 多实例、trim/export queue 或颜色 shader 当作 UI 前置条件，实际上改变了产品语义和性能变量，导致无法证明问题来自 UI 还是链路。这正是本轮跑偏的根因。

## 审计后的风险结论

1. `codex/ui-qml-migration-20260925` 与 `main=df41580` 仍然隔离，当前目录是该分支的主 checkout，不是 managed linked worktree；没有 merge、push、Release，旧 Win32 UI 也未删除。因此主线没有被本轮改动直接污染，但“隔离”不等于“内容正确”。
2. 分支中已经提交的越界代码和当前 dirty 的越界路径共同构成一个污染的研发状态。它们保留用于追溯和后续另行处理，不代表原链路仍然被证明无变化，也不代表 R5.3 完成。scope baseline 只冻结 hash，不能把这些改动变成通过证据。
3. 在当前分支上构建 QML，只能证明这个分支的 QML 入口能否运行；不能证明后端链路等价于 `main` 的原链路。后续 UI 报告必须把“QML UI 证据”和“后端链路验收”分开，禁止用 UI smoke、旧 delivery 或提交 FPS 代替后端验收。
4. `docs/CURRENT_STATUS.md`、`docs/QT_QML_UI_MIGRATION_RESEARCH_PLAN_2026-09-24.md` 和旧交接文档包含旧的“引擎改造优先”语句。它们目前作为历史资料保留；活动施工只认本审计、活动总方案、恢复手册和执行记录。任何自动化任务读到旧路线都必须停止并回到活动方案。

## 已发现的证据错误及修正

| 错误 | 为什么不成立 | 当前硬修正 |
|---|---|---|
| `VEYRA_BUILD_QML_UI=OFF` + `veyra.exe` 被记作 QML 通过 | 这是 legacy Win32 入口，不能证明 QML | 只接受 `VEYRA_BUILD_QML_UI=ON`、`veyra_qml_ui.exe` |
| QML 和 legacy 可出现在同一个 staging | 入口、DLL、QML 资源和日志会互相污染 | staging 必须只含一个 QML UI executable；entry resolver 拒绝 `veyra.exe` |
| unit runner 扫描全部 `veyra_*_tests.exe` | 后端、采集、导出和历史测试被混入 UI 结果 | 只运行同一 staging 的三个 QML 测试 |
| 播放 fixture 缺失时 smoke 少跑一例仍算通过 | 缺失输入不是通过；它隐藏了 U4 未执行 | 默认失败；显式 skipped 也必须让 gate 失败 |
| 固定目录复用旧 entry/unit/smoke 结果 | 旧日志和新日志无法区分，可能造成污染 | 每轮使用唯一 run 目录，并记录 branch/commit/executable hash |
| 旧 R5.3 delivery/perf/采集结果被当作 UI 进度 | 它们测试的是后端和 legacy，不是 QML UI | 全部降级为历史证据，移出 2.0.0 队列 |

## 2.0.0 无人值守硬合同

每一轮必须按以下顺序执行，任何一步失败都停止，不猜测、不自动绕过：

1. 记录 repo root、branch、HEAD、`main`、`git worktree list --porcelain` 和完整 dirty path；当前目录必须仍是目标分支的主 checkout，身份变化立即停止。
2. 运行 `ui-migration-scope-guard.ps1`。冻结 hash 变化、新增后端路径、baseline 缺失或 main 移动均是硬失败；不得改 guard/baseline/阈值来制造通过。
3. 用本轮唯一的 E 盘 build 目录构建 QML executable 和三个 QML-only 测试；构建目录不得复用旧 CMake cache。构建失败就记录阻塞，不修改 CMake 或后端绕过。
4. 用本轮唯一的 staging 目录执行 entry contract、unit 和 smoke。所有结果必须来自同一 staging、同一 executable hash、同一 branch/commit；旧目录只可作对照，不能进入当前结果。
5. fixture 缺失、测试缺失、skipped、超时、QML error、窗口层级不确定或 bridge 缺口，都只能标记 `blocked`/`not-run`。不能把它们改写为“诊断通过”。
6. U4 通过后才做 U5 的 17 个设计 frame 对照。截图必须逐帧记录状态和差异，不以整体观感通过；U5 发现后端缺口时停在 UI 边界。

## 冻结对象与停止动作

- 冻结的 engine/source/pipeline/sink/media/gfx/ngx/shader/CMake/旧 Win32 路径不得继续编辑；当前已有内容不回滚、不重排、不补丁式修复。
- QML 页面只能调用已存在的 bridge/facade。需要新字段、新命令、新处理顺序、新运行库或新输出能力时，记录接口缺口，停止该交互点，另开后端计划和分支并重新取得授权。
- 禁止为 UI 迁移运行 `scripts/gates/delivery.ps1`、`scripts/perf-ui-migration.ps1`、R5.3 性能/采集/导出 gate；禁止用它们的结果推进 U2–U6。
- 只有 U0–U6 的当前证据齐全并经用户验收后，才可讨论删除旧 UI、切默认入口、合并、push 或 Release；这些动作不属于无人值守授权。

## R5.3 最终口径

R5.3 **未攻克、未提交、未打标签，且不再属于 2.0.0 UI 迁移队列**。已通过的 TrueHDR/独立 LUT、部分软件短测、1080p120 与 720p60 真实采集只能作为窄范围历史证据。固定性能四轮未过，4K 输入仍有 0 帧失败，Media Foundation 实卡导出未完成，高负载音频问题没有形成最终解释，完整后端门槛未收口。任何后续文档都不得把 R5.3 写成完成、前置条件或 QML 进度。

## 四次审计：控制证据源，防止无人值守换根目录

本次重新审查发现，上一版 scope guard 虽然会冻结越界路径 hash，但仍接受 `-Root` 和 `-Baseline` 参数；无人值守调用如果传入另一份 checkout 或另一份 baseline，可能绕过当前冻结合同。baseline 和 guard 也都是未提交文件，不能把它们自身的变化当作可靠控制点。

已将 `scripts/acceptance/ui-migration-scope-guard.ps1` 改为：

- 固定仓库根目录 `C:\Users\123\Desktop\Veyra DLSS Video Player`、分支 `codex/ui-qml-migration-20260925`、`main=df41580f7fa0d2b26718f355640470e8cb94b324`、baseline 路径和 guard 自身路径；调用参数不能替换这些身份；
- baseline 记录 guard 的 SHA-256；验证时检查 guard hash、baseline schema/路径/分支/main 均未漂移；
- baseline 与 guard 必须已经 tracked 且 Git 工作树 clean。两份控制文件任一未纳入 Git 或被修改，直接停止，不能继续构建或测试；
- 继续冻结已有 engine/source/pipeline/sink/media/shader/CMake/旧 Win32 dirty 内容，新增越界路径和冻结 hash 变化仍直接失败。

这项加固已通过本地控制提交 `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d` 完成；该提交不等于 merge/push/release。重新验证后 U0 已通过，U2–U6 仍不得跳阶段执行。

## 五次审计：冻结历史构建残留并增加阶段复查

在准备控制文件时发现，scope guard 曾整体忽略源码根目录的 `veyra_preset_library_tests/`、`veyra_qml_easing_tests/` 和 `veyra_qml_quick_tests`。这三个目录共有 405 个未跟踪文件，虽然是历史构建残留，却仍可能在无人值守期间被覆盖或新增；忽略它们会让冻结合同失真。现已移除该忽略，重新生成的 baseline 记录 582 个初始变更路径，其中 474 个越界路径被冻结，残留文件按各自 SHA-256 处理，不删除也不当作 QML 源码。

方案同步增加阶段间复查：U0 后、U2 构建前后、entry contract 后、U3 结束后、U4 smoke 结束后都必须重新运行固定 root/branch/main 的 scope guard。阶段目录仍全部放在 `E:\项目\Veyra`，但构建和测试本身可能触发源码内生成物或脚本改动；任何冻结 hash、控制文件、分支或 main 指针变化都要在下一阶段前停止。这样旧的“开头检查一次、后面一路跑完”不能再把中途污染带进结果。

控制文件的本地提交链为 `3827a51`、`07a726b`、`094e5b4`、`2093a20`、`8187b73`、`53a2d31`；baseline 与 guard 已 tracked + clean。最终 U0 guard 结果写入 `E:\项目\Veyra\logs\ui-qml-migration-20260927\scope-guard\u0-final.json`：`status=pass`、`currentChanged=582`、`frozenChecked=474`、`failures=[]`。U1 只读 bridge 审计也已通过；U2–U6 仍未执行。

## 当前阶段状态（收口后的唯一活动状态）

- U0：**通过**。控制文件、分支、main 指针和冻结路径均已复核。
- U1：**通过**。bridge/facade 只读盘点完成，缺口已登记；没有为 UI 修改后端链路。
- U2–U6：**未执行**。下一轮只能从 U2 开始，必须创建新的 E 盘 build/staging/log/TEMP 并按阶段复查 guard。
- R5.3：**未攻克**，已经移出 2.0.0 UI 队列；任何 R5.3、delivery、性能、采集或导出结果都不能推进 U2–U6。
