# 性能诊断

在「设置 → 关于 → 性能诊断」选择 3、5 或 10 分钟，再正常播放或采集。遇到卡顿时按 Ctrl+Alt+M。可以提前结束，分析器会处理已经记录的部分。结束后在录制目录下的「诊断」看到报告文件夹和 ZIP。

报告包含输入和送显帧时间、设置变化、系统负载、手动标记和与当次会话相关的日志。屏幕实际刷新与端到端延迟需要单独测量。诊断包由 Veyra 保存到本机，分析器在本机运行。

- `report.html` 用于查看卡顿发生时间、帧间隔和软件能确认的原因。
- `SUMMARY.md` 和 `report.json` 提供可读摘要和结构化指标。
- `frames.csv` 和 `raw/` 保留逐帧数据和原始诊断信息。

随程序附带的 `veyra_frame_analyzer.exe` 可以重新分析一个诊断文件夹、ZIP、旧版逐帧 CSV 或播放日志。命令行示例使用当前目录里的文件。

```powershell
.\veyra_frame_analyzer.exe analyze .\Veyra诊断.zip --out .\analysis
.\veyra_frame_analyzer.exe compare .\baseline.zip .\candidate.zip --out .\comparison
```

做版本对照时，使用相同的画面、采集格式、补帧档位和显示同步设置。把开始播放时的准备阶段和稳定运行的帧时间分开查看。
