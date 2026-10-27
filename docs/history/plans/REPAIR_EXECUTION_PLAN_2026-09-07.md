# Veyra 修复执行方案：SR4K、真实补帧、DLSS5 参数与中文全屏

日期：2026-09-07。状态：**方案已编写；用户已选择 B1–B5，代码施工留给下一对话显式启动**。

本轮用户要求写详细文档、调研 Magpie，并已明确选择 B1–B5。本文件不是已完成报告，不改变当前 EXE，不授予修改驱动、运行第三方脚本、下载运行时、公开发布、改写保护哈希/门禁或启动无人值守 Goal 的权限。

配套文档：[Magpie 功能清单与选择表](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/MAGPIE_FEATURE_BACKLOG_2026-09-07.md>)；[当前交付状态](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/DELIVERY_STATUS.md>)。

## 1. 本次到底要交付什么

用户已明确要求，下次施工应一起完成：

1. 修复 SR4K 打开后严重掉速的问题：分开“超分输出到 4K”和“DLSS5 原生 4K 推理”，不能让一个复选框暗中把整条链路改为最高负载。
2. 修复 2X 补帧：不仅调用成功，而且生成内容、输出纹理、时间戳和显示调度正确。
3. 增加真正的 3X、4X DLSSG；采集、播放、视频导出共用同一实现。
4. 增加 DLSS5 参数调节，包括模型控制与处理结果的变化强度；静态图片、视频和采集都可用，导出记录采用的参数。
5. 完整的无边框全屏、可靠快捷键、中文界面，调节参数不重开视频、不把 UI 卡住。
6. 保留三个入口：采集卡、视频播放器、图片/视频增强导出。仍支持 4K SDR；实时档默认较低 NR 内部分辨率，原生 4K 可选，视频导出默认原生 4K。
7. B1 同一帧原图/处理后对比：瞬时切换与分屏拖杆，比较对象和时间点必须明确。
8. B2 常驻性能面板：展示 SR、NR、NVOF、FG、合成、呈现的真实 GPU/CPU 分项，不把设置值当实测值。
9. B3 用户预设：整套增强参数一次应用，支持新建、复制、重命名、删除和恢复内建默认，不允许任意打乱固定处理顺序。
10. B4 光流质量档：性能、平衡、质量，按本机 capability 显示实际采用档位；多个消费者共享一次估算。
11. B5 诊断中心：最近错误、可展开原因/建议、脱敏复制和打开日志目录；不自动上传日志、素材或路径。

B6–B8、C1–C7、V1–V2 仍是候选，没有自动获得施工授权。不得顺手加入另一个 FG 后端、HDR、全局窗口捕获、深度模型、任意插件链或批处理系统。

### 1.1 不作虚假承诺

- 50 个不同的源帧/秒，2X 的目标是 100 个输出时刻/秒；3X、4X 分别是 150、200。目标达成还受 GPU 吞吐、实际生成结果、显示刷新率及调度限制。
- 采集卡输出 60 帧/秒，不证明游戏画面每秒更新 60 次。30 帧游戏可能在 60Hz 传输中重复帧；50Hz 传输中的重复节奏可能更不规则。
- 60Hz 屏幕不能完整显示 100 个不同画面/秒。界面必须区分目标、生成、提交和可确认的显示统计。
- 已有本机资料显示，原生 4K NR 推理约 22–23ms/源帧。仅 NR 就超过 60 源帧/秒的 16.67ms 预算；修队列不能消除这项计算成本。
- 降低 NR 内部分辨率保留的是较快的实验增强路线，不等于原生 4K NR 画质，也不保证与 Magpie 或官方游戏集成效果相同。
- 本次不再争论 Feature 18 能不能调用，但仍要检查参数确实生效、不是重复帧、不是错误颜色、不是统计造假。

## 2. 当前证据与根因分级

这轮只读代码、已有证据与公开资料，没有新增 GPU 实测。

| 现象 | 已确认的代码事实 | 不能直接下的结论 | 修复方向 |
| --- | --- | --- | --- |
| SR4K 开启后卡顿 | Controller 把 work extent 直接设为 3840×2160；实时降分辨率分支要求 SR 关闭。后续 NR/flow/FG 共用 work extent | 不能保证只改线程就恢复原生 4K60 | 拆开 source、SR/base、NR、FG/output extent |
| 补帧数字上涨但不顺 | Graph 只有单个 generated 输出，成功 Evaluate 后增加计数；显示端不验证实际显示节奏 | 不能把 generated count 当 100Hz 显示证明 | 建立帧身份、内容和呈现证据链 |
| 实时输出节奏可疑 | 每对帧以 CPU process 返回时刻重新定基准；先呈现生成帧，再等待约半个源帧间隔呈现真实帧 | CPU 返回不等于 GPU 完成；也未证明 Present(0) 是唯一故障 | 独立稳定输出时间线与有界 GPU-ready 调度 |
| 30 帧游戏改善不明显 | 重复检测依赖极小 PTS 差；正常传输间隔的重复画面不会被这条条件识别，调用端也未消费 duplicate 结果 | 不能把所有 FG 问题都归因于重复帧 | 先证实基本 2X，再接内容节奏识别 |
| 3X/4X 不存在 | Backend 把 multiFrameCount、multiFrameIndex 固定为 1；Graph、Presenter、Encoder 均按一张生成帧设计 | capability 曾返回最大 5，不等于现有软件已支持多倍率 | 泛化整个帧批次，不能只加下拉框 |
| 参数改动体验差 | NR 参数在 Graph 中写死；部分 UI 开关调用 open，stop 会等待 worker 退出 | 不该用每次重开文件模拟实时调参 | 帧边界参数快照与异步重配置 |
| 全屏不完整 | 已有 F11/窗口样式切换，但视频区域、工具栏、子控件焦点处理不完整 | 不能写“从零增加全屏”，也不能称当前已完整验收 | 补全窗口、布局、输入与恢复行为 |

既有实卡修复解决过采集帧所有权和旧 PTS 等待，不能撤销其真实成果；但它约 49–50 源帧/秒的短测，也不能证明 SR4K 和真实 2X 已修好。历史证据见 [采集延迟修复记录](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/CAPTURE_LATENCY_FIX_2026-09-07.md>)。

### 2.1 首先阅读和修改的实际文件

| 现有文件 | 下一轮职责 |
| --- | --- |
| [EngineController.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/engine/EngineController.cpp>)、[EngineController.h](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/engine/EngineController.h>) | 参数请求、模式/分辨率规划、音视频时间线、统计；移除通过重新 open 调参 |
| [生产 EnhanceGraph.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/pipeline/EnhanceGraph.cpp>)、[头文件](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/pipeline/EnhanceGraph.h>) | 实际 GPU 图、分辨率解耦、NR 参数、光流消费者、多输出批次 |
| [DlssFgBackend.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/ngx/DlssFgBackend.cpp>)、[头文件](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/ngx/DlssFgBackend.h>) | SDK 参数类型、MFG 索引、输出有效性、错误信息 |
| [DlssNrParameters.h](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/ngx/DlssNrParameters.h>)、[NgxParameters.h](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/ngx/NgxParameters.h>) | 复用已存在的字符串和强类型 setter；不另猜参数名称 |
| [VideoPresenter.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/engine/VideoPresenter.cpp>)、[PresentSink.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/gfx/PresentSink.cpp>) | 任意批次纹理呈现、容量控制、实际提交统计、显示能力查询 |
| [CommandSlotRing.h](<C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/gfx/CommandSlotRing.h>) | 保持统一分配游标；command slot 与帧输出资源所有权分开 |
| [SceneCadenceAnalyzer.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/core/SceneCadenceAnalyzer.cpp>) | 正常 PTS 间隔下的内容重复、置信度与断流识别 |
| [VideoExportJob.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/engine/VideoExportJob.cpp>)、[NvencD3D12Encoder.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/sink/NvencD3D12Encoder.cpp>) | 多倍率帧序、CFR、尾帧、音轨；解除固定 4 个 SRV 描述符假设 |
| [应用 main.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/apps/veyra/main.cpp>) | 中文面板、参数控件、倍率、状态、全屏；不承载算法实现 |
| [ParityDecode.hlsl](<C:/Users/123/Desktop/Veyra DLSS Video Player/shaders/ParityDecode.hlsl>) | 保持已建立的颜色还原契约；新残差合成在其后独立实现 |

注意：工程还有同名的 [core/EnhanceGraph.cpp](<C:/Users/123/Desktop/Veyra DLSS Video Player/src/core/EnhanceGraph.cpp>)。不能把那里改出一套计数逻辑，就声称修复了生产 GPU 图；以上实际生产入口是 pipeline 版本。

## 3. 目标链路：把画面尺寸与昂贵推理解耦

### 3.1 大白话版本

先获得干净的原画面，需要时超分到 4K，把它作为保留细节的底图。实时档只让 DLSS5 处理较小的一份副本，取出“DLSS5 相对原副本改变了什么”，把这些变化补回 4K 底图。最后用增强后的真实帧补帧，再画播放器自己的字幕和按钮。

不是把整张画面缩到 1080p 后再放大，冒充完整的 4K 细节；也不是把 DLSS5 的 Raw 输出直接当成最终图。

### 3.2 建议的共享 GPU 图

```text
采集 / 文件解码 / 图片
        │
        ├─ 明确 range、matrix、transfer → 线性源画面
        │                  └─ 原始时序 A/B → 共享 NVOF / confidence
        │
        └─ SR 关闭：保留源底图；SR 开启：DLSS SR → 目标 4K 底图 B
                                                   │
                         ┌─────────────────────────┴──────────────────────┐
                         │ 实时 NR：缩小底图 → Parity → NR → Parity 还原 │
                         │ 原生 NR：原尺寸底图 → 相同处理                │
                         └─────────────────────────┬──────────────────────┘
                                             NR 变化量
                                                   │
                                   可调变化强度 → 回填到底图 B
                                                   │
                                  DLSSG：关闭 / 2X / 3X / 4X
                                                   │
                                共享 FrameBatch（真实帧 + 生成帧）
                                    ├─ 呈现时间线 → 播放器 UI/字幕 → 显示
                                    ├─ NVENC → CFR 视频与音轨
                                    └─ 静态图片输出（不经过 FG）
```

该路线的分辨率/残差思想可参考 Magpie，但实现须独立完成；它也明确提示降低输入分辨率会损失信息。[Magpie 0.6.6 使用说明](https://github.com/SAOG0721/Magpie/releases/tag/v0.6.6-experimental)

### 3.3 ResolutionPlan：不再只传 workWidth/workHeight

新增一个共享的、不可变的 ResolutionPlan 数据结构，至少记录：

- sourceExtent：实际解码/采集尺寸，不能从 UI 的“4K”字样推断。
- baseExtent：保留细节的真实底图尺寸；SR 开启时是用户选择的目标输出。
- nrExtent：模型实际处理尺寸；默认实时档上限约 1920×1080，保持宽高比，按运行时对齐要求取整。
- flowExtent：实际用于估算的输入尺寸；raw grid extent 另存，不能混同像素尺寸。
- fgExtent：最终真实帧与全部生成帧的尺寸；正常匹配 baseExtent。
- outputExtent：编码或显示内容尺寸；窗口物理尺寸不应触发重新创建 NR。
- mode、SR 是否真正启用、估算向量来源、色彩契约标识和配置 revision。

默认行为：

| 输入和选择 | 底图 | NR | 输出说明 |
| --- | --- | --- | --- |
| 1080p，SR 关闭，实时档 | 1080p | 1080p | 1080p 内容；窗口放大不算 DLSS SR4K |
| 1080p，SR4K，实时档 | SR 到 4K | 1080p 副本 | 4K 底图 + 低分辨率 NR 变化回填 |
| 4K，实时档 | 原生 4K | 1080p 副本 | 保留输入底图细节；不是原生 4K NR |
| 4K，原生质量档 | 原生 4K | 原生 4K | 显示实际性能警告，不保证实时 60 源帧/秒 |
| 图片/视频导出默认 | 源尺寸或显式 SR 目标 | 原生底图尺寸 | 离线处理，不沿用播放器实时降分辨率默认 |

NR 内部尺寸可提供“实时 / 平衡 / 原生 / 自定义比例”，但平衡与自定义只在合法尺寸集合中选择，面板展示实际像素，不只展示百分比。默认档不偷偷随负载改变；若提供自动档，必须由用户主动选择且明确显示发生的调整。

源已经达到选择的 SR 输出尺寸时不再创建一次“1:1 超分”，面板说明原生尺寸直通；这不应隐式开启另一个 DLAA 模式。

### 3.4 光流共享必须配套消费者坐标适配

1. 在 NR 改变画面之前，依据相同源时刻 A/B 估算光流。优先复用一次 NVOF；禁止为了 SR、NR、FG 分别运行三次相同光流。
2. 保留基础契约：current→previous；NVOF SHORT2 的 S10.5 数值除以 32 得到估算输入的像素位移。grid=4 表示采样间隔，不意味着位移再除以 4。
3. 对目标尺寸 Wt×Ht，像素位移按对应宽高比例转换；取样网格与向量幅度是两件事。宽和高分别缩放，不能只乘一个“4K 倍率”。
4. SR、NR、FG 使用各自的适配器。SDK 要像素位移还是归一化位移、是否需要反号，逐项按已有头文件与已知平移用例核对；不能把 NR 的正确符号直接视为 FG 的正确符号。
5. SR 创建标志若选择低分辨率 motion，则传源尺度；若选择输出尺度，则传输出尺度。不能只换 texture 大小、不换标志和 mvecScale。
6. confidence 与 motion 同时映射，低置信度区域清零/衰减。采集缺帧、切镜或参数时代切换时重置历史；不能拿不相邻的 A/B 当连续帧。
7. 不为缺失的游戏深度或 jitter 编造数据。现有常量/估计深度要标明来源；新接深度模型不属于本次任务。

本方案不机械采用 Magpie 作者对“某种光流/另一种 FG 更好”的评价；那些是候选依据，不是本机画质结论。[光流配置与共享记录](https://github.com/SAOG0721/Magpie/blob/experimental/docs/experimental/todos/20260905-v0.6.5-r8-TODO.md)

### 3.5 残差回填的独立实现约束

- 基础定义：同一线性颜色空间内，“NR 还原结果减去送入 NR 的底图副本”得到变化量；控制变化量后，以保边界的重采样回填到高分辨率底图。
- 默认参数的全尺寸路线应尽量保持现有 Parity 还原结果；不能无理由改变当前色彩基线。
- 所有变化为零时，无论调整哪个残差参数，结果都应等于底图。
- 总变化强度为 0 时必须回到底图；1 为未额外缩放的 NR 变化。NR 开关关闭时直接跳过模型工作，不要仅把变化强度置 0 却继续花费全部推理开销。
- NR 还原、残差合成、显示/编码各自有明确颜色空间。禁止重复 sRGB 编解码、把 Raw NR 相减、把 UI 混入模型输入。
- 降采样须有抗混叠，升采样须处理边界和振铃；先用独立实现的明确滤波器，通过棋盘/文字/高光合成图检查，再决定是否增加更昂贵的算法。
- NR 没处理出来的高频细节不能凭空补回；回填只保住底图已有细节。低分辨率增强也可能造成边缘光晕和时间闪烁，必须如实保留原生档选择。

## 4. 把“补帧调用”修成真正的输出链

### 4.1 统一 FrameBatch 与资源租约

现有 FrameOutputs 的 hasGenerated + 单个 genSlot 不够。替换为有上限的帧批次，至少包含：

- epoch、settingsRevision、sourceFrameId、A/B 的实际 PTS。
- 真实帧与 0–3 张生成帧的条目数组；每个条目有 kind、subframeIndex、目标 PTS、texture/descriptor 标识、readyFence 和资源租约。
- SDK 返回值、interpolation 是否被禁用、是否为 reset/bootstrap、无输出的明确原因。
- kind 至少区分 Real、Generated、Hold。不能把尾帧保持或解码重复帧计入 Generated。

处理规则：

1. 第一个真实帧 A 只呈现一次。收到 B 后，按验证后的时间顺序呈现 A/B 之间的生成帧，再呈现 B。
2. 即使使用相同 SDK 输入，3 张生成结果也必须拥有互不覆盖的输出资源；提交给 sink 后，在 GPU 消费完成前不得复用。
3. SDK 输入 A/B、motion、depth、参数版本的寿命覆盖该帧批次的全部 Evaluate。显示和编码不能看到半途被下个真实帧覆盖的内容。
4. command slot 环与输出帧池分别计数。保留一个图提交所有者；不能让显示线程和处理线程无锁共用同一 CommandSlotRing 游标。
5. 帧池大小由最大在途批次 × 每批最多 4 个输出及实际显存预算计算。可以预分配有界池，但不能为每帧分配/泄漏纹理，也不能无界加深队列。
6. 采集 ingress 仍为容量 1 的 latest mailbox，内部 A/B 历史另算。离线导出不丢真实帧；播放器也不得靠丢源帧伪造达标。

### 4.2 2X 的四层证据

按顺序定位，某层失败就修该层，不先怪驱动或远程软件：

1. **调用层**：feature capability、Create/Evaluate、精确参数类型、返回码、插帧禁用标记。通过不等于图像正确。
2. **内容层**：用人工生成的移动纹理/边缘 A/B，检查生成帧不是端点复制、全黑或线性混合；运动方向和中间位置正确。静止图不适合用来证明“不同生成帧”。
3. **资源层**：对照 batchId/subframeId、输入和输出纹理、fence、epoch；排除 presenter 取错 SRV、输出提前覆盖、读取了上个批次的问题。
4. **呈现层**：按目标时刻提交，记录每个条目的实际提交时间及可获得的显示反馈；不能只有累计 Present 成功次数。

SDK 支持的 outputDisableInterpolation 结果要接入有效性判断。具体读取与 GPU 消费方式按本地 SDK 核对；如果采用小型状态读回，必须异步、不能读回整幅正常播放像素、不能逐帧阻塞等状态。尚未确认有效的生成结果不得在 UI 中宣称已显示。

### 4.3 调度策略

- 文件播放用音频主时钟或既有媒体时钟，将 PTS 映射到稳定的 host deadline；不改变音轨速度来掩盖性能不足。
- 采集使用当前会话的单调时钟与源时间线建立锚点，并明确加入 A/B 所需的至少一帧等待。不能每帧用“CPU 提交完毕”重置锚点。
- 处理完成条件是对应 GPU fence；显示容量与 deadline 单独判断。采用可中断等待，收到 stop/seek/resize/设置变更时能立即退出等待，不持有 UI 锁。
- 正常路径不每 pass CPU fence wait。GPU 依赖由队列/fence 串接；CPU 只因有界池容量、呈现容量或到期时间等待。
- 对 swapchain frame-latency waitable object/最大队列深度进行明确配置；能力不可用时记录原因，仍保持软件队列有界。不能把某个 DXGI 选项当万能低延迟开关。
- 突发积压不能连续补发过期生成帧形成“追债”。采集应丢弃过期 ingress，并使历史失效；跳过未显示的过期生成帧单独计数。
- 对文件，不能丢真实源帧来提高吞吐数字。超过预算时明确报告档位无法实时运行，允许用户选择较低档；离线导出按完整时序处理。
- 显示器不足以承载 N×源帧率时，保留真实帧优先、按显示时隙选择生成帧；显示完整理论倍率不可达的提示。不得偷偷把 4X 按钮变成重复帧，也不自动改变用户选定倍率而不告知。

Present(0) 在 flip model 下允许中间提交被丢弃；这是必须检查的机制，不是目前已被证明的唯一根因。[Microsoft Present 文档](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)

Magpie 的 Front Edge Sync 是其可控提交边界的节奏控制，不是免费降低采集延迟，也不是跨进程或扫描线同步。Veyra 应独立实现上述媒体时钟策略，不能直接照搬“先把源程序限帧”的使用前提。[Magpie 帧同步说明](https://github.com/SAOG0721/Magpie/blob/experimental/docs/FRAME_SYNC_GUIDE.md)

### 4.4 必须拆开的统计

面板至少区分：

- 传输源 FPS；可选的内容更新 FPS（未知时显示未知）。
- 实际处理的真实帧 FPS；有效生成帧 FPS。
- 目标输出 FPS；实际提交 FPS；显示器 Hz。
- 可确认的显示 FPS/显示间隔。只有能关联到本应用帧的显示反馈才能填写；不支持则 null/“未能测量”，不得用 Present 返回率替代。
- ingress 丢弃、处理丢失、生成禁用、过期未呈现、显示限制各自的计数。
- capture callback→取帧、GPU 各 pass、等待、Present CPU 耗时等分项；与光子延迟严格分开。

示意：`采集 49.9 | 处理 49.8 | 有效生成 49.7 | 提交 99.5 | 屏幕 144Hz | 实际显示：未能测量`。这是字段设计示例，不是现有测试结果。

### 4.5 内容节奏：解决 30 帧游戏装在 60 帧信号中

在基本 2X 正确后，再实施：

1. 从已有 GPU 画面计算轻量差异和重复特征，允许异步读取小型统计，禁止全帧回读。结合 PTS 间隔和连续多个样本判断稳定的重复模式。
2. 模式为“跟随传输 / 自动识别 / 手动内容帧率”。手动 30 只是时间线意图，不能不看实际内容就每隔一帧强删。
3. 自动识别只有高置信度时生效；静态菜单、暂停、动画局部变化不能简单判定为低 FPS。重复帧不会每次触发全链 reset。
4. 对确定的内容端点重新建立 A/B；只在有效的时间间隔内插帧。等待端点 B 仍增加必要 lookahead，不能假称采集卡内部已经提供未来帧。
5. 文件默认不去掉源帧、不修改音视频时长。内容去重若用于输出，需要保留完整时间轴，和“源帧丢弃”分开记录。
6. 30、50、60 内容更新模式分别记录生成与提交数据，不能只在一张 60Hz 桌面静态图上验收。

## 5. 真正的 3X / 4X：从 SDK 到导出一起改

本地参数依据是 [nvsdk_ngx_defs_dlssg.h](<C:/Users/123/Desktop/Veyra DLSS Video Player/third_party_local/nvidia/DLSS_SDK_310.7.0/include/nvsdk_ngx_defs_dlssg.h>)，不是 Magpie 的界面文字。

| UI 总倍率 N | 每对真实帧生成数 | multiFrameCount | multiFrameIndex | 期望输出时刻 |
| --- | --- | --- | --- | --- |
| 关闭 / 1X | 0 | 不调用 FG | 无 | 真实帧 |
| 2X | 1 | 1 | 1 | A 到 B 区间的 1/2 |
| 3X | 2 | 2 | 1、2 | 1/3、2/3 |
| 4X | 3 | 3 | 1、2、3 | 1/4、2/4、3/4 |

这些期望时间顺序必须用已知运动验证与该 runtime 的实际输出一致，不能只写出正确 PTS 就算图像顺序正确。

实现要求：

- capability 的 MultiFrameCountMax 表示最大生成数，不是 UI 总倍率。4X 至少要求 3；每次进程/设备初始化读取本机实际 capability 和查询返回码。
- 同一真实帧的所有子调用保持相同 BackbufferFrameID、输入、epoch 与模型参数，索引顺序递增；不能每生成一张就递增真实帧 ID。
- 不支持的档位禁用并显示原因。曾经查询到最大 5 仅作为历史线索，不替代本次结果。
- 真实输出资源数量、descriptor heap 容量、fence 生命周期、Presenter 和 NVENC 全部适配。禁止只改 Backend 中两个常量。
- 不能把一次 2X 的纹理重复输出三次；不能串联两次 2X 假称原生 4X；不能用线性混合填补 SDK 失败后仍标记为 DLSSG。
- reset 的第一个端点不生成跨 epoch 帧。中途调倍率以资源重配置处理，不跨批次混用旧/新倍率。
- 4X 不要求额外等待 C 才能得到三个子时刻，但 GPU 工作与输出压力增加；性能不足必须可见，不能承诺无额外成本。

### 5.1 视频导出

1. 输出帧率从硬编码 2× 改为 source CFR × N；使用有理数时间基，不累计浮点误差。
2. 对 M 张连续真实帧，正常区间有 M 张真实帧与 (M−1)×(N−1) 张生成帧。不能把首尾缺失的未来区间当成功生成。
3. 为保持原视频时长，尾部需要时可显式 Hold；单独记录 holdFrames，不计为 generatedFrames。场景切换边界同理，不跨镜头插帧。
4. NVENC 输入使用实际输出 texture 和 D3D12 fence；移除只支持 2 real + 2 generated 的 descriptor 索引假设，遵守编码资源 map/unmap 寿命。
5. 导出参数在任务开始时冻结。播放面板继续调参不能改变正在编码的任务；取消仍保留明确 partial 策略，音轨和时间戳必须记录。
6. 原生 4K 导出可以慢于实时，不得套用采集 latest-frame 丢帧策略。图片没有时间轴，FG 控件禁用并解释原因。

## 6. DLSS5 参数面板：两类调节必须分清

“DLSS5”是面向用户的名称；技术日志注明 `DLSSNR / Feature 18 / local experimental`。当前文件不是具有通用正式 SDK 合同的公共产品接口，不能把实验参数范围写成 NVIDIA 官方保证。

Magpie 的近期改动区分了模型控制与残差控制，并修正了部分范围/术语。以下 Veyra 数据合同优先复用本项目已存在的键与类型；UI 范围是拟采用的约束，不代表每一档已在本机验证。[参数语义修订](https://github.com/SAOG0721/Magpie/blob/experimental/docs/experimental/todos/20260904-v0.6.5-r2-fix1-TODO.md)

### 6.1 模型参数：写入现有 Feature 18 Evaluate

| 中文名 | 当前已有键 | 类型 | 当前默认 | 拟定 UI | 验收限制 |
| --- | --- | --- | --- | --- | --- |
| 模型强度 | DLSSNR.Intensity | F32 | 1 | 0–1，步进 0.05 | 0 不预先认定等于关闭；按真实输出判断 |
| 局部明暗调整 | DLSSNR.LocalToneStrength | F32 | 1 | 0–1，步进 0.05 | 不标成整幅曝光控制 |
| 局部结构调整 | DLSSNR.LocalStructureStrength | F32 | 1 | 0–1，步进 0.05 | 不标成可靠几何重建 |
| 肤质结构 | DLSSNR.SkinStructureStrength | F32 | −1 | 自动 −1；手动 0–2，实验项 | 自动是哨兵值，不做普通负数滑条；不保证人物分割 |
| 风格 | DLSSNR.Style | I32 | 0 | 默认 0；实验值 1/2 经验证后开放 | 先显示编号/实验标签，不伪称官方自然/电影模式 |
| 自动遮罩 | DLSSNR.UseAutoMask | I32 | 0 | 关/开，实验项 | 返回成功不证明有效；无证据时提示未确认/不支持 |
| 画面内 UI 修正 | DLSSNR.UICorrection | I32 | 0 | 关/开，实验项 | 不能恢复原生 HUD-less 缓冲，也不保证修复游戏文字 |

约束：

- 强类型 setter 必须沿用 ParameterBlock；不要把布尔直接猜成 U32，或把 Style 当 F32。
- 提交前校验 finite/range/枚举；拒绝 NaN/Inf。参数快照保存原始值和实际应用值，避免控件显示 0.5、引擎仍用 1。
- 初始化保持当前默认值，迁移配置不改变用户已保存的 NR/SR/FG、设备或音频选项。
- Hint.Render.Preset 暂保持固定值 0，不额外发明 A/B/C、游戏名或官方模式；实验效果由 Style 与现有控制检验。
- 某个参数在固定样本上未观察到差异，只能说该样本未证实效果；若 runtime 拒绝值，回滚并显示不支持，不能留一个无效滑条冒充功能。

### 6.2 变化量参数：由 Veyra 的 GPU 合成器处理

这些参数不对应新的 NGX 键，不修改 DLL，也不依赖 RenoDX add-on：

| 中文名 | 拟定范围/默认 | 独立功能合同 |
| --- | --- | --- |
| 总变化强度 | 0–2 / 1 | 控制 NR 相对底图的总变化；0 回底图，超过 1 为额外放大 |
| 暗化变化 | 0–2 / 1 | 控制 NR 造成的变暗部分；不是语义阴影遮罩 |
| 亮化变化 | 0–2 / 1 | 控制 NR 造成的变亮部分；不是识别真实反射或发光物体 |
| 色彩变化 | 0–2 / 1 | 控制相对底图的色彩变化；NR 若降饱和，放大变化可能更灰 |
| 明度变化 | 0–2 / 1 | 控制相对底图的明度变化；不等于全图亮度旋钮 |

UI 可在提示中注明 Magpie 常用名称“阴影/结构、反射/辉光、残差饱和度/亮度”，主标签采用更准确的“变化”。不得在宣传或日志中写“已获得反射、阴影材质通道”。

独立合成实现顺序：先控制增亮/压暗变化，再调整相对底图的明度/色彩差异，最后总强度与高分辨率回填。具体数学实现、颜色空间和边缘保护须自主完成，不复制 Magpie shader。全 1 走中性快捷路径，避免额外颜色往返；组合参数通过零变化、黑白灰、饱和色和高光样本验证。

### 6.3 实时生效，不得重开视频

新增共享的 NrSettings / ResidualSettings / ProcessingSettings，推荐独立放在 `C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/engine/EnhancementSettings.h`。该文件尚不存在，只是拟定新增位置。

状态协议：

1. UI 修改 DesiredSettings，发送有界的最新请求；连续拖动滑条合并中间值，不积压几百次重配置。
2. 渲染所有者在真实帧边界接收并校验；生成同一 A/B 批次的所有子帧使用同一 settingsRevision。
3. 应用成功后发布 AppliedSettings；面板显示“已生效 / 等待应用 / 应用失败及原因”。失败保持上一次可用配置，不能更新成功外观却留下旧引擎值。
4. NR 模型参数变化使相关 NR/FG 历史失效一次；不能每个显示帧重复 reset。上游 SR/尺寸/源改变必须重置所有依赖历史。
5. 仅残差参数变化，不需要重新创建 NGX。静态图片可复用底图和 NR 还原结果，只重合成；修改模型参数则重新 Evaluate 图片，不能因“图像没动”忽略请求。
6. SR 目标、NR 分辨率、光流资源配置、倍率属于资源重配置：停止新提交 → 有界排空/取消旧 epoch → 创建所需资源 → 原子切换。不要在 UI 消息线程 join 整个播放 worker。
7. 参数改动不重新选择设备、不重开文件到 0 秒、不抹去暂停状态、字幕或导出设置。需要短暂停顿时明确显示“正在应用”，支持取消。
8. 保存 schemaVersion 和最后成功配置；导出记录冻结快照。内建默认、逐项重置和 B3 用户命名预设均属于本次范围。

Magpie 也在区分期望/实际参数、实时更新与资源重启；其相关更新记录包含未执行 GUI/GPU 验证的限制。这里仅借鉴状态管理思想，不把它的文档勾选当成本机验收。[参数交互记录](https://github.com/SAOG0721/Magpie/blob/experimental/docs/experimental/reviews/20260906-v0.6.6-parameter-interaction.md)

## 7. 中文界面与全屏

### 7.1 界面组织，不另起 UI 框架

继续 Win32。建议将文字资源从 main.cpp 集中到 `C:/Users/123/Desktop/Veyra DLSS Video Player/apps/veyra/UiStrings.h`，布局/参数窗口独立类；路径均为拟新增，不是当前已有成果。

主界面区域：

- 来源：打开视频/图片、采集设备、格式、视频与音频设备。保留实际设备标识，不因汉化改变选择。
- 增强：DLSS5 开关、SR 输出目标、NR 实时/原生档、补帧关闭/2X/3X/4X、参数面板。
- 参数：基础模型 / 实验模型 / 变化量三个分组，当前值、恢复默认、待生效状态和说明。
- 播放：暂停、进度、音量、全屏；导出：图片保存、视频导出与取消。
- 状态：真实源/生成/提交/显示限制和错误，不再只显示一个含糊 FPS。

基础汉化覆盖标题、按钮、菜单、对话框、提示、导出进度、错误摘要和状态；底层 HRESULT、参数键、设备硬件名称保留原始文本。布局按 DPI 计算，给中文文本留空间，不把固定英文宽度原封不动沿用。

### 7.2 全屏行为

- 可见“全屏”按钮，F11、Alt+Enter、视频区域双击进入/退出；Esc 退出。
- 使用统一消息/accelerator 分发，使焦点在按钮、滑条、下拉框时快捷键仍可靠；输入框编辑时不得误吞必需按键。
- 无边框窗口使用当前显示器实际区域，保持比例，隐藏播放器工具栏与多余边距；鼠标移动可显示控制层，闲置后隐藏。
- 进入前保存正常 WINDOWPLACEMENT、style/exstyle、DPI 与显示器信息；退出恢复，显示器断开时限制在可见桌面内。
- 拖到另一块屏幕更新刷新率与输出尺寸；窗口缩放只重建必要 swapchain 资源，不能连带重建 NR。
- Alt+Tab 正常恢复，不照搬 Magpie 为自身窗口捕获问题采取的“切出后必须手动重新开效果”政策。
- 播放器字幕/控件在 FG 之后合成。采集画面里已有的游戏 HUD 不能当成播放器控件移除。

## 8. 已选 B1–B5 的详细实现合同

### 8.1 B1：同一帧原图/处理后对比

必须先定义“原图”是哪一层，不能用两个不同 PTS 的画面制造效果差异。界面提供：

- **输入原画 vs 最终增强**：输入原画通过普通无 AI 缩放到输出尺寸，用于观察 SR+NR 的总体影响。
- **增强前底图 vs 最终增强**：使用 post-SR、pre-NR 的同尺寸底图，用于隔离 DLSS5/残差处理影响，默认选这个。
- 单击或按住“原图”瞬时切换；分屏拖杆使用同一 sourceFrameId、epoch、settingsRevision。拖杆只是显示合成，不重新执行 NR。
- 图片和暂停视频必须完全稳定；播放/采集只比较同一个真实锚点。A/B 之间的生成帧没有天然的“同一时刻输入原画”，此时分屏明确显示“真实帧对比”，不得拿最近端点冒充同一时刻生成帧对照。
- 对比纹理完全留在 GPU，使用有界资源租约；UI、字幕和性能面板不进入被比较的模型画面。B8 的自动成对截图没有被选择，本项不自动保存任何采集画面。

实现入口：EnhanceGraph 输出带身份的 reference texture；FrameBatch 保存引用与 fence；VideoPresenter 增加 compare pass 和拖杆常量；应用层只发送对比模式/位置。

验收：固定图片逐像素确认拖杆两侧来自同一输入；运动短片记录左右 sourceFrameId 相同；默认模式全关闭时两侧仅允许已有普通缩放误差；连续拖动不重开媒体、不增加 NGX Evaluate、不产生 CPU 全帧回读。

### 8.2 B2：真实性能面板

性能数据分成两类，严禁混写：

- GPU query timestamp：上传/颜色、SR、NVOF、NR、残差合成、FG（每个子帧及批次合计）、最终 blit；不支持或当帧未执行则为 null。
- CPU/调度：采集取帧年龄、解码、图提交、GPU 等待、deadline 等待、Present 调用、队列水位、过期丢弃。

每个样本携带 epoch/settingsRevision/extent/倍率。用固定大小历史窗口计算最近值、p50、p95；设备重建或设置时代切换时切段，不把两种配置混在一个平均值里。面板最多约 4Hz 刷新，渲染线程只写无阻塞快照，UI 线程不查询 GPU 或等待 fence。

同时展示 source、content（若可确认）、processed-real、valid-generated、submitted、display（若可确认）FPS 和显示器 Hz。不能用 `源 FPS × 倍率` 填 actual output，也不能把 CPU process 时间称为 GPU 时间或光子延迟。

验收：用独立 GPU timestamp 交叉检查至少一个 pass；禁用某效果时该 pass 显示“未执行”而非 0ms；开 SR4K 后能直接看到是哪一步增加耗时；关闭性能面板不改变算法结果和显著增加帧时间。

### 8.3 B3：用户预设

预设只保存受控的处理设置：NR 模型/变化量、SR 目标、NR 尺寸策略、FG 倍率、B4 光流请求、内容帧率模式和需要重启资源的标记。它不保存 DLL 路径、SDK 对象、设备索引、媒体路径、任意 shader 路径或可执行命令。

建议每用户存储在 `%LOCALAPPDATA%\Veyra\config\presets-v1.json`，schemaVersion=1；临时文件写完、flush、解析回读成功后再原子替换。崩溃或损坏时保留原文件/备份并回到内建默认，诊断中心说明原因，不能静默清空所有预设。

功能范围：新建、从当前复制、重命名、删除用户预设、设置默认、恢复内建默认；用户预设名去首尾空白、限制长度并防重名。内建预设只读，不用“最佳画质”一类未经证实的名字。删除只影响该预设，不删除媒体或日志。

应用预设是单个 settingsRevision 事务：先验证完整快照，再一次切换；Live 参数在帧边界生效，资源参数显示“应用中”并按 6.3 的有界重配置执行。任何字段失败则整套回滚到上一个 AppliedSettings，禁止半套新、半套旧。

验收：重启应用后数值不漂移；旧/未知 schema fail-closed 并给出诊断；切换预设保留播放位置、暂停状态、采集设备和音频选择；导出任务冻结启动时预设快照，之后修改预设不污染进行中的输出。

### 8.4 B4：光流性能/平衡/质量档

建立 `OpticalFlowRequest` 与 `OpticalFlowApplied`，分别保存 SR/NR/FG 的请求、NVOF 实际 perf level、grid、输入/输出 extent、provider、回退原因和 revision。UI 中文档为性能/平衡/质量；内部映射必须按本地 Optical Flow SDK 枚举和 capability 验证，不能凭文字猜数值。

同一 A/B、同一源尺度只执行一次共享 NVOF。多个已启用消费者请求不同时，选择能满足最高请求且本机支持的一次估算，再为各消费者做独立坐标/置信度适配；面板同时显示“请求”和“实际”。若该档不支持或资源预算不足，回滚到最近可用档并明确提示，不能无声改成 Zero Motion。

最高质量/极高开销不是本次固定第四档。只有 SDK 确实提供、实测增益和开销都可说明时，未来再加。实时默认平衡；预设可保存请求值，但不得覆盖 capability 结果。

切换档位属于资源重配置：在 epoch 边界停止新提交、排空当前有界批次、重建一次 NVOF、重置相关历史，然后原子发布 Applied。稳定 fallback 只在“失败↔恢复”状态转换时 reset，不能每帧 reset 导致永远没有历史。

验收：日志和 B2 面板显示实际 perf/grid/extent；SR/NR/FG 同时开启时每对 A/B 只有一份共享 NVOF execute；三档至少用代表运动片记录 GPU p50/p95 与非零 motion/confidence，画质不靠调用成功下结论。

### 8.5 B5：诊断中心

新增有界 `DiagnosticEvent`：时间、严重程度、component/stage、HRESULT/NGX/NVOF/SEH code、source/base/nr/flow/fg/output extent、epoch/frame/batch/subframe、settingsRevision、runtime 身份、Applied 光流/回退、重复次数和用户可执行建议。相同故障按 fingerprint 合并计数，最多保留固定数量，不能日志洪水拖慢播放。

主界面显示“最近一次问题”，详情页可展开、复制脱敏文本、打开本应用日志目录、手动关闭。复制内容默认去掉媒体完整路径、Windows 用户名、设备序列号和素材内容；可保留文件扩展名、尺寸、错误码和 runtime hash。用户复制前能预览，不自动上传、不联网、不附带图片或视频。

诊断必须记录失败而不是吞掉：参数应用回滚、capability 不支持、FG interpolation disabled、device removed、NVENC map/encode、音轨、配置损坏、显示受限和过期帧分别有原因。正常的显示器 Hz 限制是提示，不伪装成 SDK 故障。

验收：故障注入/非法参数分别产生准确事件；复制文本不含项目绝对路径或用户名；“打开日志目录”只打开既有本地目录，不创建上传包；关闭事件不删除原始日志；高频相同错误只增加 occurrenceCount。

## 9. 推进次序与每步交付物

以下是未来施工顺序，不是这一轮已经执行的事项。用 R 编号避免与旧 Phase 0–7 的历史状态混淆。每个任务只修改其必要范围，完成可用的一段就接入实际应用，不让 UI 再长期落后于探针。

| 任务 | 具体产物 | 通过条件 / 不允许的捷径 |
| --- | --- | --- |
| R0 合同准备 | 固定 A1–A7+B1–B5 范围、核对旧 Phase5–7 证据边界、当前 dirty tree 与 EXE 身份 | 不擅自 rebaseline/改门禁；不覆盖已有修改或用户删除的测试片 |
| R1 观察与能力 | FrameBatch/ResolutionPlan/Settings/Metrics/DiagnosticEvent 合同；查询 FG/NVOF/显示能力 | 先写失败用例；目标与实测拆开；未测显示率为未知 |
| R2 真正 2X | FrameBatch/资源租约、内容与索引修复、稳定输出时间线 | NR/SR 关闭时先证明不同中间帧与正确时序；再加回 NR。只调用成功不能过 |
| R3 SR4K+B1 基础 | ResolutionPlan、源侧共享光流、SR/NR/FG 适配器、低分辨率 NR 变化回填、同帧 reference | 日志证明底图4K/NR1080/FG输出尺寸；对比身份正确；不冒充 native4K |
| R4 多倍率与输出 | 泛化 MFG 2/3/4、Presenter/NVENC 描述符与资源池、CFR/尾部/音轨 | 每倍率真实子帧、PTS 和导出数量一致；不支持明确禁用 |
| R5 参数+B3 | 强类型 NR、残差 pass、实时快照、图片缓存、原子预设持久化 | 单参确实生效；整套事务回滚；调参/换预设不回到 0 秒 |
| R6 B4+内容节奏 | 共享光流三档、实际档位反馈、30/50/60 内容重复策略 | 每对 A/B 一次 NVOF；不把静态场景误当低帧率；fallback 有原因 |
| R7 B1/B2/B5 产品 UI | 对比拖杆、性能面板、诊断中心接入实际软件 | 同帧对比；GPU/CPU 统计不混；诊断脱敏且不自动上传 |
| R8 中文与完整全屏 | 全部界面汉化、布局/DPI、快捷键、无边框多屏恢复 | 暂停/拖动/焦点/Alt+Tab/全屏恢复可用，不重建无关模型 |
| R9 联合短测与交付 | 新建独立 repair-v2 验收运行器、独立只读审查、更新用户指南与状态 | 不改旧 phase/delivery gate；所有新声明有当前证据；旧 PASS 不作新发布证明 |

R2/R3 的可见诊断控件随各自代码进入应用；R7/R8 做完整体验整合，不把可用界面推迟到所有算法研究结束之后。

### 9.1 拟新增模块边界

- `C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/engine/EnhancementSettings.h`：类型、合法范围和不可变快照。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/pipeline/FrameBatch.h`：帧身份和租约，不放 UI 逻辑。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/pipeline/ResolutionPlan.h`：尺寸与消费者坐标合同。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/src/engine/PresentationScheduler.cpp`：时间映射、到期/容量判断和统计，不发起另一套 NGX。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/shaders/NrResidualComposite.hlsl`：独立变化回填，不复制竞品代码。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/include/veyra/diagnostics/DiagnosticEvent.h`：结构化、可脱敏、固定容量的诊断事件。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/src/engine/PresetStore.cpp`：schema 校验和原子本地持久化，不包含任意执行能力。
- `C:/Users/123/Desktop/Veyra DLSS Video Player/shaders/CompareComposite.hlsl`：同帧 GPU 对比，不保存或回读用户素材。
- 测试放入现有 `C:/Users/123/Desktop/Veyra DLSS Video Player/tests/unit` 和 `C:/Users/123/Desktop/Veyra DLSS Video Player/tests/integration`；探针只组装生产模块，不再堆第二套算法。

若工程已有职责相同模块，优先扩展而不是机械新建同名概念；最终新增文件以施工时检查为准。

## 10. 五分钟测试合同

本节是授权施工后的测试设计。**本次文档编写没有执行这些命令或测试。**

用户要求测试最多五分钟。全部新增运行测试共用一个最多 300 秒的实际累计执行预算，包含重跑、诊断和人工操作计时；不是每个用例五分钟。构建单独记录，不得把运行测试藏到构建命令中规避预算。

建议分配：

| 累计预算 | 验证内容 | 留下的证据 |
| --- | --- | --- |
| 0–30 秒 | CPU 合同与合成图：extent、MFG 数量/PTS、零残差/默认值、配置拒绝、重复节奏 | 单元结果与失败用例，不需实卡 |
| 30–95 秒 | 30/50/60fps 合成运动短片，2X/3X/4X 的调用、内容、资源与时序；分配几个代表组合，不穷举笛卡尔积 | 每子帧 ID/有效性、人工端点位置、生成≠复制/混合、提交间隔 |
| 95–145 秒 | 1080p→SR4K 实时 NR、4K 实时 NR、短段原生 4K；GPU query timestamp | actual extent、每 pass GPU p50/p95、处理/丢弃、正常路径无逐 pass 等待 |
| 145–195 秒 | 参数、B1 对比、B3 预设、B4 三档、B5 故障注入、B2 面板、中文/全屏 | Applied revision、同帧身份、实际档位、脱敏诊断、GPU/CPU 统计 |
| 195–245 秒 | 短视频导出代表倍率，H.264/HEVC 与音轨、CFR/尾部、取消；联合检查 | ffprobe 实际帧数/时长/音轨，generated 与 hold 分开 |
| 245–300 秒 | 仅用于首个失败的有针对性修复后短测或遗漏的关键项 | 总时长和剩余未验证项；无失败时无需用满 |

测试材料：优先项目生成的非私人固定片与图片。诊断允许对这些合成图进行少量回读；不得默认保存用户采集画面、游戏录制或桌面截图。不主动重启当前应用或占用采集设备，实卡操作仍按用户当次许可执行。

### 10.1 明确验收，不承诺物理不可能

- 2X/3X/4X：在声明支持的尺寸/档位上，真实纹理数量、运动时刻和 CFR/PTS 正确；任何 disabled/reset 输出不计成功生成。
- SR4K 实时档：NR 不再被强迫在 4K 执行，底图与输出尺寸真实；需要源 50fps 时，报告实际持续处理能力和掉帧，不能以 35fps 乘 2 写“70fps 已完成”。
- 选择 50→100 且硬件/显示能力足够时，实测提交应接近目标并有稳定间隔；屏幕不够快时报告受限，不改测试阈值伪称完整显示 100 帧。
- 不要求本机原生 4K NR 达到 60fps。4K FG、SR 本身若仍超预算，分别报告瓶颈，不能把 NR 降分辨率后的总路线未经测量就称实时。
- GPU 耗时用 query heap 的 GPU timestamp；CPU 提交耗时不能充当 GPU pass 耗时。显示扫描/光子延迟没有测量设备或可靠反馈则未测。
- 短测只证明短时正确性，不能改写成 30 分钟耐久或所有采集卡兼容。预算不足的项目记录未执行；阻断真正依赖该项的完成声明，但不再空跑一整天。

建议在开工确认时固定以下数值验收口径，不等看到结果后再放宽：合成恒定源剔除明确的启动/reset 区间后，实际处理率至少达到源率的 98%；无显示上限的提交率至少达到有效目标的 98%；连续运行窗口内 ingress 丢弃不超过 1%；生成 PTS 与 j/N 的有理数期望差不超过一个输出时间基 tick。显示器不匹配、SDK 禁用生成或帧池背压必须分别报告，不从分母中悄悄剔除。可视运动内容与 GPU 性能仍需结合前述证据，数字通过不能替代内容正确。

### 10.2 授权后的独立验收入口

构建仍使用当前入口。新功能验收必须新建 `scripts/acceptance/repair-v2.ps1`，不要改写或转发旧 Phase5–7/delivery gate；下方第二条命令是施工后应提供的目标入口，目前尚不存在：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\123\Desktop\Veyra DLSS Video Player\scripts\build.ps1" -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -Preset x64-release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\123\Desktop\Veyra DLSS Video Player\scripts\acceptance\repair-v2.ps1" -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -MaxRuntimeSeconds 300
```

新运行器在自己的子进程之间传递剩余时间预算，输出独立 run-id、每项命令/退出码/耗时/证据路径，不更新 Phase5–7 状态。可以只读运行旧测试二进制作为子检查，但不能调用旧 gate 后把其 PASS 当新版本结论。构建失败先修构建，不启动 GPU 用例。独立 Reviewer 读取本次真实证据、代码差异和遗漏项；不默认再开第二轮五分钟测试。

## 11. 依赖、运行时与禁止事项

复用当前本地依赖，不重新搭一套：

| 依赖 | 当前本地位置 | 用途与边界 |
| --- | --- | --- |
| DLSS SDK 310.7 | `C:/Users/123/Desktop/Veyra DLSS Video Player/third_party_local/nvidia/DLSS_SDK_310.7.0` | NGX 头文件、DLSSG 参数和现有静态库；不改接 Streamline |
| NVOF SDK | `C:/Users/123/Desktop/Veyra DLSS Video Player/third_party_local/nvidia/Optical_Flow_SDK_5.0.7` | NVOF 资源/格式/caps；不再把旧下载文件夹名字当阻塞 |
| NVENC 头文件 | `C:/Users/123/Desktop/Veyra DLSS Video Player/third_party_local/nvidia/nv-codec-headers` | 现有 D3D12 NVENC 路线；不打包系统 nvEncodeAPI64.dll |
| 本地运行时 | `C:/Users/123/Desktop/Veyra DLSS Video Player/runtime_local/nvidia` | 使用当前绝对路径加载规则，不依赖进程当前目录 |
| 配置 | `C:/Users/123/Desktop/Veyra DLSS Video Player/runtime_local/veyra.ini` | 版本迁移和新参数，保留现有用户选择 |

DLSSNR 仍使用根目录已知文件按现有 staging 机制进入本地 runtime。身份必须是：

```text
nvngx_dlssnr.dll
SHA256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
Size   165840496 bytes
Version 310.8.0.0
Signature Valid / NVIDIA
```

不得下载 Magpie 附件中的 DLL、运行其 OTA 脚本、从游戏/驱动缓存提取新 DLL、磁盘 patch、重签名、提交或上传运行时。现有 RenoDX add-on 不进入程序。参考 Magpie 的公开功能不代表接受 GPL 源码复用；本方案全部按独立实现估算，若未来要复制源代码，必须另行明确许可证与分发方案。

## 12. 文档、Goal 与恢复规则

本轮只新增本方案、功能清单，更新未锁定的状态/工作记录。没有改动 protected 控制面，也没有更新其 hash。

下一轮如果用户确认开始，并希望使用 Goal：

1. 先指出旧规则中“仅 2X、3X/4X 不承诺”与这次明确新增需求的差异；在 Goal 开始前取得控制面修订授权，列清要调整的条目。
2. 需要协调的现有文件包括 [AGENTS](<C:/Users/123/Desktop/Veyra DLSS Video Player/AGENTS.md>)、[产品规格](<C:/Users/123/Desktop/Veyra DLSS Video Player/VEYRA_PRODUCT_SPEC_V1.md>)、[施工手册](<C:/Users/123/Desktop/Veyra DLSS Video Player/VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md>)、[当前交付计划](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/ACTIVE_DELIVERY_PLAN.md>) 和适用的 Goal/门禁说明。安全、许可、真实证据规则不变。
3. `C:/Users/123/Desktop/Veyra DLSS Video Player/loop/CONTROL_HASHES.json` 不得由 Agent 为通过 preflight 自行重算放行；基线更新需单独明确授权和审阅差异。
4. 新计划采用 R0–R9 原子任务，但保持当前真实的 needs_review/未完成状态，不凭写计划创建阶段 checkpoint。
5. 实施时把任务、失败、新证据与唯一下一动作写入现有 loop 文件；同一故障最多三个不同且增加证据的假设。不得反复以“再测一轮”填满时间。
6. 同一 checkout 只由主 Agent 写入；独立 Reviewer 只读。没有 Reviewer 结论，交付写 needs_review，不冒称通过。
7. 禁止自动 push、发布、制作安装包或上传 artifact。技术完成与 proprietary runtime 的公开分发许可是两件事。

## 13. 开工和最终交付清单

开工前：保留已有 dirty tree；不要恢复用户删除的 `C:/Users/123/Desktop/Veyra DLSS Video Player/validation/fixed_clips/test_h264_1080p.mp4`；记录使用的代码/EXE/runtime 身份，读取当前规则，确认 A1–A7+B1–B5 固定范围和五分钟预算。

结束时必须报告：

- 哪些 R 任务完成、哪些未验证，而不是一个虚构的总完成百分比。
- SR/base/NR/FG/output 实际分辨率，默认实时档与原生档区别。
- 2X/3X/4X 的实际支持、真实帧/生成帧/保持帧数量，目标/提交/显示的差别。
- 参数中已生效、未证实、运行时不支持的各项；图片、视频、采集入口是否都接到同一设置。
- B1–B5 是否分别通过同帧身份、GPU 时间、预设事务、实际光流档位和脱敏诊断验收；缺一项就不能写“全部完成”。
- 构建命令与退出码、测试实际累计秒数、证据路径、已知风险和唯一下一动作。
- 中文和全屏的具体行为，软件打开方式；未接设备或未测 physical latency 不能写已验收。

本方案的终点是用户可操作、参数真实生效、输出真实且限制说清楚的软件，不是更多探针计数、更多未验证按钮或另一轮不设上限的实验。
