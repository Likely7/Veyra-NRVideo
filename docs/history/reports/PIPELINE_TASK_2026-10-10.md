# 软件链路优化（任务二，2026-10-10）执行记录

## 授权与范围

- 用户先后要求：“开始进行优化吧，全部搞定掉”“开始之前先创建存档，然后开分支弄，如果是结果是性能反而倒退，直接退回”。
- 范围是采集输入 → 各效果之间的衔接 → 送显/上屏，以及 owner 线程调度。VFG 是另一项任务，在 `claude/vfg-20261010` 分支单独实施，两边互不依赖。
- 不改 DLSS/XeSS/FSR、NR、SR 模型，也不改运行库、驱动和用户配置。不 merge/push/Release。

## 存档与分支

- 检查点标签 `checkpoint/pre-claude-perf-20261010` 指向 main `5ba186a`。
- bundle 和 start.json 在 `E:/项目/Veyra/archives/claude-perf-20261010`，已 verify。
- 分支 `claude/pipeline-20261010`，工作树 `E:/项目/Veyra/worktrees/pipeline-20261010`。
- 构建：`py -3.11 -I E:/项目/Veyra/tmp/claude-perf-20261010/build.py pipeline`。

## 测试方法

- 本机 RTX 5070，2560×1440 320 Hz 屏，VSync 自动。
- 采集卡 KUHAIMI 27P，2K60 NV12，信号源 PS5。
- 每组稳态窗口 20 秒；新旧行为用同一个 EXE 的测试环境变量切换。
- 脚本 `tests/claude-perf-20261010/harness/ab.py`，日志 `logs/claude-perf-20261010/pipeline/`。
- 屏幕侧时间用 PresentMon 2.6.0（管理员运行）与软件日志按 QPC 逐帧对齐：脚本 `pm_join.py`，数据 `logs/claude-perf-20261010/presentmon-prio.csv` 及 `.join.json`。
- 中途换卡（MCS 4K-T911，NV12 只有“@120”，两组源帧率分别为 60/120）的两组标 `invalid-*` 作废，没有用于结论。

## 结果

| 项 | 提交 | 结论 | 证据 |
| --- | --- | --- | --- |
| C1：日志记录 Present→上屏时间（DXGI 帧统计，VSync 送显） | d49b145、f95f1b3 | **保留** | 与 PresentMon 同场对照，4X 超载 P50/P95 3.58/6.35 ms（PresentMon）对 3.59/6.36 ms（日志）；2X 2.04/3.70 对 2.06/3.68；无效果 1.86/3.28 对 1.82/3.19。首版只取每次查询时最新显示的一帧，超载时偏低，已改为同时计入两次查询之间显示的帧（按 VSync 上界）。合成模式查询（IDXGISwapChainMedia）在本机返回不可用（-1），合成方式仍以 PresentMon 为准（Hardware Composed: Independent Flip） |
| C2：显示同步说明里加“允许撕裂 / G-SYNC·FreeSync 可少等最多一次刷新” | 6d64882 | **保留**，只改提示，默认仍为自动（VSync） | zh-TW/en/ja 已更新 |
| C3：内容重复或被判失效但已完整运算的补帧组，也作为配对节奏的校准样本 | 733bc2a、ade8af0 | **保留** | 静止黑场 60→30 + NR + VFG 2X：原帧回调→送显 41.12/41.44 → **29.07/29.37 ms**，帧率均为 30.01；运动画面不变（29.27/29.63 对 29.28/29.65 ms） |
| C7：实时源送显前先确认交换链能接，过时插帧直接让位 | 406eb99 | **负优化，已退回 548a0f8** | 2X 与无效果完全不变；4X 超载帧率 191.0 → 170.9 fps，过期插帧 46 → 521，上屏时间不变。PresentMon 显示超载时上屏慢的大头不是交换链排队 |
| 送显队列提为高优先级（已有测试开关 VEYRA_TEST_PRESENT_QUEUE_PRIORITY=high） | 不改代码 | 不采用 | PresentMon 交替 4 次：普通/高优先级 Present→上屏 3.58–3.67 / 3.61–3.63 ms，原帧采集→上屏 28.8–28.9 / 28.7–28.9 ms，都在误差内（与 10-04 结论一致） |
| C4：合并衔接小 pass | — | 不做 | 1440p 采集下调色 0.09、残差合成 0.15–0.17、上屏拷贝 0.02 ms，合计不到 0.3 ms，合并最多省 0.1–0.2 ms，且有逐位一致风险 |
| C5：owner 线程阻塞点 | — | 不做 | 所有组（含 4X 超载）送显命令槽等待均为 0 |
| C6：光流期间插入其它 pass | — | 不做 | 光流约 1.2 ms，下游 NR 依赖光流结果，链路里没有可以与它并行的 pass |

## 对之前方案的更正

- 方案里写的“VFG 4X 时 Present→上屏 P50 8.9 / P95 21.8 ms、原帧采集→上屏 P95 50 ms”，今天连续 4 次没有复现。
- 今天同配置的 PresentMon 结果：Present→上屏 3.6 / 6.4 ms；Present→显卡工作完成 1.3 / 2.2 ms；原帧采集→上屏 P50 28.8 / P95 35.5 ms。
- 那次超载里“显卡工作完成”就晚了 5.0 / 18.8 ms，主要是送显前的拷贝等显卡，不是交换链排队。所以 C7 的前提不成立，已退回。

## C3 静止画面

- 首版（733bc2a）在静止黑场下没有生效（41.13 对 41.10 ms）。
- 原因：`resolveGeneration` 在一组插帧全部作废后会清掉 `hasGenerated`，而首版仍要求它为真。
- ade8af0 改为按组内实际的生成帧来识别已完整运算的组。修正后同 EXE 对比：41.12/41.44 → 29.07/29.37 ms（`s2-c3a-old` / `s2-c3b-new`）。
- 静止画面下原帧本来就和上一帧相同，这一项真正的好处在于：画面重新动起来的那一刻，配对节奏仍处在已校准的状态，不再从完整配对延迟起步、再花约 1 秒慢慢收敛。

## 回归

- `py -3.11 -I tests/claude-perf-20261010/harness/regress.py pipeline ...`：chain、contracts、hdr、presenter、lifecycle、sr、video、rollback、inactive、inactive-node、slider、menu、components，13 组全部通过。
- 最终代码（ade8af0）回执 `logs/claude-perf-20261010/pipeline/tests-20261010-223935/receipt.json`；C3 修正前一轮 `tests-20261010-223027` 同样全部通过。
- donor 1614 文件前后逐项一致；最终构建 `logs/claude-perf-20261010/pipeline/build-20261010-223917`。
