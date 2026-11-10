# NR 推理复用（自动推理复用）2026-11-07

分支 `claude/nr-reuse-20261107`，基于 main `0079ed9`，工作树 `E:/项目/Veyra/worktrees/claude-nr-reuse-20261107`，构建目录 `E:/项目/Veyra/build/claude-nr-reuse-20261107`。

## 来由

- 用户 11-07 决定：不复制、不移植 SAOG0721/Magpie 的代码，要自研更强的 NR 提速方案。
- 提案见 `E:/项目/Veyra/reports/claude-nr-own-optim-20261107/PROPOSAL.md`。用户选了方案 A，要求：
  - 开关叫“自动推理复用”；
  - 同时提供手动模式，可选 1/2、1/3、1/4。
- 本实现从 Veyra 现有模块写起：光流、残差合成、场景/复位规则、自动档控制器思路。没有打开或参考 Magpie 源码。
- “按运动补偿复用之前的计算结果”是通用做法，例如：
  - reverse reprojection caching（Nehab 等，2007）；
  - TAA；
  - 视频编码的运动补偿。

## 行为

**只用于实时预览。** 只有预览图会被标记 `nrReusePreview`；导出、暂停单帧、图片的图不会被标记，照常逐帧完整推理。

**复用帧怎么出图**：这一帧不跑 NR 推理，输出为

```
out = 本帧源 + w × (上一推理帧的 NR 输出 − 上一推理帧的源)，位置按光流对齐
```

- 搬运的是 NR 带来的“变化”，不是旧画面，所以源画面的细节和位置总是当前帧的。
- 权重 `w` 在下列位置降为 0，这些像素显示未增强的本帧源：
  - 搬过来的关键帧源与本帧源对不上：3×3 盒均亮度，在约 2.2 次方编码下比较，差值 0.03–0.08 之间平滑过渡；
  - 对齐后的位置出了画面；
  - NR 全局保护区域内，在本帧像素上重新计算一次，复用的变化不会伸进保护区。

**变化拆成两部分搬运**：

- 邻域均值（色调、颜色）在 3×3 邻域对得上时照搬；
- 细节部分只在本像素自身的细节也与关键帧对应位置一致时才搬，阈值 0.03–0.09（同一编码下）。
- 这样随帧变化的颗粒、噪点不会被盖上上一帧的细节，只保留它自己的细节加上搬来的色调。

**暂停时补做完整推理**：进入暂停时，若最后处理的一帧是复用帧，引擎沿用“暂停期间改设置”那条重绘路径，带 reset 重新处理缓存帧，这一帧就会完整推理。日志记 `pause began lastFrameReused=...`，产品合同要求暂停单帧完整处理。

**下一帧一定重新推理**：不会连续两帧复用，所以搬运的变化最多是一个源帧之前的，错误的运动矢量最多影响一帧。

**档位**：0 = 每帧推理；1 / 2 / 3 = 每 4 / 3 / 2 帧复用 1 帧。

- 手动 1/2、1/3、1/4 分别固定为 3、2、1 档。
- 用户的“1/2、1/3、1/4”按“每 2/3/4 帧复用 1 帧”实现。若用户本意是“每 2/3/4 帧只推理 1 次”，需要放开“最多连续复用 1 帧”的限制，界面文字也要跟着改。

**自动**：引擎的 `AutoNrReuseController` 选档。

- 输入是 1 秒窗口内的平均增强耗时，以及推理帧上的 NR 耗时（各层 span 之和）。预算沿用自动 NR 尺寸：transport 帧间隔 × 0.8。
- 复用只降低平均耗时，P95 仍是推理帧的耗时，所以按平均值判断。
- 超预算持续 0.5 秒就升一档。换档后等 1.25 秒，让窗口里只剩新档位的帧再判断。
- 降档要满足两点：预测降一档后的平均耗时（当前平均 + 增加的推理份额 × NR 耗时）小于 0.85 倍预算，并且持续 3 秒。
- 换档只是图内调度，不重建、不排空呈现，所以没有切档停顿。
- 自动最多到每 2 帧复用 1 帧。

**NR 自身的时域历史**：复用帧之后的那次推理，NR 的历史停在两帧前。

- 这一帧给 NR 的运动输入（nrFlow_ 及各层 motion）改为串接后的运动 `D(p) = f(p) + D_prev(p + f(p))`。
- 时域稳定器也改读复用 pass 的运动：推理帧之后等于原光流，逐位相同；复用帧之后是串接运动。
- 稳定器的 frameMs 用距上次推理帧的间隔。

**复位**：任何 reset 都会让调度器重来，下一帧推理。reset 包括：

- seek、PTS 断点、场景切换（软件解码有 CPU 场景分析）、尺寸变化、换源、暂停刷新；
- 设置的实时修改。

**生效条件**（不满足时记 `nr-reuse ... inactive reason=`，界面显示“未生效：原因”）：

- 预览图；
- 英伟达 NGX NR，不含 AMD 后端；
- SDR 工作空间（!hdrWorking）；
- NR 运动来源为光流；
- NR 没有分在超分前后；
- NR 层之间没有保护区域；
- 列表模式（非 runtimeNodeOrder）；
- 不与自动 NR 尺寸池同时使用。复用开启时引擎不准入自动尺寸池；预热和最近图缓存也不收复用配置。

**开关的生效方式**：

- 关 ↔ 开会重建图，因为 pass 只在开启时创建；关闭时不创建任何资源，命令流与改动前相同。
- 自动、1/2、1/3、1/4 之间切换实时生效。

**显存**：两张链路分辨率的 RGBA16F 关键帧，加上光流分辨率的位移（2 张 RGBA16F）和运动（RG16F），已计入建图显存预估。

## 11-07 用户反馈后的调整

**本轮验证（真实程序，4K60，4 层 NR）**：

| | 关闭复用 | 1/2 |
|---|---|---|
| NR 第4层 平均 / P95 | 3.50 / 3.79 ms | 1.77 / 3.66 ms |
| 总耗时 | 16.24 ms | 8.36 ms |
| 提交 | 52.8 fps | 60.0 fps |

- 状态只显示“每 2 帧复用 1 帧”。
- 截图：`logs/claude-nr-reuse-20261107/ui/window-half.png`（阶段耗时）、`item-list-nr-stage.png`（三张卡在 NR 画面增强内，与 NR 层并列）。
- “输出稳定器”和“NR 全局保护区域”的 `shield` 图标原本不在图标库里，一直显示空白方块。已在 `prototypes/ui-redesign-2026-09-25/icons.js` 补上 Lucide shield，并用 `tools/qt_probe/make-icons.py` 重新生成 `qml/Veyra/IconData.js`，只多了这一项。
- 新单测 33/0、GPU 7/0、QtQuick 172/0/1。


- **改名与位置**：卡片改名“NR 自动推理复用”，与“输出稳定器 · 抗闪烁”“NR 全局保护区域”一起挪进“NR 画面增强”。三者是 NR 一级下的全局选项，与 NR 层卡片并列，排在“添加 NR 层”按钮之后；“处理顺序”点击保护区域时会先展开 NR 卡。
- **状态文字**：不再显示“已复用 N 帧”。长时间播放会到几百万帧，没有意义；只保留“自动 · 每 N 帧复用 1 帧”或“每 N 帧复用 1 帧”。
- **底部运算时间随复用实时变化**：
  - 原因：阶段耗时行显示的是“执行过该阶段的帧”的平均。复用帧不跑 NR，不进统计，所以 NR 行一直停在单次推理的耗时，看起来没效果。
  - 改法：复用帧给每层 NR 和 NR 行各记一个零长度的 GPU 计时样本。阶段行的平均值因此变成“平均每个源帧花在 NR 上的时间”，例如 1/2 复用时减半；悬停提示里的 P95 仍是单次推理耗时。
  - 零长度样本落在残差合成区间内，不改变整帧增强耗时的并集计算。自动档估算单次推理成本改用各层 P95。

## 改动文件

| 部分 | 文件 |
|---|---|
| 设置 | `include/veyra/engine/EnhancementSettings.h`：`NrReuseMode`、`nrReuse`、校验、NR 关闭时归一化 |
| 描述 | `include/veyra/engine/GraphDescription.h`；`include/veyra/pipeline/EnhanceGraph.h`（`nrReusePreview`、`nrReuse`、访问函数、计数） |
| 调度 | `include/veyra/pipeline/NrReuse.h`（`NrReuseScheduler`） |
| GPU | `include/veyra/pipeline/NrReusePass.h`、`src/pipeline/NrReusePass.cpp`、`shaders/NrReuseCompose.hlsl`、`shaders/NrReuseWarp.hlsl`，并复用 `FlowAdapt.dxil` |
| 图 | `src/pipeline/EnhanceGraph.cpp`：建 pass、生效条件、逐帧决策、跳过 NR 链、复用合成、存关键帧、串接运动、稳定器运动与间隔、实时改档、显存预估、释放 |
| 引擎 | `include/veyra/engine/AutoNrReuseController.h`；`src/engine/EngineController.cpp`（仅预览标记、自动控制、状态、自动尺寸准入）；`src/engine/EffectChain.cpp`（开关重建）；`src/engine/PreviewGpuSession.cpp` 与 `include/veyra/engine/RecentGraphCache.h`（预热、COMPUTE、最近图不收复用） |
| 界面 | `include/veyra/ui/QmlPlayerBridge.h`、`src/ui/bridge/Enhancement.cpp`（`nrReuseMode`，存偏好 `nrReuse`）、`src/ui/QmlPlayerBridge.cpp`（启动恢复）、`src/ui/bridge/Playback.cpp`（状态文字）、`qml/Veyra/ProPage.qml`（列表模式“自动推理复用”卡片） |
| 翻译 | `i18n/catalog.json`，新增 27 条 |
| 构建与测试 | `CMakeLists.txt`；`tests/unit/NrReuseTests.cpp`；`tests/integration/NrReuseGpuTests.cpp` |

## 验收

见本任务回执 `E:/项目/Veyra/reports/claude-nr-reuse-20261107/FINAL.json` 与本文件末尾的执行记录。

## 执行记录

本机 RTX 5070，驱动 32.0.16.2101，320 Hz 屏。

**构建**：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task claude-nr-reuse-20261107 --root E:/项目/Veyra/worktrees/claude-nr-reuse-20261107 --label 2.1.3-nr-reuse-test --jobs 2 <targets>`

- 首次配置 223.4 秒；
- 之后均为增量构建，逐次日志在 `logs/claude-nr-reuse-20261107/build-*`。

**新测试**：

| 测试 | 结果 |
|---|---|
| `veyra_nr_reuse_tests`（不用显卡） | 33/0。覆盖：<br>• 1/2、1/3、1/4 的复用模式；不会连续复用两帧；<br>• 复位；只在复用前一帧存关键帧；<br>• 自动档升、降与不抖；<br>• 设置校验、NR 关闭时归一化、开关重建、实时切档、仅预览标记 |
| `veyra_nr_reuse_gpu_tests`（D3D12 调试层） | 7/0。覆盖：<br>• 零运动；<br>• 平移跟随：内部 +0.2001，露出区 0；<br>• 内容对不上时原样显示源；<br>• 推理帧后的运动与光流逐位相同；<br>• 链式运动 −2−3=−5，NR 尺度下 −10；<br>• 调试层 0 错误 |

**回归**：

| 测试 | 结果 |
|---|---|
| QtQuick 全套（软件绘制） | 172/0/1，跳过的是需要专门产物目录的像素对比 |
| effect_chain | 通过 |
| preset_library | 通过 |
| repair_preset | 通过 |
| nr_model_settings | 0 失败 |
| nr_antiflicker | 0 失败 |
| repair_contract | 300/0 |
| ui_i18n | 0 失败 |
| nr_hold_gpu | 9/0 |
| settings_transaction（在运行目录内） | passed=1，调试层 0 |
| amd_nr_graph（在运行目录内，假 lmxxf） | 128/0 |

- 翻译 `extract.py --check`：missing 0。
- 构建目录里直接跑时：settings_transaction 缺 `ngx-local.json`，amd_nr_graph 缺 FidelityFX 运行组件，这是环境问题；放进运行目录后通过，失败日志保留。

**真实程序**：运行目录 `tests/claude-nr-reuse-20261107/app`，用正式 2.1.3 NVIDIA 包，换上本分支的 exe、着色器和 QML。

- 每次用独立配置、TEMP、日志和 QML 缓存根目录；
- 探针 QML 负责加 NR 层、设模式、暂停和截卡片图；
- 脚本 `reports/claude-nr-reuse-20261107/nr_reuse_run.py`。

预览的 NR 推理最大 1080p（选的尺寸只用于导出），所以单层 NR 在本机跟得上。为了制造超载，用了 4 层 NR。

| 场景（4K60 HEVC q60.mp4） | 平均增强耗时 | 跳帧 | 说明 |
|---|---:|---|---|
| 1 层，关 | 4.49 ms | 0 | 没有任何 nr-reuse 记录，关闭路径不变 |
| 1 层，手动 1/2 | 3.11 ms（细节拆分后 3.17 ms） | 0 | 状态“每 2 帧复用 1 帧”，每秒约 +30 |
| 4 层，关 | 15.24 ms | 4 秒内 1→3 | 超过 13.33 ms 预算 |
| 4 层，自动 | 15.07 → 11.64 ms | 停在 2 | 约 2 秒升到每 4 帧复用 1 帧后稳定，不再升也不降 |

- **实时切档**：720p30 平移片段，依次自动 → 1/2 → 1/3 → 1/4 → 关。
  - 每秒复用约 15、10、7.5 帧；切档不重建；
  - 关闭时重建图约 0.35 秒，之后状态清空；
  - 0 错误。
- **暂停**：1/2 模式下暂停 6 次。4 次落在复用帧上，都立即完整推理重绘；2 次落在推理帧上，不需要重绘。
- **界面**：卡片三种状态的截图在 `logs/claude-nr-reuse-20261107/ui/`，文字与实际状态一致。
- **启动脚本**：`tests/claude-nr-reuse-20261107/启动推理复用测试.cmd` 冒烟通过：独立配置，6 秒后正常退出，没有残留进程。

**画质**：离线对比工具 `veyra_nr_reuse_quality_probe`。同一组解码帧先按每帧推理跑一遍，再按 1/2 复用跑一遍，逐帧比较 8 位 sRGB 输出；图像在 `logs/claude-nr-reuse-20261107/quality/`。

| 素材 | 复用帧平均差 | PSNR | 差值 >8 的像素 | 复用后推理帧的平均差 | NR 本身的平均改动 |
|---|---:|---:|---:|---:|---:|
| 720p 真实游戏平移 | 0.70 | 45.7 dB | 0.13% | 0.58 | 4.19 |
| 1080p 合成测试卡 | 2.09 | 34.4 dB | 2.17% | 1.73 | 39.4 |
| 4K60 合成测试卡 | 1.98 | 34.2 dB | 1.93% | 1.53 | — |

- 真实画面的差异图几乎全黑，只有人物弓箭边缘有淡轮廓，右侧新露出的一列退回原图。
- 测试卡的差异集中在两处：每帧随机的噪点棋盘（新噪点只有推理才能处理），以及移动细线和方块的边缘。
- 加入细节拆分后，三项平均差都小幅下降，真实画面没有变差；4K 下复用合成的 P95 从 0.52 ms 升到 0.85 ms。
- 推理次数都减半，调试层 0 错误。

**未验证**：

- 其他显卡（尤其 30/40 系、笔记本卡）；
- HDR、节点模式、NR 分在超分前后、AMD NR：都按设计拒绝，未测；
- 真实采集卡 4K60；
- 长时间播放；
- 物理显示延迟；
- 用户人工看画面的结论。
