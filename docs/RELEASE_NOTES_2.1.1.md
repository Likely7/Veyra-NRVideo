# Veyra 2.1.1 更新报告

本版汇总2.1.0发布后Claude与Codex共同完成的功能和修复，重点是处理后录制、画面旋转、NR尺寸控制，以及采集、补帧、显示和界面链路。

## 界面绘制与默认超分模型

- 合入软件绘制设置：界面默认使用软件绘制，设置→兼容可切换软件/GPU绘制，重启后生效；视频解码、增强与呈现继续使用GPU。
- 保留OBS和监控软件的自动兼容规则，设置页显示当前实际绘制方式及切换提示。
- 默认超分模型调整为RTX视频超分（高质量）；首次启动仍保持效果关闭。已经保存的模型选择继续保留，AMD/Intel沿用可用的FSR默认项。

## 录制处理后的画面与声音

- 新增处理后画面实时录制入口，支持开始倒计时、取消和停止保存；录制直接取播放器的处理结果。
- 修复录制开始/停止的异步状态与资源释放，音视频工作队列有界，停止时正确收尾编码器和文件。
- 修复录制与音频播放的生命周期衔接，保留播放器原有的声音状态。

## 画面旋转与窗口适配

- 极简和专业模式增加旋转按钮，每次顺时针90°，全屏复用同一操作。
- 播放预览、横竖比例、极简窗口尺寸及鼠标坐标随方向同步适配；切换模式、暂停和跳转保留方向。
- 修正旋转后极简窗口反复恢复横屏比例的问题，按钮布局同步适配。

## NR手动比例与视频导出

- NR内部处理分辨率新增“手动”，25%–100%、1%步进，各层可独立调整，列表与节点共用。
- 滑条松开后提交，支持键盘/数值输入、保存预设与会话，减少拖动时反复重建处理图。
- 接入社区导出尺寸补丁，修复视频导出强制恢复原生NR尺寸的问题，固定档和手动百分比都按所选值执行。
- NR内部尺寸与最终编码尺寸分别管理：以NVIDIA路径为例，4K视频选择1080p或50% NR，NR按1920×1080处理，最终仍按所选4K尺寸导出。
- 修复多层尺寸与导出预设冻结，任务使用启动时的配置快照，播放界面的后续调整各自生效。

## 补帧、刷新率与HDR状态查询

- 将刷新率/VRR驱动查询移出播放线程，修复该同步查询造成的周期性阻塞与调度错过期限。
- 刷新率、VRR与HDR改为共享事件触发查询，按显示器缓存，合并重复请求，切屏和显示变更时更新。
- 专业模式左下“信号”区统一显示刷新率、HDR和VRR，并提供手动刷新按钮。
- 修复HDR图重建等待过期缓存的问题；已有缓存立即使用，后台更新，同时修正查询失败后的缓存恢复。

## AMD增强与抗锯齿

- 修复AMD关闭FSR超分后，处理状态仍可能重新启用超分的问题。
- 修正AMD原生HDR内容启用NR时被初始化检查拒绝的路径。通过SDR线性代理进行NR，再将增强结果还原至原HDR基底，保留高光与颜色信息。
- 调整列表模式抗锯齿顺序：启用NR时在NR组后执行AA，避免后续NR重建覆盖边缘处理。

## 采集接收与NR关闭生命周期

- 修复采集视频接收对缺少时间戳的预备样本返回失败的问题；合法缺时戳样本确认后跳过，后续有效样本继续按真实时间戳处理。
- 增加限频的采集投递/时间戳诊断，便于核对输入状态。
- 修正NR切换和关闭时的NVAPI模块生命周期，复用系统模块，避免逐图卸载造成的冲突；完善关闭阶段日志。

## 菜单、截图、全屏与快捷键

- 修复菜单和下拉框首次打开时文字位置跳动：打开前完成布局，保留原弹出动画。
- 截图通知在实际保存完成后显示真实文件路径，HDR截图正确报告JXR格式。
- 修复最大化→全屏→退出时的窗口尺寸恢复与极简布局跳变。
- 修复播放列表与主窗口争抢Esc/Delete/Enter，统一全屏退出和控制条锁定的按键归属。
- 同步简中、繁中、English和日本語界面说明与翻译。

## 下载与升级

按实际使用的显卡下载NVIDIA或AMD完整包，完整解压到新目录，运行`veyra_qml_ui.exe`。程序会读取原有用户设置，升级时保留个人预设与配置。

- [NVIDIA完整包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.1/Veyra-2.1.1-NVIDIA-win64-portable.zip)
- [AMD完整包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.1/Veyra-2.1.1-AMD-win64-portable.zip)
- [应用与依赖对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.1-sources)

本次发布为整合软件绘制与RTX视频超分默认项后重新构建的2.1.1完整包，Windows文件版本2.1.1.0。manifest保留封包时记录，正式发布时间以本页为准。

## 校验与组件

完成13组定向回归，QML117项通过；两包的软件界面/D3D12界面共四组解压启动播放检查通过，ZIP文件逐项哈希与完整性校验通过。

运行组件与模型沿用2.1.0受控供体，随包保留来源、版本、哈希、许可证和实验/社区标记。Veyra为独立开源项目，社区实验组件不代表GPU厂商官方认证。AMD NR适用RX9000及对应HIP运行环境。

下载文件的SHA256校验值见GitHub Release页面。

感谢社区提供NR导出尺寸补丁与使用反馈，也感谢持续支持项目的用户。

## English — Changes since 2.1.0

- Default the interface to software drawing with an optional GPU setting and restart confirmation; video stays on the GPU. New SR selections use RTX Video SR high, while saved selections and AMD/Intel FSR defaults are preserved. Effects remain off at first launch.
- Record the processed picture and player audio, with countdown/cancel controls, bounded asynchronous start/stop and encoder/file finalization.
- Rotate the playback view by90° in Cinema/Professional/fullscreen, with matching aspect ratio, window layout and pointer coordinates.
- Per-layer manual NR processing scale:25–100%,1% steps, saved with presets/sessions. Video export now honors fixed and percentage NR sizes while retaining the selected encoded output dimensions.
- Move display-driver queries off the playback thread. Refresh rate, VRR and HDR share an event-driven cached service and a refresh button in the signal panel.
- Correct HDR-cache waits/recovery, AMD FSR-off state, and AMD native-HDR NR initialization through an SDR-linear proxy with HDR reconstruction.
- Apply List-mode AA after the NR group; acknowledge timestamp-less capture preparation samples without rejecting subsequent delivery; retain system NVAPI across NR teardown.
- Settle popup layouts before opening, preserve the existing animation, report actual saved screenshot paths/formats, restore maximized/fullscreen geometry and scope playlist keys correctly.
- Update all four interface languages.13 targeted regression groups,117 QML tests and four extracted-package startup/playback checks passed.

Download one complete GPU package and extract it into a new directory. Runtime versions/licenses and community-experimental labels are retained from the audited2.1.0 distribution. The2.1.1 ZIPs are rebuilt with the interface-renderer and SR-default changes; embedded manifests describe their packaging time.

## 支持与反馈 / Support & feedback

<p align="center">
  <a href="https://discord.gg/c9aREyMj8"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join Veyra on Discord" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://ko-fi.com/likely7"><img src="https://storage.ko-fi.com/cdn/kofi5.png?v=6" alt="Support Veyra on Ko-fi" height="36"></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/main/docs/images/community/group-7-20261020.png" alt="Veyra 交流群 7" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-27 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-27.

[Veyra 软件知识库 / Knowledge base](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)：四个群聊汇总的使用文档，涵盖软件与采集卡常见问题。

## 下载校验 / SHA256

| 完整包 | SHA256 |
|---|---|
| NVIDIA | `31a4f9acf34b01c5bfc43d8af5c8de14232089f0b99a9e213a9a7c659c6a0277` |
| AMD | `b78b8a0ceec9d677821432c847684e5caac7005910cd44173725c83e255c5226` |
