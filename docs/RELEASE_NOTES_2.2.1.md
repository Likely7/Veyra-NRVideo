# Veyra 2.2.1

**感谢大理喜鹊（[SAOG0721/Magpie](https://github.com/SAOG0721/Magpie)）为 2.2.1 提供部分性能优化与分析。** 本版采纳了 FSR 光流重采样、减少逐帧百分位统计开销、VFG 历史维护路径和逐帧诊断方面的贡献。

**Thanks to 大理喜鹊 of [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie) for contributing performance analysis and optimizations to 2.2.1.** This release incorporates work on FSR motion resampling, frame-time statistics, VFG history handling, and frame-by-frame diagnostics.

## 简体中文

### 采集、音频与录制

- **冷启动先播原画。** 采集卡和屏幕捕获先显示原画，增强模型在后台准备完成后再接管，加载中暂停或更改设置也有对应处理。专业模式片源栏显示进度，采集完成后进入专业模式。
- 改善 NV12、YUY2 等原始采集的节奏、相近时间戳和断续帧处理。修正补帧容量与相位等待、意外低帧及暂停期间的卡顿误报。片尾统计计入已排空的原帧和生成帧。
- **去重提供三档。** 「60→30」保持固定取样节奏，30 FPS 内容对齐每组第一份画面。「60→30（兼容自动去重）」可在内容低于 30 FPS 时跳过重复画面。「自动去除」按内容检测。升级时，2.2.0 保存的 60→30 继续对应兼容档。
- 采集卡启动失败时可按条件回退至仅画面，限制无帧重开及设备重新枚举。记录失败的 DirectShow 时钟调用和 Windows 错误码，细化资源占用、设备到达/移除与恢复提示。启动器可唤起现有窗口，确认旧进程退出后再恢复采集。
- Elgato 等采集卡支持独立启停本机监听，录音仍读取采集音轨。默认解码 PCM；可选 HDMI 压缩码流直通和 AC-3 回放。NV12/P010 等补齐色度插值，MJPEG 手动限幅/全幅保留到 GPU 转换。界面显示实际采集格式与 HDR 信号。
- **奇数裁剪可以录制和保存即时回放。** NV12/P010 编码尺寸向上补成偶数，转换时复制边缘最后一列或一行，保留原画。屏幕捕获继续提供窗口/显示器的视频观看入口，移除游戏鼠标穿透，并在入口提供 Magpie 链接。
- 改善 AMD FreeSync 识别、固定刷新送显和持续撕裂诊断。全屏可显示合成到送显画面的帧率统计，保留经典全屏输出选项。

### 补帧、超分与画质

- **准星与 HUD 保护。** 支持自动中心准星、用户框选和动态 HUD，剔除区域可按效果范围设置。稳定识别后按圆点、细十字或框形局部合成，避免冻结一整块背景。
- **视频降噪。** 分别调节亮度颗粒和彩噪，支持 SDR 与 P010 HDR，默认关闭。列表模式在源尺寸处理，节点模式可放入独立降噪节点。算法来源见[第三方许可](../THIRD_PARTY_NOTICES.md)。
- **超分增加 NVIDIA Image Scaling 和 AMD VideoSR。** AMD VideoSR 使用已验证的 D3D12 路径，之前选择的实验档位会迁移到兼容档。FSR 处理更多 HDR/BGRA 格式，光流按正确尺寸和像素单位重采样，双超分的两段按各自尺寸传递运动信息。
- **减少冗余 GPU 工作。** 纹理和负向去朦胧跳过低分辨率统计及两次模糊。双超分首段同尺寸旁路时直接送入降采样，省去一张 FP16 中间纹理和整帧复制。逐帧统计跳过无需计算的百分位，VFG 仅维护历史帧时跳过该帧对的镜头检测。
- 补帧或组件页可以按需下载安装 ANR FG 官方组件。RIFE 提供 SDR 2×/3× DirectML 路径，使用[独立组件和模型](RIFE_DIRECTML.md)。AMD FRC 在适用独显与分辨率启用 SUPER 档。FSR 3.1 和 FSR 4 ML 的可用性按设备能力显示。
- Ada VFG 中档发生初始化或评估故障时可使用兼容质量恢复，后端切换和重开保留有效配置。RX 7000 NR 后台初始化并补齐 HIP 诊断。修正关闭补帧时的预设匹配，新增效果和去重档位按需保存到可兼容旧版的独立预设文件。

### 串流、字幕与界面

- **PS5 串流。** Takion 使用独立 4 MiB UDP 接收缓冲。断流后延时重连，FFmpeg 解码错误进入应用日志；保留 H.264 256 slice 补丁并加入 D3D12VA 参考帧池修复。Account ID 指南改用索尼登录途径。Xbox 软件解码保留真实协商尺寸。
- **实时字幕。** 可从 Veyra 音频做本地语音识别，也可读画面文字 OCR 或 Windows 字幕。同一段快语速测试中缩短定稿等待，语音断点和积压时保留句序。字幕字号、颜色、阴影和行数可调，本地模型及推理组件按需下载。
- 修复 HTTP MKV/WebM 内封字幕加载、隔行场频设置和网络地址窗口的 Ctrl+V 聚焦。播放列表可一键清空历史，带鱼屏两侧支持独立背景，默认黑色。
- 界面动画和部分统计提升到约 60 Hz。修复全屏与极简播放条的眼睛按钮越界、性能光晕裁切、NR 设置窄面板截断，以及节点调色滑条松手回弹。
- 修复虚拟摄像机组件安装时，Windows 授权弹窗引发的锁重入闪退。崩溃日志记录返回地址及所属模块。固定 Windows CI 依赖，打包双架构虚拟摄像机，统一开发检查至 Qt 主程序，清理已退役的 Win32 UI 和一次性脚本。

### 实测数据

| 项目 | 结果 |
| --- | --- |
| 采集先出原画 | RTX 5070 + T900 4K60 MJPEG 首帧 0.940–0.965 秒，增强接管处的提交间隔 17.6–18.1 ms。 |
| 奇数裁剪录制与回放 | WGC 测试窗口的 9 个 SDR/HDR MP4 共解码 648 帧。补齐的边缘亮度差抽样 P95 为 0。 |
| 调色与双超分 | 优化前后 16 个 SDR/P010 FP16 样本逐字节一致。双超分 31 组通过，首段旁路的线性像素最大误差 0.000488。 |
| 视频降噪 | RTX 5070、60 FPS 测试片的降噪 GPU P95 为 1080p 0.114 ms、4K 1.516 ms。 |
| 准星合成 | 1440p 游戏录屏的 VFG 2× 中、高档各检查 159 个生成帧，背景锁帧各从 75 降到 0。片头仍有个别准星残影。 |
| 实时字幕 | 快语速混音中，Parakeet 英文 P50 2.510 → 1.808 秒，Whisper 英文 2.487 → 1.997 秒，Whisper 中文 2.538 → 2.088 秒。 |
| 整包回归 | 25 组通用检查通过，含 257 项 Qt Quick 检查。NVIDIA、AMD 各核验 108 个着色器。 |

数据对应各项所列设备和测试条件。采集时间采用应用侧首帧与提交间隔。

## English

### Capture, audio and recording

- **Show the original picture while enhancement loads.** Capture-card and screen inputs begin with a raw preview and hand over when the enhanced pipeline is ready. The Professional source bar shows progress, and capture opens in Professional mode by default.
- Improved raw capture pacing, nearby or discontinuous timestamps, generation deadlines, and end-of-playback counts. The dedup selector offers fixed 60-to-30 cadence, 60-to-30 with repeat detection, and automatic detection. The fixed option aligns to the first copy of 30 FPS content. Existing 2.2.0 half-rate settings retain the compatible policy.
- Added video-only fallback for eligible capture startup failures, bounded no-frame recovery, device arrival/removal logs and precise DirectShow errors. The launcher can raise an existing instance and verifies that the old process has exited before capture recovery.
- Local capture monitoring is independent of recorded audio. Optional HDMI bitstream passthrough and AC-3 replay supplement the default PCM route. Improved NV12/P010 chroma interpolation, manual MJPEG range and capture HDR diagnostics.
- **Recording and instant replay support odd-sized crops.** The NV12/P010 conversion extends the final column or row to an even encoding size. Screen capture remains available for viewing windows and displays. The PC gaming notice links to Magpie, and the legacy mouse passthrough is removed.
- Improved AMD FreeSync detection, fixed-refresh presentation and sustained tearing diagnostics. Added fullscreen frame statistics to the presented picture.

### Frame generation, upscaling and picture quality

- Optional centre reticle, framed HUD and dynamic HUD protection use the detected shape for compositing. Effects can be excluded selectively inside marked regions.
- Added optional video denoise with separate luma and chroma controls for SDR and P010 HDR, at source resolution in List mode and as a dedicated Node stage.
- Added NVIDIA Image Scaling and AMD VideoSR. AMD VideoSR uses the validated D3D12 route and migrates the previous experimental option. FSR supports additional HDR/BGRA input formats and scales motion to the correct size and pixel units for each upscaling pass.
- Texture and negative dehaze skip unused block statistics and two blur passes. Same-size first-stage bypass in dual upscaling feeds reduction directly and saves a full-frame FP16 copy. Frame statistics and VFG history-only paths also avoid redundant work.
- ANR FG can fetch its official runtime in the app. RIFE provides SDR 2×/3× through separately installed DirectML components and models. AMD FRC uses SUPER on eligible devices and resolutions. FSR 3.1 and FSR 4 ML availability follows hardware capability.
- Improved Ada VFG quality recovery and RX 7000 NR background startup and HIP diagnostics. Fixed preset matching with frame generation disabled and retained older preset files when new effect formats require a sidecar.

### Remote Play, captions and interface

- PS5 uses a separate 4 MiB Takion UDP receive buffer, delayed reconnect and improved FFmpeg decode logs. The build retains PS5 H.264 slice handling and includes the D3D12VA reference-frame pool fix. Updated the Account ID guide to use Sony sign-in. Xbox software decode follows the negotiated size.
- Live translation can use local speech recognition from Veyra audio, on-screen OCR or Windows captions. Faster segmentation and ordered subtitle delivery handle rapid speech and brief breaks. Text size, colour, shadow and line count are adjustable. Models and inference components download on demand.
- Fixed network MKV/WebM subtitles, field-rate handling and paste focus. Added a clear-history action and independent ultrawide side backgrounds.
- UI animations and some statistics update at approximately 60 Hz. Fixed clipped performance glows, compact NR layout, the eye button in Cinema and fullscreen, and reset-after-drag behaviour in Node colour sliders.
- Fixed virtual-camera setup re-entrancy during Windows elevation. Added module-relative crash details, pinned Windows CI dependencies, x86/x64 camera packaging and developer workflow cleanup.

### Validation

- On an RTX 5070 and T900 4K60 MJPEG, raw capture started in 0.940–0.965 s and the handover interval was 17.6–18.1 ms in application measurements.
- Nine SDR/HDR cropped recordings and replays decoded 648 frames. Sixteen SDR/P010 colour samples remained byte-identical, and 31 dual upscaling cases passed.
- VFG 2× Medium and High each produced 159 generated frames for the 1440p reticle check. Background holds fell from 75 to zero per quality, with isolated reticle ghosts at the start.
- Denoise GPU P95 was 0.114 ms at 1080p60 and 1.516 ms at 4K60 on the RTX 5070 test clips. Median English caption latency in the fast-speech mix fell from 2.510 to 1.808 s for Parakeet and from 2.487 to 1.997 s for Whisper.
- 25 regression groups passed, including 257 Qt Quick checks. Both GPU packages were checked against 108 compiled shaders.

## 社区贡献 / Community contributions

感谢提交问题、提供日志与实测的用户，以及这次收录的 17 项社区 PR。Thanks to the users who reported, logged and tested these issues, and the contributors to the 17 PRs included in this release.

- 采集、串流和显卡兼容 / Capture, streaming and GPU compatibility · [#213](https://github.com/Likely7/Veyra-NRVideo/pull/213), [#219](https://github.com/Likely7/Veyra-NRVideo/pull/219), [#233](https://github.com/Likely7/Veyra-NRVideo/pull/233), [#244](https://github.com/Likely7/Veyra-NRVideo/pull/244), [#246](https://github.com/Likely7/Veyra-NRVideo/pull/246), [#251](https://github.com/Likely7/Veyra-NRVideo/pull/251), [#253](https://github.com/Likely7/Veyra-NRVideo/pull/253), [#254](https://github.com/Likely7/Veyra-NRVideo/pull/254)
- 界面、字幕与诊断 / UI, subtitles and diagnostics · [#206](https://github.com/Likely7/Veyra-NRVideo/pull/206), [#215](https://github.com/Likely7/Veyra-NRVideo/pull/215), [#228](https://github.com/Likely7/Veyra-NRVideo/pull/228), [#242](https://github.com/Likely7/Veyra-NRVideo/pull/242)
- 构建和项目维护 / Build and maintenance · [#176](https://github.com/Likely7/Veyra-NRVideo/pull/176), [#198](https://github.com/Likely7/Veyra-NRVideo/pull/198), [#199](https://github.com/Likely7/Veyra-NRVideo/pull/199), [#201](https://github.com/Likely7/Veyra-NRVideo/pull/201), [#202](https://github.com/Likely7/Veyra-NRVideo/pull/202)

[2.2.0 到 2.2.1 的完整提交 / Full commit comparison](https://github.com/Likely7/Veyra-NRVideo/compare/v2.2.0...v2.2.1)

## 支持 / Support

[交流群、赞助和中英文教程](RELEASE_SUPPORT.md) · [构建及对应源码](BUILD_2.2.1.md)
