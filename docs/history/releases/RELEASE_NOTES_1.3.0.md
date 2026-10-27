# Veyra 1.3.0

本次整合1.2.0发布后的播放器、采集音频、颜色和补帧修复。首次启动仍默认关闭所有增强，已有明确保存的设置继续保留。

## 播放器与操作

- 增加AV1软件解码（dav1d），改善MOV/ProRes等文件播放；文件硬解首帧失败可安全回退。修复部分MOV因负音频时间戳而黑屏。
- 优化拖动进度条：保留最新目标，避免先弹回旧位置；拖动过程中持续请求预览，不必等松手才更新。实际响应仍受关键帧间隔、解码与增强速度影响。
- 全屏可拖动进度条，并用左右方向键前进/后退10秒；修复控件遮挡、命中范围和焦点问题。
- 减少视频区域重绘闪烁；普通窗口尺寸变化避免不必要的整条增强链重建。

## 采集与声音

- 记住上次视频设备、采集格式、音频端点和颜色选择；按稳定设备标识恢复。
- 采集断开后尝试重连同一设备，核对格式并重置处理历史；增加音频端点独立恢复。不同设备驱动的拔插行为仍需实机验证。
- 支持明确选择DirectShow音频、采集设备内置音频或WASAPI录音端点。
- 修复自动同步被异常时间戳带偏、启动积压以及播放过程中音频延迟不断增长的部分原因；不会将用户所述VRR关联直接当作已证实根因。
- 修复重采样连续性、PCM格式解析及端点恢复；修复自动同步逐渐耗尽音频播放储备、造成随音乐出现沙沙/滋滋声的问题。本机真实录音对照已消除所复现的间隙，不代表所有采集卡均已验收。

## 颜色与增强

- 修正关闭增强时SDR输入/输出曲线不匹配导致的颜色变化，按明确的range、matrix和transfer处理。
- HDR转SDR改进亮度映射及色域压缩：PQ优先使用有效MaxCLL或母版峰值，缺失时明确回退；减少逐RGB通道硬裁切造成的颜色偏移。HDR转SDR不等于无损保留HDR。
- 修复BT.2020 NCL输入误入仅支持BT.601/709的转换路径，以及部分导出颜色元数据问题。
- NR内部处理新增480p、720p、900p、1440p，保留1080p与原生；降低NR尺寸不等于超分、补帧和输出尺寸同时降低。
- 修复NR强度高于1时暗部残差外推过度压黑。

## 补帧、性能与恢复

- 修复全关状态直接选择补帧不生效、必须先开一次NR的问题。
- 修复实时补帧时钟漂移、将CPU轮询延迟误计入GPU预算及恢复不稳定的问题。
- 实时DLSS多帧生成按子帧就绪提交，避免第一张已完成仍等待整批结束、随后被判过期。本机同设置3X对照：修前32秒420张生成帧过期，修后120秒14050张有效生成帧全部提交、过期0，稳定段显示提交180fps。该数据不代表所有显卡性能或屏幕扫描实测。
- 右侧帧率统一为显示提交；详细面板区分处理产出、原帧/生成帧提交及过期丢弃，避免处理产出180fps掩盖输出不足。
- 修复GPU计时样本消费与阶段汇总。XeSS内部精确GPU耗时仍显示不可测，总增强耗时明确不包含该部分。
- 改善NR/SR/FG/光流故障的归属与降级；XeSS呈现失败后可恢复基础播放并在同会话重试。
- 修复合法交换链模式被误拒绝，以及ResizeBuffers暂时受占用时直接进入错误的问题，涉及OBS捕获和PS5连接初始化路径。反馈者设备仍需复测。

## 导出

- 修复部分状态下点击导出无选择位置弹窗。
- 改善导出末尾计数、封装收尾、验证与文件改名的错误处理和日志，方便定位99%失败；不将反馈者未提供的原始文件故障一概宣称根治。
- 保留H.264/HEVC视频导出和HEVC Main10 HDR导出；没有新增AV1或ProRes编码导出。

## 支持边界与下载

下载 `Veyra-1.3.0-win64-portable.zip`，解压后运行Veyra.exe。另外两个source ZIP是依赖对应源码，普通用户无需下载。

尚未提供完整Dolby Vision RPU/增强层输出、Atmos对象渲染或Dolby/DTS码流直通。部分DV文件仅按明确兼容的基础层播放。NR/SR使用HDR基底保留与SDR代理时，不宣称模型原生HDR推理。RTX30/40、不同HDR屏幕、OBS、PS5和采集设备的具体表现仍可能不同。Smooth Motion继续由NVIDIA App管理，软件提供开启指引。

## English

Veyra 1.3.0 integrates fixes since 1.2.0:

- AV1 playback through dav1d, improved MOV/ProRes compatibility, safe initial hardware-decoder fallback, and a fix for MOV black screens caused by negative audio timestamps.
- Latest-target seeking and previews while dragging; fullscreen seek controls and left/right 10-second shortcuts; reduced repaint flicker and unnecessary graph rebuilds.
- Remembered capture devices/formats/audio/color settings; same-device reconnect and independent audio recovery; explicit DirectShow/embedded/WASAPI audio selection.
- Capture synchronization, startup backlog, continuous resampling, PCM-format and playback-reserve fixes. A reproduced music-correlated crackle was eliminated in local recording comparisons; this is not universal hardware acceptance.
- Correct SDR code-value handling, metadata-aware HDR-to-SDR luminance mapping and gamut compression, BT.2020 NCL handling, and export color metadata fixes.
- NR internal 480p/720p/900p/1440p options alongside 1080p/native; fixed excessive shadow darkening above strength 1.
- Standalone FG activation, live clock/budget recovery, per-subframe readiness for live DLSS MFG, clearer submitted-versus-produced FPS, timing collection and backend recovery fixes.
- More resilient swapchain negotiation/resize for capture hooks and PS5 initialization; export-dialog and finalization diagnostics improvements.

Download the portable ZIP; the two dependency-source ZIPs are for developers. Enhancements remain off on fresh installs. Full Dolby Vision, Atmos rendering, Dolby/DTS passthrough, AV1/ProRes export and precise internal XeSS FG timing are not added. Hardware-specific limits remain. Runtime identities are unchanged; FFmpeg now includes dav1d and retains the PS5 H.264 slice patch.
