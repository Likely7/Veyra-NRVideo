# 固定 6X 帧生成均匀呈现：深度排查与攻坚计划

日期：2026-09-20  
最新执行约束：用户要求跳过 NR 优化，保持原画质、所选倍率，不新增等待或自动降档。
下文 NR policy、额外 lookahead、fallback 仅保留历史研究背景，不是当前施工授权。
`7a3dfae` 已移除自动倍率选择；`55e0bdd` 已撤回不安全的共享状态回读。
组内 MFG 的 BackbufferFrameID 保持同一真实输入身份，不能将下文“单调”解读为每个子帧加一。
继续实验结果见 `FG_NON_NR_EXPERIMENTS_2026-09-20.md`。

分支：`codex/fg-cadence-audit-20260920`  
目标媒体：`E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4`  
当前目标：4K60 H.264 文件、NR 开启、DLSS 固定 6X、4K 输出。该媒体已经是
3840×2160，因此 4K 目标下 SR 应旁路；不能把 UI 中的 4K 选项当成实际执行了超分。

## 1. 这轮要解决什么

“请求 6X”与“软件统计提交了很多帧”不是同一个验收条件。固定 6X 必须同时满足：

1. 请求倍率和实际调度倍率都保持 6，不能用自动 4X 结果代替固定 6X；
2. 生成帧按相邻源帧之间的真实 PTS 有序呈现，不能出现周期性的 15–17ms 空洞；
3. 已经算好的子帧不因等待整批最后一张而过期；未算好的子帧不能被假装成有效帧；
4. 音频仍以 1 倍速连续播放，源 PTS、reset、seek、resize 和资源所有权不被破坏；
5. 队列、显存和 in-flight 作业保持有界，不能通过无限堆积把卡顿推迟到几秒以后。

6X 的初步软件验收阈值（用于排查阶段，不先改成产品宣传口径）：稳定窗口内提交率至少为
360×0.95=342/s；不得出现超过一个源帧周期 16.667ms 的提交间隔；P95/P99 间隔和
整组拒绝、子帧过期、warmup 数量必须单独记录。真正的显示器扫描和人眼延迟另测，不能用
进程内提交率冒充 scan-out。

## 2. 已知事实和当前边界

当前分支已经完成了部分恢复和门禁修复，但固定 6X 仍未通过：

| 配置 | 保留窗口结果 | 结论 |
| --- | ---: | --- |
| NR1080 + 固定 6X | 约 265.445/s，P95 15.428ms，最大 17.169ms，19 个间隔超过 16.667ms | 仍失败；存在整组拒绝和 warmup |
| NR1080 + 请求 6X 自动降到 4X | 约 240.007/s，P95 4.514ms | 只是回退证据，不是 6X 修复 |
| NR 关闭 + 固定 6X | 约 360.004/s，P95 3.198ms | 证明 6X 呈现器在有余量时能均匀提交 |
| NR720 + 固定 6X | 约 353.874/s；有限追赶实验接近 360/s | 只说明降低工作量有效，不能替代原画质验收 |

NR、光流、FG 的当前 GPU 观测约为 6.8ms、1.2ms、9.8–10ms，合计已接近或超过
60Hz 源帧的 16.667ms 预算。这是“当前串行实现的服务时间过长”的证据，不是“显卡永远
不可能做到”的结论。之前的 QPC 音频时钟外推没有实质收益；简单拆成独立 FG 队列、把
多个 Evaluate 粗暴合成一个 command list、只增大 command ring，也没有通过，不能原样重做。

## 3. 第一性原理：先把四种损失分开

每个源帧对记为 `A→B`，每对最多有 NR/光流、5 次 DLSS MFG Evaluate、资源状态转换、
fence 完成和 6 次呈现机会。对每个子帧记录四个时间点：

`GPU 开始/结束`、`CPU 观察 ready`、`媒体 deadline`、`Present begin/end`。

由此按证据分类：

| 现象 | 判定方向 | 不能直接下的结论 |
| --- | --- | --- |
| GPU 阶段总跨度稳定超过 16.667ms，CPU 无 slot/fence 等待 | 计算服务时间超预算 | 不能因此放弃查资源重复工作和队列重叠 |
| GPU 在 deadline 前完成，但 admission=false 或连续 warmup | 门禁/恢复模型误拒绝 | 不能只把预测阈值放宽，必须核对实际 ready |
| 子帧已 ready，Present 间隔仍有空洞 | 呈现队列或队头阻塞 | 不能把软件提交率当屏幕刷新率 |
| `CommandSlotRing` 有等待或资源 fence 等待占主要时间 | 资源生命周期/allocator 反压 | 不能靠增加无限缓冲解决 |
| FG1–FG5 线性占用且每次 Evaluate 有固定成本 | provider/调用契约成本 | 不能修改 DLL 或伪造运行库身份 |

## 4. 阶段 A：先补齐可归因的测量

不先改调度策略。扩展已有 bounded trace 和 GPU timestamp，按 `sourceFrameId、batchId、
subframe、settingsRevision、epoch、fence` 关联：

- 录入每个 FG1–FG5 的 GPU begin/end、所属 command list、提交 fence、完成 fence；
- 录入 NR、Flow、Residual、Color 和 Present blit 的 GPU 区间，以及 command list 之间的
  空白区间；
- 记录每个子帧的 `readyObserved、deadline、expired、suppressed、invalid、presented`，
  明确“GPU 尚未完成”和“CPU 观察晚了”两种口径；
- 记录 admission 使用的 base/FG/present/queued 预测值和实际最终结果，标记是
  `first-deadline`、`last-deadline` 还是 warmup；
- 记录 `LiveGpuScheduler` 队首阻塞时间、第二个 job 是否已有 ready 子帧、等待原因以及
  `CommandSlotRing` 的 CPU 等待次数/时长；
- 分开 `Present` 调用返回、交换链同步/tearing 模式和显示器实际刷新事件。没有 PresentMon、
  高速相机或显示器时间戳时，只报告进程内边界。

新增的日志必须保持默认低开销；详细逐帧 trace 只由测试环境变量打开，不能写盘阻塞实时路径。

## 5. 阶段 B：先修调度造成的“算好了却没呈现”

### B1. 排查并修正队头阻塞

目前 `LiveGpuScheduler` 以两个整批 job FIFO 执行；前一个 batch 的第一个子帧处于
`Pending` 时，后一个 batch 即使已有可用子帧也不能检查。改造方向是“按 PTS 有序的子帧
就绪队列”，但保留严格的时间顺序：

1. 前一个子帧未过 deadline 时可以等待；
2. 前一个子帧已过期或明确 invalid 时立即记录原因并推进；
3. 只有所有更早的子帧都呈现或被明确丢弃后，后面的 ready 子帧才可提交；
4. 不等待整批最后一个 fence，不改变 lease/consumer fence 所有权；
5. 作业数量仍有界，默认最多两个源帧对，不能借此建立隐藏深队列。

对照实验必须同时跑旧 FIFO、子帧队列和“只改 wake-up 不改顺序”三个版本，避免把偶然的
定时器抖动误认为算法收益。

### B2. 重新核对 deadline 和 catch-up

当前有限追赶把文件 MFG 的最短间隔从 90% 周期放宽到 80%，720p 对照已显示过期子帧
明显减少，但原 NR1080+6X 尚未证明。继续验证：

- `cadence.submitted()` 只在真实 Present 提交后更新；
- catch-up 不得把媒体 deadline 向前移动，也不得制造一串 sub-ms burst；
- Even/LowQueue/捕获/XeSS/FSR 的默认间隔合同互不污染；
- 如果 ready 在 deadline 前却被 pacing 丢弃，必须能从 trace 直接看出原因。

## 6. 阶段 C：修 admission/recovery，而不是盲目放门

### C1. 建立“误拒绝”证据

把每次 `admitFile` 的预测与最终事实配对：

- 拒绝后若 base/FG 实际在 deadline 前完成，说明预测过于保守，应修正 queued/progress
  计算或样本窗口；
- 拒绝后确实超过 deadline，说明门禁正确，应该优化工作或使用明确的回退，不把过期帧硬送；
- `presentP95` 必须和 GPU cost 分开，不能把 DXGI 阻塞重复加进 GPU 预测；
- first-deadline 和 last-deadline 都要通过，不能只看最后一张生成帧。

### C2. 减少恢复期间的时间线断裂

确认 admission-only skip 是否真的必须让下一对重新 reset。若 NGX 合同要求 reset，保留一次
有成本的 seed/warmup 并把它纳入预算；若不要求，不能因为一次门禁拒绝就让整段历史失效。
所有改动都要通过有效性、PTS、reset、seek、resize、stop 和设备丢失回归，不能用“平均 FPS
变高”掩盖历史污染。

### C3. 只对文件播放评估有限 lookahead

文件没有采集卡实时输入，可以在音频主时钟允许的范围内提前处理最多两个源帧对，吸收短时
GPU 抖动。该方案只缓冲短时抖动，不能解决持续服务时间大于 16.667ms 的根本问题；缓冲
上限、额外软件延迟和音频对齐必须进入报告。采集、PS5 和导出不能直接套用。

## 7. 阶段 D：在有测量的前提下降低真实服务时间

按收益和风险顺序做单因素实验，每个实验都能独立回退：

1. **状态转换和状态查询**：把同一对的 `videoFrame/depth/motion` 只在 FG 批次前后转换
   一次，去掉每个 MFG index 重复的 COMMON 往返；保留 UAV barrier 和 provider 所需状态。
   这是资源合同优化，不是简单删除 barrier。
2. **状态输出回读**：验证 `outputDisableInterpolation` 是否必须每个 subframe 都复制到
   readback；若 provider 合同允许，改成一次批次状态收集，保持逐帧 invalid/disabled 诊断。
3. **NR 前后固定 pass**：用 GPU timestamp 找出 downsample、encode、NR、decode、residual
   中真正重复的 pass；只有在输入/输出和 reset 合同完全相同的情况下合并，不能为了省一次
   dispatch 改变画质或历史。
4. **MFG command list 形态**：比较 5 个独立 list、分组 list 和单 list 的 GPU 执行/空洞，
   不预设“一次 list 一定更快”。之前单 list 约 72fps，必须先解释原因再决定。
5. **队列重叠可行性**：先用无 NGX 的小型 copy/compute 标定 RTX5070 的 direct/compute
   overlap；确认 NGX 是否允许目标队列后，再为 FG 建立独立 fence、resource ownership、
   queue wait 和 drain。禁止共享现有单调 fence，禁止复用仍被另一队列读取的 A/B/输出。
   任何“独立队列更快”的结论必须同时有 GPU timestamp 和正确的 fence 生命周期证据。

只有以上仍不足时，才评估算法侧的工作量选择（例如 NR policy 或 Flow 尺寸）；这些只能
作为明确标注的性能档，不能改名成“固定 6X 已修好”。

## 8. 阶段 E：provider 与 30/40 系兼容专项

- 保持 `multiFrameCount=5、multiFrameIndex=1..5、frameId` 单调，逐次记录 NGX 返回值和
  SEH；不修改 NVIDIA DLL，不扩大运行库身份；
- 在 RTX5070 上先验证 6X 调用契约，再用同一 trace 对比 RTX40/30 用户反馈，区分解锁路径
  失败、provider 成本和 Veyra 调度；
- 2X/4X/6X 使用同一媒体、同一 NR、同一输出尺寸，比较每个 FG index 的成本是否线性增长；
- XeSS 只走提供方自己的 pacing，不把 DLSS 的 admission 或 cadence 规则套过去；FSR 同理。

## 9. 回归矩阵和验收顺序

每次 GPU 实验顺序执行，单次不超过 300 秒，输出只写 `E:/项目/Veyra/{tests,logs,tmp}/`
本任务目录：

1. CPU：`UiContractTests、LiveGpuSchedulerTests、FgAdmissionTests、PresentationWorkerTests`；
2. 内容：固定 6X/4X/2X，NR1080 主档；NR900/720 只作诊断对照；NR 关闭作能力上限对照；
3. 生命周期：open、pause/resume、seek、resize、设置切换、stop、device-loss recovery；
4. 稳态：至少 30 秒，另做 120 秒长测，统计最终保留窗口和全程累计；
5. 非 DLSS：XeSS 2X/4X、捕获路径、导出路径，确认调度改动没有串线；
6. 质量：生成帧非恒定复制、PTS 单调、相邻输出差异存在、无 D3D12 错误；
7. 显示：若有可用高刷显示器，再用 PresentMon/外部设备核对 scan-out；否则明确写“未测”。

## 10. 每个实验的放行/回退规则

- 先保存当前可回退提交；单因素实验独立提交，失败立即回退，不在一个提交中混入多个猜测；
- 任何实验若出现 device removed、资源越界、历史帧污染、音频停顿、设置切换崩溃或导出
  回归，立即撤销，即使 FPS 提高；
- 不通过“自动降档”“关闭 NR”“改低分辨率”来关闭主问题。它们只能作为诊断和用户可见
  fallback，并保留 requested/effective multiplier 证据；
- 直到原画质固定 6X 的 cadence 验收完成，不打包、不发布、不关机。

最终报告必须包含：每个瓶颈的证据、保留和回退的提交、固定 6X 的间隔/丢弃/ready 数据、
实际修改文件、测试命令和日志路径，以及未测量的显示器物理延迟边界。若所有合法优化后仍
无法在该硬件和原画质下达到 6X，必须把“当前服务时间超过预算”的证据和最稳定的 fallback
单独写清楚，不能把它包装成 6X 已解决。
