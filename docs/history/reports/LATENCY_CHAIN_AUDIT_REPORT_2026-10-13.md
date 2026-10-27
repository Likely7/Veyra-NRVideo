# 4K 采集 NR＋XeSS 可见延迟排查（2026-10-13）

已确认软件存在两项值得修正、验证的呈现行为：显示同步的“自动”实际启用 VSync；XeSS 专用分支跳过普通播放的浅队列设置。它们提供了具体的排查方向，但本轮没有完成同一 60Hz 环境下的光学 A/B，不能宣布全部约 112ms 的根因已分解，也没有实施产品修复。

用户没有手动开启垂直同步，这与上述结论不冲突。**关效果的基线也使用 sync=1，因此不能把延迟差全部归咎于 VSync 开关本身。重点是 XeSS 接管呈现后的排队行为不同。** NR 本身的实测 GPU 耗时约 5.5ms，无法单独解释从约 33ms 到约 112ms 的变化。

## 输入、版本与测量边界

- 指定会话：`01a13328-397f-7621-920e-303cb1c737b0`，标题“分析 Veyra 延迟测试录像”。本轮读取其报告、核对两张分析截图，没有再次解码其全部 15,638 张录像帧。
- 原报告：`C:/Users/123/Desktop/延迟/analysis/Veyra_三段录像逐帧分析报告.md`。三段原片在 `E:/素材/261013/`，拍摄时间为 10 月 12 日晚。
- 找到对应运行日志：`E:/项目/Veyra/tests/claude-cadence-exact-20261012/Veyra-2.0.7-test-cadence-NVIDIA/logs/veyra-qml.log`，SHA256 `becd72cc864cd69ef367399eaadf8b6972c9fd1160cda840a11fa2d18977529c`。录像报告 SHA256 `a92286994742100694c422da89a43af80cdbe341d8c94684e3fc56b3f84ebf93`。输入回执与筛选日志见 `E:/项目/Veyra/archives/latency-chain-audit-20261013/`。
- 昨晚日志核对：MCS T911、3840×2160 NV12 60FPS、NR 内部 1920×1080、NVOF、XeSS 2X；显示统计为 60.001Hz。用户补充组合为 XeSS 2X、1080p NR、60→30、没有手动开 VSync。内容模式与日志的差异见下文。
- 源码审查基于 main `757191948bba0d847825d84ae9a1602bcaf8713a`，诊断分支 `codex/latency-chain-audit-20261013`。更新检查的独立分支、旧工作树、真实用户配置不变。

原报告的统计是同一显示环境中 USB 预览相对 HDMI 参考的**可见延迟差**，不是采集卡电路延迟。两路画面大小、扫描位置不同，手机曝光和刷新相位也有影响；增强录像只有 35/50 个有效事件，且阈值改变后结果约 100ms，不能将 112.5ms 当作无误差仪器测量。

| 录像 / 当地时间 | 原报告可见延迟中位数 | 原报告 P95 | 本轮匹配的 UTC 日志窗口 |
|---|---:|---:|---|
| 4K 无效果 / 22:14:29 | 33.3ms | 37.5ms | 14:14:29–14:15:06 |
| 4K NR＋XeSS / 22:16:00 | 112.5ms | 129.2ms | 14:16:00–14:16:23 |
| 2K 无效果 / 22:17:25 | 16.7ms | 20.8ms | 14:17:25–14:17:55 |

## 确定的软件行为

### 1. “自动”与 VSync 相同，没有手动选择 VSync 也会同步

`include/veyra/engine/PresentationSettings.h:32` 当前为：

```cpp
return s.display != DisplaySync::Tearing;
```

只有显式选择“允许撕裂”才返回 false。`DisplaySync` 的 0/1/2 分别是允许撕裂、垂直同步、自动。此策略由 `ef2069f61c1a2e1ac4de39e2af76bf34a979adae` 于 2026-10-07 00:46:44 +08 引入，提交说明为 `fix: preserve cross-vendor export and synchronize automatic presentation`；之前仅显式 Vsync 返回 true。它对应此前防撕裂修复，不能不做回归就直接撤回。

与增强录像对应的日志：

```text
2026-10-12T14:15:30.500Z [settings] Applied revision=16 ... fgBackend=XeSS flowBackend=NVIDIA_NVOF multiplier=2
2026-10-12T14:15:30.500Z [pacing] provider=XeSS applicationWait=0 vsync=true tearing=false
2026-10-12T14:15:30.500Z [pacing] revision=1 requested=false/0/2 effective=false/0/2
2026-10-12T14:16:02.397Z [present-contract] backend=XeSS sync=1 flags=0x0 hr=0x0
```

`requested=false` 指低延迟队列开关，最后的 `2` 指显示同步“自动”。关闭低延迟队列不等于关闭 VSync。基线录像的原生 DXGI Present 同样为 `sync=1`，所以这里确定的是软件实际启用了同步，尚不是两段延迟差的独立因果证明。

### 2. XeSS 路径跳过了原生路径的队列上限控制

`src/engine/VideoPresenter.cpp:469` 的 XeSS 分支关闭应用等待器并提前返回。原生分支从 `:480` 开始，即使用户关闭低延迟队列，只要 VSync 有效仍会强制队列上限 1。

`src/gfx/PresentSink.cpp:536` 的 XeSS 分支仅设置 `vsync/tearing` 并返回，没调用后面的 `SetMaximumFrameLatency`。不安装额外等待器符合 XeSS 自行调度的设计，但这不等于已经核对了代理交换链的排队上限。

本轮独立隐藏窗口探针直接加载随包 Intel 组件。首两次探针采用 3 缓冲/FlipDiscard/WaitableObject；复核发现产品 `PresentSink.cpp:234` 明确不给 XeSS 设置 WaitableObject，因此第三次按产品真实 flags（AllowTearing、无 WaitableObject）补测。以下 XeSS 结果取第三次探针，原生 1 的对照取原生 waitable 交换链；没有修改 Veyra、DLL、驱动或用户设置。

| 探针状态 | GetMaximumFrameLatency | 返回值 |
|---|---:|---|
| 原生 DXGI waitable 新建 | 1 | S_OK |
| XeSS 代理按产品 flags 初始化 | 3 | S_OK |
| XeSS 启用补帧 | 3 | S_OK |
| XeSS 设置上限 1 后读回 | 1 | Set/Get 均 S_OK |
| XeSS 关闭再开启补帧 | 1 | 两次切换均成功 |

证据：`E:/项目/Veyra/logs/latency-chain-audit-20261013/config-product-flags-probe.{log,json}`；源码 `E:/项目/Veyra/tests/latency-chain-audit-20261013/LatencyConfigProductFlagsProbe.cpp`，构建 EXE SHA256 `8b55a207aa55fc629a26de544c4b059b5ff8a94febbf3d5bd9686fd5e5576b55`。前两次探针及回执也保留。无 waitable 的原生 DXGI 对 Get/Set 返回 `0x887a0001`，而 XeSS 代理接受并读回，这是代理接口的行为，不能将其等同于已验证底层 DXGI 队列改变。

这证明配置差异和 setter 可接受，不证明实时播放中始终排满 3 帧，不证明设成 1 的画面稳定性或延迟收益。探针没有执行 Present。下一步应在实际 SDK 工作负载下验证该设置，而不是直接宣称可降低两帧延迟。

### 3. 对应录像日志实际是自动识别内容，没有启动固定 60→30

同一 revision=16 明确记录 `requested60To30=false active=false`、`capture60To30=false`、`contentPolicy=1`。`ContentRate` 定义在 `include/veyra/engine/EnhancementSettings.h:115`，1 是 Auto，5 才是 Capture60To30。

录像窗口中 `content-dedup` 每秒 observed/kept 约 30，repeats/held/blind 全为 0；采集回调约 60、真实处理约 30、mailbox 覆盖约 30、提供方报告提交约 60。**这是源帧取读变慢后丢弃旧输入，不能称为固定隔帧或自动去重正常删掉一半重复帧。** 用户记得使用 60→30，但匹配这段录像的实际日志并非该状态；此差异必须保留，不能按回忆替日志改写配置。

## 软件计时与链路检查

下表每个数都是“所选窗口内，每条日志已报告的 P95 的中位数”。它们不是全部帧合并后的 P95，不可相加得到光学延迟，不可相减得出精确的等待分项。

| 日志窗口 | 报告条数 | 回调→Present 返回 | CPU 观察 GPU 完成 | NR GPU | 光流 GPU | 应用调度等待 |
|---|---:|---:|---:|---:|---:|---:|
| 4K 无效果 | 37 | 5.458ms | 2.256ms | 0 | 0 | 0 |
| 4K NR＋XeSS | 23 | 52.482ms | 33.660ms | 5.460ms | 1.101ms | 0 |
| 2K 无效果 | 30 | 2.687ms | 1.474ms | 0 | 0 | 0 |

增强时 Present CPU 调用约 1.18ms，不能因此判定 SDK 没有等待：XeSS 在非 Intel 显卡上另有内部队列与后台呈现线程。GPUReady 是 CPU 观察到 fence 完成的年龄，包含 GPU 排队、轮询时机等，不能当作 NR 执行时间。`gpuFgBatchP95Ms=0` 也不代表 XeSS 推理零耗时，该计时点不覆盖提供方内部生成。

汇总脚本：`scripts/acceptance/latency-chain-evidence.py`。输出 `E:/项目/Veyra/reports/latency-chain-audit-20261013/historical-windows.json` 同时记录输入 SHA、计数差分和统计口径。差分得到 4K 基线处理约 59.54FPS；增强约 29.99FPS、丢旧输入约 29.99FPS；2K 基线约 59.59FPS。

额外检查：

- `EngineController.cpp:1591` 在从源读最新帧之前调用 XeLL `beginSourceInput`，顺序本身符合低延迟输入原则。
- `EngineController.cpp:1898` 对 XeSS 这类 Present 提供方设 `legacyPairDelay=0`，本轮日志调度等待也为 0。没有发现又叠了一次应用层固定 A/B 延迟，不能用删除 DLSS/VFG 的 pair phase 来修 XeSS。
- `VideoPresenter.cpp:39` 只为非 XeSS 创建独立呈现队列；XeSS 使用图的 direct queue。这里可能产生队列耦合，但本轮没有完成实际队列执行追踪，不直接判定为缺陷。
- 驱动时间戳年龄的基线约 77ms，已经与基线光学 33ms 不相容。该字段时钟/帧起点未校准，不能把它当成额外采集卡延迟再加到结果里。driverBuffers=10 也不能直接换算 10 帧延迟。

## 本轮正常负载实卡验证

未修改的已发布 r2 EXE：SHA256 `6fd35109d3c668f51fd7ce1609c8176e164227e2510473ef5a6276a34b1d713d`。独立副本 `E:/项目/Veyra/tests/latency-chain-audit-20261013/app/`，只增加测试 QML Loader，私有配置/日志/临时目录均在本轮 E 盘目录。启动前检查其他 Veyra 进程，GPU 优先级普通，原版 NR 单层 1080p、4K60 NV12、无音频和其他效果；每场有效阶段 24 秒，稳态汇总取开启后 8 秒以后的日志。

**今天显示器为 2560×1440 320.002Hz，昨晚为 PBP 60Hz。没有擅自更改显示器设置，所以这不是原场景光学复现。**

| 场景 | 实际同步 | 真帧处理 / 秒 | 覆盖旧输入 / 秒 | 回调→Present 返回的日志 P95 中位数 |
|---|---|---:|---:|---:|
| NR 单独、源时间戳 | sync=1 | 60.00 | 0 | 9.545ms |
| NR＋XeSS 2X、源时间戳 | sync=1 | 60.00 | 0 | 10.808ms |
| NR＋XeSS 2X、固定 60→30 | sync=1 | 30.00 | 0 | 10.288ms |
| NR＋XeSS 2X、固定 60→30、允许撕裂 | sync=0 / flags=0x200 | 30.00 | 0 | 10.897ms |

XeSS 提供方报告分别约 120/60FPS，不是物理屏幕实显帧率。四场夹具通过，未发生设备丢失/引擎失败；回执和原始日志各在 `E:/项目/Veyra/logs/latency-chain-audit-20261013/<场景时间>/`，汇总 `reports/latency-chain-audit-20261013/normal-load.json`。

结果支持“相同算法在不同显示条件下并非必然驻留 52ms”，不支持“关闭 VSync 已降低多少端到端延迟”。320Hz 下两个固定半帧场景近似也说明不能拿这组测试冒充 60Hz 的同步因果验证。

## 未完成的测量与失败记录

- 首场夹具错误使用设备名 `MCS4K-T911`，匹配不到实际设备；改成唯一子串 `T911` 后通过，没有换格式自动回退。
- 尝试开启既有 XeSS/subframe trace 并从 `QmlPlayerBridge::diagnosticsReport()` 导出，但该接口只有 UI 简报，不含 Logger 内存追踪。生成的 trace.txt 约 592 字节，时间线分析器报没有 XeSS trace 事件。没有取得逐帧 XeLL Sleep 时长、SDK GPU 执行或完整生产者/消费者时间线，不声称该项通过。
- 配置探针首个构建启动因 cl.exe 环境查找 WinError2 失败，改用明确 MSVC 编译器路径后成功；最终三个独立探针均编译/执行成功。前两次误用了比产品多出的 WaitableObject flag，复核后第三次按产品实际 flags 通过并保留对照，不能将前两次当作完全相同配置；完整源与日志保留。
- 未做实际 60Hz 同场 setter=1 / 默认值 A/B、ETW/PresentMon 的队列占用追踪、修复版光学复测、其他 GPU、HDR 或长期稳定性测试。

## 后续修复和验收方向

1. 让用户能明确控制实际同步状态，避免“未选择垂直同步”被理解为“自动也不会同步”。自动策略的任何修改必须回归窗口/全屏防撕裂，不能无条件退回早期 Present(0,0)。
2. 首先隔离验证 XeSS 的 `SetMaximumFrameLatency(1)`，记录 setter/Get 结果与实际队列时序；保留 XeLL 和 SDK 自身生成帧调度，不叠第二个外部帧限制器/等待器。setter 可读回只是启动条件，最终按帧率、生成帧均匀性和物理延迟验收。
3. 补上现有 bounded trace 的真正导出，或采用 ETW，把 frame ID、XeLL Sleep、取源、图提交/完成、提供方 Present 与显示事件连起来。只有看到同一帧的因果链，才决定是否进一步拆共享队列、调整生产者在途工作。
4. 在同一 PBP 60Hz、相同视频/网页源和取景下比较：基线、NR 单独、XeSS 单独、组合；每项固定半帧与自动内容、显式允许撕裂与同步、默认队列与上限 1。测试需真实刷新、NR/光流耗时、SDK 输出和光学统计同时保留，不能拿提交 FPS 当画面延迟验收。

66ms 不是必然结果。30FPS 真帧间隔为 33.3ms，插值要等下一张真帧；此外还有处理、提供方排队/调度与显示等待，部分环节可重叠。把 33ms 基线直接加一个 33ms 只能作为粗略预算，不能作为总延迟上限。但本轮也没有证据证明 100 多毫秒都是算法不可避免的成本，应先验证已发现的呈现配置和队列差异。

## 官方语义核对

- [Intel XeSS-FG 开发指南](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md)：代理交换链自行生成与调度；非 Intel GPU 使用后台线程和内部高优先级队列；必须集成 XeLL，不支持叠加其他延迟框架。
- [Intel XeLL 开发指南](https://github.com/intel/xess/blob/main/doc/xell_developer_guide_english.md)：Sleep 在输入前；VSync 对齐优化明确限定 Intel。不能把 NVIDIA 日志的真帧 30FPS直接解释为已经证明 XeLL 在执行 Intel 专属优化。
- [Microsoft SetMaximumFrameLatency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency)：该值是交换链允许排队的上限，原生接口只适用于 waitable-object 交换链。XeSS 非 waitable 代理也接受这个调用是本轮探针发现的区别；代理读回不证明底层已生效，上限也不等于实时占用。

本轮只交付排查证据、脚本和报告。没有修改产品实现、UI 统计/绘制频率、运行库/模型、全局驱动或真实用户配置；没有合并 main、推送、发布新包或关机。旧工作树与输入保护、源码 bundle 及最后状态见本轮外部 final 回执。
