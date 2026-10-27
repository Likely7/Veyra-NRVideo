# 帧同步与呈现节奏研究（2026-09-18）

## 范围与结论

用户反馈帧率不稳定、数字乱跳，要求研究 NVIDIA 对应技术。本轮只审计代码与官方资料，不修改呈现行为、不重建、不发布。审计对象是 1.4.2beta 所在 `codex/fsr41-nvidia-20260918` 当前工作树，包含未提交的 FSR4 回退。尚未收到本次反馈的显卡、倍数、刷新率、VRR 状态及复现日志，不能声称已定位用户机器根因。

确有相关技术，但解决的是不同问题：G-SYNC/VRR 对齐显示器刷新，Reflex 限制渲染排队，Blackwell 硬件 Flip Metering 精确安排生成帧的显示节奏。Veyra 已有基于 PTS 的软件调度，不能说完全没有 pacing；但直接 NGX 生成帧配合自有交换链，不等于已经接上 NVIDIA 官方 DLSS-G 呈现集成。

## 当前代码证据

- `src/engine/VideoPresenter.cpp:21` 固定 `vsync=false`；`src/gfx/PresentSink.cpp:121,297-301` 创建翻转交换链并使用 Present(0)，支持时允许 tearing。没有查到 frame-latency waitable object、最大呈现队列长度配置、Reflex Sleep/markers 或显示统计反馈。Present(0)+tearing 本身可与 VRR 共存，不能据此断言 G-SYNC 无效或 VSync 关闭就是故障根因。
- `include/veyra/engine/PresentationScheduler.h` 已按源 PTS 建立时间线；`EngineController.cpp:1154-1184` 对未到期帧等待，对过期生成帧丢弃。现有逻辑并非把整批生成帧无条件同时提交。
- `include/veyra/engine/LiveGpuScheduler.h` 的任务都在图的拥有线程运行；`EngineController.cpp:566,856,1019` 的调度推进、文件读取/解码与 graph.process 共用该线程。同步读取或 CPU 提交阻塞期间，已准备好的子帧可能无法按时提交。恢复后，while 循环可连续提交多张已到期且尚未过期的帧。这是明确的结构风险，尚无本次逐帧数据证明发生频率。
- `include/veyra/engine/FrameRateWindow.h` 使用最近 1 秒实际事件数计算速率；`FrameFlowWindow.h:30-34` 的 presentSubmitFps 统计成功提交事件，UI 也称“显示提交”。它不是面板扫描率。稳定输入在窗口边界少量上下跳动可以正常；大幅波动不能一概解释为计数误差。相同平均 FPS 也可对应完全不同的帧间隔。
- 图和呈现已经使用不同 D3D12 command queue/fence；这处理 GPU 依赖与资源寿命，不会自动消除 CPU 调度阻塞，也不证明实际显示间隔均匀。

## 官方技术与可用边界

1. **G-SYNC / G-SYNC Compatible**：显示器按可变刷新范围跟随输出，主要减轻撕裂和固定刷新节奏失配。需要兼容屏幕、正确驱动设置和可用呈现路径；软件不能给普通显示器添加 VRR，也不能靠 VRR 修复解码断供。
2. **Reflex**：对齐 CPU/GPU 工作、控制排队与延迟，提供测量标记。公开 NVAPI 有 `NvAPI_D3D_SetSleepMode`、`NvAPI_D3D_Sleep`、`NvAPI_D3D_SetLatencyMarker`。Sleep 的帧边界必须定义正确，不能给每张 MFG 子帧盲目套一次源帧限速，或阻塞负责音视频推进的线程。公开接口存在不代表它在 Veyra 的直接 NGX 多帧路径已经验证兼容。
3. **Hardware Flip Metering**：NVIDIA 说明 Blackwell 将 pacing 从 CPU 移到显示引擎；DLSS 3 的 CPU pacing 误差会在更多生成帧下累积。30/40 系不具备该 Blackwell 硬件能力，需要可靠的软件 pacing；50 系也不能仅凭型号就宣称 Veyra 已使用该能力。本轮没有找到并验证可直接套到当前自有交换链的公开通用 metering 接口。
4. **Streamline DLSS-G 官方集成**：当前文档要求配套 sl.reflex、正确帧编号和 Present 标记；其内部交换链有专用 pacer。waitable 信号只能有一个拥有者，否则竞争会破坏 pacing。官方动态 MFG 还要求与 Reflex limiter 配合。项目仍按直接 NGX 架构，不把这些 SL 接口当现成可调用能力，不顺手引入 Streamline。XeSS/FSR 的提供方调度同样不能被外层再限速一次。

## 建议修复顺序（尚未实施）

1. **先测间隔**：将已有逐帧 trace 的 PTS、GPU ready、目标提交时间、实际提交时间、过期计数，与 PresentMon/FrameView 显示事件对照。记录有效呈现模式、间隔 P50/P95/P99、最长断档、连续突发提交、丢帧和队列驻留时间。PresentMon 的显示事件是系统呈现证据，不是光子延迟实测；缺失或不可用字段不得伪造。
2. **消除呈现被阻塞**：先定位是 demux/decode、graph CPU 提交、资源等待还是显示背压。优先隔离阻塞的输入读取、保持有界预取；如仍需独立呈现调度，先定义线程拥有权、lease/fence、seek/reset/退出与字幕合成合同，不能简单跨线程调用现有 scheduler。迟到时不能连续挤帧来补齐计数，也不能无限排队积累音画延迟。
3. **补显示同步策略**：在自有交换链路径评估 DXGI 等待对象与最大延迟限制，统一 PTS deadline 和显示就绪背压；提供同步/允许撕裂的明确选择。VRR、固定刷新、跨屏和捕获兼容模式分别验证。VSync 不能作为每 pass 的 GPU fence wait，也不能同时争用提供方拥有的等待对象。
4. **最后评估 Reflex**：先验证直接 NGX 下真实帧/生成帧编号、双队列和 markers 语义，再用开关 A/B 评估。它是排队优化候选，不是无条件修复。30/40 保留 2X-6X，不以禁用补帧或偷偷改倍数作为完成。

所有步骤保持音频一倍速与源 PTS，导出不受预览同步策略影响。需要预览选帧时计数必须如实呈现；不通过平滑 FPS 文案隐藏掉帧。例：24fps×6=144fps，但 120Hz 屏幕每秒不能完整显示 144 张不同图像；保留 6X 生成选择与如实报告可显示帧数不矛盾。23.976×6=143.856，也不能按整数 144 时间基硬走。

验证至少覆盖文件 23.976/24/25/30/60、DLSS 2X/4X/6X、VRR 开关、窗口/全屏、字幕/HDR、采集输入及 seek/reset；先做针对性短测，再按新增证据扩大范围。RTX30/40 必须持卡实测，不能用本机 RTX5070 代验。本轮未执行构建、Create/Evaluate、PresentMon 录制或屏幕实测，以上不是性能验收结论。

## 已核对的在线来源

以下页面于本轮通过 HTTPS 实际读取；上游 main 文档会变化，实施时需固定使用的 SDK/提交与许可证。

- NVIDIA DLSS 4 与硬件 Flip Metering：<https://www.nvidia.com/en-us/geforce/news/dlss4-multi-frame-generation-ai-innovations/>
- NVIDIA G-SYNC：<https://www.nvidia.com/en-us/geforce/products/g-sync-monitors/>
- NVIDIA Reflex 开发入口：<https://developer.nvidia.com/performance-rendering-tools/reflex>
- NVAPI 公开声明与 Sleep 语义：<https://github.com/NVIDIA/nvapi/blob/main/nvapi.h>
- Streamline DLSS-G：<https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md>，重点 8.0、12.1、VSync/Independent Flip 和 Dynamic MFG。
- NVIDIA FrameView：<https://www.nvidia.com/en-us/geforce/technologies/frameview/>

本轮没有下载 SDK 文件到源码、复制第三方实现或新增构建产物。下一项具体工作：取得反馈机器信息并录制一次与 Veyra trace 时间对齐的呈现事件，确认主要断档发生在生成前、提交前还是显示端。
