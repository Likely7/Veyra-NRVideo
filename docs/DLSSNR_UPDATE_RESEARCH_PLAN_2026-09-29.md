# DLSS NR / 补帧上游更新调研与优化方案（第二版）

日期：2026-09-29。状态：**只调研、写方案，未下载任何二进制，未改代码**。

用户要求：
- 30/40/50 系的 NR 和补帧能力都是从 GitHub 社区项目接进来的，逐个查它们在我们对接之后的更新（更新即优化）。
- 用户已确认要做：**重复帧跳过 NR**、**NR 与显示分开 GPU 队列**。
- "降低 NR 分辨率"已有（每层六档内部尺寸），不再列为新项。其余继续找。

AGENTS 原规则禁止"联网寻找更新偷跑版"。本次按用户明确要求只看公开发布说明、提交记录和清单；任何 DLL 仍须用户提供文件并逐项授权。

## 1. 我们对接的上游，与现在的最新版

| 用途 | 上游项目 | 我们对接的版本 | 现在最新 | 更新内容与我们的关系 |
| --- | --- | --- | --- | --- |
| 30 系 NR（运行库和调用方式参考） | perseval-BLR/NeuralScreen（原 DLSS5-NeuralScreen） | `8098ccf`（1.8.2，09-13） | **v2.1.8**（09-27），领先 **408 个提交** | NR DLL **没换**：v2.1.8 清单里的 `nvngx_dlssnr.dll` 仍是 `dcc0dc24…`，与我们的 Ampere 版相同。更新的是软件层（见 §3），其中 RTX30 NR 在他们的表里仍标为"未验证" |
| 40/50 系 NR 社区 DLL | Magpie 的 DLL 包（来源为 RenoDX 圈的 `310.8.0-RTX40`，即 Uncle Burrito 的 Ada 补丁） | `984BEE0F…` | 同名文件没有新版本 | Magpie 0.6.7 起的 DLL 包新增 **SF-v2** 版 |
| NR DLL 发布源头 | RankFTW/rhi-repo（RenoDX 的运行库镜像） | — | 08-27 `310.8.0` → 08-30 `310.8.0-RTX40`、`310.8.SF`、`310.8.SF-v2` → **09-24 `310.8.Lecram`** | 我们对接之后只多了一个 NR DLL：**Lecram**。社区称它"为 RTX50 优化"，DLSS5-Feeder 已默认改用它；**没有任何公开的耗时或功耗数据** |
| 20/30/40 系 NR 补丁 | dev-camo/dlssnr-patcher、dlssnr-ada-patcher（GPL-2.0，是源码补丁器，不是二进制） | 未用 | 09-03 后未更新 | 可以从官方原版**本地生成** Ada/Ampere 版，不必下载别人改好的 DLL。可以作为以后的"可复现"来源 |
| SF 系 NR（ShortFuse） | SF-v2 DLL + "RenoDX DLSS SF" 插件 | 未用 | DLL 停在 08-30 的 SF-v2；插件一直更新到 26.0928（09-29） | SF 版给 RTX40 打了补丁，并为 RTX20/30 加了 **FP16 路线**（旧版在 30 系上走 FP8 模拟，几乎不可用）。插件是 ReShade add-on，源码未公开，不能用 |
| 40 系补帧解锁 | dashdogy/RTX40MFG-Unlock | `33b4183`（v1.3.3） | v1.3.3-hotfix.2（09-18），领先 3 个提交 | 核对了 diff：只改了游戏 overlay、菜单和日志，**补丁模式和 GPU 负载没动**，与我们无关 |
| 40 系补帧解锁 | ImDreamt/MFGAdaUnlock-RenoDx | `a8aa0d9` | 同一提交 | 没有更新 |
| 30 系补帧 | sdli1995/dlssg_for_sm86（只参考，未移植，因为没有源码） | — | 0.3.5（09-19），作者声明"内核已到实际最优，之后只修兼容" | 0.3.4 修复：**架构伪装误传给 DLSS 自己的模型会导致 30 系崩溃**；0.3.5 修复：**FG feature 重建后用错优化内核** |
| 20/30/40 系补帧（新） | **SilyNoMeta/DLSSG-Transfusion**（MIT，fork 自 TonyJoaca/DLSSG-Transfusion） | 未对接 | v1.4.5.3（09-29） | sm86 的后续开发已转到这里。做法是把 310.9.1 的 Blackwell 内核改编译到 86/89/75 上跑（"Transfusion"）。另有两项画质修复：生成帧细节恢复、双重阴影。**优化内核**实测（3070 Ti 笔记本，1080p）：公开部分 2.33→2.23ms（2x），完整版约 2.3→1.6ms（2x） |
| Magpie 主线 | SAOG0721/Magpie experimental | `3841698` | 仍是 `3841698`（09-14 之后无提交） | 活跃的 fork 也都没有 NR 方面的新提交 |
| 官方 SR/FG | NVIDIA DLSS SDK | 310.7.0 | **310.9.1**（09-08） | 官方签名文件。**注意**：我们的 Ada/Ampere 补帧补丁绑定的是 310.7 的 fatbin 身份，升级 dlssg 必须同时重做补丁身份（DLSSG-Transfusion 和 dashdogy 都已支持 310.9.1，可以对照） |

要回答的问题——有没有"能效更好的 NR DLL"：
- 50 系：只有 **Lecram**，有没有提升只能在 5070 上实测。
- 30 系：**SF-v2** 的 FP16 路线在原理上比我们现在用的 NeuralScreen 版（DCC0）合适，但需要 30 系实机验证。
- 40 系：SF-v2 同样包含 Ada 补丁。它与我们现在的 `984BEE0F` 谁快，只能实测。

## 2. 用户已确认要做

### A. 重复帧跳过 NR
- 参考：Magpie 0.6.8 的 `4c644f0`（重复帧过滤）和 `b6752e0`（重复帧不应清空时域历史）。
- **反例要注意**：NeuralScreen 的 `a0371ca5` 删掉了他们的"静止帧跳过"，理由是默认关闭、没人测出收益。所以我们必须先测收益，再决定默认值。
- 做法：
  - 文件播放：用解码的重复帧 / PTS 信息。
  - 采集卡、屏幕、PS5：GPU 上比较当前帧与上一帧，结果用 predication 决定是否执行 Evaluate；覆盖不到时，退回到用 N-1 帧的 4 字节标志决定（这不是像素回读）。
- 跳过时复用上一帧 NR 输出：不清历史、不改真实 PTS，统计里单独记"跳过次数"；导出和单帧处理不跳。
- 验收：
  - 在 30fps 游戏 / 60Hz 采集的场景下，NR Evaluate 次数约减半，并测功耗下降多少。
  - 静止和运动画面都没有新的闪烁或卡顿。

### B. NR 与显示分开 GPU 队列
- 参考：NeuralScreen `0ebb1b32` / PR #127（v2.1.5）。Present 与 NR 共用队列时，真实帧要排在正在跑的 NR 后面，约等 13ms；拆开后在 FG 2x + NR 场景下，等待降到 1ms 以内，源帧率提高 5–19%。
- 先测：记录 FG + NR 时每个真实帧从"可显示"到 Present 之间等了多久。确实有等待再改。
- 改法：显示和 Present 使用独立的 direct queue，用 fence 与 NR 队列同步。**不引入逐帧 CPU 等待**，符合 AGENTS 纪律。
- 验收：等待时间 P95 下降，源帧率不降，画面顺序正确；FG 开关和恢复路径都要回归。

## 3. 新找到、建议做的优化

### 运行库
- C1 **Lecram A/B 测试（50 系）**：你提供文件后放到独立的 `runtime_local/nvidia/nr-lecram/`，与原版和 984BEE0F 对比同一片段：每次 Evaluate 的 GPU 时间、功耗、帧率、同帧差异图。只有数据更好才加进运行时选择。
- C2 **SF-v2 候选（20/30/40 系）**：同上；30 系那部分交给有 30 系卡的人实测，不用 5070 的结果代替。
- C3 **SR/FG 升级到 310.9.1**：需要同时重做 Ada/Ampere 补帧补丁的身份，参照 dashdogy 和 DLSSG-Transfusion 对 310.9.1 的 pattern。工作量较大，单独立项。

### 补帧（30/40 系）
- C4 **DLSSG-Transfusion 的公开优化**（MIT，可以移植）：
  - `Kernel_BlendCandidatesFused` 合并写入，逐位一致，约 -4%；
  - "valid-warp" 保护；
  - UI assist（生成帧上的 HUD、半透明面板修复），这对采集画面里的游戏 HUD 有用。
  - 私有 PTX（约 -30%）不公开，不能用。
- C5 **核对 sdli 0.3.4 / 0.3.5 暴露的两类缺陷在我们这边是否存在**：
  - 我们的 NvapiArchSpoof 是否只作用于 NR（video2dlssnr 1.4.1 也修过同样的"伪装范围过大导致 30 系 SR 崩溃"）；
  - AmpereMfgUnlock 在 FG feature 重建后，会不会用到旧的内核槽。

### 防闪烁
- C6 **输出稳定器**（DLSS5-Feeder 1.17/1.18）：
  - Evaluate 后加一个 compute pass：源图 3×3 邻域与锚点比较，输入没变的区域保留上一帧输出，变了就用新结果。
  - 不做重投影，不会拖影；默认关闭，参数为强度和容差。
- C7 **Magpie 0.6.8 抗闪烁三档**：静态累积、光流累积+、低频时域重建。源码是 GPLv3，从 `3841698` 移植并补 notices；要重跑上次 temporal 的性能退化测试。

### 性能与稳定性（NeuralScreen 2.x 的软件层优化）
- C8 场景切换在 GPU 上判定，不再每帧往返一次 capture（NR +3.3%）。
- C8 灰度处理并进 capture 的同一个 command list，只提交一次、等一次。
- C8 修掉每帧分配 33MB，停掉没人读的光流计算。
- C8 NR 关闭时整条管线暂停。
- C8 不用的 NR pass 及时释放（对应我们的多层 NR）。
- C8 采集暂停后，必须真的 reset NR/FG 历史（他们的 bug 是标志位提前被清掉）。

  以上逐条对照 Veyra 现状：有的只记为"已具备"，缺的才改。
- C9 多层 NR 改尺寸 / 层数时的显存回收回归（参考 OptiScaler 3.6GB 泄漏的修复）。

## 4. 建议顺序
1. 基线测量：5070、同一片段、各 NR 版本、FG 开关，测 GPU 时间、功耗、等待时间、重复帧比例。采集卡等你空出来再测。
2. A 重复帧跳过 NR。
3. B 独立 Present 队列。
4. C5 缺陷核对和 C8 逐条对照（改动小、风险低）。
5. C6 输出稳定器，然后 C7。
6. C1/C2 等你提供 DLL 后做 A/B；C3、C4 单独立项。

每一步单次测试不超过 300 秒，结果写入 WORKLOG。不提交、不发布，除非你另行授权。

## 5. 来源
- NeuralScreen：`runtime-manifest.json`（v2.1.8），提交 `0ebb1b32`、`a0371ca5`、`6c8657b6`、`67483252`、`facdd182`、`a0be63f9`、`25180371`、`5a8f94c4`
- RankFTW/rhi-repo release 列表（NR DLL：310.8.0 / 310.8.0-RTX40 / SF / SF-v2 / Lecram）
- dashdogy/RTX40MFG-Unlock `33b4183...HEAD` diff；ImDreamt/MFGAdaUnlock-RenoDx
- sdli1995/dlssg_for_sm86 0.3.4/0.3.5；SilyNoMeta/DLSSG-Transfusion README、docs/OPTIMIZED-KERNELS.md、docs/PUBLIC-SOURCE.md
- dev-camo/dlssnr-patcher、dlssnr-ada-patcher；ShyVortex/dlss-unlocked NR-v0.9.32
- jlrouzies-fr/DLSS5-Feeder 1.17–1.18；SAOG0721/Magpie v0.6.8；NVIDIA/DLSS v310.9.1
