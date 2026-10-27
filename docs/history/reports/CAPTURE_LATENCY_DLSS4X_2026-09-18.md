# VC-007PRO / NR / DLSS 4X 实卡延迟对比

## 测量合同

用户将原请求中的 XeSS 4X 改为 DLSS 4X。本轮在 `codex/frame-pacing-20260918`、基准提交 `a471474a972ef4618002925255cf999123bc0e6f` 上增加诊断与实卡测试入口，不修改同步算法。

- 实卡：VC-007PRO，DirectShow 格式索引 12，3840x2160、请求 30fps、NV12。实际回调接近 29.97fps。
- 效果：NR 实时档，NR/光流内部 1920x1080；源图、DLSS FG、图输出均为 3840x2160；SR 关闭。不是原生 4K NR。
- 本机：RTX 5070、驱动 616.56；2560x1440 / 100Hz SDR 显示器；测试窗口外框 1280x760，最后 blit 缩放到窗口。
- 四轮分别低排队、均匀呈现（用户称前端同步）、Reflex 请求、完全关闭。显示同步统一允许撕裂；均匀呈现是 Veyra 自身实现，不是集成 RTSS。
- 每轮启动后预热 10 秒，再正式采样 120 秒。真实 HDMI WASAPI 音频入口打开，输出静音；没有停掉音频链路来降低视频延迟。
- 当前 Reflex 与 FG 同开显式回退低排队。因此这次可以测 Reflex 选项的实际行为，不能宣称测到了原生 Reflex + DLSS 4X 的延迟。

## 计时边界

起点是采集驱动调用 `SampleCB` 的软件时刻；终点是呈现函数返回后的软件时刻。HDMI 扫描/采集卡内部处理/USB/驱动在回调前的延迟、DWM/显示扫描/像素响应在 Present 后的延迟均没有外部参考测量。这里的总耗时不能称为 HDMI 到屏幕的物理端到端延迟。

NV12 是未压缩图像，本路径没有 H.264/HEVC/MJPEG 解码器，不能捏造一个“采集卡解码耗时”。入站包含采集校验、锁和 CPU 拷贝，随后 mailbox 读取、GPU 上传/颜色变换、光流、NR、残差合成、三张 DLSS 插值帧、呈现等待、blit 和 Present。

CPU 与 GPU 并行，不能把 GPU 阶段再加到 CPU 总耗时里；各阶段 P95 也不能直接相加。GPU FG batch 已含 FG1/2/3。GPU 全图区间以同帧首尾 timestamp 求包络，包含依赖间隙，排除另列的 blit。CPU ready 是轮询观察到 fence 完成的时刻，并非精确 GPU 完成时刻。

按 source/arrival 关联入站与读帧，按 source/epoch/revision 关联处理与 GPU 阶段。CPU 使用同一 steady_clock；GPU 仅在 GPU 时间域求差，不跨时钟直接减。呈现延迟按正式时间窗内 Present 返回的事件统计；GPU/处理耗时按正式时间窗内到达的源帧统计。快照计数因 50ms 采样边界可能比完整事件窗少一两帧。

逐帧日志全部开启、控制台日志关闭、文件沿用 64KiB/250ms 缓冲。日志本身有测量开销，四种模式配置一致；没有加入 GPU 像素回读或额外逐 pass fence wait。只有一轮/模式，细小差异不能宣称普适收益。

驱动拒绝了请求 3 个缓冲的协商，实际分配 10 个缓冲，每个 12441600 bytes。分配容量不等于积压十帧，不能由此推算 333ms 延迟；应用 mailbox 实际积压另看测量。

## 执行与证据

所有产物位于 `E:/项目/Veyra/`：

- 构建：`build/frame-pacing-20260918`。
- 构建日志：`logs/capture-latency-20260918/build.log`。
- 原始日志、结果、快照、关联 CSV、汇总：`tests/capture-latency-20260918`。
- 子进程临时目录：`tmp/frame-pacing-20260918`。

工作目录 `E:/项目/Veyra/worktrees/frame-pacing-20260918`，构建命令：

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/frame-pacing-20260918 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/frame-pacing-20260918 -DisplayVersion 1.4.2beta -Targets veyra_presentation_pacing_tests,veyra_capture_tests
```

构建 exit0，仅更新诊断测试程序及其产品库；GUI 可执行程序没有在本轮重新链接。实卡枚举使用 `veyra_capture_tests.exe --list`，结果见 `devices.txt`。正式测试使用：

```powershell
./scripts/test-frame-pacing.ps1 -Output E:/项目/Veyra/tests/capture-latency-20260918 -Name low -Mode 0 -Sync 0 -Multiplier 4 -Seconds 120 -Scenario capture-nr
./scripts/test-frame-pacing.ps1 -Output E:/项目/Veyra/tests/capture-latency-20260918 -Name even -Mode 1 -Sync 0 -Multiplier 4 -Seconds 120 -Scenario capture-nr
./scripts/test-frame-pacing.ps1 -Output E:/项目/Veyra/tests/capture-latency-20260918 -Name reflex -Mode 2 -Sync 0 -Multiplier 4 -Seconds 120 -Scenario capture-nr
./scripts/test-frame-pacing.ps1 -Output E:/项目/Veyra/tests/capture-latency-20260918 -Name off -Mode -1 -Sync 0 -Multiplier 4 -Seconds 120 -Scenario capture-nr
python scripts/acceptance/analyze-capture-latency.py E:/项目/Veyra/tests/capture-latency-20260918
```

## 实测结果

四轮退出码均为 0，实际测量分别 120.010、120.042、120.020、120.047 秒。P95 表示 95% 样本不超过该值，不是最坏情况。单位均为毫秒。

| 模式 | 原帧回调到 Present 返回 P50 | P95 | 最大 | 提交 FPS | 实际模式 |
| --- | ---: | ---: | ---: | ---: | --- |
| 低排队 | 42.211 | 42.553 | 43.223 | 119.873 | 低排队 |
| 均匀呈现（前端同步） | 42.290 | 44.784 | 48.074 | 119.883 | 均匀呈现 |
| Reflex 请求 | 42.188 | 42.518 | 43.408 | 119.880 | 回退低排队 |
| 完全关闭 | 42.184 | 42.507 | 43.105 | 119.886 | 关闭 |

这台机器、这组配置没有测出低排队或 Reflex 选项相对关闭的显著降延迟收益。均匀呈现的尾部等待更高，提交间隔 P95 也从关闭的 8.750ms 增到 9.670ms，不能宣称它在本场景更平滑。显示器只有 100Hz，表中约 119.88 是 Present 提交率，不是屏幕完整显示率。

### CPU / 调度分段

每格为 P50 / P95。原帧就绪后仍在等同一对 A/B 的生成帧依次呈现，因此原帧就绪早于整批 FG 完成。

| 阶段 | 低排队 | 均匀呈现 | Reflex 请求 | 关闭 |
| --- | ---: | ---: | ---: | ---: |
| 回调校验、锁与 CPU 拷贝 | 1.013 / 1.165 | 0.998 / 1.163 | 1.008 / 1.173 | 0.993 / 1.149 |
| 拷贝完成到 mailbox 读取 | 0.007 / 0.012 | 0.007 / 0.017 | 0.007 / 0.011 | 0.007 / 0.012 |
| 读取到处理图开始 | 0.014 / 0.018 | 0.014 / 0.018 | 0.013 / 0.018 | 0.013 / 0.018 |
| CPU 处理图提交（与 GPU 重叠） | 2.035 / 2.412 | 2.076 / 2.484 | 2.010 / 2.379 | 2.062 / 2.464 |
| 其中 command slot 等待 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 提交结束到原帧 ready 观察 | 7.027 / 7.509 | 7.077 / 7.764 | 7.058 / 7.535 | 7.014 / 7.493 |
| 原帧 ready 到 Present 开始 | 31.825 / 32.319 | 31.924 / 34.261 | 31.816 / 32.282 | 31.798 / 32.269 |
| 原帧呈现函数调用及紧邻 bookkeeping | 0.213 / 0.344 | 0.213 / 0.340 | 0.211 / 0.342 | 0.214 / 0.344 |
| 回调到原帧 ready（累计） | 10.145 / 10.614 | 10.235 / 10.936 | 10.151 / 10.610 | 10.143 / 10.608 |

NV12 压缩解码：不适用。GPU 上传/颜色转换见下一表。CPU 提交中还包含写上传缓冲、驱动调用等，本次没有把它进一步硬拆成未经测量的数值。

### GPU 阶段

| 阶段 | 低排队 | 均匀呈现 | Reflex 请求 | 关闭 |
| --- | ---: | ---: | ---: | ---: |
| NV12 GPU 上传/颜色转换 | 0.190 / 0.198 | 0.192 / 0.200 | 0.191 / 0.198 | 0.188 / 0.197 |
| 光流及其准备 | 1.029 / 1.170 | 1.028 / 1.160 | 1.031 / 1.162 | 1.030 / 1.164 |
| NR | 6.130 / 6.501 | 6.126 / 6.524 | 6.126 / 6.494 | 6.126 / 6.462 |
| 残差合成 | 0.232 / 0.418 | 0.233 / 0.414 | 0.232 / 0.419 | 0.233 / 0.419 |
| DLSS 插值第 1 张 | 2.609 / 2.835 | 2.638 / 2.868 | 2.635 / 2.863 | 2.607 / 2.827 |
| DLSS 插值第 2 张 | 1.485 / 1.708 | 1.505 / 1.739 | 1.508 / 1.734 | 1.483 / 1.705 |
| DLSS 插值第 3 张 | 1.482 / 1.700 | 1.503 / 1.722 | 1.507 / 1.723 | 1.482 / 1.698 |
| DLSS 三张总批次（含上三行） | 5.647 / 6.009 | 5.830 / 6.156 | 5.724 / 6.089 | 5.638 / 5.999 |
| 图 GPU 首尾区间（排除 blit） | 13.717 / 14.169 | 13.822 / 14.308 | 13.792 / 14.252 | 13.708 / 14.124 |
| 最后 blit 到窗口（每次呈现） | 0.027 / 0.029 | 0.028 / 0.029 | 0.027 / 0.029 | 0.027 / 0.029 |

SR 与 Video HDR 关闭，没有对应 GPU 样本。以上是既有阶段 timestamp 覆盖的完整区段，NR/光流阶段包含相关准备步骤，不等于只量供应商单个内核。

### 生成帧、稳定性和计数

| 指标 | 低排队 | 均匀呈现 | Reflex 请求 | 关闭 |
| --- | ---: | ---: | ---: | ---: |
| B 回调到生成帧 Present P50 / P95 | 25.538 / 34.227 | 25.607 / 36.672 | 25.518 / 34.181 | 25.502 / 34.181 |
| A 回调到生成帧 Present P50 / P95 | 58.904 / 67.598 | 58.981 / 70.040 | 58.883 / 67.554 | 58.869 / 67.558 |
| 全部提交间隔 P50 / P95 | 8.364 / 8.774 | 8.310 / 9.670 | 8.367 / 8.762 | 8.372 / 8.750 |
| 原帧延迟首 30s / 尾 30s 的 P50 | 42.250 / 42.216 | 42.327 / 42.223 | 42.208 / 42.177 | 42.196 / 42.166 |
| 原帧呈现事件样本数 | 3597 | 3598 | 3597 | 3598 |
| 生成帧呈现事件样本数 | 10789 | 10793 | 10791 | 10794 |

生成帧包含 A 与 B 的信息，约 25ms 的 B 年龄不能当成整张画面的输入响应延迟。A/B 实测到达间隔中位数约 33.37ms；4X 不会把输入采样从 30Hz 变成真实 120Hz。

四轮正式采样段：NR/FG 始终激活，采集 dropped 增量 0，补帧执行前跳过增量 0，执行后过期增量 0；command slot wait 为 0；未见随运行时间积累的排队。预热期间各有 3 个跳过机会及 provider warmup，正式窗最初约 0.85 秒仍采到最近一次跳过的受限状态，limited 样本约占 0.72%，不能将整次运行描述为从未出现受限标记。正式段没有新增跳过。

少量生成帧缺少 CPU ready 观察值：低排队 18、均匀 7、Reflex 7、关闭 12。相关 ready 分段跳过这些样本；有完整 callback/Present 的总耗时仍保留。没有把缺失值填 0，全部原帧 ready 样本完整。

## 根因范围与建议

主要可见延迟是原帧 ready 后约 32ms 的呈现等待，并不是 GPU 运算花了 42ms。`EngineController.cpp` 使用 `liveInterval + processingAllowance` 为实卡 DLSS 成对调度：本机源间隔约 33.33ms，原帧增强预算约 8.3ms，因此原帧目标约在 B 回调之后 41.6ms，再叠加实际调度和 Present。`FgRecoveryBudget.h` 的预算来自近期 GPU 基础处理成本，`PresentationScheduler.h` 再按子帧 PTS 排布三张插值与 B。

四种可选模式共用这个基础时序。关闭撤销新增同步行为，但保留补帧基础排序；低排队也不能凭名字省掉这段。30fps 插值必须等后帧到达，这是因果约束；但是当前具体的整帧延后量是实现策略，不能直接宣称所有约 32ms 都是物理上不可减少的。要降低它，需要验证整套 A/B/原帧时序，而不是删一个 sleep 导致帧成串、倒序或丢弃。

本场景优先保持关闭或低排队；没有证据支持推荐均匀呈现来降延迟。下一项唯一任务是用同一动态输入配合外部高帧率拍摄，测 HDMI 源画面与采集预览的差值，再评估基础补帧时序的可减少部分。本轮只测量，未更改调度来制造收益。

## 真实性与交付检查

真实运行日志四轮均有 NR `CreateFeature id=18 result=0x1 seh=0`、DLSSG `Create ... result=0x1 seh=0`、DLSSG `Evaluate ... generatedCount=3 ... result=0x1 seh=0`，NVOF Create/execute 成功；四轮没有 `[ERROR]`，正常释放并 clean stop。未在 RTX 30/40 上执行，不把 RTX 5070 结果扩展到这些卡。

补充 20 秒 `signal-check`（同配置、关闭模式）exit0，截图独立于四轮数据：`scripts/acceptance/capture-test-window.py` 第一次因窗口遮挡拒绝截图，之后仅将测试窗口置前重试成功。`signal-check.png` 已目视核对为 PS5 PlayStation Plus 主界面，非黑帧、非设备测试图；RGB 均值约 39.45/38.35/35.15，范围 0..255。没有自动操作用户主机进入游戏，场景以静态主界面为主，动态游戏最坏耗时和插值画质未验收。截图仅留本地，没有上传。

修改文件：`CaptureCardSource.cpp`、`EngineController.cpp`（仅增加详细日志）、`PresentationPacingTests.cpp`（实卡入口与采样）、`scripts/acceptance/analyze-capture-latency.py`、`scripts/acceptance/capture-test-window.py`、本报告和 WORKLOG。分析生成 `comparison.json`、`comparison.csv`、`stages.md` 与各组 `joined-presents.csv`，包含各阶段样本数、均值、P50/P95/P99/最大值。保留约 110MB 原始正式测试日志作为证据，无中间包或解压副本。
