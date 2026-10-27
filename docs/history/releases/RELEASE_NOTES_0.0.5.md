# Veyra 0.0.5

## 中文

### 新增与改进

- PS5 局域网串流：主机搜索、手动 IP、配对保存、唤醒和软硬解选择；串流复用现有增强、播放与音频链路。
- PC 端手柄支持触摸板、陀螺仪、震动和自适应扳机（需兼容设备、连接方式与游戏）；修复全屏后的输入失效。
- 固定用户目录加密保留主机；新增实验 PSN 浏览器登录取 Account ID。首次仍需八位配对码。
- 实验 H.265 HDR / Main10：关闭增强且 Windows HDR 开启时原生输出，否则映射到 SDR 后处理。
- 默认关闭的低延迟模式：预览可切换 NR→超分→补帧，可能降低耗时，也可能增加拖影或边缘瑕疵。
- 重做实时状态面板：阶段 GPU 耗时、增强额外延迟估计曲线、待输出帧数、正常 / 过载 / 错误状态；卡片内可展开精细诊断。
- 调整专业设置顺序、独立音频页，补齐深色悬停说明并修复提示无法弹出。

### 播放修复

- 修复部分 4K30 NR＋2X 组合中，生成帧反复过期、表现为回到 30fps 的调度问题。
- 持续欠速的实时预览跳过过期补帧 / 源帧增强机会，保持媒体时间与音频连续；导出仍完整处理。
- 修复进度条跳转回弹、seek 后输出槽被占用错误，改善过载恢复和设置切换。
- 改进串流颜色信令、输入帧率统计、停帧诊断及恢复；不保证所有设备上的偶发停帧已消失。

### 使用

下载 **Veyra-0.0.5-win64-portable.zip**，完整解压并运行 **Veyra.exe**，无需安装 SDK。不要下载源码包来运行软件。

升级先退出旧版并解压到新目录；增强设置可复制旧版 runtime_local/*.v1 和 veyra.ini。PS5 主机存于 %LOCALAPPDATA%/Veyra/remoteplay，绑定当前 Windows 用户，不要分享该目录。

PS5：同一局域网 → 启用远程游玩 → 搜索或填 IP → Account ID 与首次八位码配对 → 手柄连接电脑。流输入最高1080p，2K/4K/8K属于本地超分。

OBS 窗口采集手动选择 **Windows 10（1903及以上）**，不要依赖自动 / BitBlt。

### 边界与组件

基本 PS5 连接与 USB 手柄已有用户测试；PSN 实际网页授权、真实 PS5 HDR、蓝牙 / 多设备与长期稳定性未完整验收。首次免码注册、外网串流和原生 HDR NR 未实现。HDR 增强当前为 SDR 映射路线。

沿用原版与社区 NR、DLSS、RTX Video、XeSS / XeLL 运行组件，允许用户替换 DLL。社区 NR 为修改文件，签名状态 HashMismatch，不能称为有效 NVIDIA 签名原件。NR / DLSS FG 是 community experimental，不是厂商认证。

应用对应源码位于 v0.0.5 标签。RemotePlay-source、FFmpeg-source 是附加对应源码包，普通用户无需下载。串流组合程序适用 AGPLv3 及上游 OpenSSL 例外；SDK、DLL、模型不进入源码 Git。来源、哈希、签名、许可证与卸载信息见包内组件清单和 manifest。

## English

### Changes

- Added PS5 LAN streaming, discovery/manual IP, persistent pairing, wakeup and software/hardware decoding through the shared enhancement and audio pipeline.
- Added compatible PC gamepad touchpad, gyro, vibration and adaptive triggers; fixed input stopping in fullscreen.
- Added encrypted per-user console storage and experimental browser PSN authorization for Account ID retrieval. Initial pairing still requires the eight-digit code.
- Added experimental H.265 HDR / Main10: native output with Windows HDR and all enhancements off; otherwise tone-map to SDR before enhancement.
- Added optional NR → Upscale → Frame generation preview order, off by default. It may reduce processing cost but increase artifacts.
- Redesigned diagnostics with stage GPU timings, estimated additional enhancement delay, queued output frames and status indicators.
- Reordered Professional controls, separated audio settings and repaired dark hover help.
- Fixed scheduling that discarded generated frames in some 4K30 NR + 2X combinations. Overloaded preview skips expired video opportunities while media time advances and audio remains continuous; export keeps complete processing.
- Fixed seek-bar rebound and occupied output slots after seeks; improved recovery, streaming color metadata, FPS reporting and stall diagnostics.

### Download and Limits

Download **Veyra-0.0.5-win64-portable.zip**, extract fully, run **Veyra.exe**. No SDK installation is required. Source archives are for developers.

Keep PS5 and PC on the same LAN, enable Remote Play, pair once using Account ID and the eight-digit code, and connect the controller to the PC. Stream input is up to 1080p; higher resolutions are local upscaling targets.

Close the old version before upgrading to a new directory. Copy runtime_local/*.v1 and veyra.ini for enhancement settings. Console profiles remain encrypted under %LOCALAPPDATA%/Veyra/remoteplay and are bound to the Windows user. Do not share them.

In OBS select **Windows 10 (1903 and up)** explicitly for Window Capture.

Basic console/USB controller use passed user testing. Sony authorization, physical PS5 HDR, Bluetooth/multiple controllers and long sessions are not fully validated. Pinless first registration, Internet streaming and native HDR NR are not implemented.

Original/community NR and the existing seven enhancement DLLs are retained; replacements remain allowed. Community NR reports HashMismatch. NR and DLSS FG are community experimental, not vendor certification.

Application source matches tag v0.0.5; RemotePlay-source and FFmpeg-source assets provide dependency source. The combined streaming executable also falls under AGPLv3 with the upstream OpenSSL exception. SDKs, runtimes and models are excluded from source Git. See included component manifests and licenses.
