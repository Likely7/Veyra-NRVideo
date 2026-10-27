# 当前继续修复状态 — 用户于2026-09-08已恢复执行

最新用户澄清：测试上限是**单次最多300秒**，不是历史累计上限。下方交接中“剩余39.46秒/累计不得超过300秒/停止等待新会话”均已被本次用户指令取代；账本历史保留，仅用于耗时报告。

三个已知问题和源帧计数疑点已修复，最终18项联合回归通过，独立只读复审通过本次限定修复范围。最终状态以 `docs/REPAIR_V2_DELIVERY_2026-09-08.md` 为准；旧Phase字段不变。

---

以下为用户停止上一轮时的历史交接，不是当前待办：

# Veyra Repair v2 会话交接 — 2026-09-08

用户要求停止当前工作，交由新会话继续。本次仅保存交接，不再修改代码、构建或运行测试。当前状态：`needs_review`，R1–R8 已实现主要功能，R9 最终审查未通过；不得宣称全部完成。

## 新会话入口与约束

项目：C:\Users\123\Desktop\Veyra DLSS Video Player
完整原始请求：C:\Users\123\.codex\attachments\e80108b7-70bc-40ff-9259-349ad769d5cb\pasted-text.txt
先完整阅读 AGENTS.md 要求的项目文件、原始请求，再读本交接。旧 Phase 与历史 gate 不是本次通过证明；不要改旧 Phase 字段或用旧 gate 放行。

- 主 Agent 唯一写入者；用户允许独立只读 Reviewer。
- 不得接采集卡、下载/改动 SDK/runtime、提交/push/上传/打包；保护控制面及既有用户改动。
- 累计新运行测试上限 **300 秒**。账本 `logs/repair-v2/runtime-budget.json` 已花 **260.5401069 秒**，剩余 **39.4598931 秒**。绝不能重置预算。构建时间另计。
- 所有后续运行测试通过 `scripts/acceptance/repair-v2.ps1` 记账；不要重跑整个 all（预算不够）。
- 初始 HEAD d8f1964。初始 dirty patch、状态、控制面哈希在 `logs/repair-v2/r0-20260907/`。其中 delivery.ps1 原本已脏，不是本轮所改。已删除的 validation/fixed_clips/test_h264_1080p.mp4 不要恢复。
- NR 根目录及 runtime_local/nvidia DLL 原验证 SHA256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E；addon SHA256 837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F。本轮未加载 addon。

## 下一步：先修三个最终 Reviewer 问题

独立只读 Reviewer `review_repair_r5_r8` 最后结论仍 needs_review，发现以下三个明确问题。旧 Reviewer 会话可能无法在新会话续用，应从代码和证据重新只读审查。

1. **合法 CFR 的 PTS 量化被误判 VFR**：`src/export/VideoExportJob.cpp`（用 rg 确认实际路径）当前预读两帧，以第一间隔反推 fps，然后按 origin + count/fps 验证，容差 max(0.0002, .02/fps)。合法 60fps、1ms 时间基的 Matroska 时间戳 0/17/33/50ms 会推成 58.8235fps，第三帧即误拒绝。应从 FFmpeg `r_frame_rate` 获取候选有理帧率（不要只用 avg_frame_rate），结合源 time_base 的量化误差逐帧验证，保留真正 VFR 的明确拒绝。可在 FFmpegDemuxer / MediaFileSource / SourceInfo 暴露 nominal rate 和 timestamp quantum。增加纯逻辑量化 CFR/VFR 测试，并在剩余预算内用实际量化容器做有意义的短测。此前 avg_frame_rate 方案和首间隔方案都已失败，不要重复。
2. **参数回滚遗漏字段**：EngineController 参数事务失败时手工恢复 EnhanceGraph::Desc 字段，漏掉 `enableNvofStandalone`。可能恢复 NR 开启却没有 flow。事务开始保存完整 oldDesc，失败直接恢复整个结构，避免遗漏。核对 Desired/Applied 与图状态一致。
3. **B5 诊断丢失真实失败码**：NvOfSession 的 nvOFInit 等失败实际码、DlssFgBackend Create/Evaluate/SEH 目前常只写 Info，而 DiagnosticHistory 只收 Warn/Error。失败分支必须带真实返回码写 Error；核查 SR 同类路径。Logger 解析 NVOF status/st 别名。不要把成功 Info 全改成错误。

另一个主 Agent 尚未处理的疑点：FrameIdentity.sourceFrameId 目前来自图内部处理次数；参数修改重算缓存帧会递增，看起来像新源帧，source frame 计数也可能包含重算。应审查是否需要传 FramePacket.sequence 并分开实际源帧/图处理计数；这只是待核实项，不能当作已证实或已修复。

## 已实现内容（仍需最终审查）

- R1：ResolutionPlan、EnhancementSettings、FrameBatch、metrics/diagnostic 合同。
- R2/R4：共享 leased FrameBatch 真实 2/3/4X；generated 六纹理槽，源/生成帧有 fence/lease；NGX MultiFrameCount=N-1、Index1..N-1；每对共享 GPU status，完整 warmup，避免 warmup/source ID 冲突。Presenter/NVENC 都消费实际生成帧；尾部 Hold 明确单列。高精度 deadline wait 修复 Sleep 导致的节奏问题。
- R3：source-resolution NVOF 一次共享；4K SR base 和 NR1080 内部处理分离，面积下采样、引导 residual composite；原生 NR 可选、视频导出原生。新增 NrDownsample/NrResidualComposite/FlowAdapt shaders。
- R5：持久工作线程、单槽命令队列、UI 非阻塞 open/stop/export；Desired/Applied 整体快照事务；参数修改重算缓存帧、不 reopen 媒体；失败重建旧图回滚。CommandSlotRing discardRecording 修复注入失败后未关闭 command list 导致的 allocator E_FAIL。7 个 NR 与 5 个 residual 控件。PresetStore version1 严格白名单、临时文件 Flush/校验/原子替换、CRUD/default、坏文件保留阻止覆盖；路径 runtime_local/user-presets.v1。导出冻结设置。
- R6：NVOF 三档使用 SDK FAST20/MEDIUM10/SLOW5；caps/实际应用记录，失败不静默降档。ContentCadence 检测移动内容 30/50/60；手动档为“识别目标”，不做重采样；不匹配保留源 PTS 并警告，静止不能确认。
- R7：同帧 GPU source/base reference，按住/切换/分屏比较；比较时实际源帧标识，避免把原始帧冒充生成帧。GPU query ring 记录 color/SR/flow/NR/residual/FG 各子帧/batch/blit 真时间；CPU decode/submit/wait 分开。flow 是包含同步的队列区间。诊断 bounded64、去重、时间/级别/尺寸/身份、脱敏、预览后复制。
- R8：中文主界面和独立设置/统计/诊断窗口，DPI 缩放，F11/Alt+Enter/双击/Esc 全屏，原始图按住 V；多屏物理 DPI 未测试。

重点文件按 rg 定位：EngineController.h/cpp、EnhanceGraph.h/cpp、FrameBatch.h、ResolutionPlan.h、EnhancementSettings.h、VideoPresenter.h/cpp、VideoExportJob.cpp、CommandSlotRing.h/cpp、NvOfSession.h/cpp、DlssFgBackend.cpp、GpuTimer.h、ContentCadence.h、PresetStore.h/cpp、SettingsWindow/TelemetryWindow/DiagnosticWindow/DpiWindow、apps 主窗口、Logger/DiagnosticEvent/Redaction、shaders、tests/tools probes、scripts/acceptance/repair-v2.ps1、CMakeLists.txt。CMake 新增 base PUBLIC /utf-8 解决中文诊断编译编码问题。

## 最近构建及联合测试证据

最新构建 `logs/repair-v2/r0-20260907/build-r9-final.log` / `.json` exit0，约5.2秒。构建后只改过 acceptance 脚本；尚未修上面三个问题。
构建命令：
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root (Get-Location).Path -Preset x64-release`

最近联合检查：`logs/repair-v2/joint-bb22bfa233ee455ca030ba0eccdf5a86/result.json`，15 项软件检查 exit0，但 **不能覆盖其后静态审查发现的问题**。各项路径均为 logs/repair-v2/<ID>/：

| Case | Run ID |
|---|---|
| contracts | 82e26684824b43b0a29c52044d584cc9 |
| presets | 7e2f6c8adddb495991cb97fada187f24 |
| shaders | a64e0260509c43abba2fdd022a8e1177 |
| parameters | 3690e4226b074001bbe8f05f79b39c85 |
| fg2nr | fad542280b924d06b8d7de7159dd863e |
| sr4k | bfa154a8d3ac432588bf3c619cf6d63d |
| mfg3 | 8a83b2d3e6ca4b9585dee369c07fd775 |
| mfg4 | af7dabcc9d014b089cc4692665bde42c |
| player3 | 0e517e61610e435f9a8c047f4902c416 |
| settings | 1cfc480ddd1541eea16710b8b702459f |
| uicases | f48ae659f4c54d00be06564bc8a7c48e |
| rollback | 0670bad8af2f4e00be5fdb2d023eeeac |
| exportaudio | 002248981d4b4cc89e64968b94703171 |
| exporthevc | 38d99acd68cb4f56a61da23f74c29e47 |
| cancel | 55c595bbc7224f34b12cdb892a55a44d |

其他有用证据：
- Native4K NR 67ef0bf4657949acb1407d6abbe17445；4K realtime bd6002fad6b64601a0a27596f7b322f9。
- Flow performance fe1b2f140bb641d8b66a724ac347cd0f；quality db08c5f37bc0480dbb7906d7e0361211。
- 可见 UI 4X 6fa48edc0ba74ac5b69c26cd98fcaaab，600 real +1797 generated /10秒媒体；可见窗口测试整次35秒已记账。内部全屏组合键断言通过；实际跨显示器、光子延迟未测。
- 真实 12 源帧内容测试：2X11、3X22、4X33 generated，像素位置/hash/nonblend 验证。短导出24 source +69 generated +3 Hold =96，240fps，音轨测试保留1条 AAC。
- 参数像素实验（63d5f8cb4b714989a5d5892b7cf1c2f4，联合 parameters 同类）：intensity MAE3.2311，tone2.02611，structure1.85625，style3.78421，autoMask0.344721；**skin1.5 和 UI1 像素无变化，效果未证实**。Residual total0/1 MAE3.2323。不要把 API Applied 等同画质作用已证明。
- 注入故障用 VEYRA_TEST_REJECT_NR_STYLE2 在 NGX 前人为拒绝，不是真 GPU 故障。初次 d784238819db45bebba98667b1a2f97e 失败；discardRecording 修复后 577779a82200443fabb708a576b9600b 回滚 style0/intensity1/原位置成功。
- HEVC 初次 7ef9e5b105a24a2bbbbde302d1dd6e9c 因 avg60.1 vs 实际60拒绝；首间隔方案 7165627cfef64d999548b5fa8aab604d 通过，但仍有上述量化 CFR 缺陷。
- 初期 MFG 失败和 runner exitCode=null 的失败记录均保留并计时，不得删掉/隐藏。

## 收尾尚未完成

1. 修三个审查问题；只做有针对性的构建及短测，确保剩余39.4598931秒总预算。
2. 独立只读 Reviewer 再审，没有真实结论不得标通过。
3. 核验 protected-before.json 所列控制面哈希未变、runtime 身份、git diff --check；读取最终 EXE 哈希。
4. 更新 docs/WORKLOG.md、DELIVERY_STATUS、USER_GUIDE、loop/EVIDENCE.md/JOURNAL.md，并补完整最终交付报告；保留旧 Phase 历史字段。本交接已补 STATE repairV2 字段指向当前状态。
5. 汇总真实命令/exit codes、所有运行时间（含失败）、构建时间另列、实际 generated/Hold/PTS/尺寸/Applied 与参数效果。可离线统计 gpu-timestamp 的 p50/p95，不需要新运行测试。
6. 实卡、实际扫描输出/光子延迟、多屏 DPI、长时间稳定性、广泛场景画质均未执行；许可未解决，只限本机实验，不是公开发行完成。

当前唯一下一条代码任务：修 CFR 量化验证；之后处理完整 Desc 回滚及失败码诊断。用户本次要求停止，必须由新会话明确接续后再做。

