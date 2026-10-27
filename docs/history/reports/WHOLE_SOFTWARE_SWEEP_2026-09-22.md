# 全软件清扫排查与整改方案

日期：2026-09-22。基线 `codex/fg-independent-repair-20260922` `dc2e76a`（含本日 FG 修复）。
方法：只读源码审计（引擎/管线/gfx 由本人逐点核对；UI/源/汇由审计代理通读后本人抽样
复核关键条目）加既有运行证据。本文列出的每条都有 `file:line`，未运行新的性能测试；
"预计收益"是按代码路径估算，不是实测。已有台账
[WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22](WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22.md) 里的
未解决项不重复列出，只列本次新发现或新证据。

判定口径：**确认**＝代码路径成立且能给出触发条件；**推断**＝代码成立但用户可见度依赖
工况；**可疑**＝需要一次实验确认。行号对应本工作树，将来按符号定位。

## 0. 总览

| 类别 | 条数 | 最高优先级 |
| --- | --- | ---: |
| A 引擎/管线性能拖累 | 8 | P1 |
| B 引擎/管线隐性 bug 与生命周期 | 7 | P1 |
| C 输入源/解码/音频 | 9 | P1 |
| D UI 交互与绘制 | 15 | P1 |
| E 日志/诊断/导出 | 6 | P2 |

全库无 `TODO/FIXME/HACK` 注释；`WM_ERASEBKGND`、双缓冲、`WS_CLIPCHILDREN`、DPI 合并
刷新、全屏切换、seek 预览单飞、导出进程隔离、偏好原子写入均已核查为正确，不列入。

## A. 引擎/管线性能拖累

| 编号 | 位置 | 问题 | 触发/代价 | 判定 | 最小修复 |
| --- | --- | --- | --- | --- | --- |
| A1/P1 | `EngineController.cpp:727–732`，`PresentSink.cpp:27–58` | HDR 输入或 Video HDR 开启时，owner 线程每 2 s 调 `displayHdrActive()`：`CreateDXGIFactory1` + 枚举全部适配器/输出 + `GetDesc1` | 每次约 1–5 ms（驱动侧），在 owner 线程上，与用户"间歇卡顿"周期量级相近；只影响 HDR 工况 | 确认 | 缓存 `IDXGIFactory`/`IDXGIOutput6`，仅在 `WM_DISPLAYCHANGE`/监视器变化时重建；查询移到 UI 线程投递结果 |
| A2/P1 | `EngineController.cpp:1327`，`D3D12DeviceContext.cpp:274` | `graph.process` 入口 `waitForFenceValue(uploadFences_[parity])` 是 CPU 阻塞等待，`uploadFences_` 在 FG 之后赋值（`EnhanceGraph.cpp:2014`），即等待"两帧前整组含 FG 的 GPU 工作"完成 | DLSS 6X 每组 19–24 ms GPU，两帧前工作通常已完成，但 SR+NR+6X 下会周期性阻塞 owner 数 ms；这是 `gpuReadyP95` 25 ms 的组成部分之一 | 推断 | 把 upload fence 改为"上传+颜色转换"阶段的 fence（在 `submitAndSignal` 后 FG 前记录），FG 输出用已有 `generatedLeases_` 保护 |
| A3/P2 | `EngineController.cpp:1303,1314,1319` | 每源帧 2 个 `make_shared` + 1 个大捕获 `std::function`（捕获 12 个值），再加 `LiveGpuScheduler::push` 的 `std::function` 拷贝 | 每帧约 3–4 次堆分配，60 fps 下可忽略，但 6X 时 step lambda 每次执行都触摸 shared_ptr | 确认 | 预分配 2 个 `CompletionWatch`/`LiveStepState` 对象池（occupancy 上限 2） |
| A4/P2 | `EnhanceGraph.cpp:1266–1268` | 每帧构造 `DiagnosticEvent`（8 个 `std::string` + `std::to_string`）并 `move` 到 thread_local | 每帧约 8 次字符串分配，只在 warn/error 时被读取 | 确认 | 改为只存 POD（identity、分辨率、指针到静态字符串），或仅在 `verboseFrameLogs()` 时构造 |
| A5/P2 | `EnhanceGraph.cpp:1344–1360` | 每帧 `std::vector<double> hist(256)` + `std::vector<uint8_t> sample(2304)` + `previousLuma_` move；CPU 采样 64×36 像素 | 每帧 2 次分配 + 2304 次访存；硬解路径下这是从 GPU 纹理读的？否，从 CPU 帧读，硬解时 `sampleLuma` 走 `frame->data[0]`，D3D12VA 帧 `data[0]` 是 `AVD3D12VAFrame*`，不会走这里 | 确认（仅软解/上传路径） | 复用成员缓冲；直方图用 `std::array<double,256>` |
| A6/P2 | `EngineController.cpp:1151,1460,1559,1601` | 每源帧 3–4 次 `frameFlow->snapshot()`，每次在互斥内深拷贝 `FrameFlowMetrics`（4 个 `std::array` + optional），且每 250 ms 一次对 8192 条 timing 环做 `nth_element` | 每帧约 1 KB 拷贝 ×4，加 UI 线程每 100 ms 再拷 3 次（见 D）；互斥竞争 owner 与 UI | 确认 | `:1151` 只需 Fg1 p95，改为专用 getter；`:1559` 每帧一次即可；UI 侧取一次 |
| A7/P2 | `CommandSlotRing.cpp:133–141` | 每次 `acquire` 都 `Map/Unmap` 时间戳回读缓冲（每帧 3–8 次）并 `push_back` 到 `gpuCommandTimesMs_`（16384 上限后不再记录，不清理） | 每帧多次 Map 调用（驱动锁），且诊断向量在 16384 后静默失效 | 确认 | 仅 `verboseFrameLogs()` 或显式 profile 开关下回读；向量改环形 |
| A8/P3 | `EngineController.cpp:908–921` | 暂停循环每 16 ms：`drainLivePresentation()`（cancel 调度器）+ `present()` 同一帧 + `sleep_for(16)` | 暂停时 60 次/秒重新 blit 同一帧并 Present；XeSS 暂停时 provider 仍被喂帧 | 确认 | 暂停后只在 `refreshPausedFrame_`/尺寸变化/视图变化时 present，否则 `WaitMessage`-式等待 100 ms |

## B. 引擎/管线隐性 bug 与生命周期

| 编号 | 位置 | 问题 | 失败场景 | 判定 | 最小修复 |
| --- | --- | --- | --- | --- | --- |
| B1/P1 | `EngineController.cpp:1319` step lambda `[&]` | 捕获整个 `run()` 作用域引用，包含 `nowMs`、`audioPipe`、`cadence`、`presentationEffective`，lambda 在 `LiveGpuScheduler` 队列中最多存活 2 个源帧；`run()` 退出前 `OnExit stopPresentation` 先 `liveScheduler.reset()`，顺序正确 | 目前无悬垂；但任何在 `stopPresentation` 之后声明的新局部变量被 lambda 引用就会悬垂，且编译器不会报错 | 推断（结构风险） | 把 step 需要的状态收进一个 `LiveStepContext` 结构体，lambda 只捕获它的引用；加静态断言/注释锁定声明顺序 |
| B2/P1 | `EngineController.cpp:63` `dispatch()` | 只捕获 `std::exception`；SEH（访问违规、NGX 内部异常）直接终止进程，无日志 flush | NGX/XeSS 运行库内崩溃时诊断日志最后 250 ms 丢失，用户只看到程序消失 | 确认 | `run()` 外层加 `__try/__except` 包装（独立无对象析构的 helper）并在 filter 中 `Logger::flush()`；或进程级 `SetUnhandledExceptionFilter` 写 minidump + flush |
| B3/P1 | `EngineController.cpp:141–159` `snapshot()` | 在 `mutex_` 内调用 `activeFlow_->snapshot()`（另一互斥 + 可能的 `nth_element`），而 owner 线程每帧 36 处 `lock_guard(mutex_)` | UI 每 100 ms 3 次、设置窗 4 次/秒、诊断窗 4 次/秒：owner 在 `mutex_` 上的等待会进入 `engine-stall` 的 `controlAndSchedule` 段（已观察到 p90 58 ms 的段，虽主因是 Present） | 推断 | `snapshot()` 先拷贝 `snapshot_` 再释放 `mutex_`，然后再取 flow 快照；flow 快照结果缓存 100 ms |
| B4/P2 | `EngineController.cpp:967` + `MediaFileSource.cpp:307–311` | 任一解码帧 `pts==AV_NOPTS_VALUE` 即 `status("视频解码或时间戳错误",true)` 停播；解码器已算好 `best_effort_timestamp`（`FFmpegVideoDecoder.cpp:701`）但源不用 | 裸 H.264/部分 TS/AVI 在中途一帧缺 pts 就整段停止 | 确认 | 源侧 `pts` 取 `best_effort_timestamp`，再退化 `pkt_dts`；仍未知才报错 |
| B5/P2 | `MediaFileSource.cpp:39` | 硬解→软解回退只允许 `framesRead_==0` | 播放中途驱动/池失败（`receiveStatus()==Error`）直接"视频解码失败" | 确认（有注释说明为首帧策略） | 中途失败记录当前 PTS，重开软解并 seek 回该位置，历史 reset |
| B6/P2 | `EngineController.cpp:1091` + `FgRecoveryBudget` | 设置 revision 变化时 `fgBudget.reset()`，但 XeSS 后端下 `fgBudget` 从不喂样本，`fgBudgetLimited` 快照字段永远 false；DLSS 侧 `recovering()` 无人读取 | 无功能错误；死状态 | 确认 | 删除 `recovering()` 或接到 UI 提示 |
| B7/P3 | `EnhanceGraph.cpp:2039–2043` | 每个生成帧 `resolveFrame` 时 `Map/Unmap` 4 字节回读 | 6X 每源帧 5 次 Map；`Map` 是驱动调用非 GPU 等待，代价小但可合并 | 确认 | 一次 Map 常驻（READBACK 堆允许持久映射），只读指针 |

## C. 输入源 / 解码 / 音频

| 编号 | 位置 | 问题 | 用户可见 | 判定 | 最小修复 |
| --- | --- | --- | --- | --- | --- |
| C1/P1 | `Log.cpp:101–108` | 每条 warn/error 在全局 `mutex_` 内构造 4 个 `std::regex`（编译毫秒级）再 `fflush`；调用方含采集回调线程（`CaptureCardSource.cpp:409/418/435`，且在采集 `mutex` 内）、音频线程 | 警告风暴期间采集回调与音频线程在日志锁上排队；这是台账 C1"1 秒断供"未排除的软件侧候选之一 | 确认 | `static const std::regex` 预编译；诊断事件提取移到锁外；采集回调内只计数，锁外限频记录 |
| C2/P1 | `MediaFileSource.cpp:52,173` | ≤1080p 软解只用 1 个线程 | 无硬解时 1080p60 H.264/HEVC 软解低于实时 → 卡顿 | 确认 | `min(4, hardware_concurrency())`，仅低延迟采集限 1 |
| C3/P1 | `CaptureCardSource.cpp:394–459` | 4K 帧 `copyCaptureSample`（memcpy 12–24 MB）在采集 `mutex` 内，`tryRead` 同锁 | 回调期间 owner 的 `tryRead` 被阻塞约 1–3 ms/帧；与 A/B 无关但叠加进 owner 预算 | 推断 | 双缓冲：回调持锁只交换指针，复制在锁外进行 |
| C4/P2 | `CaptureCardSource.cpp:429` | 压缩路径每样本 `vector::assign` 新分配（MJPEG 4K 数 MB）在锁内 | 分配抖动进入采集回调 | 确认 | 预分配 payload 池 |
| C5/P2 | `CaptureAudioSession.cpp:51–63` | `pull()` 逐样本 `deque` 拷贝与 `pop_front`（8ch/48k 每 10 ms 约 3840 次） | 与 `push()`（采集线程）争锁 | 确认 | 环形 `std::vector<float>` + 游标 |
| C6/P2 | `WasapiAudioSink.cpp:358,633` | 渲染端点固定默认设备，无 `IMMNotificationClient`；设备切换靠出错后 500 ms 重试 | 插拔耳机先静音报错再恢复 | 确认 | 注册端点通知，收到默认设备变化时主动重建 |
| C7/P2 | `WasapiAudioSink.cpp:164–168` | 解码音频布局/采样率中途变化只写日志并 `stopFlag_=true`，`snapshot_.status` 不更新 | 无声且 UI 无提示 | 确认 | 上报 `audioEndpointError` 并尝试重建 resampler |
| C8/P3 | `NativeCaptureSink.cpp:137–149` | `Receive` 在 `recursive_mutex` 内调回调（含 4K memcpy），`Stop()/GetState()` 被整个回调阻塞 | 仅关闭延迟 | 确认（DirectShow 语义可接受） | 与 C3 一并处理 |
| C9/P3 | `FFmpegVideoDecoder.cpp:643` | EAGAIN 缓冲上限 16 帧，D3D11VA 时每帧持有一张池表面 | 目前调用方 send 后立即 receive，难触发 | 可疑 | 注释级提醒，上限改为与池大小关联 |

## D. UI 交互与绘制

| 编号 | 位置 | 问题 | 用户可见 | 判定 | 最小修复 |
| --- | --- | --- | --- | --- | --- |
| D1/P1 | `AppShell.cpp:399,773,790` | `statusBar` 每次 `layout()` 都被 `pos(...,false)` 隐藏，但字幕快捷键/自动对齐/失败原因文本都写进它 | 按 Z/X/T/Y/B、自动对齐、打开失败时屏幕无任何反馈；`s.status` 失败原因从不显示 | 确认 | 路由到可见的 `ColourStatus` 或字幕 overlay 2 s toast；`EmptyHint` 在 `currentFile.empty()||s.failed` 时真正显示 |
| D2/P1 | `TelemetryWindow.cpp:43,47,53` | 诊断面板 250 ms `SetWindowText` 整段 EDIT；面板隐藏时也在跑（`WS_CHILD` 不可见仍收 `WM_TIMER`） | 用户无法在诊断文本里滚动/选中；进程全程每 250 ms 多一次完整快照 + 字符串拼装 | 确认 | `WM_TIMER` 首行 `if(!IsWindowVisible(h))return 0;`；更新前后保存/恢复 `EM_GETFIRSTVISIBLELINE`、`EM_GETSEL` |
| D3/P1 | `AppShell.cpp:378,384` | `ColourStatus`（x=tw−600…tw−160）与 `MediaTitle`（右端 tw−162）区域重叠，两者不透明；`front()` 只含 MediaTitle | 设置状态文字被文件名盖住/互相闪烁 | 确认 | 状态非空时缩 MediaTitle 宽度，或独立一行 |
| D4/P1 | `AppShell.cpp:363,365`，`WorkspaceChrome.h:11` | 专业模式 `w<772` 时 `Save`[312,388] 与 `InspectorDrawer`[336,412] 重叠 52 px（最小宽 720） | 两按钮互盖、点击命中错乱 | 确认 | Drawer x 取 `max(w-384, g.left+244+84)` |
| D5/P1 | `AppShell.cpp:649` | 总增强开关忽略 `requestSettings` 返回值；能力门拒绝时 `desired_` 不变，`masterPendingRevision` 回滚条件永不成立 | 按钮显示"已开启"而引擎仍关闭 | 确认 | 返回 false 时立刻还原 `uiState.enhanced` 并提示（`enhancementCommand` :636 已这样做） |
| D6/P1 | `AppShell.cpp:222,243`，`Subtitles.cpp:650–670` | `subtitleAlignWorker` jthread 的 lambda 不接 `stop_token`，对齐循环无取消检查（上限 1800 s）；静态析构 `join()` | 对齐进行中关窗，进程后台挂到分析完成；`WM_DESTROY` 已释放 engine 后仍写日志 | 确认 | `alignSubtitleToAudio` 增加 `stop_token`，读包循环检查；`WM_DESTROY` 里 `request_stop()` |
| D7/P1 | `PopupSelector.h:68` | 弹窗嵌套 `GetMessageW(nullptr)` 循环分发主窗 `WM_TIMER`，`windowTimer` 内可 `layout()`/`MessageBoxW`/`openFile` | 菜单被布局刷新自动关闭；模态套模态；与历史"UI 栈耗尽"同类 | 确认 | `windowTimer` 开头 `if(popupSelectorOpen())return 0;` |
| D8/P1 | `AppShell.cpp:1190,358,406–411`，`SettingsWindow.cpp:1217` | 交互式缩放每个 `WM_SIZE` 完整 `layout()`：新建 `SetWindowRgn`、约 25 次 z-order `SetWindowPos`、`RedrawWindow(RDW_UPDATENOW)` 同步重绘色彩页 | 拖边框卡顿、控件跳动 | 确认 | `WM_ENTERSIZEMOVE` 期间把 layout 合并到 16 ms 定时器；region 只在尺寸变化时重建；去 `RDW_UPDATENOW` |
| D9/P2 | `AppShell.cpp:720,812`，`SeekPreview.h:23`，`CapturePanel.cpp:93–95`，`LiveStatusPanel.h:15,23`，`SettingsWindow.cpp:1809` | 每 100 ms tick 内 4–7 次完整 `PlayerSnapshot` 拷贝（含 wstring、vector、metrics 数组、flow 快照） | 与 B3 叠加，owner 互斥竞争 | 确认 | tick 内取一次快照传引用；CapturePanel 三个 getter 共用 |
| D10/P2 | `AppShell.cpp:812`，`Theme.h:121,45` | 音量滑块每 100 ms 无条件 `TBM_SETPOS` → 无条件 `InvalidateRect` | 每 tick 重绘滑块 | 确认 | 与 seekBar 一样先 `TBM_GETPOS` 判等 |
| D11/P2 | `AppShell.cpp:592`，`SettingsWindow.cpp:1815` | 状态 sink 直接 `SetWindowTextW`（不 diff）；有 `backendWarning` 时每 250 ms `message()` | XeSS/FSR 降级期间底栏文字 4 Hz 闪 | 确认 | sink 用 `setText`；`message()` 记上次文本 |
| D12/P2 | `AppShell.cpp:619`，`SettingsWindow.cpp:274–279` | 呈现页任一下拉变更同步写盘（`CreateFile/WriteFile/Flush/MoveFileEx`）；折叠点击先 load 再 save | UI 线程磁盘 IO | 确认 | 1 s 合并延迟保存，或 `WM_CLOSE` 统一保存 |
| D13/P2 | `WorkspaceChrome.h:18`，`LiveStatusDashboard.h` | 仪表盘每 250 ms 画 20–40 个文本，每个 `CreateFontW/DeleteObject` | GDI 对象抖动 | 确认 | 按 (size,weight,dpi) 缓存 HFONT |
| D14/P2 | `SubtitleOverlay.cpp:43,74` | 签名含 `preview.zoom/center`，缩放/拖动每 tick 重新 `CreateDIBSection`（4K 约 33 MB）并 GDI+ 重绘整幅 | 中键拖动时卡顿 | 确认 | 复用 DIB，仅尺寸变化时重建 |
| D15/P3 | `AppShell.cpp:648,1308`，`RemotePlayPanel.cpp:216`，`CapturePanel.cpp:64` | "最大化"按钮实际执行全屏；'V' 不排除 Ctrl；面板销毁在 UI 线程 join 工作线程；全局 `std::future` 退出时阻塞设备枚举 | 语义/退出延迟 | 确认 | 分别：`SW_MAXIMIZE`；检查 `VK_CONTROL`；worker 用世代号 detached |

## E. 日志 / 诊断 / 导出

| 编号 | 位置 | 问题 | 判定 | 最小修复 |
| --- | --- | --- | --- | --- |
| E1/P2 | `NvencD3D12Encoder.cpp:28,33,35,157` | `check()` 无条件 info 日志，每帧 3 条；4X 60fps 导出 720 行/秒 + 250 ms flush | 确认 | 仅失败或前 N 次记录 |
| E2/P2 | `Subtitles.cpp:609,613,615,618` | `detail` 未赋值时打印"auto align skipped:"，每次成功对齐产生 4 条空误导日志 | 确认 | 删除这 4 行 |
| E3/P2 | `VideoExportJob.cpp:225–229,274` | `resolveGeneration` 2 s 超时/取消未设 `failureReason`，用户只见通用失败文案 | 确认 | 设置具体原因 |
| E4/P2 | `ExportJobManager.cpp:40` | 析构 `WaitForSingleObject(process,5000)`；退出时若 worker 在 NVENC drain（每槽最多 10 s）卡满 5 s | 确认 | 退出路径先 cancel 再限时 1 s，超时交给 Job 对象 KILL_ON_CLOSE |
| E5/P3 | `EngineController.cpp:1601`（`output-queue` 行）等 | 每秒固定 4–5 条 info 日志（player-timing/present-deviation/output-queue/frame-trace/present-cost/display-stats），30 s 运行日志 200 KB | 确认 | 合并为一条 JSON 行或改为 `verboseFrameLogs()` 下输出 |
| E6/P3 | `CommandSlotRing.h:112` | `gpuCommandTimesMs_` 达 16384 后不再记录且不清理 | 确认 | 见 A7 |

## F. 已核查无问题（不再排查）

`WM_ERASEBKGND` 各窗返回 1 且经 `PaintBuffer` 双缓冲；`WS_CLIPCHILDREN/CLIPSIBLINGS` 齐全；
`WM_DPICHANGED` 用 `PostMessage` 合并；ALT+ENTER/F11/ESC/双击全屏路径一致并保存
`WINDOWPLACEMENT`；`SeekPreview` 单飞；`ExportJobManager` 进程隔离 + Job KILL_ON_CLOSE；
`UiPreferenceStore` 原子替换；文件 `seek()` `flushBuffers` + epoch + `discardBefore`；
`SubtitleLoader` `interrupt_callback` 可取消，`cuesAt` 二分；D3D12 调试层仅测试工具启用；
命令槽环 16 槽在所有重负载运行中 `slotWaits=0`；NVOF 输出用队列侧 Wait 无 CPU 等待；
`FrameFlowWindow` 各容器有上限；`LivePairLatency`/`FgRecoveryBudget` 样本有 64 上限与时间过期。

## G. 整改方案

### G1. 分批与顺序

按"用户可见 × 风险低"优先，每批一个隔离 checkpoint，一次最小实现，回归后再下一批。

| 批次 | 内容 | 涉及文件 | 回归门槛 |
| --- | --- | --- | --- |
| 第 1 批：UI 正确性（1 天） | D1、D3、D4、D5、D7、D11、D15（最大化语义） | `AppShell.cpp`、`SettingsWindow.cpp`、`PopupSelector.h` | `veyra_ui_contract_tests`、`ui-layout-dpi.py`、`ui-fg-backends.py`；手工：720/960/1080 三宽度截图对比，能力门拒绝时按钮状态 |
| 第 2 批：UI 性能与退出（1 天） | D2、D6、D8、D9、D10、D12、D13、D14、E4 | 同上 + `TelemetryWindow.cpp`、`SubtitleOverlay.cpp`、`WorkspaceChrome.h`、`Subtitles.cpp`、`ExportJobManager.cpp` | 拖边框 5 s 内 `WM_SIZE` 次数与 layout 耗时日志；对齐进行中关窗 ≤1 s 退出；`ui-stack-budget.py` |
| 第 3 批：日志与采集线程（1 天） | C1、C3、C4、C5、E1、E2、E5 | `Log.cpp`、`CaptureCardSource.cpp`、`CaptureAudioSession.cpp`、`NvencD3D12Encoder.cpp` | `veyra_capture_tests --rate-test`；实卡 4K60 120 s：`capture-callback entryToLockMs` p95 下降、无新 `capture-discontinuity`；导出 30 s 日志行数 |
| 第 4 批：解码与音频韧性（1 天） | B4、B5、C2、C6、C7、E3 | `MediaFileSource.cpp`、`FFmpegVideoDecoder.cpp`、`WasapiAudioSink.cpp`、`VideoExportJob.cpp` | 三段测试素材：无 pts 裸 H.264、1080p60 软解、播放中拔耳机；`veyra_capture_compressed_tests` |
| 第 5 批：引擎热路径（2 天，需 A/B） | A1、A2、A6、B3、A4、A5、A7、B7、A8 | `EngineController.cpp`、`EnhanceGraph.cpp`、`CommandSlotRing.cpp`、`PresentSink.cpp`、`GpuTimer.h` | `fg-independent-ab.py` 同工况 30 s 交错 A/B：`gpuReadyP95`、`engine-stall` 段数、源率不退步；HDR 工况 owner 无 `hdr-query-stall` |
| 第 6 批：健壮性（1 天） | B1、B2、B6、C8、C9、E6 | `EngineController.cpp`、`main.cpp` | 注入 SEH（测试开关）后日志末尾完整；生命周期测试 `veyra_fg_presentation_tests`、backend-switch |

### G2. 每批的停止/回退条件

- 任何一批使 `fg-independent-ab.py` 的源率、`epoch`、`gapsOver16_667ms` 中任一项退步超过
  30 s 短测的两次基线波动范围（源率 ±3/s、gaps ±10），该批中对应条目撤回。
- UI 批次以截图对比和合同测试为准，出现新的重叠/闪烁即撤回该条。
- 第 5 批 A2 若引入 `frame-pool still leased` 错误或 `fgInvalid>0`，立即撤回。

### G3. 不做的事

- 不重写 UI 框架、不引入新依赖、不改 NGX/XeSS 调用顺序。
- 不把 A 类"预计收益"当已测收益；每条在第 5 批 A/B 后才计入。
- 台账里已标"未解决/性能上限"的 D1/X1 等不因本清扫改变状态。

### G4. 验证产物

所有批次产物写入 `E:/项目/Veyra/{tests,logs,tmp}/sweep-<批次>-<日期>/`，每批在 WORKLOG
记录命令、结果与撤回项；本文档的"判定"列在每批结束后更新为"已修/已撤回/保留"。
