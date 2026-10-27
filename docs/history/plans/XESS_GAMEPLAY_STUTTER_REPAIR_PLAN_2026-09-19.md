# XeSS 游玩转身卡顿与功率波动：调查和修复方案

日期：2026-09-19。状态：用户随后授权实施，已修复下述接入问题并完成本机针对性构建和 GPU 回归；受影响用户的真实转身场景仍待验收。未打包、推送或发布。以下审计段落描述修改前基线，执行结果见文末。

## 结论与证据边界

不能把 GPU 功率上下波动直接判为显卡性能不足。源帧断供、CPU 等待、历史重建、GPU 工作量变化均可造成这种现象，需要同一时间轴区分先后。当前存在足以影响连续性的接入缺陷；尚无本批用户的日志/录屏，不能宣称已锁定共同根因。

审计基线：`codex/cross-monitor-20260919`，HEAD `412a19f`，包含未提交的跨屏、V 原图对照等已有修改。本方案不覆盖这些改动。

## 已确认的实现问题

### 1. XeLL 标记没有覆盖实际增强工作（优先级 P1）

`src/gfx/XessPresenter.cpp:183` 的 `beginFrame()` 调用 Sleep 后立即连续发送 SIMULATION_START/END、RENDERSUBMIT_START；调用入口在 `src/engine/VideoPresenter.cpp:87`。此时 Engine 已完成该源帧的 graph 处理/提交，并可能等待过呈现期限。XeLL 看不到主要处理阶段，所学习的时序不代表完整一帧。

本机 Intel XeSS 3.0.2 文档 `doc/xell_developer_guide_english.md:294` 要求 Sleep 在帧开始、输入采样前执行，且为该 frame ID 首次 XeLL 调用；SIMULATION 与 RENDERSUBMIT 应分别覆盖实际 CPU 准备和命令提交。因此这是接入位置/计时合同问题，实际贡献多少卡顿尚待测量。

当前呈现与处理在同一 owner 上同步执行（`EngineController.cpp` 的 `advanceLive`、`LiveGpuScheduler.h`）。Sleep/Present 阻塞时会推迟后续取帧。XeSS 还共享 direct queue/ring；DLSS 使用独立呈现队列，不能拿 DLSS 顺畅证明这段 XeSS 链路正常。

### 2. 暂时失去光流被扩大为关闭提供方（P1）

`EnhanceGraph.cpp:1886` 使用 `haveFlow && !reset` 标记 motion 有效；`VideoPresenter.cpp:135` 附近将 motion 有效性纳入 enabled，随后 `XessPresenter::tag` 调用 SetEnabled(false)，不再 Tag。恢复时又因 `!xessWasEnabled_` 发送 reset。

官方 FG 指南 `doc/xess_fg_developer_guide_english.md:885` 后明确区分：resetHistory 跳过当前帧插值但保留当前输入；SetEnabled(false) 不保留历史，恢复多需一帧预热。当前路径会放大历史中断。具体缺帧数量须以每帧状态测量，不能固定推算为所有设备相同。

修复必须让“用户关闭”“资源不可用”“当前帧历史不连续”成为独立状态。只有当前 color/depth/MV 资源合法时，才可保持 provider enabled 并用 resetHistory 表达断点；不能提交悬空资源或冒用上一帧运动。初始/断点帧需提供符合合同的中性 guidance。不得以拖动或转身时停补帧作为解决办法。

### 3. 多倍节奏移植缺少上游兜底（P1，条件触发）

`src/gfx/XessPacing.cpp:141` 附近在内部 scheduler 不可用时只增加 bypassed，不约束中间生成帧的节奏。固定上游 `Coldwood1026/OptiScaler` 提交 `70676c5f037c8c26f1ec355b250a72303cd268da` 的 `OptiScaler/proxies/XeFGPacing.h:750` 则检查 scheduler gate，必要时调用 PaceFrame，并避免重复等待提供方已调度的尾帧。

本地移植计划确实只移植 scheduler 核心，省略 wall-clock fallback 和时间戳相关 hooks。这是覆盖缺口，不证明本批用户触发。历史 pan4 与 package-xess4 均报告 bypassed=0、refused=0，该次运行不能归因于缺少兜底。2X 不经过这个多帧补丁，若 2X 同样卡顿应优先检查前两项及源帧断供。

GitHub 只读 `git ls-remote https://github.com/Coldwood1026/OptiScaler.git HEAD` 返回 `68d4c37c0c60eeda64234a106bf9d93289d2eb53`；本轮逐行比较的是上述固定本地版本，不声称已审计远端最新源码。

### 4. 现有平滑度指标遗漏批次边界（P1）

`XessPacing.cpp` 的 inBurstGaps 只计批内调度返回间隔，burstBoundaries 只记次数、不记时长，亦不是完整真实 Present/扫描事件。于是“批内均匀、一批之后停很久”可以显示正常的平均间隔。

历史 `E:/项目/Veyra/logs/xess-view-reset-20260919/package-xess4.engine.log` 后半段确有 backend=XeSS（前半是 DXGI），批内 mean 约 10.37ms；退出 scheduled=544/refused=0/bypassed=0。不能据此认定整段画面平滑或物理显示达到某帧率。

### 5. 生命周期与统计并发缺陷（P2）

`XessPacing::install` 持有非递归 mutex 后，在已 installed 分支调用再次取锁的 snapshot，会死锁。是否有运行路径重复 install 尚须调用生命周期测试，不当作持续转身卡顿的证据。Hook 线程写统计而 snapshot 的读取锁未被写侧遵循；需原子计数或有界快照传递，并核查卸载时在途回调。不能简单在整个 hook 外层加大锁，把等待锁带进逐帧路径。

## 高风险机制，尚未确认为本批根因

1. **转身被当作切镜。** `SceneCadenceAnalyzer.cpp` 使用未运动补偿的 SAD 与 256 档直方图，`EnhanceGraph.cpp:1348` 使用 64x36 点采样。相同场景快速转向不同亮度区域可能触发 histogram replacement（SAD > .08 且 histogram distance > .9）或 hard cut。当前测试仅有直方图稳定的移动纹理，不覆盖该情况。CPU NV12/RGB/YUY2 等输入走此判定；GPU RGB 和硬解纹理分支不走此 CPU 分析，不能把所有窗口采集问题都归因于它。
2. **断供反馈。** 采集 latest mailbox 覆盖记 Drop（`CaptureCardSource.cpp:1123`）；Engine 将 Drop/previewSkip 变为 historyReset（`EngineController.cpp:1029`）。可能形成“owner 等待 → mailbox 覆盖 → reset → 补帧预热 → 输出断档”。保留丢帧真实统计和断点重置，优先解决等待来源，不能粗暴忽略 Drop。
3. **重复排期。** 采集 XeSS 仍使用一源帧间隔的 pairDelay（`EngineController.cpp:1046,1195,1258`）；随后进入 XeLL 和 provider。`PresentationScheduler::deadline` 明确是 host+(PTS-source)+delay。须测明 provider 自带延迟与该 delay 是否重复，不能未经验证直接删掉全部等待或真实 A/B 依赖。
4. **帧时间反馈污染。** 传入 frameRenderTime 的是 presenter 调用间隔，可能夹带前次等待和源端抖动。Intel 文档称该值为帧时间，非 Intel GPU 可用于 pacing sanity-check；未知可填 0。比较有效源 PTS 间隔、当前值、0 三种诊断设置，不能把增强 GPU 耗时误填为该值。
5. **转身视觉错误与时间卡顿混淆。** 色彩图裁剪、motion/depth 全资源及单位映射需要验证。资源分辨率不同本身不构成错误，官方允许 low-res MV 与 letterbox。裁剪后空间域、缩放后的向量单位、上一帧有效区域变化才是审计重点。指南还要求前后帧插值区域尺寸相同，否则可跳过插值。不能在没有画面和尺寸证据时宣称已找到坐标 bug。

已排除的错误方向：`XessGenerationGate::observe` 位于 `if(didPresent && !isCapture)` 内（EngineController.cpp:1309-1325），只作用于文件播放，不能解释采集卡门控停补帧。文件播放的 gate 仍应单独审计其 lateness 是否包含 provider 主动等待。

## 施工顺序

### A. 先建立可关联时间线

使用同一 epoch/sourceFrameId/providerPresentId 关联：采集回调、mailbox 取帧/覆盖、graph 开始结束和 GPU 完成、计划 deadline/实际等待、XeLL Sleep、命令提交、Present 起止、provider 返回。每次 reset 写原因、MV 有效性、SetEnabled 变化、resetHistory、生成数量及批次 ID。

用预分配有界事件环、汇总输出和异常前后窗口，避免逐 API 同步日志改变结果。补全原帧/生成帧及批次边界事件；调度 hook、DXGI 调用、系统显示事件三者分开。所有耗时输出 P50/P95/P99/max 及超期计数，GPU 功耗按 QPC 对齐辅助判因。

### B. 修复状态语义和 XeLL 生命周期

先修重复 install 锁问题并补生命周期回归。然后在合法资源合同下，以 resetHistory 代替短暂 SetEnabled(false)。独立验证 XeSS 2X/3X/4X 首帧、连续转身、真实切镜、Drop、用户关闭/重开。当前 XeSS 提供方上限为 4X，6X 是 DLSS 路径，不能混用验收倍率。

将 XeLL frame ID 从 presenter 自增移到共享 owner 的实际源帧工作起点，经处理批次传到 presenter；Sleep 返回后再选取 latest 输入并设置真实 markers。明确被跳过、失败、取消的帧如何结束或放弃，不能对同一 ID Sleep 两次，不能让已提交批次错误配对。marker 改动与 pairDelay 改动分开 A/B，先确认时序，再决定实时 XeSS 由哪个时钟唯一控制呈现；文件音频主时钟不随意改变。

若等待仍占用 source owner，才评估独立呈现 owner/queue。必须先审计纹理 lease、描述符、帧槽、provider 线程归属、GPU fence、退出/resize 顺序；队列严格有界，不通过无限缓存换平滑度。

### C. 内容判定与多帧 fallback

用真实转身素材证明误切后再改 detector：优先结合已有光流置信度和运动补偿残差，检查直方图候选的空间连续性；无需额外等待 C 帧，不新增正常路径像素回读。避免只提高阈值导致真实切镜跨场景插值。先做 CPU 路径针对性验证，不能将未验证逻辑全局推广。

fallback 按固定上游实现适配，先诊断 gate 状态与 bypass 是否真实出现，再导入 provider 时间基准/尾帧归属保护。保留来源、固定提交、GPL 许可及改动记录于 THIRD_PARTY_NOTICES。只进程内修改、保持原 DLL；不得无条件对每个生成帧追加 Sleep。验证 provider scheduler 与 limiter 两种状态，以及倍率切换和退出。

### D. 验收标准

- 同一输入/窗口/刷新率，XeSS 2X/3X/4X 对比关闭 FG 与 DLSS 4X；1080p60、1440p60、4K30，NR 关闭/开启，采集 NV12/P010 与 GPU 窗口采集分开。
- 每项 120 秒（单测上限 300 秒），覆盖静止、慢转、快速 180 度转身、亮暗方向切换、闪光、真实切镜；保留输入帧或获授权素材，以相同运动段复测。
- 补测中键拖动/缩放、全屏/裁剪、跨屏、暂停恢复、倍率切换。不得出现修拖动后 XeSS 开不了的回归。
- 首先通过 API/GPU debug 与资源生命周期检查；真实切镜仍明确 reset。连续同场景转身不可无原因反复 enable/disable；所有停补帧必须有合法原因和关联事件。
- 对完整输出事件统计相对目标间隔的 >1.5x、>2x、>3x 长间隔数量，逐个归因启动/真实切镜/源丢帧/处理欠速/调度。目标是消除软件额外长间隔，不将源断流算作可凭空生成的内容。
- 以相同素材/设置比较基线与修改后的 P95/P99、最长停顿、reset 次数及源帧年龄；不能仅因平均 FPS 或功率升高判通过。修补一项后重复对应场景再继续下一项。
- 若要声称“到眼睛延迟”，需要摄像/光电等外部测量。本次软件事件只证明进程与系统显示路径，不冒充屏幕扫描延迟；本机 50 系不能替代用户 30/40 系验收。

## 本轮实际检查

读取上述产品实现、Intel 本机 SDK 文档、固定 OptiScaler 源码、现有 pan/package 日志，并只读查询 GitHub HEAD。旧 Desktop/logs/veyra-app.log 确有 XeSS 2X 会话及 API 成功记录，但来自既往反馈、缺乏本次转身场景关联，不能冒充本批复现。没有新的实机/GPU/功耗测试，也没有证明硬件驱动本身无问题。

后续产物统一置于 `E:/项目/Veyra/`；本轮复用 `build/slider-reset-20260919`，日志、测试、临时目录使用 `xess-gameplay-20260919`。源码仓库只新增源码、测试与文档。

## 授权实施结果

- XeLL Sleep 移到取源帧前，simulation/render markers 覆盖准备和 graph 提交；通过有界 identity 映射关联待呈现帧。旧画面重绘抢先呈现时，退休被超越的准备编号，使用新呈现编号，避免倒序 SetPresentId；该回退标记不冒充覆盖先前 graph 工作。
- 合法输入的历史断点提交中性 motion + resetHistory，保留当前帧，不再因暂时无有效光流关闭提供方。真实用户关闭、重复呈现、原图对照仍不生成。
- 复现 retained buffer 2560x1440、client 960x540 切到 800x600 时 119 条 COMMON/UAV 调试错误。仅将 frameRenderTime 改 0 或仅改资源 lifetime 均不能解决；固定全 buffer 插值域、按实际 contain/zoom/pan 变换 motion 后同场景错误降为 0。
- 新 `PresentMotion.hlsl` 输出 current-to-previous、低分辨率 motion 像素单位；前后 view 均参与计算，黑边/越界/reset 输出零。正常链路只有 GPU pass，不增加像素回读。测试专用回读验证恒定向量、2 倍缩放、10% 平移和 reset，结果分别约 (-2,1)、(-4,2)、(62,1)、(0,0)。
- 资源以 UNTIL_NEXT_PRESENT/COMMON 标记，同队列提交，Present 返回前不复用。未知 frameRenderTime 填 SDK 允许的 0，不将含等待的 presenter 间隔反馈为处理帧时间。
- 修重复 install 嵌套 mutex 死锁与统计读写锁不一致；移植固定上游 15 周期中位数 fallback、尾帧 limiter 归属保护；完整 hook present 返回间隔含批次边界。实测 scheduler 均可用，fallbackFrames=0，因此 fallback 尚未得到实际提供方条件触发验证。

本机 RTX 5070、驱动 32.0.16.1656：2X/3X/4X 恢复、2X/4X 平移及尺寸变化回归通过，调试错误 0。最终 4X 恢复测试三次断点下一帧均生成 3 帧，重复呈现不生成，编号超越案例正常退出。主程序 NR+XeSS4X smoke 退出 0，累计生成 588；25 秒进程中媒体先结束，末段为暂停刷新，不算 25 秒连续补帧。详细命令、日志和后续连续测试结果见 WORKLOG。

未完成：受影响用户游戏素材、真实采集 NV12/P010、30/40 系、功耗对齐、物理屏幕延迟，以及方案中完整分辨率/倍率 A/B 矩阵。未调整切镜阈值、未删除 capture pairDelay，不声称所有转身卡顿已根治。
