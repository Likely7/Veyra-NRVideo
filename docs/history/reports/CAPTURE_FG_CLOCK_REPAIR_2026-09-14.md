# 采集卡内部 FG 时间线修复（2026-09-14）

## 基线与范围

- 修复分支：`codex/capture-fg-clock-repair-20260914`
- 修复前回退 checkpoint：`d9a94eb`（`chore: checkpoint before capture FG clock repair`）
- 本轮只修实时采集/测试回放的 FG 时间锚定、诊断和状态文案；不修改 NVIDIA runtime、SDK、FFmpeg 或用户已有的采集音频设备选择改动。

## 根因

物理采集开启内部 FG 时，旧路径把源 PTS 在会话开始时一次性映射到主机时间。采集卡实际回调约 59.94fps，而协商/名义节奏约 60fps，约 0.1% 的差异会在几分钟内累积为数百毫秒的截止时间误差。程序随后把已经来不及的 FG 候选全部拒绝，UI 将持续低于目标输出误报为“过载”。

反馈日志中同时出现 `callbackFps=59.94`、`gpuReadyP95Ms` 约 2.9ms、`gpuFgBatchP95Ms` 约 5.7ms，但 `remainingDeadlineMs` 已为约 -289ms；这不是 NR/SR 或 GPU OOM。

## 修复内容

1. `PresentationScheduler` 增加 `resetPair()`，明确区分连续文件时间线和 live 输入对时间线。
2. 物理采集、PS5 和 file-backed live replay 在 FG 路径中按每个新 A/B 对重新锚定到输入到达时间。源 PTS 仍用于历史、运动和 FG 插值；当前 A/B 对内部仍按 PTS 间隔保持生成帧与真实帧的节奏。
3. DLSS FG 在 `Evaluate` 前重新锚定，准入预算不会因为旧时间线漂移而被伪造放宽；XeSS/live FG 在呈现前也使用同一对帧锚点。
4. 实时状态从“过载”改为“补帧受限”，避免把软件截止时间不足冒充物理 GPU 过载。
5. 合同测试新增 59.94/60Hz 五分钟漂移模拟：单一锚点会落后约 300ms，逐对锚定的 FG 截止时间保持在当前输入对内。

## 实际验证

构建命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player' -Preset x64-release -BuildDirectory 'C:\Users\123\Desktop\Veyra DLSS Video Player\out\build\capture-fg-clock-repair' -FfmpegRoot 'C:\veyra-deps\ffmpeg-ps5-dav1d-installed'
```

- 首次完整构建：216/216，exit 0。
- 最终增量重建：20/20，exit 0。
- `veyra_repair_contract_tests.exe`：109 checks，0 failures。
- `veyra_live_timing_tests.exe`：全部 PASS。
- `veyra_presentation_worker_tests.exe`：全部 PASS。
- `veyra_fg_admission_tests.exe dlss 2/3/4 ...`：三种倍率均 exit 0；DLSS Create/Evaluate、40 原帧、NR 40 次、D3D12 debug errors=0。
- `veyra_live_presentation_tests.exe ... --overload`：资源边界、准入统计、关闭释放均 PASS；日志记录 `timeline=capture-pair`。
- `veyra_live_presentation_tests.exe ... --source-gap`：源等待、恢复、EOF 排空、跨 epoch 全部 PASS，`EOS frames=3600 ready=3600 presented=3600 cancelled=0`。
- `git diff --check`：通过。

## 未完成的实卡边界

本轮没有占用用户采集卡，也没有在用户的 RTX 4070 Ti 与 RTX 5070 上做同素材 A/B；没有测量显示器 scanout、温度、功耗或实际物理屏幕延迟。因此不能把自动化回放通过写成用户实卡已经验收。下一步应使用修复后的程序，在原始采集格式、NR/SR 关闭、内部 DLSS FG 开启的相同条件下重新采集 30–60 秒日志，确认 `timeline=capture-pair` 且截止时间不再单调跌入数百毫秒负值。
