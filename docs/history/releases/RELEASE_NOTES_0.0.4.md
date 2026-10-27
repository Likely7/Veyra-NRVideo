# Veyra 0.0.4

## 中文

### 更新

- 修复视频导入播放时，声音先于首张增强画面开始的问题。打开文件、跳转、暂停恢复和调整增强设置后，音视频会重新对齐。
- 修复 NR 处理耗时轻微波动时声音反复停顿的问题，为短暂抖动保留容差；持续过载时仍限制声音领先，避免越播越不同步。
- 修正 XeSS 补帧和原画对比下的音频时序，避免将完整源帧时长错误缩短为补帧间隔。
- 修复采集卡在较长软件处理延迟下的同步补偿上限，并在暂停及重建增强管线时清除过期同步状态。只补偿软件新增的视频等待，不重复计算采集卡音视频共同的输入延迟。
- 增加音频连续性、跳转恢复、处理波动和过载恢复回归检查；更新中英文使用说明。

### 下载与升级

下载 **Veyra-0.0.4-win64-portable.zip**，完整解压后运行 **Veyra.exe**。运行组件已包含，无需安装 SDK。升级请退出旧版并解压到新目录；保留设置时只复制旧版 `runtime_local/*.v1` 和 `veyra.ini`。

**FFmpeg-source.zip** 是开源依赖的对应源码，普通用户不需要下载。OBS 窗口采集仍应手动选择 **Windows 10（1903及以上）**，避免“自动”选到只能捕获 UI 的 BitBlt。

### 验证与边界

RTX 5070 上的 1080p30、原生 4K30 NR＋XeSS 短测中，观测到的额外音频暂停均为 0；30/35ms 处理波动的合成采集音频测试未出现额外重置或欠载。文件跳转、暂停恢复及故意过载后的恢复回归通过。上述是软件短测，不代表长时间直播或屏幕／扬声器物理同步的测量。

本版不降低 NR 本身的运算量。GPU 持续处理不过来时，声音仍可能等待视频，请降低画质、倍率或目标分辨率。

继续提供 NVIDIA 原版 NR 与 RTX 40/50 社区实验版切换，并允许自行替换 DLL。社区版为修改文件，签名状态 **HashMismatch**；RTX 40 实机验证仍待完成。NR 与 DLSS 补帧属于 **community experimental / 社区实验集成**，不是 NVIDIA 官方认证。SDK、运行时和模型不进入源码 Git；组件来源、哈希、许可证及移除方法见包内 `docs/RUNTIME_COMPONENTS_0.0.4.md` 与清单。

## English

### Changes

- Fixed file audio starting before the first enhanced video frame. Playback now re-establishes synchronization after opening, seeking, resuming, and rebuilding enhancement settings.
- Fixed repeated audio interruptions caused by small NR processing-time fluctuations. Brief jitter has a tolerance window; sustained overload still limits audio lead to prevent accumulating drift.
- Corrected audio coverage with XeSS frame generation and original-image comparison, preserving the full source-frame interval.
- Raised the capture synchronization compensation limit for longer software processing delays and cleared stale synchronization state on pause or graph rebuild. The capture card's shared audio/video input delay is not compensated twice.
- Added regression coverage for audio continuity, seeks, timing fluctuations, and overload recovery; updated both READMEs.

### Download and Upgrade

Download **Veyra-0.0.4-win64-portable.zip**, extract it fully, and run **Veyra.exe**. Application runtimes are included; no SDK installation is needed. Close the old version and extract into a new directory. To retain settings, copy only the old `runtime_local/*.v1` files and `veyra.ini`.

**FFmpeg-source.zip** contains corresponding source for an open-source dependency and is not required to run Veyra. In OBS Window Capture, explicitly choose **Windows 10 (1903 and up)**; Automatic may select BitBlt and capture only the controls.

### Validation and Limits

Short RTX 5070 tests with NR + XeSS at 1080p30 and native 4K30 recorded zero additional audio pauses. Synthetic capture audio with 30/35ms processing fluctuations recorded no additional resets or underruns. File seek/resume and deliberate overload-recovery regressions passed. These are software tests, not long-session streaming validation or physical display/speaker synchronization measurements.

This release does not reduce NR's computational cost. Sustained GPU overload can still require waiting for video; reduce quality, multiplier, or resolution if needed.

The NVIDIA original and community RTX 40/50 NR variants remain selectable, and DLL replacement remains permitted. The community variant is modified and reports **HashMismatch**; RTX 40 hardware validation is pending. NR and DLSS frame generation are community-experimental integrations, not NVIDIA certification. SDKs, runtimes, and models remain outside source Git. See `docs/RUNTIME_COMPONENTS_0.0.4.md` and the included manifests for provenance, hashes, licenses, and removal instructions.
