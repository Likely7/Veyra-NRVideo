# Veyra 0.0.2

## 下载与使用

下载 **Veyra-0.0.2-win64-portable.zip**，完整解压，运行 **Veyra.exe**。无需另装SDK或开发工具。支持Windows 11 x64；NVIDIA增强需兼容RTX显卡及驱动，主要验证设备为RTX 5070。

升级请使用新目录。可迁移旧版 `runtime_local/*.v1` 和 `veyra.ini` 设置文件，不要覆盖新版DLL目录。源码压缩包不是可运行软件。

`Veyra-0.0.2-FFmpeg-source.zip` 是开源依赖的源码与构建资料，普通用户无需下载。`.sha256` 文件用于自愿核对下载完整性，不限制软件加载替换后的DLL。

## 本次更新

- 新增 XeSS 2X 预览补帧选择，补齐随包 XeSS / XeLL 运行组件；DLSS仍提供2X/3X/4X。
- 新增2K、8K超分目标，保留4K和画面比例；增加AMD FidelityFX光流选项，不包含AMD NR。
- 增加60fps采集按30fps内容处理的选项，在增强前减少重复处理。
- 改进真实处理产出、有效补帧、呈现提交统计，集中显示软件总延迟和各链路耗时。
- 采集采用单GPU线程调度、有界队列和过期补帧筛选；输入暂时断流时仍推进已有输出。
- 修正实卡关闭补帧时不必要的源时钟等待，避免首次回调迟到造成额外显示等待。
- 改进采集音频漂移补偿、设备恢复和切换淡入淡出；修复文件音频端点断开、暂停与seek恢复。
- 修复专业面板可见性、紧凑窗口布局、滚动和部分DPI/绘制问题。
- 移除FRUC后端与组件；旧预设迁移到当前DLSS/XeSS格式。
- EXE新增0.0.2版本信息；运行组件按目录独立放置。允许用户自行替换DLL，不再使用固定哈希/签名锁；清单记录本次发布原件。

## 边界

NR与DLSS补帧为 **community experimental / 社区实验集成**，不代表NVIDIA官方合作、认证或支持。替换DLL不保证兼容；程序仍检查接口和初始化结果。XeSS仅用于预览，AMD NR、HDR和AV1/ProRes导出未提供。原生高分辨率NR和高倍率补帧仍可能过载，软件计时不等于物理显示延迟。

本地构建和定向软件回归不代表所有显卡、驱动、采集卡及长期稳定性已验收。尚未完成与其他播放器的同源物理延迟对照。

使用教程：[中文](https://github.com/Likely7/Veyra-NRVideo/blob/main/README.md) / [English](https://github.com/Likely7/Veyra-NRVideo/blob/main/README_EN.md)。组件、版本、哈希及卸载方式见包内 `docs/RUNTIME_COMPONENTS_0.0.2.md` 和组件清单。退出后删除解压目录即可卸载。

## English

Download **Veyra-0.0.2-win64-portable.zip**, extract everything, and run **Veyra.exe**. No SDK or development tools are needed. Requires Windows 11 x64; NVIDIA enhancement needs compatible RTX hardware and drivers. The main validation GPU is an RTX 5070. Extract upgrades into a new folder and migrate only your settings, not old runtime directories.

### Changes

- XeSS 2X preview frame generation with bundled XeSS / XeLL runtimes; DLSS retains 2X / 3X / 4X.
- 1440p and 8K upscaling targets alongside 4K, with aspect ratio preserved; AMD FidelityFX optical flow is available, AMD NR is not.
- 60-to-30 capture sampling before enhancement for 30fps content transported at 60fps.
- Completed-output and presentation counters, software latency, and per-stage timing in the status panel.
- One GPU scheduling owner, bounded queues, obsolete-FG admission checks, and continued completion during input gaps.
- Removed unnecessary source-clock waiting for physical capture with FG disabled.
- Capture audio drift correction, endpoint recovery, transition fades, and improved file-audio pause/seek recovery.
- Professional-panel visibility, compact layout, scrolling, and selected DPI/painting fixes.
- Removed FRUC and migrated legacy presets to the current DLSS/XeSS format.
- Versioned 0.0.2 executable and separate runtime directories. User DLL replacement is allowed without hash/signature locks; manifests document the shipped components.

NR and DLSS FG are **community-experimental integrations**, not NVIDIA endorsement, certification, or support. Replacement DLLs may be incompatible. XeSS is preview-only; AMD NR, HDR, and AV1/ProRes export are unavailable. High-resolution/high-multiplier combinations may overload the GPU. Software timing is not physical display latency. Software regression does not establish universal hardware compatibility or long-duration stability.
