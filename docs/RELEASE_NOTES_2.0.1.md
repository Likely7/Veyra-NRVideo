# Veyra 2.0.1

2.0.0 发布后的修复版。**2.0.0 的完整更新内容保留在下方**，从 1.4.4 升级的用户请一起看。设置与 2.0.0 共用，覆盖解压或换新文件夹都可以。English follows the Chinese section.

## 中文

### 2.0.1 修复

- **导出时显卡被系统重置，不再整段作废**：8K、长视频开 NR 导出时，有 40 系用户遇到驱动卡死并被 Windows 重置。现在已导出的部分会自动保存，新开一个导出进程从断点接着导，最后无损拼接成一个文件，不重新编码；最多续 3 次。这是重置后的恢复，不代表驱动挂起根因已修复；已用模拟设备移除验证续导，8K 反馈者实机结果待确认。
- **全屏显存异常增长保护**：设置稳定后检测持续增长，自动退出全屏并在本次播放中限制再次进入，保持片源、NR 和补帧设置，用窗口继续播放，避免原先反复重建却继续增长。已分配的异常显存不保证释放，重新开程序可重测。**这是应急保护，5060 Ti 的 4K 全屏泄漏根因尚未确认、现场结果待验证**；本机加载同一 NVIDIA 插件也能稳定播放，不能仅凭模块出现就认定它是原因。设置中的 OBS 游戏采集兼容可用于切换界面显示路径后重测。
- **Xbox 空值应答导致协商中断**：成功应答里的 `null` 错误字段不会再触发字符串异常；HTTP 200/202 的未完成应答继续有限轮询，数字错误码保留，空 ICE 元数据不会再次中断连接。协商失败显示实际阶段，不再一律提示 UDP 被拦截。本机 80 项协议/本地 WebRTC 测试通过，反馈者真实 Xbox 连接结果待确认。
- 以下几项已在 2.0.0 的更新包里修过，2.0.1 一并包含：
  - 输出稳定器·抗闪烁打开再关闭后画面定住；
  - 首页"继续上次"的采集卡没有声音；
  - 多层 NR 时性能球只算一层；
  - 调色"效果"组的纹理 / 清晰度 / 去朦胧没有效果；
  - 点击任务栏图标不能最小化；
  - 采集开始时显卡设备丢失或崩溃，且检测到 NVIDIA App 画面插件或 RTSS 注入时，提示去哪里关闭。

对应源码：`Veyra-2.0.1-source.zip`；第三方依赖源码与 2.0.0 相同，`Veyra-2.0.0-dependency-source.zip` 随本 Release 一起提供。运行库没有变化。

## English

### 2.0.1 fixes

- **An export survives a GPU reset**: some RTX 40 users encountered driver hangs/resets during 8K or long NR exports. Completed parts are kept, a fresh export process resumes, and parts are joined without re-encoding (up to three resets). Recovery was tested with injected device removal; the original driver's hang and the reporting user's 8K result remain unverified.
- **Protection against sustained fullscreen VRAM growth**: after settings settle, sustained growth makes Veyra return to windowed playback and restrict fullscreen re-entry for that source session, keeping the source, NR and frame generation settings. It avoids repeated rebuilds that do not reclaim memory. Retained memory may require restarting Veyra. **This is containment; the RTX 5060 Ti 4K fullscreen leak and its field outcome remain unconfirmed.** The same NVIDIA plug-in is present in stable local playback, so its presence alone does not establish the cause. OBS game capture compatibility in Settings provides another UI display path for testing.
- **Xbox nullable replies interrupted negotiation**: null error fields in successful responses are accepted; pending HTTP 200/202 wrappers are polled within a bounded budget; numeric service errors are preserved; nullable ICE metadata no longer aborts connection setup. Failures report their actual stage rather than always blaming blocked UDP. All 80 local protocol/WebRTC checks passed; the reporting user's real-console connection still needs confirmation.
- Already in the refreshed 2.0.0 package and included here: the picture froze after turning the output stabiliser (anti-flicker) on and off; "Continue" on a capture card had no sound; the load gauge counted one of several NR layers; colour Texture / Clarity / Dehaze had no effect; clicking the taskbar button did not minimise; when the GPU device is lost or Veyra crashes as a capture starts with NVIDIA App's plug-in or RTSS injected, Veyra says where to turn them off.

Corresponding source: `Veyra-2.0.1-source.zip`; third-party dependency sources are unchanged from 2.0.0 and `Veyra-2.0.0-dependency-source.zip` is attached to this Release too. Runtimes are unchanged.

---

# Veyra 2.0.0

从 1.4.4 升级：全新 QML 界面、节点处理、多层 NR、PC/Xbox 串流与新的导出工作流。以下按“新增与改进 → 修复 → 已知边界”排列。English follows the Chinese section.

## 中文

### 新功能与重要改进

**1. 全新界面与操作**

- 新首页、最近使用/继续上次、极简观影、专业列表、节点画布、独立调色、导出和设置页；统一暗色外观、图标、字体、菜单、确认框、滚动条与动效。
- 全新圆角渐变黑底 Logo，EXE 与 Qt 窗口/任务栏图标统一。README 用新版专业模式截图替换旧动图，并重写操作教程。
- 简体中文、繁体中文、English、日本語，支持跟随系统；设置包含界面缩放、启动尺寸、快捷键、音频设备和单显卡 GPU 占用监控。
- 极简悬浮播放条可隐藏；全屏自动隐藏控制、顶部极简/专业切换、Home 快速调节与控制锁定。文件画面点击暂停、双击全屏、左右键跳转。
- 文件比例/旋转信息、适应/原始/填充、原画/增强分屏、按住 V 对比、强制 SDR 预览、截图、字幕/音轨入口和高级字幕设置接入新界面。

**2. 节点模式与多层增强**

- 新增真实节点处理链：添加、端口拖拽/点击连接、拖到线上插入、拖离断开、右键删除/复制/重置、中键平移、滚轮缩放、适配视图与自动排列。
- NR 最多四层，每层独立 Feature 实例、内部尺寸与参数；列表总开关支持恢复各层开关状态。调色可多实例，NR/调色可在允许拓扑中穿插于超分前后。
- 复制节点保留参数并旁置为未连接副本；编辑草稿与最后有效运行链区分，非法/断线状态明确提示，不将可见节点冒充运行节点。
- 列表/节点参数、会话和预设独立保存；切换恢复各自状态。个人预设支持内容选项、导入/导出、启动默认，取消强加内置预设。
- 输入后共享光流、单个超分、Video HDR 在补帧前、单个末端补帧；不是任意分支混合图。**节点离线导出延期，2.0.0 只支持列表离线导出。**
- 列表提供全局 NR 保护区域（含形状参数），排除区域内全部 NR 层并保留非 NR 处理；节点不提供保护区域。
- 调色补齐曲线、八色混色器、色轮、黑白/校准、组旁路/复位、逐参数复位、LUT 文件导入、颜色预设和原图对比。

**3. 增强后端、补帧与诊断**

- NR 提供 Lecram / SF-v2 社区运行版本选择并全链切换；新配置按显卡分档选择（RTX20/30/40 SF-v2，RTX50 Lecram），保留实验边界。
- DLSS FG 运行库升级 310.9.1，接入记录来源的 Transfusion 兼容路径；50 系原生路径保留，30/40 系实卡仍待验证。DLSS 倍率按能力提供 2/3/4/6×。
- XeSS 选项与 SDK 能力对齐，支持适用环境中的 2/3/4×；FSR 3.1 与 FSR 4 ML 分开显示，FSR 为 2×，FSR 4 限 AMD 且需实际匹配 provider，不把回退 3.1 标成 4。
- 超分增加 5K/6K/7K 尺寸选项与独立运动输入设置；运动估算质量、AMD 半分辨率光流、内容节奏、同步/低队列/输出上限在新版页面可调。
- 输出稳定器/抗闪烁控制、逐层 NR/节点 GPU 最近一秒平均耗时、帧时间折线、处理预算与排队诊断。软件提交 FPS 不等于物理显示帧数。
- 崩溃转储、设备移除/DRED 停止点、加载模块、采集驱动帧龄、显存回收诊断与分阶段重建 watchdog；这些诊断不等于已根治 TDR/显存增长。

**4. 更多来源与采集优化**

- PC 串流：Moonlight/Sunshine 协议、发现/手填主机、PIN 配对、应用列表、身份保存、键鼠/手柄输入与统计。主机 Sunshine 自行安装。
- Xbox：账号设备码登录、主机会话与原生 WebRTC、首页/弹窗入口；非官方实验功能，不保证所有服务/主机环境可用。
- 串流接入硬件 AV1/HEVC 解码、配对保持与高码率默认配置；PS5、PC、Xbox 共用现有增强链，不另造三套效果器。
- 美乐威 Pro Capture SDK 低延迟入口；普通采集路径保留。原生格式采集上传减少拷贝，MJPEG 多解码器并行并直接写 NV12；高负载下减少额外排队。
- 已有特定 KUHAIMI 27P/RTX5070 静态测试中，2K60 NV12 采集回调至 Present 返回约 1.95→0.90 ms，4K30 约 3.2→1.47 ms；**只代表该软件内部区间，不是采集卡固有延迟或屏幕端到端延迟，不外推所有设备。**

**5. 新导出工作流**

- 多文件可编辑顺序队列、每任务独立设置快照、剪辑范围/缩略图、MP4/MKV、码率直接输入、预计体积与单项剩余时间。
- 多音轨与内嵌字幕保留全部/指定轨道；自动保留时跳过不支持封装的轨道并提示，手选不兼容轨道则明确失败。
- 暂停、取消、重试、完成提示音；默认 VBR 8 Mbps。默认开始导出即关闭当前播放/采集/串流以释放 GPU，可关闭此选项。
- 离线导出完整处理源帧；节点模式不导出，FSR/XeSS 实时补帧不等于新增对应离线补帧支持。

**6. OBS 游戏采集兼容**

- 设置 → 通用与外观新增持久化开关（默认关闭），保存后询问“是/否”重启。是正常释放播放器后重新启动，否下次生效；导出时提示等待。
- 软件绘制 UI 避免 UI 与视频 DXGI 交换链竞争，视频/NR/SR/FG 仍走原 GPU 链；部分界面阴影和模糊简化。
- 游戏采集针对视频，完整 UI 使用 Windows 10 (1903+) 窗口采集。该开关不是 RTSS/游戏加加适配。

### Bug 修复

- **多层与状态**：修复堆叠 NR 黑帧/仅一层时域执行、层参数复制隔离、列表/节点配置互相污染、预设恢复混用、断线草稿改动污染运行链、非法倍率写入预设、参数复位与滑条外部状态不同步。
- **补帧与呈现**：FSR 改独立生成纹理，修复同窗口热切换资源生命周期；FSR 4 拒绝后 UI 正确回滚；修复 XeSS 临时字符串迭代器未定义行为和倍率入口；低延迟队列不再误改 NR/SR 顺序；垂直同步保持低深度排队。对比模式明确显示“补帧暂停”，避免误报无效。
- **窗口与 UI**：修复切屏/DPI 崩溃、视频覆盖控制条/菜单/提示、极简黑边与播放条裁剪、拖进度误拖窗口、空页面无法选源、竖屏适配、全屏任务栏/模式切换、Alt 导致输入停顿、弹窗不再出现、输入帧率被刷新打断、滑条支持直接输入与方向键。
- **信息显示**：GPU 占用不再累加多块显卡；阶段时间条不反复重建闪跳；列表显示每层 NR 耗时；负载预算跟随半速节奏；首页卡片稳定更新副标题，减少重建时点击丢失并增加导航日志（未将未复现的 Xbox/PS5 误开报告宣称为已完全定位）。
- **采集/串流**：美乐威 SDK 模式停止并行 DirectShow 视频以避免争用；ResizeBuffers 失败退避；修正采集颜色控制边界、MJPEG 丢帧过度重置历史、音频设备选择保存、源切换残留画面；采集/串流可以暂停再恢复；保存配对、解码路径与 Reflex 卸载修复。
- **导出**：取消后可重试、队列顺序/条目状态、格式不兼容字幕处理、暂停时 ETA 继续走、重复 ETA、慢 GPU 帧被固定两秒超时误杀。现按设备状态等待并记录慢帧/故障，不将真实 GPU 卡死当作导出成功。
- **OBS**：默认 GPU UI 下复现缩放崩溃和捕获 UI 而非视频；新兼容模式规避多交换链冲突。此前 Vulkan 方案弃用，不计入成果。
- **图标**：EXE 新图标及 Qt 窗口/任务栏图标显式设置，解决任务栏默认占位图标。

### 下载、验证与已知边界

下载 `Veyra-2.0.0-win64-portable.zip`，完整解压运行 `veyra_qml_ui.exe`；保留旧版，勿用旧配置目录覆盖。新版操作、节点教程与架构见 README。附源码、依赖源码、运行清单与 SHA256。

本地基底完成 19 组回归、10 项导出场景；OBS 候选四种增强组合 720 次缩放、8 轮最大化/还原通过，用户确认有效。最终开关重启三路径和实际 ZIP 解压播放短测通过。不同阶段证据分别记录，**不等于同一二进制已通过所有硬件和长期压力测试**。

仍未解决/未验证：部分 RTX40 NR TDR、显存持续增长根因、5090 特定 XeSS 闪退/内容节奏重复帧、部分 GC573/4K X 行为、长时间运行、真实 HDR 屏幕与端到端延迟、跨设备串流/手柄；30/40 FG 与 SF-v2、RX9000 FSR4 ML 不以本机5070代验。参数重建可能短暂停顿。RTSS/游戏加加适配取消，请避免对 Veyra 注入。

正式应用版本号不改变实验运行库边界：社区 NR、FG 优化内核等不是厂商认证；不宣称完整官方 DLSS 5 集成。允许用户替换 DLL，但不保证 ABI/驱动兼容。

## English

### New features and major improvements

**1. Rebuilt interface.** New Home/recent/resume, Cinema, Professional List, Node canvas, Colour, Export and Settings; consistent dark design, icons, menus, confirmations and animations. New rounded gradient-black EXE/window/taskbar icon and updated screenshot/tutorial. Simplified/Traditional Chinese, English and Japanese; configurable UI scale, startup size, shortcuts, audio device and monitored GPU. Hideable cinema controls, fullscreen mode switching/quick controls/locking, click-to-pause, double-click fullscreen, seeking, aspect/rotation, original/enhanced comparison, SDR preview, screenshots and subtitle/audio controls.

**2. Executable node editing and layered processing.** Add/connect/insert/disconnect/delete/duplicate/reset nodes; middle-pan, wheel zoom, fit and auto-layout. Up to four independent NR instances with individual parameters/resolution; multiple colour instances and supported ordering around a single SR. Duplicates start disconnected. Draft edits and the last valid runtime chain are separate. List/Node sessions, presets and settings persist independently; personal preset content choices/import/export/startup default replace imposed built-ins. Flow is shared after input, Video HDR precedes final FG, one FG backend is selected. This is not arbitrary branching. **Offline export is List-only; node export is deferred.** Global NR protection is List-only. Curves, mixer, wheels, B&W/calibration, group bypass/reset, parameter reset, LUTs and colour presets are connected to real processing.

**3. Enhancement backends and diagnostics.** Lecram/SF-v2 NR selection across the chain; new configurations use SF-v2 on RTX20/30/40 and Lecram on RTX50, still experimental. DLSS FG 310.9.1 with attributed Transfusion compatibility; native RTX50 path retained, RTX30/40 hardware validation outstanding. DLSS 2/3/4/6× and XeSS 2/3/4× follow capability limits. Separate FSR3.1/FSR4 ML 2× entries; FSR4 is AMD-only and requires the actual matching provider. Added 5K/6K/7K SR choices, motion inputs, flow quality/AMD half-resolution, cadence/sync/queue/cap controls, output stabiliser, per-layer/node last-second GPU timings and frame-time/load displays. Crash dumps, device-removal/DRED, module inventory, capture frame age and staged VRAM recovery diagnostics improve investigation, not proof that hangs/leaks are fixed.

**4. Sources and capture.** Moonlight/Sunshine PC discovery/manual hosts, PIN pairing, apps, identity persistence, input and statistics; Sunshine is installed separately. Unofficial Xbox device-code login, session API/native WebRTC and UI. Streaming hardware AV1/HEVC, pairing retention and high-bitrate defaults; all sources reuse the enhancement engine. Magewell Pro Capture low-latency SDK option, reduced native capture upload copies, parallel MJPEG decode/direct NV12 and reduced extra queueing under overload. Specific KUHAIMI27P/RTX5070 static tests measured capture-callback-to-Present-return improvements (2K60 NV12 ~1.95→0.90 ms; 4K30 ~3.2→1.47 ms). **These are internal software intervals, not card latency or screen-to-screen latency and not a universal device result.**

**5. Export workflow.** Ordered editable multi-file queue, per-job snapshots, trim/thumbnails, MP4/MKV, typed bitrate, estimated size and one current-file ETA. Keep all/select audio and embedded subtitles; automatic retention skips incompatible tracks with a notice, explicit incompatible selections fail. Pause/cancel/retry/completion sound; default VBR8Mbps and closing playback/capture/streaming on export (optional). Offline export processes source frames fully; List-only and no implied new XeSS/FSR offline FG support.

**6. OBS compatibility.** Persistent default-off Settings switch asks Yes/No to restart; Yes shuts down normally and relaunches, No applies next launch; active exports defer restart. Software UI avoids competing UI/video DXGI chains, while native GPU video/NR/SR/FG remain unchanged; some shadows/blur simplify. Game Capture targets video; use Windows10(1903+) Window Capture for the full UI. Not an RTSS/GamePP adaptation.

### Bug fixes

- **Layers/state:** stacked NR black frames/only one temporal instance, copied parameter isolation, List/Node contamination, mixed preset restoration, drafts changing live state, invalid FG multipliers and reset/slider binding synchronisation.
- **FG/presentation:** independent FSR output textures and safer hot switching; rollback UI after rejected FSR4; XeSS temporary-iterator undefined behaviour/capability controls; low queue no longer changes NR/SR order; shallow vsync queue. Comparison explicitly reports FG paused.
- **Windows/UI:** display/DPI crashes, picture covering controls/menus/tooltips, cinema bars/clipping, seek dragging the window, empty-source selection, portrait layout, fullscreen/taskbar/mode transitions, bare Alt stalls, disappearing dialogs, typed frame-rate resets; editable sliders and arrow keys.
- **Readouts/navigation:** monitor one GPU instead of summing adapters; stable stage bars and per-layer NR timings; half-rate budget; stable Home cards and navigation logs. The un-reproduced Xbox/PS5 wrong-dialog report is not claimed conclusively diagnosed.
- **Capture/streaming:** stop competing DirectShow video in Magewell SDK mode; resize-failure backoff; colour-control boundaries, excessive history resets on MJPEG drops, remembered audio selection, stale source picture, live-source pause/resume, pairing/decoding and Reflex unload fixes.
- **Export:** cancellation retry, queue/job state, incompatible subtitle handling, paused/duplicate ETA and slow GPU frames incorrectly failing after two seconds. Device faults/timeouts remain explicit failures with diagnostics.
- **OBS/icons:** compatibility mode avoids reproduced multi-swapchain resize crashes/UI-only capture. The failed Vulkan experiment was discarded. Explicit Qt window icons replace generic taskbar placeholders.

### Download, validation and limitations

Extract `Veyra-2.0.0-win64-portable.zip` into a new folder and run `veyra_qml_ui.exe`; retain the old version and do not overwrite with old settings. See README for the new workflow, node guide and architecture. Source/dependency archives, manifests and SHA256 are provided.

The local base passed 19 regression groups and 10 export scenarios. The OBS candidate passed 720 resizes/eight maximise-restore cycles across four enhancement configurations and user validation. Final restart paths and an extracted-package playback smoke test passed. These are separately recorded stages, **not certification of all hardware or long-term stability for a single binary**.

Outstanding: some RTX40 NR TDRs, VRAM growth root causes, specific RTX5090 XeSS/cadence reports, GC573/4K X cases, long runs, HDR displays/end-to-end latency and cross-device streaming/controllers. RTX30/40 FG/SF-v2 and RX9000 FSR4 ML require their own hardware validation. Rebuilding processing may stall briefly. RTSS/GamePP adaptation was cancelled; avoid injecting them into Veyra.

The application version does not remove experimental runtime limits. Community NR/FG kernels are not vendor certification or a complete official DLSS5 integration. DLL replacement is allowed without an ABI/driver compatibility guarantee.

## 支持与反馈 / Support & feedback

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.0.0/docs/images/2.0.0/community-group.png" alt="Veyra 交流群 4" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-09 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-09.
