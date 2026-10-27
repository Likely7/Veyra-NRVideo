Windows 视频播放器与采集卡增强工具。支持视频、图片、采集卡实时预览和 PS5 局域网串流，可组合使用超分辨率、NR 画面增强与补帧。

**本页资产**：`Veyra-1.4.0-win64-portable.zip`（免安装包，解压即用）。完整变更见 [1.4.0 更新说明](https://github.com/Likely7/Veyra-NRVideo/blob/v1.4.0/docs/RELEASE_NOTES_1.4.0.md)。

## 1.4.0 更新

**新增色彩页**：一条全浮点、位于所有效果器之前的调色链（总开关关闭时零开销、与旧链路逐像素一致）。面板按 Lightroom 的观感与手感做：渐变色轨、可拖控制点的曲线（单调三次样条，两端黑/白场点可自由拖）、四个颜色分级色轮、混色器 8 色点条 + 黑白开关、`.cube` LUT（含输入空间检查与拒绝）、命名色彩预设与 `.vpcolor` 导入导出、撤销/重做、复制/粘贴、按住看原图，以及每个分组的“眼睛”临时停用。全链路线性光 + 输出抖动（实测灰阶色带 16 px → 5 px）；HDR 在线性域调色且在 tone mapping 之前。

**修复（40 余项，逐条见更新说明）**：IMAX/HEVC 的 MKV 打不开（新增 D3D11VA 硬解路径）、导出整槽缺帧导致失败（诚实补上一帧而非伪造插值）、调色参数不实时/暂停时不刷新、载入 `.cube` 概率闪退、导出（视频与大图）完全不应用调色、PS5 串流“连不上/打不开视频”、原生采集格式只剩 2 帧、采集卡窗口被第三方直播工具识别不到、杜比/DTS 采集无声、AVerMedia GC553G2/PRO/GC575 的 5.1 直通、XeSS 2X 静默失效与高倍率节奏不匀、RTX 30 解锁后 `FG.Available=false` 等。

**移除**：AMD FSR 补帧的界面入口（切回 DLSS 容易卡住、效果也一般）；引擎后端保留，旧设置会自动回退到 DLSS 并写日志。

## 实测对比

**MJPEG / 压缩采集链路**（2 分钟真机复测，本机 ¥30 UVC，无增强）

![MJPEG 采集链路优化前后](https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/mjpeg-latency.png)

**YUY2 等原生格式采集链路**（同一基线，各多次采样）

![YUY2 等原生格式采集链路优化前后](https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/native-latency.png)

**调色链路开启前后**（采集卡 YUY2 1080p60，各 2 分钟；每帧多 0.037 ms GPU，软件侧延迟无可测变化）

![调色开启前后的延迟变化](https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/colour-latency.png)

## 已知边界

- 采集卡→屏幕的 glass-to-glass 延迟没有物理测量；上表是进程内指标。
- AVerMedia 5.1 直通只有单元测试，需持卡用户实测；10bit P010、AMD/Intel 组合、长片整段播放未测。
- 色彩页 P1 之后的能力（纹理/清晰度/去朦胧、点颜色、镜头配置文件等）属于后续版本。
- HDR 导出的 MaxCLL/MaxFALL 未重算（原样带上并标注“未更新”）。

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/community-group.jpg" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>
