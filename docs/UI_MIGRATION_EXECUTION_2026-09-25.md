# Veyra 2.0.0 统一执行记录
更新时间：2026-09-27（已补核三段 Codex 后续决策；17 屏/42 动效验收合同补齐；本轮仅文档）
当前分支：codex/ui-qml-migration-20260925
当前 HEAD：53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d（scope guard 控制提交）
baseline 初始快照：babebbb24ab1eaaf677d78c17ed3d63b8b9fbaec
main：df41580f7fa0d2b26718f355640470e8cb94b324
合并 / push / Release：均未发生。

统一需求路线图：[2.0.0 完整升级与恢复总方案](UI_V2_0_0_MASTER_PLAN_2026-09-27.md)。已对照 Claude 原始用户指令、三段后续 Codex 真实用户消息、批准原型及固定 Git 修订。它不是新的施工授权：当前仍遵守 AGENTS 的 UI-only 合同，后端另行授权、另开分支、另做验收。旧 S/G/B/R/U 编号只供追溯，不另开并行队列，不重启旧 Goal。

### 本次补核结论
- Codex `01a0dbab…`、`01a0dd72…`、`01a0de1a…` 的交接/纠偏已进入总方案 C01–C06；`36dd851 / 8a1be6a / 7444a2d / babebbb` 的方案及局部成果/未测边界保留。历史 Goal 自动续跑文本不计新授权。
- 设计仍在：16 个核心文件相对 `5f307fc` 仅 CRLF/LF 差异；一个原有截图 helper 后来修改、两个后来新增，不冒称全部字节未变。本轮未渲染，外部字体加载未验证。
- P5 拆成设计真源/偏差许可、17 屏映射、42 项完整动效、CUI01–CUI05 延期缺陷及退出条件。字体替代、纯色冒充模糊、播放条移位不能默认为已获批准。
- 主线优先、1.4.4 回退和旧 UI 保护不变；所有 17 屏、42 动效及延期缺陷均仍待完整验收。

## 完整 2.0.0 当前进度
| 阶段 | 本次确认状态 | 下一步/放行条件 |
|---|---|---|
| P0 安全接管 | 文档审计基线已建立；未获恢复后端施工授权 | 后端须明确新授权、分支策略和按切片的保护合同 |
| P1 原 R5.3 | 未解决；旧实现有局部证据，尚未归因收口 | P0 后立即核对候选身份/fixture，复现首个明确缺陷 |
| P2 NR/原链 | 有实现及历史修复，未全面验收 | 1–4 层、旧默认/低延迟、时域历史/防闪和保护验证 |
| P3 R5.4/R5.5 | 未验收 | 固定顺序等价先于排序，节点必须真实执行 |
| P4 全部功能 | 部分实现、部分桥接及后端缺口 | 按 F01–F24 和垂直切片补齐，不以灰按钮交付 |
| P5 设计与交互 | 17 屏、42 动效、CUI01–CUI05 均未完整验收 | 按总方案 P5.1–P5.5 逐屏/逐动效/真实播放核对，未批准偏差不得放行 |
| P6 总回归 | 未执行 | 同一候选版本全产品回归，失败/跳过不得计为通过 |
| P7 候选包 | 未执行 | 干净环境、依赖/许可、配置迁移和回退证据 |
| P8 最终交付 | 未授权、未执行 | 旧 UI 删除、merge、push、Release 分别确认后再做 |

恢复完整升级的下一道门为 **P0 的显式授权 → P1.1**，不是本轮立即开跑 R5.3。当前可执行范围仍是原 UI-only 合同；接口缺口记录并停止，不铺假控件，不复跑全量性能矩阵。本轮只修订方案，AGENTS/guard/baseline 和现存源码保持。此前 QML 构建与 smoke 通过仍是真实局部成果，不等于 R5.3、设计动效或完整 2.0.0 通过。

## 2026-09-27 早前 UI 止损合同与证据（历史记录，P0 前的安全锁仍有效）
- 当前工作区事实已经核对：主 checkout 切在 `codex/ui-qml-migration-20260925`，与 `main=df41580` 分支隔离，但不是 managed linked worktree；未 merge、未 push、未 Release，旧 Win32 未删除。
- 之前使用 `VEYRA_BUILD_QML_UI=OFF` + `veyra.exe` 的结果全部降级为 legacy 历史证据；QML 只接受 `VEYRA_BUILD_QML_UI=ON`、`veyra_qml_ui.exe`、单独 staging 和匹配 `qml/Veyra/qmldir`。
- 验收脚本已收紧：`qml-ui-smoke.ps1` 仅允许 qml，`FixtureRoot` 不再默认指向旧 fixture，播放 fixture 缺失默认失败；输出/TEMP 每次在 E 盘根下生成唯一 `run-*` 目录；`run-unit-ui-migration.ps1` 仅允许从同一 QML staging 执行三个 QML 测试；entry contract 的 `-QmlBuild` 必须显式提供且拒绝 `qt-probe-*`，每次使用唯一 run 目录；stage 的 `-Build`、`-App`、`-Release` 都必须显式提供。
- U4（运行/交互）与 U5（17 个 frame 设计对照）是两个独立 gate。任何 bridge 缺口只登记，不修改 engine/pipeline/source/sink/media/gfx/ngx/shaders/CMake。
- 第五次审计补上了 scope guard 的历史残留盲区：`veyra_preset_library_tests/`、`veyra_qml_easing_tests/`、`veyra_qml_quick_tests` 不再被整体忽略，当前 405 个残留文件随 baseline 冻结。build 后、entry/U3 后、smoke 后均必须重新运行 guard，阶段切换不能复用早期 guard 结果。
- 本条之后的无人值守轮次必须先运行 scope guard；scope guard、入口合同、QML-only 测试任一失败即停。R5.3/R5.4/R5.5、delivery、性能、采集矩阵和导出 gate 不属于本执行记录的活动队列。

## 当前事实
| 项目 | 事实 | 结论 |
|---|---|---|
| Git 隔离 | 当前目录在独立分支，main 未合并 | 分支隔离仍成立 |
| Worktree | 当前是主 checkout 切到隔离分支，不是 Codex managed linked worktree | 不应描述成 linked worktree |
| 已提交范围 | 1530844 至 babebbb 之间包含 engine/source/pipeline/NR/export/color 改动 | 历史后端提交冻结，不计 UI 完成；当时授权须单独判断 |
| 未提交范围 | CMake、旧 AppShell、engine/pipeline/shader、R5.3 脚本和测试仍有 dirty paths | baseline 后 hash 冻结 |
| 误测入口 | 旧证据使用 E:\项目\Veyra\build\ui-qml-migration-20260925、VEYRA_BUILD_QML_UI=OFF、veyra.exe | 不能作为 QML 证据 |
| 正确入口 | QML 使用 VEYRA_BUILD_QML_UI=ON、veyra_qml_ui.exe 和独立 staging | 后续 QML gate 只认该入口 |
| R5.3 | 有历史实现和局部通过，但性能等验收未收口；本轮仅修复 QML 构建/测试，未修复或重验 R5.3 | 未解决、冻结；不能计为已完成 |

详细越界清单见 UI_MIGRATION_SCOPE_AUDIT_2026-09-27.md。

## 已完成的 UI 侧基础
- QML 页面、组件、Qt entry 和 bridge 已存在于当前分支；
- 构建脚本已改为显式区分 qml / legacy，构建时锁定目标 executable；
- staging 脚本要求单一 UI executable，避免旧 veyra.exe 混入 QML app；
- unit 脚本只寻找三个 QML 测试，不再扫描后端全量测试；
- entry resolver / contract / QML smoke 已加入入口一致性检查。

代码状态本身不等于 gate 通过。本轮已重新构建并执行入口、QML 单测及自动 smoke，结果见下表及 `docs/UI_MIGRATION_REPAIR_2026-09-27.md`；不替代完整交互、视觉或后端验收。

## 当前待办（只允许这些）
- [x] U0：已完成控制文件提交和最终 scope guard。旧结果 `status=pass`、`currentChanged=178`、`frozenChecked=71` 仅作历史记录；当前 baseline 为 `initialChanged=582`、`frozen=474`，包含三个源码根目录残留共 405 个文件。guard 固定 root/branch/main/baseline，baseline 与 guard 均 tracked + clean；最终结果为 `status=pass`、`currentChanged=582`、`frozenChecked=474`、`failures=[]`，证据见 `E:\项目\Veyra\logs\ui-qml-migration-20260927\scope-guard\u0-final.json`。
- [x] U1：只读盘点 QML bridge 对现有 engine API 的消费，记录接口缺口；见 `docs/UI_MIGRATION_BRIDGE_AUDIT_2026-09-27.md`。没有修改冻结链路。
- [x] U2：全新 `qml-fixed` 构建完成 387/387，真实 `app-fixed` staging 含 QML UI、三个测试程序、offscreen 插件和 QtTest 模块；未改 CMake 或冻结后端源码。
- [x] U3：入口合同 7/7、三个 QML-only 测试程序 3/3 通过；Quick 内部用例 9/9，零失败、零跳过。最终结果为 `unit-final/summary.json`。
- [ ] U4：自动 smoke 8/8 通过，含文件播放和采集对话框入口；完整人工交互、窗口层级、DPI、全屏与视觉/字体验收尚未完成，不标记整个 U4 通过。
- [ ] U5：17 个设计 frame 的截图/交互对照；
- [ ] U6：整理证据并交给用户验收。

### 本轮恢复性修复收口
- Run：`repair-overnight-20260927-035400`；证据根目录：`E:/项目/Veyra/logs/repair-overnight-20260927-035400`。
- 已修复实际复现的 QString 类型误用、脚本参数传递、scratch 目录合同冲突及 Qt 测试部署缺项；构建/测试目标和超时限制收紧，QML 缓存写入本轮 E 盘目录。
- 各阶段 scope guard 通过；独立前后 hash 对比为 `checked=474, changed=[]`，其中 405 项是既有生成残留，并非 474 个后端源码文件。文档收口后的结果以 `guard-final.json` 为准。
- 本轮没有提交、改 guard/baseline、回滚冻结后端、合并 main、推送或发布；未恢复无人值守 Goal。剩余 U4/U5/U6 不在本轮自动推进范围。
- 09-25 曾有更广的引擎改造授权，不能仅按 09-27 的 UI-only 合同把全部历史后端改动追溯认定为未经授权。

## 明确移出本目标
这里的“移出”只指移出当前 UI 执行子任务，不代表取消原定 2.0.0 功能升级。需求与逐项缺口已补回总方案第 2.1 节；独立功能施工计划尚未完成，冻结后端仍不自动开工。
R5.3 多实例调色、R5.4/R5.5、EffectChain/GraphDescription 新能力、NR 多实例、颜色 shader、导出队列、trim、采集/解码/音频/补帧链路都不属于 2.0.0 UI migration。任何一点需要改它们时必须停止并另行授权。

## 证据路径约定
每轮由 UTC 时间加随机后缀生成 `<run>`，并在 WORKLOG 记录实际路径：
- 构建：`E:\项目\Veyra\build\ui-qml-migration-20260927\<run>\qml`；
- 测试/staging：`E:\项目\Veyra\tests\ui-qml-migration-20260927\<run>\app`；
- 日志：`E:\项目\Veyra\logs\ui-qml-migration-20260927\<run>\`；
- 临时目录：`E:\项目\Veyra\tmp\ui-qml-migration-20260927\<run>\`；
- entry contract 和 smoke 还会在各自 output/TEMP 根下生成唯一 `run-*` 子目录，并在 JSON 写出真实目录；旧 `ui-qml-migration-20260925`、`qt-probe-*`、`goal\r5.3` 目录不能作为当前证据。

未执行项必须明确写“未执行”，不得用旧 Win32 或 R5.3 结果替代。
