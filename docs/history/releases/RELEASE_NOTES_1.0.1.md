# Veyra 1.0.1

## 中文

下载 **Veyra-1.0.1-win64-portable.zip**，完整解压后运行 **Veyra.exe**。无需另装 SDK，附加的源码包不是运行必需品。

### 修复与改进

- 修复采集音频连接时未请求低延迟缓冲的问题：现在在连接前向驱动请求约 **10ms 一批**的音频，并按实际采样率、声道和格式对齐。这针对部分设备音频分大块交付、声音明显落后画面的情况。
- 驱动不支持或拒绝缓冲协商时保留兼容连接，记录实际结果。
- 增加音频输入块时长、到达间隔、实际分配器容量诊断，方便区分驱动交付、软件补偿、PCM队列和输出端等待。
- 保留现有音画同步、连续重采样、PS5、截图及增强功能。

**10ms 是请求的输入块长，不是端到端延迟承诺。** 驱动可能不接受；反馈设备仍需更新后实测。构建、采集合同和合成PCM/WASAPI回归已通过，不等同于全部采集卡的听觉验收。

### 升级

退出旧版，解压到新目录后启动，并重新打开采集设备，使新缓冲请求生效。PS5配对保存在当前Windows用户目录，无需重新搬运。需要保留增强预设时，可复制旧目录的 `runtime_local/*.v1` 和 `veyra.ini`。

沿用1.0.0的七个增强运行文件及原实验边界。社区NR签名状态仍为 `HashMismatch`，软件允许用户替换DLL；组件清单不是加载锁。SDK/运行时不进入源码Git，FFmpeg和串流依赖的对应源码、补丁及许可作为附加资产提供。

## English

Download **Veyra-1.0.1-win64-portable.zip**, extract it completely and run **Veyra.exe**. No separate SDK installation is needed. The source archives are not required to run the player.

### Fixes and Improvements

- Fixed missing low-latency buffer negotiation when connecting capture audio. Veyra now requests approximately **10ms input blocks** before connection, aligned to the actual sample rate, channel count and format. This addresses large delivery blocks that can leave audio noticeably behind video on some devices.
- Devices that do not support or reject the advisory request retain the compatible connection, with the result logged.
- Added input block duration, callback interval and actual allocator-capacity diagnostics alongside software compensation, PCM and endpoint queues.
- Existing audio synchronization, continuous resampling, PS5 streaming, screenshots and enhancement features are retained.

**10ms is the requested input block duration, not an end-to-end latency guarantee.** Drivers may not honor it; affected hardware still requires a post-update test. Build, capture-contract and synthetic PCM/WASAPI regression checks passed, which is not listening validation on every capture device.

### Upgrade and Components

Close the old version, extract to a new directory and reopen the capture device to apply the new request. Encrypted PS5 pairing remains in the current Windows user profile. Presets can be migrated using `runtime_local/*.v1` and `veyra.ini`.

The seven enhancement runtime files and experimental feature boundaries from 1.0.0 are unchanged. Community NR retains `HashMismatch` signature status; user DLL replacement remains allowed. Component manifests are publisher records, not loading locks. No proprietary SDK/runtime enters source Git. Matching FFmpeg and streaming dependency source, patches and licenses are provided as separate assets.
