# Veyra V1 产品与技术规格

> **2026-10-04 项目对齐**：本文为V1历史技术合同，不能将下面旧Win32/2X/SDR/禁止分发等历史范围替代后续用户授权。当前公开版2.0.3为Qt/QML新界面，具备PC/Xbox/PS5串流、分层NR、最高VFG8X和AMD/NVIDIA分包；具体支持与未验边界见 `docs/CURRENT_STATUS.md` 和2.0.3验收文档。当前仅本地NR性能优化，执行记录 `docs/PERF_EXECUTION_NR_2026-10-04.md`；已有倍速/字幕/RTSS/UI/全屏修复保留，不擅自推进HDR/Dolby PR。共享图、真实PTS/源帧、reset、颜色、必要同步、源码/二进制隔离继续适用。

> 2026-09-18：本文保留 V1 历史基线。当前发布能力、HDR/多声道扩展、最高 6X 及新功能状态见 [CURRENT_STATUS](docs/CURRENT_STATUS.md)。当前任务按[可关闭帧同步方案](docs/FRAME_PACING_EXECUTION_PLAN_2026-09-18.md)与[实际验收报告](docs/FRAME_PACING_ACCEPTANCE_2026-09-18.md)执行；NVIDIA FSR4 实验已由用户终止并回退。下文旧 SDR 限制、2X 范围和门禁不撤销后续已授权实现。单次测试最多 300 秒，未实测项明确报告。

版本：Launch V1.3（2026-09-06 接管重基线）

日期：2026-09-06
状态：用户已确认三入口首发边界；历史 Phase 5 产品级通过结论因 gate 与实现不符而撤销，组件证据保留。

## 1. 产品定义

Veyra 是一个把同一套 DLSS 增强图应用到三种输入的 Windows 本地工具：

1. HDMI/USB/PCIe 采集卡实时预览；
2. 本地视频播放；
3. 图片或视频增强并导出。

它解决的是“把最终像素组织成 DLSS 可消费的工程契约”，不是游戏 mod，也不承诺从视频像素恢复游戏引擎原生数据。

V1 完成的唯一判据：三个入口都能由一个可执行程序实际完成端到端任务，共同使用真实 Feature 18，并通过 4K SDR、长时间运行、恢复、诊断和输出复核；任何一个入口或 4K 主路径缺失，V1 都不完成。内部实现仍按原子任务递进，但不得用内部里程碑对外冒充 MVP 已发布。

### 1.1 2026-09-06 实现完整性补充

“共享一套增强引擎”必须同时满足：

- `EnhanceGraph::process` 或其明确拆分的 pass graph 自己提交真实 D3D12/NGX/NVOF 工作，而不是只增加 counter 后把工作留给 harness；
- Player、Capture、Image Export、Video Export 只通过稳定的 source/graph/sink 接口调用，不得从 3000 行以上的 probe 复制代码；
- probe 只能组装产品库和采集证据，不能成为唯一实现；
- native 4K 证据必须在 JSON 中记录 source/working/output extent 均为实际 3840×2160 路径；第二次运行 1080p 素材不构成 4K；
- 依赖 manifest 只能证明依赖身份或缺失后的显式 fallback，不能证明 provider 已实现或画质 gate 已通过。

不满足上述任一项时，即使旧 gate 返回 0，也必须重开对应阶段。

## 2. 为什么现在仍值得做

Magpie 和近期 GitHub 项目已经证明“窗口/图片/视频能调用 Feature 18”。所以 Veyra 不再把调用本身当护城河。

Veyra 的明确差异是：

- 物理采集设备是一级输入，不要求先开 OBS 或把采集画面放进另一个窗口；
- 播放与导出直接消费源文件，避免桌面二次捕获，保留 PTS、颜色和独立音轨；
- 三入口共享 D3D12 graph、Guidance、reset 和参数，结果可复现；
- 离线模式可以使用未来帧验证 motion/depth，实时工具不能；
- 画质诊断可显示 motion、depth、confidence、reset 和各 pass GPU 时间，而不是只有“开/关”。

如果最终实现只剩“打开视频然后 Zero Guidance 调 Feature 18”，项目应停止，因为那已经没有产品差异。

## 3. 首发承诺与明确边界

### 3.1 必须首发

| 场景 | 首发能力 | 成功定义 |
|---|---|---|
| Capture | Windows DirectShow/UVC 1080p/2160p 30/60 视频设备 + 同设备音频；低延迟/高质量缓冲模式；NR；可选 SR 与 FG 2X；窗口/全屏显示 | 真实 4K60-capable 设备；2160p60 连续 30 分钟；真实 Feature 18/FG；队列与 history window 有界；音频可听；每段延迟实测 |
| Player | 最高 3840×2160、23.976–60 fps、H.264/HEVC 的 MP4/MKV/MOV；播放/暂停/seek/loop/全屏；音频；外置/内嵌基础字幕；NR；可选 SR/FG 2X | 4K30 与 4K60 各连续 30 分钟 + seek storm；A/V drift 在门槛内；无旧历史泄漏；不能靠丢源帧维持播放 |
| Export | PNG/JPEG 输入输出；最高 3840×2160 H.264/HEVC 视频输入；D3D12 NVENC H.264/HEVC MP4/MKV 输出；可选 SR/NR/FG 2X；音频 remux/AAC；基础字幕策略明确 | 两种编码输出均可由 ffprobe/自身重新解码；帧数、尺寸、时长、音轨、字幕报告和 hash 满足 gate；取消/崩溃不留下假成功文件 |

### 3.2 首发不承诺

- HDR/10-bit；
- DLSSG 3X/4X；
- 所有厂商私有采集 SDK；
- VFR 原样导出；导出统一为用户选择的 CFR；
- AV1/ProRes、ComfyUI、浏览器 UI；
- 游戏端注入或 sidecar 原生 depth/motion；
- 安装包内附 NVIDIA 实验 runtime 或模型权重。

4K 指 3840×2160 像素能力，不自动包含 HDR。遇到 HDR/P010 内容必须明确拒绝或显示“需先转换为 SDR”，绝不能按 SDR 错解后继续处理。批量作业可以在单文件任务稳定后加入，但不能阻塞三个主入口的 4K gate。

## 4. 平台与运行边界

- Windows 11 x64；
- C++20、Win32、D3D12；
- NVIDIA RTX；DLSSG 参考 gate 以 RTX 5070 + HAGS on 为主；
- 直接 NGX，不同时接 Streamline；
- FFmpeg library 用于文件 demux/decode、DirectShow input、音频/字幕与 mux；
- libass + FreeType/HarfBuzz 用于 SRT/ASS 文本字幕渲染，最终字幕层位于 FG 之后；
- NVIDIA Video Codec SDK 13.1 头文件来自用户接受 EULA 后的本地目录；最终视频路径用系统 `nvEncodeAPI64.dll` + D3D12 resource/fence，不把 raw-frame `ffmpeg.exe` pipe 当首发实现；
- WIC 用于 PNG/JPEG；
- WASAPI shared/event mode 用于播放器与采集音频；
- NVOF 来自系统 `nvofapi64.dll`，编译头文件来自用户接受 EULA 后放置的本地 Optical Flow SDK；
- Depth Anything V2 Small / Video Depth Anything Small 仅在模型许可证、hash 和本地路径均确定时启用。

## 5. 不可伪造的输入现实

### 5.1 游戏原生 DLSS 可能得到

- engine depth；
- object/camera motion；
- exposure 与 camera matrices；
- jitter；
- HUD-less color、UI alpha；
- render/output subrect。

### 5.2 采集卡与普通视频实际只有

- 已经合成、色调映射和可能压缩的像素；
- PTS/duration；
- 文件或设备能提供的颜色 metadata；
- 独立音频/字幕（若容器仍保留）。

所以 Veyra 的 `depth` 和 `motion` 是 estimated guidance。UI 若已烧进像素，也无法完美分离。产品文字必须叫“估算/增强”，不得叫“原生等价”。

## 6. 统一数据契约

### 6.1 FramePacket

```cpp
struct FramePacket {
    GpuTexture color;          // canonical linear RGBA16F
    Rational pts;
    Rational duration;
    uint64_t sequence;
    ColorDescription colorInfo;
    FrameFlags flags;          // seek/cut/drop/resize/discontinuity/eos
    SourceKind sourceKind;     // capture/player/image/export
};
```

强制规则：

- 所有来源只在 ingress 做一次 range/matrix/transfer 解析和转换；
- `pts` 不用 `double` 猜，内部保持有理数/100ns 整数；
- source texture 的 D3D12 state 和 owner fence 必须随 packet 明确；
- packet 不允许隐含当前工作目录、全局设备或裸生命周期指针。

### 6.2 GuidanceFrame

```cpp
struct GuidanceFrame {
    GpuTexture motion;         // RG16F, current -> previous, workingExtent pixels
    GpuTexture depth;          // R32F, normalized relative depth
    GpuTexture confidence;     // R8_UNORM, 0 reject ... 1 trust
    bool reset;
    ResetReason resetReason;
    GuidanceProvenance provenance;
    uint64_t sourceSequence;
};
```

强制规则：

- motion 的方向、单位和分辨率不可按调用方临时猜；consumer adapter 负责缩放；
- NVOF S10.5 转换固定除以 32；
- 低 confidence 区域平滑衰减 motion；必要时整帧回退 Zero，但日志必须写明；
- depth 的 P02/P98 归一化使用时间 EMA，避免每帧呼吸；
- depth age 超限、切镜或 motion 不可靠时，Auto 不得继续喂陈旧 depth；
- NR、SR、FG 从同一个 frame sequence 消费一致 guidance/reset。

### 6.3 ResetCoordinator

以下任一事件触发一个单调 `resetEpoch`，在同一 source frame 边界重置 NVOF、depth filter、SR、Feature 18 和 DLSSG：

- open/close/source switch；
- seek；
- scene cut；
- capture frame drop 或 PTS discontinuity；
- pause 后长时间恢复；
- resize/quality-mode change；
- device lost/recreate；
- guidance provenance 发生不兼容切换。

禁止不同模块各自偷偷决定 reset，导致一半历史新、一半历史旧。

## 7. 统一处理图

```text
FrameSource
  -> Ingress color conversion (NV12/P010/YUY2/BGRA -> RGBA16F linear)
  -> Scene/cadence analyzer
  -> optional DLSS SR (V1: Zero Guidance; only when output > source)
  -> Guidance producer
       NVOF motion + cost at the post-SR working extent
       optional backward flow
       luma/depth/fb consistency -> confidence
       optional DAV2 depth + temporal reprojection
  -> parity encode -> Feature 18 -> parity decode
  -> optional DLSSG 2X
  -> strength/color mix + anti-ringing clamp
  -> UI/subtitle/diagnostics composition
  -> FrameSink (present / encode / image)
```

顺序约束：

- V1 的 SR 在 Guidance、Feature 18 和 FG 前；SR 使用显式 Zero Guidance，不消费尚未生成的 NVOF/DAV2；
- NVOF、depth、confidence、Feature 18 与 FG 全部工作在 post-SR working extent；SR bypass 时该 extent 等于 source extent；
- native 3840×2160 输入选择 4K 输出时 SR 必须 bypass；1080p/1440p 选择 4K 输出时才做 SR。UI 同时显示 source extent、working extent 和 present/export extent；
- FG 在 Feature 18 后；
- Veyra 自己生成的 UI、字幕和 OSD 在 FG 后；
- 已烧录在源视频中的 UI 只能通过 confidence/text mask 降低历史污染，不能假装分离；
- post mix 必须能以 `strength=0` 精确回到未增强基线，以便 A/B。

## 8. Guidance 与画质策略

### 8.1 Motion

实时/播放：

- 首选 NVOF；输入为 current/previous color；输出 current→previous；
- 优先支持硬件 grid 后 densify 到 post-SR `workingExtent`；
- cost 映射为 confidence；
- 在性能允许时运行 backward flow，以 forward/back error 降权遮挡区；
- SDK/硬件不可用时允许自有 compute Lucas–Kanade 作为明确标记的 fallback；Zero 只作最后回退。

离线：

- 保留前后各至少一帧；
- forward/back consistency、luma warp residual、depth residual、边界/out-of-frame 共同生成 trust；
- 不因“向量是 finite”就把 confidence 设为 1。

### 8.2 Depth

实时/播放：

- Depth Anything V2 Small，固定模型 shape，FP16；
- DirectML 与 Veyra 同一 D3D12 device/queue；优先使用 device tensor/I/O binding，禁止同步 readback 再 upload 的常态路径；
- `Low Latency` 默认禁用或低频执行；`Balanced` 默认 interval 4；
- 两次 inference 间用 motion/confidence 重投影；对 disocclusion 使用当前估计或降权；
- 模型输出用 P02/P98 + EMA 映射到 R32F relative depth。

离线高质量：

- 优先 Video Depth Anything Small 或逐帧 DAV2 + 双向时序过滤；
- 只使用允许商业使用的 Small 权重；Base/Large 不进入默认构建；
- future-looking 结果不能进入 `Low Latency`；可以进入明确标注额外帧延迟的 `Buffered Quality` 和离线导出。

Depth `Auto` 的决策依据：age、depth residual、motion confidence、scene cut、内容稳定度。二维/动漫/文字/快速切镜时允许退回 Motion Only；“有 depth”不等于“效果更好”。

### 8.3 Scene cut 与 cadence

切镜不能只靠一个固定全图平均差：

- GPU 64-bin luma histogram 距离；
- 缩略图 SAD；
- NVOF confidence collapse；
- PTS gap/duplicate；
- capture sequence drop。

至少两项同时越阈值或出现明确 discontinuity 才 reset，避免闪光/爆炸误判。所有阈值进入配置和日志。

捕获源若已经包含主机游戏的 FG 帧，再次 2X 可能放大伪影。V1 提供独立 NR/SR/FG 开关；source FPS 已达到 target FPS 时 FG 默认关闭，并报告 cadence 判定，不自动双重补帧。

### 8.4 采集后帧与延迟模式

把连续采集帧记为 `A、B、C`：

- 采集卡和驱动造成的延迟只是把 A、B、C 整体推迟送达；它不会让 Veyra 在 B 到达前访问 B。驱动内部缓冲不能算“免费 lookahead”。
- `NR Low Latency`：B 到达就处理 B，不主动等待 C；FG 关闭。ingress mailbox 容量 1，过期帧直接丢弃并 reset。
- `FG Low Latency`：要生成 A 与 B 中间的 A½，必须等 B 到达，因此算法需要一个 future-frame window。A½ 最早在 B 到达后才存在；稳定显示时理论附加下限约为半个源帧周期再加 GPU/pacing，保守调度可能接近一个源帧周期。60 fps 的一个周期为 16.7 ms，30 fps 为 33.3 ms；实际值必须测，不能拿周期公式冒充结果。
- `Buffered Quality`：等 C 到达后再最终确认 A/B 区间；B↔C 只用于 forward/backward consistency、depth stability、遮挡/切镜和 trust mask，不伪装成 DLSSG 的额外输入。它相对 FG pair mode 再需要一个 future frame，通常额外接近一个源帧周期。
- `Export Quality`：整个文件都可访问，允许更长 lookahead/双向分析，且不受交互延迟约束。

总延迟必须分项报告：`capture hardware/transport + driver queue + deliberate lookahead + GPU graph + present queue + display scanout`。Veyra 只能精确测量进入本进程之后的部分；没有外部高速相机或设备时间戳时，不得把内部延迟叫 click-to-photon。

如果玩家本人靠 Veyra 预览操控游戏，默认 `NR Low Latency` 或 `FG Low Latency`；如果采集卡 HDMI OUT 直通玩家显示器，而 Veyra 只服务观众、录制或第二屏，则允许 `Buffered Quality`，因为控制延迟走直通链路。

画质收益不是线性的：从只见 A 到等到 B，才能做真正的 A↔B 双向 flow、遮挡判断和中间帧，这是主要收益；再等 C 主要改善加速度、切镜确认和深度稳定，通常是次要收益。默认不能为了很小的 C 收益牺牲交互延迟。

### 8.5 Preset 与后处理

- Feature 18 暴露已确认的 NR preset/style/intensity/local tone/local structure/skin 等参数；参数类型必须来自已验证契约；
- DLSS SR model preset 仅在官方 header/API 支持时使用；E/F/J/K/L/M 不得靠第三方 README 硬编码；
- 可做 `Natural`、`Old Video`、`AI Video`、`Game Capture` 四个配置预设，但预设只是参数集合；
- `Old Video` 可在 DLSS 前做轻量 deblock/deband，`AI Video` 可加强 scene-cut 与 history clamp；必须在 UI/日志中明确这些不是 DLSS 本身；
- 后处理只允许有界 detail/color mix、anti-ringing clamp 和可关闭 sharpening。默认不得叠加会掩盖 DLSS 真实输出的强滤镜。

## 9. 三种模式的具体规格

### 9.1 CaptureSource

实现：FFmpeg `libavdevice` 的 `dshow` input；只支持 Windows 枚举到的设备。第一版不接 Elgato/Blackmagic 等私有 SDK。

必须：

- 枚举 video/audio device，显示 friendly name 与可用格式；
- 用户可选择设备真实公布的 1920×1080 或 3840×2160、30/60 fps 和音频设备；首发认证样本必须包含 2160p60；
- 接受 NV12/YUY2/MJPEG/H.264/HEVC 中设备真实支持的 SDR 格式；不能静默伪装成请求格式；P010/HDR 不得被错当 SDR；
- `fflags=nobuffer`、low-delay、受控 `rtbufsize`；ingress latest-frame mailbox 容量 1，图内 A/B/C window 最大 3；
- 处理落后时丢旧视频帧，计数并 reset temporal history；音频环形缓冲有上限；
- UI 显式提供 `NR Low Latency / FG Low Latency / Buffered Quality`，显示主动 lookahead 帧数和换算毫秒数；
- window/fullscreen present；VSync/tearing 行为显式；
- 分开显示 capture ingress、GPU graph、present queue 的 latency；不要把内部时间叫端到端 click-to-photon。

参考 gate：真实 4K60-capable 设备输入 2160p60，连续 30 分钟；ingress queue ≤1、history window ≤3；working set/显存无持续增长；device removed=0；A/V 可听；NR Evaluate 全成功；FG on 时内部生成 cadence 为 120 Hz 且生成帧不等邻帧/简单 blend。另跑 1080p60→4K 输出，证明 SR 路径。没有 4K 设备时 Phase 7 保持阻塞，不能拿文件或虚拟摄像头替代。

### 9.2 MediaFileSource + DisplaySink

必须：

- H.264/HEVC，MP4/MKV/MOV，最高 3840×2160、23.976–60 fps；优先共享 D3D12VA decode；软件 fallback 明示；
- 打开、播放、暂停、seek、关闭、全屏、窗口 resize；
- WASAPI audio master clock，视频按 PTS present；
- 10 次随机 seek 后首帧 reset；不得显示 seek 前历史；
- 支持外置 SRT 和容器内首条文本字幕；ASS/SSA 至少正确使用 libass 渲染基础样式。字幕一律在 FG 后合成，不能被 NR/FG 扭曲；
- EOF/drain、loop 和错误状态真实可见。

A/V gate：4K30 与 4K60 各循环 30 分钟，drift 绝对值 ≤50 ms；P95 present lateness、真实源帧 drop、解码 fallback 进入日志；不可用理论 FPS 替代。

### 9.3 ImageSource / ExportSource + EncodeSink

图片：

- WIC decode PNG/JPEG，应用 EXIF orientation；
- 单帧 NR 的 reset=1；motion/depth 默认 Zero；可选 SR；
- PNG/JPEG 输出，保留 ICC/EXIF 能力若已实现，否则明确报告丢弃；
- 不覆盖原文件；先写 `.partial`，验证后原子 rename。

视频：

- 使用同一文件 demux/decode/EnhanceGraph；不允许另写一个 Python 核心；
- 输入/输出最高 3840×2160；输出 CFR 23.976/24/25/30/50/60/120 或 source nominal×2；DLSSG 只生成中间帧，时间戳严格位于相邻源帧之间；
- Feature 18/FG 输出在 GPU 上转换为 NV12；10-bit/HDR 尚未支持时不得生成伪 P010；
- 使用 Video Codec SDK 13.1 的 D3D12 NVENC：注册 `ID3D12Resource`，为每个 slot 提供 input/output fence point，编码 H.264 或 HEVC；禁止整帧 GPU→CPU raw readback；
- 只把压缩 bitstream 交给 FFmpeg `libavformat` mux；音频优先 remux，容器不兼容则 AAC；字幕可兼容时复制/转换，否则在开始前明确报告；
- cancel 时停止 producer、drain/abort encoder 与 muxer，然后删除仅本任务的 `.partial`；已经成功的文件不删；
- 完成后用 ffprobe + 自身 decoder 复核帧数、尺寸、FPS、duration、音轨和首尾非黑帧。

## 10. UI

一个 Win32 可执行程序，三个 tab：

- `Capture`：设备、格式、音频、输出尺寸、NR/SR/FG、质量档、开始/停止；
- `Player`：打开、播放/暂停、seek、全屏、NR/SR/FG、质量档；
- `Export`：图片/视频、输出路径/尺寸/FPS、质量档、开始/取消、进度；
- `Diagnostics` 折叠区：runtime identity、Feature 18 state、guidance provenance、depth age、reset reason、FPS、GPU ms、queue depth、dropped frames。
- 设置持久化、首次运行依赖检查、最近文件/设备、日志导出、崩溃后的 `.partial` 恢复/清理提示属于首发，不得留成“以后再做”。

首发 UI 可以朴素，但错误不能只进日志。任何 fallback（software decode、Zero motion、depth disabled、FG unavailable）必须在 UI 明示。

## 11. 性能预算

参考机器：RTX 5070、driver 616.56、Windows 11、HAGS on。

- 4K60 native input、NR-only：RTX 5070 参考机连续 30 分钟不得持续积压，源帧处理 drop <0.1%；若实际 P95 graph time 超过 16.7 ms，4K60 realtime gate 失败，不能静默降分辨率；
- 4K60 + FG 2X：内部 cadence 必须稳定产生 120 Hz 时间线；显示器不足 4K120 时可 offscreen 验证 generated output，但 UI 必须说明实际 present 上限；
- `NR Low Latency` 的 `lookaheadFrames=0`；`FG Low Latency=1`；`Buffered Quality=2`。这些数字表示生成当前区间需要看到多少后帧，不等于宣称显示延迟恰好为 `N/f`；实际 ingress→present 与外部端到端时间必须测量；
- Capture ingress queue depth ≤1，history window ≤3，GPU in-flight slots 4–6；禁止为了通过 4K gate 建立更深隐藏队列；
- Player 4K60：不因正常处理主动丢源帧；若算力不足必须明确暂停/降级所选 feature，不能篡改 PTS；
- Export：D3D12 NVENC，不回读 raw pixels；吞吐可以低于实时，但内存/显存有上限，working set 不随时长线性增长；
- 4K 运行必须记录 D3D12 budget/usage、各资源池峰值；RTX 5070 12 GB 参考机在峰值仍保留至少 1.5 GiB budget headroom，否则 gate 失败；
- 每个 pass 记录 GPU timestamp；不得用 CPU wall time代替 GPU cost；
- Depth 若让实时路径超预算，Auto 降低更新频率或关闭 depth，不能积压帧。

阈值如因真实硬件不可能达到，可以以实际日志向用户申请修订；Agent 不得自行放宽 gate。

## 12. 质量验证

固定 corpus：

- 细线/纹理慢移；
- 大幅高速平移；
- 遮挡与显露；
- 烟火/粒子/半透明；
- 人脸/皮肤；
- 动漫/二维画面；
- UI/字幕；
- 硬切、闪光、重复帧、掉帧；
- 老压缩视频；
- AI 生成视频的形变/纹理漂移。

每段至少输出 `off / zero / motion / motion+depth / auto`。保存：输入 hash、配置、runtime hash、输出 hash、flow/depth/confidence 可视化、reset timeline、GPU times。

“比 Magpie 好”的发布条件不是单个截图。必须同机、同输入、同输出分辨率/FPS、同类参数，至少在指定场景的 temporal residual/flicker 与盲评同时胜出，才允许写场景限定结论。

## 13. 安全与许可证

- `nvngx_dlssnr.dll` 固定 SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，size `165840496`，NVIDIA 签名；不修改、不提交、不分发；
- `renodx-dlss5-1.addon64` 固定 SHA256 `837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F`，size `359424`，未签名；不加载、不注入、不分发；
- 所有 DLL 以绝对路径和受限 `LoadLibraryExW` flags 加载；
- Magpie GPLv3 和无许可证竞品代码不得复制；
- NVOF SDK、DLSS SDK、ORT runtime、模型、FFmpeg binary 各自有 manifest、hash、license；
- 公开发布/安装包/上传 artifact 前必须单独完成分发许可审查；本规格只授权本机研发。

### 13.1 DLSS 5 官方发布后的项目决定

- NVIDIA 已正式发布 DLSS 5 消费者功能，不代表公开 SDK 已向任意 Windows 视频应用开放；“驱动中存在 runtime”也不等于拥有 header、Application ID、调用契约或分发权。
- 用户决定当前版本继续以固定身份的实验 `nvngx_dlssnr.dll`/Feature 18 为研发后端。该决定只改变开发优先级，不取消 hash、签名、绝对路径、可关闭和不得分发的约束。
- 不得从游戏目录、驱动 OTA 缓存或第三方 release 抽取替换 DLL；不得把外置依赖改名、patch 或藏进安装资源。
- 产品 UI 和日志必须显示 `Experimental local runtime`、实际版本/hash 与不可用原因。未找到精确 hash 时实验功能 fail closed，应用的非 DLSS 诊断/设置界面可以启动。
- 公共 SDK 后续若真正提供，必须新增独立 `OfficialDlss5Backend` 或做一次有证据的后端替换；不得把未知 ABI 当成当前 Feature 18 的原位升级。

## 14. V1 Definition of Done

同时满足：

1. Phase 0–7 gate 与独立 Reviewer 全通过；
2. 三个 tab 均有真实端到端录屏/日志/输出证据；
3. Capture 30 分钟、Player 30 分钟、Export 固定样本全部通过；
4. Feature 18、SR、NVOF、DLSSG 的真实 Create/Evaluate/return code 进入日志；
5. Guidance 可视化证明不是 Zero 冒充 motion/depth；
6. 图片/视频输出非黑、非恒定、非简单复制；FG 帧非重复/线性混合；
7. A/V、PTS、frame count、duration、reset、latency 和内存达到门槛；
8. proprietary/local assets 未被 Git 跟踪；
9. UI 明示所有 fallback 和实验性质；
10. 不声明未验证的“原生等价”或“全面优于 Magpie”。
11. 4K30/60 Player、4K60 Capture、4K H.264/HEVC Export 的独立证据齐全；不能用 1080p→4K 的单次 SR harness 冒充 4K 产品支持；
12. 设置、依赖诊断、设备丢失/重新打开、导出崩溃恢复和日志导出达到首发行为；
13. 若 NVIDIA/模型/FFmpeg/编码器分发权尚未解决，只能标记 `release candidate / distribution blocked`，不能生成并公开上传带受限资产的安装包。
14. 所有“passed”状态必须由当前 Product Spec 对应 gate 支持；发现 harness-only、假 4K、manifest-only 或阈值被缩短的旧 gate 时必须回滚状态并重验。
