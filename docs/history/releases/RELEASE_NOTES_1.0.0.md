# Veyra 1.0.0

## 中文

### 下载与使用

下载 **Veyra-1.0.0-win64-portable.zip**，完整解压，双击 **Veyra.exe**。无需安装 SDK 或开发工具。其余 Source 源码包不需要下载来运行软件。

升级请退出旧版并解压到新目录。增强预设可从旧目录复制 `runtime_local/*.v1` 和 `veyra.ini`；PS5 配对保存在当前 Windows 用户的加密数据目录，无需重新搬运。

### 本次更新

- 更换透明 Logo，补齐 EXE、任务栏和窗口图标。
- 专业模式顶部新增“截图”：一键保存当前增强后的完整画面为 PNG，位置为系统“图片”文件夹下的 `Veyra Screenshots`。不包含工具栏、对比视图或窗口缩放裁切；原生 HDR 截图暂不支持。
- 整合 PS5 串流、采集卡和视频播放修复。修复部分 PS5 H.264 硬解只显示细条、硬解帧资源复用问题；改善色度采样和放大显示，保留兼容采样选项。
- 改进 PS5 串流停帧检测与有限次数重连，修复音频时钟漂移导致声音补偿持续增加的问题。
- 修复部分场景下 DLSS 补帧未有效输出，以及过载时 GPU 耗时指标失效的问题。
- 统一状态面板口径：主曲线显示总增强处理耗时，额外画面延迟估计移入精细诊断。两者都不是按键到屏幕实测延迟。
- 修复视频接近结尾、处理跟不上时的帧引用失效崩溃；尺寸突变的输入明确停止，避免越界。
- 损坏视频不再导出不完整结果后报告成功。导出收尾会检查完整性，可取消，可能需要额外时间。
- 新增可切换的 **GPU DIS · FAST 实验光流**，默认仍为 NVIDIA NVOF。它不保证更快，本机 RTX 5070 的 1080p 合成测试明显慢于 NVOF，保留供效果对比。

### 使用提醒与边界

- PS5 与电脑接同一局域网，首次使用 Account ID 和八位码配对；后续选择已保存主机。手柄连接电脑，USB 优先。输入最高 1080p，2K/4K/8K 是本地超分目标；请求码率不等于主机实际发送码率。
- OBS 窗口采集请手动选择 **Windows 10（1903及以上）**，不要依赖自动 / BitBlt。
- NR、DLSS FG、XeSS、GPU DIS、PSN 登录及 HDR 中标明的实验功能仍保留实验属性。1.0.0 是应用版本号，不代表厂商认证或所有设备/游戏组合均已验证。
- 视频导出暂不保留内嵌字幕；不要让多个软件实例同时导出到同一个目标文件。多显卡、蓝牙设备、真实 HDR 屏幕及长时间串流仍需持续验证。
- 运行组件允许用户替换 DLL。社区 NR 是修改版，签名状态为 `HashMismatch`；不是有效签名原版。

沿用七个既有增强运行文件，源码与运行组件分离。包内 manifest 列出来源、版本、大小、SHA256、签名和许可证；它是发布记录，不是 DLL 替换锁。FFmpeg 与串流依赖的对应源码、补丁和构建说明作为附加源码资产提供，普通用户无需下载。

## English

Download **Veyra-1.0.0-win64-portable.zip**, extract it completely and run **Veyra.exe**. No SDK or development tools are required. Source archives are optional for ordinary users.

### Changes

- New transparent logo and executable, taskbar and window icons.
- Added **Screenshot** to the Professional toolbar. Saves the latest processed full-resolution image as PNG under **Pictures / Veyra Screenshots**, without application UI, comparison overlays or window zoom/cropping. Native HDR screenshots are not supported yet.
- Fixed PS5 H.264 hardware decoding that displayed only a thin strip, and corrected reuse of hardware-frame resources. Improved chroma reconstruction and scaling, with a compatibility option retained.
- Improved stream-stall detection and bounded reconnection. Fixed audio-clock drift that could cause compensation to grow during play.
- Fixed ineffective DLSS frame generation in some scenarios and missing GPU timings under overload.
- The main chart now shows total enhancement GPU processing time; estimated extra picture delay is listed separately in detailed diagnostics. Neither is measured button-to-screen latency.
- Fixed an end-of-file crash under processing overload. Midstream size changes now stop explicitly rather than writing outside allocated resources.
- Corrupted input no longer produces a false successful export. Export completion validates integrity and can take extra time; cancellation is supported.
- Added experimental **GPU DIS · FAST** optical flow. NVIDIA NVOF remains the default. This implementation was substantially slower than NVOF on our RTX 5070 in a 1080p synthetic test; it is available for comparisons, not advertised as a performance upgrade.

### Notes

Close the previous version before extracting to a new directory. Enhancement presets may be copied from `runtime_local/*.v1` and `veyra.ini`; encrypted PS5 pairing remains in the Windows user profile.

PS5 streaming uses the local network and supports input up to 1080p; higher resolutions are local upscaling targets. Connect the controller to the PC, preferably via USB. Requested bitrate is not guaranteed received bitrate. For OBS Window Capture, explicitly select **Windows 10 (1903 and up)**.

Experimental enhancement, PSN and HDR features remain experimental in application version 1.0.0. This release is not vendor certification. Embedded subtitles are not preserved in video exports; avoid concurrent exports from multiple instances to the same target. Multi-GPU, Bluetooth, physical HDR displays and long sessions are not comprehensively validated.

The seven existing enhancement runtime files are retained. DLL replacement remains allowed; the community NR file has `HashMismatch` signature status. The package includes component provenance, hashes and applicable licenses. Matching FFmpeg and streaming dependency source, patches and build instructions are supplied separately; no proprietary SDK/runtime is added to source Git.
