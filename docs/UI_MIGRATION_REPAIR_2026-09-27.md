# QML 恢复性修复报告（2026-09-27）

## 结论与边界
本轮修复已定位的 QML 构建、部署和验收脚本故障，不继续旧 R5.3 目标。全新构建、入口合同、三个 QML 测试程序及八项自动 smoke 已通过；完整交互/视觉、后端等价性与性能仍未验收，不能宣布整个迁移或所有历史问题已经完成。

没有证据在本报告中确认后方模型路由或所谓“降智”；未修改模型、provider 或账户计费配置。工程缺陷与路由原因是不同结论。

仓库为 `C:/Users/123/Desktop/Veyra DLSS Video Player`，分支 `codex/ui-qml-migration-20260925`。这是主 checkout 上的独立分支，不是 managed linked worktree。HEAD 保持 `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`，main 保持 `df41580f7fa0d2b26718f355640470e8cb94b324`；本轮没有新增 commit/tag、merge、push 或 Release。冻结后端、CMake、旧 AppShell 保持原样，未盲目回滚。09-25 曾有更广的后端授权，不能按 09-27 的边界追溯判定全部历史工作未经授权。

## 已证实的问题与修复
| 文件 | 问题及最小修复 |
|---|---|
| `tests/qml/QuickSmokeTests.cpp` | 全新构建复现 C2665：QString 被错误传入 fromLocal8Bit；改用 applicationDirPath + QDir 构造 staged qml 路径 |
| `scripts/run-unit-ui-migration.ps1` | 字符串数组传参改为命名 hashtable；撤销预建 scratch，由拒绝已有目录的 data test 自行创建；timeout 限制 1–300 秒 |
| `scripts/acceptance/qml-ui-smoke.ps1` | 同类传参修复；ExitAfterMs 上限 270000，加 30 秒余量不超过 300 秒 |
| `scripts/stage-ui-migration.ps1` | 文件检查证实缺 offscreen 插件及 QtTest 模块；扫描 Quick Test imports 并部署已安装 Qt 的 qoffscreen.dll，检查 QtTest/qmldir |
| `scripts/acceptance/resolve-ui-migration-entry.ps1` | 强制检查上述两个依赖，并记录 offscreen hash 和 QtTest 路径 |
| `scripts/acceptance/test-ui-migration-entry-contract.ps1` | 正例补齐依赖；增加缺 offscreen、缺 QtTest 两个反例 |
| `scripts/build-ui-migration.ps1`、`.cmd` | 限制为四个 QML target，不再透传任意构建目标/选项 |

unit/smoke 子进程设置 stderr 日志，并把 QML cache 明确写入本轮 E 盘 TEMP 子目录，未修改全局环境。计划修正了“一个 UI 入口”不等于“只准一个 exe”的矛盾，保留三个测试程序；恢复手册补上真实 staging 步骤及有限重试/停止条件。

首次 unit 的 data test 报 scratch 已存在，easing 超时。初次 stderr 不足以单独证明所有超时均由 offscreen 缺失引起；缺项来自文件检查，补齐后复测通过，不倒填失败原因。

## 验证证据
Run：`repair-overnight-20260927-035400`。以下日志路径基于 `E:/项目/Veyra/logs/repair-overnight-20260927-035400`。

| 检查 | 结果 | 证据 |
|---|---|---|
| 初次构建 | C2665 真实失败，保留原日志 | `build-qml.log` |
| 修复后全新目录构建 | 387/387，exit 0 | `build-qml-fixed.log` |
| 入口合同 | 7/7，failures=0 | `entry-contract-fixed/run-20260927T040503574Z-439d5f62/result.json` |
| QML 单测 | 三程序均 pass/exit 0 | `unit-final/summary.json` |
| Quick 内部用例 | 9 passed、0 failed、0 skipped | `unit-final/veyra_qml_quick_tests.log` |
| 自动 smoke | 8/8，均 exit 0 | `smoke/run-20260927T040646067Z-1b47552e/result.json` |
| 六份 PowerShell 脚本语法 | 零语法错误 | `syntax-final.json` |
| 参数反例 | 拒绝后端 target、unit/smoke 超 300 秒 | `script-checks.json` |
| 冻结项独立前后 hash | checked=474、changed=[] | `frozen-comparison.json` |

各阶段 scope guard 已通过；文档收口后的最终结果写入 `guard-final.json`，失败不得拿早期结果报告完成。474 项包含 405 个既有生成残留，不等于 474 个后端源码文件；本轮未改 guard/baseline。

八项 smoke：home、minimal、professional、node、export、settings、capture dialog、professional playback。实际日志未匹配到所检索的 QML 引用/类型/语法、对象创建、缺模块和加载失败错误。播放记录 sourceAccepted=173、realSubmitted=173、realPresented=168、generatedPresented=0 并正常退出；这些是软件计数，不是物理屏幕帧率或后端无回归证明。

## 产物与执行入口
- 修复前备份：`E:/项目/Veyra/archives/repair-overnight-20260927-035400`，含 13 个候选文件、tracked-before.patch、status-before.txt、frozen-before.json；不是全项目备份。
- 成功构建：`E:/项目/Veyra/build/repair-overnight-20260927-035400/qml-fixed`。
- 可运行入口：`E:/项目/Veyra/tests/repair-overnight-20260927-035400/app-fixed/veyra_qml_ui.exe`。
- runtime 来源：`E:/项目/Veyra/releases/1.4.4/final/Veyra-1.4.4-win64-portable`。
- 夹具：`E:/项目/Veyra/tests/repair-overnight-20260927-035400/fixtures/test_av_1080p.mp4`；本地既有夹具复制，来源/hash 见 fixture-source.json，未复用旧测试结果。
- TEMP 根：`E:/项目/Veyra/tmp/repair-overnight-20260927-035400`。

脚本入口与完整参数合同见恢复手册：build-ui-migration.ps1（qml）、stage-ui-migration.ps1、test-ui-migration-entry-contract.ps1、run-unit-ui-migration.ps1、qml-ui-smoke.ps1（本轮 ExitAfterMs=3500）。均显式传入本轮 build/staging/log/temp/fixture；后续复现必须换新 run 的空目录。最终命令为 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-migration-scope-guard.ps1 -Evidence E:/项目/Veyra/logs/repair-overnight-20260927-035400/guard-final.json` 及 `git diff --check`。

## 未完成与停止条件
- U2/U3 通过，U4 仅自动 smoke 通过；完整人工交互/窗口/DPI/全屏、U5 十七帧设计对照、U6 用户验收未完成。
- DXC DLL 发现、VCINSTALLDIR 和字体目录警告保留。测试通过不代表视觉、全新机器便携包或 Release 验收。unit 控制台可能显示 `=3`，判断以每程序 status/exit 的 JSON 为准。
- 冻结后端、R5.3、NR、采集/导出性能未重验；没有恢复到 main 旧实现，不声称原链路已经等价恢复。
- 失败日志和构建保留追溯；staging 含指向 Release runtime 的 junction，不可盲目递归删除。
- 本轮到此报告结束，不恢复无人值守或扩展审计。有限重试、测试时限及禁止改 guard 制造通过只是工程约束，不是服务端计费上限/外部自动熔断。
