# Veyra 1.4.4

本版汇总 1.4.3 发布后的修复，重点是**补帧闪烁的三个独立成因、帧同步重做、采集串档与延迟**。
只保留有实测证据的修复；已证伪的实验方向（同组 GPU 成本估算、去掉 Present 清屏、盲目加队列、
固定等待、单 pending、无条件接纳晚到生成等）全部没有进入本版。

## 补帧与呈现

- **修掉三类闪烁源头**，三个问题互不相关、都已被日志坐实：
  - 文件播放跳帧后不再清空 NR/补帧时域历史。原判定按“跳帧计数超阈值”触发，计数器还在“提交”
    环节清零，误判频繁；改为按媒体时间间隔判断，250 ms 内保留历史。
  - 重复呈现同一帧不再被当成断点：它曾让 `xessWasEnabled_` 变假，下一帧强制提供方重置历史，
    XeSS/FSR 每次都要重新预热。
  - 采集丢帧不再整条时域历史重置。丢一帧就清空 NR 累积、让提供方重新预热，是采集下闪烁的直接来源。
- **新增“严格补帧节奏”开关**（专业模式 → 补帧页，默认关闭）。关闭＝1.4.0 的宽松规则：整组能赶上
  最后期限就生成；开启＝第一张生成帧也要赶上自己的期限，否则整组不生成，节奏更整齐、生成帧更少。
  本版同时修正该开关的排版：它此前和上下两行控件重叠。
- **帧同步重新设计**。旧的三个模式选择器（低排队／均匀呈现／Reflex）实测无可测差异，已删除；
  改为三项独立控件：**低延迟队列、显示同步、输出上限**。
  - **输出上限现在决定补帧实际生成多少**，不再是“算完再丢”。原生 DLSS 6X 开启“跟随显示器”实测：
    生成帧 5636→1659（−71%）、GPU 88.5%→62.7%、**功耗 221→174 W**；真超分档只降约 4 W，
    **不要按原生档的数字期待省电**。
  - 限速器改为绝对时间网格，修掉 100 FPS 上限实测只跑到 95.7–97.4/s 的漂移，现在四轮都是 100.0/s。
  - XeSS/FSR 由提供方持有交换链，相关控件置灰并说明“提供方自行调度”，不会偷偷压低输入帧率。
- **撤回一个指标结论**：`display-stats` 的计数器无法反映面板真实显示帧数（撕裂 + 翻转丢弃下 DXGI
  给不出），日志已改为只报原始计数并注明局限；此前基于它得出的“稳定性”结论全部撤回。

## 采集卡

- **修掉采集格式串档**：格式选择改用稳定标识（宽高＋时长＋子类型＋格式类型＋位深＋压缩），修复某些
  驱动会改动能力表导致“选 1080p YUY2 却打开 4K RGB24”的问题；重连时逐项核对协商结果。
- 采集颜色/HDR：新增输入色彩空间（自动／PQ／HLG／Rec.709）与输入范围（自动／有限／完整）选择，
  按设备保存并兼容旧配置；只有 P010/P016 才允许手动 HDR 覆盖。
- 新增 Elgato 4K60 Pro MK.2 专用入口：读取驱动私有 HDR InfoFrame，接收原生 HDR 时关闭卡内
  HDR→SDR 映射；不修改其他采集卡，也不改全局色彩公式。**该设备仍需实卡复测。**
- **采集链路内部延迟**（1440p60 NV12，本机实测）：不开增强 2.3 ms、仅 NR 9.9 ms、NR+2X 22 ms、
  NR+4X 26 ms。补帧多出来的部分是**配对等待**（要等下一帧才能插中间帧），不是处理时间变慢。
- 采集音频与线程韧性：回调拷贝移出邮箱锁、owner 按采集事件唤醒、压缩采集缓冲池复用、原生帧
  256 字节行对齐与单平面单次拷贝、软解线程数收敛、音频端点变化走恢复、owner 线程 MMCSS 优先级。
- 过滤“持续粘住”的驱动 discontinuity 标志（要求时间戳与到达节奏同时证明正常）；孤立标志、压缩
  输入和真实断点仍然保留。

## 界面、字幕与导出

- 修复专业模式**快速滚动时的白影/重叠**：面板此前用 `SWP_NOREDRAW` 搬子窗口后只做异步重绘，滚动
  那一刻屏幕上还是旧位置像素（实测与稳定帧相差 4263 像素，现为 0）。滚动改为在输入消息内提交
  整帧，并把滚出视口的行隐藏，一次滚动只重绘可见行。
- 增强页与补帧页重排：控件不再互相压盖；`Smooth Motion 开启方法` 展开说明时会把下方各行整体下移，
  不再覆盖其他控件。
- 字幕：默认保留设置字号，双行不再自动缩小；目标行数可调并保存。
- 全屏新增锁定：锁定时鼠标活动不弹出控制条，避免 HDR 反复开关导致的色调跳动。
- 输出限帧：关闭／跟随显示器／自定义帧率，**默认关闭**。
- 其它界面与稳定性修复：设置保存合并、诊断面板不可见时不刷新、拖动边角不再栈耗尽、最大化按真
  最大化、崩溃时写出 minidump 并 flush 日志、导出管理器析构等待、暂停轮询降到 50 ms。

## NR

- 新增**时间域防闪烁**（默认关闭）：双历史纹理、光流重投影、邻域一致性、保护区/羽化和无效时间/
  切镜 reset，保留 HDR 有符号分量；自然样本源残差波动下降约 71–76%。**它不是独立输入降噪。**
- NR 新增可命名预设（保存/覆盖/应用/删除），只覆盖 NR 模型、残差、剔除区与时间域开关，不碰超分、
  补帧、调色或音频。

## 验证与已知问题

本机 RTX 5070 门槛：UI 合同 384 用例、呈现 worker 127、修复合同 205、补帧呈现 D3D12 errors=0、
后端切换 16，以及新增的设置面板布局与滚动提交回归（含 96/192 DPI 与展开态）。补帧、采集延迟、
帧同步与输出上限为 30 秒量级短测 + 交替 A/B；便携包经打包校验与冒烟运行。

**未解决 / 不宣称**：

- 真 1080p→4K 超分＋NR 下 DLSS 4X/6X 仍有长间隔，XeSS 4X 重负载连续性不足；**软件提交 FPS 不等于
  屏幕刷新率或肉眼流畅度**，本版不宣称“补帧受限已根治”。
- 独立输入降噪尚未实现；时间域防闪烁不等于源画面降噪。
- XeSS/FSR 提供方交换链的最终输出限帧仍未支持。
- 杜比视界 P5/RPU 重建按用户决定暂缓。
- GC551/GC573、Elgato MK.2 实卡、多显示器、HDR 屏观感与 input-to-photon 延迟未由发布者验收；
  本机只有一个物理显示器，合成 DPI 检查不冒充真实双屏验收。
- `display-stats` 相关结论已撤回，不再作为任何“稳定”证据。

NR 与 DLSS 帧生成属于 **community experimental / 社区实验集成**，不是 NVIDIA 官方认证、官方合作
或完整游戏原生集成。首次启动全部增强默认关闭；包内不含个人配置、凭据或测试媒体。

## 快速上手要点

- **采集卡**：先关掉其他软件对同一张卡的占用 → 点“采集”选设备/分辨率/帧率/像素格式 → 先关掉增强确认基础画面，再逐项打开 NR、超分、补帧。同一模式下优先试 YUY2 / NV12。音频监听默认关闭，按来源选 `[DirectShow]` 或 `[WASAPI]`。
- **内容节奏**：主机游戏 30fps、采集卡输出 60fps 时，在“内容节奏”选“采集60→30fps处理（PS5 30帧）”；真正的 60fps 内容保持源时间戳。
- **PS5 串流**：PS5 与电脑同一局域网，PS5 设置 → 系统 → 远程游玩 启用；软件点 **PS5** 搜索主机（搜不到就用 IPv4 手填）→ 填 **PSN Account ID**（不是昵称）→ 首次仍需 PS5 上的八位配对码。手柄接**电脑**，DualSense 优先 USB。
- **Smooth Motion（NVIDIA App 驱动插帧）**：只用驱动插帧时，把软件补帧倍率设为**关闭补帧**，再到 NVIDIA App → 图形 → Veyra.exe → AI 插帧 → 开，然后重启播放器。只想用软件 DLSS / XeSS 时，在 NVIDIA App 关掉 AI 插帧并重启。两者也允许同时开启，但叠加效果未验证。
- **NR 运行版本**：专业模式 → 增强 → NR 运行版本，可选 NVIDIA 原版（RTX 50）、社区兼容版（RTX 40/50 实验）或 RTX 30 兼容（实验）。切换会短暂停顿，失败自动恢复上一套设置；选定版本后再手动开启 NR。
- **HDR**：采集默认跟随设备元数据；设备漏报时手动选 PQ 或 HLG，且必须选 P010/P016（10bit 本身不代表 HDR）。HDR 预览需要显示器开启 Windows HDR；HDR 视频导出选 HEVC（Main10 / BT.2020 / PQ）。
- **OBS / 录屏**：添加“窗口采集”选 Veyra，并把捕获方式**手动**设为 **Windows 10（1903及以上）**；“自动”可能选中 BitBlt 导致视频区域全黑。软件里的“直播兼容 · 实验”只切换显示交换链，默认关闭即可。
- **音量与画面**：上下方向键、日常模式滚轮调音量；专业模式画面滚轮是缩放，中键拖动平移，右键恢复适应窗口。
- **保留设置**：升级时只复制旧目录的 `runtime_local/*.v1` 与 `veyra.ini`；不要用旧目录整体覆盖新版运行组件。卸载=退出后删除解压目录。

## Download / English Summary

Download **Veyra-1.4.4-win64-portable.zip**, extract to a new folder and run Veyra.exe.
**Veyra-1.4.4-source.zip** contains the application source plus the unchanged patched FFmpeg and
RemotePlay dependency source archives. SHA-256 files accompany both downloads.

This release fixes the three independent causes of frame-generation flicker (preview skips, repeated
presents and capture drops no longer reset temporal/provider history), adds a strict FG-cadence switch
(default off = the 1.4.0 admission rule), redesigns frame pacing into three independent controls where
the output cap decides how many frames are actually generated (native DLSS 6X + follow-display:
generated frames −71%, GPU 88.5→62.7%, power 221→174 W), fixes capture-format mix-ups by identifying
formats with a stable key, adds capture color-space/range selection plus an Elgato MK.2 HDR metadata
path, and fixes the professional panel's scroll ghosting (4263 → 0 stale pixels). Known limits:
true-upscale 4X/6X still shows long gaps, XeSS 4X continuity under heavy load is still open,
independent input denoising is not implemented, and submitted FPS is not physical refresh.

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.4/docs/images/1.4.4/community-group.png" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>
