# Veyra 2.0.4

正式应用版本 / Stable application release · 2026-10-06

提供NVIDIA/AMD完整便携ZIP、应用源码、完整依赖源码及SHA256SUMS。社区NR/VFG和非官方主机串流保留实验性质。

## 中文

- **NR 画面调控：** 总变化强度最高5，保留关闭调控的原始效果；自动分别适配风格0/1/2，并可调力度。手动提供九项色彩、亮度、高光和时域保护。每层独立，列表/节点、会话/预设与导出使用同一设置。
- **Xbox 恢复：** H.264 解码报错或压缩输入丢包后清理参考历史、等待关键帧；连续失败时每会话有界重开硬解，再失败回退软解；仍无法恢复时明确结束并保留失败原因。恢复首帧带时间线断点，音频不靠停声制造同步。
- **UI：** 列表画面与参数面板之间可拖动，记忆宽度、双击还原，视频窗口随面板同步；滑条旁数字支持点击输入、Enter/失焦确认、Esc取消；修复极简浮窗切模式误触播放/暂停、调色开关窄列溢出、NR卡片间距与导出滚动条。
- **统一预设：** 移除误加的独立补帧预设。在“另存为列表/节点预设”中勾选“光流与内容节奏”，保存光流算法、运动估算质量、AMD半分辨率和内容节奏；未勾选时应用预设保留当前值。支持默认预设、重启及导入导出，旧预设保留原有行为。
- **播放与性能：** 整合已验证的核心/单NR缓存、暂停复用及限定队列/导出调度，自定义0.25–4倍速、布局/全屏恢复、RTSS重启兼容；预热默认关闭。GPU优先级无保存值时默认实时，系统拒绝时尝试高档并显示实际状态，用户旧选择保持。
- **现场修复与字幕：** 采集跳帧不再错误地反馈为低源节奏/频繁清历史；播放期间请求退出进程电源节流，停止恢复系统策略。每次图重建恢复GPU耗时采样；AMD NR不再被非NVIDIA规范化逻辑关闭。libass渲染外挂/内嵌ASS特效和字体附件，普通SRT/VTT保持原样。

分别提供完整 NVIDIA / AMD 包，两个包同一EXE，运行组件和模型原字节保持，保留 patched FFmpeg 和 libass 许可证。新文件夹解压运行 `veyra_qml_ui.exe`，不用下载SDK或源码才能运行。ZIP可用Windows或7-Zip解压。

本机RTX5070的短测不能代替真实Xbox/RX9070XT、RX9000 NR、RTX20/30/40、受影响采集卡及后台系统场景。采集输入仍约60Hz，原NR+4K SR+3X组合实际消费约55–57fps，未宣称本版本该组合必达60fps。Xbox日志的硬解Invalid argument触发器尚未在AMD实机复制，恢复路径修复不等于驱动/补帧根因已查明。软件回退可能增加CPU与解码延迟，实时GPU优先级可能影响同时运行的其他程序，可在设置改为高/普通。可选Auto NR/先粗后细会改变画面；HDR/Dolby PR13/14继续暂缓。详见 [整合验收](RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md) 和 [本次预设纠偏验收](LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md)。

## 优化数据 / Measured improvements

本机RTX5070、驱动616.56、Lecram310.8.3.0正常负载三轮匹配对照。以下指标分别衡量创建、暂停编辑、进程GPU占用和增强区间：

| 场景 / Operation | 优化前 / Before | 优化后 / After | 减少 / Reduction |
|---|---:|---:|---:|
| NR暖创建 / Warm NR creation | 1569.510 ms | 379.349 ms | 75.83% |
| 严格匹配的单NR再次开启 / Eligible single-NR reactivation | 363.8505 ms | 3.6020 ms | 99.01% |
| 暂停单NR残差编辑 / Paused single-NR residual edit | 7.46140 ms | 0.76570 ms | 89.74% |
| 暂停本进程GPU最大引擎利用率 / Paused process GPU-engine utilization | 2.237185% | 0.010999% | 99.51% |
| 双NR1080＋SR4K＋DLSS2X增强区间 / Enhancement interval | 19.481 ms | 18.795 ms | 3.52% |

暖创建排除首次冷创建；近期缓存只在严格准入和key一致时命中。暂停编辑统计CPU处理含GPU完成等待；模型/源/尺寸等变化会失效并完整重算。GPU利用率是PID专属计数器，三轮各24个稳定样本，不能等同整卡占用。首启冷创建没有提速，缓存可保留显存，预热默认关闭。

最后一组软件Present P99为17.236→17.233ms，基本不变；单层原生NR仍约6.1ms。不能据此宣称所有素材FPS提高、后台掉帧根治或屏幕延迟降低。后台Create、NVOF预取和整链低分辨率等出现负收益/画面差异的候选已撤回。可选Auto NR/先粗后细会改变画面，未混入同画质默认收益。

完整统计、准入、负优化与证据见[2.0.4数据报告](PERF_RELEASE_REPORT_2.0.4_2026-10-06.md)及[最终33组普通播放](PERF_R0_ACCEPTANCE_2026-10-05.md)。上述计时来自封存优化节点与最终性能候选；正式2.0.4继承其实现，并经过后续整合/预设回归，未将历史节点计时伪装成新的逐显卡测试。

## 验收与升级

列表预设纠偏最终验收：preset482项、chain236项、repair246项、Qt Quick50项，均无失败；真实桥接覆盖勾选/未勾选、列表/节点应用、导入导出、重启/默认值。真实FSR3→XeSS→DLSS→FSR3热切换、NR强度5风格1自动/风格2手动、多层设置与PNG/4K HEVC完整60帧导出通过。两个显卡包从干净解压目录启动，Qt/FFmpeg由包内26个模块加载，ASS五事件/字体附件正常。RTX5070的软件回归不能代替AMD、其他RTX、真实Xbox与用户实卡验收。

请按Veyra使用的GPU选择一个便携ZIP，在新目录完整解压运行veyra_qml_ui.exe，保留旧版以便回退。先检查基础播放，再开启需要的效果。默认未启用效果和NR画面调控；旧明确保存选择保留。GPU优先级可在设置中改为高/普通，实时请求可能影响同时运行的程序。Qt、patched FFmpeg、libass、已审计增强组件和许可证随包提供；无需安装Python/开发SDK。

对应源码与静态字幕依赖重编译见[BUILD_2.0.4](BUILD_2.0.4.md)，包含完整应用、实际patched FFmpeg来源、libass与字体依赖源码/配方。运行时身份保持既有批准原件；publisher manifest用于核查分发内容，不阻止用户自行替换DLL，也不保证替换兼容。

## English

- NR total strength reaches 5 with optional, per-layer automatic/manual picture control. Styles 0/1/2 have separate automatic defaults; nine manual controls and an amount slider are shared by sessions, presets and exports. Disabling control preserves the original residual behavior.
- Xbox decoder recovery flushes reference history after hard errors or compressed-input loss, waits for a keyframe, and permits one hardware reopen followed by one software fallback per session. Persistent failures end with a preserved error; the first recovered frame resets enhancement history.
- Professional panels are resizable and remember their width. Slider numbers accept typed values. Floating mode controls avoid unintended playback clicks; card spacing, narrow colour controls and the export scrollbar are repaired.
- The existing list/node preset dialog includes an optional optical-flow and content-cadence block. It preserves the shared backend, estimation quality, AMD half-resolution and cadence through restart, startup defaults and import/export. Unchecked fields keep their current values. The separate frame-generation preset UI was removed.
- Includes later NR performance work, custom playback rate, RTSS compatibility and fullscreen/layout repairs. Prewarming defaults off. An unset GPU scheduling preference now requests Realtime, with an explicit High fallback when refused; saved preferences remain unchanged.
- Includes capture cadence/drop recovery, measured GPU timing after graph rebuilds, AMD NR enablement and libass animated/embedded ASS subtitles with font attachments.

Choose the NVIDIA or AMD portable ZIP for the active GPU. Both use one production EXE, unchanged audited runtimes, patched FFmpeg and libass notices. Local RTX5070 regression is not actual Xbox/RX9070XT, RX9000 inference, other RTX hardware, capture-card or physical-display certification. Software decoding can add CPU/latency; GPU priority can be changed in Settings. The original demanding capture combination is not guaranteed to reach 60 processed fps. HDR/Dolby PR13/14 remain deferred.

## Additional measurement and validation notes

The table reports three-run matched ordinary-load results on RTX5070/616.56 with Lecram310.8.3.0. Creation, residual-edit processing, process GPU utilization and enhancement intervals are separate metrics. Successful software-Present P99 stayed17.236→17.233ms; native single-NR remained about6.1ms. Cold startup did not improve, cached resources can retain VRAM, and prewarming defaults off. Rejected background creation/prefetch/whole-chain candidates are absent from the final product. These results do not establish higher FPS or lower physical display latency on every GPU/source.

Final preset regression passed482 preset,236 chain,246 repair and50 Qt Quick checks, plus actual shared-preset restoration/import/export, supported FG hot switching, NR style controls and full60-frame4K HEVC export. Both portable packages used the same tested EXE and own Qt/FFmpeg modules on clean extraction. Use the appropriate GPU ZIP in a new folder, retain the old release for rollback and read [build/source instructions](BUILD_2.0.4.md) for the complete application and dependency sources. Field hardware limits above remain applicable.

## 支持与反馈 / Support & feedback

<p align="center">
  <a href="https://discord.gg/c9aREyMj8"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join Veyra on Discord" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://ko-fi.com/likely7"><img src="https://storage.ko-fi.com/cdn/kofi5.png?v=6" alt="Support Veyra on Ko-fi" height="36"></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.0.0/docs/images/2.0.0/community-group.png" alt="Veyra 交流群 4" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-09 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-09.
