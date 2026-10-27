# DLSS / XeSS 补帧不稳定：独立复核与修复方案

日期：2026-09-22。独立于此前 Agent 结论的源码与证据复核，只出方案，未改产品代码、
未构建、未运行新的 GPU 测试。所有数字来自已保留的日志与逐帧 trace，重新解析结果
写入 `E:/项目/Veyra/tests/fg-independent-review-20260922/trace-analysis.json`。

核对基线：工作树 `E:/项目/Veyra/worktrees/playback-nr-20260920`，分支
`codex/bounded-full-chain-20260922`，HEAD `76eaf0d`，未提交改动仅 `docs/WORKLOG.md`
和新增的 `docs/XESS_140_COMPARISON_2026-09-22.md`。桌面 checkout 在
`codex/capture-color-144beta-20260920`（`ed00218`），不是本轮基线。

## 0. 一句话结论

- **XeSS 重负载（真超分+NR+4X）的源帧倒退是软件问题，有明确根因**：单一 owner 线程
  在 XeSS 提供方的 `Present` 内被阻塞约 19 ms/帧（p50 27 ms），加上 XeLL sleep，
  线程约 90% 时间在等提供方；提供方又用它观察到的 present 间隔反推节奏
  （`frameRenderTime=0`），形成正反馈，稳定在约 39 源帧/s、组内间隔 8.5 ms。
  GPU 只有 72% 忙是因为 CPU 侧串行化，不是算力不足，也不是"调度器判错"。
- **欠速时的闪烁有一个可验证的具体机制**：预览跳帧被当作历史断点，同时重置
  NR（`kReset=1`）、DLSS FG 和 XeSS 历史。当前 XeSS 重负载运行中 25% 的呈现原帧是
  重置帧。达到目标帧率后不再跳帧，重置消失，与用户描述一致。视觉上尚未验证。
- **DLSS 4X/6X + NR 在 RTX 5070 上是性能上限，但失败形态是软件决定的**：每对源帧
  GPU 串行成本约 19.3 ms（原生）/ 24 ms（真超分），超过 16.67 ms；准入机制以"整组
  丢弃"应对，产生每第 4 组一个 16.7 ms 空档（原生 6X），这比均匀略低的帧率更显眼。
- **1.4.0/1.4.3 不是"更好的调度"**：它们靠 suppress/resume 门在重负载下反复关掉
  生成（30 秒内 35 次），Present 不再阻塞，源帧才保持 58/s。恢复该门不是修复。
- **尚无任何物理显示证据**。三个版本都是 vsync=0、允许撕裂、flip-discard 三缓冲；
  日志里连显示器刷新率都没记录。用户"读数 100–200 FPS 却卡顿"与呈现子集不均匀相容，
  但目前无法证明。

## 1. 已试方案清单（去重，防止重复尝试）

来源：`FG_EXPERIMENT_INDEX_2026-09-21.md`、`FG_NON_NR_EXPERIMENTS_2026-09-20.md`、
`POST_1_4_3_REPAIR_LEDGER_2026-09-21.md`、`DLSS_RECOVERY_REPAIR_2026-09-20.md`、
`FG_STABILITY_PROGRESS_2026-09-21.md`、Git 历史。

| 方向 | 状态 | 证据 | 结论适用条件 / 不适用条件 |
| --- | --- | --- | --- |
| 同组 GPU 成本估算器替代 P95 相加 | 已删除 | 无可重复收益 | 仅 DLSS 文件路径、原生 4K、RTX5070 |
| 去掉呈现清屏 | 已回退 | 无收益 | 同上 |
| 光流历史复制 / 原生光流尺寸直传 DLSSG | 已回退 | 无收益 | 同上 |
| 提升图形队列优先级 | 已回退 | 无可重复改善 | 同上 |
| 全局取消 DLSS admission / 无限接纳晚到生成 | 已删除 | 采集 204.10→184.76 提交/s，过期和丢源增加；文件测试也退步 | 证明"取消准入"更差，不证明当前准入的形状最优 |
| XeSS 只保留一组 pending 源帧 | 已删除 | 源 52.25→49.48/s | 只测过 20 s，且当时 owner 仍被 Present 阻塞 |
| NVOF 与 Video SR 重叠 | 已删除 | 未接受 | 关键路径 SR→NR→FG 有数据依赖，可重叠部分很小 |
| 固定 20/35/50 ms 等待、扩队列 | 已回退 | 增加延迟无改善 | — |
| "简单 CPU/presenter 拆线程、拆 owner" | 未实施，被列为"无新证据不重试" | 无 A/B 数据，是方案层面删除 | **本轮提供了新证据（§3.1），且给出显式资源合同（§5 X3）** |
| 用 Present 间隔作 XeSS frameRenderTime | 已回退（dd69e1f） | 间隔混入提供方等待，会放大节奏 | 正确回退；但不等于"应传 0" |
| XeSS 真实源 PTS 间隔作 frameRenderTime（`VEYRA_TEST_XESS_SOURCE_TIMING`） | 代码在，默认关 | 真超分+NR+4X 源 39.84→52.25/s，GPU 72.7→94.8%；整段仍有 1/4 输出间隔 <1 ms 和约 46 ms 偶发长间隔 | 收益可重复；未完成画面对应验收。**本轮判断它是 §3.1 正反馈的直接解药之一** |
| 1.4.0/1.4.3 XeSS suppress/resume 门 | dd69e1f 删除 | 30 s 内 35 次开关；关闭时 Present 0.3 ms，开启时 18 ms | 门只是把阻塞问题换成"少补帧"，不是修复 |
| DLSS 无历史时整组重置改为单次 2X 播种 | 已保留 | 预热调用减少，提交率 202.57→204.10/s | 正确，但不能解决超预算 |
| XeSS fence / deadline 只读诊断 | 保留 | 351 次首帧调度进入时全部有未完成 fence | 证明有真实依赖，不证明全是 GPU 执行 |
| 有界修复：HDR 状态按显示器隔离、采集格式身份 | 已提交 | 不涉及节奏 | — |

## 2. 证据表（能证明什么 / 不能证明什么）

所有"Present"均为应用提交或提供方 SDK 返回，不是扫描输出；GPU% 为 NVML 设备级均值。

### 2.1 XeSS 4X，2560×1440 全屏交换链，4K 增强输出，RTX 5070（`fg-140-compare-actual-fullscreen-20260922`）

| 指标 | current SR+NR | current 原生+NR | 1.4.3 SR+NR | 1.4.0 SR+NR |
| --- | ---: | ---: | ---: | ---: |
| 源提交/s（10–29 s） | 39.33 | 59.94 | 58.58 | 59.24 |
| 生成/s | 88.49 | 179.82 | 36.18 | 32.93 |
| suppress/resume 事件（整段） | 0 | 0 | 35 | 37 |
| `previewSkipped`（整段解码丢弃） | 552 / 1652 | 0 | 75 | 29 |
| 历史 epoch（= 重置次数） | 270 | 1 | 58 | 28 |
| 连续帧 Present 阻塞（trace，ms） | mean 18.8 / p50 27.1 / p95 28.6 | mean 12.5 / p50 12.4 | 开启时 ≈18，关闭时 0.3 | 同 1.4.3 |
| 重置帧 Present 阻塞（ms） | mean 8.6 | — | — | — |
| 原帧 Present 返回间隔（ms） | mean 25.4 / p95 40.2 / max 41.4 | — | — | — |
| 相邻呈现原帧 PTS 步长 | 1 周期 572 次，3 周期 171 次，4 周期 19 次 | 全部 1 周期 | — | — |
| 组内生成帧间隔（xess-pacing） | 8.44–8.54 ms | 4.13–4.22 ms | — | — |
| 提供方周期环中位数（启动段） | 15.8→21.5 ms 递增 | 15.0→15.7 ms | 13.8→21.3 ms | — |
| gpuReadyP95 / presentP95（player-timing） | 31.0 / 28.6 ms | 17.5 / 12.9 ms | 25.2 / 18.3 ms | 12.7 / 2.3 ms |
| GPU 各段 P95（SR/NR/流/色/残差） | 5.9 / 7.4 / 1.4–1.8 / 0.2 / 0.3 ms | 0 / 7.5 / 1.7 / 0.6 / 0.4 | 5.2 / 7.2 / 1.7 | 5.5 / 7.1 / 1.7 |
| GPU 均值 / 范围 | 72% [70–74] | 93% [91–94] | 81% [72–96] | 81% [72–96] |
| 功率 | 186 W [181–188] | 223 W | 215 W [207–221] | 215 W [207–226] |
| slotWaits | 0 | 0 | 0 | 5 |

能证明：current SR 场景下 owner 线程每个源帧在 XeSS `Present` 内平均阻塞 18.8 ms，
39 帧/s × 18.8 ms ≈ 73% 的线程时间；再加 XeLL sleep（同工况另一构建 trace：
mean 4.7 / p95 15.8 / max 18–25 ms），线程约 90% 时间在等提供方。组内间隔 8.5 ms 是
提供方自己的调度器给出的（`bypassed=0 fallbackFrames=0`，未走移植的回退路径）。
1.4.3 的 GPU/功率 72–96% 摆动与门开关同步（约 0.9 s 一次）。

不能证明：屏幕上任何一帧的实际显示时刻；提供方内部 GPU 成本（XeSS 提供方工作不在
我们的 GPU 计时里，`gpuFgBatchP95=0`）；肉眼画质差异。

### 2.2 DLSS，RTX 5070，revision 3（`bounded-repair-20260922/matrix-final`）

| 指标 | 原生 4K+NR 2X | 原生 4K+NR 6X | 1080→4K SR+NR 6X |
| --- | ---: | ---: | ---: |
| 提交/s（trace 留存段） | 120.1 | 287.5 | 148.5 |
| Present 返回间隔 p95 / p99 / max（ms） | 8.76 / 8.88 / 9.10 | 14.50 / 16.79 / 16.98 | 16.92 / 17.10 / 17.40 |
| 间隔 >16.667 ms 次数 | 0 / 1169 | 32 / 1542 | 177 / 1150 |
| 每组有效生成帧分布 | 1×585 | 5×244，0×80 | 5×137，0×329 |
| 零生成连续组长度 | — | 全部为 1（5,5,5,0 模式） | 2×68，3×59，4×2 |
| 准入拒绝 / 总对 | 1 / 1573 | 388 / 1624 (24%) | 1167 / 1648 (71%) |
| 预热（播种）评估次数 | 2 | 384 | 1173 |
| 每对 GPU 串行成本（P95 相加） | NR 6.7 + FG 2.9 + 流 1.2 ≈ 11.3 | NR 7.0 + FG 10.2 + 流 1.4 + 色/残 0.7 ≈ 19.3 | SR 5.2 + NR 6.8 + FG 10.6 + 流 1.3 + 0.5 ≈ 24.4 |
| 准入预测值（日志） | 11.5 | 19.2 | 23–24 |
| presentCpuP95 | 0.32 ms | 0.31 ms | 0.36 ms |
| GPU 均值 / 功率 | 61% / 174 W | 90% [76–94] / 231 W | 91% [69–94] / 241 W |

能证明：DLSS 路径的 Present 不阻塞 owner（独立呈现队列有效）；6X+NR 每对 GPU 成本
超过源周期，准入拒绝率与超出量一致；空档以"整组为 0"的形式出现，且在原生 6X 下
呈稳定的每第 4 组一次；2X 在提交层面均匀。

不能证明：NGX 内部是否可以更快；其他显卡的比例；屏幕呈现。

### 2.3 链路其他环节

| 环节 | 证据 | 结论 |
| --- | --- | --- |
| 解码 | 所有运行 decodeP95 ≤ 0.8 ms（硬解） | 非瓶颈 |
| 命令槽环 | slotWaits=0（16 槽） | 非瓶颈 |
| NVOF | `NvOfSession::execute` 只做 CPU 提交，输出用队列侧 `Wait`（`EnhanceGraph.cpp:1636`） | 无 CPU 等待 |
| 上传 | CPU 上传路径已优化（P010 0.93 ms） | 本轮工况为硬解，无 CPU 上传 |
| 源时间线 | XeSS SR 场景 `timeline` 断点 0 次、`scene` 断点 0 次 | 270 次 epoch 全部来自预览跳帧的软断点 |
| 音频时钟 | playbackSpeed 0.95–1.07 | 媒体钟正常 |
| 日志 | `Log.cpp:119` 锁内 fflush，250 ms/64 KiB/警告触发 | 不是 15 s 计时器；owner 线程写日志会短暂持锁，量级毫秒 |
| HDR 查询 | 仅 HDR 输入或 Video HDR 开启时每 2 s 一次 | 本轮 SDR 工况不触发 |

## 3. 根因分析

### 3.1 XeSS：owner 线程被提供方 Present 串行化 + 零 frameRenderTime 正反馈（置信度：高）

代码事实：

- 单 owner 线程顺序做：`beginSourceInput()`（内部 `xellSleep`，`XessPresenter.cpp:210`，
  `bLowLatencyMode=1` 见 `:159`）→ 读源 → `graph.process` → 调度器 `advance` → 在
  step lambda 里直接调 `presenter.present()`（`EngineController.cpp:1358`）→ 提供方
  `Present`（`PresentSink.cpp` `present()`）。`LiveGpuScheduler` 的任务在 owner 线程
  执行（`LiveGpuScheduler.h`），Present 内的阻塞不会让出线程。
- Intel 文档（`xess_fg_developer_guide_english.md` L151–153）：非 Intel GPU 上节奏由
  提供方的高优先级后台线程和内部高优先级 direct queue 完成。trace 显示该
  `Present` 在 4X 连续帧时阻塞 p50 27 ms，与"等待上一组 3 张生成帧按 8.5 ms 间隔
  发完"（3×8.5≈25.5 ms）一致；原生场景阻塞 12.4 ms = 3×4.17 ms。
- `VideoPresenter.cpp:172–183`：默认 `frameRenderTime=0`。Intel 文档 L887：非 Intel
  GPU 上此值用于"sanity-check presentation pacing"，为 0 是允许的，但字段将弃用。
  提供方在 0 时只能用自己观察到的 present 间隔估计周期。日志 `xess-pacing` 显示
  提供方周期环中位数在 SR 场景从 15.8 ms 递增到 21.5 ms，而原生场景保持 15–15.7 ms。
- 正反馈：owner 在 Present 内被阻塞 → 下一源帧读取推迟 → 提供方看到更长的
  present 间隔 → 组内间隔拉大 → 下一次 Present 阻塞更久。原生场景每对 GPU 成本
  约 10 ms，循环收敛在 60 Hz；SR 场景多出约 6–7 ms GPU 和排队（gpuReady 31 ms），
  循环收敛在 39 源帧/s、8.5 ms 组内间隔。

用户反馈对应：#2 重负载更易发生；#5 GPU 未满（72%）却卡顿；#6 轻负载（原生）较好。

`VEYRA_TEST_XESS_SOURCE_TIMING` 的 39.8→52.3/s 收益正是打断这个反馈：提供方拿到真实
16.67 ms 周期后不再放大组内间隔。它没到 60/s，因为 Present 仍阻塞 owner 约 12.5 ms
加 XeLL sleep。

Magpie（固定提交 3841698）的对照：效果线程与呈现线程分离；XeSS 优先用相邻已接受
帧的真实时间戳作 `frameRenderTime`。这两点与本节根因一致，不代表 Magpie 整体更稳。
XeLL 文档（`xell_developer_guide_english.md`）明确"应用限帧器与 XeLL 同时开启不建议"；
Veyra 文件播放的媒体时钟等待就是应用限帧器。

### 3.2 预览跳帧 → 全历史重置（置信度：机制高；是否即用户所见闪烁，中）

- `EngineController.cpp:1024` 在媒体钟落后一个源周期以上时丢弃已解码候选
  （`previewCandidateExpired`），`:1055–1058` 把它记为 `temporalBreak` → `historyReset`。
- `graph.process(frame,pts,historyReset,...)` 收到 `reset=true` 后：`EnhanceGraph.cpp:1310`
  清 `prevValid_`/场景/亮度历史；`:1704` `++epoch_`；`:1816` NR `kReset=1`；`:1921`
  `presentMotionValid_=false` → `VideoPresenter.cpp:172` XeSS `resetHistory=1`；`:1928`
  DLSS `resetFg=true`（整组只做 1 次播种，不出有效生成帧）。
- 当前 SR XeSS 运行：190/762 呈现原帧是重置帧（25%），全部由 2–3 帧跳过触发；
  没有场景切换或 PTS 断点。每个重置帧：XeSS 不插帧（用户看到 1 张原帧后接下一组
  3 张生成帧，节奏不连续）；NR 时间域历史清零后再重建（纹理/噪声形态在两三帧内
  变化）；DLSS 路径则整组空档。
- 达到目标帧率时没有跳帧，没有重置，闪烁消失——与用户反馈 #4 完全一致。

这是"待验证假设"，需要 §6 的 C 实验用像素证据确认。其中 NR 重置是亮度/纹理闪烁的
候选，XeSS/DLSS 重置是节奏闪烁（顿挫）的候选，两者要分开验。

### 3.3 DLSS 4X/6X + NR：性能上限 + 整组丢弃形成周期性空档（置信度：高）

每对 GPU 串行成本 19.3 ms（原生 6X）/ 24.4 ms（SR 6X）> 16.67 ms。SR→NR→FG 有数据
依赖，无法并行；NVOF 可重叠部分 <2 ms，已被实验证伪无收益。准入按预测拒绝 24%/71%
的对，每次拒绝仍做 1 次播种评估（384/1173 次，每次约 2 ms，输出不呈现，但为下一组
保住历史所必需）。结果是 6X 原生下稳定的"5,5,5,0"模式：每 4 个源帧一个 16.7 ms 空档
（约 15 Hz 顿挫），用户感知比"均匀 240 FPS"差得多，而 UI 读数 287。

这不是调度错误：预测值 19.2 与实测一致，拒绝的对确实来不及。任何不降低每对成本或
不改变失败形态的调度改动都不会改善，此前多轮实验已证明。

### 3.4 显示端（置信度：无法判定，缺测量）

三版都是 `vsync=0`、`DXGI_PRESENT_ALLOW_TEARING`、flip-discard 三缓冲
（`PresentSink.cpp`）。240–290 提交/s 在 60/144/165 Hz 显示器上只有一个子集被扫描出；
子集是否均匀取决于提交时序。日志没有记录显示器刷新率（`displayRefreshFps` 存在但仅
在"跟随显示器"限帧时调用）。用户 #1 "读数高却卡顿"与此相容，但没有证据。

### 3.5 间歇卡顿与功耗波动（置信度：低，未复现）

30 s 运行中未见 >100 ms 的间隔。1.4.3 的功耗 207–226 W 摆动与 suppress/resume 门
同步（0.9 s 周期），current 已无此摆动（181–188 W）。用户所述"约 15 s"不是固定周期，
本轮没有数据；候选：HDR 2 s 查询（仅 HDR）、音频端点恢复、日志 flush、DWM/系统。
需要 §6 的长时运行加异常前后环形 trace 导出才能归因。

### 3.6 采集路径（未测，推断）

采集 + XeSS 走同一 owner，机制同 §3.1：owner 阻塞 → mailbox 覆盖（Drop）→
`breaksHistory(Drop)` 触发 `temporalBreak` → 同 §3.2 的全重置。5090 日志（4K P010 60、
原生 NR、DLSS 2X）显示 mailbox 丢源和历史重置，与此一致，但没有 XeSS 采集 trace。

## 4. 按优先级的发现与修复

### F1. XeSS：提供方 Present 与 XeLL sleep 串行化 owner 线程（可修）

- 证据与位置：§2.1、§3.1；`EngineController.cpp:938,1358`，`XessPresenter.cpp:159,210`，
  `VideoPresenter.cpp:172–183`，`LiveGpuScheduler.h`。
- 根因置信度：高。
- 最小修复（分三步，逐步验证，每步独立 checkpoint）：
  - **X1 真实源周期作 `frameRenderTime`**：把 `VEYRA_TEST_XESS_SOURCE_TIMING` 的逻辑
    改为默认行为（条件不变：历史有效、身份连续、PTS 递增、0.125–500 ms；其余传 0）。
    不是新方向，是已有收益的候选转正。
  - **X2 文件播放关闭 XeLL 低延迟等待**：`bLowLatencyMode=0`（仍按文档调用
    `xellSleep`/markers，仅改 `xellSetSleepMode`）。先用环境变量做对照。采集路径另测。
  - **X3 呈现线程解耦**（结构性，仅在 X1/X2 后仍不到 60/s 时做）：present-sink 后端
    （XeSS/FSR）的 `presenter.present()` 移到独立呈现线程。显式合同：
    1. owner 提交图后，用共享 fence 值 `F_ready(parity)` 交接；呈现线程在同一 direct
       queue 上 `Wait(F_ready)`，用自己的 allocator ring 录制 blit + tag，再 `Present`。
    2. `Present` 返回后呈现线程 `Signal(F_consumed)`；owner 复用该 parity 前 GPU 侧
       `Wait(F_consumed)`（复用现有 `presentationFences_` 路径，`EnhanceGraph.cpp:1329`）。
       `XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT` 要求 tag 到 Present 之间无写入，仍满足。
    3. 最多 1 个 Present 在飞；调度器 step 返回 `Pending` 直到呈现线程回报完成。
       不加缓冲、不加队列深度（occupancy 仍为 2）。
    4. XeLL markers：SIMULATION/RENDERSUBMIT 在 owner，PRESENT_START/END 在呈现线程；
       Intel 文档允许多线程调用（`Multithreading` 节），需实测 XeLL 对跨线程 marker 的
       返回值。
    5. resize/seek/后端切换/退出：先 join 呈现线程再 drain，复用现有 backend-switch
       和 lifecycle 测试。
    与被否决的"简单拆线程"的区别：显式 fence 合同、单飞、无新队列、复用 DLSS 已验证的
    生产者/消费者 fence 路径，并有 §2.1 的新定量证据。
- 风险：X1 低（字段仅做 sanity-check）；X2 低（可能略增采集延迟，仅文件启用）；
  X3 中（D3D12 多线程、提供方线程安全、生命周期）。
- 验证：同 `fg-utilization-matrix.py` 两工况各 30 s 交错 A/B，收益后 120 s。判定：
  源提交 ≥ 58/s 且 suppress 为 0；epoch 变化 ≤ 5/30 s；组内间隔 4.0–4.5 ms；
  Present 阻塞 p50 < 5 ms（X3）；`fgInvalid`、像素对应检查不退步；帧龄不增。
- 停止/回退：任一步源率下降、`frameGenResult<0`、D3D12 错误、组内间隔 <1 ms 占比上升、
  帧龄 P95 增加超过一个源周期，则撤回该步。

### F2. 预览跳帧触发全历史重置（可修；闪烁的首要待验证假设）

- 证据与位置：§3.2；`EngineController.cpp:1024,1055–1058`，`EnhanceGraph.cpp:1310,
  1704,1816,1921,1928`，`VideoPresenter.cpp:172`。
- 根因置信度：机制高，视觉归因中。
- 最小修复：区分"硬断点"和"有界跳帧"。跳过 ≤2 个源帧、无场景切换/PTS 断点/Drop 时：
  不设 NR `kReset`，不设 XeSS `resetHistory`，不设 DLSS `resetFg`；光流本就在实际处理的
  相邻两帧间计算，`prevPtsMs_` 保持真实 PTS，生成帧 PTS 按真实 A/B 插值（已如此）。
  epoch 仍递增（标记诊断），但不触发历史清零。超过 2 帧仍按现状重置。先做环境变量
  对照，通过 C 实验再转正。
- 风险：跨 2–3 帧的运动向量更大，快速运动下拖影可能增加；场景切换检测仍有效。
- 验证：§6 C 实验（像素级）。判定：静态灰块在重置/非重置帧间的亮度标准差下降；
  运动图案位置误差不劣化；主观 A/B 无新增拖影。
- 停止/回退：位置误差诊断（已有 6X 门槛）退步，或拖影可见，则限制到 ≤1 帧或撤回。

### F3. DLSS 4X/6X + NR 超过每对 GPU 预算（性能上限；失败形态可商议）

- 证据与位置：§2.2、§3.3；`FgRecoveryBudget.h` `admitFile`，`EnhanceGraph.cpp:1928–1944`。
- 置信度：高。
- 可做的：
  - 停止对该工况的调度实验（既有多轮反证）。
  - UI 已有 `fgBudgetLimited`；建议把"每对预算/实测"数字暴露在诊断面板，让用户看到
    是上限而不是 bug。
  - **需用户决定的策略项**：拒绝的对当前输出 0 张生成帧（16.7 ms 空档）。替代方案是
    该对透明地按"能承受的子帧数"评估（例如 2X 一组），保持历史连续，UI 明示实际倍率。
    这不是"偷偷降倍率"，但确实改变用户选定倍率的执行方式，须用户明确授权。前置条件：
    用 `tools/fg_harness` 验证 NGX MFG 在不重置的情况下逐对改变 `multiFrameCount`
    是否产生有效输出（AGENTS.md 要求不猜 NGX 参数行为）。
- 风险：策略项未授权前不做。
- 验证：harness 逐对 5/1 交替，检查 `outputDisableInterpolation` 与图像。
- 停止条件：harness 显示需要重置，则该策略作废，仅保留诊断展示。

### F4. 显示端呈现子集未测量（测量缺口）

- 最小补充：打开会话时记录 `displayRefreshFps`；每秒记录 `IDXGISwapChain::GetFrameStatistics`
  的 `PresentCount`/`PresentRefreshCount`/`SyncRefreshCount` 差分，得到"每次刷新被显示
  的提交数/被丢弃的提交数"。不需要管理员权限（PresentMon 曾因权限失败）。XeSS 代理
  交换链是否转发该接口需实测；不转发则记录"不可测"。
- 判定：同一工况下若丢弃分布不均（例如连续刷新周期内显示数 0/2/0/2），则证明用户
  #1 的机制；若均匀，则排除显示端。
- 风险：无产品行为变化。

### F5. 间歇卡顿 / 功耗波动（怀疑，未复现）

- 最小实验：一次 300 s 运行（单次上限），SR+NR+XeSS 4X 与原生+NR+DLSS 6X 各一次，
  telemetry 200 ms 采样，环形 trace（8192 条）在任一 Present 间隔 >100 ms 时导出一次
  快照（利用现有 `frameTraceSize`/snapshot），不逐帧写盘。
- 判定：把长间隔与 telemetry 功率/频率、`engine-stall` 分段、音频恢复、HDR 查询对齐；
  只有对齐后才做点修复。
- 当前不做任何代码改动。

### F6. 采集 + XeSS（推断，未测）

- F1 的三步完成后在 VC-007PRO 1440p60/4K30 复测；判定同 F1，并加 mailbox 覆盖计数。

### 明确不做

- 恢复 XeSS suppress/resume 门；取消 DLSS 准入；增加输出缓冲或固定等待；
  自动/静默降倍率；在没有 F4 证据前宣称"均匀呈现"。

## 5. 实施顺序与 checkpoint

1. 存档 tag 后，F4 测量补充（无行为变化）先进入，让后续每步都有显示端数据。
2. F1-X1（30 s A/B → 120 s）。
3. F1-X2（同上，文件路径）。
4. F2（环境变量对照 + C 实验），与 X1/X2 无耦合，可并行准备测试素材。
5. F1-X3 仅在 X1+X2 后源率仍 <58/s 时启动，独立分支。
6. F3 策略项等用户决定；harness 实验可先做。
7. F5 长时运行在 X1/X2 之后跑一次，避免把已知阻塞混入。

每步：一次最小实现，最多一次有证据修订，无收益即撤回并记录到实验索引。

## 6. 最小区分性实验

| 编号 | 变量 | 固定项 | 测量 | 判定 |
| --- | --- | --- | --- | --- |
| A | X1 开/关 | derived1080 + SR3 + NR + XeSS4，全屏 1440，30 s 交错 ×2 | 源/s、epoch、组内间隔、Present 阻塞、frameGenResult | 源 ≥52 且 epoch 下降视为通过，否则撤回 |
| B | X2 开/关（在 A 通过配置上） | 同 A | XessSleep 累计、源/s、帧龄 | sleep p95 <2 ms 且源不降 |
| C | F2 开/关；NR 开/关 | 合成素材：静态灰块 + 匀速运动条 + 每 3 s 一次人工欠速（`VEYRA_TEST_VIDEO_WORK_MS` 注入） | 抓帧（诊断运行）：灰块亮度逐帧 σ、运动条位置误差、重置帧数 | F2 开时 σ 下降且位置误差不升；NR 关时 σ 若已低则闪烁归 NR 重置 |
| D | 显示刷新率 60 vs 最高（同一显示器切模式） | 原生+NR+DLSS6 | GetFrameStatistics 丢弃分布、主观 | 分布不均则 F4 成立 |
| E | fg_harness multiFrameCount 5/1 交替 | 固定素材 | 有效标志、图像 | 决定 F3 策略是否可行 |

不测的：只跑平均 FPS 的重复矩阵、不加变量的组合穷举。

## 7. 对用户六条反馈的对应

| 反馈 | 对应 |
| --- | --- |
| 1 读数高却卡顿 | DLSS 6X 每 4 组一个 16.7 ms 空档（§3.3）；XeSS 组内 8.5 ms 加组间空档（§3.1）；显示子集未测（F4） |
| 2 重负载更易，低倍率也有 | §3.1 阈值行为；低倍率在更慢显卡上同样触发 §3.2/§3.3，本机未复现 |
| 3 间歇卡顿、功耗波动 | 1.4.3 门开关造成 0.9 s 功率摆动；"15 s"未复现（F5） |
| 4 欠速闪烁、满速消失 | §3.2 跳帧全重置（F2，待像素验证） |
| 5 GPU 未满也发生 | XeSS：CPU 侧串行化（F1）；DLSS 6X：已 90%，是上限（F3） |
| 6 轻负载较好但未证均匀 | DLSS 2X 提交层面 p99 8.9 ms 均匀；显示层面未测（F4） |

## 8. 本轮产物

- 本文档；`E:/项目/Veyra/tests/fg-independent-review-20260922/trace-analysis.json`。
- 未修改产品代码、测试脚本、便携包；未构建、未提交、未推送。
