# QML UI 目标子计划（历史恢复范围）
日期：2026-09-27
状态：完整目标统一见 [2.0.0 完整升级与恢复总方案](UI_V2_0_0_MASTER_PLAN_2026-09-27.md)。本文件保留 UI-only 恢复记录，不是完整交付范围；原 G/B/M01–M42 内容以 Git `53a2d31:docs/UI_FULL_GOAL_PLAN_2026-09-26.md` 为固定需求证据。

R5.3 恢复为新方案的第一工程关口，随后覆盖 R5.4/R5.5、全部功能、设计、回归和正式交付；不恢复旧 Goal。当前代码安全锁继续生效，必须先经新方案 P0 显式迁移。本轮未启动代码修改、测试或无人值守。

## 目标
交付已批准设计稿对应的 QML 前端：页面、窗口、弹窗、菜单、动效、全屏、DPI、入口，以及已经由既有 bridge 暴露的功能操作可用。后端处理链保持现状，QML 只通过既有 bridge/facade 读取状态和发出命令；未暴露的能力记录为接口缺口。

## 工作包
1. 边界封存：scope guard、branch/main/worktree 记录、QML 与 legacy 构建入口分离。
2. 桥接核对：确认页面使用的字段、信号、命令和线程生命周期；只在 src/ui/Qml*.cpp 等允许路径补适配。
3. 页面完成：home、minimal、professional、node、export、settings、capture/PS5/screen dialogs、preset dialogs、fullscreen overlays。
4. 行为完成：页面切换、拖放、播放控制、菜单、弹窗、快捷键、导出入口、设置保存和恢复；行为必须调用已有能力。
5. 验证：QML-only tests、唯一 run 目录的 entry contract、QML smoke/U4 交互和独立的 U5 17 frame 设计对照。
6. 封存验收：整理 E 盘证据；用户验收后再决定旧 UI 删除和主线整合。

## 本轮 UI 子任务不执行的工作
新算法、新 shader、新颜色链、新导出能力、新采集格式、新音频链、新 NR/FG 多实例和性能修复不在本轮 UI 施工范围。原已确认的升级需求保留在 `docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md` 第 2.1 节，不能因此标记为已完成或产品需求取消。R5.3 不是页面/UI 自动测试的前置条件，但依赖它的多实例调色升级也不能跳过其正确性和性能验收。禁止将其重新混入 UI 自动执行队列。

## 停止条件
- scope guard 失败；
- QML 需要修改 engine/pipeline 才能显示某项数据；
- 构建入口、staging 或测试无法证明运行的是 veyra_qml_ui.exe；
- 设计差异尚未定位却试图以整体截图通过。
- 播放 fixture 缺失却未显式记录为 skipped，或把 skipped 当作通过。
- 证据来自旧 run 目录、legacy staging 或 `veyra.exe`。

停止后记录事实和缺口，等待新的后端授权，不继续猜测式修复。
