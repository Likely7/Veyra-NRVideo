# FRUC 移除与诊断收尾

日期：2026-09-11。基线 `da5c1b4`。本次软件构建与相关回归通过；用户实卡验收未执行。

## 本次范围

按最新用户决定：收尾 FRUC 移除；已有帧率/计时保留，修复尚存的诊断缺口。性能优化暂缓，音画同步由用户实卡验收，UI/跨屏 DPI 保持现状，发布不急。AMD NR 暂时搁置，不是当前交付阻塞项，也不宣称已经实现。

## FRUC 移除

- 删除 `FrucBackend`、`FrucWorker`、IPC 协议、独立集成测试及旧 UI 测试脚本。此前提交已移除 CMake targets 和共享图执行分支，本次彻底删除残留源码与 `Fruc = Dlss` 别名、`--fruc` 参数。
- 专业模式只提供 DLSS / XeSS，修正对应 UI 测试索引及 XeSS 初始化失败回退检查；保留 XeSS 预览 2X 边界。
- 预设写入格式升到 v8：v4/v5 的旧 FRUC=1、v6/v7 的 FRUC=1 均迁移为 DLSS，原倍率保留；v6/v7 的 XeSS=2 迁移为新 XeSS=1。v8 的 2 是非法值，不会再写含糊的 v7。无效配置继续拒绝并保护原文件。
- 修正 `package-portable.ps1` 的末尾逗号语法错误；应用 allowlist 不含 FRUC worker，runtime allowlist 不含 NvOFFRUC/CUDA11。未制作新包或修改已发布资产。
- 删除本机开发运行目录的 `NvOFFRUC.dll`、`cudart64_110.dll` 和构建目录的两个旧 FRUC EXE。仅清理这四个明确路径；本地 SDK 原件、历史日志和旧发布资产保留。
- 修正上一轮删分支后 `resolveGeneration` 日志仍引用未定义 `disabled` 的编译错误；保留 DLSS disable readback 原有语义。

旧软件不理解 v8 预设；本次不提供把新预设直接交回旧版本使用的保证。历史 FRUC 文档保留为当时证据，不是当前功能清单。

## 诊断修复

此前普通 reset 原因最终默认成 SceneCut，设置切换一律记 Resize。现在明确保留 Seek / PauseResume / Settings，来源 Drop 与 PTS discontinuity 分开；共享图返回实际检测的 PTS 跳变、切镜或 cadence break。没有原因时保留 None，不编造切镜。

新增 `diagnostics/ResetCause.h` 和 `FrameTrace.h`，接入 `EngineController` 与现有 Logger 诊断预览：

- 固定 8192 条事件环、覆盖计数、按记录顺序导出；独立于64条去重错误记录。
- 保存会话、revision、epoch、source、batch、fence、PTS、主机单调时间，记录 Submitted / Ready / Present / Gpu / Reset / Cancelled。
- Submitted 的耗时是图 CPU 提交，Ready 是观察到 fence 完成，Gpu 是真实 GPU timestamp 样本，Present 是提交返回，均不等于屏幕扫描。
- Reset 的 detail 对应 `ResetReason`，count 对应 `ResetOutcome`，总耗时为现有生命周期 CPU 观测；原因9=Settings、10=PtsDiscontinuity、11=CadenceBreak，原枚举编号保留。
- 每秒只写轨迹容量/覆盖汇总；热路径记录不格式化、不写磁盘、不增加 GPU 像素回读。诊断预览读取固定上限快照后再格式化，格式化期间不占轨迹锁。
- 实卡、物理 device lost 和屏幕扫描仍未测。轨迹是近期窗口，不是无限历史，不承诺崩溃后恢复尚未导出的内存事件。

没有修改画质参数、SR -> NR -> FG 顺序、源帧淘汰策略或导出完整性检查。

## 实际验证

以下命令从项目根目录执行，日志位于 gitignored 目录。每个测试进程少于300秒；GPU测试串行。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -Preset x64-release
./scripts/acceptance/scheduler-short-test.ps1 -Name fruc-final-contract -Exe out/build/x64-release/veyra_repair_contract_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name fruc-final-presets -Exe out/build/x64-release/veyra_repair_preset_tests.exe -TestArgs logs/continuation-repair-20260910/fruc-final-presets.v8
./scripts/acceptance/scheduler-short-test.ps1 -Name fruc-final-live -Exe out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('loop/local/fixed_clips/test_av_1080p.mp4','logs/continuation-repair-20260910/fruc-final-live')
python scripts/acceptance/ui-fg-backends.py
python scripts/acceptance/ui-fg-backends.py --reject-xess
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root "C:\Users\123\Desktop\Veyra DLSS Video Player"
```

使用旧目录中的固定测试视频不代表启用旧 Loop。

| 项目 | 实际结果 / 证据 |
| --- | --- |
| Release 构建 | exit0；`logs/continuation-repair-20260910/fruc-trace-build-final.log` |
| 合同 | 77项PASS，0.073秒；`logs/scheduler-repair-20260910/fruc-final-contract.result.json` |
| 预设 | 原有roundtrip及30组v4-v8后端/倍率迁移PASS，0.177秒；`fruc-final-presets.result.json` |
| 实际引擎回放 | 28项PASS，4.792秒；NR+DLSS2X/4X、设置、暂停恢复、诊断导出；`fruc-final-live.result.json` |
| 后端UI切换 | XeSS -> DLSS -> off PASS；`logs/continuation-repair-20260910/ui-fg-1789091809460815600/result.json` |
| UI故障回退 | XeSS拒绝后保留DLSS，再关闭PASS；`ui-fg-1789091833910763700/result.json` |
| delivery | 23项PASS，42.852秒；`logs/delivery/ef265e2874624e6aa6e80f7a8c34f8d8/result.json` |
| 脚本/残留 | PowerShell Parser解析打包脚本PASS；运行代码/构建/测试仅保留预设迁移注释中的FRUC；`git diff --check`通过 |

真实 RTX 日志 `logs/continuation-repair-20260910/fruc-final-live/engine.log`：Feature18 Create `result=0x1 handle=non-null seh=0`；DLSSG Create / Evaluate `result=0x1 seh=0`。暂停恢复生命周期 reason=7/outcome=1；设置重建 reason=9。实际诊断导出465条事件、overwritten=0，见同目录 `diagnostics.txt`。delivery实际覆盖NR/NVOF、原生4K正确性、文件/图片、NVENC H.264/HEVC及取消，不是性能或实体采集验收。

最终应用 SHA256：`E078E0B9348841E8A109E1160E704862F175B43F7C5E07C1ADB1D50F73432804`。
本机 NR DLL SHA256仍为 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，Authenticode Valid；无SDK/runtime/模型进入Git。

失败留档：第一轮新增轨迹编译因 `runSessionId` 声明顺序失败（`fruc-trace-build.log`），调整声明顺序后完整构建通过；一次由 Windows PowerShell `-File` 传数组导致测试参数绑定失败，尚未启动测试程序，改在当前PowerShell直接调用脚本后通过。前置纯FRUC构建与预设检查另存 `fruc-removal-build.log`、`fruc-removal-presets-first.result.json`，不替代最终证据。

## 下一步

本次授权软件收尾完成，等待用户用本机 `Veyra.cmd` 启动新EXE实测。原生4K高成本组合性能、真实采集音画同步、物理音频设备拔插、真实跨屏DPI和AMD NR不宣称通过；按用户决定不继续扩展、打包或发布。仅建立本地源码存档。
