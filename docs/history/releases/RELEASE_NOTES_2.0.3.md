# Veyra 2.0.3

## 中文

运行包按显卡分为 **NVIDIA** 和 **AMD**。选 Veyra 实际使用的显卡对应包，完整解压后运行 `veyra_qml_ui.exe`。两包使用同一程序，播放器、采集、串流、图片/视频导出完整保留。运行只需下载一个显卡包；源码包仅供重编译。

- **NVIDIA 包**：保留三版 NVIDIA NR、DLSS/RTX 超分、RTX Video HDR、DLSS 补帧、NVIDIA VFG、FSR3.1 和 XeSS；不携带 AMD NR。按用户追加决定保留原有 FSR3.1/4 共用组件，NVIDIA 的 FSR4 选项仍置灰。
- **AMD 包**：保留 lmxxf AMD NR、FSR4/FSR3.1 兼容和 XeSS；不携带 NVIDIA NR、NGX、VFG 或 CUDA 运行库。AMD NR 需要 RX9000 与驱动 HIP 7，已携带完整 0.39 模型/内核。
- **不可用功能保留并置灰**：NR 四版本、超分算法、补帧后端、RTX HDR、光流选择与节点添加使用统一能力判断并显示原因。恢复旧预设/会话会关闭不可用效果，保留参数；直接设置和导出预设也检查能力。
- **NVIDIA VFG**：2X/3X/4X/5X/6X/7X/8X 全倍率及低/中/高三档，接入预览、预设、重启保存与独立视频导出 worker。裁掉九个未使用的 NPP 库，减少约 274 MiB 解压体积。High 8X 运算成本高，不能保证实时 240fps；离线导出保持完整处理。
- **Xbox**：补齐音频配置后的 WASAPI 启动，增加有界重连恢复与诊断。用户日志里的服务器关闭/会话断开仍可能发生；本轮没有 Xbox 真机长测，不宣称网络根因已消除。
- **视频导出码率**：编辑有效码率立即提交，修改其他选项保留草稿，开始导出前完整验证并冻结 worker 设置，修复自动跳回默认及直接导出被误拒绝。
- **小飞机兼容**：修复软件绘制界面的透明背景；保留兼容绘制与不透明背景。不再把 NVIDIA App 图画插件注入直接判断为崩溃原因并拦截；真正 GPU/运行错误继续记录。
- **极简模式**：修复非整数缩放时原生视频窗口右侧少像素造成的黑边，按实际客户区覆盖和边缘裁剪处理。

NR/补帧仍是带明确边界的实验能力。AMD NR 内部最高 1080p 像素预算，HDR、原生 1440p/4K NR 导出尚不支持。实际 NVIDIA 测试机为 RTX5070 / 驱动616.56；AMD RX9000 推理、RTX40、用户616.92、Xbox真机有声/长期稳定和屏幕端到端延迟未获实机验收。软件提交帧率不等于物理显示帧率。HDR/Dolby Vision PR #13/#14 本轮暂缓。

## English

Choose the **NVIDIA** or **AMD** portable package for Veyra's active GPU, extract with 7-Zip and run `veyra_qml_ui.exe`. Both use the same full player; download one GPU package to run, source assets only to rebuild.

- NVIDIA keeps its three NR runtimes, DLSS/RTX SR, RTX Video HDR, DLSS FG, native VFG, FSR3.1 and XeSS; AMD NR assets are excluded. The existing shared FidelityFX 2.3.0 runtime is retained by explicit user choice; FSR4 ML remains disabled on NVIDIA.
- AMD keeps lmxxf NR 0.39, FSR4/3.1 compatibility and XeSS; NVIDIA NR/NGX/VFG/CUDA files are excluded. AMD NR requires RX9000 and driver HIP 7. Its complete weights/kernels are retained.
- Unsupported choices stay visible and disabled with an actual hardware/component reason in List/Node mode. Restored configurations retain their parameters but disable unavailable stages. Direct settings and export presets also enforce availability.
- VFG supports every 2–8X multiplier and Low/Medium/High, including preview, persistence, presets and isolated NVENC export workers. Nine unrelated NPP libraries are omitted (about 274 MiB unpacked). High 8X is expensive and does not promise real-time 240fps; offline export keeps all source frames.
- Fix Xbox audio startup and bounded session recovery; bitrate edits/reset/validation and frozen export settings; transparent RTSS-compatible UI backgrounds; and the minimal window's right-edge gap at fractional DPI. NVIDIA App injections no longer trigger the removed false crash check; actual GPU failures are still logged.

RTX5070/616.56 was tested. RX9000 inference, RTX40, the reported 616.92 driver, physical Xbox audio/long sessions and physical display latency remain unverified. AMD NR is limited to its 1080p pixel budget and does not support HDR or native 1440p/4K NR export. Submitted FPS is not measured physical display FPS. HDR/Dolby Vision PRs #13/#14 are deferred.

Both packages carry separate identities, licenses and matching [application source](https://github.com/Likely7/Veyra-NRVideo/archive/refs/tags/v2.0.3.zip), [dependency source](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.3/Veyra-2.0.3-dependency-source.zip) and [rebuild instructions](https://github.com/Likely7/Veyra-NRVideo/blob/v2.0.3/docs/BUILD_2.0.3.md). Source Git excludes proprietary SDKs/models/runtimes. Experimental runtime distribution is user-authorized, not vendor endorsement/certification. Publisher manifests do not hash-lock user DLL replacements.

## 支持与反馈 / Support & feedback

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.0.0/docs/images/2.0.0/community-group.png" alt="Veyra 交流群 4" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-09 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-09.
