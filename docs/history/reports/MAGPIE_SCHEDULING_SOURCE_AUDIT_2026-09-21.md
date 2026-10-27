# Magpie 帧生成源码复核

2026-09-21，按用户要求同步上游并检查实际链路。仅源码调查，未改 Veyra 产品代码，未执行新的 Magpie 性能测试。

## 来源与同步

- 仓库：https://github.com/SAOG0721/Magpie ，experimental 分支，GPLv3。
- 本地：`E:/项目/Veyra/downloads/magpie-six-issue-audit-20260920`。
- 已执行 `git fetch origin --prune`、`git ls-remote --symref origin HEAD`。
- 上游 HEAD 与本地均为 `3841698348bfb246623d4acf791984c8b68a577b`，没有发现比此前固定版本更新的源码；工作区干净。
- 下列位置均相对此固定提交的 `src/Magpie.Core/`。源码能力不证明用户安装二进制对应此提交。

## DLSS 实际链路

`Renderer.cpp:493,2794` 创建独立后台线程处理采集和效果，前端负责显示。
`Renderer.cpp:3539` 附近的 `_CompleteBackendFrame` 在效果完成提交后执行 DLSS，逐张发布生成帧，最后发布原帧。

`DLSSFrameGenerator.cpp:850-892` 每次生成提交 D3D12 命令、Signal，安排 D3D11 Wait，
随后还在 CPU 上 `WaitForFence(outputReady)`。读取 SDK 的有效性标志后才发布这一张。
同一生成纹理/allocator 的复用受等待保护。这是逐张串行等待，不是五张补帧免费并发。
`DLSSFrameGenerator.cpp:523` 将最大生成数 clamp 到3，因此此版本 DLSS 最高4X，不提供6X证明。

`Renderer.cpp:3680-3715` 在同步发布路径等待共享纹理槽可用。前端未消费会反压后台，
不是无限缓存。`Renderer.cpp:1438` 附近遇到未来呈现期限返回 Retry，让前端继续处理消息，
不在 FIFO 作业里直接睡到期限。

`FramePresentationTiming.h` 的 CaptureFrameCadence 扣除单独累计的下游槽位等待并平滑
估计输入周期，再除以倍率；FramePresentationClock 长时间停顿后重新锚定，避免无限追赶。
这控制的是采集增强器的输出节奏。Veyra 文件播放另有真实 PTS 和音频时钟，不能直接
用处理速度替代媒体时钟，否则可能引入慢放、音画偏离或更多延迟。

## XeSS 实际链路

`XeSSFGPresenter.cpp:477-487` 创建独立 D3D12 device/direct queue；效果侧是 D3D11。
`:857-862` 通过共享 fence 的 Signal/Flush/queue Wait 交接输入；`:1043-1044`
再通过 Signal/D3D11 Wait 约束消费后的复用。独立队列仍有真实数据依赖。

`XeSSFGTiming.h:14` 的 Submit：优先相邻已接受采集帧的真实时间戳；无可用时间戳时，
用提交间隔减去已记录的额外等待，使用9样本窗口中位数（启动前两样本取较短者）。
序列/资源代次/帧号和时间异常触发重置。已接受采集帧间隔不等于游戏产生帧的间隔。
`:948-951` 将估计同时传给 SDK frameRenderTime 和补丁 sourcePeriodNs。

`XeSSFGPacing.h:362` 的 TsDetour **修改** SDK 时间戳，不只是观察：保留原生锚点，
按估计输入周期/倍率确定同组子帧步长，修正原生最小值钳制带来的步长。
过期槽位不逐个顺延到现在，每组重新取锚点，仍允许本组有限追赶突发。
`:250-269` 的 ScheduleFrame 仍调用原生 scheduler；不能假定 scheduler 只有睡眠，
Veyra 本轮已证实该处可等待未完成 fence。

Veyra 当前候选仅传真实源间隔，默认关闭；时间戳 hook 是只读诊断，未照搬上述改写。
还有一个需谨慎核对的细节：Magpie extraWaitNs 累计整个 scheduler 调用的墙钟时间，
可能包含资源等待，不能一律解释为纯节奏等待后扣除。其有有效采集时间戳时优先用时间戳。

## 对 Veyra 的可用价值与边界

1. **优先价值：明确增强与呈现的所有权交接。** Magpie 两线程和有界槽位降低 CPU
   相互阻塞的可能；Veyra XeSS 共享图形队列已观察到增强排队对 provider 等待的影响。
   下一候选必须先证明 provider 消费完毕后的退休 fence，以及 resize/seek/退出如何
   取消和释放；仅开一个新队列会制造纹理提前复用。已有失败拆队列方案不能原样重试。
2. **XeSS 时间戳改写可研究，但不是现成成功结论。** 先判断剩余长间隔中资源等待与
   截止时间等待的占比，再决定是否有必要移植。不得绕过 fence 或人为追加固定延迟。
3. **前端非阻塞期限处理可作合同参照。** Veyra 已有调度器和 DLSS 独立呈现队列，
   应检查具体阻塞点，不能把同类架构换名当成新优化。
4. **不照搬逐张 CPU fence 等待。** 这可能限制提交并降低并行度；Magpie 的复用合同
   需要它，Veyra 应保留自己的多槽/fence 保护，不能删除安全等待也不能凭空添加等待。
5. **没有证据证明 Magpie 同负载更稳。** 原生4K与真超分、采集帧率与媒体帧率、
   DLSS4X与6X、软件提交与屏幕显示都需区分。源码未提供重负载满倍率保证。

重试前查 [实验索引](FG_EXPERIMENT_INDEX_2026-09-21.md)。本轮无代码移植，故不新增
第三方衍生源码；将来移植须记录此固定提交、GPLv3、具体文件和改动至 THIRD_PARTY_NOTICES。

## 继续复核：CPU 阻塞与资源归还合同

2026-09-21 继续只读核对当前 Veyra，未运行新性能测试，未改变产品行为。

- `include/veyra/engine/LiveGpuScheduler.h` 的任务直接在 graph owner 线程执行。
  `EngineController.cpp:1275,1349` 的任务回调直接调用 presenter；Future deadline
  可以返回 Pending，但进入 provider Present 后的阻塞不会让出此工作线程。
  因此 XeSS 的 SDK 等待会阻止同一线程提交下一帧增强。这是源码事实，尚不是
  已证明可消除的 GPU 空闲：已有 GPU 命令可能仍在执行。
- `VideoPresenter.cpp:28` 仅为 DLSS 创建独立呈现队列。独立 GPU queue 不等于
  独立 CPU 提交线程；不能把 Veyra 已有的队列重复包装成 Magpie 式双线程优化。
- `VideoPresenter.cpp` 在 `ring.submitAndSignal` 后登记
  `graph.presentationSubmitted`，随后才调用 `sink_.present`。这枚 fence 覆盖
  应用 blit，不覆盖之后 XeSS 在 Present 中追加的命令。当前 XeSS 未走该独立
  队列分支，同线程提交和同 GPU 队列排序是现有保护的一部分；未发现据此即可
  判定当前存在提前复用的证据。
- `XessPresenter.cpp:262` 使用 UNTIL_NEXT_PRESENT 输入有效期。
  Magpie `XeSSFGPresenter.cpp:1043` 在 Present 返回后 Signal，再让 D3D11
  等待这个值。若移植此架构，须查清所用 SDK 对输入消费/内部队列交接的合同，
  不能把前置 blit fence 当 provider 退休 fence，也不能只凭 CPU 返回判断 GPU 完成。
- `EnhanceGraph.h:186` 同时检查输出 lease 和 consumer fence；
  `EnhanceGraph.cpp:1330` 在复用前安排 GPU Wait。但深度、映射运动纹理、
  presenter 的 backbuffer、描述符和 XeLL cycle 也要有明确归属，单独保护彩色
  输出不足以允许两个 CPU 线程并发访问现有对象。

### 下一候选的准入条件

仅在时间线显示 Present 阻塞期间存在可提前提交的独立工作时，考虑有界的
producer/presenter 交接；不加深队列、不改变倍率、不增加固定等待。
先建立不可变帧描述与完整输入资源租约，指定 producer-ready 和 provider-consumed
两端 fence，串行化 Tag/Present/XeLL，定义 seek/resize/退出取消与退休顺序。
记录当前排队深度下可重叠的实际工作，再做一个有界对照；若 GPU 已持续占满关键
执行资源，则拆线程可能只有风险，没有吞吐收益。

另一路是 Magpie 的 XeSS 时间戳策略，但必须先分离资源等待与期限等待。不能靠
压短期限把五张图挤在一起、提高 FPS 数字来冒充流畅改善。DLSS 不采用这条 XeSS
补丁；其预算拒绝/历史预热仍按 P2 单独归因。
