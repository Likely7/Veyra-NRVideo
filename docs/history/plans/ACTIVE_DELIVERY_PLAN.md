# 2026-09-06 用户直接接管执行计划（历史记录）

> 2026-09-18：当前入口为 [CURRENT_STATUS](../../CURRENT_STATUS.md)，当前施工为 [Video HDR / NVIDIA FSR4](VIDEO_HDR_FSR41_EXECUTION_PLAN_2026-09-18.md)。下文是历史记录，不是当前产品缺陷清单。
>
> 2026-09-10：用户已废弃旧 Loop 与本计划的强制推进顺序。当前按最新用户指令和对应修复方案执行，不运行旧控制哈希 preflight，不以旧 STATE/BACKLOG 阻塞修复。保留本文件供追溯，已有测试工具仍可直接用于相关回归。

授权原话：直接接手，先整理文档并制定详细计划，完成软件；测试最多五分钟；当前采集卡未接入，由用户亲自实卡测试；开启目标模式。

本修订替代旧 R0–R12 的严格串行施工顺序、30 分钟耐久和无人值守实卡验收要求；不撤销代码正确性、诚实证据、固定二进制身份或许可证要求。仅本次用户授权重建控制面 hash，此后恢复锁定。旧长测保留为历史，不重跑，也不冒充当前通过。

## 交付与验收

2026-09-07 补充用户决定：实测原生 4K NR GPU 约 22–23ms/帧、颜色还原约 0.28ms，用户明确回答“接受，默认实时档，原生 4K 可选”。因此播放器/采集默认采用明确标注的 1080 working 实时档（仍解码 4K 输入，最终显示缩放）；保留原生 4K 及 SR 4K 可选。视频导出保留原生 4K。禁止把实时档的 60fps 冒充原生 4K NR 60fps。本条与交付文档同步属于新的用户授权修订，控制面记录新 hash 后再次锁定。

- 交付一个本机可运行 Win32 程序：播放器、DirectShow 采集卡入口、图片/视频增强导出。三入口共用产品 EnhanceGraph，不能用三份 probe 拼壳。
- 保留 Windows x64/C++20/D3D12、4K SDR、实验 NR、可选 SR 与 FG 2X、NVOF 和明确的质量降级。Depth 是可选 provider，不可把常量深度称为 AI 深度，也不可把 Auto fallback 称为已实现 provider。
- 自动测试每次完整调用上限 300 秒（超时杀本次子进程并报告失败）；优先 10–60 秒窄测。最终联合视频测试也在 300 秒内；不以缩短测试声称证明长期稳定。不得循环重跑长测消耗用户时间。
- 采集功能要实现枚举、格式选择、启动/停止、音视频接入和断连处理；硬件未连接标 `awaiting_user_capture_test`，不阻止交付给用户测试，但绝不写实卡通过。
- 本机交付与公开发布分开。未解决分发许可仍 `distribution_blocked`；不做安装包、不上传 runtime。目标完成指约定本机软件交付，不代表授权公开首发。
- 阶段状态表示验收，不禁止开发有独立价值的下一产品模块。允许 UI/controller 与底层缺陷迭代集成；不允许未通过底层被标成上层成功。新上下文只读 Reviewer 检查最终差异及短测，无需再重跑小时级耐久。

## 已核对的真实起点

HEAD 6f0ebaa；工作树只有用户测试片删除，保留。已有共享 `src/pipeline/EnhanceGraph.cpp`，不再说 GPU 链仅在 probe。旧 `src/core/EnhanceGraph.cpp` 是单元测试用计数实现，必须移除产品证明中的歧义。尚无可用产品 UI/capture/export。

当前优先缺陷：软件 NV12 upload 读取未初始化描述符、shader 把字节位型当浮点、chroma 宽度错误；NR 仍绑 Zero motion，NVOF 放在 NR 后；reset 不拒绝旧 NVOF；quality JSON 的 native extent/CPU 当 GPU 时间/硬编码 queue 和 fence/析构后 VRAM 都不能证明真实指标。旧结论“串行 runner 是唯一瓶颈”未经正确计时证明，撤回。

## 执行清单与顺序

### F0 文档与证据修复

更新 README/规则/Goal/State/Backlog，保留历史日志原文并追加纠错；建立本计划为唯一顺序，重建控制 hash，preflight。将无观测值改成 null/未实现，不能默认为 0。不把历史 30 分钟计数作为当前正确画面证据。

### F1 首先修真实画面输入

修 `src/pipeline/EnhanceGraph.cpp` 的 software NV12 ingestion：使用明确 rowPitch/plane footprint CopyTextureRegion 或完整有效的 raw SRV；每平面正确尺寸，YUV metadata 显式传递，upload buffer 用 fence 管所有权。修 `shaders/Nv12Upload.hlsl` 的字节对齐/归一化（若退出主路径仍不能留坏备用）。去除未经证明的“注入/驱动”归因。短视频 no-feature/NR 与 GBV 窄测，诊断抓帧验证非黑且随输入变化；诊断 readback 不进入正常路径。

### F2 共享质量与生命周期

post-SR 原始 working color -> NVOF A/B -> densify/confidence -> NR parity encode/evaluate/decode -> FG -> sink。canonical flow 为 current→previous working pixels，FG 单独适配；first/reset 禁止旧 flow，scene/seek/resize/source-switch 全历史 reset。复用 NvOfSession 和既有已证明 raw/32 契约。统一资源池/command slots，先消灭错误再优化同步。用 query heap+queue frequency 得到真实 GPU 时间，CPU enqueue 时间单列。临时缺失 depth 显示 Motion Only，provider 资产许可/shape核验后再接可选推理，不写假成功。

### F3 产品播放器先落地

新增共享 engine/controller（不把 main.cpp 复制一份），worker 拥有 D3D12/NGX；UI 通过有界命令队列 open/pause/seek/stop/resize/toggle。Win32 窗口有打开文件、播放/暂停、进度拖动、NR/SR/FG/quality、全屏、状态/错误和最近文件；字幕/OSD 在 FG 后。复用 MediaFileSource、PresentSink、WasapiAudioSink。audio clock 做主时钟，不截断 lateness 来伪同步；性能不足明确显示，不隐性丢播放器源帧。打开 native4K和1080p→4K视频做短测。

### F4 图片与视频导出

图片用 Windows WIC 解码/方向/sRGB，接同一 graph；WIC PNG/JPEG 写临时文件、解码回验后原子改名，默认不覆盖。视频用系统 nvEncodeAPI64.dll 的 D3D12 NVENC，input texture/fence 与输出 bitstream slot 有界；FFmpeg 仅封装码流与保留/转换音频。需要 Video Codec SDK 的官方 header，先检查本地依赖，再核对官方可取的 SDK/header 与许可证；禁止从未知 DLL 猜 ABI。没有 SDK 只阻对应项，不耽误 UI/图片/capture。不得把 CPU raw pipe当合格视频导出。进度/取消/错误/partial恢复实际接通。

### F5 采集入口

使用 FFmpeg avdevice dshow（若依赖未构建 avdevice，则 Windows DirectShow 接入），真实 friendly-name/media-type/音频枚举；选择设备实际提供的1080p/2160p30/60。latest mailbox容量1，丢帧触发统一reset；无FG为0额外lookahead，FG为A/B一帧，质量模式最多A/B/C；不得宣称采集缓冲提供免费未来帧。无设备明确显示“未发现采集卡”，不造虚拟PASS。用户实卡清单覆盖选择格式、预览/音频、NR/FG、停止重开、断连、延迟与画质。

### F6 收尾与交付

首次依赖检查/设置/运行日志导出（排除媒体SDK和runtime）；短测覆盖视频播放与seek、开关、4K输入输出、PNG/JPEG、H264/HEVC导出解码回验。独立只读复核，不做并行写入。记录 exe/runtime/input/config身份、真实调用/退出码、输出位置、已知限制。交付可执行文件路径与最短操作说明，采集硬件验收单独留给用户。阶段门禁仍诚实报告未过项，不凭 UI 出现宣布完成。

## 依赖位置与构建

- runtime 固定 `runtime_local/nvidia/nvngx_dlssnr.dll`，SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`；启动绝对路径/身份检查，不联网换版。ReShade addon永不加载。
- NGX SDK：`third_party_local/nvidia/DLSS_SDK_310.7.0`；NVOF：`third_party_local/nvidia/Optical_Flow_SDK_5.0.7`；FFmpeg：`C:/veyra-deps/installed/x64-windows`（用 CMake cache 核对实际路径）。
- 构建：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root . -Preset x64-release`。
- gate：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`；phase gate重写为本次短测合同，缺项明确失败。
- 每轮改动前 JOURNAL 写任务/窄测，完成后 STATE/EVIDENCE/WORKLOG 写实际结果。失败三种不同假设仍无法推进时只阻塞该独立项，不重跑旧矩阵空转。不得无授权改驱动、关远程软件、删除 STOP、放宽画面正确性。
