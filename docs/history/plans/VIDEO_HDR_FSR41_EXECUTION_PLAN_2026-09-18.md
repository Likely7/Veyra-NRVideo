# Video HDR 与 NVIDIA FSR 4.1 施工方案

> 2026-09-18 最终决定：用户取消 FSR4 修复并要求回退，实验入口和接入已撤除。RTX Video HDR 保留；下文 FSR4 目标、测试和下一步均为已终止计划的历史记录，不再执行。回退结果见 WORKLOG。

日期：2026-09-18

用户后续授权本地测试 UI：在超分算法下拉中加入「FSR 4.1 AI（实验）」并随本地测试包携带独立 provider，取代本文此前仅诊断入口的阶段限制。默认仍关闭增强，不授予公开发布；画质与 RTX30/40 矩阵仍待验收。

本方案对应用户决定：先接入 RTX Video HDR，再设法把 FSR 4.1 AI 超分接入 NVIDIA 路径。用户已授权施工、构建和本地测试；不改全局驱动设置、不推送、不发布。当前进度见下方执行记录与 WORKLOG，不由计划推断测试成功。

## 目标与边界

### 首要目标：RTX Video HDR

在不破坏原生 HDR 直通的前提下，让 SDR 文件、采集卡和 Remote Play 视频可以在 Veyra 内转换为 HDR，并继续经过已有 NR、SR、FG、字幕和输出链。目标包含：

- HDR 显示：HDR10/PQ、BT.2020、RGB10/P010 合同明确。
- HDR 导出：HEVC Main10/P010，正确写入色彩参数；不把普通 PNG/JPEG 冒充 HDR。
- 转换只对 SDR 输入执行；原生 PQ/HLG 输入不重复 TrueHDR。
- Video HDR 输出可继续进入内部 FG；生成帧不得再次执行 TrueHDR。
- 能力不可用时只关闭 HDR 转换，播放器、普通 SDR 显示和导出仍可用，并记录真实 NGX 结果、SEH 和驱动原因。

### 第二目标：NVIDIA FSR 4.1.1

把 `int3rrobang/fsr4-int8-reverse-engineering` 的 provider 作为隔离实验后端，验证它是否能在 Veyra 的视频输入合同下稳定运行。目标不是把研究预览伪装成官方支持：

- 固定上游提交 `88635b94083965a7c3b5f64e099808b8ba2ce576`。
- 使用本机用户提供的官方 AMD 4.1.1 provider/SDK 数据，仅在 `E:/项目/Veyra/` 下构建；官方 DLL、提取 container、权重和生成 DLL 不进入 Git。
- 运行时必须记录 provider 版本、是否确实进入 4.1.1 INT8 ML、输入/输出尺寸和回退原因；禁止静默回退 FSR 3.1。
- 先做诊断工具和 A/B，未通过画质、时序、性能及设备移除门槛前，不把它加入默认 UI 或发布包。
- 失败时回退现有 FSR 3.1/普通缩放，不能让打开 FSR4 使播放链路失效。

## 开工前固定证据

施工前创建两个隔离点：

1. `codex/video-hdr-20260918`：从当前 main 创建；先完成 Video HDR 最小闭环和导出。
2. `codex/fsr41-nvidia-20260918`：从 Video HDR 分支的稳定提交创建；只放 FSR 4.1 provider/适配和实验探针。

开工已保存 Git 状态并创建存档 `0e3d4ac` / `checkpoint/pre-video-hdr-fsr41-20260918`，HDR 隔离工作区为 `E:/项目/Veyra/worktrees/video-hdr-20260918`。FSR 分支在 HDR 稳定提交后创建。所有构建、SDK 副本、日志、测试包和临时目录遵守 AGENTS.md 的 `E:/项目/Veyra/` 规则。

Video HDR 现有运行库身份（只读记录，不代表已执行）：

```text
third_party_local/nvidia/RTX_Video_SDK_1.1.0/bin/Windows/x64/rel/nvngx_truehdr.dll
SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B
Size 3955752 bytes, Version 1.1.0.0, Authenticode Valid NVIDIA
```

FSR 4.1 provider 的官方 DLL、SDK 路径和 SHA-256 在开工时重新登记；不能从游戏目录、驱动缓存或网络临时地址取文件。没有官方 4.1.1 provider 数据时，只能先编译无专有数据的 smoke/接口检查，不能宣称 FSR4 已接入。

## A 阶段：Video HDR 颜色合同

### A1. 拆分输入、工作和输出状态

修改 `EnhanceGraphDesc` 与相关状态，使下列字段独立存在：

- `sourceTransfer/sourcePrimaries/sourceRange/sourceBitDepth`：实际输入颜色描述。
- `hdrInput`：输入是否已经是 HDR；不能用输出目标推断。
- `hdrConversion`：SDR→TrueHDR 是否启用及其能力状态。
- `workingTransfer/workingPrimaries`：图内纹理的真实合同。
- `hdrOutput/hdr10Output`：显示或导出目标，不再作为输入 HDR 的代理。

重点检查 `src/pipeline/EnhanceGraph.cpp` 当前的 `hdrOutput && !hdrInput` 拒绝、FSR HDR 禁止、HDR 视频 SR 代理、调色/残差合成和 FG 输入资源。删除保护前先把所有使用 `desc_.hdrOutput` 的分支逐一改成正确字段。

### A2. 新增 TrueHDR backend

新增 `include/veyra/ngx/TrueHdrBackend.h`、`src/ngx/TrueHdrBackend.cpp`，独立封装：

- 绝对路径 `LoadLibraryExW` 加载 `nvngx_truehdr.dll`，不依赖当前工作目录。
- 初始化/关闭、参数分配、`Available`、最低驱动版本、`FeatureInitResult` 查询。
- 建立一次 Feature；帧级 Evaluate 使用共享 D3D12 资源、command slot 和 fence。
- 输入使用 SDK 支持的 RGBA8/BGRA8/RGB10 SDR 资源；从线性 FP16 工作图先做一次明确 SDR 编码，不把 FP16 直接伪装成 SDR。
- 输出使用 RGB10 PQ/BT.2020 或 FP16 scRGB 的一种明确路径；首个产品闭环优先 RGB10/PQ，以匹配已有 HDR10/HEVC 输出。
- 所有返回值、SEH、资源尺寸/格式、GPU 时间和失败参数进入日志；不照搬样例的逐帧 CPU 阻塞等待。

TrueHDR 每个源帧最多执行一次，结果进入后续 FG。实际 SDK 没有时序 reset 参数；暂停/seek 复用同一逐帧 Feature，其他历史模块仍按图合同 reset。尺寸、源、输出 HDR 状态及设备生命周期变化时随图释放/重建，退出释放。不能为暂停/seek 的每次刷新引入额外 CPU/GPU 等待。

### A3. 接入共享 EnhanceGraph

推荐首个顺序：

```text
SDR decode -> NR/SR/调色 -> SDR 编码纹理 -> TrueHDR -> FG -> 字幕/OSD -> sink
原生 HDR decode -> 现有 HDR 合同 -> FG -> 字幕/OSD -> sink
```

实现要点：

- 对文件、采集、Remote Play 共用同一 graph；不复制三条 TrueHDR 链路。
- 现有 SR→NR 与低延迟 NR→SR 都支持，但 TrueHDR 只放在最终 SDR 源结果到 FG 之间。
- NR/SR 关闭时，TrueHDR 仍必须能独立工作；TrueHDR 关闭时，原 SDR 行为逐像素保持。
- 字幕/OSD 在 FG 后按 HDR 参考白合成；避免字幕被 AI 转换或补帧。
- 光流、depth、confidence 继续作为 guidance 支路，不从最终 PQ 数值计算错误 motion。
- 原生 HDR 不经过 SDR→HDR；HDR 输入的 transfer/matrix/range 解析和旁路行为保持可追溯。

### A4. 显示和导出

检查并修改：

- `src/engine/VideoPresenter.cpp`：HDR swapchain、DXGI color space、跨屏/全屏重建和 HDR 开关。
- `src/sink/NvencD3D12Encoder.cpp`：P010/HEVC Main10 输入、fence、BT.2020/PQ metadata；不依赖显示器是否 HDR 才允许离屏导出。
- `src/engine/VideoExportJob.cpp`：SDR 输入启用 TrueHDR 后，导出颜色描述、MaxCLL/MaxFALL 只在有可信来源时写入；不能复制错误旧 metadata。
- 截图链：明确提供 HDR 原始格式或 SDR 映射图，普通 PNG/JPEG 标明为 SDR。

上述验收是开发期针对性检查，不恢复用户已经取消的导出门禁、结束逐帧扫描或额外耗时的强制校验。保留 API 错误处理和真实输出格式合同即可。

UI 首版只提供总开关、峰值亮度、对比度、饱和度、中灰和状态原因。参数范围遵循 SDK；保存设置并在暂停时立即刷新。显示“请求值”和“实际生效值”分开，失败不能只显示已开启。

### A5. Video HDR 验收门

自动检查：

- TrueHDR DLL 身份、绝对加载路径、Available/driver/FeatureInitResult。
- Create/Evaluate/Release 每阶段真实返回值和异常日志。
- 8-bit SDR、RGB10 SDR、原生 PQ、混合色彩描述、full/limited range。
- 720p/1080p/2160p、宽高比、resize、seek、暂停恢复、源切换、device lost。
- SDR 播放、采集、Remote Play、HDR 显示、HDR 导出、FG 2X/4X/6X 组合。
- 没有 HDR 显示器时仍可完成离屏导出和日志验证；没有 TrueHDR 能力时回退 SDR。

人工/画质检查：黑位、近黑渐变、203/400/1000 nit 色块、BT.2020 边界色、移动高光、字幕白位和场景切换。不能用“画面变亮”代替 HDR 正确性，也不能把显示器截图当作 HDR 数值证明。

Video HDR 阶段只有在“播放不回退错误、导出可解码、颜色 metadata 正确、FG 组合通过”后才允许合并到 FSR 分支。

## B 阶段：NVIDIA FSR 4.1.1 实验后端

### B1. 外部 provider 构建与隔离

在 `E:/项目/Veyra/deps/fsr41-nvidia-20260918/` 保存上游 checkout 和用户提供的 SDK 引用；在 `E:/项目/Veyra/build/fsr41-nvidia-20260918/` 构建。不得把官方 DLL、提取 container、t18、权重、生成 DLL 或完整 SDK 复制到 Git 工作区。

先运行上游 CPU smoke 和 provider 查询；再固定 FFX API 版本、导出符号、尺寸查询和质量档映射。输出文件名按 provider 要求保留，不在产品目录覆盖现有 AMD provider。

### B2. Veyra 适配层

在现有 `FsrSrBackend` 内新增显式实验 provider 分支，复用 graph 入口和资源生命周期；由绝对路径、精确版本与调用方槽位 ABI 区分，不另建重复 backend，也不把 4.1 provider 伪装成现有 AMD 3.1 provider：

- provider 路径、版本、哈希和实验状态显式记录。
- 查询版本后只选择确实为 4.1.1 INT8 的 provider；未知版本拒绝创建。
- 资源尺寸、dispatch 线程组、quality ratio、HDR 输入能力和 reset 合同逐项适配。
- Veyra 视频 guidance 只有估计光流和常量/代理 depth；按现有 current→previous、post-SR 像素单位合同传入，保留低置信度衰减。
- 不制造 jitter：视频没有真实引擎抖动；固定零 jitter 必须记录为视频模式限制。
- provider 创建/dispatch 失败时按帧或会话级回退现有 FSR 3.1/普通缩放，不锁死播放。
- seek、scene cut、PTS discontinuity、capture drop、resize、pause/resume、source switch、device lost 原子 reset。

### B3. 实验探针和 A/B

增加独立探针或扩展现有 `fsr_probe`，只组装产品 backend：

- 固定单帧、连续序列、reset 和 destroy；记录 provider 版本及每次 dispatch GPU 时间。
- 720p→1080p、1080p→1440p/2160p、2160p 原生、不同宽高比和奇数尺寸。
- 静态文字、细线、头发、纹理、快速移动、遮挡、scene cut、字幕前后。
- 对照普通缩放、现有 FSR 3.1、DLSS SR/VSR；同一输入帧、同一 PTS、同一输出尺寸。
- 记录显存、GPU 利用率、掉帧、设备移除、长时间稳定性和画质差异；不能只验证 Create 成功。

通过条件：至少两段各不超过 300 秒的持续处理无设备移除/卡死，记录两段之间是否重启，不能冒充连续十分钟；所有 reset 场景可恢复；相同素材动态 A/B 没有系统性拖影/闪烁/文字破碎。分别报告各分辨率的 GPU 时间、显存及可实时帧率，不承诺所有显卡原生 4K 实时。画质或稳定性未通过时保持隔离诊断入口；单纯性能不足应给出可用的尺寸/质量档及离线导出结果，不把整个后端一概判为不能用。

### B4. 硬件矩阵

先在能运行 INT8 provider 的 NVIDIA 卡上完成证据，再分别测试 RTX 30、RTX 40、RTX 50。不能用上游 GTX 1650 报告代替 Veyra 测试，也不能用 RTX 50 的结果替代 30/40。每张卡记录驱动版本、显存、provider 身份、输入尺寸、质量档、平均/P95 GPU 时间和失败码。

FSR 4.1 实验不修改 NVAPI 伪装、DLSS MFG 解锁或现有 FG 调度；这三者分别记录，以便定位是 provider、资源合同还是帧调度造成的失败。

## 许可与发布门

- Veyra 源码仍不包含 NVIDIA SDK/runtime、AMD 官方 DLL、FSR4 提取数据或本地构建 DLL。
- 发生源码移植时，在 `THIRD_PARTY_NOTICES.md` 增加固定仓库、提交、许可证、文件清单和 Veyra 改动；只借鉴参考项目不复制实现时也记录来源。
- RTX Video SDK 运行库适用 NVIDIA SDK 许可证，不能重新标为 GPL/MIT；发布前需要独立的 runtime manifest 和用户授权。
- FSR 4.1 研究 provider 的上游源码为 MIT，但 AMD 官方数据、提取字节和各 SDK 文件按其逐文件许可处理；不能把研究仓库的 README 当成分发授权。
- Video HDR 代码通过验收后仍不自动发布；新的运行库进入 Release 需要当前对话单独授权。

## 交付顺序

1. 建立 Video HDR 隔离分支、状态存档和本地 TrueHDR 身份记录。
2. 完成颜色合同拆分、TrueHDR backend、共享 graph 和 SDR HDR10 显示。
3. 完成 Main10 导出、字幕/OSD、设置保存、回退和专项日志。
4. 运行 Video HDR 自动门与实机矩阵；未通过项写明，不用旧 HDR 记录替代。
5. 从 HDR 稳定提交建立 FSR4 实验分支，构建上游 provider smoke。
6. 接入 FsrSrBackend 实验分支和独立探针，完成本机 A/B 并记录硬件矩阵缺项。
7. 只有两阶段证据齐全后才讨论 UI 默认项、Release runtime 和版本号。

## 执行记录

2026-09-18：已完成文档刷新及 Video HDR 首轮实现。最终颜色路线选择 RGBA8 SDR -> TrueHDR FP16 scRGB（1=80 nit）-> 既有 HDR sink，非直接 PQ 输出。共用图、live 参数、schema 20 保存、请求/生效状态、SDR 显示回退、HEVC Main10 导出已接入。

RTX 5070 / 616.56：Create/Evaluate/Release 0x1、SEH 0；HDR 独立、2X/4X/6X、NR+DLSS SR+6X、原生 HDR 回归通过。90 帧导出为 Main10/P010 等价编码、BT.2020/PQ，开发期 ffprobe/解码通过；移走 TrueHDR 后导出 SDR 通过。预设 roundtrip/迁移通过。实际 HDR 显示（当前 Windows HDR 未开启）、跨屏、物理采集、PS5 和 RTX 30/40 未执行，不据离屏测试宣称全链路实卡验收。

FSR provider 已从固定上游提交在外部目录生成/构建，完成六槽资源与同队列 reset 适配。CPU smoke、RTX 5070 真实影片和合成序列、奇数尺寸、NR+HDR+6X 组合均通过；首次创建耗时 23.7 秒，后续进程约 0.5 秒，不能忽略冷启动成本。画质尚未通过正式开放标准，保持诊断入口；30/40、实际 HDR 显示和完整动态场景矩阵未执行。实际命令和结果见 WORKLOG 与 LOCAL_HDR_FSR41_TEST_2026-09-18.md。
