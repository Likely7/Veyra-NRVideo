# 采集卡延迟：还能降多少，从哪里降

日期：2026-09-22。基线 `codex/fg-independent-repair-20260922` `d9baf27`。只读源码核对加
既有实卡证据（VC-007PRO，RTX 5070，2026-09-18 各 120 s 测试），未运行新测试。
本文只谈软件段：采集回调进入 → 呈现 `Present` 返回。HDMI 到卡、卡内部/USB、
Present 之后到屏幕扫描三段未测量，本文不能宣称降低了这些。

## 1. 当前延迟账（实测，ms，P50）

| 段 | 4K30 NV12，NR 实时 1080，DLSS 4X | 1440p60 同设置 | 来源 |
| --- | ---: | ---: | --- |
| 回调进入 → 复制完成（锁内 memcpy） | 1.01 | ~0.7 | `stages.md` callback_copy_lock |
| 复制完成 → owner 读到 | 0.01 | 0.01 | mailbox_until_read |
| 读到 → 图提交完成（CPU） | 2.06 | ~2 | graph_cpu_submission |
| 提交 → 原帧 GPU 就绪（颜色 0.2 + 光流 1.0 + NR 6.1 + 残差 0.2 + 3 张 FG 5.9） | 7.2 | ~6.6 | real_submit_to_ready |
| **原帧就绪 → 原帧 Present 开始（等待）** | **29.5**（旧策略 31.9） | **15.9** | real_ready_to_present_begin |
| Present 调用 | 0.22 | 0.20 | real_present_call |
| **回调 → 原帧 Present 返回** | **40.2** | **25.4** | real_callback_to_present_return |
| 回调 → 第 1 张生成帧 Present | 15.2 | — | subframe_1_callback_to_present |
| 无 FG 时回调 → Present 返回 | 约 10.5（=回调到就绪+0.2） | 约 9.5 | 由 paceSourcePts=false 推出，见 §2.1 |

**结论：开 FG 时，40 ms 里有 29.5 ms 是软件主动等待，不是处理耗时。** 关 FG 时软件延迟
约 10 ms，几乎全部是 GPU 处理。

## 2. 每一段的成因与可降空间

### 2.1 无 FG 路径（约 10 ms）：已接近下限，只剩约 1–2 ms

`EngineController.cpp:1292` `paceSourcePts=options.fg||!physicalCapture` → 无 FG 的实卡
`deadline()` 返回 `host_`（`PresentationScheduler.h:20`），就绪即呈现。剩余成本：

| 项 | 现状 | 可降 | 做法 |
| --- | --- | --- | --- |
| 回调锁内 memcpy 4K NV12 12 MB（`CaptureCardSource.cpp:444`） | 1.0 ms 且阻塞 `tryRead` | 0.3–0.5 ms | 回调只在锁内交换指针，memcpy 移到锁外（三帧轮转：驱动写、我们复制、owner 读） |
| owner 唤醒粒度（`waitLive` `slice(1ms)`，`DeadlineWait.h:16`） | 每次等待最多 1 ms 过冲 | 0.3–0.5 ms | 采集回调 `wake.notify_one()` 已存在，但 owner 在 `waitLive` 用的是定时器不是该条件变量；把等待改为"条件变量或截止时间，先到先醒" |
| 第二次拷贝：mailbox AVFrame → upload 堆（`EnhanceGraph.cpp:1507`） | 约 0.9 ms（已优化后的 P010 值） | 0.5–0.9 ms | 回调直接写入 upload 堆的三缓冲（write-combined，只顺序写），跳过 AVFrame 中转 |
| NR 6.1 ms | 算法成本 | 0 | 不动 |
| 光流 1.0 ms、颜色 0.2 ms | 算法成本 | 0 | 不动 |

合计可降约 1–2 ms，从约 10 到约 8–9 ms。风险低，改动局部。

### 2.2 FG 路径（约 40 ms）：29.5 ms 等待的构成

`EngineController.cpp:1098–1102`：

```
pairDelay = LivePairLatency.select(...)   // 自适应，下限 = 实测 max(ready_i − arrival_B + pts_B − pts_i) 的 P95 + 1 ms
legacyPairDelay = liveInterval + processingAllowance   // 一个源周期 33.3 + baseCost P95(≤33.3)
```

B 帧到达后，B 的呈现时刻被安排在 `arrival_B + pairDelay`，三张生成帧按 PTS 均匀排在
B 之前。4X 时 B 之前要放 3 张，所以 B 至少延后 3/4 周期 = 25 ms，加上第 1 张生成帧
必须先算好（回调后 13.1 ms 就绪）。当前自适应结果 29.5 ms = 25 ms 相位 + 约 4.5 ms
余量（P95 + 1 ms 唤醒 + 自适应每步 0.25 ms 的收敛滞后）。

**这 25 ms 是"插值帧要显示在 A 和 B 之间"的几何结果，不是可优化的浪费。** 要显示
A→B 之间的 3 张帧，B 必须等它们显示完。唯一能压缩的是：

| 项 | 现状 | 可降 | 做法 | 风险 |
| --- | --- | --- | --- | --- |
| 自适应余量 | P95 + 1 ms，8 批起步，每批最多前移 0.25 ms | 1–2 ms | 余量改为 P95 + 0.3 ms（唤醒改条件变量后可行）；收敛步长放宽到 0.5 ms | 余量不足时生成帧过期，已有过期计数可监控 |
| 第 1 张生成帧的就绪时刻 13.1 ms | NR 6.1 + 光流 1.0 + FG1 2.6 + 提交 2 + 回调 1 | 1–3 ms | 2.1 节的三项 + FG1 与 NR 无法并行（依赖） | 低 |
| 提交间隔抖动导致的 P95 尾巴 | present_interval P95 8.7 vs P50 8.36 | 0.3 ms | 同上唤醒改进 | 低 |

FG 路径合计可降约 2–4 ms，从 40 到 36–38 ms。**不可能降到 20 ms 级别，除非改变
呈现语义**（见 §3）。

### 2.3 XeSS 路径

`legacyPairDelay=0`（`:1101`），Veyra 不加相位；XeSS 提供方内部持有当前帧直到下一帧
到达才插值，本身就是一个源周期的滞后（Intel 文档"Frame generation delays presentation
of the application frames"）。加上本日 X1 修复前的 Present 阻塞，XeSS 采集延迟应该
与 DLSS 同量级，但没有实卡测量。2.1 节的改动同样适用于 XeSS 输入侧。

## 3. 需要用户决定的语义选项（能大幅降但改变产品行为）

| 选项 | 延迟变化 | 代价 | 判定 |
| --- | --- | --- | --- |
| A. FG 下"外推"而非"插值"：用 A、B 预测 B 之后的帧，B 到达即显示 | 从 40 降到约 12–15 ms | DLSSG/XeFG 都是插值 API，不支持外推；需要自己写运动补偿外推，画质在快速运动下明显下降，且与 AGENTS.md "不用重复/线性混合冒充 DLSSG" 冲突 | 不建议 |
| B. FG 下 B 到达立即显示 B，把生成帧显示在 B 之后（即插值帧"回放"过去） | 原帧延迟降到约 12 ms，但画面时间线倒退，运动出现来回抖动 | 视觉上不可接受 | 不做 |
| C. 采集默认"低延迟档"：NR 关或 NR 降到 720 内部，FG 关 | 约 4–6 ms（NR 6.1 ms 是最大项） | 画质换延迟，用户已可手动选择 | 已是用户可选项，不改默认 |
| D. 2X 而非 4X | B 相位从 3/4 周期降到 1/2 周期：40 → 约 32 ms | 用户可选 | 已可选 |

## 4. 未测量但可能存在的段

| 段 | 现状 | 建议 |
| --- | --- | --- |
| Present 返回 → 屏幕 | `vsync=0`、允许撕裂、flip-discard 三缓冲、`SetMaximumFrameLatency(3)`（`PresentSink.cpp:454`，默认 pacing 关时为 3） | 默认 3 帧队列深度在 GPU 未饱和时不会积压，但没有测量。低排队模式改为 1 已存在（帧同步开关），实卡数据显示低排队与关闭 P50 差 0.2 ms，说明当前没有积压。建议用本日新增的 `display-stats` 长测确认 `notDisplayed` 分布 |
| 专业模式视频子窗口 `SetWindowRgn` 圆角（`AppShell.cpp:358`） | 带 region 的子窗口会让 DWM 走重定向合成而不是独立 flip | 可能多 1 帧合成延迟（约 8–16 ms）；这是所有段里最可能被忽视的一项。验证：全屏（无 region）与专业模式各测 `display-stats` 与 PresentMon 的 `PresentMode`（Composed 还是 Hardware Independent Flip） |
| 字幕/保护 overlay 为 `WS_EX_LAYERED` 子窗口（`SubtitleOverlay.cpp:58`） | 分层子窗口覆盖在视频上同样会阻止独立 flip | 只在有字幕时存在；同上验证 |
| 卡内部/USB | 未测 | 需要外部计时器（LED + 高速相机）或带时间码的信号源；软件无法测 |
| 线程优先级 | 全库无 `SetThreadPriority`/MMCSS | 采集回调（DirectShow 线程）与 owner 线程都是普通优先级；重负载 UI 重绘或系统活动可抢占。加 MMCSS "Pro Audio"/"Games" 到 owner 与回调线程是低风险、可能收回 0.5–2 ms 尾部的改动 |

## 5. 建议的最小方案（按收益/风险排序）

1. **验证 DWM 合成模式**（0 代码，最高潜在收益）：专业模式（圆角 region）与全屏各跑
   30 s，用 PresentMon（若权限允许）或本日 `display-stats` 看 `refreshesWithoutNewFrame`；
   若专业模式是 Composed，把圆角改为在 blit 着色器里画（去掉 `SetWindowRgn`），能省
   一帧合成延迟。
2. **owner 唤醒改条件变量**（约 0.5 ms，低风险）：采集回调已 `notify_one`，让
   `waitLive` 同时等条件变量与截止时间。
3. **回调锁外复制 + 直写 upload 堆**（约 1–1.5 ms，中风险）：三缓冲 upload 堆，回调
   写、owner 读、GPU 复制各占一个；去掉 AVFrame 中转。与清扫方案 C3 合并。
4. **MMCSS 线程特征**（0.5–2 ms 尾部，低风险）：owner 与采集回调线程注册
   `AvSetMmThreadCharacteristics("Pro Audio")`，失败即忽略。
5. **自适应余量收紧**（1–2 ms，中风险）：`LivePairLatency.h:29` 的 `+10000`（1 ms）
   改为 `+3000`，步长 `2500` 改 `5000`；以 `generatedExpiredAfterEval` 不上升为门槛。

预计总收益：无 FG 约 2–3 ms（10 → 7–8），FG 4X 约 3–5 ms（40 → 35–37）。若第 1 项
证实专业模式在 Composed 模式，再额外减一帧合成延迟。

每项一次 30 s 实卡 A/B（`test-capture-version-comparison.ps1` 已有），判定指标：
`real_callback_to_present_return` P50/P95 下降，`generatedExpiredAfterEval`、
`mailboxOverwritten`、`slotWaits` 不上升。

## 6. 不做的事

- 不减少 FG 倍率、不关 NR 来"降延迟"；这些已是用户选项。
- 不做外推或时间线倒退的呈现。
- 不把 Present 返回当作屏幕延迟；HDMI 输入端到屏幕的端到端只能用外部计时。

## 附：2560×1440 60p NV12 软件内部延迟实测（2026-09-22，交付构建）

设备 MCS 4K--T800，`capture:0:4:-1`（device 0 / format 4，`SetFormat nativeIndex=4
subtype=0x3231564E`，`bytes=5529600` = 2560×1440×1.5，确认 NV12 1440p）。
每档 25 s、跑 2 轮，数据 `b12/capture-2k60/`，日志 `logs/.../capture-2k60.log`。

**测量口径**：DirectShow 回调拿到采集样本 → `Present()` 返回。
**不含**采集卡自身延迟、USB 传输、驱动排队（都发生在回调之前），
**也不含** `Present()` 之后的扫描输出。所以这是软件内部这一段，不是端到端到眼睛。

| 配置 | 回调拷贝 | 读取时龄 | CPU 处理 | GPU 执行 | Present | 配对等待 | **合计 P95** | 最差 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 不开增强 | 0.39 / 0.34 | 0.40 / 0.36 | 0.48 / 0.49 | 1.21 / 1.09 | 0.40 / 0.35 | 0.001 | **2.34 / 2.26** | 2.51 / 3.00 |
| 仅降噪 | 0.41 / 0.36 | 0.41 / 0.38 | 1.39 / 1.32 | 7.95 / 7.89 | 0.41 / 0.38 | 0.001 | **9.90 / 9.81** | 10.86 / 10.98 |
| 降噪 + 补帧 2X | 0.39 / 0.37 | 0.38 / 0.39 | 1.76 / 1.63 | 10.02 / 9.80 | 0.39 / 0.35 | 9.96 | **21.93 / 22.33** | 26.46 / 26.60 |
| 降噪 + 补帧 4X | 0.41 / 0.35 | 0.48 / 0.51 | 2.22 / 1.86 | 9.88 / 10.03 | 0.37 / 0.32 | 13.52 | **26.03 / 26.08** | 27.44 / 26.70 |

全部 8 轮 `callbackFps=59.94`、0 ERROR；不开增强 dropped=0，其余各 1–2 帧（启动期）。

三条结论：

1. **软件自身的固定开销约 2.3 ms**，其中回调拷贝 0.35–0.41、邮箱读取时龄 0.36–0.51、
   CPU 处理 0.48、Present 0.35–0.40，GPU 仅做颜色与 blit 约 1.1–1.2。
   即"搬运"这条路已经很薄，继续优化拷贝的空间见 `UNIFIED_REPAIR_EXECUTION` §3d
   （实测上限 0.43 ms）。
2. **降噪加约 7.5 ms，全部是 GPU 时间**（`gpuNrP95` 6.6–6.8）。这是模型推理成本，
   不是调度问题。
3. **补帧加的是"配对等待"，不是处理时间**：`schedulingWaitP95Ms` 从不开补帧的
   0.001 跳到 2X 的 9.96、4X 的 13.52，而 GPU 执行只从 7.9 升到 9.9。
   要在 A 与 B 之间插帧，必须等 B 真正到达，60 fps 下一个源间隔就是 16.67 ms。
   这是补帧的固有代价，不是可以"优化掉"的开销（见 AGENTS.md 对 lookahead 的规定）。

**给用户的取舍**：要最低延迟就关补帧（2.3 ms）；要画质又要低延迟就只开降噪（9.9 ms）；
开补帧则 2X 约 22 ms、4X 约 26 ms，多出来的十几毫秒买的是插出来的中间帧。
