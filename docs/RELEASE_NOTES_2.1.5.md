# Veyra 2.1.5 更新说明

## 采集预览

- **改善开启去重后的补帧节奏。** 例如 40 FPS 游戏通过 60 Hz 采集时，去重后相邻有效画面的到达间隔会在约 17 和 33 ms 之间交替。现在图内补帧按去重后的内容时间轴安排呈现，减少相邻帧忽快忽慢的情况。
- **校正部分采集设备报告的异常帧时间戳。** 当原始采集回调稳定、驱动时间戳持续明显偏离时，预览按实际采集节奏推进。正常的 59.94 / 60 Hz 时间戳照常使用。音频同步继续读取驱动原始时间。
- **本机数据。** T911 采集 4K60 NV12、约 40 FPS 内容、VFG 3 倍的 A/B 中，提交帧率保持约 120 FPS，提交间隔 P99 从 17.33 ms 降到 9.04 ms，界面 1% low 中位数从 57.66 升到 110.03 FPS。真实帧从采集回调到呈现提交返回的中位耗时由 24.12 ms 增至 31.54 ms。以上为软件侧计时。

## 下载与设置

选择与 Veyra 实际使用显卡匹配的 **NVIDIA** 或 **AMD** 便携包，完整解压并运行 veyra_qml_ui.exe。可在退出 2.1.4 后覆盖解压同显卡包。若解压到新目录，复制旧目录的 runtime_local/user-data-2.0.0 和自定义 runtime_local/luts。自定义数据目录以「设置 → 关于」显示为准。

已安装的 VB-CABLE 可继续使用。便携包中的社区实验增强运行时、版本、许可和哈希见包内清单。

## English

- Improved frame pacing after duplicate-frame removal for capture preview. Graph-based frame generation now schedules presentation on the deduplicated content timeline, reducing uneven delivery when capturing 40 FPS content at 60 Hz.
- Raw capture preview corrects timestamps when the device reports a persistent mismatch against stable callback cadence. Valid 59.94 / 60 Hz timestamps pass through, and audio sync retains the original driver timing.
- In a T911 4K60 NV12 test with approximately 40 FPS content and VFG 3×, the P99 interval between presentation submissions fell from 17.33 to 9.04 ms at roughly 120 submitted FPS. Median capture callback to Present return for real frames rose from 24.12 to 31.54 ms.
- Exit Veyra before overwriting 2.1.4 with the matching GPU package, or extract to a new folder and copy runtime_local/user-data-2.0.0 and any custom runtime_local/luts. Existing VB-CABLE installations are reused.
