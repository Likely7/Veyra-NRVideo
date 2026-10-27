# 本地整合状态 / Local integration status

> 后续更新：下文“历史快照”部分保留 dc44c48 阶段的分支、候选和待办证据。1.3.0 已整合其后修复并正式发布；最新完成项、边界和远端资产证据以[1.3.0更新说明](../releases/RELEASE_NOTES_1.3.0.md)与[发布记录](../releases/RELEASE_1.3.0_EXECUTION.md)为准，包括真实采集杂音修复、HDR转SDR新映射和逐子帧呈现。

更新日期：2026-09-15。当前源码基线：`0605304`；下文历史快照基线：`dc44c48`。本文不是发布说明或实卡验收报告。

## 当前 1.3.0 状态

- 当前公开版本：[`v1.3.0`](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.3.0)，Release 已公开，六个资产均已上传并完成远端大小/SHA-256核对。
- 当前 `main`：`0605304`，发布标签 `v1.3.0` 保持指向发布提交 `05dea56`；后续提交只补充发布审计文档，没有改动发布包代码。
- 构建、软件 delivery、便携包隔离启动和 AV1 播放验证已完成；实体采集卡拔插、OBS、PS5、RTX30/40 和显示器扫描输出仍按硬件逐台验收。
- 详细功能清单见 [1.3.0 更新说明](../releases/RELEASE_NOTES_1.3.0.md)，构建与依赖来源见 [构建说明](BUILD.md)。

## 发布与分支（历史快照）

- 本节原记录的下载版本为 `v1.2.0`；该值属于历史快照，当前下载版本已更新为 `v1.3.0`，远端核验见上方当前状态与发布记录。
- `main` 已从 `edabd3c` 快进到 `dc44c48`；后者与保留的 `codex/capture-fg-clock-repair-20260914` 修复分支指向同一代码基线。
- 合并完成时 `git status --short` 为空，原有23个未提交文件已由 `dc44c48` 保存。构建、运行组件、测试媒体和日志继续留在忽略目录，没有通过删除它们制造干净状态。
- 媒体兼容、采集格式、HDR/多声道、RTX30及此前PS5修复分支均已包含于主线历史，不需要再次逐个合并。
- 旧源码归档 `codex/github-source-archive` 与已被取代的 `codex/smooth-motion-experiment` 不合入；原分支保留。正式采用允许用户叠加、不强制互斥的Smooth Motion指引。
- “未推送、未发布、未变更应用版本号”是 dc44c48 阶段的结论，已由后续 1.3.0 发布取代。

## 1.2.0 之后已整合、尚未发布的修复（历史快照，已由1.3.0发布吸收）

| 修复 | 提交 | 实际证据与未验证边界 |
| --- | --- | --- |
| AV1软件解码与文件硬解首帧安全回退 | `9b6c414` | AV1 MP4、ProRes MOV等样本通过；本地FFmpeg启用dav1d并保留PS5 slice补丁。不是所有MOV编码组合保证，也未增加AV1/ProRes导出。 |
| 合法负AAC时间戳导致MOV黑屏 | `de17c2b` | 以实际预填和有限PTS启动音频时钟；原问题MOV短测542帧、failed=false。 |
| 采集卡内置音频无法选择 | `d9a94eb` | 增加视频filter内置音频pin绑定、稳定DevicePath及旧连接串兼容；本机独立音频枚举和专项通过，反馈者内置音频实卡未验收。此提交标题为checkpoint，但确实包含该修复。 |
| 长时间实时补帧截止时间漂移 | `ebba6f1` | live帧对重新锚定，源PTS仍保持真实；五分钟漂移模拟、DLSS 2/3/4 Create/Evaluate等通过。未完成反馈者显卡/采集卡同源实测。 |
| 音频连续性、格式与恢复 | `dc44c48` | 保留连续重采样历史、有限浮点余量，区分软件空拉与设备断流；包含validBits/packed24、预填及视频reset隔离修复。撤销cubic和块级AGC方向；真实沙沙声未验收。 |
| WASAPI录音端点输入 | 本轮本地整合 | UI同时显示`[WASAPI]`与`[DirectShow]`；共享模式、稳定endpoint ID、QPC时间轴、静音包和有界重连接入现有PCM链。本机USB3端点和视频+WASAPI组合通过静音回归；用户设备与听感未验收。 |

详细记录：[媒体兼容](../plans/MEDIA_FILE_COMPATIBILITY_PLAN_2026-09-14.md)、[音频设备选择](CAPTURE_AUDIO_DEVICE_SELECTION_REPAIR_2026-09-14.md)、[FG时钟](CAPTURE_FG_CLOCK_REPAIR_2026-09-14.md)、[当前音频修复§6](../plans/CAPTURE_AUDIO_WAVEFORM_REPAIR_PLAN_2026-09-15.md)。旧音频施工结论仅作追溯，以新方案§6为准。

## 已发布基线中保留的改动

- **1.1.0**：采集格式扩展与步长/UV/高位深SDR修复；Smooth Motion使用指引。
- **1.1.1**：首次启动效果全关，保留已保存设置；RTX30实验兼容选项，RTX30实卡验收仍未完成。
- **1.2.0**：HDR输入/增强/导出、SDR显示开关与文件/采集5.1；同时修正FP16截图行数据、六声道缓冲和单声道/立体声匹配问题。真实HDR显示、5.1扬声器及具体采集设备仍须验收，PS5保持立体声。
- 此前PS5解码细条、重连额度、串流恢复和补帧调度修复继续保留。没有恢复旧32-slice FFmpeg限制，也没有合入废弃的强制互斥实验。

## 当前候选与验证证据（历史候选）

候选为 dc44c48 阶段的本机完整构建目录，不是当前 1.3.0 便携发布包。移动EXE时不能遗漏同目录依赖和shader。

- 路径：`out/build/audio-continuity-repair-20260915/veyra.exe`（相对项目根目录）。
- 大小：11,463,168字节（本轮自动同步修复候选）。
- SHA256：`F060238E39311D9E2D3D8B8CB4EA50B0CBB6E4BD24E67BC10D90A249F417BACC`。
- RemotePlay ON；FFmpeg前缀 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed`。SDK、DLL和模型未加入源码Git。
- 前轮最终专项：12组短用例均exit0；离线生产DSP47/47；两组各120秒±1000ppm漂移回归exit0，P95软件偏差4.24594/4.30137ms，missing=0、resets=1（启动），队列高水位89.6458ms。均不冒充用户声学验收。
- 日志：`logs/audio-continuity-repair-20260915/final/`，含 `results.json`、`drift-results.json`、波形/端点/漂移日志；日志被Git忽略，仅本机存在。
- 合并轮复核：`cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0（ninja no work to do）；`veyra_audio_waveform_tests.exe --offline` 47/47；`git diff --check`通过。合并轮没有重跑完整端点/漂移/GPU套件。
- 本次文档更新仅检查文档差异与本地文件链接，不以文档提交宣称新增运行验证。

## 未闭环问题与下一步（历史记录）

用户在火堆、口哨场景听到持续沙沙声，OBS对照无此现象。已确认的音频代码缺陷被修复，不等于已定位并根治整个听感问题；旧 `clipped=0`、静音端点测试和构建成功都不足以证明保真。

用户当前远程无法测试，不要求立即实卡验收。后续唯一诊断主线是取得问题场景同源PCM，在原始输入、转换后、最终输出前定位首次额外失真，再与系统输出/OBS进行条件匹配对照。完整生产链分段tap尚未实现，不能写成已有功能。不再凭音高猜过载，不盲加低通或块级自动增益。

上述“后续发布需要另行授权”是历史状态；1.3.0 已完成授权发布。当前仍未闭环的能力和硬件边界以 1.3.0 更新说明为准。

WASAPI专项记录见 [WASAPI接入方案与证据](../plans/WASAPI_CAPTURE_INPUT_PLAN_2026-09-15.md)。

历史候选还包含[采集自动补偿与慢启动积压修复](CAPTURE_VRR_AUDIO_SYNC_AUDIT_2026-09-15.md)：以匹配原帧的到达时间校验跨流时间戳，异常时回退本机处理时间估算；启动/恢复时清理已过期音频并明确记录。该候选的测试结果以此专项文档为准，不替代 1.3.0 发布包的硬件验收。反馈者实卡VRR原因尚未证实；根目录旧启动脚本是否指向当前发布包，仍需按构建说明使用明确的便携目录。
