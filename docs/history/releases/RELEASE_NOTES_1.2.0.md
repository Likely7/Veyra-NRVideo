# Veyra 1.2.0

## 中文

- 新增 HDR 文件与 P010/P016 采集输入：识别明确标记的 HDR10 / PQ、HLG 与 BT.2020 NCL；采集设备漏报时可手动指定 PQ 或 HLG。P010 本身不等于 HDR。
- HDR 下可使用 NR、DLSS SR / RTX Video SR，以及 DLSS / XeSS 预览补帧。NR 与 RTX Video SR 使用 SDR 代理和保留的 HDR 基底合成，不应被理解为原生 HDR 模型推理。
- 新增 HEVC Main10 / BT.2020 / PQ HDR 导出与 FP16 scRGB JPEG XR 截图。普通 PNG/JPEG 保持 SDR；XeSS 仍只用于预览。
- 文件和采集的 PCM 音频现在保留最多 5.1 的真实声道位置；Windows 立体声端点会明确降混。PS5 串流保持上游提供的立体声，不把它伪装成 5.1。
- 采集卡面板新增“转为 SDR 显示”，默认关闭。打开后把 HDR 映射为 SDR 预览，不需要改 PS5 或 Windows HDR，也不关闭增强；播放中切换不需要重新连接。截图跟随预览，视频导出不受这个开关影响。

**已验证范围**：RTX 5070 上的 HDR GPU 处理、HDR 文件导出、NR / SR / DLSS / XeSS 组合，以及软件层面的六声道数据与时钟测试。真实 HDR 屏观感、5.1 扬声器定位、特定采集卡的 HDR/多声道能力仍需要实机验收。压缩 Dolby/DTS 码流直通和 Atmos 对象音频未实现。

下载 **Veyra-1.2.0-win64-portable.zip**，完整解压后运行 `Veyra.exe`。升级建议解压到新目录；保留旧设置会恢复先前已保存的效果开关。Smooth Motion 继续由 NVIDIA App 管理，Veyra 不修改驱动设置。

## English

- Added HDR file and P010/P016 capture input for explicitly described HDR10 / PQ, HLG, and BT.2020 NCL sources. PQ or HLG can be selected manually when a capture device omits metadata. P010 alone does not imply HDR.
- NR, DLSS SR / RTX Video SR, and DLSS / XeSS preview generation can run with HDR. NR and RTX Video SR combine an SDR proxy with a retained HDR base; this is not native HDR model inference.
- Added HEVC Main10 / BT.2020 / PQ HDR export and FP16 scRGB JPEG XR screenshots. Ordinary PNG/JPEG remain SDR; XeSS remains preview-only.
- File and capture PCM retain real speaker positions up to 5.1. Stereo Windows endpoints receive an explicit downmix. PS5 Remote Play remains stereo when the upstream stream is stereo.
- The capture panel now has **Convert to SDR display**, off by default. It maps HDR to SDR for live preview without changing PS5 or Windows HDR and without disabling enhancement. It can switch during playback without reconnecting. Screenshots follow the preview; video export does not.

**Verified scope**: HDR GPU processing, HDR file export, NR / SR / DLSS / XeSS combinations, and software six-channel data and clock tests on RTX 5070. HDR display appearance, 5.1 speaker placement, and individual capture-card HDR/multichannel behavior still need hardware acceptance. Compressed Dolby/DTS passthrough and Atmos object audio are not implemented.

Download **Veyra-1.2.0-win64-portable.zip**, extract fully, and run `Veyra.exe`. NVIDIA App continues to manage Smooth Motion; Veyra does not change driver settings.

---

The RemotePlay-source and FFmpeg-source ZIPs provide corresponding dependency source and are not needed to run the application. Proprietary SDKs, credentials, development files, logs, and test media are excluded.
