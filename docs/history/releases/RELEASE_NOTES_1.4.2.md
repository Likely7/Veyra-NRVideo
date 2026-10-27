# Veyra 1.4.2

本次重点：**RTX Video HDR** 与 **可完全关闭的帧同步**，同时整合 1.4.1 之后的播放器、采集、字幕和界面修复。

## RTX Video HDR：SDR 视频转 HDR

- 专业模式 → 增强 → **RTX Video HDR**，可调对比度、饱和度、中间灰和峰值亮度。
- 接入共享增强链，可与 NR、DLSS / RTX 超分和补帧组合；原生 HDR 输入不重复转换。
- 支持转换后的 HEVC Main10 HDR 导出。HDR 预览需要兼容的 HDR 显示器，并开启 Windows HDR；SDR 屏保持 SDR，不会故意变灰。HDR 导出不要求本机有 HDR 屏。
- 开关下显示实际预览状态，区分“请求开启”与“实际运行”。本次集成视频版 RTX Video HDR，不是游戏版 RTX HDR。

<p align="center"><img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.2/docs/images/1.4.2/rtx-video-hdr-comparison.jpg" alt="RTX Video HDR 用户实测：上关闭，下开启" width="720"></p>
<p align="center"><small>用户提供的同一设备相机拍摄对比：上图关闭，下图开启。照片展示该设备的观感，不代表原始 HDR 像素或屏幕亮度测量；软件 FPS 也不是屏幕物理刷新率。</small></p>

## 帧同步：按需控制排队和呈现节奏

入口：专业模式 → **运动 → 帧同步**。默认关闭，选项与设置可保存。

| 选项 | 实际作用 |
| --- | --- |
| 关闭 | 关闭新增帧同步策略，保留正常媒体时钟及必要的补帧依赖 |
| 低排队 | 减少提前处理与呈现排队，控制积压 |
| 均匀呈现 · 前端同步 | 在低排队基础上按媒体时间安排提交，避免一串帧挤着提交 |
| NVIDIA Reflex · 实验 | 无补帧时使用 NVAPI Sleep/markers；开启补帧时明确回退低排队，保留补帧倍率 |

显示同步另可选择允许撕裂、垂直同步或自动；自动当前采用 VSync，不会替你修改 G-SYNC。XeSS 保持提供方调度，不叠加这套前端节奏控制。

修复过期生成帧阻挡已就绪原帧的等待；降低部分采集 DLSS 路径的额外相位余量。帧同步不等于提高 GPU 算力，也不保证所有组合降低延迟。[软件侧延迟对照与测试口径](https://github.com/Likely7/Veyra-NRVideo/blob/v1.4.2/docs/FRAME_PACING_ACCEPTANCE_2026-09-18.md)。

## 其他新增与修复

- **窗口 / 显示器采集**：专业模式左侧窗口图标进入，支持 WGC、DXGI 显示器兼容、目标选择、裁剪、鼠标指针和帧率上限。默认沿用目标显示器刷新率；仅采画面，声音由原应用播放。静止画面不重复填帧凑 FPS。
- **采集卡设备帧率**：手输 30、40 或小数，0 沿用所选格式。重新连接后直接向 DirectShow 设备协商；拒绝或返回其他档位时明确报错，不偷偷改用软件丢帧限速。设备是否支持取决于驱动与格式；不能保证自动去除游戏重复画面。
- **图形字幕**：增加 MKV 内嵌 PGS、DVD、DVB 字幕显示，保留文本字幕及音轨切换。
- **调色预设**：保存时可命名，并提供明确反馈，修复保存无反应。
- **采集 HDR**：修复 P010 明确 PQ/HLG、但缺少色域字段时的默认值，以及调色截断 HDR 负分量造成的颜色异常。设备不报 HDR 元数据时仍需手选，P010 本身不等于 HDR。
- **XeSS**：修复缩放时重建呈现资源等问题；撤回导致无法开启的拖动暂停生成方案，拖动画面时继续补帧。
- **界面**：修复全屏操作提示残留、专业模式 PS5 按钮被遮挡、设置滚动残影与选框悬停闪烁。
- **诊断**：细分“补帧受限”原因，避免把输入不足一概显示为 GPU 瓶颈。5060 反馈者的具体实卡根因尚未确认，不宣称全部解决。
- **撤回 NVIDIA FSR 4.1 实验**：画质未达预期，旧实验配置迁移至 RTX 视频超分“高”；保留原有 DLSS SR、RTX 视频超分和官方 FSR 能力。
- 保留 1.4.1 的 RTX30/40 DLSS 最高 6X 兼容修复、持续播放补帧受限修复、MKV 打开与拖动优化、设置保存及导出修复。RTX50 保持原生 DLSS 路径。

## 下载与验证范围

下载 **Veyra-1.4.2-win64-portable.zip**，完整解压运行 Veyra.exe。首次效果全关。升级请解压至新目录，需要时迁移旧配置，不要整体覆盖旧运行库。

**Veyra-1.4.2-source.zip** 包含对应应用源码，以及未改变的 FFmpeg / Remote Play 依赖源码包；运行播放器不需要下载源码。

本机 GPU 验证环境 RTX5070 / 616.56；已有 TrueHDR 创建、执行、NR/SR/FG 组合及 Main10 导出证据。用户提供了本次 HDR 显示对比照片。采集卡手动帧率已在 OBS 虚拟摄像头验证 30/40/默认60 FPS，VC-007PRO 本轮未连接；RTX30/40 HDR、不同物理采集卡与 PS5 本轮未逐台验收。NR、补帧兼容层和相关集成仍为实验功能，不代表 NVIDIA 官方认证或支持。

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.2/docs/images/1.4.2/community-group.png" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>
