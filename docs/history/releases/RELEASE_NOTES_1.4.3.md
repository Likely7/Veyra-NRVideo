# Veyra 1.4.3

本版汇总 1.4.2 发布后的修复。最新测试包已获用户验收；只保留有效修复，失败或无收益的调度实验已回退。

## 补帧与帧同步

- 修复 DLSS / XeSS 切换自动弹回、XeSS 2X 后更高倍率被旧能力缓存阻挡；切换失败恢复原设置，界面不再假装应用成功。
- 修正 XeLL 标记周期、运动/重置与呈现资源生命周期，改善 XeSS 运行与切换稳定性。
- 修正调度成本统计：初始化样本不污染稳态预算，阻塞 Present 的 CPU 时间不再重复算作 GPU 成本；调整实时呈现相位和窗口采集帧间隔。
- 修正 GPU 完成 fence 归属、首个生成帧截止时间及已排队工作计费，改善预算拒绝后的恢复；文件播放使用有界追赶。
- 保留用户选择的倍率，不自动改为低倍率；没有新增固定 35/50ms 等待。过载时仍可能错过呈现机会。
- XeSS 垂直同步交由代理交换链呈现；关闭帧同步时不额外叠加等待。DLSS 垂直同步不再依赖最大排队 API 是否可用。
- 增加独立 XeSS 提交统计；采集 FPS 改用近期窗口，避免长期平均掩盖当前状态。

## 窗口、调色与操作

- 修复拖动窗口边角时 Win32 回调栈耗尽造成的卡死/崩溃；合并跨屏 DPI 字体与布局刷新，避免同步消息重复嵌套。
- 参数滑条新增单项还原图标；NR、羽化、调色的未提交数值不再被刷新覆盖。还原失败保留输入，成功只清除对应项草稿。
- 按住 V 查看原画时同时绕过调色。
- 上下方向键调节音量，日常模式画面与音量控件支持滚轮调音量；专业模式画面滚轮保留缩放，不抢编辑框按键。

## 字幕

- 修复字幕按钮点开就关闭；设置面板可连续调整样式、延迟等参数。
- 新增目标行数：默认 2 行，可选自动或 1–8 行，按完整文本测量缩放，不直接丢弃第三行之后的内容。
- 修复多组字幕顶部/中部/底部排布、居中及样式缓存，保留 ASS 显式定位；升级迁移旧设置。

## 采集、解码与截图

- 音频样本缺少有效 DirectShow 时间戳时，按真实样本数建立后备时钟；分离 PCM 与压缩音频接收，编码变化重新连接。
- 设备模式枚举失败时尝试驱动当前格式，改善部分采集设备兼容性。GC551/GC573 实卡效果仍待反馈。
- 压缩采集队列保持有界；帧间编码溢出后清理解码参考链并等待关键帧，避免持续使用损坏参考。
- 修复解码帧身份、FFmpeg EAGAIN 后同包重送、输出 PTS/色彩元数据配对与 B 帧重排处理；释放遗漏的 D3D11 Context4 引用。
- 修复 NR + RTX Video HDR 截图中不合理的逐位相等拒绝条件，保留浮点 HDR 数据检查和原子保存。
- 修复 MSVC 中文环境的头文件依赖解析，避免增量构建混入旧 ABI 对象。

保留 RTX Video HDR、窗口/屏幕采集、PS5、图形字幕及原有导出能力。NVIDIA FSR4 实验保持撤回。增强运行组件身份与 1.4.2 相同，属于实验集成，不代表厂商认证。首次启动全部增强默认关闭；包内不含个人配置、凭据或测试媒体。

## 验证与已知问题

窗口缩放、合成 DPI 变化、参数草稿、字幕、补帧切换、解码和便携包有本机针对性回归。最新窗口修复包已获用户反馈“没啥问题”；本机只有一个物理显示器，不将合成 DPI 检查冒充真实双屏验收。RTX5070 的结果不代表所有型号、驱动或采集卡。

**固定 6X 的均匀呈现仍未解决。** 本机 RTX5070、原生 4K60 素材、NR1080、4K 输出的近期对照约 295–303 次提交/秒，仍有约 16.8ms 长间隔；实际 SR 被原生 4K 旁路，不是额外超分负载成绩。不能据此宣称稳定 360fps 或无卡顿。2X/4X 针对性短测约 120/240 次提交/秒。软件提交 FPS 不等于屏幕刷新或光学延迟。

GC573 RGB 实际欠速、部分设备功耗波动、YUY2 50fps 的用户观感及与 PotPlayer 的清晰度差异，尚未取得完整定因/实卡验收。本版修复已定位的软件缺陷，不宣称这些反馈全部根治。

## Download / English Summary

Download **Veyra-1.4.3-win64-portable.zip**, extract to a new folder and run Veyra.exe. **Veyra-1.4.3-source.zip** includes application source plus the unchanged patched FFmpeg and RemotePlay dependency source archives. SHA-256 files accompany both downloads.

This release repairs DLSS/XeSS switching and scheduling accounting, GPU completion synchronization, resize crashes and DPI refresh, parameter reset/draft handling, subtitle dialogs/layout, capture audio/decoder handling, and NR + RTX Video HDR screenshot rejection. Selected multipliers are retained with no added fixed 35/50ms wait. Rejected experiments are excluded. Fixed 6X can still present unevenly; submitted FPS is not physical refresh. GC551/GC573 and all GPU/driver combinations are not claimed validated.

## 支持与反馈

如果这个项目帮到了你，可以请作者喝杯咖啡（微信扫码，完全自愿，不影响任何功能）；有问题或想第一时间拿到 beta 版，欢迎进群。

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.2/docs/images/1.4.2/community-group.png" alt="Veyra 交流群 / bug 反馈 / beta 版本" width="220">
</p>
<p align="center"><small>左：微信赞助　右：Veyra 交流群（bug 反馈与 beta 版本发布）</small></p>
