# Veyra 2.2.1

## 简体中文

### 采集、低延迟与播放

- 采集先显示原画，模型在后台准备完成后切换到增强画面。专业模式的片源栏显示初始化和切换进度。连接采集源默认进入专业模式。
- 改善 NV12/YUY2 等原始采集的帧节奏、相近时间戳和断续帧处理，修正真实帧与生成帧进度、片尾排空计数，以及某些状态下 1% Low 的读数。保留采集卡格式与实际 HDR 信号诊断。
- 「去除重复帧」提供固定节奏的「60→30」、会随内容降低保留帧率的「60→30（兼容自动去重）」和「自动去除」。升级后原 2.2.0 的 60→30 对应兼容档。
- 采集卡启动失败时可按条件回退到仅画面，针对设备重枚举、重复启动和无帧重连增加限制、错误细节和恢复提示。Elgato 等采集卡支持在播放器独立启停本机监听，录音仍按采集音轨设置。
- NV12/P010 等采集色度采用插值，MJPEG 手动限幅/全幅沿用到 GPU 转换。可选 HDMI 压缩码流直通和 AC-3 回放，默认仍解码成 PCM，便于监听、处理与录音。
- 优化暂停日志、固定刷新撕裂提示、AMD FreeSync 能力识别及异常时的崩溃堆栈日志。屏幕捕获保留窗口/显示器观看用途，入口说明引导 PC 游戏用户使用 Magpie。游戏模式鼠标穿透已移除。

- 修复裁剪后尺寸为奇数时录制和即时回放无法启动的问题。编码器通过延展最后一列或一行补齐偶数尺寸，保留完整画面。

### 补帧、超分与画质

- 纹理和负向去朦胧跳过用不到的调色统计与模糊。双超分首段尺寸与输入相同且无需执行模型时，直接将首段输入交给降采样，省去一次整帧复制。
- 新增可选的准星/HUD 保护和更精细的框内 HUD 模式。宽圆点和细十字的保护按准星轮廓合成，避免中央背景出现被锁住的方块。原 1440p 录屏在 VFG 2× 中、高档各检查 159 个生成帧，方块内背景锁帧均从 75 张降到 0 张。开头仍有个别单准星残影，筛查未检出三准星。
- 新增视频降噪，默认关闭。列表模式在源尺寸先处理，节点模式可添加独立节点。亮度颗粒和彩噪分别调节，支持 SDR 和 P010 HDR。RTX 5070 实播 60 FPS 测试片的降噪 GPU P95 为 1080p 0.114 ms、4K 1.516 ms。
- ANR FG 可在补帧或组件页面下载官方运行组件并启用。AMD FRC 在符合条件的独显和分辨率使用 SUPER 档。RIFE 提供 DirectML 的 SDR 2×/3×，使用[单独配置的组件和模型](RIFE_DIRECTML.md)。
- 增加 NVIDIA Image Scaling 与 AMD VideoSR。FSR 修复 HDR/BGRA 等输入、较小光流及双重超分的运动采样。FSR 3.1 兼容和 FSR 4 ML 适用范围按 GPU 能力显示。Ada VFG 中档故障可降档恢复，RX 7000 NR 初始化放到工作线程并记录 HIP 错误。
- 移植 Magpie 提供的三项优化：FSR 光流按源尺寸和像素单位重采样、减少逐帧的百分位统计、补帧只维护历史时跳过无用的镜头检测。开发者可按需记录有限长度的逐帧诊断。

### 远程串流、字幕和界面

- PS5 串流使用独立的 4 MiB Takion UDP 接收缓冲，断流后延时重连并记录 FFmpeg 解码细节。FFmpeg 包含 PS5 256 slice 与 D3D12VA 参考帧池补丁。PSN Account ID 指南改为索尼登录相关途径。Xbox 软件解码保留真实协商尺寸。
- 实时翻译可选画面文字 OCR、Veyra 音频语音识别或 Windows 字幕。缩短部分语种定稿等待，在积压时有序更新字幕。字幕字号、颜色、阴影与行数可调。本地模型与推理所需组件按需安装。网络 MKV/WebM 内封字幕载入与地址栏粘贴得到修复。
- 播放列表支持清空历史。增加带鱼屏两侧背景选项，默认黑色。界面动画与部分统计刷新提高到约 60 Hz，修复性能光晕裁切及多处窄窗控件。极简和全屏的播放条眼睛按钮留在框内，节点调色滑条松手后显示实际保存的值。
- 修正虚拟摄像机安装确认时的锁重入崩溃。便携启动器增加现有实例唤起、采集占用恢复和失败日志。新增固定依赖的 Windows CI 构建与 32 位虚拟摄像机打包。

### 更新方式

从 2.2.0 更新时，使用对应 GPU 的完整安装包或在新目录解压对应便携 ZIP，再运行根目录的 `Veyra.exe`。新版启动器的修复需要完整包。安装版可以选择原安装位置进行升级。先保留已有设置与自定义 LUT 的备份，对比 2.2.0 时可选用单独目录。AMD/NVIDIA 分版内的实验组件以运行清单为准，ANR 和实时翻译模型走程序内下载。

## English

### Capture and playback

- Capture shows the original video while enhancement models prepare, then switches to enhanced output. The Professional source bar shows progress, and capture opens in Professional mode by default.
- Improved raw capture timing and repeated-frame handling, including the split between fixed 60-to-30 cadence, 60-to-30 with repeat detection and automatic detection. Existing 2.2.0 half-rate settings map to the compatible mode. End-of-playback counters now include the drained frames.
- Added bounded capture recovery, more precise device and launcher diagnostics, and video-only fallback for certain audio startup failures. Local capture monitoring is available independently of the recording track. Improved chroma sampling and preservation of manual MJPEG range. Optional HDMI bitstream passthrough and AC-3 replay remain off by default.
- Fixed pause-related timing traces and added fixed-refresh tearing diagnostics and AMD FreeSync detection. Screen capture is for watching display or window video. The capture notice links to Magpie for PC gaming.

- Recording and instant replay support odd-sized crops. The converter extends the last column or row to the encoder's even dimensions and preserves the full picture.

### Frame generation and upscaling

- Texture and negative dehaze skip unused colour statistics and blur passes. In dual upscaling, a same-size first stage that bypasses its model feeds the reduction stage directly, saving a full-frame copy.
- Optional HUD and crosshair protection uses the shape of wide centre dots and thin crosshairs to preserve background motion. On the supplied 1440p clip, the background lock check dropped from 75 to 0 out of 159 generated frames for each VFG 2× quality tested (Medium and High). Isolated reticle ghosts still appear at the start of the clip.
- Added optional video denoise with independent luma and chroma controls for SDR and P010 HDR. List mode runs it at the source resolution, and node mode offers a dedicated stage. It starts disabled. Measured denoise GPU P95 on the RTX 5070 was 0.114 ms at 1080p60 and 1.516 ms at 4K60.
- ANR FG can fetch its official runtime from the app. Added the AMD FRC SUPER profile for eligible GPUs. RIFE SDR 2×/3× runs through the separately installed DirectML components and models. [RIFE setup](RIFE_DIRECTML.md)
- Added NVIDIA Image Scaling and AMD VideoSR. Fixed FSR motion resampling, dual upscaling and HDR/format compatibility. Improved VFG quality recovery on Ada and startup diagnostics for experimental RX 7000 NR.
- Ported Magpie's FSR motion resizing, optional per-frame percentile calculation and reduced work on VFG history-only frames. Added opt-in frame timing traces for diagnosis.

### Remote Play, translation and interface

- PS5 Remote Play uses a 4 MiB Takion receiver buffer, a reconnect delay and better decode logs. Patched FFmpeg retains PS5 slice handling and includes the D3D12VA reference pool fix. Updated PSN Account ID guidance. Xbox software decode respects the negotiated frame size.
- Live translation accepts on-screen OCR, Veyra audio or Windows captions. Faster ordered speech captions, adjustable size, colour, shadow and line count. Models download on demand. Fixed network container subtitles and network field focus.
- Clear playlist history in one action, choose optional ultrawide side backgrounds and use the smoother UI cadence. The hide button fits in the cinema and fullscreen controls, and node colour sliders show the committed value after release. Fixed virtual-camera setup re-entrancy and improved existing-window activation and launch errors. Added a pinned Windows CI build with both virtual-camera architectures.
- Use the full installer or portable ZIP to pick up the revised launcher. Start `Veyra.exe` at the package root. In-app updates replace the versioned application directory. Hardware and optional runtimes determine which effects are available.

## 支持 / Support

[交流群、赞助和中英文教程](RELEASE_SUPPORT.md) · [构建及对应源码](BUILD_2.2.1.md)
