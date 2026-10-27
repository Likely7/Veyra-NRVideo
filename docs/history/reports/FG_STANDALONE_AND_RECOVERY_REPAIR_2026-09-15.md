# 独立补帧与剩余代码缺口修复

## 用户报告与根因

用户报告：全部效果关闭时直接选择帧生成无效，先打开NR、再关NR后才正常。核对后，直接原因位于UI总开关，不是缺少一条强制光流初始化调用。

`SettingsWindow::liveField(202)`原来走通用参数提交；`AppShell::applySettings`在总增强关闭时只保存草稿，因此FG请求未送入EngineController。NR/SR按钮走独立消息，能够开启总增强；开一次NR也顺带启用了先前保存的FG配置。底层 `initNvof()` / `runMotion` 本来就包含DLSS和XeSS独立依赖。

现在FG倍率选择与NR/SR按钮走同一显式开关事务：选2X及以上时允许开启总增强，选择关闭补帧不会开启总增强；仍保留参数校验和事务回退。修改后从全关启动实际DLSS、XeSS，NR始终未执行，不使用“先偷偷运行一次NR”的变通方案。

## 一起处理的缺口

- `FgRecoveryBudget::complete`接受可缺失的GPU时间，缺失时不添加CPU轮询耗时。已有样本会过期；每个候选仍按真实截止时间判断，不把未知当零或取消过载保护。
- 非NVIDIA的NR/SR/DLSS请求在建图前归一化为实际支持状态，重算处理尺寸并同步已应用设置/警告，保留XeSS选择。实际AMD/Intel硬件仍待验收；不自动替换用户光流后端。
- 面板在关闭补帧但处理欠速时显示“处理过载”，不再显示“补帧受限”。
- 采集音频增加独立PCM进度监测：按数据包计数判断，静音包继续到达不触发恢复；3秒无进展后按1至5秒退避重试原DirectShow音频端点。复用原连接和格式协商函数，没有复制两套音频接入。
- DirectShow共享图重建音频pin前先Stop，失败时不继续改图；成功时保留视频filter及格式，移除旧音频分支后重新连接、恢复音量/同步设置、Run。停止/移除/恢复HRESULT均记录；下一张视频帧显式标记Discontinuity并增加源epoch，防止接上旧光流历史。
- 音频修复会短暂停共享DirectShow图，不承诺视频完全不中断。WASAPI沿用自己的端点恢复；未自行改成默认麦克风或其他设备。驱动Stop异常的最终关闭仍依赖驱动释放行为，记录错误不等于已证明任意故障驱动安全退出。

## XeSS数值的复核

用户关于“旧版/其他机器可以显示”的描述尚未有对应版本与截图。已检索历史 `9057d2f`、`83f90ba` 的面板代码，都对XeSS FG耗时明确显示“不可测”。本机使用的XeSS 3.0.2头文件 `xefg_swapchain_present_status_t`提供framesPresented、frameGenResult、isFrameGenEnabled，当前接入没有内部GPU耗时字段。

这些证据不能推出用户没看到数字，也不能推出所有历史版本都一样。当前能读到的是SDK报告的帧数/生成状态，以及Veyra自身GPU阶段和CPU Present调用耗时；它们不是同一个指标。不同机器性能不会改变这段UI主动显示“不可测”的条件。未拿到对应旧版本证据前，不用CPU耗时或缓存值冒充精确XeSS GPU耗时。

## 验证

候选目录 `out/build/audio-continuity-repair-20260915`；入口 `out/start-user-issues-candidate.cmd`。最终EXE SHA256：`00DD5FD72B1849006F86041723B150E9EDF3D19575DFBCC5B0E47CE2BF86AD8E`。原runtime和patched FFmpeg不变。

实际执行：

1. `cmd.exe /c out\build\veyra-build-x64-release.cmd` 两次最终均exit0。主构建 `logs/fg-standalone-recovery-build-20260915.log` 79/79；补显式Discontinuity与FG日志标签后 `logs/fg-standalone-recovery-build-final-20260915.log` 30/30。
2. `veyra.exe <用户4K视频> --no-nr --no-sr --no-fg --smoke-fg-only --smoke-seconds 30`，经run-short-test限制55秒，exit0。新增 `FgOnlyChecks.h`通过实际控件CBN_SELCHANGE路径，不直接调用engine来绕开故障UI。依次确认全关、DLSS实际生成、关总增强、XeSS实际生成，两个后端均有NVOF且NR评估为0。日志 `logs/fg-only-selector-test-20260915.log`。NVOF CreateInstanceD3D12/CreateOpticalFlowD3D12状态0；XeSS Init/GetSwapChain/Present结果0，持续framesPresented=2。此短测位于最后一次仅音频断点/日志标签修改之前。
3. `veyra_repair_contract_tests.exe`：144 checks / 0 failures，包含新增非NVIDIA设置归一化和PCM进度恢复策略，exit0，`logs/fg-recovery-contract-20260915.log`。
4. `veyra_presentation_worker_tests.exe`：exit0，包含新增缺失GPU时间不污染预算、超期仍拒绝，`logs/fg-recovery-budget-20260915.log`。首次误写为不存在的veyra_live_gpu_scheduler_tests.exe，命令未执行；按CMake目标名纠正后通过，不采纳那次失败为测试结果。
5. 最终候选重跑 `--smoke-transport --smoke-seconds 18`，run-short-test上限45秒，exit0，`logs/fullscreen-transport-isolated-20260915.log`。专业/日常全屏完整命中区、长按拖动时不隐藏/不提前seek、松手目标呈现、暂停左右10秒、音量焦点全屏快捷键、窗口音量键隔离均通过。前轮自动测试失败保留；本次独立通过与用户确认共同构成第5项证据。

DirectShow音频分支热恢复尚未在反馈者实卡测试；恢复策略合同不冒充真实拔插和声音恢复。非NVIDIA处理仅做合同验证。本机NR/DLSS/XeSS证据不扩大到4070/4070Ti或其他设备。

6. 最终执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915`，exit0，结果 `logs/delivery/1987bb07e103441591d6dacf2e632cac/result.json`。实际NR/NVOF、原生4K、GUI播放/暂停seek、图片、H.264/HEVC 4K 2X帧数/音轨/取消检查通过；capture仍明确awaiting_user_capture_test。该结果对应最终候选hash，未把实卡边界抹掉。

## 剩余边界

原生Dolby Vision、XeSS内部精确计时未实现。HDR转SDR的问题映射需要对应素材/输出路由支持，不能凭“发灰”统一再改gamma。OBS/PS5、40系过载及原问题文件导出仍按14项审计保留问题设备验收要求；没有因新增恢复代码就把它们标记根治。源码提交与本地候选交付不代表发布授权。
