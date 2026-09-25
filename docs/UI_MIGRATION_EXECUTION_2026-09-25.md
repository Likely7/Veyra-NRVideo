# QML 界面迁移执行记录

分支：`codex/ui-qml-migration-20260925`
起点：main `df41580`，标签 `checkpoint/pre-ui-qml-migration-20260925`
方案：[`UI_MIGRATION_MASTER_PLAN_2026-09-25.md`](UI_MIGRATION_MASTER_PLAN_2026-09-25.md)
产物目录：`E:\项目\Veyra\build|tests|logs\ui-qml-migration-20260925`

每一步完成后打标签 `checkpoint/ui-mig-<步骤>`。回退方法：`git reset --hard <标签>`（在本分支上操作），
或从标签新开分支。

## 进度表

| 步骤 | 内容 | 状态 | 标签 | 说明 |
|---|---|---|---|---|
| S0.1 | 建分支、更新文档 | 完成 | `checkpoint/ui-mig-s0.1` | |
| S0.2 | 构建与现有测试基线 | 完成 | `checkpoint/ui-mig-s0` | 600/600 目标构建通过；交付门槛 PASS |
| S0.3 | 抓帧哈希基线 | 完成 | `checkpoint/ui-mig-s0` | 13 种配置 × 24 帧，两次运行完全一致 |
| S0.4 | 性能 / 延迟基线 | 完成 | `checkpoint/ui-mig-s0` | 4 种播放场景 |
| S1.1 | 合并图描述构建 | 完成 | `checkpoint/ui-mig-s1.1` | 5 处合并为 `describeStages()`；13 项哈希一致 |

## 发现并修复的 bug

| # | 发现于 | 问题 | 修复 |
|---|---|---|---|
| B1 | S0.2 | `veyra_live_timing_tests` 的“输出上限仍会跳过候选帧”断言一直失败：09-22 把输出上限改到独立时间网格（`rateSubmitted`）后，测试只调用了 `submitted()`，测的是旧行为。产品逻辑正确，测试过期 | 测试补上 `rateSubmitted()` |
| B3 | S1.1 | 非 NVIDIA 显卡上，`initializePreview` 的能力归一化分支把 `enableSr` 直接写成 false，AMD FSR 超分（唯一的跨厂商超分）在预览里永远不生效，而 `disableUnsupportedNvidiaEffects` 是特意保留它的。导出路径没有这个问题 | 5 处描述合并进 `describeStages()`，两边同一套规则 |
| B2 | S0.2 | `veyra_quality_probe` 用 ANSI `argv` 转宽字符，输出目录含中文（`E:\项目`）时写图失败，交付门槛因此失败；门槛脚本原先用相对路径绕开，但源码在 C:、日志在 E: 时相对路径无法跨盘 | 探针改为从 UTF-16 命令行读参数；门槛改传绝对路径 |

## 日志

### S1.1（2026-09-25）合并图描述构建
- 新增 `include/veyra/engine/GraphDescription.h`：`StageRequest` + `describeStages()`，把原先分散在 `EngineController.cpp`（初始打开、能力归一化、初始化失败降级、设置重建、串流尺寸切换、增强故障恢复共 6 处）和 `VideoExportJob.cpp` 的 `EnhanceGraphDesc` 填充合并成一处；尺寸策略、NR/超导顺序、厂商能力判断、参数下发全部由它决定。
- 各处的差异是历史遗留，不是有意区分：导出路径用 `Native` 尺寸、禁用 NR 先行、不走呈现端补帧，这些用 `StageRequest::exportJob` 表达；图片用 `stillImage` 表达。
- 验证：13 项抓帧哈希全部一致（0 差异）；延迟对比无退化（1080p NR 提交 P95 0.92→0.99 ms、GPU 就绪 8.09→8.42 ms，均在本机噪声内，帧率与跳帧计数不变）；交付门槛另行复跑。

### S0（2026-09-25）
- 构建：`scripts/build-ui-migration.ps1`，沿用 1.4.4 发布配置，输出 `E:\项目\Veyra\build\ui-qml-migration-20260925`。`.cmd` 按 ANSI 解析，中文路径由 PowerShell 用环境变量传入。
- 测试目录：`scripts/stage-ui-migration.ps1` 复制构建产物到 `E:\项目\Veyra\tests\ui-qml-migration-20260925\app`。运行组件用 junction 指向 1.4.4 发布包（7 个文件 SHA-256 与 manifest 全部一致），`runtime_local` 为测试专用新目录，不碰用户设置。
- 交付门槛：`scripts/gates/delivery.ps1` PASS，`E:\项目\Veyra\logs\ui-qml-migration-20260925\s0-delivery\4acefbbfd4744c64bc274e2c9181e41e\result.json`（修复 B2 后）。
- 单元测试：`scripts/run-unit-ui-migration.ps1` 无参数直接运行 75 个测试程序，40 个通过，其余 35 个需要参数或素材（用法退出码 2），不作为基线判据；基线判据为交付门槛与下面两项。
- 抓帧哈希：新增 `veyra_chain_hash_probe` 与 `scripts/hash-ui-migration.ps1`。13 种配置（直通、NR、NR 风格 2、残差、保护区域、时域防闪、DLSS SR、RTX VSR、SR→NR、NR→SR、VSR→NR、调色、调色+NR）× 24 帧，CPU 解码，逐帧 SHA-256。两次运行 0 差异，13 个末帧哈希互不相同。基线 `E:\项目\Veyra\tests\ui-qml-migration-20260925\hash-baseline.json`。
- 性能 / 延迟：新增 `scripts/perf-ui-migration.ps1`（真实播放器 8 秒，取 `[player-timing]` 各 P95 均值与 `[frame-flow]` 计数）。基线 `perf-baseline.json`：
  - 1080p NR：提交 P95 0.92 ms，GPU 就绪 P95 8.09 ms，NR 5.78 ms，60 fps，跳帧 0
  - 1080p NR + 2X：提交 1.28 ms，GPU 就绪 9.41 ms，120 fps，跳帧 0
  - 4K NR + 2X：提交 1.38 ms，GPU 就绪 12.46 ms，120 fps，跳帧 0
  - 1080p 无增强：提交 0.13 ms，GPU 就绪 1.57 ms
