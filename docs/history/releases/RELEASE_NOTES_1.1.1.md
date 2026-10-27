# Veyra 1.1.1

## 中文

- 新增 **RTX 30 兼容 · 实验** NR 运行版本，独立于 NVIDIA 原版和 RTX 40/50 社区版，无需手动覆盖 DLL。
- **首次启动默认关闭所有增强**：NR、超分、内部补帧全部关闭，避免第一次打开画面就加载高负载效果。已保存的设置继续保留。
- 改善实验运行版本切换的隔离与释放，兼容查询仅限所选 NR 组件；不修改系统驱动或磁盘 DLL。

使用方法：专业模式 → 增强 → NR 运行版本，选择与显卡对应的版本，再手动开启 NR。RTX30 先试实时档并关闭 SR/FG，确认能正常运行后再调整。该选项不解锁 DLSS 帧生成。

**验证范围**：RTX5070 上通过真实 NR 执行、三种运行版本同进程切换及播放/截图/导出回归。RTX30 实卡性能、画质与稳定性仍待用户验收；不要把实验选项理解为全系列保证支持。两个社区 NR 文件均为修改版，签名状态 HashMismatch。允许用户替换 DLL，不强制哈希锁。

下载 **Veyra-1.1.1-win64-portable.zip**，完整解压后运行 Veyra.exe。升级请解压到新目录；复制旧设置会恢复原来开启的效果。Smooth Motion 仍由 NVIDIA App 管理，软件默认全关不改变驱动设置。

## English

- Added a separate **RTX 30 compatibility · Experimental** NR runtime option, alongside the original and RTX 40/50 community variants. No manual DLL overwrite is needed.
- **All enhancements are off on a fresh installation**: NR, super resolution and internal frame generation. Existing saved preferences remain intact.
- Scoped runtime compatibility and teardown to the selected NR component without modifying driver entry points or DLL files on disk.

Select the NR runtime in Professional mode, then explicitly enable NR. Start RTX30 testing with realtime NR and SR/FG disabled. This option does not unlock DLSS frame generation.

Actual NR execution, same-process runtime switching, playback, screenshots and export were tested on RTX5070. RTX30 performance, quality and stability still require hardware validation. Both community NR binaries have Authenticode status HashMismatch. User DLL replacement remains allowed.

Download **Veyra-1.1.1-win64-portable.zip**, extract fully and run Veyra.exe. Restoring old preferences restores their enabled effects. NVIDIA App continues to control Smooth Motion independently.

---

The RemotePlay-source and FFmpeg-source ZIPs provide corresponding dependency source and are not needed to run Veyra. Proprietary SDKs, credentials and development files are excluded. The portable includes the existing PS5 H.264 hardware-decoder slice fix; all experimental features retain their stated limits.
