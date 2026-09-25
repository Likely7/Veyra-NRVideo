# 当前项目状态 / Current Status

## 进行中：QML 新界面迁移与引擎改造（2026-09-25 开工）

分支 `codex/ui-qml-migration-20260925`，起点 main `df41580`（标签 `checkpoint/pre-ui-qml-migration-20260925`）。
按 [总方案](UI_MIGRATION_MASTER_PLAN_2026-09-25.md) 施工：S0 基线 → S1 引擎地基（画面不变）→ S2 NR 叠层 / 独立调色 /
可排序执行器 / 导出补齐 → S3 Qt 探针 → S4 QML 全功能界面 → S5 删除旧界面与打包。设计稿为
`prototypes/ui-redesign-2026-09-25/`，设计决定见 [界面设计方案](UI_REDESIGN_QML_NODE_PLAN_2026-09-25.md)。
进度、存档标签、测试结果见 [执行记录](UI_MIGRATION_EXECUTION_2026-09-25.md)。补帧锁定在最后；只显示提交帧率；
不保留旧界面。未合并 main、未推送、未发布。

## 正式版 1.4.4 已发布（2026-09-22）

`v1.4.4` 已推送到 GitHub 并作为 latest 发布：源码提交 `6851c27`，
https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.4 。
便携包 472478966 字节 / SHA256 `BFF5149B55F8665FEBEAECEEE421956DF72370FA56515A013EE6ABBD45B8878B`，
对应源码 215437386 字节 / SHA256 `7D3A8EFED76422EAB536D714221F579F1478921B927149FBB5ADFFA8635C53BA`；
发布 EXE SHA256 `92AC81B14AB050895B33FD2A822C179F65C4A42BD89C6C00772B89C5684D7121`，
与用户已验收的 test6 包一致。便携冒烟 7/7 PASS，受影响 UI 门槛全过；README 更新日志只保留 1.4.4
（介绍/功能表/使用教程保留；首次改写误删，已在 `b5821cc` 找回）。
完整步骤、失败记录与遗留见 [1.4.4 发布执行记录](RELEASE_1.4.4_EXECUTION.md)。

## 1.4.4 test6：UI 修复重构建（2026-09-22）

test5 反馈的两处 UI 问题已修复并重构建：`严格补帧节奏` 之前不在帧生成页的 final layout 里
（保留创建坐标 `y=190`，与上下两行实测重叠 4/12 dip），现已按 8 dip 节奏独占 `y=370` 一行；
面板滚动改为在输入消息内提交整帧重绘（`RDW_UPDATENOW`）+ 视口外行虚拟化，滚动瞬间抓屏与
稳定帧差值 4263→0 像素。补帧调度逻辑未改。新增 `veyra_settings_layout_tests`；受影响
UI 门槛全过。产物 `E:\项目\Veyra\test-packages\1.4.4\Veyra-1.4.4-test6`
（`veyra.exe` SHA256 `92AC81B14AB050895B33FD2A822C179F65C4A42BD89C6C00772B89C5684D7121`）。
**未推送、未发布。** 详见 [WORKLOG](WORKLOG.md) 顶部。

## 最新入口：统一修复计划（2026-09-22）

分支 `codex/fg-independent-repair-20260922`（存档 `checkpoint/pre-fg-independent-repair-20260922`）。
已实施并短测的 FG 修复见 [执行记录](FG_INDEPENDENT_REPAIR_EXECUTION_2026-09-22.md)：
XeSS 真实源周期提示、有界跳帧保留历史、DLSS 超预算对改 2X 组、显示帧统计；
X2/X3 已撤回。[统一修复计划](UNIFIED_REPAIR_PLAN_2026-09-22.md) 第 1–9 批已实施（标签
`checkpoint/plan-b9-done-20260922`），结果、XeSS 复跑排查（时段差异，非回归）、
直写 upload 堆的实测否决与专业模式延迟实测见
[执行记录](UNIFIED_REPAIR_EXECUTION_2026-09-22.md)；
依据为 [全软件清扫](WHOLE_SOFTWARE_SWEEP_2026-09-22.md) 与
[采集延迟复查](CAPTURE_LATENCY_REVIEW_2026-09-22.md)。运行入口
`E:/项目/Veyra/tests/fg-independent-repair-20260922/app/veyra.exe`（staging，非便携包）。
长测、实卡、肉眼验收由用户执行；未合并 main、未推送、未发布。下文为历史状态。

## 最新结果：有界修复收尾（2026-09-22）

本轮已完成有限范围的实施与验收；下节“只出方案”为历史记录。
开工存档 `7724ea8` / `checkpoint/pre-bounded-repair-20260922`，
当前分支 `codex/bounded-full-chain-20260922`。
保留修复：HDR 查询状态按显示器隔离 `f718c02`，采集格式身份恢复及协商核对
`7fe6201`，分别有 `checkpoint/hdr-target-state-20260922` 和
`checkpoint/capture-format-contract-20260922`；此前 P010 上传优化继续保留。
完整证据见 [有界修复执行记录](BOUNDED_REPAIR_EXECUTION_2026-09-22.md)。

产品构建、相关 UI/字幕/颜色/预设、后端切换、暂停 seek、实卡格式重连通过。
独立的运动矩形诊断在 DLSS 6X 位置误差门槛失败，已记录，不能列入通过项。
RTX5070 原生视频+NR：DLSS2 末段约120提交/s，DLSS6约287但P99仍16.79ms；
真超分+NR：DLSS6约148提交/s，XeSS4约40源提交/s。均为有界追踪末段，
不是整段均值或物理屏幕FPS，也没有同期旧版A/B证明本轮性能收益。

高倍率不均匀、欠速闪烁、间歇卡顿及历史 XeSS 重负载倒退仍未解决。
本轮没有新的产品调度改动；查过 Intel/Magpie 合同后未确认可靠的新修复点，
按用户要求停止无依据的性能尝试，既不宣称全是硬件瓶颈，也不宣称全部修好。
未重新验收的设备/功能及未交付项见总台账。本机开发运行入口：
`E:/项目/Veyra/tests/bounded-repair-20260922/app/veyra.exe`，依赖本机链接，非便携包。
本轮没有打包、合并 main、推送或发布；关机由最终交接执行。

## 当前任务：全链路回归复核，只出方案（2026-09-22）

完整问题入口：[全软件问题台账及历史 DLSS/XeSS 专项](WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22.md)。
历史 DLSS 6X 的高提交率/长空档、真超分+NR 下 XeSS 4X 相对 1.4.3 的源连续性倒退
仍未解决，列为第一优先级；最新采集日志不取代这些问题。台账同时列出其他模块的
未解决项、代码/功能缺口、已有修复的实机验收边界和暂缓项，不把它们一概标成新 bug。

最新入口：[卡顿、闪烁与采集清晰度全链路方案](FULL_CHAIN_REGRESSION_REPAIR_PLAN_2026-09-22.md)。
当前开发分支 `codex/5090-capture-fg-20260921`，HEAD `edfd886`，仍在
`E:/项目/Veyra/worktrees/playback-nr-20260920`。下方分支名和“下一步”属于历史状态。

新用户日志的 8 次图初始化均关闭 SR/NR/FG/NVOF；4K30 P010 有约 1.18/1.31 秒
真实回调来帧间隔。上游停供、应用回调阻塞与线程调度仍未区分，不能归因于补帧。
另确认采集最近打开只持有数字格式索引、默认重连帧率核对缺失的合同风险；
日志中有手动改模式，不能称用户已遭静默降级。采集色度/最终缩放路径差异单独验清晰度。

用户要求先排查和写方案，本轮未新增产品代码或运行测试。既有未提交 HDR 查询
三态候选仅构建未运行，SDR 呈现资源测试通过不等于用户欠速闪烁通过。
“约 15 秒”不作为固定计时器假设；DLSS/XeSS 从 2X 到各自最高倍率一起覆盖。
已失败的扩队列、固定等待、拆 owner、重叠、全局取消 admission 等未列为新施工项。
没有新增包、提交、合并、推送或发布；用户实际闪烁和间歇卡顿仍未解决。

## 最新实修：5090 P010 采集反馈（2026-09-21）

工作分支 `codex/5090-capture-fg-20260921`，开工存档 `3953bff`。
已去掉 CPU P010/P016 上传前的冗余转换/复制；本机两轮 P010 上传中位耗时
约 2.4ms 降至 0.93–0.98ms，18 项实际 GPU 像素输出与基线完全一致，HDR
颜色测试及 DLSS 呈现资源回归通过。不改变画质、倍率或增加等待。
这不是 5090 整链路帧率验收；15 秒卡顿、欠速闪烁、高倍率不均匀仍未解决。
实测工况、范围和证据见 [5090 采集修复记录](RTX5090_CAPTURE_FG_REPAIR_2026-09-21.md)。
未更新既有便携包；本轮仅本地构建，无合并、推送或发布。

## 当前执行入口（2026-09-21，链路复核后）

1.4.3 之后的完整修复、回退、诊断和未完成项总账见
[POST_1_4_3_REPAIR_LEDGER_2026-09-21](POST_1_4_3_REPAIR_LEDGER_2026-09-21.md)。

此前研究方案：[DLSS / XeSS 补帧执行与呈现重构](FG_RUNTIME_REPAIR_PLAN_2026-09-21.md)。
基于 `b69d22c`、固定 Magpie 源码、Intel SDK 合同和既有测试；本轮仅更新文档，
未实施重构、未运行新性能测试。方案覆盖 2X、DLSS 最高 6X 与 XeSS 最高 4X，
先核对资源和线程合同，只修有证据的时间/准入或资源错误；不再把关键路径重叠列为默认待办。当前不能承诺
所有硬件与负载有 90% 成功率；高倍率稳定性和 30/40 本轮实卡验收仍未完成。

本轮另生成两个 1.4.4 XeSS A/B 测试包：当前 XeSS 与仅恢复 1.4.3 XeSS
实现的对照包。两包都保留同一 1.4.4 其他修复，短测均能进入 XeSS 4X；
路径、差异和限制见 [1.4.4 XeSS A/B 对照](XESS_1_4_4_AB_COMPARE_2026-09-21.md)。

继续优化前先查 [实验索引与重试约束](FG_EXPERIMENT_INDEX_2026-09-21.md)：
区分已删除的失败实验、默认关闭的未验收候选和只读诊断，禁止无新证据重复已失败方案。
已同步并复核 [Magpie 最新 experimental 源码](MAGPIE_SCHEDULING_SOURCE_AUDIT_2026-09-21.md)：
上游仍为3841698；其DLSS最高4X且逐生成帧等待，XeSS包含时间戳改写和独立队列交接，不能直接推断性能更好。

既有执行与其他功能收尾：[补帧稳定性与现有功能收尾](FG_STABILITY_COMPLETION_PLAN_2026-09-21.md)。
用户已授权执行；源帧关联诊断已构建并运行，XeSS真实源时间提示候选在120秒测试中保持吞吐收益，
整段统计仍发现约四分之一的输出间隔短于1ms及约46ms的偶发长间隔；后端/倍率切换回归通过。
尚未通过完整节奏/画质验收，默认仍关闭。详见
[本轮诊断与候选](FG_STABILITY_PROGRESS_2026-09-21.md)。
新增只读 fence 追踪：重负载保留的351次首帧调度全部在进入时有未完成GPU依赖，
不能把约8.5ms的前段耗时全部当作无效等待删除；仍需区分队列积压、GPU工作与唤醒。
诊断版后端切换回归通过，尚无新增已接受的调度优化。
后续“XeSS仅保留一组待处理”实验降低了突发与帧龄，但源帧吞吐退步，已撤回并重新构建。
DLSS旧证据复核确认重负载整组空档主要伴随预算拒绝/历史播种；同组GPU耗时也超过60fps预算，
尚不能归结为纯调度故障或直接取消预算检查。具体数字见WORKLOG最新条目。
开发工作区为 `E:/项目/Veyra/worktrees/playback-nr-20260920`，
分支 `codex/fg-stability-20260921`；开工存档 `d2ae8ee`，诊断存档 `0143baf`。
正式版仍为1.4.3，最新本地交付仍为下述1.4.4beta。

- **XeSS**：真实1080p到4K超分＋NR＋4X的源帧连续性比1.4.3差；旧版反复
  关闭补帧，不能直接恢复它并宣称4X修好。果冻感、功耗波动尚未解决。
- **DLSS**：4X/6X重负载均匀呈现仍未通过；不能将原生4K输入的高FPS套用到真实超分。
- **输出限制**：默认不限制；XeSS/FSR输出限帧尚未完成。
- **NR**：防闪默认关闭、部分样本验证通过，快速游戏画质及重负载有待验收；
  独立降噪未接入，模型风格仍为0/1/2，NR叠层继续暂缓。
- **UI/字幕/采集**：已有修复和局部回归不撤销；快速滚动、物理跨屏、受影响
  设备及HDR浮窗观感不得算全面通过。杜比视界新处理继续暂缓。

最新同条件证据见[版本对照](RELEASE_143_SCHEDULING_COMPARISON_2026-09-21.md)，
原因与未知项见[链路审查](FG_PIPELINE_REASSESSMENT_2026-09-21.md)。
下文按历史顺序保留；“未发现回退”“通过”“最新”等只适用于对应日期和工况，
与本节冲突时以本节及其引用证据为准。

## 既有交付与历史记录

2026-09-21 local test package delivered from source checkpoint `f029707`:
`E:/项目/Veyra/test-packages/1.4.4beta-20260921-nr-followup/`.
Seven portable smokes, packaged DLSS/XeSS switching and 124-file ZIP
verification pass. This is partial capability acceptance; the true-SR
high-multiplier cadence and independent denoise limitations below remain.

2026-09-21 follow-up: retained temporal mask/time corrections and GPU
neighborhood reuse; fixed repeated Present-deviation sampling and added
separate entry/return diagnostics. NR temporal remains default-off.
Local 120-second DLSS/XeSS 4X tests with original 4K (SR bypass) pass;
true 1080p-to-4K SR plus NR fails 4X/6X cadence acceptance. Independent
VFX denoise cannot create its effect and is not integrated. No fixed delay,
hidden downshift, new runtime, main merge or publication. Detailed results:
[current follow-up acceptance](NR_FG_FOLLOWUP_ACCEPTANCE_2026-09-21.md).
This supersedes the timing interpretation and pending tests below.

2026-09-21 local bounded NR/performance review: temporal protection/time fixes
and shared-neighborhood GPU optimization tested on RTX 5070. Default remains
off; natural-video visual acceptance and independent VFX denoise are not
delivered. No general 1.4.3 performance regression established. See
[evidence and limits](NR_QUALITY_PERFORMANCE_ACCEPTANCE_2026-09-21.md).
No new release or main merge.

**2026-09-20：1.4.3 已正式发布为 [GitHub Latest](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.3)。** 发布源码/标签对应 `3b4570e`，正确修复已整合至 main；正式便携包七项检查、120 个载荷校验、对应源码 856 个文件校验及四项远端资产哈希核对通过。用户最新测试包与正式 EXE 完全相同。固定 6X 均匀呈现及未定因设备反馈仍按下方边界处理。发布记录见 [RELEASE_1.4.3_EXECUTION.md](RELEASE_1.4.3_EXECUTION.md)。

2026-09-20：用户验收最新 1.4.3 测试包并授权正式发布。当前发布工作包含截至 `139db25` 的产品修复及远端中文 README 精简，分支审计与正式构建/发布结果统一记录在 [1.4.3 发布执行记录](RELEASE_1.4.3_EXECUTION.md)。下方“仅本地/未发布”语句均为历史阶段记录；固定 6X 均匀呈现仍未解决，不随正式版发布改为通过。

2026-09-20 后续修复：设置数值草稿、羽化刷新递归通知和单项还原已修正；跨屏 DPI 字体/布局刷新合并到异步 UI 消息，避免 SetWindowPos 内重复布局。DLSS 6X / XeSS 4X 合成 DPI 切换与设置回归通过。本机仅一个物理显示器，不能宣称真实双屏卡死已验证消除。新本地包目录 `E:/项目/Veyra/test-packages/1.4.3-20260920-display-fix/`，验收见窗口修复文档。本轮不改变补帧调度。

2026-09-20 窗口边角拖动崩溃已定位为 UI 回调栈耗尽，拆分 AppShell / SettingsWindow 的重型消息处理后，本机真实鼠标拖动、采集、DLSS 6X / XeSS 4X 缩放和合成 DPI 回归通过。新版测试包输出到 `E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix/`；下方 final 目录为旧包。本轮不修改补帧调度，固定 6X 均匀呈现仍未解决。见 [窗口缩放修复](WINDOW_RESIZE_REPAIR_2026-09-20.md)。

2026-09-20 用户决定暂止优化，交付重新构建的 1.4.3 本地测试包。基于 `d60ec88`，产品代码保持 `7a3dfae`，失败实验均回退；固定 6X 均匀呈现问题仍未解决。最新包位于 `E:/项目/Veyra/test-packages/1.4.3-20260920-final/`，构建、CPU/UI 回归及便携包七项运行检查通过。未推送或发布 GitHub；此前“目标继续”和“未打包”是历史状态。

2026-09-20 最新约束与实验：跳过 NR 优化，不增加等待，不自动降档。`7a3dfae` 保留用户所选倍率，`55e0bdd` 撤回不安全的共享状态回读。新增整组耗时预测、取消重复清屏、光流历史直接复制三项对照均未证明显著改善，已全部回退；固定 6X 本轮留存窗口约 298–303 提交/s，仍有约 17ms 空档，**尚未修好**。详见 [非 NR 实验记录](FG_NON_NR_EXPERIMENTS_2026-09-20.md)。下文自动 4X 结果仅是历史，已不代表当前行为。

最新恢复修正：预算不足时在当前原帧完成可承受的单次预热，避免下一组再次只出原帧。固定 6X 同参数约 280 提交/s，仍有约 15ms 空档，尚未完成均匀呈现修复。126 项 CPU 检查、2X/6X GPU 恢复检查、生命周期、主程序启动和 XeSS 2X 短测通过。详情见本轮部分验收文档，不能用降档结果替代固定 6X 验收。

更新：2026-09-20。发布状态与开发状态分别记录，历史计划不代表当前验收。

最新固定 6X 调查：同一 p001.mp4、RTX5070、NR 开启、4K 目标，首帧截止时间与排队成本修正后，固定 6X 末尾留存窗口约 265 提交/s，但仍有 15-17ms 空档，**尚未通过均匀呈现验收**。2X 仍稳定 120；请求 6X 自动回退实际 4X 时稳定 240，此结果不能代替固定 6X 修复。关闭 NR 的诊断对照固定 6X 可达稳定 360，不作为用户解决方案。121 项 CPU 检查及 GPU 恢复/倍率检查通过，详见 [本轮部分验收](FG_CADENCE_REPAIR_ACCEPTANCE_2026-09-20.md)。仅本地构建，未打包或发布。

最新节奏调查：已存档 `b2c3d0e` / `checkpoint/pre-fg-cadence-audit-20260920`，在 `codex/fg-cadence-audit-20260920` 复测相同实卡参数。关闭逐帧文件日志后仍约 194 提交/s，末尾 8.07 秒内 90 次间隔超过 16.667ms，478 批中 233 批只呈现原帧，直接记录到 114 次拒绝后预热。确认提交节奏断续，尚未修复；PresentMon 权限不足，未测物理显示。见 [节奏调查](FG_CADENCE_AUDIT_2026-09-20.md)。

最新 DLSS 恢复修复：`codex/dlss-recovery-20260920` 从 `c8a3828` 隔离，将无有效 A/B 历史时的整组 MFG 重置改为单次完整预热，按实际预热成本判定预算，新增未执行重置候选计数。主程序构建、105 项 CPU 调度检查和 RTX5070 的 1080p NR+DLSS 2X/4X/6X 恢复检查通过，D3D12 错误为 0。用户关闭程序后，实卡 1440p60 NV12、NR＋DLSS SR 到 4K＋6X 各 120 秒对照：提交率 202.57→204.10/s，预热调用减少但主要吞吐限制未解决；关闭测试预算拒绝反而降到 184.76/s，采集丢帧及生成过期增加。未达到稳定 6X，不能宣称已修好。未覆盖用户便携包、未发布。见 [恢复修复记录](DLSS_RECOVERY_REPAIR_2026-09-20.md)。

最新切换修复（09-20 最终回归）：`codex/fg-backend-switch-20260919` 修正 XeSS 旧倍率缓存、UI 请求拒绝、失败回退及异步准备帧的 XeLL 标记周期。RTX5070 引擎/真实下拉切换、失败注入、拖动/重置/缩放回归通过。YUY2 50fps 合成输入 2X/4X 有生成输出，1:1 单像素测试未发现额外模糊；用户所述观感及与 PotPlayer 的差异仍缺同条件对照，不能宣称解决。详见 [切换修复记录](FG_BACKEND_SWITCH_REPAIR_2026-09-19.md)。仅本地编译，未更新用户便携包或发布。

最新补充修复：`codex/seven-audit-20260919` 基于 `80f7dc4` 完成七项独立审查修复，涵盖压缩采集参考链/帧身份/PTS、FFmpeg EAGAIN 重送、D3D11 引用释放及字幕布局/缓存；同时修正构建依赖编码和字幕设置编译类型问题。干净构建、H.264/HEVC 软件及硬解、4K 文件、字幕像素、时序回归和主程序启动检查通过。实际采集卡队列与 GC573/GC551 未验收，详见 [七项修复报告](SEVEN_AUDIT_REPAIR_2026-09-19.md)。此前 DLSS/XeSS 调度修复保留；本轮仅本地，未打包或发布。

最新本地修复：`codex/fg-scheduling-repair-20260919` 从存档 `5a1931b` 修复 DLSS 暖机预算污染、CPU Present 重复计费、窗口采集期限抖动，以及 XeSS 实时呈现前的额外相位等待。构建和本机持续生成/拥堵恢复测试结果见 [补帧调度验收](FG_SCHEDULING_ACCEPTANCE_2026-09-19.md)。GC573 53fps 只新增定位诊断，尚无实卡根因与修复证明；5080 功耗、30/40 系和物理显示验收未覆盖。本轮未打包、合并 main、推送或发布。

**1.4.2 已正式发布并设为最新版**：[GitHub Release](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.2)，标签提交274d826。正式包、对应源码和SHA256共四资产已核对远端digest；软件短测、HDR组合与便携包七组检查通过。具体边界与发布后证据见WORKLOG。以下为历史开发记录。

当前任务：用户已授权正式发布 **1.4.2**，整合 RTX Video HDR、帧同步及后续采集/字幕/UI 修复，并修复全屏操作提示残留。新群二维码与用户 HDR 对比照片纳入 README/Release，赞助图保留。正式发布进度、验收与资产以 [1.4.2 发布记录](RELEASE_1.4.2_EXECUTION.md) 为准，下方“仅本地”“未合并”等均是先前阶段记录。

最新本地交付准备：`codex/screen-capture-20260919` 汇总此前隔离修复，构建显示版本 1.4.2，新增设备帧率手动协商、恢复专业模式 PS5 入口、修复设置滚动及选框重绘。下方“未打包”等描述为各阶段当时状态，本轮以 [1.4.2 验收记录](UI_CAPTURE_RATE_1.4.2_2026-09-19.md) 和 WORKLOG 为准。仅本地测试包，无 main 合并、推送或 GitHub Release。

## 已发布 1.4.1

- RTX 30/40 DLSS 开启、6X 解锁及 3060 初始化/执行卡死修复；用户已反馈测试可用，不代表所有型号和驱动均覆盖。
- 修复持续播放后的补帧受限、源帧与生成帧调度、采集瞬时停顿；软件 FPS 是提交读数，不是屏幕物理刷新率。
- MKV 字幕与音轨选择、打开/拖动、设置保存、全屏播放阻止休眠等修复已发布。
- 导出保留实际编码/封装错误处理，已取消资格门禁与结束逐帧扫描。
- 原生 HDR 输入保留、HDR 基底增强和 HEVC Main10 导出已存在；这不等于 SDR 转 RTX Video HDR。
- Dolby/DTS 采集可解码为 PCM；不等于压缩码流直通或 Atmos 对象渲染。圆刚专项探索已收尾；独立诊断工具留在存档分支。
- Smooth Motion 由 NVIDIA App 管理，用户确认本机可用。Veyra 不修改驱动配置，不把驱动生成帧计入导出。

发布证据见 [1.4.1 执行记录](RELEASE_1.4.1_EXECUTION.md)、[更新说明](RELEASE_NOTES_1.4.1.md)。

## 本轮开发

新增本地修复（2026-09-19）：[5060、命名调色预设与图形字幕报告](5060_PRESETS_BITMAP_PLAN_2026-09-18.md)。从 `050811b` 存档后在 `codex/5060-presets-subtitles-20260918` 隔离完成预设按钮重叠/窄窗口布局、命名保存反馈、PGS/DVD/DVB图形字幕显示及状态误分类修正；CPU、实际UI、5070 DLSS2/4/6X和VC-007PRO NR+DLSS4X短测通过。5060日志实际效果全关且输入时间线反复重置，新增原因诊断，**尚未根治或通过该用户实卡验收**。保留可运行构建 `E:/项目/Veyra/build/frame-pacing-20260918/veyra.exe`，未打新包、合并main、推送或发布。

VC-007PRO 1440p60 / 4K30、NR实时+DLSS4X 的同构建对照已完成，各120秒中位软件延迟25.36/40.09ms。追加20秒逐帧短测：原帧观察就绪后等待呈现15.92/29.65ms；2K60自动/最小/驱动默认均实际分配10个allocator缓冲，没有测得缓冲选项带来的有效差异。详见[1440p60与设备缓冲实测](CAPTURE_1440P60_COMPARISON_2026-09-18.md)，非HDMI到屏幕端延迟，未修改产品调度。

帧同步已在隔离分支 `codex/frame-pacing-20260918` 实现并完成本机短测，基线存档 `6e69eeb`。入口：专业模式 → 运动 → 帧同步，默认关闭；低排队、均匀呈现、Reflex 实验和独立显示同步偏好可保存。无补帧文件测试减少了一帧提前缓存驻留；6X 提交吞吐保持 144fps，未证明显著降延迟或更平滑。Reflex 无补帧时真实调用 NVAPI，开启补帧时明确回退低排队并保留倍率；XeSS 继续由提供方调度。VC-007PRO 4K30 NV12、NR+DLSS4X 已完成四模式各 120 秒实卡测试，详见[采集阶段计时](CAPTURE_LATENCY_DLSS4X_2026-09-18.md)。屏幕端到端延迟、30/40 实卡、PS5、VRR 未验证，完整原计划尚未全部验收。其他验收见[帧同步报告](FRAME_PACING_ACCEPTANCE_2026-09-18.md)。未合并 main、推送、发布或更新现有 1.4.2beta 包。

采集呈现相位优化及原始 1.4.2beta 引擎同参数对照见[延迟优化与历史对比](CAPTURE_LATENCY_REDUCTION_2026-09-18.md)。保持 4K30 NV12、NR、DLSS4X，不使用换格式或降低倍率解释收益。

DLSS / XeSS 双侧长帧调查及后续修复见[调查](FRAME_STALL_INVESTIGATION_2026-09-18.md)与[修复实测](FRAME_STALL_REPAIR_2026-09-18.md)。XeSS 改为保留呈现缓冲，三次缩放从丢 9 帧降到 0，代价是小窗口原生 NR 测得约多 2–4ms；另修复过期生成帧先等 GPU、阻挡就绪原帧的问题。DLSS4X 与 XeSS2X 后续各 120 秒均零采集丢帧，DLSS 注入阻塞恢复及 2X/6X/4X 切换通过。不代表全部卡顿已修好：两后端均抓到第 304 帧 NR Evaluate 约 49ms；另一次 DLSS CPU 尾部约 388ms、粉丝 HDR 正常播放约 1.4 秒呈现阻塞仍未根治。保留细分诊断，不更新既有 beta 包。

RTX Video HDR 独立改动保留。用户已明确终止 NVIDIA FSR 4.1 实验并要求回退，不再执行此前 FSR4 施工和画质修复计划。

**最新决定：FSR4 画质不接受，已撤掉实验入口和接入。** 原有 DLSS SR、RTX Video SR、官方 AMD FSR 保留；旧实验预设 mode 6 读取时转为 RTX Video SR 高档，保留其他参数。HDR 在 SDR 显示器预览不执行转换，保留开关旁的真实状态提示。回退构建与检查结果见 WORKLOG。

- 存档提交 `0e3d4ac`，标签 `checkpoint/pre-video-hdr-fsr41-20260918`。
- 隔离分支 `codex/video-hdr-20260918`；工作区 `E:/项目/Veyra/worktrees/video-hdr-20260918`。
- RTX Video HDR 已接入共享图、设置和 Main10 导出；RTX 5070 / 616.56 上 Create/Evaluate、2X/4X/6X、NR+SR+6X、原生 HDR 回归、HDR 导出与缺库 SDR 回退通过。显示器 HDR 当前未开启，实际 HDR 显示、采集卡、PS5 与 RTX 30/40 未执行。
- HDR 里程碑提交 `3477ed2`；后续隔离分支 `codex/fsr41-nvidia-20260918`，同名外部工作区。
- NVIDIA FSR 4.1.1 INT8 的 UI、环境变量接入、后端适配、专用测试及构建/打包脚本已回退到 HDR 里程碑前的 FSR 实现。旧日志和外部研究产物只作失败实验记录，不作为当前可用功能。
- HDR 参数页在 1280x800 窗口已目视检查，滑块数值即时更新。全部测试、限制及本地包说明见 [本地测试记录](LOCAL_HDR_FSR41_TEST_2026-09-18.md)。
- 用户授权本地 `1.4.2beta` 内测群包，新增已核验的官方 TrueHDR 原件；未授权 GitHub push/Release 或 Agent 代发。便携包路径、哈希及解压后实测见 WORKLOG。实际 HDR 屏幕和 RTX30/40 HDR 仍待内测。

所有新产物放 `E:/项目/Veyra/`，源码/文档留在隔离工作区；逐项结果以 [WORKLOG](WORKLOG.md) 为准。
