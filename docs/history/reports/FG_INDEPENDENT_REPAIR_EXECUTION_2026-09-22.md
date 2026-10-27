# DLSS / XeSS 补帧独立修复执行记录

日期：2026-09-22。隔离分支 `codex/fg-independent-repair-20260922`，工作树
`E:/项目/Veyra/worktrees/fg-independent-repair-20260922`，开工存档
`checkpoint/pre-fg-independent-repair-20260922`（`c3f1ec8`）。方案见
[独立复核与修复方案](FG_INDEPENDENT_REVIEW_2026-09-22.md)。本记录只包含本机
RTX 5070 / 驱动 32.0.16.1656 / 100 Hz 显示器上的 30 秒短测；120 秒以上长测、
其他显卡、采集卡和肉眼画质由用户验收，本文不宣称这些已通过。

产物根目录：`E:/项目/Veyra/tests/fg-independent-repair-20260922/`（`app/` 为研发
staging，运行库/shader 为 junction 到既有受控目录，不是便携包）；构建
`E:/项目/Veyra/build/fg-independent-repair-20260922/`；日志
`E:/项目/Veyra/logs/fg-independent-repair-20260922/`；临时
`E:/项目/Veyra/tmp/fg-independent-repair-20260922/`。

## 1. 保留的产品改动

| 编号 | 改动 | 位置 | 对照开关（仅诊断） |
| --- | --- | --- | --- |
| X1 | XeSS `frameRenderTime` 默认传真实相邻源 PTS 间隔（历史有效、身份递增、PTS 递增、0.125–500 ms），重置/重复仍传 0；有界跳帧后身份可跨 2–3 帧 | `VideoPresenter.cpp` XeSS tag 段 | `VEYRA_DISABLE_XESS_SOURCE_TIMING=1` 恢复传 0 |
| F2 | 预览跳帧 ≤2 帧且无源断点时不再清 NR/XeSS/DLSS 历史（epoch 不递增，A 仍是上一处理帧的真实 PTS）；>2 帧或任何断点标志仍完整重置 | `EngineController.cpp` `boundedSkip` | `VEYRA_TEST_PREVIEW_SKIP_RESET=1` 恢复无条件重置 |
| F3 | DLSS 文件路径：整组超预算但"基础工作 + 一次首帧插值"能赶上中点时，评估 2X 组并保留历史（`FgDecision::Reduced`），UI `previewFgMultiplier=2`；否则沿用播种/跳过 | `FgRecoveryBudget::canAdmitReduced`、`EnhanceGraph::process`、文件准入 lambda | `VEYRA_TEST_FG_NO_REDUCED=1` 恢复整组丢弃 |
| F4 | 交换链创建时记录显示器刷新率；每秒 `GetFrameStatistics` 差分记录 presents / displayed / refreshes / notDisplayed | `PresentSink::sampleFrameStatistics`，日志 `display-stats` | `VEYRA_TEST_NO_DISPLAY_STATS=1` |
| 诊断 | `fg_harness --fg-planar-alt`：multiFrameCount 5/1/5 交替不重置 | `tools/fg_harness` | — |
| 诊断 | `scripts/acceptance/fg-independent-ab.py`：按环境变量变体串行调用既有矩阵脚本并汇总 | 脚本 | — |

## 2. 尝试后撤回

| 编号 | 尝试 | 结果 | 处理 |
| --- | --- | --- | --- |
| X2 | 文件播放 XeLL `bLowLatencyMode=0` | `xefgSwapChainSetEnabled` 返回 -15（LATENCY_REDUCTION_UNSUPPORTED），提供方拒绝生成；Intel 指南明确"XeLL 未启用则关闭补帧" | 默认保持 1；保留 `VEYRA_TEST_XESS_LOW_LATENCY=0` 诊断开关 |
| X3 | 提供方 `Present` 移到辅助线程（单飞、fence 合同不变、owner 在 pending 时让出） | 真超分+NR+4X：源 51.0→23.8/s，GPU 94.9%→43.9%，`gpuReadyP95` 20→66 ms，组内间隔 4.3→9.9 ms；trace 显示提供方 `Present` 内阻塞变为 p50 44 ms，其周期环中位数从 15.9 ms 爬到 26 ms | 代码全部移除，不留开关。原因见 §4.3 |

## 3. 门槛测试（本机）

| 测试 | 结果 | 证据 |
| --- | --- | --- |
| 构建 veyra + 4 个 FG 测试 + harness + scheduler 单测 | exit 0 | `logs/.../build7.log` |
| `veyra_presentation_worker_tests`（含新增 6 项 reduced 预算断言） | 127 PASS / 0 FAIL | `tests/.../unit-scheduler.log` |
| `veyra_fg_presentation_tests`（4X→6X→4X、resize、fence 复用） | exit 0，D3D12 errors=0 | `fg-presentation2.log` |
| `veyra_fg_settings_tests backend-switch`（XeSS4→DLSS4→XeSS4→XeSS2→XeSS4→DLSS6→XeSS4） | 16 PASS / 0 FAIL | `settings2-backend-switch.log` |
| `fg_harness --fg-planar6` | 55/55 独立输出，位置通过 33/55（sub2/sub5 系统性偏差，与既有 6X 位置误差记录一致，非本轮引入） | `harness/planar6.log` |
| `fg_harness --fg-planar-alt` | 35/35 独立输出，group=1 中点 5/5 精确 8.0 px，group=5 各 sub 与固定模式逐项一致；无 disabled | `harness/planar-alt.log` |

harness 结论：NGX MFG 在不重置的情况下逐对切换 `multiFrameCount` 5↔1，输出有效且
位置正确。这是 F3 的前置条件。

## 4. 短测结果（30 秒，全屏 2560×1440，统计 10–29 秒）

### 4.1 XeSS 4X，1080p→4K Video SR + 实时 NR（重负载）

`ab-xess4-sr/`（同一构建，仅环境变量不同，串行运行）：

| 变体 | 源/s | 生成/s | epoch | 呈现原帧间 epoch 变化 | 组内间隔 | Present 阻塞 p50/p95 | 原帧间隔 p95 | GPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline（传 0，跳帧重置） | 53.6 | 141.2 | 177 | 90 | 4.96 ms | 17.2 / 17.9 | 29.1 | 95.5% |
| x1（真实周期，跳帧重置） | 53.5 | 141.0 | 182 | 92 | 4.27 ms | 15.0 / 17.0 | 26.8 | 95.1% |
| full（x1 + F2） | 50.8 | 152.3 | 1 | 0 | 4.33 ms | 13.5 / 15.9 | 22.2 | 94.9% |

同日早晨的旧构建（`bounded-repair` exe）在同一会话中重跑：源 41.9/s，生成 94.3/s，
组内 8.16 ms，Present 阻塞 p50 20.8 ms，GPU 70.2%。早晨原始对照为 39.3/s、72%。

解读：
- 本轮三个变体源率都在 50–54/s，均高于旧构建的 39–42/s，而它们与旧构建的差别不只
  X1/F2（构建、F4 采样、harness 汇总也不同）。**X1 本身在 baseline→x1 中未改变源率**，
  改变的是组内间隔（4.96→4.27 ms）和 Present 阻塞（17.2→15.0 ms）。旧构建在同一会话
  重跑仍是 42/s、8.2 ms 组内，因此差异不是环境漂移。
- F2 把重置次数从 177 降到 1；呈现原帧间隔 p95 从 26.8 降到 22.2 ms。代价是源率
  53.5→50.8/s（跳帧从 178 增至 250）：不重置后每帧都做完整 4X 生成（生成/s 反而上升），
  GPU 时间被生成占用。这是"更多生成、稍少原帧"的取舍，画面对应尚未肉眼验收。
- F4：100 Hz 显示器上每秒 displayed≈101、notDisplayed=0。应用侧 Present 全部被扫描出，
  组内生成帧由提供方内部 Present，不在此计数。显示端在本机没有丢弃应用提交。

### 4.1b XeSS 重复性（`ab4-xess-final/`，同一构建，稍后串行重跑）

| 变体 / 工况 | 源/s | 生成/s | epoch | 组内间隔 | Present 阻塞 p50/p95 | 原帧间隔 p95 | GPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline / SR+NR 4X | 38.0 | 85.5 | 263 | 8.76 ms | 11.5 / 29.4 | 41.6 | 72.8% |
| full / SR+NR 4X | 45.4 | 136.1 | 1 | 4.77 ms | 12.8 / 16.4 | 26.5 | 95.5% |
| baseline / 原生 4K+NR 4X | 59.2 | 175.1 | 20 | 4.25 ms | 11.9 / 14.4 | 17.8 | 94.4% |
| full / 原生 4K+NR 4X | 58.5 | 175.4 | 1 | 4.17 ms | 11.5 / 12.3 | 17.8 | 94.6% |
| baseline / SR+NR 2X | 59.9 | 59.9 | 1 | — | 10.4 / 11.0 | 17.4 | 95.0% |
| full / SR+NR 2X | 60.0 | 60.0 | 1 | — | 10.2 / 10.7 | 17.4 | 95.1% |

轻负载（原生 4X、SR 2X）两变体一致，full 没有退步；原生 4X 的 20 次重置也归零。

同一构建的 baseline 变体两次结果差异很大（53.6 vs 38.0 源/s；组内 4.96 vs 8.76 ms）。
这与早晨旧构建的两次（39.3、41.9）一起说明：**传 0 的旧路径存在两个稳态**，取决于
提供方在启动几秒内把周期估计锁在约 16 ms 还是约 21–25 ms（`xess-pacing` 前几行的
ring median 分别为 14.3→20.7 ms 和 0→16.8→25.6 ms）。锁在高值后 GPU 只有 72%、
组内 8.7 ms、源 38/s，即用户报告的"GPU 未满却卡顿"形态；锁在低值时接近满速。
X1 的两次 full 结果为 51.0 和 45.4 源/s，组内都在 4.3–4.8 ms、GPU 95%，没有进入
高值稳态。这是 X1 的主要价值：消除双稳态而不是提高峰值；但两次 45–51 的差异说明
30 秒短测不足以给出精确均值，长测由用户执行。

### 4.2 DLSS（F3 reduced 组）

`ab3-dlss/`，同一构建，`noreduced` = `VEYRA_TEST_FG_NO_REDUCED=1` 对照：

| 工况 | 变体 | 提交/s | 间隔 >16.667 ms | 间隔 p95 / p99 / max (ms) | 每对有效生成分布 | 拒绝对 | 播种评估 | reduced 对 | 过期生成 |
| --- | --- | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: |
| 原生 4K+NR 6X | noreduced | 238.4 | 73 / 1460 | 16.67 / 17.01 / 17.73 | 5×219，0×149 | 654 | 657 | 0 | 12 |
| 原生 4K+NR 6X | full | 237.1 | **3** / 1481 | **8.53** / 8.81 / 16.94 | 5×185，1×185，0×6 | 838 | 25 | 790 | 6 |
| 1080→4K SR+NR 6X | noreduced | 121.2 | 194 / 1041 | 16.94 / 17.08 / 17.32 | 5×105，0×411 | 1302 | 1314 | 0 | 7 |
| 1080→4K SR+NR 6X | full | 115.7 | 155 / 1043 | 16.92 / 17.07 / 17.95 | 5×84，1×84，0×374 | 1213 | 956 | 269 | 8 |
| 原生 4K+NR 2X | noreduced | 120.0 | 0 | 8.79 / 9.26 / 9.91 | 1×585 | 0 | 1 | 0 | 1 |
| 原生 4K+NR 2X | full | 120.0 | 0 | 8.78 / 9.04 / 9.69 | 1×585 | 0 | 1 | 0 | 2 |

解读：
- 原生 6X 的"5,5,5,0"周期性空档被 2X 组替代：提交率不变，超过一个源周期的空档从 73
  降到 3，p95 从 16.7 ms 降到 8.5 ms。播种评估从 657 降到 25，说明此前 24% 的对在
  做"评估却不呈现"的预热，现在同样的一次评估产出可呈现的中点帧。`fgInvalid` 为 0。
  用户看到的会是交替 6X/2X 的组，UI 已按 `previewFgMultiplier=2` 提示当前对。
- 真超分 6X 每对约 24 ms，基础工作已超过 8.3 ms 的中点，多数拒绝对连 2X 都赶不上
  （0×374），只有 269 对能用 reduced。空档 194→155，是有限改善；这部分仍是性能上限。
- 2X 不受影响（同一路径不触发 reduced）。
- F4：原生 6X 每秒提交 241 次、显示 100 次、未显示 141 次，与 100 Hz 显示器一致；
  未显示分布 p50 140 / p95 151，与 noreduced 的 140 / 151 相同。

### 4.3 X3 为什么失败

trace（`async-trace/sr-nr-xess4/`，20 s，`VEYRA_TEST_TRACE_XESS=1`）：owner 的
`Present` 阻塞降到 p50 0.27 ms（目标达到），但提供方内部 `XessPresent` 变为 p50 44 ms，
`ProviderDeadline` 从 4 ms 升到 11 ms，周期环中位数 15.9→26.3 ms。提供方在非 Intel
GPU 上用它自己的高优先级线程节奏，并以观察到的 present 到达间隔估计周期；owner 不再
被阻塞后立刻读下一源帧，源之间的处理成本（SR+NR+流约 12 ms）加上 XeLL sleep 使提供方
看到更长且更不规则的到达间隔，估计周期放大，组内间隔从 4.3 拉到 9.9 ms，owner 又在
`gpuReady`（等 GPU 就绪）处等待 65 ms。结论：在这个提供方上，"owner 被 Present 阻塞"
恰好起到了把源提交节奏锁到提供方输出节奏的作用；去掉阻塞需要同时替代提供方的周期估计
（Magpie 改写时间戳的做法），单独解耦线程是负优化。已从代码删除。

## 5. 判定与停止条件

- X1：保留。组内间隔回到 4.2–4.3 ms 且不降源率。
- F2：保留，但需用户在长测和肉眼上确认无新增拖影；若快速运动场景出现可见拖影，
  把上限从 2 帧降到 1 帧（`previewSkippedSinceSubmit<=1`）或撤回。
- F3：保留。原生 6X 长空档 73→3 且无 `fgInvalid`；真超分 6X 仅小幅改善，剩余属上限。
- F4：保留（只读诊断）。
- X2、X3：撤回，记入实验索引，不再重试同形态。

## 6. 未做与边界

- 未做 120 秒以上长测、采集卡、PS5、RTX 30/40/5090、多显示器、HDR。
- 未做像素级 C 实验（合成素材的亮度脉冲），F2 的"闪烁消失"只在 epoch 计数层面成立。
- XeSS 重负载 4X 在本机仍不到 60 源帧/s（50–54），剩余差距是 SR+NR+流约 12 ms 加
  Present 阻塞约 14 ms 的串行成本，属于当前架构下的上限；进一步需要改写提供方时间戳
  或减少每帧 GPU 工作，本轮未授权。
- 未打包、未合并 main、未推送、未发布。
