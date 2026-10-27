# XeSS 多帧节奏移植计划（A-2，未开工）

上游：`Coldwood1026/OptiScaler`（GPL-3.0，固定提交 `70676c5f`，
`OptiScaler/proxies/XeFGPacing.h`，853 行）。本地克隆在
`third_party_local/community/OptiScaler`（gitignore，不入 Git）。
提供方：`runtime_local/intel/experimental/libxess_fg.dll` 1.3.1.78（与 A-1 解锁同一份审计身份）。

## 问题

只有 2X 时生成帧每帧恰好一张，间距没问题。>2X 时提供方把一次 burst 里的生成帧**背靠背**
交给交换链，最后才由限流块压住最后一帧 → 观感是"一串挤在一起然后空一截"，
帧计数器翻倍但运动不均匀。"最后一帧"还带两个相同时间戳（4X 实测 `0, i, 2i, 2i`）。

## 上游做法（逐项核实过）

| 项 | RVA | 说明 |
| --- | --- | --- |
| Present thunk | `0x25C0` | `jmp 0x21F730` + 11 字节 int3 填充 → 16 字节槽位，可整槽重定向，无需代码洞 |
| 循环内 present 调用点返回地址 | `0x2202ED` | 唯一需要被节奏化的循环调用 |
| 最后一帧 present 返回地址 | `0x220467` | 提供方自己调度，不能重复打时间戳 |
| 调度 thunk / 函数 | `0x3100` → `0x21EE30` | 提供方自己的帧调度器；签名 `bool(ctx, burst, gate, timing, index)` |
| ring 快照 thunk / 函数 | `0x4DA0` → `0x224CF0` | 填 16 字节 `{样本数, 中位时长}`；`timing` 参数就是它 |
| burst 相对 present arg5 | `-0x38` | `count = *(u64*)(burst+8)` = 本次 burst 的生成帧数 |
| gate 字节 | `burst+0xC0` | 调度器 arg3 = `gate & 1` |
| 调度器开关 | `ctx+0x340`(限流) / `ctx+0x341`(使能) | 两者与限流互斥：限流接管时调度器是空转，必须先问这两个字节 |

上游 `Detour` 只在返回地址等于上表两个 RVA 时才介入，其它 present 原样转发。
如果调度器不可用（限流接管），退回墙钟节流；上游后来还加了时间戳 hook 做更细的对齐。

## 我们的移植范围（本轮决定）

**只移植核心调度调用**（对应上游文档里"ScheduleFrame is the call"那一层）：
1. `ThunkHook`（已存在，A-2 前置提交 `50e4c9c`，含 8 项单测）接管 `0x25C0`，校验 16 字节形状后整体重定向；
2. 替换函数按返回地址过滤，只对 `0x2202ED` 的循环 present 调 `ScheduleFrame(ctx, burst, index)`；
3. `ScheduleFrame` 复刻上游参数重建：`gate = burst[0xC0]&1`、`timing = ringSnapshot(ctx+0x168, &block)`、
   先检查 `ctx[0x340]==0 && ctx[0x341]!=0`，不满足就不调用（避免"静默空转被当成已生效"）；
4. 不移植时间戳/截止时间 hook（上游 0x3430/0x7A30 那一层）与墙钟回退——
   先看核心调度是否已经解决 burst 挤压，再决定要不要加；
5. 只在 XeSS 会话且请求倍率 > 2X 时安装，退出时回滚；失败即回滚并保留 2X。

## 验收方式（本机可做）

- 安装后跑 `veyra.exe --fg-xess --fg-multiplier 4 --smoke-seconds 15 <视频>`；
- 采集 present 间隔（XeSS 呈现回调已有计数与 presentId），看生成帧间隔是否从"成串+空档"
  变成接近 `真实帧周期 / 倍数`；
- 反例检查：返回地址不是两个调用点时必须完全转发（对照 2X 行为不变）；
- 崩溃/挂起即视为移植失败并回滚，不允许用"能跑起来"通过。

## 风险

这是全项目最深的一处内部改写：present thunk 被重定向后，任何参数重建错误都会直接
打到交换链提交路径。风险控制只能是：严格返回地址过滤、调度器开关预检、可回滚、
以及只在高倍率 XeSS 会话内启用。**未完成前，>2X 的生成帧间距继续标注"未验证"。**
