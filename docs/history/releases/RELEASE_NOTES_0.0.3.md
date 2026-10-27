# Veyra 0.0.3

## 中文

### 更新

- 新增 NR 运行版本选择：NVIDIA 原版默认启用，可切换到随包附带的 RTX 40/50 社区兼容实验版，无需覆盖 DLL。
- 运行版本设置随预设保存；播放中切换会重建增强管线，失败恢复上一套设置。图片处理和视频导出使用所选 NR 版本。
- 新增“直播兼容 · 实验”显示开关，默认关闭；可以在标准和实验交换链之间切换，不检测或依赖特定直播软件。
- 实时处理状态显示已应用的 NR 版本与显示模式。旧预设升级后默认使用原版 NR 和标准显示。
- 修复部分中文 MSVC/Ninja 环境无法跟踪头文件依赖的问题，避免增量构建混用不同设置结构导致启动崩溃。
- 更新中英文 README、运行组件说明和 OBS 采集教程。

### 下载与使用

普通用户只需下载 **Veyra-0.0.3-win64-portable.zip**，完整解压后运行 **Veyra.exe**。不需要另装SDK。FFmpeg-source.zip 是开源依赖的对应源码资料，普通用户不需要下载。

OBS 请使用“窗口采集”，把捕获方式明确设为 **Windows 10（1903及以上）**。“自动”可能选到 BitBlt，只能看到UI；切换上述方式已在反馈场景中恢复视频。直播兼容开关不能替代正确的捕获方式。

### 验证边界

- 两种NR运行版本在RTX5070上已运行；RTX40实机验证仍待完成，社区版不保证覆盖所有显卡或驱动。
- 社区NR为修改版，Authenticode状态为 **HashMismatch**。原版与社区版独立存放；文件版本、哈希、来源与卸载方式见包内组件文档及manifest。
- NR和DLSS补帧属于 **community experimental / 社区实验集成**，不是厂商官方认证或完整原生游戏集成。软件继续允许用户自行替换DLL，不设置固定哈希启动锁。
- 未承诺降低NR耗时、所有录屏工具兼容、长期直播稳定性或生成帧捕获节奏。专业模式整体布局调整本版尚未实施。
- 预设升级为v10；升级前可备份设置，旧软件无法读取新格式预设。

## English

### Changes

- Added NR runtime selection: keep the NVIDIA original as default or select the bundled community RTX 40/50 experimental variant without replacing DLLs.
- Saved the selection in presets and propagated it to playback, image processing, and video export. Runtime changes rebuild the enhancement graph; failed changes restore the previous settings.
- Added an optional experimental broadcast presentation mode, off by default, without detecting a specific recording application.
- Added applied NR/runtime and presentation-mode status. Older presets migrate to the original NR runtime and standard presentation.
- Fixed MSVC/Ninja header-dependency detection on localized Windows installations, preventing stale object files and startup crashes after incremental builds.
- Updated bilingual documentation, component records, and OBS capture instructions.

### Download and Capture

Download **Veyra-0.0.3-win64-portable.zip**, extract it fully, and run **Veyra.exe**. No SDK installation is required. The separate FFmpeg-source.zip contains corresponding source material for the open-source dependency; it is not needed to run Veyra.

In OBS Window Capture, explicitly select **Windows 10 (1903 and up)**. Automatic can select BitBlt and capture only the controls. The experimental swapchain switch does not fix BitBlt capture.

### Limits

Both NR variants have been exercised on an RTX 5070; RTX 40 hardware validation remains pending. The community NR DLL is modified and reports **HashMismatch**, not a valid NVIDIA signature. See the included component documentation and manifests for identities, locations, and removal instructions. Runtime replacement remains permitted.

NR and DLSS frame generation are community-experimental integrations, not vendor certification. This release does not claim improved NR throughput, universal recorder compatibility, long-session streaming validation, or verified capture cadence for generated frames. The proposed Professional-mode layout redesign is not included. Presets now use schema v10; back up settings before upgrading if an older version must remain usable.
