# 统一修复计划执行记录（2026-09-22）

分支 `codex/fg-independent-repair-20260922`，实施提交 `e11aa56`，标签
`checkpoint/plan-b1-pre-20260922`（开工）与 `checkpoint/plan-b6-done-20260922`（完成）。
计划见 [统一修复计划](../plans/UNIFIED_REPAIR_PLAN_2026-09-22.md)。本机 RTX 5070、100 Hz
显示器；所有数字为 30 s 短测或单元/合同测试，长测、实卡、多显示器、HDR 屏、肉眼画质
由用户验收。运行入口 `E:/项目/Veyra/tests/fg-independent-repair-20260922/app/veyra.exe`
（交付构建 SHA256 前 16 位 `1EB4288CBF1CF701`，staging，运行库为 junction，非便携包）。
产物：`E:/项目/Veyra/tests/fg-independent-repair-20260922/{b12,b3456,b7,b8,b9}/`，构建日志
`E:/项目/Veyra/logs/fg-independent-repair-20260922/build-b*.log`。

## 1. 逐项状态

| 编号 | 状态 | 备注 |
| --- | --- | --- |
| **第 1 批** | | |
| D1 | 已修 | `statusBar` 删除；字幕快捷键/自动对齐/关闭进度走 `showToast` 到底栏 `ColourStatus`（3 s 后恢复设置消息）；日常模式无文件或失败时 `EmptyTitle/EmptyHint` 显示并含失败原因 |
| D3 | 已修 | 状态宽度 `min(440,max(120,tw/3))`，标题宽度相应减去 |
| D4 | 已修（改方案） | 窄于 960 px 时截图按钮移入左侧竖栏 y=372，抽屉按钮保持 `w-384`；原"右移抽屉"方案在 720 px 下仍与模式切换重叠 |
| D5 | 已修 | `Master` 分支检查 `requestSettings` 返回值并还原 |
| D7 | 已修 | `windowTimer` 首行在弹窗打开时返回 |
| D11 | 已修 | 状态 sink 走 `setText` 去重；`message()` 记上次文本 |
| D15a | 已修 | 最大化按钮真最大化；Ctrl+V 不再触发 |
| B12 | 已修 | 删除 4 行空 "skipped" 日志 |
| **第 2 批** | | |
| D2 | 已修 | 诊断面板不可见时不刷新；刷新前后恢复首行与选区 |
| D6 | 已修 | `alignSubtitleToAudio` 增加 `stop_token`；jthread lambda 接 token；`WM_DESTROY` `request_stop()` |
| D8 | 已修 | `WM_ENTERSIZEMOVE` 起 16 ms 合并 layout；`SetWindowRgn` 仅尺寸/模式变化时重建；去 `RDW_UPDATENOW` |
| D9 | 部分 | `seekPreview.tick` 复用 tick 快照；CapturePanel 三个 getter 只在面板创建时调用，非每 tick，无需改 |
| D10 | 已修 | 音量滑块判等 |
| D12 | 已修 | 呈现设置保存 1 s 合并（`PreferenceSaveTimer`），关窗时立即保存；折叠状态掩码不变不写 |
| D13 | 已修 | `cachedChromeFont` 按 (dpi,size,weight) 缓存 |
| D14 | 已修 | 字幕 DIB 线程局部缓存，仅尺寸变化时重建 |
| D15b | 部分 | CapturePanel 未完成的枚举 future 在销毁时交给 detached 线程；RemotePlayPanel 的 worker 已接 `stop_token`，未改 |
| E4 | 已修 | 导出管理器析构等待 1 s |
| **第 3 批** | | |
| C1 | 已修 | 4 个 `static const std::regex`；提取在锁外，锁内只更新 `latestProblem_`/历史 |
| C3 | **未做** | 采集回调三缓冲锁外复制涉及 mailbox 所有权重构，本轮未动；见 §4 |
| C4 | 未做 | 同上 |
| C5 | 已修 | `PcmQueue` 连续存储 + 头游标，`pull` 用 `memcpy` |
| E1 | 已修 | NVENC 成功日志前 24 次，之后仅失败 |
| E5 | 未做 | 每秒日志合并未做（分析脚本依赖现有行格式） |
| 延迟 4 | 已修 | owner 线程 `MmcssScope("Pro Audio")`；采集回调线程 thread_local 注册 |
| **第 4 批** | | |
| B4 | 已修 | `pts` → `best_effort_timestamp` → `pkt_dts`，每 300 次记一条日志 |
| B5 | 未做 | 中途硬解→软解回退需要 seek 回位与历史重置，本轮未动 |
| C2 | 已修 | 软解线程 `clamp(cores/2,2,4)` |
| C6 | 已修 | `IMMNotificationClient`，默认渲染端点变化置 `AUDCLNT_E_DEVICE_INVALIDATED` 走现有恢复 |
| C7 | 未做 | 格式变化重建 resampler 未动 |
| E3 | 已修 | 2 s GPU 超时设置具体原因 |
| **第 5 批** | | |
| A1 | 已修 | HDR 查询缓存 `IDXGIFactory1`，`IsCurrent()` 失效时重建 |
| 延迟 2 | 未做 | owner 唤醒改事件未动（`waitLive` 仍 1 ms 定时器切片） |
| 延迟 3b | 未做 | 依赖 C3 |
| A2 | **已撤回** | upload fence 前移到 FG 之前后，`veyra_fg_presentation_tests` 报 49 条跨队列 barrier 错误（`uploadFences_` 同时是 DLSS 呈现队列的 handoff fence，必须覆盖 FG 列表把 `videoFrame_` 转回 COMMON）。恢复原位并加注释 |
| 延迟 5 | 已修 | `LivePairLatency` 余量 1 ms→0.5 ms，步长 0.25→0.5 ms；单测常数同步 |
| A6+B3 | 已修 | `snapshot()` 拷贝后释放引擎互斥再取 flow；flow 快照 50 ms 缓存 |
| A4+A5 | 未做 | 每帧诊断上下文与直方图缓冲未动 |
| A7 | 已修 | 命令槽时间戳回读仅 `VEYRA_PROFILE_SLOTS` 或详细日志下 |
| B7 | 已修 | `fgDisableReadback_` 常驻 Map |
| A8 | 已修 | 暂停轮询 50 ms |
| 延迟 1 | 未做 | 专业模式圆角是否 Composed 的测量需要 PresentMon 权限或用户实屏，未做 |
| **第 6 批** | | |
| B2 | 已修 | `SetUnhandledExceptionFilter` 写 minidump 并 flush；`VEYRA_TEST_RAISE_SEH=1` 验证：日志末尾完整，dmp 255 KB |
| B1 | 未做 | step lambda 状态收敛属重构，本轮未动 |
| B6 | 撤回 | `recovering()` 有单测使用，保留 |
| C8/C9/E6 | 未做 | 注释级项未动 |

## 2. 门槛测试结果

| 测试 | 结果 | 日志 |
| --- | --- | --- |
| `veyra_ui_contract_tests` | PASS（384 布局用例） | `b12/` |
| `veyra_popup_selector_tests` | 19 PASS | `b12/` |
| `veyra_subtitle_panel_tests` | 31 PASS | `b12/` |
| `veyra_subtitle_overlay_tests` | 18 PASS | `b12/` |
| `ui-fg-backends.py` | exit 0 | `b12/ui-fg-backends/` |
| `ui-layout-dpi.py` | **exit 1，"status detail cannot scroll"** | 基线 exe（`bounded-repair` app）同样失败同一断言，属本机既有状态，非本轮引入；`header controls overlap` 断言在 D4 改方案后通过 |
| `veyra_presentation_worker_tests` | 127 PASS | `b3456/` |
| `veyra_repair_contract_tests` | 205 PASS | `b3456/` |
| `veyra_capture_audio_tests` | 18 PASS | `b3456/` |
| `veyra_fg_presentation_tests` | exit 0，D3D12 errors=0（A2 撤回后） | `b3456/fg-presentation.out` |
| `veyra_fg_settings_tests backend-switch` | 16 PASS（最终 exe） | `b3456/settings2.log` |
| `veyra_fg_admission_tests dlss 4` | pass=1，debugErrors=0 | `b3456/admission.log` |
| SEH 注入 | minidump 写出，日志 flush | `b3456/seh.log`，`veyra-crash-*.dmp` |
| 8 s 文件音频 smoke | exit 0，0 ERROR | `b3456/audio-smoke.log` |
| `veyra_source_tests` | **未运行**：corpus 需 `veyra_clip_gen`，本机 FFmpeg 无 libopenh264 | `b3456/clipgen.log` |
| `veyra_audio_timeline_tests` 等 3 个 | 未运行（需参数，本轮未查） | — |

## 3. FG 短测（30 s，全屏，`b3456/ab-final/`，与本日早些 `ab4`/`ab3` 同构建族对照）

| 工况 | 指标 | 本轮 | 之前（第 0 批后） |
| --- | --- | ---: | ---: |
| SR+NR XeSS 4X | 源/s | 49.9 | 45.4 |
| | 生成/s | 149.7 | 136.1 |
| | 原帧间隔 p95 / max | 23.0 / 24.8 | 26.5 / 27.5 |
| | `gpuReadyP95` | 20.8 | 23.2 |
| 原生 XeSS 4X | 源/s | 60.0 | 58.5 |
| | Present 阻塞 p50 | **0.73** | 11.5 |
| | `gpuReadyP95` | 15.8 | 18.0 |
| 原生 DLSS 6X | 提交/s | 269.9 | 237.1 |
| | 间隔 >16.67 ms | **0** | 3 |
| | 间隔 p95 / max | 8.44 / 8.96 | 8.53 / 16.94 |
| | reduced 对 | 574 | 814 |
| SR+NR DLSS 6X | 提交/s | 140.3 | 115.7 |
| | 间隔 >16.67 ms | 118 | 155 |
| | reduced 对 | 623 | 269 |
| 原生 DLSS 2X | 提交/s | 120.0 | 120.0 |
| | 间隔 p95 | 8.76 | 8.78 |

所有工况 `errorLines=0`，`notDisplayed` 分布与刷新率一致。原生 XeSS 4X 的 Present 阻塞
从 11.5 ms 降到 0.73 ms 是本轮最大的意外收益，最可能来自 MMCSS 让提供方节奏线程与
owner 不再互相抢占；DLSS 6X 提交率 237→270 与 SR 6X 116→140 同理。这些是单次 30 s
短测，用户长测确认前不计入。

## 3b. 第 7 批（补做未完成项，提交 `6d019dc`，标签 `checkpoint/plan-b7-done-20260922`）

| 编号 | 状态 | 备注 |
| --- | --- | --- |
| C3 | 已修 | 原生采集回调第三帧 staging：锁内认领、锁外复制、锁内指针交换发布；`tryRead` 不再被复制阻塞 |
| 延迟 2 | 已修 | 采集源暴露自动重置事件；owner `waitLive` 改 `WaitForMultipleObjects{事件,定时器}` |
| B5 | 已修 | 中途硬解失败：重开软解、`seekToUs` 回上次 PTS、置 Seek 标志与新 epoch |
| C7 | 已修 | 采样率/采样格式变化重建 resampler；仅声道布局变化才停流 |
| A4/A5 | 已修 | 每帧诊断上下文复用成员，字符串仅变化时更新；亮度采样与直方图缓冲复用 |
| E6 | 已修 | 命令槽 GPU 时长记录满 16384 后环形覆盖 |
| C9 | 已修 | 硬解路径 EAGAIN 缓冲上限 8（原 16），避免占满解码池 |
| C8 | 注释 | Receive 锁范围说明 |
| C4、延迟 3b、E5、B1 | 未做 | 压缩 payload 池、直写 upload 堆、每秒日志合并、step lambda 收敛 |

门槛：scheduler 127、修复合同 205、采集音频 18、FG 呈现 D3D12 errors=0、backend-switch 16、
文件 NR+DLSS2 10 s smoke 463 帧 0 错误；实卡 MCS 4K--T800 4K30 rate test PASS（callback
29.97）；25 s 采集 NR+DLSS4X 新/旧 exe 对照：`callbackToPresentReturnP95` 42.7 vs 42.5 ms，
`gpuReadyP95` 10.6 vs 10.6，dropped 0/0，expired 2/2。**C3 与事件唤醒在本工况没有可测收益**：
FG 下延迟由相位等待主导，且复制本就与 GPU 工作重叠；收益（若有）要在无 FG 或 1440p60
工况用 `test-capture-version-comparison.ps1` 120 s 测，由用户执行。注意 `capture-callback`
日志的 `entryToLockMs` 现在包含锁外复制时间，与旧版数值不可直接比较。

## 3c. 第 8 批（收尾，提交 `8a1bb0a`，标签 `checkpoint/plan-b8-done-20260922`）

| 编号 | 状态 | 备注 |
| --- | --- | --- |
| C4 | 已修 | 压缩采集 payload 缓冲池（≤8 个），回调 `takePayload` 复用，解码后/丢弃/清空时归还；`close()` 清池 |
| 延迟 3b（有界） | 已修 | 原生采集 AVFrame 以 256 字节行对齐分配，与 D3D12 upload pitch 一致；图入口 `linesize==stride` 时每平面一次 `memcpy`，否则逐行回退。**未做**直写 upload 堆（需 mailbox 与 upload 环所有权合并） |
| E5 | 已修 | `present-deviation`/`output-queue`/`frame-trace` 每秒行并入 `player-timing`（新增 `entryAbsP95Ms returnAbsP95Ms pendingFrames enhancementProcessingMs`）；`VEYRA_VERBOSE_FRAME_LOGS` 下保留旧行 |
| B1（有界） | 已修 | `OnExit stopPresentation` 移到 `while(!stop_)` 前一行并加注释：它最后声明、最先析构，调度器在其 lambda 引用的局部之前销毁。**未做** step lambda 状态收敛为结构体 |

门槛（`b8/`）：scheduler 127、修复合同 205、FG 呈现 D3D12 errors=0、backend-switch 16、
实卡 rate test PASS；25 s 实卡 NR+DLSS4X `callbackToPresentReturnP95` 42.5 ms dropped 0；
25 s 实卡无 FG 新/旧 exe 对照 11.56 vs 11.52 ms（持平，readAge 0.98 vs 1.03 ms，processCpu
2.01 vs 2.06 ms）。`veyra_capture_compressed_tests` SKIP（缺 `loop/local/fixed_clips/test_h264_1080p.mp4`）。

### 3c.1 第 8 批 FG 矩阵（`b8/ab-final/`）与第 6 批对照

| 工况 | 指标 | 第 8 批 | 第 6 批 |
| --- | --- | ---: | ---: |
| SR+NR XeSS 4X | 源/s | 45.8 | 49.9 |
| | 原帧间隔 p95 / max | 26.2 / 27.1 | 23.0 / 24.8 |
| 原生 XeSS 4X | 源/s | 58.8 | 60.0 |
| | Present 阻塞 p50 | **12.0** | 0.73 |
| | `gpuReadyP95` | 18.0 | 15.8 |
| 原生 DLSS 6X | 提交/s | 240.2 | 269.9 |
| | 间隔 >16.67 ms | 2 | 0 |
| | reduced 对 | 792 | 574 |
| SR+NR DLSS 6X | 提交/s | 121.1 | 140.3 |
| | 间隔 >16.67 ms | 142 | 118 |
| 原生 DLSS 2X | 提交/s | 120.0 | 120.0 |
| | 间隔 p95 | 8.79 | 8.76 |

所有工况 `errorLines=0`。第 8 批矩阵整体比第 6 批慢一档（约 8–12%），原生 XeSS 4X 的
Present 阻塞回到 12 ms。**已排除代码回归**：用第 6 批提交 `e11aa56` 重新构建
（`build/fg-b6-check-20260922`，exe SHA 前 16 位 `5E5C2B0EF2DE2FEC`，staging
`tests/fg-independent-repair-20260922/app-b6/`），与第 8 批 exe 交替各跑 2 次同一工况：

| 运行 | 源/s | Present 阻塞 p50 / p95 | `gpuReadyP95` | >16.67 ms |
| --- | ---: | ---: | ---: | ---: |
| 第 6 批矩阵（15:54） | 60.0 | 0.73 / 1.07 | 15.8 | 412 |
| 第 8 批矩阵（16:22） | 58.8 | 12.0 / 12.9 | 18.0 | 612 |
| 第 8 批 exe 复跑 1/2 | 58.0 / 58.0 | 11.4 / 12.3，11.3 / 12.8 | 18.1 / 18.2 | 769 / 727 |
| **第 6 批 exe** 交替 1/2 | 58.2 / 58.0 | 11.4 / 12.4，11.6 / 12.5 | 18.1 / 18.1 | 760 / 760 |
| 第 8 批 exe 交替 1/2 | 57.9 / 57.8 | 11.4 / 12.4，11.4 / 12.4 | 18.1 / 18.1 | 771 / 772 |

同一时段两个 exe 完全一致（差异 <0.2 ms），差的是时段：第 6 批矩阵时 NVML GPU 均值 87%，
之后所有运行都是 94–95%，`gpuReadyP95` 抬高 2.3 ms。这与独立复核里记录的 XeSS 提供方
双稳态（源周期 38 vs 54 之类的两档）一致：GPU 余量少 2–3 ms 时提供方节奏线程回到
"owner 在 Present 里等一个输出周期" 的状态。**结论**：第 7/8 批没有引入回归；第 6 批矩阵
的 0.73 ms 是有利时段的一次结果，不能当作稳定收益。用户长测请按 §6 第 1 条同时记录
NVML GPU 均值与 `gpuReadyP95`，并对比 `VEYRA_DISABLE_XESS_SOURCE_TIMING=1`。
原始数据：`b8/ab-xess-rerun{1,2}`、`b8/bisect-{b6,b8}-{1,2}`，日志
`logs/fg-independent-repair-20260922/{ab-xess-rerun*,bisect-*}.log`。

## 3d. 第 9 批（收口三个遗留项，标签 `checkpoint/plan-b9-done-20260922`）

### B1 完整版：step lambda 捕获显式化（已修）

计划原文要求"把 step 需要的状态收进 `LiveStepContext` 结构体"。实际做法改为**把默认
引用捕获 `[&]` 换成逐个列出的显式捕获**，理由是两者达到同一个目的（让新增引用变成编译
错误），但显式捕获与 `[&]` 语义完全相同、零行为风险，而搬进结构体要重排一个约 400 行
lambda 触及的全部局部量，属于高风险重构且无运行时收益。

用编译器枚举出该 lambda 实际引用的局部量：第一轮 31 个（MSVC 到 100 条错误即 C1003
截断），补齐后第二轮再报 14 个，合计 **45 个按引用捕获** + 原有 12 个按值捕获。全部列出
后编译通过（`logs/.../b1-probe{,2,3}.log`）。现在任何人在调度器守卫之后新声明局部并在
step 中引用，会直接编译失败，而不是静默悬垂——这正是清扫表 B1 要求的"锁定声明顺序"。

### 延迟 3b 完整版：直写 upload 堆（**实测后不做**）

先量化收益上限再决定。本机 RTX5070 实测（源码与结果见
`b9/upload-bench/{up.cpp,result.txt}`，4K NV12 = 11.87 MB，60 次取中位数）：

| 拷贝 | 中位耗时 |
| --- | ---: |
| memcpy → UPLOAD 堆（每平面一次，第 8 批后的形态） | 0.433 ms |
| memcpy → UPLOAD 堆（逐行，第 8 批前的形态） | 0.437 ms |
| memcpy → 普通系统内存（对照） | 0.449 ms |

关键结论：**写 UPLOAD 堆并不比写普通内存慢**（write-combined 顺序写满速），所以图内
那次 AVFrame→upload 拷贝只值 0.43 ms。实卡日志里回调那次拷贝是 0.95 ms
（`capture-callback entryToCopiedMs≈0.95`，12.44 MB），慢一倍是因为**读**驱动缓冲
才是瓶颈，不是写。

因此无论走哪条路——让 mailbox 帧直接落在 upload 堆，还是扣住 `IMediaSample` 到 owner
线程再拷一次——省掉的都只是这 0.43 ms 的一次，占无 FG 端到端 10.8 ms 的 **4%**；有 FG
时相位等待主导（第 7 批已测），收益接近 0。代价是把 D3D12 资源所有权与 fence 退休打进
`src/source`（目前该层不依赖 D3D12），并要在 NV12/P010/YUY2/RGB24、重连、改分辨率和
D3D12VA 硬解各条路径上重新做实卡回归。**收益 4%、风险覆盖整条实卡热路径，判定不做**，
证据留档。真要再压延迟，方向是 GPU 侧（无 FG 时 `gpuReadyP95` 7.8 ms 占 72%），不是这次拷贝。

### 延迟 1：专业模式是否比全屏多延迟（已测，无差异）

同一 exe、同一片源，pro（窗口专业模式）与 fullscreen 交替各跑 2 次 30 s
（`b9/view-ab/`，日志 `logs/.../view-ab.log`）：

| 工况 | 视图 | 帧 | presents/s | displayed/s | notDisplayed/s | absLatenessP95 | presentCpuP95 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| DLSS 2X | pro | 1657 / 1666 | 120.4 / 120.5 | 100.4 / 100.5 | 19.96 / 19.91 | 0.75 / 0.77 | 0.30 / 0.35 |
| DLSS 2X | fullscreen | 1666 / 1662 | 120.7 / 120.5 | 100.7 / 100.5 | 20.0 / 19.98 | 0.77 / 0.77 | 0.32 / 0.35 |
| XeSS 4X | pro | 1661 / 1444 | 60.6 / 60.7 | 101.1 / 101.2 | 0 / 0 | 1.48 / 8.67 | 1.01 / 6.19 |
| XeSS 4X | fullscreen | 1653 / 1661 | 60.6 / 60.5 | 101.0 / 100.9 | 0 / 0 | 1.52 / 1.44 | 0.92 / 0.86 |

DLSS 2X 两种视图逐项相同（差异 ≤0.02 ms），100 Hz 下 `displayed`/`refreshes` 都是
100.5/s，`refreshesWithoutNewFrame=0`。**专业模式没有可测的额外延迟。** XeSS 4X 那一次
pro 8.67 ms 是 §3c.1 记录的提供方双稳态坏时段（同组 fullscreen 1.44，另一次 pro 1.48），
不是视图差异。**边界**：DXGI `GetFrameStatistics` 在两种视图下返回相同数值，这**不能**
单独证明走的是 independent flip；要区分 Composed 与 independent flip 仍需 PresentMon
权限。本条只结论"用户可见延迟与显示节奏无差异"。

### 第 9 批 FG 矩阵（`b9/ab-final/`）——第 8 批的“变慢”没有复现

同一套 5 个工况、同样 30 s，第 9 批 exe（含 B1 显式捕获）与第 6 批、第 8 批对照：

| 工况 | 指标 | 第 6 批 | 第 8 批 | **第 9 批** |
| --- | --- | ---: | ---: | ---: |
| SR+NR XeSS 4X | 源/s | 49.88 | 45.79 | **49.78** |
| | `gpuReadyP95` | 20.83 | 22.87 | **20.91** |
| 原生 XeSS 4X | 源/s | 60.00 | 58.83 | **60.00** |
| | Present 阻塞 p50 / p95 | 0.733 / 1.067 | 11.98 / 12.89 | **0.747 / 1.030** |
| | 间隔 >16.67 ms | 412 | 612 | **415** |
| | NVML GPU 均值 | 87.3% | 94.3% | **88.4%** |
| 原生 DLSS 6X | 提交/s | 269.9 | 240.2 | **268.6** |
| | 间隔 >16.67 ms | 0 | 2 | **0** |
| SR+NR DLSS 6X | 提交/s | 140.3 | 121.1 | **139.2** |
| 原生 DLSS 2X | 提交/s | 120.0 | 120.0 | **120.0** |

五个工况全部回到第 6 批水平，`errorLines` 全 0。原生 XeSS 4X 的 Present 阻塞
0.747 ms 与第 6 批 0.733 ms 一致，对应 GPU 均值 88.4% 对 87.3%；第 8 批那次是
94.3%。**至此两个方向都验证了 §3c.1 的结论**：同一时段跑两个不同 exe 结果相同
（11.4 ms 对 11.4 ms），不同时段跑最新 exe 又回到 0.75 ms——差异来自 GPU 余量
所处的提供方节奏档位，不是第 7/8 批的代码。

需要注意 SR+NR XeSS 4X 这一档：第 8 批与第 9 批 NVML GPU 都是 95.5%、频率温度
也几乎相同（2834 MHz / 68.1 °C 对 2834 MHz / 66.8 °C），速率却是 45.8 对 49.8。
所以“GPU 余量”只是相关量，不是唯一判据；该工况的档位切换还有别的触发条件，本轮
没有定位。用户长测若遇到慢档，请同时记录 `gpuReadyP95`、NVML GPU 均值与
`xess-fg` 行，便于继续缩小。

### 第 9 批门槛（`b9/`）

| 测试 | 结果 |
| --- | --- |
| `veyra_presentation_worker_tests` | 127 PASS |
| `veyra_repair_contract_tests` | 205 checks 0 failures |
| `veyra_capture_audio_tests` | exit 0 |
| `veyra_fg_presentation_tests` | exit 0，D3D12 errors=0，ERROR 0 行 |
| `veyra_fg_settings_tests backend-switch` | 16 PASS，0 ERROR |
| `veyra_fg_admission_tests dlss 4` | pass，debugErrors=0 |
| 实卡 rate test（MCS 4K--T800 4K30） | PASS callback=29.968 reads=181 monotonic |
| 实卡 25 s NR+DLSS4X | `callbackToPresentReturn` P95 **40.03** ms（第 8 批 42.5），dropped 0，expired 0，0 ERROR |
| 实卡 25 s 无 FG | P95 **10.84** ms（第 8 批 11.56），`gpuReadyP95` 7.82，dropped 0，0 ERROR |
| `veyra_capture_compressed_tests` | SKIP（仍缺 `loop/local/fixed_clips/test_h264_1080p.mp4`） |

两个实卡数字都略好于第 8 批，属同向噪声范围，**不单独计为收益**，只证明 B1 显式捕获没有
引入回归。另：`tools/fsr_probe`、`tools/fsr_upscale_probe` 在 worktree 里用
`../../third_party_local/...` 相对路径找 FidelityFX 头文件，worktree 下必然找不到，
`ninja`（全目标）因此失败。这是既有问题、与本轮无关，产品目标 `veyra` 与全部测试目标
单独构建均通过；未修，记录待办。

## 3e. 修改前 / 修改后同时段 A/B：Reduced 路径在真超分 6X 下是净负收益

之前所有对照都是"本轮各批次之间"，没有把**修改前的基线 exe** 拉进同一时段。这次补上：
基线 `tests/bounded-repair-20260922/app/veyra.exe`（对应存档
`checkpoint/pre-fg-independent-repair-20260922` / `c3f1ec8`）与当前 exe 交替各跑 2 轮，
每轮 30 s，证据 `b9/effect-ab/`，日志 `logs/.../effect-ab.log`。
指标用 `displaySubmits` 的逐秒增量（实际提交到交换链的帧数），丢掉前 2 秒预热。

| 工况 | 基线 呈现/s（均值 / 标准差 / 最低秒） | 当前 呈现/s | 生成帧 基线→当前 |
| --- | --- | --- | --- |
| 原生 NR XeSS 4X | 60.75 / 0.60 / 60，60.62 / 0.56 / 60 | 60.58 / 0.57 / 60，60.75 / 0.60 / 60 | 4938→4944，4956→4950 |
| 原生 NR DLSS 6X | 294.6 / 11.10 / **246**，309.5 / 11.22 / **261** | 285.4 / 11.18 / **257**，294.3 / 7.06 / **272** | 6425→6175，6789→6386 |
| 真超分 NR DLSS 6X | 162.3 / 4.88 / 146，165.4 / 6.34 / 146 | **146.7 / 18.22 / 63**，**148.8 / 18.20 / 63** | 2775→2374，2746→2432 |

结论分三档：

- **XeSS 4X：无可测差异。** 两版逐项相同。注意 `displaySubmits` 只计软件提交，
  XeSS 提供方内部生成的帧不计入，所以这一档只能说明源速率与提交节奏没变，
  不能用来判断提供方节奏。
- **原生 DLSS 6X：轻微正面。** 生成帧少 3–6%，但最低秒从 246/261 抬到 257/272，
  标准差持平或更好。属于削峰。
- **真超分 NR DLSS 6X：净负收益，是本轮引入的退步。** 呈现率从约 164/s 掉到约 147/s
  （−10%），且逐秒序列整体低一档（基线 156–171，当前 144–158），不是个别抖动。

### 责任改动已定位并用开关证实

逐秒日志显示满组准入数下降：基线 `admittedPairs=550/546`，当前 `348/347` 外加
`reducedPairs=638/678`。即**本来能按完整 6X 组准入的 pair，被改判成了 2X 降级组**
（5 张生成变 1 张），不只是把原本会被丢弃的组补回来。用本轮预留的开关直接验证
（`b9/reduced-ab/`，同 exe、同时段、交替 2 轮）：

| 配置 | 呈现/s 均值 | 标准差 | 最低秒 | 生成帧 | 满组 | 降级组 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 默认（Reduced 开） | 147.0 / 144.3 | 17.92 / 17.36 | 62 / 62 | 2377 / 2323 | 345 / 335 | 646 / 631 |
| `VEYRA_TEST_FG_NO_REDUCED=1` | **159.4 / 163.4** | 20.94 / **6.35** | 61 / **141** | 2710 / 2810 | 537 / 558 | 0 |
| 修改前基线 exe | 162.3 / 165.4 | 4.88 / 6.34 | 146 / 146 | 2775 / 2746 | 550 / 546 | 0 |

关掉 Reduced，满组数、生成帧和呈现率全部回到修改前水平。**Reduced 路径对这一工况
负全责。** 另外那个深谷（最低秒 62）在关掉 Reduced 时也出现过一次（rep1 最低 61），
说明深谷另有原因——同一秒两版都有一次约 36.8 ms 的 `graph-cpu-stall`，是既有停顿，
不是 Reduced 造成的；Reduced 只稳定地压低了整体水平。

### 待用户决定

Reduced 的设计意图是"超预算时给出可呈现的中点，而不是整组空洞"，在原生 DLSS 6X 上
确实抬高了最低秒；但在真超分 6X 上它抢走了本可完整准入的组，净损失 10% 呈现帧。
**未擅自改默认值**，三条路可选：一是默认关闭 Reduced；二是只在满组准入本就会失败时
才允许降级（收紧触发条件，需要改 `canAdmitReduced` 的调用位置）；三是按是否开启真超分
分档。请长测确认主观观感后再定——数值上少 10% 生成帧与主观流畅度不一定同向。

## 3f. 补齐其余工况的同时段 A/B：真超分 XeSS 4X 是本轮最大收益

`b9/effect-ab2/`，日志 `logs/.../effect-ab2.log`，方法同 §3e。

| 工况 | 基线 呈现/s（均值 / 标准差） | 当前 呈现/s | 生成帧 | `gpuReadyP95` | 绝对迟到 P95 |
| --- | --- | --- | --- | --- | --- |
| 真超分 NR XeSS 4X | 40.58 / 1.04，41.61 / 1.09 | **50.38 / 0.56，50.04 / 0.54** | 2490→4122，2517→4110 | 29.5→21.1 / 24.8 | 59.5→49.6 / 50.8 |
| 原生 NR DLSS 2X | 121.29 / 0.93，120.96 / 1.06 | 121.21 / 1.26，121.12 / 1.05 | 1662→1662 | 12.57→12.49 | 0.75→0.73 |
| 原生 NR 不开补帧 | 60.58 / 0.57，60.75 / 0.52 | 60.75 / 0.52，60.83 / 0.37 | — | 10.31→10.19 | 0.75→0.73 |

真超分 XeSS 4X **呈现率 +23%，生成帧 +65%，`gpuReadyP95` 降 8 ms，迟到降 10 ms，
标准差也从 1.04 收到 0.56**，两轮一致。这是本轮唯一一个大幅且稳定的正面结果。
DLSS 2X 与不开补帧两档逐项持平，说明基础播放路径没有被这轮改动拖慢。

### 收益归因：不是 XeSS 时序提示，也不是有界跳帧

用预留开关在同一时段各跑 2 轮（`b9/isolate-xess/`）：

| 配置 | 呈现/s | 标准差 | 生成帧 | `previewSkipped` | `gpuReadyP95` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 修改前基线 exe | 40.5 / 42.5 | 1.08 / 4.28 | 2502 / 2706 | 535 / 480 | 29.7 / 29.6 |
| 当前默认 | 50.0 / 49.8 | 0.45 / 0.37 | 4092 / 4065 | 282 / 291 | 21.1 / 21.2 |
| 当前 + `VEYRA_DISABLE_XESS_SOURCE_TIMING=1` | 49.8 / 49.8 | 0.41 / 0.41 | 4044 / 4053 | 299 / 294 | 21.2 / 21.2 |
| 当前 + `VEYRA_TEST_PREVIEW_SKIP_RESET=1` | **53.1 / 53.0** | 0.60 / 0.54 | **3690 / 3681** | 208 / 208 | 21.9 / 22.0 |

两条结论都与原计划的预期不同，如实记录：

- **X1（XeSS 真实源周期 `frameRenderTime` 提示）在本工况无可测作用**：关掉它是 49.8，
  开着是 50.0。之前文档把这一档的收益记在它头上，是跨时段对比得出的，不成立。
- **有界跳帧不是这次提速的原因，它本身是一笔交换**：恢复"每次跳帧都重置历史"的旧行为后
  呈现率反而更高（53.0 对 50.0），但生成帧少 10%（3681 对 4065）。即有界跳帧用 6% 的源
  呈现率换了 11% 的生成帧，外加少掉一半历史重置。它要解决的是**闪烁与拖影**，那是主观
  画质，这些数值测不出来，必须靠肉眼长测判定。
- **那么 40.5→50 的提速来自别处，本轮未能进一步归因。** 两个开关都排除后仍有 +23%，
  最可能的来源是那些"每帧少做事/少抢锁"的改动——`snapshot()` 拷贝后才释放引擎互斥
  （A6+B3）、命令槽时间戳回读改为仅在开启剖析时执行（A7）——两者都与
  `gpuReadyP95` 从 29.7 降到 21.1 吻合。要确证需要为它们各自加 A/B 开关并重新构建，
  本轮没做。

### 全部六个工况的最终结论

| 工况 | 修改前 → 当前 | 判定 |
| --- | --- | --- |
| 真超分 NR XeSS 4X | 40.6 → 50.2 呈现/s | **大幅正面** |
| 原生 NR DLSS 6X | 最低秒 246/261 → 257/272，生成帧 −3~6% | 轻微正面（削峰） |
| 原生 NR XeSS 4X | 逐项相同 | 中性 |
| 原生 NR DLSS 2X | 逐项相同 | 中性 |
| 原生 NR 不开补帧 | 逐项相同 | 中性 |
| 真超分 NR DLSS 6X | 164 → 147 呈现/s | **退步，已定位到 Reduced（§3e）** |

## 3g. 第 10 批：Reduced 路径先修记账、再默认关闭

§3e 定位到 Reduced 是净负收益后，先尝试"弄成赚的"，失败，最后按测量默认关闭。

### 第一步：记账 bug（已修，保留）

`FgRecoveryBudget::complete` 的第三个参数是 `warmup`，调用处传的是
`historyReset||fgRecovery||fgReduced`。`warmup=true` 时这次测量**只进 `warmup_`，
完全不更新 `base_` 与 `fg_`**。于是约 40% 的降级组的耗时从不进入成本模型，模型只看得到
贵的满组，预测值居高不下 → 更多组被拒 → 更多降级，形成自我强化的回路。

原意只是"降级组的补帧耗时不能拉低满组的补帧估计"，这对**补帧**成本成立，对**基础**
成本不成立——基础工作是每源帧一次，与倍率无关。改法：新增 `fgCostComparable` 参数，
降级组只跳过 `fgCost()`，基础耗时照常记账（`ms - 本组实测补帧耗时`，可比）。
改动 2 个文件 14 行。

效果：真超分 6X 抖动从 `sd=15.99` 降到 `5.48`，最低秒从 **67 抬到 128**。
但满组准入只从 340 变成 335，**没有恢复**。

### 第二步：同时段四方对照，判定仍为净负

`b10/fix-ab/`，日志 `logs/.../fix-ab.log`。修复后 exe、修复前 exe、当前 exe 关闭降级、
修改前基线 exe，四者交替，2 轮。

| 工况 | 配置 | 呈现/s | 标准差 | 最低秒 | 满组 |
| --- | --- | ---: | ---: | ---: | ---: |
| 原生 NR DLSS 6X | 修改前基线 | 288.3 / 289.7 | 7.26 / 6.81 | 262 / 267 | 1212 / 1237 |
| | 关闭降级 | 288.5 / 286.5 | 5.52 / 8.70 | 272 / 261 | 1222 / 1221 |
| | 降级（修记账后） | 275.7 / 272.4 | 7.60 / 7.94 | 254 / 259 | 1046 / 983 |
| | 降级（修记账前） | 273.1 / 274.8 | 7.11 / 8.55 | 258 / 255 | 1018 / 1040 |
| 真超分 NR DLSS 6X | 修改前基线 | 148.9 / 138.0 | 4.93 / 7.50 | 135 / 126 | 477 / 425 |
| | 关闭降级 | 149.8 / 151.0 | 5.61 / 7.04 | 136 / 126 | 482 / 491 |
| | 降级（修记账后） | 144.2 / 作废 | 5.48 | 128 | 335 |
| | 降级（修记账前） | 141.7 / 142.5 | 15.99 / 5.18 | 67 / — | 340 / 387 |

（真超分 6X 修记账后第 2 轮作废：运行中被手动暂停，日志可见 `state=closed` 与
epoch 1→2，帧数只有 1224。全部 52 次运行中仅此 1 次有中途重置，其余数据不受影响。）

两条结论：

1. **关闭降级后当前构建与修改前基线逐项一致**（原生 288.5/286.5 对 288.3/289.7，
   真超分 149.8/151.0 对 148.9/138.0；满组 1222/1221 对 1212/1237）。说明这两个工况
   除 Reduced 外**没有其他退步**。
2. **降级在两个 6X 工况都是净负，修记账后依然是。** 原生 288→275（满组 1212→1046），
   真超分 149→144（满组 477→335）。此前 §3e 记的"原生 6X 轻微正面"是跨时段比较得出的，
   同时段一比并不成立，已推翻。

根因：一个降级中点本身要占 GPU 时间，这段时间下一个满组就付不起了，于是**每救一次，
扣掉的帧比补上的多**。这不是参数没调好，是这个做法在当前预算模型下的结构性问题。

### 第三步：默认关闭（已改）

`VEYRA_TEST_FG_NO_REDUCED` 取消，改为 `VEYRA_TEST_FG_REDUCED=1` 才启用，默认走回
原来的 reject/seed。记账修复保留——它本身是对的，且只在启用降级时才起作用。
代码与测量结论一并写进注释，注明"没有同时段 A/B 证明有收益之前不要再默认打开"。
`scripts/acceptance/fg-independent-ab.py` 的变体同步改名（`noreduced` → `reduced`）。

原本要解决的"6X 整组空洞"问题**因此回到未解决状态**，如实记录：那个最深的空洞在两版
和关闭降级时都出现过，同一秒两版都有一次约 36.8 ms 的 `graph-cpu-stall`，是既有停顿，
不是 Reduced 造成也不是 Reduced 能解决的。要治它应该去查那次图提交停顿本身。

## 3h. 更正：§3e / §3g 的"净负收益"判定错误，Reduced 恢复默认开启

§3e 与 §3g 用**提交帧数**（`traceSubmitPerSec` / `displaySubmits` 逐秒增量）判定
Reduced 是退步。这个判据在 6X + 100 Hz 面板的组合下不成立，结论作废。

### 为什么判据错了

补上 DXGI `GetFrameStatistics` 的送显数据（`b10/gaps-ab-{1,2}/`，同一 exe 用开关切换、
同时段交替、各 2 轮）：

| 工况 | 配置 | 提交/s | **实际显示/s** | 丢弃未显示/s | **缺帧刷新/s** | 间隔>16.67ms | `gpuReadyP95` |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 原生 NR DLSS 6X | 降级关 | 292.7 / 292.8 | **100.3 / 100.2** | 192.4 / 192.6 | **0 / 0** | 33 / 35 | 23.75 / 23.76 |
| | 降级开 | 283.1 / 279.2 | **100.3 / 100.2** | 182.8 / 179.0 | **0 / 0** | **0 / 0** | 23.33 / 23.09 |
| 真超分 NR DLSS 6X | 降级关 | 157.2 / 159.8 | **100.7 / 100.8** | 58.4 / 59.0 | **0 / 0** | 141 / 159 | 25.65 / 25.45 |
| | 降级开 | 148.5 / 148.1 | **100.7 / 100.8** | 47.8 / 47.4 | **0 / 0** | **83 / 98** | **22.61 / 22.77** |

100 Hz 面板每秒只能点亮约 100 张不同画面。两种配置的 `displayed` 完全相同，
`refreshesWithoutNewFrame` 全部为 0。**降级少提交的那约 10 张/s，全部落在
`notDisplayed` 里，本来就不会被显示。** 用提交帧数判优劣，等于拿一个超供约 3 倍的
数字当观感指标，必然得出错误结论。

按送显口径重判，降级开启在每一项可见指标上不差，有两项更好：
长间隔 34→0（原生）、150→90（真超分），`gpuReadyP95` 真超分降约 3 ms。

### 处理

- **Reduced 恢复默认开启**，`VEYRA_TEST_FG_NO_REDUCED=1` 仍可关闭用于 A/B。
- §3g 的记账修复**保留**：它修掉的是真实 bug（降级组被当 warmup，基础耗时不进成本模型），
  并使真超分 6X 的逐秒抖动从 `sd=15.99` 降到 `5.48`、最低秒 67→128。
- **没有加任何按显示器刷新率限速的逻辑，也不会加。** 曾考虑"仅在提交超过刷新率时才降级"，
  已否决：超刷新率提交并非纯浪费（翻转丢弃下提交越勤，每次刷新拿到的画面越新），
  且刷新率在 VRR、换屏、改模式、多显示器下都会失效，会造成行为随显示器变化的隐性规则。
- 确认软件**不存在**按刷新率限速：`displayRefreshHz_` 全代码仅被一个 getter 读取并写进
  日志，不参与任何判断；Present 用 `syncInterval=0` + `ALLOW_TEARING`。实测 100 Hz 面板上
  每秒提交 292 次即为直接证据。

### 教训（写给后续）

**判断补帧收益一律用送显口径**：`displayed`、`refreshesWithoutNewFrame`、帧间隔分布。
提交帧数（`traceSubmitPerSec`、`displaySubmits`）只能用于判断管线是否在工作，
在提交速率高于刷新率时它与观感无关。本轮先后两次用错判据（§3、§3e/§3g），
第一次是跨时段比较，第二次是口径错误，两次都已推翻。

## 3i. 最终帧率呈现稳定性报告（恢复默认后的交付构建）

`b11/stability/`，5 个工况各 30 s 全屏，100 Hz 面板。`presents`/`displayed` 来自
DXGI `GetFrameStatistics`，逐秒增量取均值；`pres/s` 与 `sd`/`min`/`max` 为软件提交的
逐秒分布；`gaps>16.7ms` 为软件提交间隔超过一帧（16.67 ms）的次数。

| 工况 | 提交/s | 逐秒 sd | 最低秒 | **实际显示/s** | **缺帧刷新/s** | 提交间隔>16.7ms | Present阻塞p50 | 错误 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 原生 NR DLSS 2X | 121.2 | 1.31 | 117 | **100.4** | **0** | **0** | 0.19 | 0 |
| 原生 NR DLSS 6X | 276.0 | 7.23 | 264 | **100.4** | **0** | **0** | 0.18 | 0 |
| 真超分 NR DLSS 6X | 149.1 | 3.97 | 138 | **100.6** | **0** | 104 | 0.19 | 0 |
| 原生 NR XeSS 4X | 60.8 | 0.60 | 59 | **101.1** | **0** | 400 | 0.70 | 0 |
| 真超分 NR XeSS 4X | 50.9 | 0.40 | 49 | **101.0** | **0** | 744 | 13.35 | 0 |

**核心结论：五个工况的 `refreshesWithoutNewFrame` 全部为 0，`displayed` 全部在
100.4–101.1，即面板每一次刷新都拿到了新画面，没有任何一次刷新落空。**

两点判读说明，避免后续再次误读：

- **XeSS 两行的"提交间隔>16.7ms"不能按字面理解。** XeSS 的生成帧由提供方在自己的线程
  上直接送显，不经过软件的 present 计数，所以软件侧每秒只提交 51–61 次、间隔本就
  16–20 ms。证据是这两行的 `displayed`（101.1 / 101.0）**高于** `presents`（60.7 / 51.0）
  且 `notDisplayed` 为 0——多出来的就是提供方自己送显的帧。该列只对 DLSS 三行有意义。
- **DLSS 三行是真实的提交均匀度**：2X 与原生 6X 均为 0 次长间隔；真超分 6X 104 次，
  对应 §3h 中降级开启后的 83–98 区间，比关闭时的 141–159 明显更少。

交付构建 SHA256 前 16 位 `1EB4288CBF1CF701`。

## 3j. 重大更正：display-stats 指标是错的，§3h / §3i 的送显结论全部撤回

### 错在哪

`PresentSink::sampleFrameStatistics` 把 `delta.displayed` 取成了
`DXGI_FRAME_STATISTICS::PresentRefreshCount` 的增量。该字段是"最后一次显示的提交落在
第几次刷新"，是一个**刷新序号**，它每秒的增量恒等于刷新率，与显示了多少帧无关。
于是派生出的 `notDisplayed = presents - displayed` 与
`refreshesWithoutNewFrame = refreshes - displayed` 在结构上必然接近 0。

破绽其实一直在数据里：不开补帧时 `presents=60.7` 而 `displayed=101.18`，
显示比提交还多，物理上不可能。此前在 XeSS 两行看到同样现象时被错误解释为
"提供方自己送显"，该解释不成立。

### 改用 PresentCount 也不对

先把 `displayed` 改成 `DXGI_FRAME_STATISTICS::PresentCount`，实测（`b15/`）：

```
refreshHz=100.000 presents=273 displayed=273 refreshes=100 displayedRefreshDelta=100
```

`PresentCount` 的增量等于我们自己的 Present 调用数（273），两个 refresh 计数器都等于
刷新率（100）。**在"关闭垂直同步 + 允许撕裂 + 翻转丢弃"的交换链上，
`GetFrameStatistics` 的任何字段或组合都给不出"面板实际显示了多少帧"**——
撕裂状态下一次刷新可能显示多帧的拼接，"显示帧数"本身不是良定义的量。

### 最终处理

日志改为只报原始计数并写明其局限，删除两个派生的伪指标：

```
display-stats] refreshHz presentCalls dxgiPresentCount refreshes presentRefreshDelta backend
(这些计数器无法反映面板实际显示了多少帧；测量扫描输出需要 PresentMon)
```

`scripts/acceptance/fg-independent-ab.py` 同步兼容新旧字段名。

### 撤回的结论

- **§3h 全部撤回**：其核心论据"两种配置 `displayed` 相同、`refreshesWithoutNewFrame`
  全为 0"来自错误指标，不成立。因此"Reduced 在送显口径上等同或更好"**没有证据支持**。
- **§3i 的"实际显示/s"与"缺帧刷新/s"两列作废**，该表其余列（提交/s、逐秒 sd、
  最低秒、提交间隔、Present 阻塞）来自自有 trace，仍然有效。
- **§3e / §3g 基于提交帧数的判定重新成为唯一有数据支持的口径**，但它们本身也只
  衡量软件提交侧，不等于观感。

即：**Reduced 路径到底是赚是亏，目前没有可信结论**。默认值维持开启（现状），
不因为一个已被推翻的论据去改它；要定论必须先有 PresentMon 或等价的扫描输出测量。

### 教训

本轮第三次判据错误：先是跨时段比较（§3c.1 推翻 §3），再是用提交帧数当观感
（§3h 推翻 §3e/§3g），现在是指标定义本身错误（本节推翻 §3h）。
**新增一条硬规矩：任何新指标在用于判断之前，必须先做一次"物理不可能"检查**——
本例中"显示帧数 > 提交帧数"和"100 Hz 面板显示 270 帧/s"都应当在第一次出现时就拦下。

## 3k. 帧同步重新设计（标签 `checkpoint/pre-pacing-redesign-20260922` 起）

### 为什么重做

用户反馈"帧同步没啥用"。新增冒烟开关后同时段交替各 2 轮实测（`b13/pacing-ab/`），
三个模式在吞吐、逐秒 sd、媒体时钟偏差上**无任何可测差异**，逐秒 sd 开启后反而略差
（DLSS 4X：关 3.30–3.55，开 3.44–4.79）。设置确实生效（`effective=true/2/0`），
是设计本身没做事：

- **均匀呈现是空操作**：文件播放呈现已准到媒体时间 0.8 ms 内，自然产出间距
  （6X 组内 2.72 ms）本就接近目标（2.78 ms），再加下限不改变任何东西。
- **Reflex 一开补帧就退回低排队**，低排队只把 DXGI 队列深度 3 改 1。
- **XeSS/FSR 下整个功能被禁用**，只剩 vsync 一位透传。
- **输出限帧只在呈现时丢弃已算好的帧**，GPU 白算：原生 6X 在 100 Hz 屏上每秒生成
  约 5670 帧（30 s）、提交约 262/s，面板最多显示 100。

### 改动

1. **输出上限接回补帧生成量**（核心）。按 `ceil(上限/源帧率)` 推出所需倍率，
   `clamp(needed, 2, 用户倍率)`，只降不升。100 Hz 配 60 fps 源 → 2X。
   降档通过已有的 `snapshot_.previewFgMultiplier` 反映到界面。
2. **删除模式选择器**。`PacingMode` 枚举与 `mode` 字段保留但不再参与任何判断
   （不动已保存设置的字段布局）；`enabled` 语义收窄为"低延迟队列"。
3. **限速器改绝对时间网格**。原来用"上次实际提交 + 间隔"，抖动逐次累积成漂移。
4. **XeSS/FSR 置灰**。新增引擎侧 `presentationProviderOwned` 布尔量供 UI 判断，
   不靠解析中文状态串。
5. **控件重排为三项独立**：低延迟队列（复选框 240）、显示同步（242）、
   输出上限（243+244）。删除 241，三项不再互相置灰。状态串改三段式。
6. 冒烟开关改为 `--smoke-pacing off|lowlatency` 与新增 `--smoke-output-cap <fps|display>`；
   `ui-frame-pacing.py` 同步去掉对 241 的依赖。

### 实测：输出上限确实省 GPU（`b18/cap-ab/`，同时段交替各 2 轮）

| 工况 | 配置 | 生成帧 | 实际倍率 | 提交/s | GPU | 功耗 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 原生 NR DLSS 6X | 上限关 | 5636 / 5540 | 4.42 | 263.9 / 261.1 | 88.5% / 89.7% | 221.3 / 226.5 W |
| | 跟随显示器 | **1659 / 1650** | **2.0** | **100.0 / 100.0** | **62.7% / 62.4%** | **174.3 / 175.8 W** |
| 真超分 NR DLSS 6X | 上限关 | 2190 / 2126 | 2.24 | 139.5 / 136.3 | 92.0% / 92.2% | 237.5 / 238.5 W |
| | 跟随显示器 | 1621 / 1621 | 1.92 | 100.0 / 100.0 | 90.5% / 90.6% | 233.6 / 234.3 W |

- **原生 6X 收益大**：生成帧 −71%，GPU −27 个百分点，**功耗 −47 W（−21%）**。
- **真超分 6X 收益小**：功耗 −4 W（−2%）。该档瓶颈在超分与降噪，补帧占比小，
  且本就只跑到 2.24 倍，降到 1.92 倍空间有限。**不要按原生的数字向用户承诺省电。**

### 限速器漂移（改绝对网格前后）

| 工况 | 改前提交/s | 改后提交/s |
| --- | ---: | ---: |
| 原生 6X + 跟随显示器 | 97.36 / 97.33 | **100.0 / 100.0** |
| 真超分 6X + 跟随显示器 | 95.66 / 95.66 | **100.0 / 100.0** |

改前低于 100 Hz 刷新率，意味着每秒有 2–4 次刷新拿不到新帧；改后四轮全部精确落在上限。

### 门槛

scheduler 127、修复合同 205、FG 呈现 D3D12 errors=0 且 0 ERROR、backend-switch 16 PASS 0 ERROR。
全部工况 `errorLines=0`。

### 未验收项（如实标注）

- **低延迟队列**对"提交到扫描输出"的排队延迟的影响，本机测不到，需 PresentMon
  或外部测量设备。**不给该项任何结论。**
- **降档后的主观流畅度**：生成帧少了 71%，数值上提交率仍达 100/s，
  但 6X 降到 2X 肉眼是否可接受必须由用户实看判定。
- 默认值未改：输出上限默认关闭，只在用户显式开启时生效。

## 4. 未做项的原因与下一步

- **C3/C4/延迟 3b（采集三缓冲、锁外复制、直写 upload 堆）**：需要重构 mailbox 所有权
  （驱动写/复制/owner 读三方），并与 D3D12VA 硬件路径共存；改动面大，需实卡回归，
  本轮不冒进。建议单独一批，实卡 A/B 为门槛。
- **B5（中途硬解回退）**：需要保存位置、重开软解、seek 回位并触发历史重置；单独一批。
- **延迟 2（owner 唤醒改事件）**：`waitLive` 与 `LiveGpuScheduler` 的 wakeAt 绑定，改为
  `WaitForMultipleObjects` 需要采集源暴露事件句柄；与 C3 一起做。
- **延迟 1（专业模式 Composed 测量）**：需 PresentMon 权限或用户实屏，请用户在长测时
  对比全屏与专业模式的 `display-stats`。
- **第 7/8 批已补做** C3、C4、延迟 2、延迟 3b（有界）、B5、C7、A4/A5、E5、E6、C9、B1（有界）。
- **第 9 批收口**：B1 完整版以显式捕获实现；延迟 3b 完整版实测收益 4% 后判定不做（见 §3d）；
  延迟 1 实测 pro 与 fullscreen 无差异。
- **仍未做**：Composed vs independent flip 的确证（需 PresentMon 权限）；
  `tools/fsr*_probe` 在 worktree 下的相对路径失效。

## 5. 撤回记录

- A2（upload fence 前移）：跨队列 barrier 错误 49 条，撤回，原因已写入代码注释。
- B6（删 `recovering()`）：单测使用，撤回。
- D4 原方案（抽屉右移）：720 px 仍重叠，改为截图按钮入竖栏。

## 6. 用户长测请看

1. `b3456/ab-final` 的收益是否在 120 s 与实卡上复现；重点 XeSS 原生 4X 的 Present 阻塞
   和 DLSS 6X 的零长空档。注意 §3c.1：原生 XeSS 4X 的 0.73 ms 只在 GPU 均值 87% 的时段
   出现，之后稳定在 11–12 ms；长测请记录 NVML GPU 均值与 `gpuReadyP95` 一起看。
1c. 第 9 批矩阵已把第 8 批的“变慢”证伪（§3d），长测请以第 6/9 批数值为基准。
1b. 第 8 批新增：压缩采集（MJPEG）长播内存是否平稳；无 FG 1440p60 实卡 120 s 用
   `test-capture-version-comparison.ps1` 对照 `app-b6/veyra.exe` 与 `app/veyra.exe`。
2. 字幕快捷键、失败原因 toast、窄窗口截图按钮位置、弹出下拉不再自动关闭。
3. 拖边框、中键拖动缩放（字幕开启时）、诊断面板滚动。
4. 播放中拔插耳机是否在 1 s 内恢复。
5. 长测中若崩溃，`logs/` 下应有 `veyra-crash-*.dmp` 且日志末尾完整。
