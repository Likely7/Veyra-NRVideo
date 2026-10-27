# 可关闭帧同步：本机验收与延迟报告

日期：2026-09-18。本机实现、构建、软件短测完成；完整硬件验收和 Reflex+FG 原生整合未完成。没有证明用户反馈的长期帧率波动已经根治。

## 交付与入口

- 开工存档：`6e69eeb`，tag `checkpoint/pre-frame-pacing-20260918`，包含此前 HDR、FSR4 回退和 beta 改动。
- 隔离分支：`codex/frame-pacing-20260918`；源码 `E:/项目/Veyra/worktrees/frame-pacing-20260918`。
- 本机程序：`E:/项目/Veyra/build/frame-pacing-20260918/veyra.exe`，仍标识 `1.4.2beta`，属于开发构建，依赖本机运行库目录，不是新的便携包。
- 专业模式 → **运动** → 向下滚动到 **帧同步**。默认关闭；关闭后记住模式和显示同步偏好，下次启动仍关闭。
- 未合并 main、推送、发布、重新打包。现有 1.4.2beta ZIP 不包含本轮改动。

| 选择 | 实际行为 |
| --- | --- |
| 关闭 | 不调用新增容量等待、均匀节奏或 Reflex 周期调用，恢复基线队列上限；正常音画同步及补帧依赖仍存在 |
| 低排队 | 无补帧时减少提前增强队列至一批，交换链最大排队设为 1；FG 保留两批资源窗口 |
| 均匀呈现 · 前端同步 | 低排队基础上，在 Present 前按媒体时间安排最小间隔；可有限追赶，过期生成帧跳过并统计 |
| NVIDIA Reflex · 实验 | 无补帧时原生 NVAPI Sleep/markers；开启 FG 时状态明确提示改用低排队，保留 2X/4X/6X |

显示同步独立可选“允许撕裂／垂直同步／自动”。自动目前采用 VSync，状态注明 VRR 未知，不检测或修改 G-SYNC 设置。XeSS/FSR 外部交换链保持提供方调度，新增选项明确显示暂不生效；实测 XeSS 2X 正常，未恢复 FSR UI。

“完全关闭”指关闭本轮新增策略。等待后的返回状态、关闭失败均有日志和 UI 提示；不能关闭 GPU 资源安全、媒体时钟、补帧后一帧依赖或系统/外部限速。现有 NR/SR 顺序的“低延迟模式”与此独立。

## 环境与测量口径

RTX 5070，驱动 616.56，2560×1440、100Hz、SDR。主对照素材为真实 1920×1080、24fps、E-AC3 音轨的 17.124 秒 MKV，路径 `E:/项目/Veyra/tests/1.4.2beta/visible-scene.mkv`。多数稳态对照采样 10 秒，统计日志排除开始 2 秒；帧率变体和组合效果为 6 秒短测。不是长时间稳定性证明，未测量实际音画偏移。

`ready→return` 是 **CPU 首次观测增强帧就绪，到 Present 返回** 的软件驻留时间，包含媒体定时和队列等待。它不是 GPU 执行时间，也不是 HDMI 输入、鼠标输入或屏幕发光延迟。文件可以提前解码，减少这一指标不等于影片在屏幕上提前相同时长。

PresentMon 2.5.1 实际尝试启动 ETW，失败：`failed to start trace session: access denied`，要求管理员或 Performance Log Users。没有修改系统组/权限；证据在 `tests/frame-pacing-20260918/low-initial/presentmon.stderr`。**实际显示事件、扫描节奏及屏幕端到端延迟未测量**，软件提交 FPS 不代表屏幕物理刷新率。

## 无补帧：各选项软件实测

单位 ms；P50 是中位数，P95 是 95 分位。所有以下稳态测试为 24.00 提交 fps，无源预览跳帧。

| 模式 / 显示同步 | ready→return P50 / P95 | 提交间隔 P95 | 证据目录名 |
| --- | ---: | ---: | --- |
| 关闭 | 81.548 / 82.543 | 42.371 | off-final |
| 关闭，旧非 waitable 交换链对照 | 81.602 / 82.618 | 42.470 | off-legacy |
| 低排队 / 允许撕裂 | 40.871 / 41.430 | 42.303 | low-final |
| 低排队 / VSync | 40.901 / 41.427 | 42.273 | low-vsync |
| 低排队 / 自动 | 40.952 / 41.454 | 42.335 | low-auto |
| 均匀呈现 / 允许撕裂 | 40.937 / 41.542 | 42.363 | even-final2 |
| 均匀呈现 / VSync | 40.838 / 41.464 | 42.297 | even-vsync |
| 均匀呈现 / 自动 | 40.855 / 41.537 | 42.368 | even-auto-final |
| Reflex / 允许撕裂 | 40.886 / 41.446 | 42.346 | reflex-final |
| Reflex / VSync | 40.842 / 41.447 | 42.345 | reflex-vsync |
| Reflex / 自动 | 40.871 / 41.450 | 42.322 | reflex-auto |

解释：关闭时提前增强两批，开启时一批，约减少一张 24fps 源帧的提前驻留。关闭与同一构建的旧交换链路径读数接近；不是两套完整历史二进制的性能对比。Reflex 没有显示优于低排队的额外收益，均匀模式也未证明显著改善正常素材的提交抖动。

Reflex 真实日志：`enable status=0 intervalUs=0 boost=0`；`disable status=0 successfulSleeps=252 successfulMarkers=1509`。没有逐子帧错误调用源帧 Sleep；无补帧标记围绕增强提交与呈现，不解释为游戏输入或仿真延迟。

## DLSS 补帧与刷新率

| 模式，24fps 输入 | 提交 fps | ready→return P50 / P95 ms | 间隔 P95 / P99 ms | 证据目录名 |
| --- | ---: | ---: | ---: | --- |
| 关闭，6X | 144.00 | 58.908 / 80.401 | 7.423 / 7.573 | off6 |
| 低排队，6X | 144.01 | 58.679 / 80.201 | 7.356 / 7.471 | low6 |
| 均匀呈现，6X | 144.00 | 60.689 / 80.205 | 7.348 / 7.494 | even6-final |
| Reflex 选择，实际低排队，6X | 144.00 | 58.498 / 80.213 | 7.356 / 7.458 | reflex6-final |
| 均匀呈现，2X | 48.00 | 77.549 / 81.394 | 21.389 / 21.502 | even2 |
| 均匀呈现，4X | 96.00 | 59.051 / 80.522 | 10.861 / 11.011 | even4 |
| 均匀呈现，6X + VSync | 100.00 | 53.971 / 77.182 | 11.558 / 11.965 | even6-vsync |

除最后一行外均允许撕裂。6X 的必要前后帧依赖和有界两批资源窗口保留，不能从低 GPU 占用推导为没有等待。几种模式的差别较小，不宣称 6X 大幅降延迟。

100Hz VSync 对照保持 6X 生成设置和媒体一倍速，实际提交约 100fps，最新定时日志累计记录 **445 张过期生成帧**、源预览跳帧 0。这是刷新率失配时的预览选择，不伪装成 144Hz 实际显示。普通 even4/even6-final/effects6 日志各有 1 张过期生成帧，不能称绝对零丢帧。上述过期计数是最新周期日志的累计值，不是去预热统计窗口内的精确计数。

有理数/其他帧率短测：23.976×6 → 143.85 提交 fps，25×2 → 50.00，30×4 → 120.00，60×2 → 120.00，媒体速率约 1。分别见 rate23976/rate25/rate30/rate60；不证明数小时漂移已验收。

NR + DLSS SR（1080p→1440p）+6X+均匀呈现：143.98 提交 fps，媒体速率 1.001，NR/SR/FG 真实启用。`effects6/engine.log` 中 Feature18、DLSS SR、DLSSG Create 均 `result=0x1 seh=0`，SR/FG Evaluate 同样成功，DLSSG `generatedCount=5`。该组合是运行回归，不是画质或 HDR 验收。

## 功能回归与发现的缺陷

- 设置与单测：18 种持久化组合、v1–5 迁移默认关闭、损坏文件保护、cadence reset；`preferences-final-result.txt` PASS。
- 实际 UI：三次真实启动，开启保存、关闭保存、重启恢复、1280×900/800×600、帮助展开与滚动可达，`ui-verified/result.json` PASS。已目视核对该目录大窗/小窗截图，画面非黑且新状态文本换行正常；小窗控件需滚动查看。被遮挡的一张截图跳过，不算视觉通过证据。
- 生命周期：无 FG 和 6X 分别完成暂停中关闭、暂停 seek、恢复、三模式现场切换、resize、关闭后继续播放和 stop，见 lifecycle/lifecycle6。
- 过载：注入每帧 80ms CPU 处理，无 FG/6X 均报告跳帧并维持媒体速率；关闭在 1 秒内生效，移除注入后恢复。无 FG 速率 0.970、跳过 33 帧；6X 速率约 0.922、跳过 32 帧。见 overload-final/overload6。不要拿包含过载及切换的整段汇总作稳态性能数据。
- 无音轨回归：chain-playback 保持墙钟播放速度，暂停/seek/恢复正常。
- NVENC 回归：chain-export 在 1/3/5 源帧时取消，保留 `.partial`；随后正常导出 12 源帧/12 编码帧，CreateInstance `status=0`。没有修改导出策略或加回结束逐帧扫描。
- XeSS 2X：真实提供方生成，新选项有效状态关闭、提供方继续调度，见 xess2。没有把应用 24fps Present 计数当作提供方最终显示帧率。
- 采集调度分支模拟：replay-off6-fixed/replay-low-fixed/replay-even6-fixed 通过。低排队无 FG 队列上限由实际 2 降为 1；这不是物理采集延迟测试。

本轮失败和修正保留原证据：

1. 最初把最小间隔设为完整一个输出周期，系统唤醒误差不断累积，6X 从 144 降到 136.67fps（even6-fixed）。改为媒体绝对时钟加 90% 最小间隔的有限追赶后恢复 144；不是修改 FPS 计数。
2. 控件状态 ID 1240 落入既有色彩控件范围，改为独立 ID 1150 后完成 UI 验收。
3. 采集分支 `fileAwaitingVideo` 不会被文件首帧路径清除，导致低排队上限未生效；改为显式采集条件。
4. 文件回放模拟缺少硬件到达节奏，却每批重置实时锚点。关闭也跑到约 1.895 倍速（replay-off6）；仅模拟回放改用连续 PTS，真实硬件与远程串流的成对锚点保留，复测正常。
5. PrintWindow 截图有 GPU 黑区/残留，改成验证本应用遮挡状态后截屏；误捕获的遮挡图已删除。自动断言和目视截图证据分别记录。

## 构建、复现与文件

以下命令在隔离源码目录执行过；各单项限时不超过 240 秒。`B=E:/项目/Veyra/build/frame-pacing-20260918`、`T=E:/项目/Veyra/tests/frame-pacing-20260918`、`M=E:/项目/Veyra/tests/1.4.2beta/visible-scene.mkv` 为下列简写，运行时替换为完整路径。

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/frame-pacing-20260918 -DependencyCache E:/项目/Veyra/build/fsr41-nvidia-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/frame-pacing-20260918 -DisplayVersion 1.4.2beta -Targets veyra,veyra_presentation_pacing_tests,veyra_scheduling_chain_tests
./scripts/test-frame-pacing.ps1 -Name even-final2 -Mode 1 -Seconds 10
./scripts/test-frame-pacing.ps1 -Name reflex6-final -Mode 2 -Multiplier 6 -Seconds 10
./scripts/test-frame-pacing.ps1 -Name effects6 -Mode 1 -Multiplier 6 -Seconds 6 -Scenario effects
./scripts/test-frame-pacing.ps1 -Name replay-even6-fixed -Mode 1 -Multiplier 6 -Seconds 6 -Scenario replay
./scripts/test-frame-pacing.ps1 -Name overload-final -Mode 1 -Seconds 3 -Scenario overload
python scripts/acceptance/analyze-frame-pacing.py E:/项目/Veyra/tests/frame-pacing-20260918
```

其余对照使用相同 runner 的 `-Mode -1/0/1/2`、`-Sync 0/1/2`、`-Multiplier 1/2/4/6`、`-Scenario legacy/lifecycle/xess` 与目录对应参数；帧率变体用 `-Media T/media/rate-*.mkv`。实际 UI 命令：`python scripts/acceptance/ui-frame-pacing.py B T/ui-verified M`。单测：`B/veyra_ui_contract_tests.exe T/preferences-final`；重跑必须使用新的目录，损坏配置用例会有意留下损坏文件。既有调度回归：`B/veyra_scheduling_chain_tests.exe T/media/silent24.mkv T/chain-playback playback` 及 `T/chain-export export`。均通过。

最终产品构建日志 `E:/项目/Veyra/logs/frame-pacing-20260918/build-closeout.log`，新增组合测试目标重编译日志 `build-effects-test.log`，均 exit 0。原始结果在 T 下各目录的 `result.txt`、`engine.log`、`snapshots.csv`；`summary.csv/json` 为分析脚本输出，保留中间方案与场景测试，本报告只引用明确列出的有效稳态对照。失败测试不进入汇总；功能 PASS 不自动证明性能有收益。

修改范围：PresentationSettings/ReflexSession 新增；EngineController/VideoPresenter/PresentSink 接入拥有线程、容量与生命周期；SettingsWindow/AppShell/UiPreferenceStore 接入设置和 v6 持久化；CMake、单元/集成测试、三个验收脚本及文档。NVAPI 最小 ABI 的来源固定为 NVIDIA/nvapi `87dca625e83fd89a983e19b904e5f3a580da90d2`，MIT 来源及改动记录在 THIRD_PARTY_NOTICES；没有引入 Streamline。

所有新构建、测试、下载及日志按 AGENTS 放在 `E:/项目/Veyra/`。源码 Git 不带 DLL、SDK、模型、媒体、截图或本机偏好；本轮未生成便携中间包/解压副本，tmp 为空，保留构建和必要验收证据。

## 未完成的验收与下一步

未执行 RTX30/40 实卡、真实采集设备、PS5、VRR/G-SYNC、60/120/144Hz 多显示器切换、HDR/fullscreen/OBS 组合、device-lost 和驱动失败故障注入、长时间播放或光子延迟测试。没有原用户此次逐帧记录，不能宣布其长时间抖动根因已解决。原生 Reflex+FG 的 out-of-band 标记与实际收益尚未完成，不能用回退模式的通过替代。

下一项唯一任务是让反馈者在 30/40 机器上做同素材、同 NR/SR、同倍率的关闭/低排队/均匀呈现对照，取得系统显示事件及音画证据；确认实际改善与延迟代价后再决定合并发布。
