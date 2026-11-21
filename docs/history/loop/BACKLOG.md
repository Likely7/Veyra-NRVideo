# Veyra Launch V1 Backlog

## 当前唯一队列：2026-09-08 优化 Goal

按用户最新请求与 docs/QUALITY_OPTIMIZATION_PLAN_2026-09-08.md 执行；导出完整性检查保持现状，每次测试最多 300 秒。历史队列不删，不把未完成项改写为完成。

- [x] **Q0 控制基线同步 — 用户确认后已执行，preflight 71/71**：preflight 唯一失败 README hash，来自已授权的 f79ef95 文档更新；原 proposal 与授权后执行记录在 logs/optimization-goal-20260908/；已恢复施工。
- [ ] **Q1 输入尺寸**：解除统一 3840×2160 / 偶数尺寸拒绝，适配竖图、长图、奇数尺寸与实际 GPU/model 能力；不偷偷裁剪或冒充原生处理。
- [ ] **Q2 专业预览缩放**：画面悬停滚轮缩放，不抢设置面板滚动，不改变增强/导出分辨率、不重启源或 NGX。
- [ ] **Q3 颜色与时间**：duration / metadata / reset、RGB 中转损失、PNG 通道交换；导出检查不扩展。
- [ ] **Q4 SR 合同**：曝光、标志、motion 纹理/单位、subrect/reset；真实 GPU 对照。
- [ ] **Q5 UI 保护**：GPU 确定区域保护，私有资源先单独复验；区分 NR/SR/FG 与实验项。
- [ ] **Q6 motion/FG**：可信度、切镜与历史、真实 2X/3X/4X 调度；A/B 双向按成本验证，不增加默认 C 帧等待。
- [ ] **Q7 候选收益**：RTX Video SR 与深度按方案独立验证，未证明收益不默认堆模型，缺依赖/条款如实记录。
- [ ] **Q8 软件门禁与独立只读复核**：处理旧尾帧合同差异，不降低验证；真实短测、Reviewer、本地 checkpoint；实卡/分发另留档。

## 历史交付队列（F6，2026-09-07）

- [x] 真实共享输入/NR guidance/资源修复，Win32播放器、WIC图片、D3D12 NVENC视频、DirectShow输入实现。
- [x] 用户确认默认实时内部处理档，保留原生4K可选/原生4K导出。
- [x] 联合软件短测31.59秒通过，证据见DELIVERY_STATUS。
- [x] 新上下文只读Reviewer：3个P1修复复验后PASS，见REVIEW_F6。
- [x] 本机交付Veyra.cmd与文档；实卡交给用户，不等硬件接入、不假报实测。

以下原阶段队列保留为历史规格，不得绕过上述用户授权新顺序重跑长测。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

状态：`TODO / DOING / BLOCKED / GATE_PASS / REVIEW_PASS / DONE`。Maker 每个 cycle 只处理当前 Phase 第一个前置满足的 TODO。若某项因外部许可/硬件明确标为 BLOCKED，可继续同一 Phase 后面与它独立的 TODO；该 Phase 最终 gate 仍不得通过。

## Phase 0–4 — 已验证基础

- [x] Phase 0：D3D12 device、4-slot command ring、runtime identity/probe。checkpoint `1fb7afa5779a6c6a0a585d4ecf18a640260a2fcf`。
- [x] Phase 1：signed-snippet Feature 18 Create；300/300 Evaluate；真实非黑输出。checkpoint `1dcf834f56986d334a2a6256b196fa1d878ad9b1`。
- [x] Phase 2：parity encode/decode CPU/GPU golden 与完整 Feature 18 pipeline。checkpoint `e0250b5431ee6677a0917b97af821c5579e61d77`。
- [x] Phase 3：FFmpeg D3D12VA decode、YUV→linear、seek/endurance。checkpoint `2c9f2f19d818e7dd34fa6df4cf7d7c88ee12792a`。
- [x] Phase 4：DLSS SR 绝对路径修复、1080p→4K 30 Evaluate、resize。checkpoint `8cdf1ea2f9b0bb1331f9219a2305cef5efd3c8d8`。

这些 checkpoint 是基础证据，不代表 V1 三种模式完成。旧 Phase 5 Zero-only gate 证据因产品边界改变而失效。

## Phase 5 — 产品级质量核心修复（当前，2026-09-06 重开）

历史 `c4ee3f3` 及其日志只保留为组件证据。旧 Phase 5 gate 接受 manifest-only depth、第二次 1080p endurance 冒充 4K 位置证据，且 `EnhanceGraph` 只增加计数，因此旧 gate/Reviewer/checkpoint 不再满足当前 Product Spec。

- [x] **R0.0 对抗式状态审计**：确认 preflight 70/70、Release build exit 0；确认 Phase 0–4 有效；确认 Phase 5 假通过、Phase 6 未提交资产、约 31 个 worktree status entry、tracked 测试片删除、无 UI/Capture/Export；用户决定继续实验 Feature 18。（2026-09-06）
- [x] **R0.1 新 Maker 接管指纹**：重新运行 preflight/build/git status/diff-check；记录 `git diff --binary` hash；逐项分类当前未提交文件为 keep/repair/obsolete；不得 reset/clean/checkout；用新 run-id 重跑 NVOF/FG/audio/player 窄 probe。
- [x] **R1.1 phase5 gate 重建并先红**：重写 `scripts/gates/phase5.ps1`；移除 manifest-only depth 和假 4K；恢复 30 分钟门槛；要求产品 library、真实 extent/hash/timing/VRAM/queue/reset；当前实现必须因具体缺项 exit 1。
- [x] **R1.2 确定性 corpus**：用 `veyra_clip_gen` 在 ignored run 目录生成 1080p60/4K60 translation、occlusion、cut/flash/duplicate、particles、UI；记录生成命令/SHA256；gate 不再依赖已删除 tracked MP4。
- [x] **R2.1 FramePacket**：迁移到 `include/veyra/pipeline`；加入 rational/100ns PTS、duration、ColorDescription、FrameFlags、SourceKind、GPU resource state/owner fence/slot；兼容层只用于迁移期。
- [x] **R2.2 FrameWindow/GuidanceFrame**：固定 prev/current/next 与 lookaheadFrames 0/1/2，删除无界 vector；motion=current→previous working pixels、depth R32F、confidence R8、provenance/age/sourceSequence/reset reason。
- [x] **R2.3 ResetCoordinator tests**：open/seek/cut/drop/resize/source-switch/pause-resume/device-lost 八类事件同一 epoch；负/缺失 PTS、跨 epoch history、窗口上限和 fence ownership 测试 Debug/Release 通过。
<!-- 2026-09-06 Goal cycles 030-033: R0.1 takeover fingerprint 61feb89b + probes r0-*; R1.1 gate 42-check fail-closed proven red 34/42; R1.2 corpus 10 clips + SHA256 manifest (corpus:* green); R2.1-R2.3 delivered as one contract family include/veyra/pipeline + PipelineContractTests 50/50 debug+release (rational PTS, 8 reset reasons at frame boundaries, cross-epoch rejection, window cap, fence ownership). Checkpoints: bb9c5361/7ec1e0c/c7d6414/07eb66d. -->
- [x] **R3.1 CMake 产品库**：建立并实际链接 `veyra_pipeline`、`veyra_guidance`、`veyra_sources`、`veyra_sinks`；不允许空 target 或只含接口。
- [x] **R3.2 真 EnhanceGraph**：把 D3D12VA/YUV、SR、scene、NVOF/confidence、parity encode、Feature18、parity decode、optional FG、post mix 的真实提交从 `player_probe`/harness 迁入共享 graph；主路径 readback=0。
- [x] **R3.3 probe 去重**：`player_probe` 只做参数、组装、scenario、JSON；目标小于 800 行且不保留第二套 NGX/NVOF/barrier/resource lifetime。headless test 与 player probe 对 300 帧产生同计数/输出 hash。
- [ ] **R4.1 NVOF provider**：复用已存在 SDK 5.0.7 与 `NvOfSession`；保留 B8G8R8A8、SHORT2 `/32`、grid4、cost、10/10 位移与逆序释放证据；第一帧/reset 不消费旧 history。
- [ ] **R4.2 Guidance validator**：GPU cost+luma warp residual+out-of-frame/occlusion；Buffered/Export 增加 forward/back consistency；confidence 平滑衰减 motion；可视化与 P05/P50/P95。
- [ ] **R4.3 Depth 依赖核验**：核对 DAV2 Small FP16 与 ML runtime 的 URL/version/hash/license/names/shape/opset；manifest 只过身份检查，不能标 provider 完成。
- [ ] **R4.4 DepthAnythingProvider**：同 adapter、sequential、固定 shape、GPU binding；P02/P98+EMA、motion reprojection、age/residual；missing/late/stale/inconsistent 明确 Auto→Motion Only；render thread 无同步整帧 readback/upload。
- [ ] **R4.5 Scene/reset 集成**：现有 analyzer 接真实 histogram/SAD/PTS/sequence/NVOF confidence；硬切 reset、闪光不误切、duplicate/cross-cut history=0；所有 temporal backend 同 epoch。
- [ ] **R4.6 质量矩阵**：同一 corpus 输出 off/zero/motion/motion+depth/auto，保存 input/config/runtime/output hash、flow/depth/confidence/reset timeline、GPU p50/p95；不写未经证据的“优于 Magpie”。
- [ ] **R5.1 Phase 5 endurance/gate**：共享 graph 的 1080p60/native 4K60 各 30 分钟；NR/NVOF/depth/fallback 计数真实；queue/resource/memory/VRAM 有界；headroom≥1.5GiB；deviceRemoved=0；phase5 exit 0。
- [ ] **R5.2 Reviewer/checkpoint**：新上下文 Reviewer 独立重跑，修完 P0/P1 后再 gate+review；本地 checkpoint；STATE 才可 Phase5 passed/Phase6 unlocked。

## Phase 6 — DLSSG 2X 与实时 engine（锁定；已有资产待迁移）

- [x] **历史组件证据保留**：DLSSG capability/Create/Evaluate/Release；60 real→59 generated；non-duplicate/non-blend/midpoint/cut reset；WASAPI event probe；NVOF 真实 execute；Present/teardown s10 修复。它们不是 Phase gate。
- [ ] **R6.1 GBV id=938**：按 command list/root parameter/descriptor index 定位；所有 descriptor-table 槽写有效或显式 null descriptor；L2 runtime/teardown/final 0 ERROR/CORRUPTION，禁止过滤或关 GBV。
- [ ] **R6.2 Query-heap GPU timing**：upload/YUV/SR/NVOF input+execute+densify/confidence/parity/NR/decode/FG/composite/present 的 p50/p95；异步 resolve，不每帧等待。
- [ ] **R6.3 性能/资源池**：消除重复 4K blit/颜色转换/per-pass waits/临时纹理；4–6 command slots；resource pool/VRAM 峰值；teardown 保留 drainQueue 与 NVOF unregister→texture release→destroy/unload。
- [ ] **R6.4 A/V scheduler**：WASAPI audio master；seek 原子 flush/re-anchor/reset；Player 不丢真实源帧；1080p/4K drift P95≤50ms、underrun/overrun=0、late/source drop=0。
- [ ] **R6.5 bounded modes**：NR Low Latency=0、FG Low Latency=1、Buffered Quality=2；Capture mailbox=1、history≤3、Player backpressure；lookahead 与内部延迟分项入 JSON。
- [ ] **R6.6 product FG truth**：共享 engine 再证明 generated 非邻帧/非 blend、PTS 中点、cut/duplicate/drop 不跨界，60→59。
- [ ] **R6.7 Player scenarios**：真实 1080p→4K 和 native 4K H.264/HEVC+audio；play/pause/10 seek/resize/loop；NR/SR/FG toggle；0 readback；L0/L1/L2/no-feature 全部自然 return。
- [ ] **R6.8 endurance/gate**：4K30/60 各 30 分钟；4K60 NR 不积压；FG 内部时间线≥110Hz；队列≤4、WS growth<512MiB、VRAM headroom；phase6 exit 0。
- [ ] **R6.9 Reviewer/checkpoint**：独立 Reviewer、修 P0/P1、重新 gate、本地 checkpoint，才解锁 Phase7。

## Phase 7 — 三个首发产品闭环（锁定）

- [ ] **R7.1 app/controller**：建立 `apps/veyra` Win32 程序、单 render engine、UI-safe command queue、统一 lifecycle；缺 runtime 时设置/诊断可打开但功能 fail closed。
- [ ] **R7.2 Player tab**：MediaFileSource、Present、WASAPI、open/play/pause/seek/loop/fullscreen、NR/SR/FG/quality、外置 SRT+首条内嵌文本字幕（FG 后合成）、实时诊断。
- [ ] **R8.1 CaptureCardSource**：FFmpeg avdevice dshow + DirectShow friendly-name/media-type 枚举；回报实际 codec/color/size/fps；1080p/2160p 30/60 SDR video+audio；HDR fail closed；mailbox=1、drop reset。
- [ ] **R8.2 Capture tab**：设备/格式/音频/输出、三延迟模式、NR/SR/FG、start/stop/fullscreen、lookahead/drop/latency/fallback 可见。
- [ ] **R8.3 真实硬件 gate** BLOCKED until device present：真实 4K60 SDR+HDMI audio 30 分钟；窗口/虚拟摄像头不能替代。
- [ ] **R9.1 ImageSource/Sink**：WIC PNG/JPEG、EXIF orientation、sRGB、single-frame reset、optional SR+NR、partial+decode-back+atomic rename、不覆盖。
- [ ] **R10.1 Video Codec SDK dependency** BLOCKED：用户正式取得 SDK 13.1 到 `third_party_local/nvidia/Video_Codec_SDK_13.1.0`；核对 header/sample/version/hash/EULA。
- [ ] **R10.2 NvencD3D12Encoder**：系统 nvEncodeAPI64.dll、H.264/HEVC/4K capability、4–8 slot、D3D12 NV12 resource registration、input/output fences、只回 CPU 压缩 bitstream。
- [ ] **R10.3 VideoExportSink**：共享 graph、MP4/MKV CFR、optional FG2X、audio remux/AAC、字幕 policy、cancel/crash partial recovery、ffprobe+self-decode；H.264/HEVC 均过。
- [ ] **R11.1 Export tab**：图片/视频 input/output/size/FPS/codec/quality/start/cancel/progress/error，真实调用 R9/R10。
- [ ] **R11.2 产品恢复**：首次依赖检查、设置与最近文件/设备、source reconnect、device lost、残留 job/partial 提示、日志包导出且排除 runtime/SDK/媒体。
- [ ] **R11.3 错误/fallback UI**：Unsupported/MissingAsset/InvalidMedia/RuntimeFailure/DeviceLost/Cancelled；software decode、Zero、depth off、FG unavailable 全部明示。
- [ ] **R12.1 phase7 联合 gate 先红再绿**：4K30/60 Player、真实 4K60 Capture、PNG/JPEG、4K H.264/HEVC Export、UI/恢复五组缺一即 fail；所有 evidence 绑定本次 hash/run-id。
- [ ] **R12.2 Magpie 同源 A/B**：至少五类同机同输入同输出；只记录场景限定结论。
- [ ] **R12.3 许可证/分发审计**：Git/候选包无 proprietary/local assets；THIRD_PARTY_NOTICES；外置 runtime 流程不宣称解决授权；未清零即 distribution_blocked。
- [ ] **R12.4 Reviewer/final checkpoint**：联合 gate+独立 Reviewer，无 P0/P1；本地功能 checkpoint；不得自动 push/打包/发布。

## 不能移出 V1 的门槛

- 真实物理采集设备，不是窗口/桌面 capture；
- 真正可交互播放器，不是只解码 CLI；
- 图片和视频两种导出，不是截图保存；
- Feature 18 真实 Evaluate；
- NVOF/estimated depth 的真实身份与 fallback；
- DLSSG generated frame 真实性；
- 音频、时序、reset、延迟、内存与许可证证据。
- native 4K Player/Capture/Export，不用 1080p harness、静态图片或虚拟摄像头冒充；
- D3D12 NVENC H.264/HEVC，不用 raw-frame pipe 冒充首发导出；
- 首次运行、设置持久化、设备恢复、partial job recovery 和日志导出。
