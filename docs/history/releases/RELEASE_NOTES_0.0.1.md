# Veyra 0.0.1

首个公开源码与免安装实验版。

## 本版本包含

- 日常观看模式与可展开的专业工作台。
- 视频、图片和采集设备入口。
- 播放、音量、字幕、全屏、最近文件和专业模式对比工具。
- 可选的增强、超分、补帧、图片导出和视频导出工作流。
- 运行目录自定位：便携版从 `Veyra.exe` 所在目录读取 shader、设置、日志与运行时，不再依赖开发机绝对路径。
- 用户授权的 Runtime Pack：固定版本的 DLSS SR、DLSSG、DLSSNR、RTX Video VSR 与 FRUC 运行时，随包提供哈希、签名状态和许可证文件。

## 使用

解压后运行 `Veyra.exe`。Release 构建会校验 `runtime_local/nvidia/release-runtime-manifest.json` 中列出的运行时哈希和签名；NR 与 FRUC 在启动时还会校验固定身份。兼容的 RTX 环境可直接启用相应功能。校验、驱动、硬件或初始化失败时，只关闭受影响的增强，播放器、采集与导出仍可启动。

NR、DLSSG 和 FRUC 是社区实验运行时，不代表 NVIDIA 官方合作、认证或支持。请不要替换包内 DLL，也不要从游戏目录、驱动缓存或未知来源复制文件。

## 已知限制

- 这是实验版本，未宣称全部 GPU、驱动、采集卡或高倍率补帧组合已完成长期验证。
- HDR、AV1 / ProRes 导出和 VFR 原样输出不在本版本范围。
- 3X / 4X 补帧为实验功能；所选倍率不保证每对输入帧都能生成有效画面。
- 没有测量物理屏幕扫描延迟；采集卡、音频和长时间稳定性仍需真实设备验证。
- Runtime Pack 是独立的 Release 载荷；若平台或权利方要求移除，Veyra 基础包仍可独立发布和运行。

## English summary

This is Veyra's first public source snapshot and portable experimental build. Extract the archive and run `Veyra.exe`. The user-authorized Runtime Pack contains version-pinned DLSS SR, DLSSG, DLSSNR, RTX Video VSR, and FRUC components with a manifest, hashes, signature states, and license files. The release builder validates the listed runtime files, while NR and FRUC validate their pinned identities at startup. Veyra keeps player, capture, and export available when a component cannot run.

NR, DLSSG, and FRUC are community-experimental runtimes. They do not imply NVIDIA endorsement, certification, partnership, or support. This release does not claim universal GPU, capture-device, high-multiplier frame-generation, HDR, AV1/ProRes, VFR, physical-latency, or long-duration validation.
