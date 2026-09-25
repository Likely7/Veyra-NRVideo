# QML 界面迁移执行记录

分支：`codex/ui-qml-migration-20260925`
起点：main `df41580`，标签 `checkpoint/pre-ui-qml-migration-20260925`
方案：[`UI_MIGRATION_MASTER_PLAN_2026-09-25.md`](UI_MIGRATION_MASTER_PLAN_2026-09-25.md)
产物目录：`E:\项目\Veyra\build|tests|logs\ui-qml-migration-20260925`

每一步完成后打标签 `checkpoint/ui-mig-<步骤>`。回退方法：`git reset --hard <标签>`（在本分支上操作），
或从标签新开分支。

## 进度表

| 步骤 | 内容 | 状态 | 标签 | 说明 |
|---|---|---|---|---|
| S0.1 | 建分支、更新文档 | 完成 | `checkpoint/ui-mig-s0.1` | |
| S0.2 | 构建与现有测试基线 | 完成 | `checkpoint/ui-mig-s0` | 600/600 目标构建通过；交付门槛 PASS |
| S0.3 | 抓帧哈希基线 | 完成 | `checkpoint/ui-mig-s0` | 13 种配置 × 24 帧，两次运行完全一致 |
| S0.4 | 性能 / 延迟基线 | 完成 | `checkpoint/ui-mig-s0` | 4 种播放场景 |
| S1.1 | 合并图描述构建 | 完成 | `checkpoint/ui-mig-s1.1` | 6 处合并为 `describeStages()`；13 项哈希一致 |
| S1.7 | 快照补字段（宽高比 / 时长 / 封面） | 完成 | `checkpoint/ui-mig-s1.7` | 宽高比与时长实测正确；关键哈希一致 |
| S1.2+S1.3 | 效果链模型与效果注册表 | 完成 | `checkpoint/ui-mig-s1.2` | 40 项单元检查全过；引擎尚未改用它 |
| S1.4 | 重建判断改由链推导（影子比对→切换） | 完成 | `checkpoint/ui-mig-s1.4b` | 影子 0 不一致后切换；13 项哈希一致，门槛 PASS |
| S1.5 | 导出携带效果链 | 完成 | `checkpoint/ui-mig-s1.4` | 共享内存版本 2→3，接收端反算校验 |
| S1.6 | 统一预设库 | 完成 | `checkpoint/ui-mig-s1.6` | 33 项单元检查全过；旧文件只读导入 |
| S1.8 | 界面接口层 `PlayerUiFacade` | 完成 | `checkpoint/ui-mig-s1.8` | 快照按差异推送、命令排队、预设与最近文件集中 |
| S2.0 | NR 叠层可行性探针 | 完成 | `checkpoint/ui-mig-s2.0` | **4/6/8 个句柄全部成功** |
| S2.1 | NR 叠层实现 | 完成（已修 G1） | `checkpoint/ui-mig-s2.1b` | 13 项原有哈希逐像素一致；1/2/3/4 层均为真实图像 |
| S2.1b | NR 时域防闪烁修复 | 完成 | `checkpoint/ui-mig-s2.1b` | 单层路径不变；叠层下开关由“完全无效”变为生效 |
| S3.0 | Qt 6.8.3 安装 + 承载 D3D12 窗口实测 | 完成 | `checkpoint/ui-mig-s3.0` | 499/499 Present 成功，平均 0.205 ms，QML 动画未被拖慢 |
| S3.0b | （旧行）Qt 6.8.3 安装 | 完成 | | `E:\项目\Veyra\deps\qt.8.3\msvc2022_64`，含 Core/Gui/Qml/Quick/QuickControls2 + windeployqt |

## 发现并修复的 bug

| # | 发现于 | 问题 | 修复 |
|---|---|---|---|
| B1 | S0.2 | `veyra_live_timing_tests` 的“输出上限仍会跳过候选帧”断言一直失败：09-22 把输出上限改到独立时间网格（`rateSubmitted`）后，测试只调用了 `submitted()`，测的是旧行为。产品逻辑正确，测试过期 | 测试补上 `rateSubmitted()` |
| B3 | S1.1 | 非 NVIDIA 显卡上，`initializePreview` 的能力归一化分支把 `enableSr` 直接写成 false，AMD FSR 超分（唯一的跨厂商超分）在预览里永远不生效，而 `disableUnsupportedNvidiaEffects` 是特意保留它的。导出路径没有这个问题 | 5 处描述合并进 `describeStages()`，两边同一套规则 |
| B4 | S1.7 | 新建的 `PosterFrame.cpp` 直接把 FFmpeg 头文件放在 C++ 作用域里引用，`sws_*` 按 C++ 名字改编，链接必然失败（“无法解析的外部符号”）。项目里其它 FFmpeg 使用者都用 `extern "C" {}` 包住，新文件漏了 | 用 `extern "C" {}` 包住三个 FFmpeg 头；同时确认 `veyra_engine` 不需要额外链接 FFmpeg 库 |
| B2 | S0.2 | `veyra_quality_probe` 用 ANSI `argv` 转宽字符，输出目录含中文（`E:\项目`）时写图失败，交付门槛因此失败；门槛脚本原先用相对路径绕开，但源码在 C:、日志在 E: 时相对路径无法跨盘 | 探针改为从 UTF-16 命令行读参数；门槛改传绝对路径 |

## 日志

### S3.0（2026-09-25）Qt 承载 D3D12 视频窗口 —— 可行性已实测通过
这是整个迁移**唯一真正的技术风险**：视频由一个原生子 HWND 承载，Veyra 为它创建 D3D12 flip-model swapchain。如果 Qt 接管了呈现，低延迟优势就没了。已用最小探针实测，不再靠推断。

探针：`tools/qt_probe/main.cpp`（工具 `veyra_qt_probe`），QML 窗口里放一个持续旋转的矩形（模拟真实界面动画），同时按 ~120 Hz 上限向原生子窗口 Present，计时每个 Present 并统计 QML 动画帧数。

**本机实测（RTX 5070，100 Hz 面板，Qt 6.8.3）**
| 指标 | 结果 |
|---|---|
| Present 次数 | 499 次，**失败 0** |
| 平均 Present 耗时 | **0.205 ms** |
| Present 期间 QML 动画帧 | 402 帧 / 4 秒 ≈ 100/s，**正好是面板刷新率，未被拖慢** |

**结论**：Qt 可以承载原生 D3D12 呈现窗口，**不需要** `QQuickWidget`、`QQuickRenderControl` 或任何把视频合成进 QML 场景图的方案。视频仍是原生子窗口、自己的 swapchain、自己的 Present 调用；QML 只负责它周围和上面的界面层。低延迟路径得以保留。计划中原定的 `WindowContainer` 方案（Qt 6.7+ 支持，原生窗口始终绘制在 QML 场景之上）成立。

**过程中修掉的两个真问题**
1. `EngineController.cpp:1455`：同一个 lambda 无条件捕获 `&remote`，而 `remote` 只在 `VEYRA_ENABLE_REMOTEPLAY` 打开时声明 ⇒ **关闭 remoteplay 的配置根本编译不过**。已把该捕获放进同一个宏。这不是 Qt 引入的，是既有配置组合没人编译过。
2. Qt 部署：`windeployqt` 不扫描 QML import，QML 模块要单独拷贝；`qtquickcontrols2plugin.dll` 还需要 `bin\*.dll` 里的 Qt 库才能加载。已固化为文档步骤。

**未完成 / 不夸大**：以上只证明“承载可行 + Present 不被拖慢”。**没有**验证：QML 界面本身的渲染开销、窗口缩放/移动、多显示器与 HDR 输出、真实播放负载下的端到端延迟。这些要在 S4 用真正的播放器界面复测。探针只调用 Present，不画内容，所以它的 0.205 ms 是**呈现开销**，不是端到端延迟。

代码：`tools/qt_probe/main.cpp`、`scripts/build-qt-probe.{ps1,cmd}`（独立构建目录，产品构建树未被 Qt 重配置）。

### 单元测试基线口径（2026-09-25 核对，重要）
本仓库**当前就有大量单元测试在本机失败**，且与本次迁移无关。对照 S0（迁移开工时，16:14）与当前（21:30）两次完整跑：
- S0：pass=40 fail=35 skip=3；当前：pass=44 fail=33 skip=3。
- **回归：0 项**（S0 通过、现在失败的测试一个都没有）。
- 新增 2 个测试（`veyra_effect_chain_tests`、`veyra_preset_library_tests`），均通过。
- 新增通过 4 项：2 个新测试 + `veyra_live_timing_tests`（B1 修好的陈旧断言）+ `veyra_source_tests`（夹具路径修好）。
- 剩余失败项在 S0 就同样失败（多为缺采集卡、缺录音端点、缺语料或本机运行时依赖），**不得**把通过数上升当成“全绿”，也不得把历史失败误报为本次回归。

顺带修掉一个测试环境问题：若干契约测试用**相对路径**打开固定语料（`loop/local/fixed_clips/corpus/...`），而单元脚本的工作目录是暂存后的 app 目录，于是报“corpus missing”。修法是把 `loop/` 夹具树暂存到 app 目录旁边（`run-unit-ui-migration.ps1`），**不改这些测试**。`veyra_source_tests` 因此由 fail 转为 pass。

### S2.1b（2026-09-25）NR 时域防闪烁修复（用户报告“好像没什么用”）
用户反馈的“时域防闪烁”（1.4.4 新增，默认关闭）经实测确有两个真实缺陷，且**不是阈值调参问题**：

**G1 — 叠层 NR 输出全黑（严重）。** 残差合成 pass 只声明了 1 个 UAV，于是每一层的合成都通过同一个描述符槽写入，而它后面的 barrier 命名的是另一张纹理 ⇒ 第 1 层起读到的是**从未被写过的纹理**。
- 这是我上一轮 S2.1 的误判：我当时只看到 1/2/3/4 层哈希两两不同就宣布通过。实际上那些是**逐深度的黑帧**（黑帧之间也有细微差异，因为未初始化显存内容不同）。哈希证明“输出变了”，不能证明“输出是对的”。
- 现在残差 pass 每层一个输出描述符（`srvCount=3L, uavCount=L`）。
- **教训已写入本文件**：叠层类改动的验收必须包含“输出是真实图像”的检查（均值/标准差/最值），不能只看哈希互异。

**G2 — 时域防闪烁在叠层链里完全无效。** 初始化写成 `nrLayerCount_==1 && desc_.nrTemporal`，只要 NR 层数 > 1，pass 就从未被初始化，开关点了等于没点（这也解释了为什么在不同机器/不同链条上反馈不一致）。
- 现在**每层**按自己的开关初始化，输出写该层自己的全分辨率目标（`fullTarget()`），下一层读到的就是稳定后的图像。
- 初始化必须写在描述符写入**之前**：`initialize()` 才分配描述符所命名的 `raw` 纹理。顺序写反过一次，表现为输出被压平（std 从 90 掉到 2.5）、且轻微拖影。

**实测（本机 RTX 5070，1920×1080，同一测试片段，12 帧）**
| 配置 | 帧间平均差（越低越稳） | 末帧图像标准差 |
|---|---|---|
| 单层 NR | 2.5398 | 90.19 |
| 单层 NR + 时域 | 3.0590（作用于约 93% 像素） | 90.47 |
| 双 NR 叠层 | 3.2603 | 84.25 |
| 双 NR 叠层 + 时域 | 3.8720 | 86.24 |

- 叠层深度越大细节越低（std 90.2 / 84.3 / 80.0 / 77.1），符合“多级降噪”的预期，不再是黑帧。
- 时域防闪烁确实在改画面（约 93% 的像素、平均改动 3–8 灰阶），且**没有把图像压平**（std 基本不变）。它抑制的是时间方向的不稳定，不是空间细节。
- 图像资源预算改为按层数计算：叠层每层一整套纹理，之前只算了一份；深层链会先通过预算检查、再在分配时失败。

**未完成 / 不夸大**：以上均为本机软件短测。用户报告的场景是**采集/实时**链路（`EngineController` 的掉帧复位启发式），与本次修的两处不同；本次**没有**动那部分逻辑。因此不能声称“用户看到的闪烁已解决”。下一步应让用户在实际采集场景下复测 G2 修复后的开关，并单独排查 `EngineController.cpp` 的 history-reset 启发式（该处已有 3 次修补：`830ce61`、`6905368`、`d9da384`）。

### S2.1（2026-09-25）NR 叠层实现
- 新增 `include/veyra/pipeline/NrInstance.h` + `src/pipeline/NrInstance.cpp`：**每层 NR 自有一整套资源** —— 编码 proxy、神经输出、解码结果、残差暂存、自己的输入纹理，以及自己的 `NrTemporalPass` 时域历史。这与 Magpie `DLSSNRMultiPass` 的结构一致（一个片段会话 + 每 pass 独立句柄）。
- `EnhanceGraph` 由单实例改为 `nrInstances_` 列表：`initNgxFeatures()` 为每层创建一个句柄和参数块；`process()` 按层依次「降采样 → 编码 → 求值 → 解码」；残差合成也逐层执行，**第 k 层合成到第 k-1 层的结果上**，最后一层写入图自己的残差纹理（后续 blit 一行未改）。
- 描述符槽位按「SRV 全部在前、UAV 全部在后」重算：编码 `SRV k / UAV L+k`，解码 `SRV 3k..3k+2 / UAV 3L+k`，降采样 `SRV 2k / UAV 2L+2k`，残差 `SRV 3k..3k+2 / UAV 3L`。这是本步最容易出错的地方，改完由逐像素哈希兜住。
- **单层路径保持逐字节不变**：单层时第 0 层直接借用图自己的输入纹理（`inputIsBorrowed()`），跳过降采样，与原实现完全一致。实测 13 项原有哈希全部 `same`。
- 每层参数在建图时从描述播种（`desc_.nrLayersModel` 等）；`applySettings()` 的实时更新仍然只改第 0 层并给后续层**递增 `inputRevision`**，让它们丢弃失效的时域历史 —— 对应 Magpie 的 inputRevision 契约。
- 抓帧探针新增 `nr2`/`nr3`/`nr4` 三种叠层用例（每层强度 1.0 / 0.8 / 0.6 / 0.4，刻意选可观测的差异值）。实测：**1、2、3、4 层输出两两不同**，证明链路真的在叠加而不是只跑第一层。
- 踩到并修掉的两个自身 bug：(1) 早期版本每层都在同一条命令列表上继续录，导致 `acquireNext` 失败（slot-ring E_FAIL）；(2) 每层参数没有在建图时播种，导致所有层都用默认值、叠层输出与单层相同。
- 期间还修了一个测试环境问题：多个残留的 `veyra.exe` 进程占住测试目录里的可执行文件，导致拷贝失败、误判为代码回归。

### S2.0（2026-09-25）NR 叠层可行性探针（结论：可行）
新增 `tools/nr_probe/main.cpp`（目标 `veyra_nr_probe`）：在一个 NR 片段会话上创建 N 个 Feature 18 句柄，各自绑定独立的色彩/光流/深度/输出纹理并逐句柄执行，最后全部释放。

本机 RTX 5070 + `nvngx_dlssnr.dll` 310.8.0.0（NVIDIA 原版）实测：

| 请求实例数 | 创建 | 执行 | 释放 | 结果 |
|---|---|---|---|---|
| 2 | 2 | 2 | 2 | 成功 |
| 4 | 4 | 4 | 4 | 成功 |
| 6 | 6 | 6 | 6 | 成功 |
| 8 | 8 | 8 | 8 | 成功 |

结论：**一个片段会话 + 每层独立句柄/参数块/纹理 的结构在本机可用**，与 Magpie（`DLSSNRMultiPass.h`：每个 pass 各自 `CreateFeature`）的做法一致。界面里“最多 4 层”因此是产品选择，不是运行时限制。

未验证：社区兼容版与 RTX 30 兼容版的多种子；每层的显存与 GPU 耗时叠加；多层时域历史的画质表现。这三点在 S2.1 实现后单独测。

### S1.4 切换 + S1.8（2026-09-25）重建判断切换与界面接口层
- **S1.4 切换**：影子模式跑过 6 个 settings 类 smoke 用例（master / settings / fg-only / view / output-cap / rollback）确认 0 处不一致后，`EngineController` 改用 `requiresGraphRebuild()`，旧的手写字段判断已删除。仍保留三类**设置本身表达不了**的条件：有效 HDR 输出（取决于显示器）、各阶段实际尺寸（来自 `ResolutionPlan`）、非 NVIDIA 显卡的能力归一化。
  - 重新验证：13 项抓帧哈希 0 差异；交付门槛 PASS；smoke 用例按各自正确参数复跑全部通过（`--smoke-fg-only` 必须不带 `--nr` 启动、`--smoke-output-cap` 需要帧率值、`--smoke-rollback` 需要 `VEYRA_TEST_REJECT_NR_STYLE2=1`，我最初传错了参数，不是产品问题）。
- **S1.8 `PlayerUiFacade`**（`include/veyra/ui/PlayerUiFacade.h` + `src/ui/PlayerUiFacade.cpp`）：
  - `poll()` 只在上次拿到的快照真正变化时才递增 `revision`，界面据此避免整份重刷（取代原来 100/250 ms 固定轮询后无条件重绘）；
  - 命令走 `applySettings()`，同时保留界面自己的“待应用”副本，界面可以在引擎确认前就显示用户刚点的值；
  - 预设库、最近文件（上限 8 条，带存在性刷新）、上次采集会话（含设备、格式、预设名、帧率）集中在 Facade，并原子写入 `ui-session.v1`；
  - 旧预设迁移 `importLegacyStores()` 走 `PresetStore` 自己的解析器（不重写一遍位置式 schema），重名一律跳过，旧文件保持不动，方便降级。
  - 这一层不依赖 Win32 也不依赖 Qt，S4 的 QML 前端和现有 Win32 外壳可以共用。

### S1.6（2026-09-25）统一预设库
- 新增 `include/veyra/engine/PresetLibrary.h` + `src/engine/PresetLibrary.cpp`：一个存储放全部预设，每个预设用 `contents` 掩码声明自己包含哪些部分（画质链路 / 调色 / 补帧 / 音频偏移），**这正是设计稿里“另存为时勾选要保存的部分”**。
- 列表预设与节点预设用 `kind` 分开，互相不串。
- 内置四个预设（原画 / 流畅 / 均衡 / 极致）；内置只读：不能改名、不能删除，但可以复制后改。
- 管理操作：另存（重名可覆盖，覆盖内置会被拒）、改名、复制、删除、设为启动默认。
- `apply()` 只覆盖预设声明包含的部分，其余字段原样保留 —— “只存链路”的预设不会顺手清掉用户的调色。
- `importLegacy()` 用于把旧的 `user-presets.v1` / `nr-presets.v1` 导入：重名的一律跳过，不覆盖用户已有预设。
- 文件格式 `VEYRA_PRESET_LIBRARY 1`，原子写（临时文件 + `MoveFileEx`），损坏文件保留原文并拒绝覆盖。
- 新增 `veyra_preset_library_tests`（33 项检查）：内置只读、部分内容保存与套用、完整往返（含每层 NR 参数与节点模式标记）、管理操作与默认项持久化、旧文件导入不覆盖、损坏文件保护。
- **注意**：这一步只是把库建好，界面还没接上；现有 Win32 界面继续用旧的 `PresetStore`。

### S1.4 + S1.5（2026-09-25）重建判断与导出链路
- S1.4：`EngineController` 现在同时计算旧的字段式判断和新的 `requiresGraphRebuild()`，不一致就写 `settings-rebuild` 警告日志，但**仍按旧判断执行**。跑 6 个 settings 类 smoke 用例（master / settings / fg-only / view / output-cap / rollback），0 处不一致。确认后再切换、再删旧判断。
- S1.5：导出的共享内存头从版本 2 升到 3，多带一份 `EffectChain`；工作进程收到后会把链反算回设置并与头部里的设置逐字段比对，不一致直接拒绝任务，避免读到错版布局。

### S1.2 + S1.3（2026-09-25）效果链模型与效果注册表
- 新增 `include/veyra/engine/EffectChain.h` + `src/engine/EffectChain.cpp`：
  - `EffectType` 六种（调色、超分辨率、NR、保护区域、RTX Video HDR、补帧），每种在 `effectCatalog()` 里登记名称、实例上限（调色 6、NR 4、其余 1）、是否可重复、是否必须最后、是否改变分辨率、是否实验；
  - `ChainNode` 每节点带自己的载荷（NR 的参数与运行版本、保护区域、调色、Video HDR），NR 的每层参数独立；
  - `EffectChain` 固定容量 16 节点 + `nodeCount` + 模式（列表 / 节点）+ 补帧倍率，整体可平凡复制（导出共享内存要求的静态断言仍然成立）；
  - `validateChain()`：数量上限、补帧只能最后、**RTX Video HDR 锁死在补帧前面**（用户 2026-09-25 决定，两种模式都不能拖动它；它和补帧之间不允许存在其它启用节点，它也不能排到补帧之后）；
  - `toChain()` / `fromChain()`：与 `EnhancementSettings` 双向转换，转换不会碰采集、音频、导出等非阶段字段；
  - `requiresGraphRebuild()`：把“哪些变化要重建管线”从 `EngineController` 里 25 个字段的手写“或”判断收进一处。
- 这一步只是把结构立起来，**引擎仍然使用 `EnhancementSettings`**，行为一字未改；`applySettings` 里原本的拒绝规则继续生效。
- 新增 `veyra_effect_chain_tests`（34 项检查）：往返转换保真、禁用阶段保持禁用、非阶段字段不被改动、列表顺序、补帧必须在最后（含“后面只有禁用节点时允许”）、HDR 之后只能接补帧、NR 层数上限、以及重建判断的正反例。
- 下一步 S1.4 会让引擎改用 `requiresGraphRebuild()`，并先与旧判断并行比对后再切换。

### S1.7（2026-09-25）快照补字段
- `SourceInfo` 新增 `displayAspect`（应用容器 SAR 与旋转后的显示宽高比）与 `rotationDegrees`；`FFmpegDemuxer` 增加 `sampleAspect()` / `rotationDegrees()`（读 `sample_aspect_ratio`、`rotate` 元数据和 `AV_PKT_DATA_DISPLAYMATRIX`，用 FFmpeg 6+ 的 `codecpar->coded_side_data` 接口）。
- 四种源都填了这个字段：文件按 SAR+旋转计算；采集卡与屏幕捕获按格式尺寸；PS5 按请求尺寸或实际解码尺寸。原先完全没有宽高比信息，变形宽银幕片源会算错窗口大小。
- `PlayerSnapshot` 新增 `sourceWidth/sourceHeight/sourceDisplayAspect/sourceRotationDegrees`（打开时即可用，不必等第一帧）和封面帧 `posterRgba/posterWidth/posterHeight`（新增 `src/engine/PosterFrame.cpp`，swscale 缩到最大 320×180，只在打开时算一次，失败就留空，不伪造）。
- 抓帧探针现在同时输出源尺寸、宽高比、旋转和时长，便于以后回归。
- 验证：1080p 测试片读出 `displayAspect=1.7778 rotation=0 duration=60.000`；13 项中的 5 项抽查哈希全部一致；完整构建通过。

### S1.1（2026-09-25）合并图描述构建
- 新增 `include/veyra/engine/GraphDescription.h`：`StageRequest` + `describeStages()`，把原先分散在 `EngineController.cpp`（初始打开、能力归一化、初始化失败降级、设置重建、串流尺寸切换、增强故障恢复共 6 处）和 `VideoExportJob.cpp` 的 `EnhanceGraphDesc` 填充合并成一处；尺寸策略、NR/超导顺序、厂商能力判断、参数下发全部由它决定。
- 各处的差异是历史遗留，不是有意区分：导出路径用 `Native` 尺寸、禁用 NR 先行、不走呈现端补帧，这些用 `StageRequest::exportJob` 表达；图片用 `stillImage` 表达。
- 验证：13 项抓帧哈希全部一致（0 差异）；延迟对比无退化（1080p NR 提交 P95 0.92→0.99 ms、GPU 就绪 8.09→8.42 ms，均在本机噪声内，帧率与跳帧计数不变）；交付门槛另行复跑。

### S0（2026-09-25）
- 构建：`scripts/build-ui-migration.ps1`，沿用 1.4.4 发布配置，输出 `E:\项目\Veyra\build\ui-qml-migration-20260925`。`.cmd` 按 ANSI 解析，中文路径由 PowerShell 用环境变量传入。
- 测试目录：`scripts/stage-ui-migration.ps1` 复制构建产物到 `E:\项目\Veyra\tests\ui-qml-migration-20260925\app`。运行组件用 junction 指向 1.4.4 发布包（7 个文件 SHA-256 与 manifest 全部一致），`runtime_local` 为测试专用新目录，不碰用户设置。
- 交付门槛：`scripts/gates/delivery.ps1` PASS，`E:\项目\Veyra\logs\ui-qml-migration-20260925\s0-delivery\4acefbbfd4744c64bc274e2c9181e41e\result.json`（修复 B2 后）。
- 单元测试：`scripts/run-unit-ui-migration.ps1` 无参数直接运行 75 个测试程序，40 个通过，其余 35 个需要参数或素材（用法退出码 2），不作为基线判据；基线判据为交付门槛与下面两项。
- 抓帧哈希：新增 `veyra_chain_hash_probe` 与 `scripts/hash-ui-migration.ps1`。13 种配置（直通、NR、NR 风格 2、残差、保护区域、时域防闪、DLSS SR、RTX VSR、SR→NR、NR→SR、VSR→NR、调色、调色+NR）× 24 帧，CPU 解码，逐帧 SHA-256。两次运行 0 差异，13 个末帧哈希互不相同。基线 `E:\项目\Veyra\tests\ui-qml-migration-20260925\hash-baseline.json`。
- 性能 / 延迟：新增 `scripts/perf-ui-migration.ps1`（真实播放器 8 秒，取 `[player-timing]` 各 P95 均值与 `[frame-flow]` 计数）。基线 `perf-baseline.json`：
  - 1080p NR：提交 P95 0.92 ms，GPU 就绪 P95 8.09 ms，NR 5.78 ms，60 fps，跳帧 0
  - 1080p NR + 2X：提交 1.28 ms，GPU 就绪 9.41 ms，120 fps，跳帧 0
  - 4K NR + 2X：提交 1.38 ms，GPU 就绪 12.46 ms，120 fps，跳帧 0
  - 1080p 无增强：提交 0.13 ms，GPU 就绪 1.57 ms
