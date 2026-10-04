# NR / 增强管线性能优化方案（2026-10-03，交 Codex 实施）

来源：用户要求研究 SAOG0721/Magpie（集成 DLSSNR / RTX Video / DLSS·XeSS 补帧的 Magpie 实验分支，GPL-3.0，与 Veyra 同许可）和原版 Blinue/Magpie 的性能做法，搬可用的、并在其基础上继续深挖。本文由 Claude 起草，未写任何实现代码；所有"收益"都是估计，必须实测后才能写进发布说明。

## 0. 必读与约束

- 资料：`E:/项目/Veyra/deps/saog-magpie-research-20261003/`（21 个版本发行说明、`docs_*` 设计/复盘文档、`src_*` 关键源码：`DLSSNRFilter.cpp`、`DLSSNRMultiPass.h`、`DuplicateFrameCS.hlsl`、`HalfResOpticalFlow.cpp`）；`E:/项目/Veyra/deps/magpie-research-20261003/`（原版 Magpie D3D12 分支 `DuplicateFrameChecker`、`FrameRingBuffer`、`FrameProducer`）。重点读：
  - `docs_experimental_reviews_20261001-v0.6.9-dlssnr-overhead.md` + 对应 TODO（多层 NR 整链调度）
  - `docs_experimental_reviews_20261001-v0.6.9-duplicate-frame-filter.md` + TODO（重复帧过滤与缓存失效规则）
  - `docs_experimental_reviews_20260905-v0.6.5-r9-dlssnr-switching-stutter-REVIEW.md`（捕获中断、性能监视器无界等待等教训）
- 借用代码须在文件头注明来源仓库、文件和许可（GPL-3.0）。
- 遵守仓库 AGENTS.md：独立 worktree / build / tests / logs 目录（`E:/项目/Veyra/...`），不改用户驱动 / NVIDIA App / OBS / RTSS 配置，数据目录仍为 `user-data-2.0.0`，新增中文文案跑 `scripts/i18n/extract.py` 并补齐 zh-TW/en/ja。
- 基线分支：用当时最新的集成线（2.0.1 候选 / main，以实际为准）。注意 `worktrees/player-startup-speed-20261003` 有未提交改动（`main.cpp`、`EngineController.*`、`QmlPlayerBridge.*`、`SettingsPage.qml` 等），与本方案会冲突，先协调合并顺序。Claude 的小飞机修复在 `claude/rtss-compat-20261003`（28a440b，未合并），它在 `SettingsPage.qml` 新增了"监控软件兼容"行，"性能"分组要与之相邻排列。
- 不允许为了帧率删除必要的 GPU 同步、吞掉设备移除、或把未就绪的读回结果当作有效判断。

## 1. 基线（本机 RTX 5070，驱动 32.0.16.1656，2026-10-03）

1080p30 文件，NR（community-Lecram-RTX50）+ DLSS SR 到 4K，日志 `[player-timing]`：

| 项 | P95 |
|---|---|
| gpuNrP95Ms | 5.57 |
| gpuSrP95Ms | 2.8–3.2 |
| gpuFlowP95Ms（NVOF grid4） | 1.2–1.4 |
| gpuResidualP95Ms | 0.24 |
| gpuColorP95Ms | 0.03 |
| enhancementProcessingMs | 9.9–12.1 |

`[reset-lifecycle]`：开 NR 重建 createMs=2377（其中 `NGX Init_with_ProjectID` 1.63 s、NR snippet Init_Ext 0.11 s、CreateFeature 0.52 s）；再开 SR 重建 createMs=1342（Init_with_ProjectID 0.92 s）。每次重建都 `Shutdown1` 后再 `Init_with_ProjectID`。重建期间画面停住。

SAOG 分支日志（RTX 5070 Ti，4K 级）：NR 100% 输入 15–18 ms，40% 约 4–6.5 ms，25% 约 2.6–3.1 ms，即 NR 耗时近似随输入像素线性下降。

结论：开销几乎全在 NGX（NR/SR）和 NVOF，自写着色器不到 3%。着色器半精度、DXC、描述符堆这类优化不做。

## 2. 已有 / 不做

- NR"内部处理分辨率 + 残差回填"（SAOG v0.6.0 的头号优化）Veyra 已有：`qml/Veyra/NrLayerEditor.qml` 的 `nr-resolution`，`GraphDescription.h` 的 `nrLayersExtent`，`shaders/NrDownsample.hlsl`（面积平均）、`shaders/NrResidualComposite.hlsl`（边缘加权双线性）。不重做；采样核差异（Lanczos-2 / Catmull-Rom）属画质，不在本方案。
- 半分辨率块匹配光流、深度估算：不做（Veyra 已有 NVOF / AMD OF / GPU-DIS；SAOG 自己已放弃深度）。

## 3. 第 0 阶段：先做三个实验（结论写进 docs 再动后续）

| 实验 | 方法 | 决定什么 |
|---|---|---|
| E1 NR 实例能否动态改输入尺寸 | 独立测试程序或测试开关：以最大尺寸 CreateFeature（DLSSNR feature id 18），Evaluate 时传更小的输入/子区域（参照 DLSS SR 的 render subrect 参数），检查返回值、画面、历史。社区 DLL 与官方 DLL 各测一次 | 3b 用"单实例动态尺寸"还是"预建 2–3 个尺寸实例切换" |
| E2 NGX 能否在后台线程建实例 | 主线程持续 Evaluate 旧实例，后台线程用独立 command allocator/list 在同一设备上 CreateFeature，跑 10 分钟 + 反复 50 次 | 2b 是否可行 |
| E3 真实重复帧比例 | 采集卡（用户有）/屏幕捕获，记录 60 秒内逐帧精确相同的比例；主机 30/40/60 帧游戏、视频、桌面各一段 | 1a、1b 的真实收益 |

每个实验产出一个 `docs/PERF_E{n}_*.md`，含原始日志路径。

## 4. 第一阶段：收益大、风险低、不改画面

### 1a 重复帧整链跳过

现状：`src/pipeline/EnhanceGraph.cpp` 约 1834 行 `analyzeLuma` 只对 CPU 上传路径做抽样 SAD，结果 `out.contentDuplicate` 仅用于补帧抑制（约 2778 行），且受 `desc_.contentRate!=Transport` 限制；NR/SR/光流每帧照跑。GPU 解码 / D3D12 输入（`gpuRgb` 分支）没有任何重复检测。

设计：
- GPU 精确比较：参照 `DuplicateFrameCS.hlsl`（Gather 四通道比较，8×8 组内归并 + 原子标志）。SDR 比 RGB，HDR/FP16 比 RGBA。在源颜色转换后、NR/SR/光流之前比较"本帧源"与"上一帧被接受的源"。
- 判断时机：比较结果需在录制 NR 之前拿到。方案 A（先做）：同一队列先提交比较 + 拷回，CPU 等结果（原版 Magpie 用 D3D11 小 dispatch，约几十微秒；Veyra 用 D3D12 也要量 `dupReadbackWaitUs`）。方案 B（量化后若等待明显再做）：动态抽查——连续 16 帧不重复后逐步拉长检查间隔（原版 Magpie `DuplicateFrameChecker`：初始 16 帧，跳检数递增到 16），一旦命中重复恢复逐帧。禁止把未就绪结果当作"不重复"之外的任何判断。
- 命中重复：不推进 NR/SR/光流历史，不 Evaluate，直接重复呈现上一份增强输出（沿用现有"重复呈现"路径，补帧仍按现有 `contentDuplicate` 规则处理）。光流下一次计算必须对比"上一帧被接受的源"，不是被跳过的重复帧。
- 缓存失效（照 SAOG 0.6.9 规则）：参数修订、上游输入修订、resize、颜色语义变化、设备重建、epoch/seek、捕获中断都使缓存失效，下一帧强制完整处理。失败输出不得成为可复用缓存。
- 导出：文件源也走同一逻辑（重复帧少，开销是一次比较）。导出时必须保证输出帧数与时间戳不变，只是复用像素。

开关：设置 → 性能 →"跳过重复帧"，默认开，偏好键 `skipDuplicateFrames`（bool）。

日志：`[dup-frame] checked= skipped= ratio= readbackWaitP95Us= mode=exact|dynamic`，每 10 秒一行；性能球新增"重复帧已跳过 xx%"。

验收：
- E3 的素材上，跳过率与 E3 统计一致；30-in-60 素材 `enhancementProcessingMs` 平均值约减半（报告实测值）。
- 无重复的视频：每帧开销增加 ≤0.1 ms（实测），画面与关闭时逐帧一致（抽 300 帧比对）。
- 补帧开/关、NR 抗闪烁开/关、seek、暂停、resize、HDR 各一轮，无闪烁、无历史错乱。
- 导出帧数、时间戳、时长与关闭时一致。

### 5a 跳过黑边（Veyra 现在没有）

设计：
- 检测：在源颜色上统计每行/列最大亮度（低分辨率缩略即可，GPU 小 dispatch 或复用已有 luma 抽样），连续 N 帧（≥60）一致才确认有效区域；阈值保守（如线性亮度 <1.5% 且方差极小）；暗场景、淡入淡出不改变已确认区域；变化需持续 ≥1 秒才切换。
- 处理：NR / SR / 光流只处理有效区域，输出时黑边区域直接填黑（不经过模型）。有效区域变化 = 一次受控重建（复用 2a 后的快速重建）；若 E1 证明可动态尺寸，则不重建。
- 只对"标准"比例吸附：2.39/2.35/2.2/1.85 上下黑边、4:3/3:2 左右黑边；非标准或检测不稳定时不裁。

开关：设置 → 性能 →"自动跳过黑边"，默认开，键 `skipLetterbox`（bool）。

日志：`[letterbox] active=x,y,w,h ratio= confirmedAfterFrames=`；性能球"已跳过黑边 xx%"。

验收：2.39:1 片源、4:3 游戏、全画幅片源、暗场景片段、片头淡入片段各一段；裁剪不误伤画面（逐帧比对有效区域外是否全黑），NR/SR GPU 时间按像素比例下降（报告实测）。

### 3a 进程 GPU 调度优先级

设计：启动时 `D3DKMTSetProcessSchedulingPriorityClass`（gdi32 导出，按函数指针调用），设置项：普通 / 高 / 实时。原版 Magpie #1146 用"高"；SAOG 0.6.1 改"实时"并有用户反馈改善；原版作者与 Sunshine 均记录"实时"可能让系统不稳。默认"高"。失败只记日志。

开关：设置 → 性能 →"GPU 优先级"，键 `gpuPriority`（"normal"/"high"/"realtime"），改后立即生效（可重复调用），不需要重启。

验收：同时运行一个满载 GPU 的程序（如 3D 测试或游戏），对比普通/高/实时下 Veyra 的帧时间 P95 与掉帧数；记录对方程序帧率变化。

## 5. 第二阶段：消除切换卡顿

### 2a 跨重建保留 NGX 核心

现状：`EnhanceGraph` 自己持有 `coreHost_`（约 1092 行 `std::make_unique<ngx::NgxCoreHost>()`），每次重建先 `Shutdown1` 再 `Init_with_ProjectID`。实时改参数走 `EnhanceGraph::applySettings`（约 2820 行），返回 false 才重建。

设计：
- 把 `NgxCoreHost` 提升到设备上下文级（同一 D3D12 设备、同一运行库目录内复用），`EnhanceGraph` 只借用。设备重建、运行库目录变化（社区/官方 NR DLL 切换）、TDR 后（NGX 在同进程设备重置后不可再初始化，见 2.0.1 记录）才真正关闭/重开。
- 补帧兼容补丁（Ada/Ampere MFG 解锁、DLSSG 转接、`FgCompatibilitySession` 的 prepareDriver / beginInitialization / endInitialization）是挂在核心初始化前后的：补帧后端或补帧开关变化时仍按原流程完整重开核心；其余重建（NR/SR 开关、分辨率、层数）保留核心。
- 参数块：重建前确认旧图的参数块全部 destroy（现有"leaked parameter block"检测保留）。
- 关闭顺序不变：功能 → 参数块 → 核心。

验收：开/关 NR、开/关 SR、改 NR 层数、改 NR 分辨率各 20 次，`[reset-lifecycle] createMs` 前后对比（目标：非补帧相关重建 <0.6 s，报告实测）；切补帧后端仍正常；VRAM 不随次数增长（`[vram-watch]`）；退出无泄漏警告。

### 2c 最近配置实例缓存

设计：重建时把旧图的 NR/SR feature（及其纹理）按 key（尺寸、模型、标志、运行库）保留 1 份，显存余量（budget − usage）大于其占用 ×1.5 才保留，否则立即释放。切回同一配置时直接复用，NR 历史 reset 一次。

无开关。日志 `[feature-cache] hit|miss|evict key= bytes=`。

验收：NR 开→关→开，第二次开 createMs 再降（报告实测）；显存紧张时不保留（用 `VEYRA_TEST_VRAM_LEAK_MIB` 制造压力验证）。

### 2d 启动预热

设计：首页空闲 2 秒后，后台初始化 NGX 核心并按默认预设建 NR/SR 实例（尺寸取上次会话源尺寸，没有则 1080p），首次打开视频时若配置匹配直接接管。用户在预热中打开视频：等预热完成而不是并行再建。

开关：设置 → 性能 →"启动时预热增强组件"，默认开，键 `prewarmEnhancement`（bool）。非 NVIDIA 显卡不预热。

验收：冷启动后首次打开视频开 NR，首帧时间与停顿对比（报告实测）；关闭开关时不占显存。

### 2b 后台建图、无缝切换（依赖 E2）

设计：需要重建时，旧图继续出画面；新图在后台线程创建（独立 allocator/list，同一设备与队列）；创建完成后在帧边界原子切换并释放旧图。显存不足以并存时退回现有停顿式重建并提示。

无开关。日志 `[graph-swap] buildMs= swapFrame= fallback=`。

验收：切换时 `[present]` 无空档（呈现间隔最大值 < 2 个帧间隔），音画同步不漂；连续切换 50 次无泄漏、无设备移除。

## 6. 第三阶段：按负载自动调整

### 3b 动态 NR 内部分辨率（依赖 E1）

设计：
- 在 NR 层"内部处理分辨率"里新增"自动（按负载）"选项。
- 预算 = 帧间隔 × 0.8（帧间隔取内容帧率，见 1b；补帧开启时按真实帧计算）。每 0.5 秒看 `enhancementProcessingMs` P95：超预算降一档，连续 3 秒低于预算 × 0.7 升一档。档位 100/85/70/55/40%，下限 40%（SAOG 记录过小尺寸会 TDR；E1 中确认安全下限）。
- 实现：E1 可行则单实例改输入尺寸；否则预建当前档与相邻低一档两个实例，切档时 reset 一次 NR 历史。
- 导出不使用（导出始终完整处理）。

日志 `[nr-auto] level= budgetMs= p95Ms= reason=`；性能球"NR 自动 70%"。

验收：播放中启动一个 GPU 满载程序，自动档能把帧时间拉回预算内且不来回跳（每分钟切档 ≤4 次）；画质：切档瞬间无明显跳变（录屏检查）。

### 1b 按内容帧率回收预算

设计：1a 统计出稳定的内容帧率（如 60Hz 信号中 30 帧内容）后，3b 的预算按内容帧间隔计算，从而自动把 NR 提到更高档位。只在"自动"档生效，不单独做开关。

验收：30-in-60 素材上，自动档稳定后档位高于同素材关闭 1a 时（报告实测），且无掉帧。

### 1c 重复帧节奏预测

设计：1a 的统计确认出稳定节奏（例如 60Hz 信号里"新、旧"交替，或 3:2 下拉）后，按节奏预测下一帧是否重复：预测为重复的帧不做比较直接复用；预测为新帧的照常处理。每 N 帧（初始 8，可调）仍做一次真实比较校验；任何一次预测错误（预测重复但实际不同）立即退出预测、恢复逐帧比较，并把这一帧按新帧完整处理。节奏来源可复用 `SceneCadenceAnalyzer`（`src/core/SceneCadenceAnalyzer.cpp`）。

收益主要在高分辨率下省掉比较与读回等待，并让调度器提前知道"下一帧不用算"（可配合 3b 提前分配预算）。不单独做开关，随"跳过重复帧"开关。

日志 `[dup-frame] predicted= verified= mispredicted=`。

验收：30-in-60、24-in-60（3:2）、40-in-120 素材上误判率为 0（每次校验都对得上）；节奏打破（游戏掉帧、切场景）时 1 帧内退出预测；关闭预测与开启时输出逐帧一致。

## 7. 第四阶段：结构性改动

### 3c 队列分工

设计：NR/SR/光流放到 `D3D12_COMMAND_LIST_TYPE_COMPUTE` 队列（普通优先级），颜色输出与呈现 blit 走 DIRECT 队列（`D3D12_COMMAND_QUEUE_PRIORITY_HIGH`）。现有补帧独立呈现队列（`src/engine/VideoPresenter.cpp` 约 30 行）先改为 HIGH 做小实验，再决定全面拆分。须先确认 NGX NR/SR Evaluate 可录制在 compute list 上（不行就只调呈现队列优先级）。

验收：NR 满载 + 补帧 2×/3× 时，`[pacing]` / `[present-cost]` 呈现间隔抖动下降（报告实测）。

### 5b 文件播放与导出跨帧并行

现状：光流在 NVOF 独立硬件上执行，但主队列 `Wait(nvofOutFence_)` 后才继续（约 2181–2195 行），着色单元在这段时间空闲。

设计：文件播放与导出时（直播采集禁用，避免加延迟），帧 N 的 NR/SR 录制期间提前提交帧 N+1 的 NVOF；需要两套 flow/conf 纹理与对应 fence。导出另可让两帧同时在途（编码在 NVENC 独立引擎）。

验收：8K 与 4K 导出耗时对比（目标提速 10% 以上，报告实测），输出与串行版逐帧一致；直播采集路径行为不变。

### 4a/4b 多层 NR 整链调度 + 先粗后细

现状：多层 NR 每层从上一层全分辨率输出重新降采样（约 2415 行），每层在全分辨率叠回（`compositeNrLayer`，约 2332 行）。

设计：
- 4a：照 SAOG 0.6.9，只降采样一次得 L0，各层在低分辨率直接串联得 LN，出口一次 `LN − L0`、一次升采样叠回。注意 Veyra 每层有独立残差参数（`residualSettings`），而 SAOG 改成了一组总参数，画面会变；需要先定方案：保留每层残差参数（在低分辨率逐层应用）或改为总参数（需用户确认）。
- 4b：允许各层不同内部分辨率（低分辨率在前、高分辨率在后），做成多层 NR 的推荐组合。

验收：2/3 层时 `gpuResidualP95Ms` 与总处理时间下降（报告实测）；单层画面与现状逐帧一致。

### 5c 暂停时 GPU 空闲

设计：暂停后完成一次处理即停止重复处理（呈现沿用已有帧）；暂停中只改残差类参数时，复用 NR 输出只重做叠加（SAOG 0.6.9"Residual-only 不重跑 NR"）。

验收：暂停 30 秒 GPU 占用接近 0；暂停中拖残差滑块画面实时变化且 NR Evaluate 计数不增加。

## 8. 设置与界面汇总

设置 → 新增"性能"分组（放在"监控软件兼容"之后）：

| 行 | 控件 | 键 | 默认 |
|---|---|---|---|
| 跳过重复帧 | 开关 | `skipDuplicateFrames` | 开 |
| 自动跳过黑边 | 开关 | `skipLetterbox` | 开 |
| GPU 优先级 | 普通 / 高 / 实时 | `gpuPriority` | 高 |
| 启动时预热增强组件 | 开关 | `prewarmEnhancement` | 开 |

NR 层"内部处理分辨率"新增"自动（按负载）"（3b），默认不选。偏好在 `QmlPlayerBridge::setPreference` 白名单/校验中登记；需要 Qt 启动前读取的放到 `apps/veyra-qml/main.cpp` 的偏好读取块。性能球新增状态行：重复帧跳过比例、黑边跳过比例、NR 自动档位。

## 9. 对比测试（每个阶段都必须做）

### 9.1 对照组

| 组 | 内容 | 用途 |
|---|---|---|
| A 基线 | 开始本方案前的版本（2.0.1 候选或当时 main 的同一提交），完整构建 + 打包 | 所有"前后对比"的"前" |
| B 本阶段 | 本阶段完成后的构建，新功能按默认开关 | "后" |
| B-off | 同一构建，本阶段新开关全部关闭 | 证明关闭后与 A 行为、画面一致（回归保护） |
| C 外部参照 | SAOG0721/Magpie Experimental v0.6.9（官方发布包，只在隔离目录运行，不改其配置以外的任何东西） | NR 开销、切换停顿、重复帧跳过率的横向参照；仅作参考，不追求数字相同 |

A、B、B-off 用同一台机器、同一驱动、同一 NR DLL（记录文件版本与 SHA256），显示器刷新率、HDR 开关、窗口大小（固定 2560×1440 全屏或固定窗口）一致；测试前关闭其他 GPU 负载程序，记录 `nvidia-smi` 显存/频率快照。

### 9.2 固定素材（放 `E:/项目/Veyra/tests/perf-matrix/media/`，记录来源与 SHA256）

| 编号 | 素材 | 覆盖项 |
|---|---|---|
| M1 | 1080p30 实拍/游戏录像 60 秒（现有 `gta6-1080p30-60s.mp4`） | 基础 NR/SR |
| M2 | 4K60 文件 60 秒 | 高分辨率开销、导出 |
| M3 | 2.39:1 电影片段（含暗场、淡入淡出） | 5a |
| M4 | 4:3 游戏画面（左右黑边） | 5a |
| M5 | 采集卡：主机 30 帧游戏走 60Hz 信号，60 秒（需用户提供或现场采集） | 1a/1b/1c |
| M6 | 采集卡：60 帧游戏 | 1a 无重复时的额外开销 |
| M7 | 24p 内容 3:2 下拉到 60Hz（可用 ffmpeg 生成） | 1c |
| M8 | 屏幕捕获：桌面静止 + 窗口拖动 | 1a、5c |
| M9 | 8K 导出 10 秒片段 | 5b |

### 9.3 配置矩阵（每个素材选适用的行）

| 编号 | 增强设置 |
|---|---|
| S1 | 仅 NR 1 层，内部 100% |
| S2 | NR 1 层，内部 50% |
| S3 | NR 1 层 + DLSS SR 到 4K |
| S4 | NR 2 层 + SR + 补帧 2× |
| S5 | NR 3 层（先粗后细组合，4b 后） |
| S6 | S3 + 同时运行满载 GPU 程序（3a/3b 用） |

### 9.4 指标与采集方法

- 性能：从日志 `[player-timing]` 取 `enhancementProcessingMs`、`gpuNrP95Ms`、`gpuSrP95Ms`、`gpuFlowP95Ms`、`gpuResidualP95Ms`；`[present-cost]`/`[pacing]` 取呈现间隔 P50/P95/P99 与最大值；掉帧数；`[vram-watch]` 显存峰值；进程 CPU%（psutil 每秒采样）。每组跑 3 次，取中位数，报告最小/最大。
- 切换停顿（第二阶段）：`[reset-lifecycle] createMs/totalMs`；呈现空档 = 切换前后两次真实 Present 的时间差；每种切换 20 次取 P50/P95。
- 新增计数：`[dup-frame]`、`[letterbox]`、`[nr-auto]`、`[feature-cache]`、`[graph-swap]` 各行。
- 画质 / 一致性：
  - 不应改画面的项（1a、1c、2a、2c、2d、2b、3a、3c、5b、5c，以及 B-off）：导出同一素材（导出可复现），与 A 的导出逐帧比较，要求解码后逐帧一致（像素完全相同；若因 NGX 非确定性不能完全一致，先用 A 自己导两次量出噪声底，再要求 B 与 A 的差不超过该噪声底），用 ffmpeg `-lavfi psnr/ssim` 输出逐帧数值存档。
  - 会改画面的项（5a 黑边区域、3b、1b、4a/4b）：PSNR/SSIM 对比 + 关键帧截图并排（静止细节、文字/UI、细线、运动边缘、暗部、高光），交用户主观确认；不能用"更快"代替画质结论。
  - 播放（非导出）画面：`scripts/ui-check` 截图对比关键帧。
- 导出：输出帧数、时间戳、时长、音轨与 A 一致（ffprobe）；耗时与平均 fps。

### 9.5 结果表模板（每阶段一份，写进 `docs/PERF_RESULTS_<阶段>_2026-10-xx.md`）

| 素材 | 设置 | 指标 | A | B | B-off | C（参考） | 变化 | 结论 |
|---|---|---|---|---|---|---|---|---|
| M5 | S3 | enhancementProcessingMs 平均 | | | | | | |
| M5 | S3 | 重复帧跳过率 | — | | — | | | |
| M1 | S3 | 呈现间隔 P99 | | | | | | |
| … | | | | | | | | |

规则：只有 A/B/B-off 三组都跑完、画质一致性检查通过的项目才能标"完成"；数字退化或画面不一致的项目回退或关默认，并在结果文档写明原因。

### 9.6 脚本

在 `scripts/perf/` 新增驱动脚本：给定构建目录、素材、设置、次数，自动启动（复用 `scripts/ui-check/ui-check.py` 与现有测试开关）、采集日志与 psutil、解析上面的指标、输出 CSV 与结果表草稿。外部参照 C 只做手动或半自动记录（不改其配置以外的东西），写明测法。

## 10. 交付与记录

- 每个阶段单独提交，提交信息写清第 9 节对比测试的前后数字；`docs/WORKLOG.md` 加条目；阶段结论写 `docs/PERF_*_2026-10-xx.md`。
- 回归：repair contract、xbox、Qt Quick、i18n、qml-data、导出 queue/lifecycle/mp4/mkv、OBS 游戏采集（含兼容模式）、小飞机自动兼容（若已合并）。
- 不发布、不推送，完成后向用户报告实测数据，由用户决定是否进版本。
