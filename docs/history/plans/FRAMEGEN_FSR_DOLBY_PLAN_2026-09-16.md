# Veyra 帧生成 / 超分 / 杜比 实施计划（2026-09-16）

本文件是完整重写版，取代 `docs/XESSMFG_DLSS6X_DOLBY_PLAN_2026-09-15.md`（同日删除旧文件）。

> **实施结果（2026-09-16 晚更新；已合并到 main，merge `24e7de7`；未推送、未发布）**
>
> | 工作流 | 结果 | 证据 |
> | --- | --- | --- |
> | A XeSS MFG（2X/3X/4X） | **已完成**（`4124a9d`） | 4X 实跑 1677 生成/563 真实，退出时 5/5 回滚 |
> | A-2 XeSS 节奏 hook | **已移植并实测**（`ef016ba`）：present thunk + 提供方调度器接管，仅 >2X 安装、退出回滚；上游时间戳/墙钟层未移植 | 4K/30fps 4X：同 burst 间距 mean 8.303ms（目标 8.33）、refused=0，339 真实/1005 生成，exit 0；状态文档 §二 |
> | B DLSS 6X | **已完成**（`35dd632`） | 6X 实跑 2215 生成/445 真实 |
> | C-2 40 系 Ada 解锁 | **已实现**（`9179449` + `aa168ab`）：架构比较改写 + PTX 中点修正 + 8 槽重定向 + count/index validator + 三段 SHA-256 闸 | 本机结构/防篡改/回滚全过；**3X/4X 真出帧行为待 40 系实机**（`final-40x6x` 包已出） |
> | D 30 系原生 DLSS-G | **已实现**（`4154733` + `3dd5add`）：69 fatbin→sm_86、200 指针槽 + 44 lea + 2 gate 全量发布/回滚、私有 CUDA 预检 | probe 全周期 applied/readBack/restored=1；**30 系实机行为未验证**（`final-30x86` 系列包已出） |
> | E AMD FSR 补帧 | **已完成并实测 2X**（`27656a6`…`0d3c073`）：真实运动、节奏、4K 几何、设置重建、遥测有据；4.0.1 ML 需 AMD 实机 | `docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md` |
> | F FSR 超分 | **已接入产品并实测（3.1.x）**：内容正确、非 NVIDIA 形状可跑；相对画质略软已如实记录；4.x ML 需 AMD 实机 | `docs/FSR_UPSCALING_PLAN_2026-09-16.md` |
> | G-1 杜比位流探测 | **已完成**（本机采集卡不提供位流；SPDIF 端点实测支持 AC-3/DTS） | 状态文档 §一.3 |
> | G-2 杜比解码 + 直通 | **两条路都已实现**：无 PCM 时解码 AC-3/E-AC-3(DD+)/DTS 回 5.1 PCM（`56bb0f6`）+ 位流直通给功放/回音壁（`275c3f2`） | 解码：本地真实 AC-3/E-AC-3 六声道验证；直通：SPDIF 独占载波写穿 245760 字节。**真实位流采集卡端到端未验证** |
>
> 逐项验收清单与最终构建/测试结果见 `docs/FRAMEGEN_FSR_DOLBY_STATUS_2026-09-16.md`（本表与其 §二同步；验收操作顺序见其 §三）。

## 0. 已拍板决策（不再重复讨论）

| # | 决策 | 说明 |
| --- | --- | --- |
| 1 | **有成熟开源实现就直接搬过来改造，不重复造轮子** | 已写入 `AGENTS.md` 首条用户决定；义务是逐项标注来源/固定提交/许可证/改动 |
| 2 | **XeSS MFG 走解锁路径** | 移植 OptiScaler 的 `XeFGUnlock`/`XeFGPacing`（经 Magpie fork 适配版），2X/3X/4X；非 Intel 卡必须解锁 + 节奏修正 |
| 3 | **DLSS 帧生成按显卡能力动态给倍率** | 50 系原生到 6X；40 系官方 2X、移植解锁到 3X/4X/6X；30 系 MFG 不可能（内核缺失），只能拿原生 2X 或 FSR 补帧 |
| 4 | **40 系解锁不得影响 50 系** | 用户条件授权："不会就可以随便弄"；见 §3.C 六条硬性保证 |
| 5 | **AMD 补帧必做，优先用新版** | 先接 `AMD FSR Frame Generation 4.0.1`（ML，SDK 2.3.0），不支持的显卡回退本地 SDK 1.1.4 的 3.1.x |
| 6 | **FSR 超分按显卡分档** | AMD 卡给 FSR 4.1（ML）；N 卡给 FSR 2 / FSR 3.1；N 卡跑 FSR4 目前无可用方案，列入观察名单 |
| 7 | **杜比取效果最好的一档** | 位流直通（DD+/TrueHD → 功放）为主，FFmpeg 解码成 5.1/7.1 PCM 为兜底 |
| 8 | **对 NVIDIA / Intel 运行库只允许进程内修改** | 不改磁盘、不重签名、不伪装身份；失败即禁用该能力，不静默降级 |

## 1. 最终能力矩阵（按显卡）

标注含义：`官方` = 厂商原生支持；`移植` = 直接搬社区开源实现；`回退` = 前一条不可用时的降级；`—` = 不适用。所有"移植"能力在界面上必须标为实验。

| GPU | DLSS 帧生成 | DLSS 超分 | XeSS 超分 / 补帧 | FSR 超分 | FSR 补帧 | 驱动级补帧 |
| --- | --- | --- | --- | --- | --- | --- |
| NVIDIA 50 系（Blackwell） | **官方 2X/3X/4X/6X** | 官方 | 官方 2X；**移植** 3X/4X | 2 / 3.1 | 3.1（4.0 视支持） | NVIDIA App AI 插帧（支持列表为准） |
| NVIDIA 40 系（Ada） | 官方 2X；**移植解锁** 3X/4X/6X | 官方 | 官方 2X；**移植** 3X/4X | 2 / 3.1 | 3.1（4.0 视支持） | 同上 |
| NVIDIA 30 系（Ampere） | **无 MFG**；`移植` 原生 2X（sm86）或改用 FSR 补帧 | 官方 | 官方 2X；**移植** 3X/4X | 2 / 3.1 | 3.1 | 同上（支持列表为准） |
| NVIDIA 20 系（Turing） | 无原生；可用 FSR 补帧替代（dlssg-to-fsr3 路线） | 官方 | 官方 2X | 2 / 3.1 | 3.1 | 同上 |
| AMD RDNA4 | — | — | XeSS 超分：DP4a 回退（待验证）；XeSS 补帧：待验证 | **官方 4.1（ML）** + 2/3.1 | **官方 4.0（ML）** + 3.1 | AFMF（驱动层，非我们实现） |
| AMD RDNA3 | — | — | 同上（待验证） | **官方 4.1**（SDK 2.3.0 起）+ 2/3.1 | 4.0（视支持）+ 3.1 | AFMF |
| AMD RDNA2/1 | — | — | XeSS 超分：DP4a 回退（待验证） | 2 / 3.1 | 3.1 | AFMF（按 AMD 支持列表） |
| Intel Arc | — | — | 官方（含厂商 MFG） | 2 / 3.1 | 3.1（视支持） | — |

## 2. 移植来源与许可证清单

| 能力 | 上游 | 许可 | 我们怎么用 |
| --- | --- | --- | --- |
| XeSS MFG 解锁 + 节奏 | `Coldwood1026/OptiScaler`（固定提交 `70676c5f037c8c26f1ec355b250a72303cd268da`）的 `XeFGUnlock.h` / `XeFGPacing.h` | GPL-3.0 | 直接移植补丁与节奏逻辑 |
| XeSS MFG 工程化封装 | `SAOG0721/Magpie`（`experimental`）的 `XeSSFGCompatibility.h` / `XeSSFGPatchTransaction.h` / `XeSSFGPacing.h` / `XeSSFGTiming.h` / `XeSSFGPresenter.cpp` | GPL-3.0 | 直接采用其结构与生命周期设计，替换模块身份为我们随包的 DLL |
| 40 系 DLSS MFG 解锁 | `ImDreamt/MFGAdaUnlock-RenoDx` | MIT | 直接移植两处架构比较补丁 + 内核中点修正 + 强制软件 flip metering；弃用其 ReShade/Detours 宿主 |
| 40 系倍率/能力策略参考 | `dashdogy/RTX40MFG-Unlock` | MIT | 参考其倍率与能力控制、Dynamic 思路 |
| 30 系原生 2X | `sdli1995/dlssg_for_sm86`（管理器参考 `BUNNY-19C/DLSSG-30s-manager`） | MIT | 直接移植代理与配置思路（若立项） |
| FSR 帧生成 3.1.x | 本地 `third_party_local/amd/FidelityFX-SDK` 1.1.4（`ffx_frameinterpolation`） | AMD FidelityFX 许可 | 直接接入 |
| FSR 帧生成 4.0.x（ML）/ 超分 4.1 | AMD FSR SDK **2.3.0**（`AMD FSR Frame Generation 4.0.1`、`AMD FSR Upscaling 4.1.1`） | AMD FSR SDK 许可 | 需下载并登记新 SDK；发布带 DLL 属范围变更 |
| FSR4 on NVIDIA 观察项 | `int3rrobang/fsr4-int8-reverse-engineering` | MIT | **仅观察**，成熟后可移植（详见 §3.F） |
| AMD 光流搬运参考 | `SAOG0721/Magpie` 的 `AmdOpticalFlowProvider.cpp` / `HalfResOpticalFlow.cpp` | GPL-3.0 | 参考其"估计光流喂 FG"的做法（我们已有 AMD 光流后端） |

Veyra 自身为 GPLv3，与以上 GPL-3.0 / MIT 来源兼容。所有移植必须写入 `THIRD_PARTY_NOTICES.md`，并随源码/对应源码包提供。

## 3. 工作流

### A. XeSS MFG（解锁路径，2X/3X/4X）

- 目标：非 Intel 卡（含 NVIDIA）开放 2X/3X/4X；Intel 走 SDK 原生；默认 2X；5X–8X 不在首批。
- 前置事实：我们随包的 `libxess_fg.dll` = 1.3.1.78 / 22,957,432 字节 / SHA-256 `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27`，与 Magpie 锁定同一份；`dumpbin /exports` 确认已导出 `xefgSwapChainSetNumInterpolatedFrames`。
- 我们当前的三处缺陷：`XessPresenter.cpp:82` 写死 `maxInterpolatedFrames=1`；从未调用 setter；能力查询用扩展 API 且传 1，导致日志 `maxInterpolations=1` 是请求值回读。
- 接入：移植上游四个文件 → 模块身份校验（路径 + 哈希 + PE 标识 + 原字节）→ 事务安装（首次 XeFG 上下文创建前）→ 会话退出恢复；请求超过实际上限时明确报错，不静默降级。
- 验证：SDK 合成测试（45 次提交分别得 2/3/4 倍完整帧数）；失败注入（错哈希/错偏移/部分写入）；真实窗口 2→3→4→2 与 DLSS NR 组合；RTSS 等 overlay 共存记录（上游有 `FAST_FAIL_GUARD_ICALL_CHECK_FAILURE` 记录，列为已知风险）；显示层需 ETW/PresentMon 或拍摄抽样。

### B. DLSS 6X（50 系原生）

- 前置事实：随包 `nvngx_dlssg.dll` 310.7.0.0；本机 RTX 5070 报 `MultiFrameCountMax=5`（=6X）；RTX 4060 报 1（=2X）。
- 改动：`DlssFgBackend` 上限 3→`min(max,5)`；`FrameBatch` 4→6、`interpolate()` 上限 4→6；生成池 3/parity→5/parity（`generatedLeases_`/`genFrame_` 6→10）；`VideoPresenter` SRV 堆 8→12 + slot 映射；`validate()` 1..6；UI 加 6X；预设 v10→v11 迁移；调度/准入/截止时间与导出路径按 5 张生成帧重算。
- **能力驱动 UI（必做）**：上限 = `min(上报值,5)+1`；40 系/更早只给 关闭/2X，3X/4X/6X 置灰并写明原因；上报 0 或查询失败只给 关闭/2X；用户选超上限 → 明确拒绝并保留原值，**不再出现"选完被整档关掉 FG"**（当前行为）。
- 验证：插值表 1..6；限制注入（上限=1 与 =0）；50 系实机 6X 的画质/延迟/显存/长时；40 系补一次 2X 上限回归。

### C. DLSS MFG 解锁（40 系 Ada）

- 机制（上游公开说明）：`nvngx_dlssg.dll` 里 `DLSSGInstanceManager::PopulateParameters` 用 NVAPI 架构 id 与 `0x1b0`(Blackwell) 比较决定"上报 5 还是 1"，同一常量第二处比较驱动真正生成；**两处都要改**，否则选项出现但黑屏。NGX 加载时校验 snippet 签名，**只能改内存**。只解锁帧数会得到重复中点帧（内核里编译死 `0.5`），必须做内核 PTX 中点修正。Ada 无硬件 flip metering，必须强制软件回退。
- 接入：直接移植 `MFGAdaUnlock-RenoDx`（MIT）的三件套与偏移定位方法；模块身份换成我们随包的 310.7.0.0；不引入 ReShade/Detours。
- **不影响 50 系的六条硬性保证**（用户条件授权的兑现）：
  1. 按架构分流：只有 Ada 且 `MultiFrameCountMax` 低于请求时才进补丁路径；Blackwell 走原生查询，**不触碰内存**；
  2. 参数不跨架构：Ada 才需要"强制软件 flip metering"，50 系有硬件 flip metering，**绝不套用**；两套补丁集合分开定义；
  3. 只改本进程镜像：不影响驱动、系统 DRS、其他进程与其他 Veyra 实例；退出即消失，磁盘哈希不变；
  4. 身份校验前置：路径 + SHA-256 + PE 标识 + 目标字节全匹配才装；不匹配 → 不装、回落 2X；
  5. 可回退：会话结束/切后端/切回 2X/退出时恢复并回读校验；恢复失败 → 停止新 FG 会话并提示重启；
  6. 验收必须含 50 系对照：同构建在 50 系上确认补丁路径未进入、原生 3X/4X/6X 不变、会话前后内存字节一致。
- 验证：生成帧必须携带真实运动（中点修正前后对比）；黑屏/冻结/异常退出必须回滚；偏移与内核补丁针对我们 Release 的 DLL 单独审计留档。

### D. 30 系（原生 2X，可选立项）

- MFG 不可能：DLSS 4 snippet 无 sm_80/sm_86 机器码（PTX sm_89×70、PTX sm_120×31、cubin sm_89×31），PTX 只向前兼容 → 模块加载失败；社区评估重定向后因 Ampere FP16 张量吞吐不足而放弃。
- 可选路径：移植 `dlssg_for_sm86`（MIT）拿**原生 2X**（4K 显存约 +700–770 MiB）；或改用 FSR 补帧（工作流 E）。
- 立项前需确认：是否接受引入另一份 DLSSG 运行库身份（Release 范围变更）。

### E. AMD 补帧（AMD FSR Frame Generation）

- 命名澄清：**FSR 3 / FSR 4 是超分版本号**；补帧叫 **AMD FSR Frame Generation**（旧称 FSR 3 Frame Generation，SDK 里叫 Frame Interpolation）。AFMF 是 AMD 驱动层补帧，不在我们范围。
- 版本策略（用户："有新的肯定用新的"）：优先 **SDK 2.3.0 的 `AMD FSR Frame Generation 4.0.1`（ML）**；不支持的显卡回退本地 SDK 1.1.4 的 3.1.x；界面显示实际生效版本。
- 接入：新增 `FrameGenerationBackend::Fsr`，与 DLSS / XeSS 并列；复用 `FrameBatch` / `PresentationScheduler`；按 SDK 的 Swapchain 契约管理资源与标记（中等接入量）。
- 倍率现实：官方设计是"每真实帧 1 张生成帧"（2X），是否支持更高倍率以 SDK 文档与实测为准，不对外承诺 4X/6X。
- 验证：真实运动（非重复帧）、批次内节奏均匀、与 DLSS/XeSS FG 同源画质与延迟对照、至少 NVIDIA 与 AMD 各一张卡。

### F. FSR 超分（按显卡分档）

- **AMD 卡**：FSR 4.1（ML，SDK 2.3.0）+ FSR 2/3.1；FSR 4.1 官方支持列表为 RDNA4，SDK 2.3.0 起 RDNA3 独显亦支持。
- **N 卡**：FSR 2 / FSR 3.1（非 ML，compute 实现，全卡可用），作为第三条超分路径与 DLSS/XeSS 对照；**不开放 FSR 4.1**。
- **N 卡跑 FSR4 的现状（2026-09-16 核查，结论：暂无可用方案）**：
  - `optiscaler/OptiScaler`（11047★）README 明确：FSR 4.X *officially RDNA4 and RDNA3 dGPUs only*；
  - 唯一针对 N 卡的 `int3rrobang/fsr4-int8-reverse-engineering`（MIT，0★）为**研究预览**：逆向 INT8 路径、目标 Turing、重写 11 个 4.1.1 内核；离线字节一致，端到端仅"一台机器的用户视觉报告"，自述 *do not ship this*，且不含 AMD 二进制；
  - 原因是"要重做"而非"解锁"：ML 模型 + AMD 打包内核本就走 RDNA AI 指令，换平台等于重实现内核/量化/调度。
  - **跟踪策略**：列入观察名单；出现"可移植、可复现、非研究预览"的实现或 AMD 官方跨厂商支持时，按决策 #1 直接移植。

### G. 杜比（直通优先，解码兜底）

- 前置事实：本机采集卡音频端 23 个媒体类型**全部为 2 声道 PCM**（无 AC-3/E-AC-3/TrueHD/DTS 位流）；我们代码现只接受 PCM；PS5 串流音频是 Opus，与该工作流无关。
- 效果排序：① **位流直通**（DD+/TrueHD 原样给功放，真 Atmos、零损失）② FFmpeg `eac3/ac3/truehd` 解码为 5.1/7.1 PCM（对象层丢失）③ 不做软件 Atmos 对象渲染。
- 设计：设备能力探测（记录全部 subtype）→ 采集侧接受压缩 subtype 且不解析/不解码/不重采样 → 输出侧 WASAPI 独占协商 `IEC61937_DOLBY_DIGITAL_PLUS(_ATMOS)` / `_DOLBY_DIGITAL` / `_DOLBY_MLP` 等 → 失败回退解码模式；直通模式绕过全部 DSP（音量由功放控制）；UI 提供"音频模式：直通/解码/关闭"并显示实际协商格式。
- 合规：不得写"支持 Dolby Atmos"，只写"杜比数字+ 位流直通（含 Atmos）"与"解码为 5.1/7.1 PCM"。
- 验证：需位流直通采集设备 + Atmos 功放（用户侧硬件，当前设备不具备）；软件侧先做 subtype 解析、格式协商、失败回退的 dry-run。

## 4. 交付顺序与验收门槛

| 阶段 | 内容 | 门槛 |
| --- | --- | --- |
| A-1 | XeSS 能力查询修正 + 补丁管理器 + 五处偏移审计 | 偏移审计脚本、失败注入、无 UI 变化 |
| C-1 | 40 系前置实验（直接提交 `multiFrameCount=2/3`，不写补丁） | 判定门在"能力上报"还是"生成内部"，形成结论文档 |
| B-1 | DLSS 6X 结构扩容 + 能力驱动倍率 UI | 插值/批/池/SRV 扩容 + 上限注入测试 + 50 系实机 6X |
| C-2 | Ada MFG 解锁（移植三件套） | 真实运动验证 + 50 系对照（六条保证逐条验证） |
| A-2 | XeSS 节奏 hook + UI 倍率 + 统计 | 2→3→4→2 连续性 + overlay 共存记录 |
| E-1 | 引入 AMD FSR SDK 2.3.0，接 4.0.x ML 补帧（回退 3.1.x） | 真实运动 + 节奏 + 双厂商对照 |
| F-1 | FSR 超分（AMD 4.1 / N 卡 2、3.1） | 与 DLSS/XeSS 同图对照；N 卡不出现 FSR4 选项 |
| D-1 | 30 系原生 2X（可选，需先授权运行库身份） | 独立立项后另定 |
| G-1 | 杜比位流探测 + 直通骨架 | 设备能力如实显示；无位流设备显示"不支持" |
| G-2 | 杜比解码兜底 + 格式协商 + UI 音频模式 | FFmpeg 解码器确认 + 回退路径 |

每阶段按项目纪律交付：构建 + 本次相关自动/手工检查 + `docs/WORKLOG.md` 记录命令/结果/日志路径/失败与修复；未执行项明确写"未执行"。

## 5. 依赖用户/外部的待办

1. **AMD FSR SDK 2.3.0**：下载、落 `third_party_local`、登记来源版本与哈希（新增本地 SDK）。
2. **40 系实卡**：C-1/C-2 需要一张 40 系卡做实测（用户反馈机 RTX 4060 可用则优先）。
3. **杜比硬件**：位流直通需支持位流输出的采集设备 + Atmos 功放/回音壁。
4. **发布范围**：FidelityFX 运行时 DLL、ML 模型、另一份 DLSSG 身份是否进 Release，均需单独授权。


## 6. 附录 A：关键证据与上游细节（施工时直接查这里）

### A.1 XeSS MFG：上游五处补丁（OptiScaler，固定提交 `70676c5f`）

| 代号 | 位置 | 原指令 → 改后 | 作用 |
| --- | --- | --- | --- |
| U1 | `0x20DA4F` | `jne → jmp` | 每帧帧数解析不再因 "XeLL too old for MFG" 回退 2X |
| U2 | `0x1A5DE4` | `je → jmp` | `Settings::mfgAllowed()` 恒真，阻止模型 17/18 降级 |
| U3 | `0x1A517D` | `mov ebx,3 → N` | 抬高默认插帧上限 |
| U4 | `0x1A45C2` | `mov [rdi+0x16c],1 → N` | provider 判定不可用时不再把覆盖值钉成 1 |
| U5 | `0x20973B` | `mov eax,1 → N` | 让 `xefgSwapChainGetProperties` 上报真实上限 |

另需节奏 hook：重定向 `0x25C0` 的 present thunk（上游 `XeFGPacing.h`），把 burst 内每个生成帧按 `duration/multiplier` 铺开；否则 >2X 会成串乱序。

### A.2 40 系 DLSS MFG：上游机制细节

- `NVSDK_NGX_GetGPUArchitecture` 硬编码最低架构 `mov eax,0x190`（Ada，40 系本就能过）。
- `DLSSGInstanceManager::PopulateParameters` 用 NVAPI 架构 id 与 `0x1b0`（Blackwell）比较，决定上报的最大生成帧数是 5 还是 1；同一常量的第二处比较驱动真正的生成能力——**两处都要改**。
- **NGX 加载时校验 snippet 的 Authenticode 签名**：磁盘改字节会让帧生成消失，只能内存补丁。
- 内核里编译死 `0.5` 混合权重 → 4X 会产出三张相同中点帧；必须重写 PTX 权重并重发 fatbin（上游 `TemporalFix`）。
- Ada 无硬件 flip metering，必须强制 `ForceFlipMeteringOff`（软件回退）。
- 30 系不可行的证据：DLSS 4 snippet 的 fatbin 只有 PTX sm_89 ×70、PTX sm_120 ×31、cubin sm_89 ×31，没有 sm_80/sm_86。
- 社区项目速查：`dashdogy/RTX40MFG-Unlock`（MIT，923★，最高 6x + Dynamic）、`ImDreamt/MFGAdaUnlock-RenoDx`（MIT，57★，内存补丁 + 中点修正）、`ShyVortex/dlss-unlocked`（MIT，474★）、`Nukem9/dlssg-to-fsr3`（4985★，20/30 系替换路线）、`sdli1995/dlssg_for_sm86`（30 系原生 2X）。

### A.3 本机实测证据（写计划期间的只读取证）

- 采集卡模式下窗口标题曾为 776 字符串：`Veyra — capture2:…`（已由 `cd3e829` 修复，与本文无关，记录在此避免混淆）。
- `MultiFrameCountMax`：RTX 5070 = **5**（6X 可用）；RTX 4060 = **1**（2X）；另有 4 条 NGX 记录为 0（查询失败/非 NVIDIA 路径）。
- `libxess_fg.dll`：1.3.1.78 / 22,957,432 字节 / SHA-256 `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27`；我们日志里 115 条 `maxInterpolations=1` 全部是"传 1 再回读"的结果，不是真实上限。
- 采集卡音频：23 个媒体类型全部 `subtype=0x00000001`（PCM）、最多 2 声道、8k–96k，无任何 Dolby/DTS 位流类型。

### A.4 我们需要改的本机代码位置（施工索引）

| 位置 | 现状 | 计划 |
| --- | --- | --- |
| `src/gfx/XessPresenter.cpp:82` | `init.maxInterpolatedFrames=1` 写死 | 按请求倍率设置，并用简单版 API 查真实上限 |
| `src/gfx/XessPresenter.cpp:60-70` | 未加载 `xefgSwapChainSetNumInterpolatedFrames` | 加载并在倍率变化时调用 |
| `src/ngx/DlssFgBackend.cpp:227` | `multiFrameCount>3` 拒绝 | 放宽到 `min(max,5)` |
| `src/pipeline/EnhanceGraph.cpp:395` | `fgMultiplier>4` 拒绝 | 放宽到 6，并按能力校验 |
| `include/veyra/pipeline/FrameBatch.h` | `std::array<BatchFrame,4>`、`interpolate` 禁 `n>4` | 扩到 6 |
| `include/veyra/pipeline/EnhanceGraph.h:325` | `generatedLeases_[6]`（每 parity 3 张） | 扩到 10（每 parity 5 张） |
| `src/engine/VideoPresenter.cpp:17` | SRV 堆 8 个（2 real + 6 gen） | 扩到 12 |
| `apps/veyra/SettingsWindow.cpp:66/94/118` | XeSS 倍率限 2、DLSS 限 4 | 按能力动态给 关闭/2X/3X/4X/6X |
| `include/veyra/engine/EnhancementSettings.h` | `multiplier` 1..4 | 1..6 + 预设 v11 迁移 |

## 7. 明确不承诺

- 不承诺解锁后画质/延迟一定优于 2X；不承诺所有 40 系/其他卡都能开到 6X。
- 30 系及更早不承诺 DLSS 多帧（内核缺失是硬事实）。
- N 卡不承诺 FSR 4.1（当前无可用实现）。
- 不承诺 FSR 补帧能到 4X/6X（官方设计为 2X，倍率以上限实测为准）。
- 不承诺在当前采集卡上获得 Atmos（硬件不提供位流）。
