# 当前项目状态

更新于 2026-11-20。Veyra 2.2.0 的下载、升级方法、完整更新与对比图见 [GitHub Release](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.2.0)。[对应源码与发行构建](BUILD_2.2.0.md)。

## 2.2.0

- 11 月 20 日社区修复覆盖导出续跑与文件恢复、硬解回退、字幕字体、Xbox 音频时钟、PS5 手柄、Moonlight 配对和虚拟摄像机。#203 实时翻译在「设置 → 播放 → 实时翻译设置」和专业模式的字幕菜单提供。本机模型与 CPU 运行库按需下载。构建和验收范围见 [工作日志](WORKLOG.md)。

- 采集卡顿、自动去重及 60→30、采集爆音与空压缩帧、VFG/FSR/XeSS 稳定性。屏幕捕获拖拽裁切、录制分段、全屏 Home 耗时、RTP/UDP 组播及网络直播时移。见[采集与直播记录](CAPTURE_AND_LIVE_PLAN_2026-11-19.md)。
- Claude 的全来源高质量缩放、界面排版和五种新语言，补帧节奏修复、自适应倍率、HUD 锁定、NVENC 两遍编码与 AQ、补帧输出上限和 CUDA 缓存修复。见[缩放记录](PRESENT_SCALING_2026-11-18.md)和[补帧与录制记录](PUSHFPS_PORTS_2026-11-19.md)。
- 打开「采集卡 → 如何选择采集格式？」阅读内置指南。先选像素格式和输入色彩，再按原帖设置 60 帧采集、去重、VFG 和超分。
- [美乐威低延迟采集](MAGEWELL_LOW_LATENCY_2026-11-19.md)和[带签名清单的便携 ZIP 更新](PORTABLE_UPDATE_PLAN_2026-11-19.md)均已合入源码。发行包内置正式更新公钥，验证 GitHub Release 中的签名清单。
- 发行提供 NVIDIA 和 AMD 安装器、便携版。Windows 安装器尚未配置 Authenticode 发布者签名。以下各项构建、回归及硬件数据来自各自注明的版本。

## PS5 SDR亮度修复

源端 BT.1886 解码/sRGB编码不匹配已修正：SDR保留RGB码值，8位回退与PQ/HDR分开处理。完整构建、实际软硬解与GPU颜色/文件/采集/HDR回归通过；旧策略四组GPU误差13/255，新策略不超过1/255，图到呈现误差0。当前私有程序 `E:/项目/Veyra/tests/codex-ps5-sdr-code-values-20261119/app/veyra_qml_ui.exe`。[验证与边界](PS5_SDR_CODE_VALUES_2026-11-19.md)；真实PS5同场景尚未复测。本轮本地main领先远端，不把源码修复冒充既有下载包已更新。

## 主线

唯一源码：`E:/项目/Veyra/worktrees/main-claude-anr-20261113`，分支`main`。

虚拟摄像机Codex处理后全分辨率输出与Claude协议2/有序送帧/消费者帧率/OBS节拍修复已合main，产品提交`740534e`。此前NR/SR/补帧、AMD AMF FRC、RX9000 FSR4回退、即时回放/F9/麦克风、采集监听与录音解耦、浏览器全屏XeSS卡死、全效果剔除编辑、日落主题和启动音乐修复均已在main；未把旧分支重复合并。

本轮构建和回归结果见[主线整合记录](MAIN_INTEGRATION_2026-11-18.md)，计划见[本轮方案](MAIN_INTEGRATION_PLAN_2026-11-18.md)，产物位于`E:/项目/Veyra/{build,tests,logs,reports,tmp}/codex-main-integration-20261118`。旧状态全文在[历史归档](history/reports/CURRENT_STATUS_before_main_2026-11-18.md)，其中“最新/未合main/未发布”只描述当时。

## 八项社区 PR

11 月 18 日的整合记录保留作历史。11 月 19 日的[社区 PR 整合](COMMUNITY_PRS_2026-11-19.md)移除了 B 站专属播放、账号与扩展逻辑，保留通用直链和 Dolby Vision。

#140/#133/#127/#115/#99/#96/#75 保留原 head 合并，#136 纯源码采用（原 DLL/LIB 历史不进入主线）。通用浏览器播放桥、Dolby Vision、韩语、300%音量、Xbox/PS5/Elgato/截图与 Linux OBS 滤镜的适配及证据见[本轮记录](PR_INTEGRATION_2026-11-18.md)。当前构建 `E:/项目/Veyra/build/codex-pr-integration-20261118/app`，私有测试程序位于同任务 tests/app；GitHub 整合结果记在 WORKLOG/FINAL.json。旧主线整合回执仍保留作前序证据。

## 虚拟摄像机

- 录制旁启用，在直播软件中选择Veyra Virtual Camera；输出处理后的完整画面，尺寸独立于软件窗口。
- 默认帧率自动跟随消费者，30/60/120可作上限；30FPS源建议2X或4X配60FPS直播，3X的90→60固有快慢交替无法消除。
- 系统组件过期时设置页提示“更新组件”；安装需要本人确认Windows管理员界面。旧协议模块不会读取新协议，管理员OBS需更新已装系统组件并重启消费者。
- SDR NV12/709局部输出；HDR在该边界转SDR。摄像机只含视频，音频单独采集；XeSS显示端生成帧不包含在图内摄像机输出。
- [实现合同](VIRTUAL_CAMERA_PLAN_2026-11-17.md) · [Claude节奏修复与历史测量](VIRTUAL_CAMERA_CADENCE_2026-11-17.md)。历史RTX5070短测不等于其他GPU、真实直播或光学延迟验收。

## 发布与保留项

既有公开2.1.9发行记录见[发行执行](RELEASE_2.1.9_EXECUTION_2026-11-15.md)；11月17日两份虚拟摄像机附加包/对应源码的发布记录在[WORKLOG](WORKLOG.md)。GitHub main 和 v2.2.0 标签包含 11 月 19 日至 20 日的社区整合、安装器与本版更新说明。

Lecram两线继续暂停；AverMedia未完成诊断、Veyra原生音频驱动原型仍隔离保留。已完成分支的恢复方式见[分支归档](BRANCH_ARCHIVE.md)。脏工作树、依赖、用户配置、正式ZIP及许可对应源码保护。

后续任务从[开发与验证](DEVELOPMENT.md)进入。硬件/实卡/其他消费者未验边界以具体本轮记录为准，不把早期“通过”当本次结果。
