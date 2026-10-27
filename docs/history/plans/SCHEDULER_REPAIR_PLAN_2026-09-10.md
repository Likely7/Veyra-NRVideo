# 实时调度与无效 GPU 工作修复计划

> 当前任务已续接至 [后端切换、真实帧率、链路延迟与音画同步方案](CONTINUATION_REPAIR_PLAN_2026-09-10.md)。本文未完成的S1计时、FRUC恢复及S3/S4仍在新方案范围内；最新第一步先修XeSS控件可见性，不能再将下方历史“下一项”作为唯一任务队列。

日期：2026-09-10。状态：S1 核心账本与 S2 的 DLSS 路径已实现并短测；完整 S3/S4 尚未完成。范围：采集卡实时预览和播放器实时呈现；不改变导出完整性检查，不接入 Depth Anything，也不调整 SR -> NR -> FG 的算法顺序。

2026-09-11更新：FRUC轻量CUDA reseed及按实际调用轮转输入通过跳过恢复像素2X/3X/4X，产品重新启用FRUC提交前admission；下方“FRUC已撤下”是旧失败记录。设置去重/音频隔离/排空后合并与生成帧A/B实测关系已加入。完整单GPU所有者仍未实施；当前证据以继续修复实施记录最新段为准。

## 2026-09-10 实施决定

用户已明确废弃旧 Loop、控制哈希停工及早期 Phase 排队，当前直接按修复任务执行。旧文件保留为历史资料，不更新 CONTROL_HASHES 来制造通过。详情和实际命令见 `SCHEDULER_REPAIR_IMPLEMENTATION_2026-09-10.md`。

- 已完成：独立 session/revision/epoch 账本、取消回调、有效生成/Present 速率、GPU 样本 epoch 过滤、真实采集 callback sequence、DLSS 整对 FG 提交前判断及跳过后 reseed、2X/3X/4X 资源和 PTS 回归。
- S2 首版按整对判断，以最后一个生成时间戳判断该对是否还有价值；不跳过 MFG 中间 index。预测使用同窗口的图开始至 GPU-ready 实测 P95 加 Present P95，扣掉当前已耗时；不相加可能重叠的 GPU 子阶段。8 个有效样本前只有已过期判断，500ms 无有效样本清除旧预测，不会永久禁止恢复尝试。
- FRUC 试接后发生重复重建、有效生成不增长，已撤下产品入口的 deadline skip。保留原来的 FRUC 路径；不得拿诊断 API 测试成功掩盖该回归。
- 续接定位：reset 像素错误常为旧真实帧/上一对插值输出；诊断性 CUDA 全上下文 CPU 同步通过，但多个纯 GPU completion 候选仍失败，已全部撤下。下一步需定位 CUDA/D3D11 资源映射和完成可见性，不能把失败候选接入产品。
- 原生 4K NR + VSR 最高 + DLSS4X 的同源过载回放：150 源帧观察点，旧策略 450 次 FG Evaluate、444 个过期输出、0 个生成呈现；新策略跳过 450 次、0 个过期输出、0 个生成呈现，处理约 27 -> 33 fps。仅证明该回放的无效计算减少，不是实卡延迟、GPU 占用百分比或 NR 推理加速证明。
- S1 的全部 reset 耗时分解、详细逐帧固定环追踪尚未补齐；默认仍使用已有按需逐帧日志，加每秒活动窗口和窗口关闭汇总。命令槽 high-water 为观察值，非逐提交的精确峰值。
- S3 的单线程 GPU 所有者状态机与真实源帧进一步 coalesce 未实施；当前保留 mutex 串行保护的图/呈现双线程及两批上限。既有 Drop 策略保留已完成真实帧，只抑制旧生成帧，不冒充所有旧 epoch 真实帧都被取消。
- S4 不合并未知依赖的 command list，不替换未通过像素验证的 FRUC reset。下一项是 FRUC reset 的同步、时间戳及恢复像素问题；实卡仍由用户验收。

## 结论和目标

当前实现已经限制了帧积压，但不具备完整的 deadline-aware 前沿调度：一部分补帧可能在 GPU 完成后才因过期被放弃呈现。过载时，这会表现为 GPU 利用率高、实际提交帧率低、画面周期性卡顿。

这不是无限队列失控。采集输入是容量 1 的 latest-frame mailbox，实时呈现 worker 的 active + queued batch 总数最多为 2，D3D12 command slot ring 固定为 6。修复目标不是扩大这些上限，而是在提交可选的昂贵工作前判断它是否仍有显示价值，并让每个丢弃点可观测。

Depth Anything 不在本次修复范围。当前产品没有 TensorRT、ONNX 或 DirectML 深度推理；NR 绑定固定 `nrZeroDepth_`，FG 绑定静态 `depthTex_`。因此 FP16/INT8 深度模型不可能是当前高占用或卡顿的来源。

## 已知事实

| 项目 | 当前行为 | 依据 | 影响 |
| --- | --- | --- | --- |
| 采集输入 | 一个待读取帧；新回调覆盖旧帧并计为 dropped | `CaptureCardSource.cpp` | 不会无限堆积，但过载会丢旧源帧 |
| 呈现批次 | active + queue 总数最多 2 | `PresentationWorker.h` | 防止租约/纹理无限占用 |
| GPU 命令 | 固定 6 个 command slot；slot 复用前等待其 fence | `CommandSlotRing.cpp` | 有硬性背压，不能无限向 GPU 发送命令 |
| 生成帧 | 已提交的 generated frame 到 deadline 后会跳过呈现 | `EngineController.cpp` | 存在已计算但无显示价值的 GPU 工作 |
| 原生成本 | 原生 4K NR 约 22.9 ms，最高档 RTX Video SR 约 6.6 ms | `PHYSICAL_CAPTURE_DIAGNOSIS_2026-09-09.md` | 二者已经超过 60 fps 的 16.67 ms 源帧预算；调度优化不能把它变成实时档 |

## 不可破坏的约束

1. 采集保持 mailbox 容量 1；不得为了提高表面 FPS 增加秒级缓冲。
2. 文件播放不得丢源帧、篡改 PTS 或破坏音频主时钟。采集可以淘汰过时源帧，但必须记录来源序列和 reset。
3. 只允许单一 graph/GPU 所有者提交和修改资源状态。不能让 UI、采集回调和呈现线程并发调用 `EnhanceGraph`。
4. source switch、pause/resume、seek、resize、capture drop、scene cut、device lost 和 settings revision 切换必须原子取消旧 epoch；任何被取消 batch 的 lease 只能在消费者 fence 完成后重用。
5. 正常播放、采集和视频导出不得增加 GPU->CPU 像素回读、每 pass CPU fence wait 或无界日志/帧队列。
6. 原生 4K NR 仍保留为非默认的高成本选项。实时档必须继续明确标识内部 NR 尺寸，不能因调度修复改写其含义。

## 实施顺序

### S1：先建立可信的帧流账本

修改 `FrameMetrics`、`EngineController`、诊断日志和专业模式 telemetry。每个计数和时间样本必须绑定 `sessionId`、实际 `settingsRevision`、`epoch`、source sequence、batch id 与 fence value；异步完成的旧 revision 样本不能混进当前窗口。

新增以下单调计数，按 revision/epoch 分窗显示和落盘：

- `captureReceived`、`mailboxOverwritten`、`sourceAccepted`、`sourceSkippedBeforeGraph`；
- `realSubmitted`、`fgCandidate`、`fgSkippedBeforeEval`、`fgEvaluated`、`fgReadyValid`；
- `realPresented`、`generatedPresented`、`generatedExpiredAfterEval`、`cancelledBeforePresent`；
- `commandSlotsInFlight`、`commandSlotHighWater`、`slotReuseWaitCount/Ms`；
- `presentationBatchHighWater`、`gpuReadyWaitMs`、`deadlineWaitMs`、`captureArrivalToPresentReturnMs`；
- reset 总数及每个原因的次数、排空耗时、FRUC worker 重建耗时。

日志改为每秒一条聚合记录，逐帧细节只在显式诊断开关下写入固定容量环形缓冲。错误、HRESULT、SEH、NGX/NVOF 返回值、配置事务和 reset 仍立即记录。专业模式显示“有效生成 fps”和“Present 提交 fps”，不把两者叫真实扫描率或 HDMI 到屏幕延迟。

验收：快速切换 NR 尺寸、SR 后端、FG 倍率、暂停和恢复后，旧样本不会污染新 revision；故意令生成 deadline 过期时，`fgEvaluated`、`generatedExpiredAfterEval` 与 `generatedPresented` 的关系可从单个日志窗口复原。

### S2：在提交前淘汰没有显示价值的可选工作

仅对实时采集启用，文件播放保持完整帧语义。

1. 收到 B 帧后，按该帧的 arrival host timestamp、源 duration、当前 epoch 和已应用配置计算真实/生成帧 deadline。不得把采集卡内部缓冲当未来帧。
2. 使用同 revision 的近期 GPU P95 加上已测 CPU submit/present P95 形成保守预测。窗口无样本或刚切换配置时，标记为 warm-up，不做虚假的乐观估计。
3. 若生成帧在开始 FG Evaluate 前已经过期，或预测完成时间已越过其 deadline，则不提交该帧的 DLSSG/FRUC Evaluate；记录 `fgSkippedBeforeEval`，仍允许当前真实 B 帧按最新可用时机呈现。
4. 如果 B 帧本身已经过时，回到最新 mailbox 样本，不对已经失去预览价值的旧源帧执行 SR/NR/FG。该跳过必须设置 Drop/Discontinuity 并触发同一 epoch reset，不能把旧 motion/depth/history 接给新帧。
5. 已经提交到 GPU 的命令不能撤回。其生成输出若过期继续按当前 lease/fence 回收，但必须计入 `generatedExpiredAfterEval`，不能被合并进“有效生成”。

第一版只在过载时减掉可选 FG 工作，不自动关闭用户手动选择的 NR/SR，也不改变实际使用的质量档。若 NR/SR 本身连续超过输入预算，UI 直接显示“当前组合无法实时处理”，由用户切换到实时档或较低质量。

验收：在故意超过预算的 1080p60 输入上，`generatedExpiredAfterEval` 显著下降，`fgSkippedBeforeEval` 对应增加，纹理租约、epoch、PTS 和 reset 契约保持通过。真实帧不得被“补帧优化”错误地重复或跨 epoch 呈现。

### S3：把实时路径整理为显式状态机

将当前“处理整批 -> 等 GPU 完成 -> 依 PTS 呈现”的隐式循环整理为单一 GPU 所有者下的可推进状态机：

```text
capture callback -> mailbox(1) -> admission/coalesce -> graph submit
                                          |                 |
                                          |             fence poll
                                          v                 v
                                     reset boundary <- ready batch -> deadline/present
```

- `admission` 只消费最新源帧，并以现实可用的 slot、lease 和 deadline 决定是否提交。
- `graph submit` 允许在安全的两个 batch 资源窗口内继续推进，不能等待某个将来 generated PTS 时阻塞 source 读取。
- `fence poll` 保持非阻塞；轮询等待由已有 `DeadlineWait` 统一处理，不能重新引入 busy spin 或每个 batch 新建定时器。
- `present` 优先处理将到 deadline 的有效帧；旧 epoch、已取消或已过期生成帧只释放 lease，不补交欠帧。
- 资源上限维持当前两套 real parity slot、最多六个 generated slot和 6 个 command slot。任何新队列必须有容量、high-water、丢弃理由和消费者 fence 生命周期。

不在此步骤把 `PresentationWorker` 的上限简单从 2 提高。那会提高表面吞吐，同时把卡顿改成更长的采集延迟。

验收：模拟 15/30/60 fps 输入、2X/3X/4X、暂停、source switch 和 capture drop，检查 source 序列递增、PTS 合法、没有跨 epoch 显示、没有 `frame-pool still leased`，队列峰值永远不超过设计值。

### S4：减少已证实的提交路径浪费

完成 S1 数据后再决定，不预先假定收益。

- 比较每帧 command-list 数、`slotReuseWaitMs`、graph submit CPU 时间和 GPU queue 区间。若多个紧邻的小提交造成 slot wait，合并同一依赖链中可合并的 copy/compute/FG 记录；不跨 NGX/NVOF 实际要求强行合并。
- 比较默认日志与详细追踪日志。若批量日志降低提交 P95 或 source overwrite，保留批量模式；若收益不可复现，不作为性能结论。
- 检查 settings/resize/rebuild 中的 `drainQueue()` 只发生在真正资源生命周期边界，绝不挪入稳态每帧路径。
- FRUC worker 的轻量 reset 仍是独立实验。必须先用切镜、丢帧、PTS 回退后的连续像素/PTS 对照证明与 fresh worker 一致，才允许替换重建；不通过就保持正确但昂贵的 fresh worker。

验收：每项改动单独做同源 A/B，并记录命令、输入 hash、配置、GPU timestamps、CPU waits、丢帧和输出契约。GPU 阶段本身未缩短时，不得把调度收益写成“NR 加速”。

## 测试矩阵

每次完整调用不超过 300 秒；绝大多数单项 10--60 秒。先离线/回放，再由用户进行实卡验收。

| 场景 | 目标证据 |
| --- | --- |
| 1080p60 回放，NR realtime，VSR low，FG off/2X/4X | 基线吞吐、各阶段时间、slot wait、实际提交和生成有效率 |
| 同源 1080p60，原生 4K NR + VSR high | 明确过载时的 `skip-before-eval` 与 `expired-after-eval`；不要求 60fps |
| 15/30 fps 内容封装在 60 fps 采集传输 | cadence 识别、输入速率与有效处理速率分离、无重复历史污染 |
| seek/pause/resize/source switch/capture drop | epoch/reset、lease、PTS 和取消安全 |
| DLSSG 2X/3X/4X 与 FRUC 2X/3X/4X | 多子帧的有效性、deadline、重复标志和资源上限分别验证 |
| 用户真实采集卡 | callback FPS、mailbox overwrite、有效处理/生成/提交 FPS、画面年龄；真实扫描率和端到端光子延迟仍需外部测量 |

通过标准不是“GPU 占用降低到某个百分比”。必须同时满足：无界队列为零、没有跨 epoch/错误 PTS/早期纹理覆盖、过载时不再大量执行注定过期的 FG、同配置有效呈现节奏没有回退、实时档与原生 4K 档标签仍然诚实。

## 风险与回退

- 预测低估成本会让生成帧仍迟到；预测高估会少生成。第一版保守跳过并把原因公开，之后只能以同配置日志校准。
- 过早丢弃真实 B 帧会损害采集连贯性，因此 S2 先只跳过 FG；真实帧 coalesce 需要单独 gate 和用户实卡观察。
- 线程边界/lease 改动可能引入 resize、切换或 device-lost 崩溃。每一阶段保持旧路径可切换，且先通过现有 reset/resize 短测。
- 调度修复不能抵消原生 4K NR 的物理 GPU 开销。若关键路径持续超过预算，正确结果是提示质量选择，不是伪造更低延迟。

## 交付顺序

1. S1 指标与固定容量诊断缓冲。
2. S2 仅对采集 FG 的 pre-evaluate deadline skip。
3. S3 实时状态机和生命周期回归。
4. S4 经同源 A/B 证实的提交、日志或 FRUC 改动。
5. 用户实卡验收后，再决定默认策略是否需要调整。

每一步完成后更新 `docs/WORKLOG.md`，记录实际 build/test 命令、日志路径、失败证据和未解决风险。任何 GPU/实卡未执行项必须明确标为未执行。
