# QML 界面迁移执行记录（活动版）
更新时间：2026-09-27（第五次对抗式审计后）
当前分支：codex/ui-qml-migration-20260925
审计时 HEAD：babebbb（checkpoint/ui-mig-r5.2-c）
main：df41580
合并 / push / Release：均未发生。

本文件重写为 UI-only 执行记录。旧版 R5.3/R5.4/R5.5 长记录现作为历史审计材料，不再驱动任务队列。

## 2026-09-27 纠偏后的执行合同
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
| 已提交范围 | 1530844 至 babebbb 之间混入 engine/source/pipeline/NR/export/color 改动 | 越界提交冻结，不计 UI 完成 |
| 未提交范围 | CMake、旧 AppShell、engine/pipeline/shader、R5.3 脚本和测试仍有 dirty paths | baseline 后 hash 冻结 |
| 误测入口 | 旧证据使用 E:\项目\Veyra\build\ui-qml-migration-20260925、VEYRA_BUILD_QML_UI=OFF、veyra.exe | 不能作为 QML 证据 |
| 正确入口 | QML 使用 VEYRA_BUILD_QML_UI=ON、veyra_qml_ui.exe 和独立 staging | 后续 QML gate 只认该入口 |
| R5.3 | 没有形成独立、完整、可接受的后端验收；且本目标不包含它 | 暂停，不再推进 |

详细越界清单见 UI_MIGRATION_SCOPE_AUDIT_2026-09-27.md。

## 已完成的 UI 侧基础
- QML 页面、组件、Qt entry 和 bridge 已存在于当前分支；
- 构建脚本已改为显式区分 qml / legacy，构建时锁定目标 executable；
- staging 脚本要求单一 UI executable，避免旧 veyra.exe 混入 QML app；
- unit 脚本只寻找三个 QML 测试，不再扫描后端全量测试；
- entry resolver / contract / QML smoke 已加入入口一致性检查。

以上只是代码状态，不等于 gate 通过；最终以本轮重新执行证据为准。

## 当前待办（只允许这些）
- [ ] U0：控制文件提交后重新运行 scope guard；旧结果 `status=pass`、`currentChanged=178`、`frozenChecked=71` 仅作历史记录。当前 baseline 已重建为 `initialChanged=582`、`frozen=475`，包含三个源码根目录残留共 405 个文件；guard 已固定 root/branch/main/baseline 并要求 baseline 与 guard tracked + clean。提交和新 E 盘证据验证完成前不进入 U2；
- [x] U1：只读盘点 QML bridge 对现有 engine API 的消费，记录接口缺口；见 `docs/UI_MIGRATION_BRIDGE_AUDIT_2026-09-27.md`。没有修改冻结链路。
- [ ] U2：构建 QML target，确认 cache、目标文件和 Qt module；
- [ ] U3：entry contract + 三个 QML-only 测试；
- [ ] U4：QML smoke、窗口层级、文件打开和 dialog 状态；
- [ ] U5：17 个设计 frame 的截图/交互对照；
- [ ] U6：整理证据并交给用户验收。

## 明确移出本目标
R5.3 多实例调色、R5.4/R5.5、EffectChain/GraphDescription 新能力、NR 多实例、颜色 shader、导出队列、trim、采集/解码/音频/补帧链路都不属于 2.0.0 UI migration。任何一点需要改它们时必须停止并另行授权。

## 证据路径约定
每轮由 UTC 时间加随机后缀生成 `<run>`，并在 WORKLOG 记录实际路径：
- 构建：`E:\项目\Veyra\build\ui-qml-migration-20260927\<run>\qml`；
- 测试/staging：`E:\项目\Veyra\tests\ui-qml-migration-20260927\<run>\app`；
- 日志：`E:\项目\Veyra\logs\ui-qml-migration-20260927\<run>\`；
- 临时目录：`E:\项目\Veyra\tmp\ui-qml-migration-20260927\<run>\`；
- entry contract 和 smoke 还会在各自 output/TEMP 根下生成唯一 `run-*` 子目录，并在 JSON 写出真实目录；旧 `ui-qml-migration-20260925`、`qt-probe-*`、`goal\r5.3` 目录不能作为当前证据。

未执行项必须明确写“未执行”，不得用旧 Win32 或 R5.3 结果替代。
