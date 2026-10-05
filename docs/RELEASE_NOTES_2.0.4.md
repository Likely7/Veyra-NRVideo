# Veyra 2.0.4

本地发布前测试候选 / local candidate for testing before publication. Public assets remain at 2.0.3.

## 中文

- **NR 画面调控：** 总变化强度最高5，保留关闭调控的原始效果；自动分别适配风格0/1/2，并可调力度。手动提供九项色彩、亮度、高光和时域保护。每层独立，列表/节点、会话/预设与导出使用同一设置。
- **Xbox 恢复：** H.264 解码报错或压缩输入丢包后清理参考历史、等待关键帧；连续失败时每会话有界重开硬解，再失败回退软解；仍无法恢复时明确结束并保留失败原因。恢复首帧带时间线断点，音频不靠停声制造同步。
- **UI：** 列表画面与参数面板之间可拖动，记忆宽度、双击还原，视频窗口随面板同步；滑条旁数字支持点击输入、Enter/失焦确认、Esc取消；修复极简浮窗切模式误触播放/暂停、调色开关窄列溢出、NR卡片间距与导出滚动条。
- **统一预设：** 移除误加的独立补帧预设。在“另存为列表/节点预设”中勾选“光流与内容节奏”，保存光流算法、运动估算质量、AMD半分辨率和内容节奏；未勾选时应用预设保留当前值。支持默认预设、重启及导入导出，旧预设保留原有行为。
- **播放与性能：** 整合已验证的核心/单NR缓存、暂停复用及限定队列/导出调度，自定义0.25–4倍速、布局/全屏恢复、RTSS重启兼容；预热默认关闭。GPU优先级无保存值时默认实时，系统拒绝时尝试高档并显示实际状态，用户旧选择保持。
- **现场修复与字幕：** 采集跳帧不再错误地反馈为低源节奏/频繁清历史；播放期间请求退出进程电源节流，停止恢复系统策略。每次图重建恢复GPU耗时采样；AMD NR不再被非NVIDIA规范化逻辑关闭。libass渲染外挂/内嵌ASS特效和字体附件，普通SRT/VTT保持原样。

分别提供完整 NVIDIA / AMD 包，两个包同一EXE，运行组件和模型原字节保持，保留 patched FFmpeg 和 libass 许可证。新文件夹解压运行 `veyra_qml_ui.exe`，不用下载SDK或源码才能运行。ZIP可用Windows或7-Zip解压。

本机RTX5070的短测不能代替真实Xbox/RX9070XT、RX9000 NR、RTX20/30/40、受影响采集卡及后台系统场景。采集输入仍约60Hz，原NR+4K SR+3X组合实际消费约55–57fps，未宣称本版本该组合必达60fps。Xbox日志的硬解Invalid argument触发器尚未在AMD实机复制，恢复路径修复不等于驱动/补帧根因已查明。软件回退可能增加CPU与解码延迟，实时GPU优先级可能影响同时运行的其他程序，可在设置改为高/普通。可选Auto NR/先粗后细会改变画面；HDR/Dolby PR13/14继续暂缓。详见 [整合验收](RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md) 和 [本次预设纠偏验收](LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md)。

## English

- NR total strength reaches 5 with optional, per-layer automatic/manual picture control. Styles 0/1/2 have separate automatic defaults; nine manual controls and an amount slider are shared by sessions, presets and exports. Disabling control preserves the original residual behavior.
- Xbox decoder recovery flushes reference history after hard errors or compressed-input loss, waits for a keyframe, and permits one hardware reopen followed by one software fallback per session. Persistent failures end with a preserved error; the first recovered frame resets enhancement history.
- Professional panels are resizable and remember their width. Slider numbers accept typed values. Floating mode controls avoid unintended playback clicks; card spacing, narrow colour controls and the export scrollbar are repaired.
- The existing list/node preset dialog includes an optional optical-flow and content-cadence block. It preserves the shared backend, estimation quality, AMD half-resolution and cadence through restart, startup defaults and import/export. Unchecked fields keep their current values. The separate frame-generation preset UI was removed.
- Includes later NR performance work, custom playback rate, RTSS compatibility and fullscreen/layout repairs. Prewarming defaults off. An unset GPU scheduling preference now requests Realtime, with an explicit High fallback when refused; saved preferences remain unchanged.
- Includes capture cadence/drop recovery, measured GPU timing after graph rebuilds, AMD NR enablement and libass animated/embedded ASS subtitles with font attachments.

Choose the NVIDIA or AMD portable ZIP for the active GPU. Both use one production EXE, unchanged audited runtimes, patched FFmpeg and libass notices. Local RTX5070 regression is not actual Xbox/RX9070XT, RX9000 inference, other RTX hardware, capture-card or physical-display certification. Software decoding can add CPU/latency; GPU priority can be changed in Settings. The original demanding capture combination is not guaranteed to reach 60 processed fps. HDR/Dolby PR13/14 remain deferred.

## 支持与反馈 / Support & feedback

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.0.0/docs/images/2.0.0/community-group.png" alt="Veyra 交流群 4" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-09 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-09.
