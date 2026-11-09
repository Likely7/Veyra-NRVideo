# Veyra 2.1.4 更新说明

本次更新从 2.1.3 开始汇总。新增采集卡音频转接、NVIDIA DLAA 和 NR 推理复用，扩展双重超分的算法组合，并修复音量记忆、录制快捷键、RTX 超分选项和 AMD NR 初始化。

## 采集卡音频转接

- 采集卡面板新增可选的 **Veyra 虚拟扬声器**。程序内附原版 VB-Audio VB-CABLE Pack46，选择「安装」并确认后即可离线打开安装器。Veyra 会检测系统里已安装的驱动，软件升级时可直接复用。
- 当采集卡直采无声、Windows「侦听此设备」有声时，从面板打开「Windows 录制设置」，进入采集卡音频属性的「侦听」页，播放目标选 **CABLE Input**。回到 Veyra，在「音频监听」选对应的虚拟扬声器，在音频输出选真实耳机或音箱。Veyra 通过 WASAPI 回录接收 PCM，继续应用音画同步、手动偏移和软件音量。
- 安装状态、使用说明和卸载入口各自显示。采集中可以先查看卸载说明，断开采集后再确认卸载。回录线路会拦截输出到同一端点，防止声音回灌。VB-CABLE 采用捐赠软件许可，来源和支持方式随包提供。 [VB-Audio 官方说明](https://vb-audio.com/Cable/) · [VB-Audio 支持与分发条款](https://vb-audio.com/Services/licensing.htm)

## 双重超分、DLAA 与 HDR

- **两次超分可以混搭算法。** 基础超分完成后，按设定降低尺寸，再执行第二次超分。两次可分别选 DLSS SR、FSR 3.1、RTX 视频超分标准／高码率／流媒体，分别设置目标尺寸和可用档位。两次 DLSS SR 或两次 FSR 各自维护处理上下文和时域历史。
- **抗锯齿加入 NVIDIA DLAA。** 用同尺寸的 DLSS 模式处理视频帧，按可用能力启用，并在需要时为 DLAA 适配相邻帧光流。专业模式的列表和节点都能使用，支持相应预设。 [双重超分与配置兼容](VOLUME_DUAL_SR_2026-11-08.md)
- **改善原生 HDR 下的超分与抗锯齿。** FSR 可进入 HDR 处理路径。DLSS SR、FSR 和 DLAA 共用广色域恢复，保留超出 BT.709 的颜色分量。两次超分各自处理 HDR 色彩，相关的显存预算和初始化失败回退一并完善。 [HDR、DLAA 与录制验收](RECORD_HDR_DLAA_2026-11-08.md)

## NR 推理复用与监控

- **NR 自动推理复用**位于专业列表的「NR 画面增强」，默认关闭。手动 1/2、1/3、1/4 分别表示每 2、3、4 帧复用 1 帧。自动档根据增强耗时选择复用频率。复用时用光流把上一推理帧的增强变化对齐到当前帧，并在内容不匹配、出界或保护区域降低权重。
- 该选项用于符合条件的 NVIDIA NR、SDR 光流预览。暂停单帧和导出会完整推理。输出稳定器和 NR 全局保护区域归到 NR 画面增强下，补全相关图标。阶段耗时按每个源帧统计 NR 平均用时，悬停 P95 仍反映推理帧的耗时。 [NR 复用条件与实现](NR_INFERENCE_REUSE_2026-11-07.md)
- 帧率卡显示 **提交 FPS 与 1% low**。1% low 由最近 10 秒内的呈现提交间隔估算。调色的负色温响应更平滑，减轻冷端拉满时的过强蓝偏。 [帧率指标与色温修正](FPS_LOW1_AND_COLOR_TEMPERATURE_2026-11-07.md)

## 稳定性与兼容

- **音量记忆**会自动保存调整后的数值，关闭软件时补写尚未落盘的音量。重新启动后恢复音量。
- **录制快捷键**过滤长按产生的自动重复事件。录制开停日志记录触发来源，快捷键日志记录触发时的有效映射，便于区分按钮与键盘触发。
- **RTX 视频超分选择**按 QML 模块内容隔离编译缓存，覆盖升级后使用与新版界面匹配的缓存，修复部分用户点标准、高码率或流媒体后跳回 DLSS SR 的问题。
- **AMD lmxxf NR**在主进程创建前先用独立子进程预检 HIP 初始化，按通过的档位选择调度优先级。预检失败时可降档或拦截初始化，避免直接在播放器进程里尝试失败档。 [QML 缓存与 HIP 预检](CLAUDE_FIXES_MERGE_2026-11-06.md)

## 测试数据

| 项目 | 结果与条件 |
|---|---|
| 2.1.4 两个便携包 | 最小 PATH 独立冷启动，NVIDIA 与 AMD 包各完整呈现 90 帧 H.264 测试片。AMD 包的基础播放在同一台 RTX 5070 主机上验收 |
| 界面与升级 | Qt Quick 175 项通过。同目录从 2.1.3 覆盖到 2.1.4 后，旧缓存保留，新版第二次启动复用其 81 个 QML 缓存文件 |
| 采集音频 | WASAPI 合同 19 项、防回灌 2 项通过。合成 PCM 接真实 WASAPI 输出，手动偏移 100 ms 通过 |
| 画质回归 | RTX 5070 上 NR 复用及 DLAA、DLSS SR、FSR 的 HDR 用例通过。1440p DLAA 与 4K 超分联用的测试完成 4 帧，其中 3 帧使用光流 |
| NR 复用样例 | 开发阶段 RTX 5070 的 4K60 输入、4 层 NR 样例，1/2 复用使平均增强耗时由 16.24 ms 降至 8.36 ms，提交帧率由 52.8 升至 60.0 FPS。结果取决于片源、参数和硬件 |

## 下载与迁移设置

选择与 Veyra 实际使用显卡匹配的 **NVIDIA 或 AMD** 便携包，完整解压到可写目录并运行 `veyra_qml_ui.exe`。

推荐在退出新旧版后，将旧版的 `runtime_local/user-data-2.0.0` 复制到新版对应位置。自定义 LUT 另复制 `runtime_local/luts`。如果曾更改数据目录，以「设置 → 关于」显示的路径为准。同显卡包也支持退出后覆盖解压。新目录便于保留旧版和原始配置。

应用与 RX 7000 网络对应源码见 [2.1.4 源码 Release](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.4-sources)。 [构建与固定依赖源码](BUILD_2.1.4.md)

## English

- Optional capture-card audio routing through the bundled, original VB-Audio VB-CABLE installer. Select **CABLE Input** as the Windows **Listen to this device** target, select the virtual speaker in Veyra, and use real speakers or headphones as the output. Veyra applies its existing A/V sync and manual offset to the captured audio.
- Dual super resolution supports independent DLSS SR, FSR 3.1 and RTX Video SR passes. NVIDIA DLAA is available under anti-aliasing. DLAA, DLSS SR and FSR share HDR wide-gamut restoration.
- Real-time NVIDIA NR inference reuse is off by default and offers automatic, 1/2, 1/3 and 1/4 modes. Frame-rate monitoring now shows presentation-submission FPS and 1% low. Cool color-temperature adjustments are smoother.
- Volume persists across restarts. Recording shortcuts filter key repeats and log the trigger source. Content-based QML cache isolation fixes RTX Video SR selections reverting to DLSS SR after some upgrades. AMD lmxxf NR uses a HIP preflight before initializing in the player process.
- Extract the matching GPU package and run `veyra_qml_ui.exe`. For settings migration, exit both versions and copy `runtime_local/user-data-2.0.0` and any custom `runtime_local/luts`. For a custom data directory, use the path shown in Settings → About.
