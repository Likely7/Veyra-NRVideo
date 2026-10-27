# 2026-09-16 采集卡延迟全面排查（只排查，未改产品代码）

用户："优化采集卡延迟问题，甚至整个播放器链路都要优化……找出我们延迟低的原因和延迟高的原因，
再给优化方案……不要擅自动手，先仔细排查"。

本轮**没有修改任何产品代码**，只做了代码审计 + 本机实测 + 历史日志/文档交叉验证。

## 1. 本机实测（今天，RTX 5070 / 驱动 32.0.16.1656）

### 1a. 实卡 A/B（用户释放采集卡后补测，同一路信号、同一会话）

采集卡 `USB3 Video`（VID_345F，圆刚类设备）。格式索引见 §3.1；`capture:<设备>:<格式>:-1:0`
表示禁用采集音频。每次 12 秒窗口，指标来自 `[capture-timing]`：

| 配置 | callbackFps | callback→Present 返回 p95 | processCpu p95 | gpuReady p95 | 丢帧 |
| --- | --- | --- | --- | --- | --- |
| **YUY2 1080p60（原生直连）** | 60.01 | **2.640 ms** | 0.412 ms | 0.637 ms | 0 |
| **MJPEG 1080p60（兼容解码）** | 60.02 | **4.121 ms** | 2.160 ms | 0.576 ms | 0 |
| YUY2 3840x2160@18 | 18.00 | 3.889 ms | 1.186 ms | 0.873 ms | 0 |
| MJPEG 3840x2160@18 | 18.02 | **11.011 ms** | **7.036 ms** | 1.052 ms | 0 |
| YUY2 1080p60 + NR+SR | 60.01 | **13.121 ms** | 1.600 ms | 10.449 ms（Sr 3.16 / Nr 6.35 / Flow 1.18） | 0 |
| YUY2 1080p60 + NR+SR+FG 2X | 60.01 | **17.467 ms** | 1.845 ms | 12.971 ms（FgBatch 2.84） | 0 |
| YUY2 1080p60 + 采集卡 WASAPI 音频 | 60.01 | **2.644 ms** | 0.426 ms | 0.714 ms | 0 |

带音频那次 `[live-audio-sync]` 全程 `compensationMs=0.000`（`endpointMs=22.0`、
`audioIngressMapMs=17.0`、`skewMs≈-43…-50`），说明这套配置下自动 A/V 补偿**没有**给视频加等待；
`[present]` 也确认 `vsync=0 tearing=1`。

### 1b. 虚拟摄像头（同一代码路径，NV12 1440p60）

（首次排查时实卡被 OBS 独占，用 OBS 虚拟摄像头测的我们自己的管线开销）

用 OBS 虚拟摄像头（NV12 2560x1440@60，走的是同一条 DirectShow 原生路径）测到的**我们自己的
管线开销**（每档 12 秒窗口）：

| 配置 | processed fps | callbackFps | callback→Present 返回 p95 | 丢帧 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 无特效 | 60.00 | 60.15 | **12.45 ms** | 0 | 583 帧 |
| NR + SR | 60.00 | 60.11 | **24.44 ms** | 0 | processCpuP95 2.02ms |
| NR + SR + FG 2X | 59.00 | 60.09 | 20.35 ms | 0 | schedulingWait 6.0ms |
| NR + SR + `--realtime` | 60.00 | 60.08 | **12.51 ms** | 0 | 601 帧 |

历史实卡数据（`docs/CAPTURE_LATENCY_FIX_2026-09-07.md`，MJPEG 1920x1080@50 实卡）：
全关 **3.67 ms** / NR 4.77 ms / NR+FG **16.28 ms**。同一指标、同一台机器，说明"我们自己的
那段"在 1080p50 上只有约 1/4 帧。

**从 1a 得到的关键修正**：本机实测下，MJPEG 在 **1080p60 只比 YUY2 多 1.5ms**（4.12 vs 2.64），
在 **4K18 多 7ms**（11.0 vs 3.9，主要是 processCpu：系统解码 + 颜色转换 + CPU 拷贝）。
所以"MPEG = 几百毫秒"在我们这段**不成立**；我们的解码回退路径在 1080p60 上几乎不额外加延迟。

注意：`callback→Present 返回` **不是** HDMI→屏幕光子的端到端延迟（软件内部计时，UI 里也这么
标注）。端到端只能用外部方法测（§6）。

## 2. 为什么我们比 OBS / PotPlayer 低（代码证据）

1. **原生终点滤波器取代 SampleGrabber**（`src/source/NativeCaptureSink.cpp`）：自实现
   `IBaseFilter/IPin/IMemInputPin`，注释原文 "no legacy SampleGrabber orientation/VideoInfo2
   restriction, conversion filter or extra queue"，回调**借用** sample，不额外排队。
2. **`ConnectDirect` 直连、转换器为 0**：日志 `native ConnectDirect subtype=... hr=0x0
   converters=0`。OBS/PotPlayer 的同链路里常插入颜色转换/缩放/解码过滤器，每个都自带缓冲。
3. **容量 1 的 latest-frame mailbox + 双自有缓冲**：日志 `mailbox=1 ownedBuffers=2`；回调把帧
   拷进自有 AVFrame 后立刻归还驱动 sample；消费者慢时**覆盖**待取帧而不是积压。历史实卡验证：
   读者停顿 300ms 期间收到 16 次回调、覆盖 14 帧，恢复时读到的是**最新**帧（年龄 8.9ms）。
4. **等所有东西就绪再 Run**（`deferredRun=1`）：2026-09-07 修掉的"先 Run 再建 NGX 导致积压旧帧"
   （当时实测 29 帧/4 秒、单次等待 940.79ms）不再存在。
5. **不等绝对源 PTS**：无 FG 立即提交最新帧；FG 按 host-time 锚点最多等**半个输入间隔**
   （硬上限 33.33ms）。
6. **固定 System Clock**（`CLSID_SystemClock` + `SetSyncSource`），避免 DirectShow "live source"
   时钟漂移带来的隐性等待。
7. **呈现策略**：flip 模型 3 buffer + `VideoPresenter` 里 `d.vsync=false` + 支持时
   `DXGI_PRESENT_ALLOW_TEARING` → `SyncInterval=0`，不等 vblank 排队（日志 `vsync=0 tearing=1`）。
8. **增强开销是测出来的**（上表），GPU 跟不上时丢**过期帧**，不让延迟无限增长。

## 3. 为什么有一批用户延迟非常高（分层，按可能性排序）

### 3.1 采集卡内部编码（"MPEG" 那批）—— 最可能的主因

> **实测修正（见 §1a）**：在 1080p60 上，MJPEG 只让我们多 1.5ms；4K18 上多 7ms（CPU 解码+转换）。
> 所以"MPEG 让**我们**慢几百毫秒"不成立。真正会变成高延迟的机制是下面这条**背压链**：
> 卡内编码 → 我们用 CPU 解码/转换（4K 实测 7ms/帧）→ 弱 CPU 上单帧预算（16.7ms）被吃掉大半 →
> **UVC 驱动/卡侧缓冲被填满**（我们消费不过来）→ 卡继续把**旧帧**交给我们 →
> `readAgeMs` 变大、`callbackFps` 低于名义帧率 → 体感延迟累积。
> 我们的 mailbox 只能丢**自己**的旧帧，救不了卡/驱动侧的积压。**判据就是日志里的
> `readAgeMs` 与 `callbackFps`**（§4）。

同一块卡的枚举（本机 `veyra_capture_tests.exe --list`）：

```
FORMAT 0:0  1920x1080@60  YUY2   [format 0]     ← 原生路径：ConnectDirect，converters=0
FORMAT 0:22 3840x2160@18  YUY2   [format 22]
FORMAT 0:24 1920x1080@60  MJPEG  [format 24]    ← 兼容/解码路径
FORMAT 0:46 3840x2160@18  MJPEG  [format 46]
```

选到 MJPEG/H.264/HEVC（很多卡叫 MPEG）时走兼容路径，日志原文：

```
[capture] explicit RGB32 compatibility path subtype=0x47504A4D diagnostic=false
          (decoder/color converter may be inserted)
```

即 `设备 → 系统解码器 → 颜色转换器 → SampleGrabber(RGB32, CPU 拷贝) → NullRenderer`。
延迟来源：① 卡自己的硬件编码器（MJPEG 每帧独立、H.264/HEVC 有 GOP 与重排序，卡内 1~数帧）；
② 系统解码器的重排序缓冲（H.264/HEVC 默认非低延迟，可能 3 帧以上，我们没请求
`CODECAPI_AVLowLatencyMode`）；③ 多一次颜色转换与 CPU 拷贝；④ NullRenderer 按呈现时间持有
sample。合计 100–300ms 很常见，正对应"延迟非常高"。

格式默认选择逻辑（`CapturePanel.cpp`）：`restore = formats.empty() ? -1 : 0` ——
**记住上次选择，否则取列表第 0 项**。这卡第 0 项恰好是 YUY2；但不少卡把 MJPEG/H.264 排在前面，
或者用户为了 4K60 只能选 MJPEG/H.264。这就是"同一软件有人跟手、有人很黏"。

### 3.2 自动 A/V 同步补偿把视频"等"后了

`CaptureSyncTarget` 会把视频时间轴对齐到音频观测（置信带 80ms、目标上限 1500ms），
`setSync(mode,offset)` 手动偏移 ±250ms，默认 `audioSync=Automatic`。若采集音频本身晚
（卡的数字音频缓冲大、或用户选了 WASAPI 端点），引擎会**主动等视频**对齐音频 ——
这部分延迟整段加在体感上，且**不体现在 `callbackToPresentReturn` 里**。
`AudioSyncMode::Off`/Manual 能直接消掉（代价是可能 A/V 不同步或要手动校准）。

### 3.3 增强负载与 GPU 竞争

今天实测 NR+SR 让管线从 12.45ms → 24.44ms（1440p60 虚拟摄像头）；历史 1080p50 实卡只 +1.1ms。
高分辨率+高帧率+全开时我们这段会明显变长；GPU 满时走丢帧路径（不涨延迟但会顿）。

### 3.4 显示/合成与刷新率相位

flip 链 + DWM 合成：即使 `SyncInterval=0`，DWM 也在下一个合成点显示（通常再 +1 刷新）；
源 60fps 与显示器 59.94Hz/120Hz 相位漂移时会周期性多压 1 帧。OBS/PotPlayer 同样吃这份，
但独占全屏会少一层。

### 3.5 用户侧与卡固件侧

USB 降速/走 Hub/线材差 → UVC 重传（表现为 `callbackFps` 波动、`captureDropped>0`）；
卡的"低延迟模式"未开（部分卡叫 Instant Gameview）；主机/电视的后处理（电视游戏模式未开）
——这部分我们看不到但用户会算到我们头上。

## 4. 三行定位法（让反馈用户直接给结论）

1. `[capture] configured ... upstreamSubtype=0x...` → `0x32595559`=YUY2、`0x47504A4D`=MJPG、
   `0x34363248`="H264"、`0x43564548`="HEVC"；**是 MJPEG/H264/HEVC 就先按 §3.1 处理**。
2. `[present] present-sink: ... vsync=? tearing=?` → 应为 `vsync=0 tearing=1`；否则该环境不支持
   tearing 或走了兼容模式。
3. `[capture-timing] ... callbackFps=? dropped=? callbackToPresentReturnP95Ms=? gpuNrP95Ms=? ...`
   → 区分"卡不给帧"（callbackFps 低）与"我们处理慢"（p95/NVOF/NR 高）。
4. 另加两个设置：采集音频同步模式、采集音频设备是否为卡的数字音频。

## 5. 优化方案（按性价比排序；本轮未实施）

**P0（低风险先做）**

1. **格式选择"低延迟优先"排序/标注**：原生（YUY2/NV12/P010）排在 MJPEG/H.264 前；列表直接标注
   "原生（最低延迟）/卡内编码（+1~3 帧）/需系统解码（更高）"；用户选后者时提示可改选原生模式。
   实卡数据支撑：1080p60 YUY2 我们这侧 2.64ms、MJPEG 4.12ms；4K18 MJPEG 的 CPU 解码要 7ms/帧。
2. **解码路径低延迟化**：给插入的解码器 MFT 设置 `CODECAPI_AVLowLatencyMode=1` 并记录是否接受。
3. **视频 pin 缓冲协商**：像音频那样对视频输出 pin 调 `IAMBufferNegotiation::
   SuggestAllocatorProperties`，把 cBuffers/cbBuffer 压到最小可行值（USB3 下 1080p60 YUY2 单帧
   ~4MB 完全放得下）。
4. **音频同步补偿上限**：自动模式给"等视频"加显式上限（如 ≤40ms），超出就让音频追，并在状态
   栏显示当前等待量。
5. **背压/积压告警（新增，来自 §1a/§3.1 的实测修正）**：当 `readAgeMs > 1 帧` 或
   `callbackFps < 名义帧率×0.9` 时，在状态栏提示"采集卡正在缓冲：延迟上升"，并建议降分辨率/
   换原生格式；同时把 `readAgeMs`、`callbackFps`、`compensationMs`、`skewMs` 显示在采集诊断面板。

**P1（中等改动）**

5. **采集延迟面板**：三行显示"卡→我们（callbackFps/dropped）/我们处理（CPU+GPU 各段 p95）/
   我们→显示（Present 返回）"，并给建议（如"4K60 MJPEG 建议改 1080p60 YUY2"）；
   第 4 行显示卡侧证据（`readAgeMs`/`callbackFps`/`compensationMs`）。
6. **音频缓冲档位**：现在请求 10ms（实测协商 22ms）；加"低延迟(5ms)"档并标注稳定性风险。
7. **独占全屏（捕获低延迟）选项**：绕开 DWM 再省约 1 帧；默认关闭（alt-tab/HDR/OSD 风险）。

**P2（大工程，需实卡验证）**

8. **采集改用 Media Foundation + D3D11 纹理共享**：`MF_LOW_LATENCY`、
   `MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT=1`、GPU 直通（去掉 CPU 拷贝与 NullRenderer）。
9. **卡内 H.264/HEVC 用硬件解码**（NVDEC/D3D11VA），绕开 MS MFT 的软件重排序延迟。

## 6. 待补的实测（需用户配合，不改代码）

1. ~~释放采集卡做格式 A/B~~ **已完成（§1a）**，命令留存备查：

```
out/build/audio-continuity-repair-20260915/veyra_capture_tests.exe --list
out/build/audio-continuity-repair-20260915/veyra.exe "capture:0:0:-1:0"  --smoke-seconds 12 --no-nr --no-sr --no-fg
out/build/audio-continuity-repair-20260915/veyra.exe "capture:0:24:-1:0" --smoke-seconds 12 --no-nr --no-sr --no-fg
```

   对比 `callbackToPresentReturnP95Ms`、`callbackFps`、`captureDropped`。
2. **端到端（玻璃到玻璃）**：手机 240fps 慢动作同时拍主机画面与显示器（或主机跑毫秒计时器），
   同条件再拍 OBS 预览与 PotPlayer。这是唯一能回答"我们比 OBS 低多少毫秒"的证据。
3. 让 2–3 位"延迟高"的用户提供 §4 的日志行 + 卡型号 + 是否走 Hub。

## 7. 本轮边界

- 未改任何产品代码；未强杀 OBS 抢占采集卡；未宣称"已经更快"。
- 本机实测是虚拟摄像头（NV12 1440p60）与历史实卡 1080p50；实卡 YUY2 vs MJPEG 的直接 A/B
  与端到端光子延迟仍待 §6 两项测量。
