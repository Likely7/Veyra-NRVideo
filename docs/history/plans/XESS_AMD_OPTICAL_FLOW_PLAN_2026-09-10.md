# XeSS 补帧与 AMD 光流接入方案

> 当前续接：[后端切换、真实帧率与音画同步修复方案](CONTINUATION_REPAIR_PLAN_2026-09-10.md)。XeSS预览2X与AMD光流已有代码和RTX5070短测；XeSS实际UI可见性/切换、真实输出口径仍待修复，AMD NR本体未接入。本文早期“仅研究”与Phase顺序只代表当时状态，不代替当前验收。

日期：2026-09-10。状态：研究完成；已有窄范围产品接入，仍处于 Phase 7 `in_progress`。本文不改变阶段门槛或默认处理路线。

当前实现包含统一的 2K / 4K / 8K SR 目标、AMD FidelityFX 光流 provider 和 Intel XeSS-FG 代理交换链显示后端。RTX 5070 窄测记录了 AMD OF Create/Dispatch/Destroy 成功（48 次 dispatch），并以非周期纹理验证 canonical `current -> previous` 方向：源图右移 2 像素得到 `(-2, 0)`，左移 2 像素得到 `(2, 0)`；XeSS 初始化、资源标记和 Present 成功（43 次 SDK generated 计数，debugErrors=0）；SR 2K/8K 输出非黑。它们只证明该机器上的所测 SDK 调用链与 AMD OF 的已知平移方向，**不证明** AMD GPU 兼容性、实际扫描输出帧率、画质、8K 实时性或长时稳定性。

最新扩展：[2K / 8K 超分与 AMD NR 接入方案](SR_TARGETS_AMD_NR_PLAN_2026-09-10.md)。AMD 光流与 AMD 执行 NR 是两个不同模块：当前接入不提供 AMD DLSS5/NR、DLSS SR、NVIDIA FRUC、DLSS 帧生成、NVENC 或 AMD AMF 导出。该文保留 AMD NR 的后续研究路线，不能把已有光流实现写成 AMD DLSS5。

原始用户要求是研究截图中作者提到的“DLSS5 + XeSS FG，以 AMD 光流给前两者提供运动信息”。以下研究、接口合同和验收条件继续作为实现依据；其中“后续实施”段落应按当前已接入范围理解，尚未实际验收的部分仍需按证据完成。

## 1. 结论与推荐

这条路线有公开实现和官方接口，值得接成可选组合。Veyra 继续使用已有 NR 和超分，新增 **AMD FidelityFX 光流** 与 **Intel XeSS-FG 显示补帧**。它们都可以以 RTX 5070 为接入目标，不要求另外安装 AMD/Intel 显卡；实际初始化和输出仍须在本机验证。

推荐分开验证：先增加 AMD 光流，与 NVOF 在同一 NR 配置下对照；再增加 XeSS 2X，与现有 DLSS 2X、FRUC 2X 对照。保留现有后端和默认值，只有测到收益后再决定推荐组合。

必须提前明确的边界：

- 当前 Intel 官方文档明确规定：**非 Intel GPU 最多生成 1 个中间帧，即 2X 总倍率**。因此 RTX 5070 上的 XeSS 3X/4X 不列为本次可实现承诺。应用查询能力后决定选项，不因 Magpie 有 x2-x4 标记文件就开放所有倍率。
- XeSS-FG 的公开接口在代理交换链的 `Present` 中生成和调度帧，没有本次查得的公开“输出中间帧纹理到调用方”接口。首批支持采集预览和视频观看；视频导出的 DLSS/FRUC 路径保留，不把录屏包装成 XeSS 离线导出。
- AMD 光流运行在计算着色器上，可能与 NR/SR 争用 GPU。它不是把 NVIDIA 工作转移给另一张 AMD 显卡，也没有证据证明必然比 NVOF 快。
- 光流改善运动估计、XeSS 改变补帧与呈现方式；二者都不能直接消除固定分辨率下 NR 推理自身的成本。作者的综合体验不等于纯 NR 耗时下降。
- 15/30fps 输入可以作为实验场景，但 Intel 文档建议基础输入至少 40fps、推荐 60fps。该建议不是 API 硬拒绝 30fps 的证据，也不能被忽略后承诺所有 PS5 30fps 场景完美补帧。

## 2. 核查来源与可证实范围

| 来源 | 固定身份 / 状态 | 证实的内容 |
| --- | --- | --- |
| 用户截图 | “067内测”；没有帖子 URL 或内测构建身份 | 作者声称正在比较 DLSS5、XeSS FG 与 AMD 光流；不能确认 067 的全部修改或复现实测 |
| Magpie `experimental` | `ac1cc8b0f2efc78323898395cc1336bcbecdc276`，提交时间 2026-09-07T20:00:14Z | 已有 `XeSSFGPresenter`、`AmdOpticalFlowProvider`、共享 Guidance 和 AMD 档位；不必等待 067 才知道接入方法 |
| Magpie Releases | 本次 API 查询最新为 `v0.6.6-experimental`，2026-09-06 | 未查到公开 0.6.7 Release；不把公开代码冒充截图中的内测构建 |
| 本机 Magpie | `E:/Ai/mg/Magpie-Experimental-x64`，manifest 0.6.6 / `9824d758b162ad3c5b5acc81e2e14c83f138e13d` | 实际存在 XeSS FG 标记文件、`libxess_fg.dll` 1.3.1.78、`libxell.dll` 1.3.0.5、`libxess.dll` 2.0.2.68；本轮只读版本，未加载或复制 DLL |
| Intel XeSS 官方 `main` | `de0fb9c1c510661c571164e1418ceca8101dab69` | XeSS 3 SDK、FG guide 1.3、D3D12/C 接口、SM6.4 非 Intel 支持、XeLL 依赖、2X 上限 |
| Intel 官方 Release | `v3.0.2`，commit `8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0` | XeLL 更新为 1.3.2.10，修正非 Intel GPU 停止 Present 仍打 marker 的内存泄漏，以及帧限单位错误 |
| AMD 当前官方 SDK | `60f4ea81909200d8542eca14dccb2628b763a9a3`，仓库 README 为 FSR SDK 2.3.0 | 独立 Optical Flow context、D3D12 backend、GPU shader、8x8 block 输出、SCD 与对应文件许可证 |
| AMD 官方独立 OF 说明 | `v1.1.4` / `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` 内的 Optical Flow 1.1.2 文档 | 输入/输出、SM6.2、wave、msad4、7级金字塔；旧文档用于解释算法，不当成当前 SDK 所有 ABI 的真源 |

Intel `main` 对 v3.0.2 的差异只涉及部分头文件与 `inc/MIT.txt`；此次 compare 列表没有 FG DLL、FG 头文件和 FG guide 的变化。实施时优先锁定官方 v3.0.2 成套依赖，不能把本机 Magpie 的旧 XeLL 当最新组合。

公开源码/文档缓存位于 Git 忽略目录 `logs/xess-amd-research-20260910/{magpie,intel,amd}/`。永久链接见文末；后续 Agent 必须按固定提交核对，不能把更新后的 `main` 与本文证据混用。

## 3. Magpie 实际怎么接

### 3.1 “效果器”文件不是算法本体

`src/Effects/XeSSFG/XeSS_MultiFrameGeneration_ZeroMV.hlsl` 声明倍率、光流方式、档位，shader 本身只是返回输入采样。真正执行的是 `XeSSFGPresenter.cpp`，负责 XeLL、XeSS context、交换链、资源标记和 Present。

所以把这个 HLSL 放进 Veyra 只会得到一个透传 shader，不会增加 XeSS 补帧。需要实现 SDK 后端和呈现生命周期。

### 3.2 AMD 光流

公开 `AmdOpticalFlowProvider.cpp` 调用：

```text
ffxGetInterfaceDX12
  -> ffxOpticalflowContextCreate
  -> ffxOpticalflowGetSharedResourceDescriptions
  -> 每帧 ffxOpticalflowContextDispatch
  -> 稀疏 R16G16_SINT 光流稠密化和尺度换算
  -> motion / confidence 交给 Guidance 消费者
```

“性能”档把输入宽高各减半，处理像素数约四分之一；“质量”档为完整输入尺寸。返回消费者时按实际宽高比例缩放位移，不代表推理性能一定提高四倍。

它不是 NVIDIA NVOF 的 S10.5 格式：AMD 当前 block 位移不能照抄 `/32`。8x8 是网格间隔，也不能把向量直接乘 8。具体符号、整数位移和像素中心必须用已知平移确认。

公开实现的 AMD `DenseConfidence` 是固定 0.65，首帧/reset 清零，SDK 本身不返回 NVOF 那种 cost 表面。这个常数不是逐像素可靠性估计。Veyra 应保留已有回投亮度/出界校验，并为 AMD 单独构造置信度，不复制固定常数作为完成标准。

### 3.3 XeSS 补帧

公开实现创建 XeLL 并关联 XeSS，查询 `maxSupportedInterpolations`，以 `xefgSwapChainD3D12InitFromSwapChainDesc` 建立代理交换链。每个真实帧标记运动向量、平面深度、单位 view/projection matrix、reset 和 Present ID，再调用代理 `Present`。

它使用 `R32_FLOAT` 固定平面深度及外部光流，或者明确的 Zero-MV 回退，不是恢复游戏的真实深度/相机。可以作为 Veyra 的视频适配试验起点，不能称为官方视频输入方案或原生游戏等价。

Magpie 主体是 D3D11，因此增加 D3D11/D3D12 共享纹理、fence 和拷贝。Veyra 本身是 D3D12，可直接把同设备资源交给 SDK，省去新建这套跨 API 桥接。SDK 自身仍可能复制资源，不能因此宣传全链零拷贝。

## 4. 对照 Veyra 当前实现

基线 HEAD：`f6d24d3798e48328c1acc2e7af67b946eee7a7a5`。工作树带上一轮 UI 闪白修复，完整保留。

| 当前位置 | 现状 | 本次接入需要的变化 |
| --- | --- | --- |
| `src/pipeline/EnhanceGraph.cpp` | 真实产品图；直接持有 `NvOfSession` 并调用 NVOF、稠密化、置信度；光流取原始源空间颜色 | 把这块封为真实可切换的 motion provider，保留原路径回归；增加 AMD provider |
| `include/veyra/guidance/IGuidanceProvider.h` | 有设计接口，产品图没有通过它调用现有 NVOF | 不能只实现新类然后让 UI 假切换；补齐设备、command context、ready fence 后在产品图真正接通 |
| `shaders/NvofDensify.hlsl`、`shaders/FlowAdapt.hlsl` | 已有 NVOF cost、回投残差、出界处理及消费者尺度转换 | 保留 NVOF 编码转换；增加 AMD 专用稠密化/可靠性，复用尺度合同 |
| `include/veyra/pipeline/FrameBatch.h` | real/generated 色彩纹理 lease；没有对应每帧的 motion/depth lease | XeSS 必须获得该真实帧的不可变 guidance，不能呈现时读取 graph 的全局最新光流 |
| `src/engine/VideoPresenter.cpp`、`src/gfx/PresentSink.cpp` | 普通 DXGI 交换链；presenter 画缩放、黑边、缩放预览和比较 | 增加独立 XeSS 交换链实现；复用公共 blit/视口逻辑 |
| `src/engine/EngineController.cpp` | 采集两批次呈现 worker；文件按音频/PTS 调度；处理 FPS 已按 GPU 完成统计 | XeSS 模式只提交真实帧，SDK 负责子帧；避免旧子帧等待和 XeLL 重复限速 |
| `include/veyra/engine/EnhancementSettings.h` | FG 为 DLSS/FRUC，flow 为 NVOF 三档 | 增加后端枚举与独立 AMD 两档，保持旧预设含义；查询实际能力决定倍率 |

现有 flow 宽高由实时/原生 ResolutionPlan 决定，不总是完整源分辨率。新 UI/日志必须分别记录 source、flow、SR、NR、FG 插值区和交换链尺寸，不能把不同内部尺寸归为同一个“4K”性能结论。

## 5. 目标管线

```text
视频 / 采集 -> 原生颜色解析 -> 内容节奏处理
                         |
                         +-> NVOF 或 AMD OF -> 可靠性校验 -> 每帧 Guidance
                         |                                   |
                         +-> SR（可选）-> NR（可选）----------+
                                           |
                       +-------------------+--------------------+
                       |                                        |
                DLSS / FRUC 图内补帧                    XeSS 显示补帧
                -> 有界帧批次                           -> 真实帧与 Guidance lease
                -> 原有 Present / 导出                  -> XeLL + 代理交换链
                                                        -> SDK 生成并呈现
```

SR 仍在 NR 前，补帧仍在 NR 后；同一颜色与 NR 图供所有来源使用。增加显示 sink 不等于复制第二套 NR/SR 引擎。图片不做补帧。

### 5.1 AMD provider 的实施合同

1. 采用当前官方 SDK 中最小 Optical Flow + D3D12 backend + 所需公共 shader 子集，锁定同一提交；不引入 FSR4、完整示例引擎或额外神经模型。
2. 使用 Veyra 的 D3D12 device、队列和 command slot，按 API 查询共享资源描述，检查 SM/wave/格式能力、shader 编译和每次 FFX 返回码。保留 SDK 的硬件 permutation 选择，不能在 NVIDIA 上硬强制 Wave64。
3. 为光流分支提供颜色语义明确的输入。例如复用显式 linear-to-sRGB 的 OF 输入变换并声明 SRGB；若选择 linear 输入，则按该版本支持的 transfer 值设置。不能把 linear 像素标成 SRGB 再做一次 gamma。主画面仍走现有一次颜色转换路径。
4. `AMD Quality` 默认使用 ResolutionPlan 的已公布 flow 尺寸；`AMD Performance` 在该尺寸上宽高各减半。日志/UI显示实际数值。与 Magpie 完整源尺寸档比较时另建同尺寸用例，不暗中把 4K→1080→540 的快速结果当原生 4K 光流。
5. AMD 稀疏向量先在 OF 尺寸内完成插值和可靠性验证，输出同一 canonical `current -> previous` 像素运动合同；再按目标 NR/SR/XeSS 尺度适配。使用已知 +/-X、+/-Y、斜向和奇数尺寸实验确认，不沿用 NVOF `/32`。
6. 无 cost 表面时以回投残差、出界、局部歧义生成 confidence。双向检验先作独立质量实验，不能为了名义上“更好”默认再做整帧光流。SCD 是已有切镜逻辑的输入之一，不直接当无误判的全局 reset。
7. 同一来源帧对、epoch、provider、档位和尺寸只计算一次，NR/XeSS 共享结果。资源持续保留至所有消费者 fence 完成；context reset 支持首帧、跳转、切镜、掉帧、重配置和设备丢失。
8. 先顺序记录 AMD compute 工作，证明正确后才研究独立 compute queue。只有并行执行确实减少关键路径时再采用，不能以“异步”命名代替收益。

### 5.2 XeSS 显示后端的实施合同

**依赖与初始化：**

- 新增可关闭的 XeSS runtime/交换链适配器，使用固定官方 `libxess_fg.dll`、`libxell.dll` 和匹配头文件。XeSS-SR 的 `libxess.dll` 不是 XeSS-FG 的必需前置，不替换现有 DLSS/RTX Video SR。
- DLL 外置、绝对路径加载；所有依赖缺失/不支持只影响对应后端，不能让基础播放器启动失败。先创建 XeLL context，再 XeSS context、关联 latency reduction、查询 capability/properties，然后建立交换链。
- 首选在新的后端生命周期里调用 `InitFromSwapChainDesc`，关闭并释放同一视频 HWND 的旧交换链。不能直接包裹仍被多个 COM 引用持有的旧对象：官方 `InitFromSwapChain` 会释放传入交换链，且要求引用计数为 1。
- 最初只开 2X。查询 `maxSupportedInterpolations` 并验证请求；返回码 >=0 也可能是警告/跳帧，不能一律计为有效生成。

**每帧输入与资源所有权：**

- 颜色使用 NR 后、播放器 OSD 前的 SDR 帧。提供 RG16F 运动、等尺寸 depth、frame ID、view/projection、reset；固定平面深度和单位矩阵必须在实验记录中明示。不额外引入默认深度模型。
- 除 `sourceFrameId / settingsRevision / epoch`，记录 motion 对应的 previous source ID。XeSS 代理的上一真实帧必须和该光流上一帧一致；若呈现跳过源帧，reset 或重新建立正确帧对，不能把 B→A 光流用于 C→A。
- 给每个在途真实帧附 guidance lease 或安全的快照纹理，不能直接借用下一次 `graph.process` 会覆盖的 `flowTex_`。颜色、motion、depth 的资源与描述符复用都受消费者 fence 约束。
- `RV_UNTIL_NEXT_PRESENT` 的输入只能在对应 Present 后按队列顺序重用；额外记录 Present 后的 GPU signal 作为池回收依据。跨队列用 GPU wait；不能只看到函数返回就从别的队列覆盖资源。`RV_ONLY_NOW` 会要求 SDK 记录复制，须计入成本。
- 首版按匹配的视频插值区适配运动/深度尺寸。光流计算分辨率与 SDK 接收 guidance 分辨率分别记录。Intel guide 推荐低分辨率 motion，但同版头文件已将 `HIGH_RES_MV` 标为 deprecated/no effect；不能靠切这个 flag 宣称优化成功，须以实际资源尺寸、结果和耗时验证。

**调度与 UI：**

- XeSS 接管的是生成子帧和子帧节奏。Veyra 仍负责真实视频 PTS、文件背压、音频主时钟、采集取样和取消；不要给 XeSS 路线构造伪 `BatchFrame::Generated`。
- 真实帧的 PTS/输入就绪等待放在 `xellSleep` 前；为一个真实帧 ID 提供准确的 simulation、render-submit、Present markers。窗口无需游戏输入模拟，marker 只描述本应用真实工作区间，不伪造采集延迟。
- XeLL 的可选限帧由自身 API 管；旧 `PresentationScheduler` 的 generated 子帧 deadline 不再叠加在 XeSS 后面。采集 mailbox 仍为 1；内外队列、源帧年龄和 VSync/VRR 需要一起测，不能通过更深缓冲换漂亮 FPS。
- `frameRenderTime` 仅填文档所述的真实基础帧时长；没有可信测量时允许填 0，不能拿单个 NR GPU ms 或倍率倒数顶替。
- 播放器按钮/毛玻璃仍在视频区域外；已有独立字幕/OSD 层保持在生成之后。若字幕必须进入交换链则用公开 HUD-less/UI composition 资源合同。烧录 HUD 仍属于源像素，NR 的保护矩形不自动等于 XeSS 的 UI 保护。
- 黑边使用明确的插值 subrect；宽高比、滚轮预览缩放/平移发生变化时同步变换 guidance 或 reset/bypass 该过渡。比较/分屏如无同帧匹配指导，则明确临时停止插值，不能让两条不同颜色历史混插。
- 暂停、最小化、停流关闭插值；seek/切镜使用 `resetHistory` 跳过跨界插帧。销毁时先排空工作，释放 backbuffer/代理 COM 引用，销毁 XeSS，最后 XeLL。停止 Present 时不能继续无界提交 marker。
- XeSS 不支持 DXGI exclusive fullscreen；现有播放器无边框视频全屏可以保留，须验证多屏、resize、模式展开和关闭重开。单进程仅一个活动 XeSS 代理交换链。

**FPS 与导出：**

- 底栏“处理 fps”继续统计真实源帧 GPU 完成，不能乘 2。专业诊断另外显示代理应用 Present 率、SDK `framesPresented` 窗口计数、`frameGenResult`、启用状态与丢弃/重置。
- Intel 将 `framesPresented` 作为输出帧率估计来源，但这不是物理扫描测量，也不独立证明每一帧有正确中间运动。必须和调试标记/输出内容观察结合。
- 对视频导出，能力表标明 XeSS 当前仅实时显示可用；保留现有导出设置并让用户明确选择 DLSS/FRUC/关闭，不静默把 XeSS 导出改成别的算法。既有导出完整性检查保持现状。

## 6. 专业模式的新增选项

| 控件 | 选项 / 行为 |
| --- | --- |
| 运动估计 | `NVIDIA NVOF`、`AMD FidelityFX`；保留 NVOF 默认。关闭仅用于明确的诊断/回退 |
| 光流档位 | NVOF 保留原三档；AMD 为 `性能` / `质量` 两段按钮，旁边显示实际 flow 尺寸 |
| 补帧后端 | 保留 `DLSS` / `FRUC`，增加 `XeSS` |
| 倍率 | XeSS 按本机能力显示可选值；5070 当前预期仅 `2X`，不显示可点击的假 3X/4X |
| 状态 | requested/applied、正在重建、回退原因和实际处理尺寸；不把选择成功当作增强成功 |

全部沿用现有实时生效、失败回滚、还原默认、滚轮只滚页面的规则。新增 enum/预设字段保持旧文件值的解释不变。日常模式不新增整片参数面板。

## 7. 分步施工与完成门槛

| 顺序 | 交付 | 必须看到的证据 |
| --- | --- | --- |
| R0 固定依赖与基线 | 官方 SDK 来源、固定版本/文件清单、同素材现有后端基线；可选构建目标 | Git 无 SDK/runtime；实际 capability、源/内部/显示尺寸、输入/EXE/runtime 身份可追溯 |
| R1 AMD OF 最小闭环 | `AmdOpticalFlowProvider` 与实际 graph 调用；NVOF 原路径保留；独立 motion 诊断 | FFX Create/Dispatch 成功；正负位移、奇数尺寸、reset 后正确；NR-only 同配置 A/B 与 GPU 时间 |
| R2 XeSS 独立显示闭环 | 可复用的 XeSS runtime/交换链库，由薄 probe 组装；使用真实 NVOF/AMD guidance | 5070 实际能力、2X Present status、可见正确中间运动；Zero-MV 仅作诊断对照；暂停/恢复/resize 安全 |
| R3 产品接入 | 每帧 guidance lease、graph/present 后端选择、XeLL 调度、字幕/预览/诊断 | 无跨帧/跨 epoch 资源污染；采集回放和文件 PTS/A/V；真实处理 FPS 不翻倍造假 |
| R4 专业模式与回归 | 新选择项、持久化、错误提示、既有 UI 回归和现有 delivery | 当前代码/EXE 绑定的软件证据；原 DLSS/FRUC 与三入口功能无退步 |
| R5 用户实卡比较 | 固定 PS5 30fps 场景，经采集60→处理30，比较后端 | 体验、画质、源帧年龄与实际帧节奏；才据此讨论默认推荐组合 |

probe 核心不能永久留在 `tools/*/main.cpp`。建议新增的实现位置为 `include/veyra/guidance` / `src/guidance` 的 AMD provider、`include/veyra/gfx` / `src/gfx` 的 XeSS runtime/交换链；通过现有 `VideoPresenter` 和 `EnhanceGraph` 使用。接口名以实施时最小改动为准，不要求额外造通用插件系统。

R1 如没有速度收益但能在明确场景改善运动/NR，可以作为用户可选质量档保留；若画质、性能均无收益，则停止扩建该候选。R2 独立成立，不以 AMD 胜出为必需条件。

## 8. 怎样证明效果变好

### 8.1 分离变量

同一素材、GPU/驱动、实际 SR/NR/FG/交换链尺寸、SR 档位、NR 参数、输出刷新方式、源处理帧率和 applied revision，按下面顺序比较：

1. 固定 SR/NR，FG 关闭：NVOF 对 AMD 性能/质量。
2. 固定 NR/SR/光流：DLSS 2X 对 XeSS 2X；FRUC 2X 单列，因为它的内部光流无法由此 selector 替换。
3. 比较完整 `SR + NR + XeSS 2X + AMD OF` 与当前完整链。
4. 原生4K NR和实时内部NR分开，不用1080内部处理胜出冒充原生4K速度。

测试输入以 1080p30/60→4K、原生4K18、原生4K30/60分组，先查 30 与 60 常用场景。15fps 是压力与伪影样本；30fps→XeSS2X目标60，15fps→XeSS2X目标30。**不能把 XeSS 2X 与旧 DLSS/FRUC 4X 的 GPU 占用直接对比后宣布算法更快。**

复用已有 capture60→30 取样选项，先在 graph 前进行。不把重复60帧送完全部增强再在 Present 减半；不得靠改写PTS假造“30fps内容”。

### 8.2 正确性和质量

- 光流：已知整/半像素位移、正负方向、遮挡、出界、细线、低纹理、固定文字、亮度突变；同时观察输入、flow 和 warp residual。
- AMD 必须确认 reset 首帧及下一帧恢复，不能沿用旧 context 金字塔。固定 confidence=0.65 不算验证通过。
- XeSS：使用官方 `SHOW_ONLY_INTERPOLATION`、`TAG_INTERPOLATED_FRAMES`、`PRESENT_FAILED_INTERPOLATION` 和 Present status 检查。这些 debug 开关只在短测启用。
- `SHOW_ONLY_INTERPOLATION` 失败时可显示原帧，所以它单独不证明成功；SDK计数也不足。结合调试标记、确定性平移的中间位置和必要的测试录制/Inspector 抓取，排除重复帧、黑帧和简单混合。
- 不能用当前 `readPresentedFrameForTest` 直接读应用 backbuffer来证明 XeSS 插值，因为生成帧在代理内部。诊断录屏只用于验证，不进入正常采集或导出路径。
- 没有真实 depth 时重点观察显露区域、近远景、人物肢体、字幕和不规则运动。静止截图更锐不能证明时间稳定。

### 8.3 性能和节奏

- 记录源到达/处理/有意取样丢弃、有效生成、SDK输出计数、Present间隔P95/最大值、源帧年龄、A/V、重置/重建耗时、显存和在途资源上限。
- AMD OF GPU query与准备/稠密化/置信度分开；XeSS Present CPU耗时与内部GPU插值成本分开。没有可准确取得的纯内核时间时标未测，采用完整链吞吐、输出节奏及GPU trace对比，不用CPU调用时间充数。
- 先做10–30秒窄测，有实际收益再做三次60–120秒固定配置对照。**每次完整测试独立外部上限290秒，不是所有测试合计5分钟。** GPU场景串行。
- 候选推荐条件：收益可重复且大于波动，正确性和自然画质不退步，不靠增加延迟/队列深度换流畅。无测量前不许写“低于10ms”“GPU60%”或“比 Magpie 更快”。

## 9. 依赖与复用方式

本轮没有复制 Magpie 代码进入产品或引入新的受版本控制 DLL。AMD OF 和 XeSS 开发依赖仍在本地忽略目录，产品以其公开接口独立接入，不需要复制 Magpie 的整套 Renderer/UI。

- AMD 当前大 SDK 是混合许可证；已核对 OF `.h/.cpp` 和相关 shader 在 MIT 文件清单内，源文件本身也有 MIT notice。可以使用该子集，但必须保留逐文件来源、版权和完整 notice，不能把整个 Redstone SDK 当 MIT。
- Intel `LICENSE.txt` 允许原样再分发二进制并保留许可/版权，禁止修改与逆向。后续成套 DLL、运行库、notice 和 manifest 外置，头文件不因 `main` 新增部分 MIT 文件就假定全部开放。本文不更改 Veyra 的 GPLv3；分发集成版时须核对相容性/所需链接许可，不能仅凭外置 DLL 判定已解决。
- 本项目已有开源复用授权与 GPLv3，不再因泛泛的“不能复制竞品”阻塞研究；若实施中采用 Magpie 原创实现，按实际复用文件保留 GPL 来源及对应源码义务，并另核对第三方二进制，不能机械改名抹去归因。
- 官方 SDK开发资产保存在 `third_party_local`，运行时在 `runtime_local` 或受控 staging；不写进源码、Git LFS或资源文件。AMD源码子集未来若需版本化，只能按已核对许可单独引入，不能把整个SDK目录提交。
- 本轮不下载/替换/执行运行时，不更新 Release 或远端。本机已有 XeLL 1.3.0.5 仅为观察对象；官方 v3.0.2 的暂停相关修复应纳入选版。

## 10. 本轮结果与下一步

实际执行：Git 状态/日志、`rg`与源码读取、`gh api` 查询 Magpie/Intel/AMD 的固定提交、树、Release和compare，以及官方 raw 文档/源码下载到忽略目录；本机 Magpie版本读取；应用和指定NR hash/签名核对。随后完成 Release 构建，并执行契约、预设、XeSS 和 AMD OF 短测。

`veyra_repair_contract_tests.exe` 的 36 项检查通过；`veyra_repair_preset_tests.exe` 使用临时预设文件路径通过；`veyra_motion_validation_tests.exe` 的水平/垂直 confidence 验证及 D3D12 debug 检查通过。RTX 5070 上的 `veyra_experimental_backend_tests.exe xess` 成功完成 XeSS 初始化、资源标记和 Present，记录 `generated=43`、`debugErrors=0`；`... amd` 成功完成 AMD OF Create/Dispatch/Destroy，记录 `amdDispatches=48`、`debugErrors=0`，且非周期纹理平移的最终 canonical flow 为右移 `(-2, 0)`、左移 `(2, 0)`；`... sr2k` 与 `... sr8k` 各完成三次 DLSS SR Evaluate，并读回确认非黑的 `2560x1440` 与 `7680x4320` 输出。每项执行均少于五秒。这些短测只证明本机 SDK 调用、资源提交、AMD OF 所测方向和所测 SR 输出，不证明真实扫描输出帧率、画质、AMD GPU 兼容性、实卡效果、8K 实时性或长期稳定性。

本轮应用 EXE 由当前 Release 构建生成；指定 NR hash 仍为 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，签名 Valid。没有修改控制面或原始产品方案，没有 push、Release 或 Runtime Pack 变更。

查询失败留实记录：尝试 Intel Release 名称 `v3.0` 得到404，随后采用已核实存在的 `v3.0.2`；一次 Windows `rg`字面通配路径失败，已改用结构化manifest及实际文件枚举。均非效果测试失败或成功。

本文原下一条任务为R0/R1的AMD光流诊断，现已完成窄范围实现与 SDK 提交测试。下一步是 AMD 实机兼容性、真实呈现节奏和画质对照；AMD NR 的后续边界仍以 [补充方案](SR_TARGETS_AMD_NR_PLAN_2026-09-10.md) 为准。当前不把实验后端标为可交付的 AMD DLSS5。

## 11. 原始来源

- [Magpie 固定提交的 AMD 光流实现](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/AmdOpticalFlowProvider.cpp)
- [Magpie 固定提交的 XeSS 呈现实现](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/XeSSFGPresenter.cpp)
- [Magpie XeSS 效果器标记](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Effects/XeSSFG/XeSS_MultiFrameGeneration_ZeroMV.hlsl)
- [Intel XeSS-FG 官方指南与硬件限制](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md#requirements)
- [Intel XeLL 官方调度指南](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md)
- [Intel FG 头文件：HIGH_RES_MV 为 deprecated](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/inc/xess_fg/xefg_swapchain.h)
- [Intel v3.0.2 发布记录](https://github.com/intel/xess/releases/tag/v3.0.2)
- [Intel SDK 许可证](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/LICENSE.txt)
- [AMD 官方 Optical Flow 算法与输入输出说明，v1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55/docs/techniques/optical-flow.md)
- [AMD 当前 Optical Flow API](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/60f4ea81909200d8542eca14dccb2628b763a9a3/Kits/FidelityFX/framegeneration/fsr3/include/ffx_opticalflow.h)
- [AMD 当前 Optical Flow 实现](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/60f4ea81909200d8542eca14dccb2628b763a9a3/Kits/FidelityFX/framegeneration/fsr3/internal/ffx_opticalflow.cpp)
- [AMD 逐文件许可清单](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/60f4ea81909200d8542eca14dccb2628b763a9a3/Kits/FidelityFX/docs/license.md)
