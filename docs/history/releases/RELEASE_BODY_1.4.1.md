Windows 视频播放器与采集卡增强工具。**1.4.1 重点修复 RTX 30/40 DLSS 补帧，以及持续运行后“补帧受限”的问题。**

下载 **`Veyra-1.4.1-win64-portable.zip`**，完整解压后运行 `Veyra.exe`。另两个 ZIP 是依赖对应源码，普通用户无需下载。

## 重点修复

- **RTX 30/40 最高 6X DLSS 补帧**：修复无法开启、请求高倍率却回到 2X，以及 **RTX 3060 开启后卡死**。修正兼容初始化、网络选择、资源处理和异常探测退出。
- **运行一段时间后补帧受限**：修复统计、队列等待和恢复调度错误，避免显卡未满载却长期不再补帧；同步修复 6X 的批次数组、计时与资源交接。
- **RTX 50 保持原生路径**。受影响用户已反馈最新测试包可用；30/40 兼容解锁仍属社区实验功能，未宣称所有型号和驱动组合均已验证。

## 用户实测截图

**RTX 4070：4K IMAX 素材，DLSS 6X，NR/SR 关闭。** 软件显示提交 360.0 fps，当前状态正常。

<p align="center"><img src="https://github.com/Likely7/Veyra-NRVideo/releases/download/v1.4.1/rtx4070-dlss6x-imax.png" alt="RTX 4070：IMAX 素材与 DLSS 6X 设置" width="900"></p>

**RTX 4070：任务管理器与播放状态同屏。** 截图显示 GPU 64%，DLSS 6X，软件显示提交 360.0 fps。

<p align="center"><img src="https://github.com/Likely7/Veyra-NRVideo/releases/download/v1.4.1/rtx4070-dlss6x-gpu.png" alt="RTX 4070：DLSS 6X 播放与 GPU 状态" width="900"></p>

**RTX 5070：4K 输入/输出，1080p 内部 NR 处理与 DLSS 6X。** 截图显示 GPU 61%，软件显示提交 180.0 fps，当前状态正常。

<p align="center"><img src="https://github.com/Likely7/Veyra-NRVideo/releases/download/v1.4.1/rtx5070-nr-dlss6x.png" alt="RTX 5070：NR 与 DLSS 6X 同时运行" width="900"></p>

以上为用户提供的实测截图；帧率是软件“显示提交”读数，不代表显示器的物理刷新率，也不是所有素材与设置的性能承诺。

## 其他更新

- MKV **主/副字幕可选、内嵌音轨可切换**，导出跟随所选音轨；多字幕大文件改为单次后台扫描，优化打开速度和拖动定位。
- 修复 **NR/SR 等设置重启后丢失**、启动增强时拖动卡住，以及无声视频欠速后播放变慢。
- 播放期间阻止自动屏保和休眠；暂停、停止后恢复系统正常策略。
- 修正先开 NR 再开 XeSS 时的呈现缓冲索引不一致。
- 修复音频输出故障触发整个采集图误重启、画面突然卡顿；改进圆刚音频设备定位、初始化时序与诊断。
- 修复 **NVENC 版本兼容与导出错误回传**；取消导出资格门禁和完成后的整片逐帧复检，编码封装完成即可结束。保留 VFR 时间间隔，HDR 自动选择 HEVC Main10，并修复 6X 资源容量和取消导出清理。
- 整合色彩分组折叠、预设保存行与底部设置状态改进。

完整变更、测试与边界：[1.4.1 更新说明](https://github.com/Likely7/Veyra-NRVideo/blob/v1.4.1/docs/RELEASE_NOTES_1.4.1.md)。

高分辨率全增强仍受单帧处理预算限制，本次不承诺所有设置满帧率。部分合成图案的 6X 插值位置误差仍有待改善。圆刚各型号 5.1、AMD/Intel 组合仍需对应硬件验证。实验运行组件不代表厂商认证；原有 DLL 身份、许可证和源码分离方式保持。

## English

**Fixes RTX 30/40 DLSS FG up to 6X, RTX 3060 freezes on enabling FG, and persistent FG limitation during sustained playback.** RTX 50 keeps its native path. Affected users report successful testing; compatibility unlocks remain community experimental.

Also fixes embedded subtitle/audio selection, large-MKV startup and seeking, saved settings, sleep prevention, NR/XeSS presentation, capture audio recovery and AverMedia initialization. Export corrects NVENC ABI compatibility, preserves variable timing/selected audio, removes qualification gates and the final full-file decode, and handles HDR through HEVC Main10.

Download the portable ZIP. The other two archives provide corresponding dependency source. See the full notes for testing and remaining limitations.

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/community-group.jpg" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>
