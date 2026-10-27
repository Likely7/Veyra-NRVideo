# NR / 补帧优化施工方案（2026-09-29）

调研依据：`DLSSNR_UPDATE_RESEARCH_PLAN_2026-09-29.md`（上游逐项对比）。本文件是执行方案；开工前需要用户确认。

## 0. 用户决定与范围

| 项 | 决定 |
| --- | --- |
| 重复帧跳过 NR | **不做**。NeuralScreen 做过又删除了（`a0371ca5`：没人测出收益） |
| 降低 NR 分辨率 | 已有（每层六档），不列入 |
| Lecram（310.8.Lecram，50 系） | **要测**。文件由用户下载提供（见 §6） |
| 独立 Present 队列 | 做，前提是先测出确实有等待 |
| Magpie 三档抗闪烁 | 做，排在输出稳定器之后（阶段 6） |
| NeuralScreen 其余软件优化 | 并入阶段 4 逐条对照 |
| SR/FG 升 310.9.1 | 做，单独立项（阶段 7） |
| DLSSG-Transfusion 公开部分 | 做，依赖阶段 7（阶段 8） |
| SF-v2 | 有条件做：需要用户提供文件，且需要 30/40 系实机（阶段 9） |

约束沿用：
- 在隔离 worktree 施工；不改原 checkout、main、1.4.4、用户配置和共享的 remoteplay 目录。
- 单次测试不超过 300 秒，构建不超过 900 秒；同一原因最多修两次，不伪造结果。
- 不连接用户的 PS5；采集卡被 1.4.4 占用时不打开也不查询；测试静音。
- 不提交、不发布；Runtime Pack 变更须另行授权。

## 1. 阶段 1：基线测量（不改代码）

目的：给后面每一项提供"改之前"的数据，没有收益的项直接撤掉。

- 机器：本机 RTX 5070。片段用 p001.mp4，1080p 实时档。依次测以下配置：
  - (a) 只开 NR 单层；
  - (b) NR + DLSS FG 2x；
  - (c) NR + XeSS FG 2x；
  - (d) NR 两层。
- 指标：
  - 每次 NR Evaluate 的 GPU 时间 P50/P95（沿用现有 gpuTimer 日志）；
  - 整卡功耗和 GPU 占用（沿用 `compare-releases` 的 nvidia-smi 采样和裁剪方法）；
  - 源帧率、预览跳帧数；
  - Present 等待：每个真实帧从 `presentationReadyFence` 满足到 Present 调用之间的时长。现有日志不够的话，只加诊断日志，不改行为。
- 每个配置跑 30 秒，AB-BA 交替两轮，排除温度和频率漂移。
- 产出：`tests/nr-fg-opt-20260929/baseline/`，以及一张汇总表。

## 2. 阶段 2：Lecram A/B（等用户提供文件）

- 2.1 身份登记：记录 zip 和 DLL 的 SHA-256、大小、文件版本、签名状态（预期为未签名或 HashMismatch）。
  - zip 必须与 GitHub 公布的 digest 一致，**不一致立即停止**。
  - 解压到 `runtime_local/nvidia/nr-lecram/`（gitignore，不覆盖原版和社区版）。
- 2.2 **先不改代码**做对比：复制一份测试候选包，把 Lecram 放进候选包自己的 `nr-community/` 目录，用 `--nr-community` 与原版（`--nr-original`）、现社区版（984BEE0F）三方对比。只动这份测试副本。
  - 测量方法与阶段 1 相同，另加同帧画面对比：固定暂停帧截图，算差值图和 PSNR。
  - 稳定性：Create/Evaluate 结果码、300 次 Evaluate 不出错、改尺寸 / 开关 NR / 切层后能恢复。
- 2.3 判定：
  - Lecram 的 GPU 时间或功耗优于原版 **5% 以上**，画面没有明显劣化、没有失败，才进入 2.4。
  - 否则只在 WORKLOG 记下数据，不接入。
- 2.4 接入（通过判定后才做）：
  - `NrRuntime` 枚举新增 `Lecram`，名称为"community-RTX50-Lecram"，目录为 `nr-lecram`。
  - 同步修改：`EnhanceGraph.cpp:1115`、`DlssNrRuntimeAdapter.cpp:412` 的允许路径、`EnhancementSettings.h` 的两处校验、预设存储、QML 运行版本下拉框、LiveStatusPanel 文案、`stage-runtime.ps1`（可选存在）、portable-smoke 用例。
  - UI 标注"社区实验版 · 仅 RTX 50"；非 50 系卡上该项不可选并给出原因。
  - **默认值不变**，仍为 NVIDIA 原版。
  - 旧配置里没有这一项，迁移时保持原值。

## 3. 阶段 3：独立 Present 队列扩展

现状（`src/engine/VideoPresenter.cpp:28-37`）：只有 **DLSS FG** 路径（`fgEnabled && !xess && !fsr`）创建独立的 `presentationQueue_`，并用生产者 / 消费者 fence 衔接。**只开 NR（不开 FG）、XeSS FG、FSR FG** 这三条路径仍在主 direct queue 上 Present，真实帧会排在正在执行的 NR 后面。

- 3.1 进入条件：阶段 1 测出纯 NR 或 XeSS 路径的 Present 等待 P95 ≥ 2ms。达不到就不改，只记录数据。
- 3.2 先改纯 NR 路径：
  - 复用 DLSS FG 已有的 `presentationQueue_`、`presentationRing_` 和 fence 合同，把判定条件扩展到"不开 FG"；
  - 共享纹理沿用 `presentationReadyFence` 和 `presentationSubmitted` 的等待链；
  - 不新增逐帧 CPU 等待。
- 3.3 XeSS / FSR：
  - 这两家的 provider 自己持有 queue，接口要求 swapchain 与生成帧的 queue 一致，风险较高；
  - 只有纯 NR 路径取得收益后，才单独评估是否可行；**不强行改**。
- 3.4 验收：
  - Present 等待 P95 下降，源帧率不降；
  - 帧顺序正确，现有 FG 调度 / 恢复回归通过；
  - 开关 NR、改尺寸、暂停恢复、切换源这些路径都没有黑帧、没有冻结；
  - D3D12 debug layer 零错误。

## 4. 阶段 4：缺陷核对（参考其他项目刚修过的同类问题）

| 项 | 来源 | 现状核对 | 动作 |
| --- | --- | --- | --- |
| 架构伪装范围 | sdli 0.3.4、video2dlssnr 1.4.1（伪装传给 SR 导致 30 系崩溃） | 已读代码：NR 伪装只作用于所选 NR DLL 的导入表（`DlssNrRuntimeAdapter.cpp:131`）；FG 伪装只在 `VEYRA_TEST_NVAPI_SPOOF_ARCH` 诊断模式下安装（`EnhanceGraph.cpp:885-899`），正常 30 系路径保留真实架构 | 补一个单元断言："正常模式下 FG 不安装伪装；NR 伪装不改 SR 模块的导入表"。结论写入 WORKLOG |
| FG 重建后内核槽 | sdli 0.3.5（重建后用错优化内核） | AmpereMfgUnlock 在进程内存里改 dlssg 的 fatbin 槽，按模块生命周期存在 | 核对：FG feature 销毁并重建时（改倍率、改尺寸、恢复）补丁是否保持一致、会不会重复安装、release 是否只执行一次。可以在 5070 上做机制测试（不走 30 系路径时应为 no-op）；真实效果需要 30 系实机 |
| 采集暂停后必须真的 reset | NeuralScreen `5a8f94c4`（标志位被提前清掉，重置没发生） | 需读 `EngineController.cpp` 的 capture stall / pause 路径 | 加断言测试：采集暂停再恢复后，NR 时域历史和 FG 槽确实被 reset，日志里的 reset 与实际动作一一对应 |
| 不用的 NR 层要释放 | NeuralScreen `25180371`、OptiScaler v0.8.5（泄漏 3.6GB） | 多层 NR 已有删层和改尺寸功能 | 回归测试：4 层 → 1 层、改尺寸 12 次，进程显存（DXGI QueryVideoMemoryInfo）回到基线 ±100MB |
| NR 关闭时管线暂停 | NeuralScreen `a0be63f9` | Veyra 关闭 NR 时不创建 NR feature | 只核对：NR 关闭时没有 NR 相关的 dispatch 和显存残留。已具备就记为"已具备" |
| 场景切换在 GPU 上判定 | NeuralScreen `6c8657b6`（省掉每帧一次 capture 往返，NR +3.3%） | 需核对 Veyra 的 scene-cut 判定是否有 CPU 往返或回读 | 有往返就改为 GPU 判定，结果留在 GPU 或延后一帧读 4 字节；没有就记为已具备 |
| 合并提交 | NeuralScreen `67483252`（灰度处理并进 capture 的 command list，只提交一次、等一次） | 需核对采集到图的颜色转换是否单独提交 | 统计每帧 ExecuteCommandLists 和 fence 等待次数，能合并的合并 |
| 每帧大块分配、无用计算 | NeuralScreen `facdd182`（每帧分配 33MB；计算了没人读的光流） | 需核对 Veyra 每帧有没有临时分配，光流在不需要时是否仍在计算 | 用阶段 1 的日志统计每帧分配；光流只在 NR 时域 / FG / 保护区需要时才算 |

## 5. 阶段 5：输出稳定器（防闪烁，默认关闭）

- 参考：DLSS5-Feeder 1.17/1.18 的 Output stabiliser（只借鉴公开描述的行为；许可证核对后如需引用代码，在 notices 里归因）。
- 算法（独立实现）：
  - 每个输出像素计算源图 3×3 亮度 box，与"锚点"（该像素上次判定为变化时的 box）比较相对差；
  - 差值小于容差，视为输入未变：`out = lerp(nr, prevOut, holdStrength)`；
  - 差值大于容差：直接用 `nr` 并更新锚点；
  - 不做重投影。
- 实现：新增 `shaders/NrHold.hlsl` 和 `src/pipeline/NrHoldPass.cpp`，放在最后一层 NR（及 residual composite）之后、SR/FG 之前。每层独立的版本以后再说。
  - 资源：前一帧输出和锚点两张纹理，尺寸与工作尺寸一致。
  - 遇到 seek、场景切换、PTS 断点、改尺寸、切源、暂停恢复时，与现有历史一起 reset。
- 设置：`nrHoldStrength`（0–1，0 表示关闭，默认 0）、`nrHoldTolerance`。进入 settings、预设存储（新增版本号，旧配置默认 0）和 ProPage 的 NR 高级区。
- 验收：
  - 合成测试：静止图加微小逐帧扰动，输出逐帧差值显著下降；运动块不拖影（边缘误差不超过关闭时）；
  - 真实片段：GPU 开销 1080p < 0.3ms；
  - D3D12 零错误；强度为 0 时输出与关闭时逐位一致。

## 5A. 阶段 6：Magpie 抗闪烁三档

- 来源：SAOG0721/Magpie `3841698`（GPLv3）：`DLSSNRTemporalShader.h`、`DLSSNRTemporal.cpp`、`include/DLSSNRTemporalState.h`、`DLSSNR_AI_Filter.hlsl`、`tests/DLSSNRTemporalTests.cpp`。逐文件在 THIRD_PARTY_NOTICES 标注来源、提交和改动。
- 在现有"光流累积"基础上增加三档：
  - **静态累积**：只平滑静止区域；
  - **光流累积+**：60ms 起效、180ms 释放，拒绝方向相反的修正；
  - **低频时域重建**：半分辨率残差做时域滤波，保留当前帧高频。
- 抗闪烁改为互斥选项：无 / 静态 / 光流 / 光流+ / 低频。与阶段 5 的输出稳定器可以同时开，顺序为"时域在前、稳定器在后"。
- 顺带修掉之前审计发现的两个问题：
  - `NrTemporal.hlsl` 的零修正门会绕过历史，要把"没有修正"和"保护区"分成两个 mask；
  - `NrTemporalPass` 只把 frameMs 夹到 1..250，时间戳断点要独立判定并 reset。
- 移植上游的测试用例：有符号残差、开关方差、运动 / reset / 拒绝、HDR 数值有限、细节保持、奇数尺寸。改为在 D3D12 上真实执行。
- 性能：沿用 10×10 halo 优化，重跑之前 P95 lateness 46ms 的那组测试，每档都不得明显退化（lateness P95 < 5ms），否则该档不上线。

## 5B. 阶段 7：SR/FG 升级到 310.9.1

- 文件：NVIDIA 官方 DLSS SDK 310.9.1（github.com/NVIDIA/DLSS，官方签名）。由用户下载放到 `third_party_local/nvidia/DLSS_SDK_310.9.1/`，按 AGENTS 规则登记身份。
- 改动：
  - `stage-runtime.ps1` 的固定身份，包括 dlssg 的 pinned SHA；
  - 我们 40 系补帧补丁（AdaMfgUnlock：架构比较、中点 PTX、计数门）和 30 系补丁（AmpereMfgUnlock：69 fatbin / 200 指针槽 / gate）都绑定 310.7 的字节身份，必须按 310.9.1 重新定位；对照 dashdogy v1.3.3（已支持 310.9.1）和 DLSSG-Transfusion 的 pattern；
  - NvapiArchSpoof 的 wrapper RVA（310.7 为 0x1670）同样要重定位。
- 身份不匹配时拒绝安装补丁、退回 2x，不猜。
- 验收：
  - 5070：SR/FG 原生路径回归（FG 2x–6x、恢复、切换后端），画质和帧率不低于 310.7；
  - 40/30 系补丁：只能在 5070 上做结构校验（pattern 命中数、PTX 重建哈希、回滚），真实效果需要 40/30 系实机；
  - 310.7 保留为可回退选项，直到实机验收通过。

## 5C. 阶段 8：DLSSG-Transfusion 公开部分（依赖阶段 7）

- 来源：SilyNoMeta/DLSSG-Transfusion（MIT，基于 TonyJoaca v1.4.5）。私有 PTX 不公开，不用。
- 可移植的部分：
  - `Kernel_BlendCandidatesFused` 合并写入（`st.global.v2.u32`），逐位一致，30 系 1080p 约 -4%；
  - valid-warp 保护策略；
  - UI assist：生成帧上的 HUD、半透明面板重组。这对采集画面里的游戏 HUD 有意义，但 Veyra 没有游戏引擎提供的 UI 信息，需要先评估它在视频 / 采集输入上是否适用。
- "Blackwell 内核移植到 89/86"是它的核心，与我们现有的补丁方式不同。先比较两者在 40/30 系上的画质和耗时（需要实机），不直接替换。
- 验收：逐位一致性（与不开优化相比）、5070 上的机制测试；收益数据需要 30/40 系实机。

## 5D. 阶段 9：SF-v2（有条件）

- 需要用户下载 `nvngx_dlssnr_310.8.SF-v2.zip`（rhi-repo，Wan2GP 记录的 zip SHA-256 为 `1DA35941894994EB087E017577829E492454E9BAE3A6A9397027069CEB74955C`）。
- 5070 上只能测"能加载、不出错、50 系上与原版差异"；它的价值在 30/40 系，需要有对应卡的人实测，才能决定是否替换现在的 30 系版本（DCC0）和 40 系版本（984BEE0F）。
- 接入方式与 Lecram 相同：独立目录，新增运行版本选项，不改默认。

## 6. Lecram 文件（请用户自行下载）

- 地址：https://github.com/RankFTW/rhi-repo/releases/download/dlssnr-310.8.Lecram/nvngx_dlssnr_310.8.Lecram.zip
- 大小：116,658,260 字节
- GitHub 公布的 SHA-256：`ddb64e5545ba3f217c32555d2eb8fdfe76b9510d2256f13e8a5ecb1a9650ca54`
- 这是社区修改、未签名的 NVIDIA 运行库，仅供本机研发测试。下载后告诉我路径即可，我先校验哈希再解压。

## 7. 顺序与交付
1. 阶段 1 基线；
2. 阶段 4 缺陷核对（与 1 并行，不占 GPU）；
3. 阶段 2 Lecram（文件到位后）；
4. 阶段 3 独立 Present 队列（取决于阶段 1 的数据）；
5. 阶段 5 输出稳定器；
6. 阶段 6 Magpie 抗闪烁三档；
7. 阶段 7 SR/FG 310.9.1（SDK 文件到位后）；
8. 阶段 8 DLSSG-Transfusion 公开部分；
9. 阶段 9 SF-v2（文件和 30/40 系实机都具备后）。

每阶段结束更新 WORKLOG 和本文件的结果区，报告候选包路径与 EXE SHA，列出未测项（30/40 系实机、采集卡、PS5）。

## 8. 结果区

### 阶段 1 基线（2026-09-29，case 341/342）

测试条件：
- RTX 5070；当前源码编译的旧 Win32 smoke 程序（与 QML 界面同一引擎），Veyra.exe SHA `a5e6340b…dc8a`；
- p001 去音轨版，4K60 H.264 输入，`--realtime` 1080p 内部处理，不开 SR；
- 两轮顺序为 ABCD / DCBA；每项取第 6 秒以后每秒 P95 的中位数，功耗取稳定段中位数。

| 配置 | NR P95 ms | 光流 ms | FG 批次 ms | GPU 完成 P95 ms | 功耗 W | 占用 % | 帧率 | 生成帧 | 迟到 P95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A 原版 NR（r2） | 6.41 | 1.20 | — | 10.26 | 141.7 | 45 | 61 | 0 | 0.76 |
| A 原版 NR（r1，首次启动） | 7.03 | 1.31 | — | 10.71 | 149.9 | 52 | 60 | 0 | 0.83 |
| B 原版 + DLSS FG 2x | 6.58–7.15 | 1.21–1.33 | 2.84–3.06 | 12.6–13.4 | 175.4–180.7 | 62–69 | 60 | 1799–1804 | 0.75–0.86 |
| C 原版 + XeSS FG 2x | 6.44–6.51 | 1.27–1.29 | —（provider 内部） | 11.3–11.4 | 171.3–172.5 | 61–62 | 60 | 1790 | 1.27–1.30 |
| D 社区版 NR | 6.38–6.40 | 1.18 | — | 10.24–10.25 | 140.7–141.5 | 44–45 | 60 | 0 | 0.74–0.77 |

结论：
- **社区版（984BEE0F）与原版在 5070 上一样**：NR 耗时 6.4ms、功耗约 141W，差异在噪声以内。
- 1080p 下 NR 每帧约 6.4ms，占 60fps 帧预算（16.7ms）的约 38%。DLSS FG 2x 额外增加约 3ms GPU、约 35W；XeSS FG 2x 约 +30W。
- **每次启动的第一次运行偏高**：A 在 r1 比 r2 多 0.6ms、多 8W，B 在 r1 也偏高。所以 Lecram 对比要先跑一次预热并丢弃，每个版本至少三轮交替，5% 的判定阈值（约 0.3ms）才可信。
- **Present 等待**：文件播放下各配置迟到 P95 都在 0.74–1.3ms，没有看到 NeuralScreen 那种约 13ms 的排队。现有日志里没有"Present 前等待"的直接指标，阶段 3 要先加这个诊断，并在实时采集路径上测（采集卡空出来之后）。按目前的数据，阶段 3 的进入条件（≥2ms）在文件播放下不满足。
- 未测：两层 NR（smoke 没有命令行入口）、采集卡 / PS5 实时路径。

### 阶段 1 基线：原生 4K 加压（用户指定，case 343/344）

用户指出 1080p 没有压力，改用 `GTAVI_An_Extended_Look_4K_Native.mp4`：
- 截取第 60–240 秒，视频流原样复制并去掉音轨，4K30 H.264，约 70.6Mbps；
- 使用 `--native`（NR 在完整 4K 上运行）；
- 两轮顺序为 A(预热丢弃)ABCD / A(预热丢弃)DCBA。

| 配置 | NR P95 ms | 光流 ms | FG 批次 ms | GPU 完成 P95 ms | 功耗 W | 占用 % | 帧率 | 生成帧 | 迟到 P95 ms | 显存 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A 原版 NR | 23.16–23.25 | 3.36 | — | 29.0 | 219.5–220.7 | 73–74 | 30 | 0 | 0.79–0.85 | 约 3800 |
| B 原版 + DLSS FG 2x | 23.42–23.48 | 3.36–3.40 | 2.88–2.91 | 32.3–32.4 | 235.7–236.8 | 83 | 30 | 900 | 0.81–0.82 | 约 4670 |
| C 原版 + XeSS FG 2x | 23.42–24.75 | 3.47 | —（provider 内部） | 30.5–31.8 | 233.9–235.5 | 82–87 | 30 | 890 | 1.40–1.47 | 约 4400 |
| D 社区版 NR | 23.21–24.22 | 3.37 | — | 29.0–29.9 | 220.2–222.0 | 74–77 | 30 | 0 | 0.82–0.84 | 约 3850 |

结论：
- 原生 4K NR 每帧约 23.2ms，是 1080p 的 3.6 倍，占 30fps 预算（33.3ms）的 70%。开 DLSS FG 2x 后 GPU 完成 P95 达 32.4ms，**离 33.3ms 上限只剩约 1ms**。如果换成 4K60 源，或者再加一层 NR，必然掉帧。
- 社区版与原版依旧没有差异。
- 所有配置都没有预览跳帧、没有过期的生成帧，迟到 P95 ≤1.5ms。文件播放下仍看不到 Present 排队。
- **单次运行的波动约 1ms**（C 为 23.4 与 24.8，D 为 23.2 与 24.2），约 4–5%。所以 Lecram 的对比至少要三轮交替，并去掉预热运行，否则 5% 的差异分不出来。**Lecram 对比统一用这套原生 4K 条件。**

### 阶段 2 Lecram A/B（2026-09-29，case 345–349）

**文件身份**（用户下载到桌面"更新文件"，复制到 `E:/项目/Veyra/downloads/nr-fg-opt-20260929/`，桌面原件未动）：

| 文件 | 核对 |
| --- | --- |
| `nvngx_dlssnr_310.8.Lecram.zip` | SHA `ddb64e55…ca54`，与 GitHub digest 一致 |
| Lecram 里的 `nvngx_dlssnr.dll` | 165,840,496 字节，**文件版本 310.8.3.0**，SHA `F95FEB54…9FCC`，签名 HashMismatch（NVIDIA 证书，内容已被修改） |
| `nvngx_dlssnr_310.8.SF-v2.zip` | SHA `1da35941…955c`，与 GitHub digest 一致 |
| SF-v2 里的 `nvngx_dlssnr.dll` | 165,830,144 字节，版本 310.8.SF.0，SHA `6EB209E7…3927`（与 Wan2GP 文档记录一致），NotSigned |
| `DLSS-310.9.1.zip` | SHA `c2ada58c…a062`（GitHub 对源码包不公布 digest） |
| SDK 里的 rel `nvngx_dlss.dll` / `nvngx_dlssg.dll` | 310.9.1.0，NVIDIA 签名 Valid；dlssg 的 SHA `FF6E90EB…0B82`，与 NeuralScreen v2.1.8 清单一致 |

**与原版的字节差异**：Lecram 改了 `.data` 区段 16,238,717 字节（该区段存放 GPU 内核）和 `.text` 区段 25 字节；模型权重所在的 `.rsrc` 区段只差 4 字节。社区版 984BEE0F 的改法相同（`.data` 13.5M 字节）。

**性能**（原生 4K、GTA6 4K30、只开 NR；三轮交替 AALD / ALDA / ADAL，第 1 轮首项作为预热丢弃；日志确认 L 从 `app-lecram/.../nr-community` 加载）：

| 版本 | 次数 | NR P95 ms（逐次） | 平均 | GPU 完成 ms | 功耗 W（逐次） | 平均 W | 占用 % |
| --- | ---: | --- | ---: | ---: | --- | ---: | ---: |
| A 原版 | 5 | 24.83 / 24.28 / 23.95 / 24.41 / 23.46 | 24.18 | 29.75 | 220.7 / 220.3 / 221.5 / 221.2 / 221.5 | 221.0 | 73.8 |
| L Lecram | 3 | 23.92 / 22.99 / 23.95 | **23.62（-2.3%）** | 29.02 | 216.2 / 215.8 / 217.1 | **216.3（-4.7W，-2.1%）** | 71.2 |
| D 社区版 | 3 | 24.40 / 24.28 / 24.24 | 24.31 | 29.73 | 220.8 / 220.9 / 222.5 | 221.4 | 74.3 |

- 功耗：Lecram 三次全部低于原版的五次，方向稳定。耗时的区间与原版有重叠，均值差 0.56ms。
- 全部运行 exit 0、failed=false、30fps、无跳帧、无 ERROR；显存与原版相同（约 3.9GB）。

**画面**（`lecram_image.py`，case 349）：
- 同一个离线 NR 探针、同一段 1080p GTA6 帧（第 2 秒起 24 帧，关闭时域），只替换 NR DLL。
- 原版与 Lecram 的 base / raw / filtered 三路输出**逐字节相同**（文件哈希都是 `a9e271e0…`）。
- 对照：NR 输出相对输入确有变化（平均差 0.0044，27.8% 的像素变化超过 2/255），说明比较是有效的。
- D3D12 debug layer 零错误。

**判定**：Lecram 是**画面逐位一致的纯内核优化**，在 5070 原生 4K 下约快 2.3%、省约 4.7W。**没有达到 §2.3 的 5% 门槛**，按方案不自动接入，交给用户决定。

未测：Lecram + FG 的组合、1080p 实时档、长时间稳定性（本次共约 3×30 秒加 24 帧离线）。

### 阶段 2.4 接入：Lecram 替换默认位置（用户："直接接入，替换原有文件"，case 350–359）

- **做法**：Lecram 直接占用默认位置 `runtime/experimental/nvngx_dlssnr.dll`（原"NVIDIA 原版"），不新增枚举项。
  - 50 系画面逐位一致，只是更快；30/40 系本来就走社区版 / Ampere 版位置，不受影响。
  - NVIDIA 原版 E16BCF15 仍保留在 1.4.4 发布包和下载目录，作为回退。
- **改动**：
  - `scripts/stage-runtime.ps1`：来源改为 gitignore 的 `third_party_local/nvidia/dlssnr-310.8.Lecram/`，固定身份为 F95FEB54 / 310.8.3.0 / HashMismatch，清单同步。
  - `scripts/package-portable.ps1`：默认 NR 条目改为同一身份。
  - `scripts/stage-ui-migration.ps1`：新增 `-NrDefaultRuntime`（校验身份；复制 runtime 而不是链接；改写清单条目；版本写成数字格式）。
  - 旧 Win32 文案：`SettingsWindow.cpp`（下拉框改为"默认 · Lecram 优化 · RTX 50"）、`LiveStatusPanel.h`、`SettingHelp.h`。
  - `QmlPlayerBridge::componentList`：兼容清单里的 `fileVersion` / `authenticode` 字段，并显示所在目录。修复了组件页版本和签名一直为空的问题。
  - AGENTS.md（顶部决定和二进制身份段）、THIRD_PARTY_NOTICES.md。
  - 范围基线 j：`scope-baseline-j.json`，SHA `11fa7ab3…a9bc`。
  - 内部日志 id 仍为 `NVIDIA-original`，因为验收脚本依赖它；日志同时打印了实际路径。
- **验证**：
  - 350 构建 exit 0。
  - 353 `stage-runtime.ps1` 装好 Lecram 并写出清单（351/352 失败：一次是 bash 传中文路径乱码，一次是我写坏了源路径的反斜杠，已修正）。
  - 354 用新参数组装新界面候选包 `tests/nr-fg-opt-20260929/candidate-lecram`（EXE `3fe153bf…d7ad`）；1.4.4 发布包的原版 DLL 仍是 E16BCF15，未改动。
  - 356 五页加载检查 0 错误。
  - 357 在运行中的新界面读取组件列表：默认 NR 显示"310.8.3.0 · HashMismatch · runtime/experimental"，三个 NR 文件按目录区分（CL_PASS）。
  - 359 重新编译的 smoke 程序在默认位置加载 Lecram（日志路径确认），原生 4K GTA6：

| 配置 | NR P95 ms | GPU 完成 P95 ms | 功耗 W | 帧率 / 生成帧 |
| --- | ---: | ---: | ---: | --- |
| 只开 NR（两次） | 22.54 / 22.53 | 28.55 / 28.48 | 217.0 / 216.3 | 30 / 0 |
| NR + DLSS FG 2x | 22.81 | **31.04**（原版基线 32.3–32.4） | **231.8**（原版 235.7–236.8） | 30 / 898 |

  开补帧时的余量从约 1ms 增加到约 2.3ms。本次运行的温度和时段与基线不同，这组数字只作接入后的功能确认，不作精确的收益结论。

### 用户决定（2026-09-29）：SR/FG 不升级；RTX30 位置换 SF-v2（case 360–369）

- **阶段 7（SR/FG 升 310.9.1）取消**：避免 30/40 系补帧补丁失效。SDK 文件保留在 `third_party_local/nvidia/DLSS_SDK_310.9.1/`，未接入。阶段 8（DLSSG-Transfusion）依赖阶段 7，一并搁置。
- **字节比较（SF-v2 与 NeuralScreen DCC0）**：
  - DCC0 只改了内核（`.data`）和 19 字节 `.text`，`.rsrc` 模型区与原版完全相同；
  - SF-v2 改了 `.data`、25 字节 `.text`，以及 `.rsrc` 中约 1.42 亿字节；
  - SF-v2 与 Lecram 的 `.text` 完全相同，出自同一条改版线；
  - 版本号：SF-v2 的字符串为 "310.8.SF.0"，数字为 310.8.2.0。
- **兼容层**：原来的 RTX30 兼容层把 Ampere 改报为 Blackwell（0x1B0），这是为 DCC0 设计的（它把 Ampere 能跑的内核放在 Blackwell 的位置）。SF-v2 自己放开了显卡检查，并按真实架构选择 20/30 的 FP16 路线，继续改报会让它选到 Ampere 跑不了的内核。
  - 新增 `nrRuntimeNeedsAmpereRewrite`：310.8.2 及以上不改写，310.8.0 / 310.8.1 保持原行为，未知新版本默认改写；
  - `installAmpereCompatibility` 从已加载 DLL 自身的 VS_FIXEDFILEINFO 读取版本（不新增链接库），并记录日志。
- **改动文件**：`NrArchitecturePolicy.h`、`DlssNrRuntimeAdapter.cpp`、`RepairPresetTests.cpp`（5 条版本断言）、`package-portable.ps1`（2.0.0 起 nr-ampere 条目改为 SF-v2 身份）、`stage-ui-migration.ps1`（新增 `-NrAmpereRuntime`，两个位置共用身份校验）、旧 Win32 三处文案、AGENTS、notices。范围基线 k（`4a867a35…`）。
- **验证**：
  - 360/365 构建 exit 0；361 单元测试 exit 0。
  - 363 在 5070 上以 `--nr-ampere` 运行 SF-v2：日志为 "runtime version 310.8.2.0" 和 "architecture rewrite not installed"，原生 4K 下 NR 23.5–24.6ms、30fps、failed=false。
  - 364 同帧离线对比：**SF-v2 在 5070 上的 NR 输出与 NVIDIA 原版逐字节相同**，debugErrors=0。所以 `.rsrc` 的改动只作用于 20/30 系的 FP16 路线。
  - 366 候选包 `tests/nr-fg-opt-20260929/candidate-nr-runtimes`（EXE `fad030d7…`，默认位置 Lecram、30 系位置 SF-v2）；368 加载检查 0 错误；369 组件页显示两个替换（CL_PASS）。
  - 1.4.4 发布包的 nr-ampere 仍为 DCC0DC24。
- **未验证**：RTX 30 实卡上 SF-v2 是否能建立 NR、性能和画面如何。本机没有 30 系卡。

## 2026-09-30 用户决定：补帧运行库升到 310.9.1，改用 DLSSG-Transfusion 补丁

- **背景**：2026-09-29 曾决定不升级 310.9.1（怕 30/40 补帧补丁失效）。用户看到 Transfusion 的优化内核后改为升级，
  并接受这批内核“两位作者都未授权”的风险（“我们现在也是灰色地带……可以拿过来用”）。
- **只换补帧 DLL**：SR (`nvngx_dlss.dll`) 仍固定 310.7；`nvngx_dlssg.dll` 换成官方 310.9.1
  （`FF6E90EB…170B82`，7,460,976 bytes，Authenticode Valid/NVIDIA，来源 `third_party_local/nvidia/DLSS_SDK_310.9.1`）。
  310.7 的补帧运行库保留，可回退。
- **补帧补丁换源**：310.7 的 `AdaMfgUnlock` / `AmpereMfgUnlock`（dashdogy/RTX40MFG-Unlock 系列）不适用于 310.9.1，
  改由 `src/ngx/DlssgTransfusion.cpp` 在 NGX 初始化前处理：架构门槛 0x1b0→0x190/0x170、
  `GetGPUArchitecture` 与 `*_GetFeatureRequirements` 的 Ada 立即数、provider count/index validator、
  Blackwell 内核 fatbin 重建与容器就地重定向、DL1/DL2 网络内核替换、输出图像内核替换、valid-warp 质量策略。
  310.7 的旧模块保留在源码中，只有在运行库是 310.7 时才会装载。
- **优化内核**：29 个 `.ptx` 作为运行时文件随包提供（`runtime/experimental/dlssg-kernels`），不进源码 Git；
  缺失只关闭优化，不影响播放。来源与授权状态见 `THIRD_PARTY_NOTICES.md`。

### 本机验收（RTX 5070，驱动 32.0.16.1656；run 日志 `E:/项目/Veyra/tests/fg-3109-20260930/runs`）

| 用例 | 结果 |
| --- | --- |
| 408 | 310.9.1 未打补丁基线：55 帧全部 distinct，`sequenceHash=0x3039202C86BADB9F`，submit+wait 中位 1.043 ms |
| 415 | 310.7 运行库基线：**同一哈希**，证明两版 provider 输出一致，跨版本比较有效 |
| 416 / 431–433 | 全补丁：16 描述符重定向、31 容器就地重定向、144 次写入、27 网络 + 2 图像内核；哈希 `0x2055B062C57E7690`（三次一致） |
| 421 / 434 | 关优化内核：`optimized=false`，哈希回到基线 |
| 422 | 关 Blackwell 内核：`descriptors=8`（只改写 scatter），哈希回到基线 |
| 423 | 关质量策略：哈希回到基线 |
| 441 / 442 | FG 真值 harness（平移/方向/切场景）：打补丁前后均 **PASS**，两版 blend 指标逐项相同 |
| 450 / 451 | 产品 60fps 冒烟（FG 2X，SR 310.7 + FG 310.9.1）：2400+ 生成帧、`failed=false`，补丁应用与回滚 `restored=true` |

- **未决问题（不得当成已完成）**：valid-warp 质量策略与优化内核**同时**生效时，合成序列哈希与基线不同；
  单独任一项都与基线逐位一致。真值 harness 两种配置都通过，但这既不是逐位相等的证明，也不是画质结论。
  下一判据（尚未执行）：用 `--capture-dir` 抓两版生成帧做数值比较，判断它是目标内的抗撕裂/抗拖影改动，
  还是 50 系跑 Ada 路径带来的差异。
- **未验证**：RTX 40 / RTX 30 实卡上的补丁与多帧生成；本机只有 5070，只走 Ada 路径。
- **发布**：不提交、不推送、不发布；Runtime Pack 与 Release 仍需另行授权。
