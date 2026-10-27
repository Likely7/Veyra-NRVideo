# Veyra 统一修复计划（2026-09-22）

本计划合并三份同日审查：
[FG 独立修复执行记录](../reports/FG_INDEPENDENT_REPAIR_EXECUTION_2026-09-22.md)（已实施并短测）、
[全软件清扫](../reports/WHOLE_SOFTWARE_SWEEP_2026-09-22.md)（45 条新发现）、
[采集延迟复查](../reports/CAPTURE_LATENCY_REVIEW_2026-09-22.md)（5 项可降）。
基线：分支 `codex/fg-independent-repair-20260922`，HEAD `d2dc95b`，存档
`checkpoint/pre-fg-independent-repair-20260922`。旧台账
[WHOLE_PRODUCT_ISSUE_LEDGER](../reports/WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22.md) 中标为
"性能上限/暂缓/撤回"的项目不在本计划内。

> 2026-09-22 执行结果：第 1–6 批已实施，逐项状态、撤回与未做原因见
> [执行记录](../reports/UNIFIED_REPAIR_EXECUTION_2026-09-22.md)。下文为施工前的计划原文。

## 0. 总则

- 每批开工前打 tag `checkpoint/plan-b<N>-pre-<日期>`，在同一隔离分支顺序施工；
  一批未过门槛不进入下一批。
- 每条改动"一次最小实现，最多一次有新证据的修订"，否则撤回并写入
  [实验索引](../reports/FG_EXPERIMENT_INDEX_2026-09-21.md)。
- 产物统一写 `E:/项目/Veyra/{build,tests,logs,tmp}/plan-b<N>-<日期>/`；构建沿用本日
  在会话内导入 vcvars 的方式（`build-isolated.ps1` 的子进程路径在本机遇到 D8050，
  见执行记录），产物 exe 复制到 `tests/.../app/`（运行库为 junction）。
- Agent 只做 30 s 短测与单元/合同测试；120 s 以上、实卡、多显示器、HDR 屏、肉眼
  画质由用户验收。
- 不合并 main、不推送、不发布；每批结束更新 WORKLOG 与本文档的状态列。

## 1. 已完成（第 0 批，待用户长测）

| 编号 | 改动 | 对照开关 | 短测结果 | 用户长测看什么 |
| --- | --- | --- | --- | --- |
| X1 | XeSS `frameRenderTime` 传真实源周期 | `VEYRA_DISABLE_XESS_SOURCE_TIMING=1` | 消除提供方双稳态（旧路径 38 vs 54 源/s），组内 4.3 ms | 真超分+NR+4X 120 s 是否稳定 ≥45 源/s，不再掉到 38 |
| F2 | ≤2 帧预览跳帧保留 NR/XeSS/DLSS 历史 | `VEYRA_TEST_PREVIEW_SKIP_RESET=1` | 重置 177→1，原帧间隔 p95 29→22 ms | 快速运动是否新增拖影；有则改 `<=1` 或撤回 |
| F3 | DLSS 超预算对改可呈现 2X 组 | `VEYRA_TEST_FG_NO_REDUCED=1` | 原生 6X 长空档 73→3，p95 16.7→8.5 ms | 6X/2X 组交替是否可察；UI 已提示当前对 2X |
| F4 | 记录刷新率与 DXGI 帧统计差分 | `VEYRA_TEST_NO_DISPLAY_STATS=1` | 本机 100 Hz 上应用提交全部被扫描出 | `display-stats` 行 `notDisplayed` 是否均匀 |

已撤回并记录：X2（XeLL 关低延迟，提供方 -15）、X3（Present 辅助线程，源 51→24/s）。

## 2. 第 1 批：UI 正确性（预计 1 天，风险低）

目标：用户可见的错误反馈与重叠。全部是局部改动，无引擎影响。

| 编号 | 位置 | 改动 | 验收 |
| --- | --- | --- | --- |
| D1 | `AppShell.cpp:399,773,790` | 删除永远隐藏的 `statusBar`；字幕快捷键/自动对齐/打开失败文本改为 2 s toast（复用 `ColourStatus` 或字幕 overlay 顶行）；`EmptyHint` 在 `currentFile.empty()\|\|s.failed` 时真正显示失败原因 | 手工：按 Z/X/T/Y/B 有反馈；打开损坏文件看到具体原因 |
| D3 | `AppShell.cpp:378,384` | `ColourStatus` 非空时 `MediaTitle` 宽度减去状态宽度加 8 px；两者不再重叠 | `ui-layout-dpi.py`；720/960/1080/1280 四宽截图 |
| D4 | `AppShell.cpp:363,365` | `InspectorDrawer.x = max(w-384, g.left+244+76+8)` | 同上，`w=720` 无重叠 |
| D5 | `AppShell.cpp:649` | `Master` 分支检查 `requestSettings` 返回值，false 时还原 `uiState.enhanced` 并提示（与 `:636` 一致） | `ui-fg-backends.py`；手工：XeSS 6X 请求被拒时按钮状态 |
| D7 | `AppShell.cpp` `windowTimer` 开头 | `if(veyra::ui::popupSelectorOpen())return 0;` | `veyra_popup_selector_tests`；手工：打开下拉时不自动关闭 |
| D11 | `AppShell.cpp:592`，`SettingsWindow.cpp:1815` | 状态 sink 改 `setText`；`message()` 记上次文本不重复 `SetWindowText` | 手工：XeSS 降级期间底栏不闪 |
| D15a | `AppShell.cpp:648` | `WindowMax` 改 `ShowWindow(SW_MAXIMIZE/SW_RESTORE)`；`:1308` 'V' 排除 Ctrl | 手工 |
| B12 | `Subtitles.cpp:609,613,615,618` | 删除 4 行空 "skipped" 日志 | 日志无空行 |

门槛：`veyra_ui_contract_tests`、`veyra_popup_selector_tests`、`veyra_subtitle_panel_tests`、
`ui-layout-dpi.py`、`ui-fg-backends.py`、`ui-stack-budget.py` 全过；截图对比无新重叠。
撤回条件：任一合同测试失败或截图出现新重叠。

## 3. 第 2 批：UI 性能与退出（预计 1 天，风险低）

| 编号 | 位置 | 改动 | 验收 |
| --- | --- | --- | --- |
| D2 | `TelemetryWindow.cpp:43,53` | `WM_TIMER` 首行 `if(!IsWindowVisible(h))return 0;`；更新前后保存/恢复 `EM_GETFIRSTVISIBLELINE` 与 `EM_GETSEL` | 手工：诊断面板可滚动选中；面板关闭时进程 CPU 下降 |
| D6 | `AppShell.cpp:222,243`，`Subtitles.cpp:650` | `alignSubtitleToAudio` 增加 `std::stop_token`，读包循环每 64 包检查；jthread lambda 接 token；`WM_DESTROY` 里 `request_stop()` | 手工：对齐进行中关窗 ≤1 s 退出 |
| D8 | `AppShell.cpp:1190,358,406–411`，`SettingsWindow.cpp:1217` | `WM_ENTERSIZEMOVE` 期间 `WM_SIZE` 只置脏标记，由 16 ms 定时器合并 `layout()`；`SetWindowRgn` 只在尺寸变化时重建；去掉 `RDW_UPDATENOW` | `ui-window-resize.py`、`ui-native-resize.py`；拖边框 5 s 内 layout 次数日志 |
| D9 | `AppShell.cpp:720,812`，`SeekPreview.h:23`，`CapturePanel.cpp:93–95`，`LiveStatusPanel.h:15,23` | tick 内取一次 `PlayerSnapshot` 传引用；CapturePanel 三个 getter 共用同一份 | 代码审查 + 上述 UI 脚本 |
| D10 | `AppShell.cpp:812` | 音量滑块先 `TBM_GETPOS` 判等 | 手工 |
| D12 | `AppShell.cpp:619`，`SettingsWindow.cpp:274–279` | 偏好保存改 1 s 合并延迟（定时器），`WM_CLOSE` 立即保存；折叠状态不再先 load 再 save | `veyra_repair_preset_tests`；手工：连续改设置后重开保留 |
| D13 | `WorkspaceChrome.h:18` | 按 (size,weight,dpi) 缓存 HFONT，DPI 变化时清空 | `ui-display-transition.py` |
| D14 | `SubtitleOverlay.cpp:43,74` | 签名去掉 zoom/center，改为只在尺寸/内容变化时重建 DIB；zoom/center 变化只重绘 | `veyra_subtitle_overlay_tests`；手工：中键拖动流畅 |
| D15b | `RemotePlayPanel.cpp:216`，`CapturePanel.cpp:64` | worker 用世代号 + detached，结果投递 `PostMessage`；全局 `std::future` 改成员并在 `WM_DESTROY` 前 cancel | 手工：枚举进行中关窗不卡 |
| E4 | `ExportJobManager.cpp:40` | 析构 cancel 后最多等 1 s，超时交给 Job 对象 KILL_ON_CLOSE | `veyra_export_worker_failure_tests` |

门槛：第 1 批全部脚本 + `ui-window-resize.py` + `veyra_subtitle_overlay_tests`；
`ui-stack-budget.py` 栈深不退步。撤回条件同第 1 批。

## 4. 第 3 批：日志与采集线程（预计 1 天，风险中）

| 编号 | 位置 | 改动 | 验收 |
| --- | --- | --- | --- |
| C1 | `Log.cpp:101–108` | 4 个 `std::regex` 改 `static const`；诊断事件提取移到 `mutex_` 外（先格式化到局部，再进锁只做队列/写文件） | `veyra_repair_contract_tests`；microbench：1000 条 warn 耗时 |
| C3+采集延迟 3 | `CaptureCardSource.cpp:394–459`，`NativeCaptureSink.cpp:137` | 三缓冲：回调持锁只交换指针并记录时间戳，`copyCaptureSample` 在锁外；`log::warn` 在锁外按计数限频 | `veyra_capture_tests --rate-test`；实卡 4K60 30 s：`capture-callback entryToLockMs` p95 下降、`mailboxOverwritten` 不上升 |
| C4 | `CaptureCardSource.cpp:429` | 压缩 payload 预分配池（按 `compressedQueueLimit`+2） | `veyra_capture_compressed_tests` |
| C5 | `CaptureAudioSession.cpp:51–63` | `deque<float>` 改环形 `vector<float>` + 读写游标 | `veyra_capture_audio_tests`、`veyra_audio_timeline_tests` |
| E1 | `NvencD3D12Encoder.cpp:28` | `check()` 仅失败或前 8 次记录；每秒一条汇总 | `veyra_export_probe` 30 s 导出，日志行数 <100 |
| E5 | `EngineController.cpp:1601` 等 | 每秒 4–5 条 info 合并为一条 `player-second` 行；其余转 `verboseFrameLogs()` | 分析脚本 `analyze-fg-cadence.py`/`fg-independent-ab.py` 改读新行 |
| 采集延迟 4 | `EngineController.cpp` run() 入口，`CaptureCardSource.cpp` 回调线程首次进入 | `AvSetMmThreadCharacteristicsW(L"Pro Audio")`，失败忽略并记一条日志；退出 `AvRevertMmThreadCharacteristics` | 实卡 30 s：`real_callback_to_present_return` P99/max 下降 |

门槛：上述单测 + `test-capture-version-comparison.ps1` 30 s A/B（用户实卡）：
P50 不退步、P99 下降、丢帧不上升。撤回条件：任一实卡指标退步或 `capture-discontinuity` 新增。

## 5. 第 4 批：解码与音频韧性（预计 1 天，风险中）

| 编号 | 位置 | 改动 | 验收 |
| --- | --- | --- | --- |
| B4 | `MediaFileSource.cpp:307–311` | `pts` 取 `frame->best_effort_timestamp`，再 `pkt_dts`，仍无才 unknown | 新增单测素材：裸 H.264 ES；`veyra_source_tests` |
| B5 | `MediaFileSource.cpp:39` | 中途硬解失败：记录当前 PTS，重开软解并 seek 回该位置，标记 `Discontinuity` | `veyra_source_tests` 注入解码错误 |
| C2 | `MediaFileSource.cpp:52,173` | 软解线程 `min(4, hardware_concurrency())`；仅低延迟采集限 1 | 1080p60 软解 30 s `previewSkipped` 下降 |
| C6 | `WasapiAudioSink.cpp:358` | 注册 `IMMNotificationClient`，默认设备变化时主动重建端点（复用现有恢复路径） | 手工：播放中拔插耳机 |
| C7 | `WasapiAudioSink.cpp:164–168` | 格式变化时尝试重建 resampler；失败上报 `audioEndpointError` | `veyra_audio_track_output_tests` |
| E3 | `VideoExportJob.cpp:225–229` | 超时/取消设置具体 `failureReason` | `veyra_export_worker_failure_tests` |

门槛：`veyra_source_tests`、`veyra_player_tracks_tests`、`veyra_multichannel_tests`、
`veyra_capture_compressed_tests`；三段手工素材。撤回条件：任一现有素材播放退步。

## 6. 第 5 批：引擎热路径与采集延迟（预计 2 天，需 A/B，风险中高）

每条独立 checkpoint，用 `fg-independent-ab.py` 同工况 30 s 交错 A/B。

| 编号 | 位置 | 改动 | 预计收益 | A/B 判定 |
| --- | --- | --- | --- | --- |
| A1 | `EngineController.cpp:727`，`PresentSink.cpp:27` | 缓存 `IDXGIFactory1`/`IDXGIOutput6`，仅 `WM_DISPLAYCHANGE` 或监视器句柄变化时重建；查询结果由 UI 线程投递 | HDR 工况每 2 s 省 1–5 ms owner 阻塞 | HDR 素材：`hdr-query-stall` 为 0，`engine-stall` 段数下降 |
| 采集延迟 2 | `EngineController.cpp:650` `waitLive`，`DeadlineWait.h` | 等待改为 `WaitForMultipleObjects{采集事件, 定时器}`；采集回调 `SetEvent` | 无 FG 约 0.5 ms | 实卡：`mailbox_until_read` P95 下降 |
| 采集延迟 3b | `EnhanceGraph.cpp:1507`，`CaptureCardSource.cpp` | 回调直写 upload 堆三缓冲，去 AVFrame 中转（与第 3 批 C3 衔接） | 0.5–0.9 ms | 实卡：`real_callback_to_ready` 下降；`veyra_cpu_yuv_upload_tests` 像素一致 |
| A2 | `EnhanceGraph.cpp:1327,2014` | `uploadFences_[parity]` 改为颜色转换阶段后的 fence；FG 输出仍由 `generatedLeases_` 保护 | SR+NR+6X 减少 owner CPU 阻塞 | `gpuReadyP95` 下降；`frame-pool still leased`=0；`fgInvalid`=0 |
| 采集延迟 5 | `LivePairLatency.h:29,30` | 余量 `+10000`→`+3000`，步长 `2500`→`5000` | 4X 1–2 ms | 实卡：`real_ready_to_present_begin` 下降，`generatedExpiredAfterEval` 不上升 |
| A6+B3 | `EngineController.cpp:141,1151,1559`，`FrameFlowWindow.h:137` | `snapshot()` 先拷 `snapshot_` 再释放 `mutex_`；flow 快照缓存 100 ms；`:1151` 改专用 getter | 减少 owner/UI 锁竞争 | `engine-stall controlAndSchedule` 段数下降 |
| A4+A5 | `EnhanceGraph.cpp:1266,1344` | `DiagnosticEvent` 改 POD；直方图/采样缓冲改成员 | 每帧省约 10 次分配 | `graphSubmitP95Ms` 不上升（可能小降） |
| A7+B7 | `CommandSlotRing.cpp:133`，`EnhanceGraph.cpp:2039` | 时间戳回读仅 profile 开关下 Map；`fgDisableReadback_` 常驻 Map | 每帧省 3–8 次 Map | 同上 |
| A8 | `EngineController.cpp:908–921` | 暂停时仅在 `refreshPausedFrame_`/视图/尺寸变化时 present，否则等 100 ms | 暂停时 GPU/CPU 占用下降 | 暂停 30 s 功耗/占用 |
| 采集延迟 1 | `AppShell.cpp:358`，`SubtitleOverlay.cpp:58` | **先测量**：专业模式（圆角 region）vs 全屏，`display-stats`/PresentMon 的呈现模式；若 Composed，圆角移入 blit 着色器，字幕 overlay 改为在视频 blit 后合成 | 可能省一帧合成（8–16 ms） | PresentMon `PresentMode=Hardware Independent Flip` |

门槛：每条 A/B 后源率、`epoch`、`gapsOver16_667ms` 不退步超过两次基线波动
（源率 ±3/s、gaps ±10）；实卡指标见各行。撤回条件：任一退步或新增 D3D12 错误。

## 7. 第 6 批：健壮性（预计 1 天，风险低）

| 编号 | 位置 | 改动 | 验收 |
| --- | --- | --- | --- |
| B2 | `EngineController.cpp:63`，`apps/veyra/main.cpp` | `SetUnhandledExceptionFilter`：写 minidump 到 `logs/`，`Logger::flush()`；`run()` 外层 SEH 包装（无对象析构的 helper） | 注入 `VEYRA_TEST_RAISE_SEH`：日志末尾完整、dmp 存在 |
| B1 | `EngineController.cpp:1319` | step lambda 需要的状态收进 `LiveStepContext`，lambda 只捕获它；声明顺序加 `static_assert`/注释 | 编译 + `veyra_live_presentation_tests`、`veyra_fg_presentation_tests`、backend-switch |
| B6 | `FgRecoveryBudget.h:91` | 删除无人读取的 `recovering()` 或接 UI | 编译 |
| C8/C9/E6 | `NativeCaptureSink.cpp`、`FFmpegVideoDecoder.cpp:643`、`CommandSlotRing.h:112` | 注释级：EAGAIN 上限与池关联；`gpuCommandTimesMs_` 改环形 | 编译 |

## 8. 用户决定项（不在批次内，等指示）

| 项 | 内容 | 影响 |
| --- | --- | --- |
| U1 | 采集延迟"低延迟档"默认值（NR 关/720 内部、FG 关） | 约 4–6 ms，画质换延迟；已是手动选项 |
| U2 | F2 上限 2 帧还是 1 帧 | 取决于长测拖影观感 |
| U3 | F3 的 UI 呈现方式（当前"目标 6X / 当前 2X"提示） | 用户可见性 |
| U4 | 第 5 批"采集延迟 1"若证实 Composed：圆角进着色器会改变专业模式外观 | 外观 |

## 9. 明确不做

- 不恢复 XeSS suppress 门、不取消 DLSS 准入、不加固定等待或缓冲、不自动降倍率。
- 不做外推代替插值、不做时间线倒退呈现。
- 不重写 UI 框架、不引入新依赖、不改 NGX/XeFG 调用顺序。
- 不把 A 类"预计收益"当已测；第 5 批每条 A/B 后才计入。
- 已撤回的 X2、X3、以及实验索引里的全部旧方向不重试。

## 10. 时间与顺序

| 批次 | 内容 | 预计 | Agent 门槛 | 用户验收 |
| --- | --- | --- | --- | --- |
| 0 | FG 修复（已完成） | — | 已过 | 120 s 长测、肉眼 |
| 1 | UI 正确性 | 1 天 | 合同测试 + 截图 | 抽查 |
| 2 | UI 性能与退出 | 1 天 | 同上 + 缩放脚本 | 拖动手感 |
| 3 | 日志与采集线程 | 1 天 | 单测 + 30 s 实卡 | 120 s 实卡 |
| 4 | 解码与音频韧性 | 1 天 | 单测 + 三段素材 | 自有素材 |
| 5 | 引擎热路径与采集延迟 | 2 天 | 30 s A/B | 120 s A/B、实卡 |
| 6 | 健壮性 | 1 天 | 注入测试 | — |

第 1、2 批可以并行准备但顺序提交；第 3 与第 4 批互不依赖；第 5 批依赖第 3 批的 C3。
每批结束在本文档"状态"列写"已修/已撤回/保留"，并更新 WORKLOG。
