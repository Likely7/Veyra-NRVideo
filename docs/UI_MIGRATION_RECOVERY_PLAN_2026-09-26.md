# QML 迁移恢复与无人值守运行手册（2026-09-27）
本手册用于从当前跑偏状态恢复执行顺序，不恢复旧的“先改引擎再换 UI”方案。

## 每轮固定顺序
1. 记录 `git status --short --branch`、`git rev-parse --show-toplevel`、`git rev-parse HEAD`、`git rev-parse main`、`git worktree list --porcelain`，确认分支仍为 `codex/ui-qml-migration-20260925`，并明确当前目录是否为 linked worktree。
2. 运行 `scripts/acceptance/ui-migration-scope-guard.ps1`；脚本固定当前仓库、目标分支、`main=df41580f7fa0d2b26718f355640470e8cb94b324`、baseline 和 guard 路径，并要求两份控制文件已 tracked 且 clean。失败立即停，不构建、不测试、不改代码。
3. 生成本轮 UTC+随机后缀的 `build`、`staging`、`logs`、`tmp` 四个 E 盘目录。目录必须为空；任何已有 CMake cache、旧 executable、旧 `qml/` 或旧 `entry.json` 都不能复用。stage 的 Release 源也必须显式指定为 E 盘目录，不能使用脚本旧默认值。
4. 只使用 `scripts/build-ui-migration.ps1 -UiTarget qml -Out <本轮build> -Log <本轮logs>\build.log -Temp <本轮tmp> -Targets veyra_qml_data_tests veyra_qml_easing_tests veyra_qml_quick_tests`；脚本会同时构建 `veyra_qml_ui`。构建前后各运行一次 scope guard；任一次失败立即停止。
5. 使用本轮 build 显式运行 entry contract：`test-ui-migration-entry-contract.ps1 -QmlBuild <本轮build> -OutputDirectory <本轮logs>\entry-contract`。entry contract 后再次运行 scope guard，再用同一 QML staging 运行 `run-unit-ui-migration.ps1`；unit runner 只能从同一 QML staging 取三个 QML 测试。entry JSON 的 branch、commit、cache 开关和 executable hash 必须与本轮一致。
6. U3 完成后再次运行 scope guard，再运行 `qml-ui-smoke.ps1`；显式传入本轮 staging、fixture root、output root 和 TEMP root。脚本在两个 root 下生成唯一 `run-*` 子目录并在 `result.json` 记录实际路径。缺少 `test_av_1080p.mp4` 默认失败；若明确使用 `-AllowMissingPlaybackFixture`，只能记录 skipped，不能记为 playback 通过，进程仍必须退出 1。smoke 完成后再运行一次 scope guard。
7. U4 运行/交互通过后，单独执行 U5：逐张记录 17 个设计 frame 的截图和差异；未执行、失败、接口缺口必须保留原状态。
8. 只在 `qml/**`、`apps/veyra-qml/**`、QML bridge、QML 测试、迁移脚本和 UI 文档中修复；更新执行文档和 `docs/WORKLOG.md`。

## QML 入口合同
- VEYRA_BUILD_QML_UI=ON；
- veyra_qml_ui.exe；
- 本轮新建且为空的 ...\app\qml staging；
- staging 根目录不得同时存在 veyra.exe；
- qml/Veyra/qmldir 存在且与构建目录匹配。

旧 veyra.exe、VEYRA_BUILD_QML_UI=OFF、旧 delivery staging 和后端 fixture 不能被写成 QML 通过。旧日志即使显示 pass，也不能替代当前 run 的 QML 证据。

## 无人值守停止规则
scope guard 任一冻结 hash 变化、新增后端路径、main 指针变化、控制文件未 tracked/不 clean、入口合同不一致、非空/复用 staging、QML-only 测试缺失或失败，均立即停止。不得通过参数替换 root/baseline，不得自动回滚、删除、改阈值、跳过测试或把失败改称诊断通过。若 QML 为了完成某控件需要修改 engine/pipeline/source/sink/media/gfx/ngx/shaders/CMake，立即记录接口缺口并结束本轮。

## 后端接口缺口
只记录缺口，不改 engine/pipeline/source/sink/media/gfx/ngx/shaders/CMakeLists.txt。需要新增能力时创建独立后端计划和分支，由用户另行授权；当前 2.0.0 目标暂停该能力。

## 结束条件
U0–U6 当前证据齐全且用户验收后，才可讨论删除旧 Win32 UI、切换默认入口或整合主线。合并、push、Release 仍需用户当时明确授权。

## 当前污染状态说明

本分支保留了审计发现的历史后端改动，scope baseline 只负责冻结它们，不代表它们通过了链路验收，也不代表 QML 结果可以外推为 `main` 等价。R5.3、delivery、性能、采集和导出证据全部退出本手册的 UI 队列；不得因为无人值守流程需要“有东西可跑”而重新打开这些路径。
