# 已知 4K 画面的超分重建对照

Cycle77，基线 `3aa9c77`。本轮只新增独立诊断与 CMake target，不改变产品默认配置或导出完整性检查。**当前 DLSS SR 在两个构造样本里降低了空间误差，但提高了运动对齐后的误差变化和最大局部误差，不能据此声称全面改善画质。**

## 输入与比较合同

`tests/integration/SrReconstructionTests.cpp` 生成逐帧精确的 3840×2160 RGB8 参考；先解码 sRGB 到线性光，2×2 box 平均，再编码为 1920×1080 RGB8 输入。相同输入依次走共享 EnhanceGraph 的普通线性双线性缩放、DLSS SR、重新创建的 DLSS SR 重复组。所有组输出均为真实 3840×2160 资源。24 个被评分源帧不运行 NR/FG，NVOF 实际执行；共享图初始化仍创建 NR/FG，并有一次 frameId=0 的 FG 预热 Evaluate，它不参与本次评分。

每组 24 帧、60fps PTS，仅首帧 reset。保存每组最后输出、最后源输入及 4K 参考 PNG；所有 24 帧都计算误差，不是以末帧替代序列。SR 重复组逐帧完整像素 hash 必须相同。每场景独立进程设 275 秒 watchdog。

- 场景0：四个频率的棋盘纹理（方格边长 4/8/16/32 输出像素），整体每帧左移 4px。
- 场景1：平滑周期背景每帧左移 4px，带细纹理前景每帧右移 8px，产生覆盖与显露。
- 重建 MAE / PSNR / 最大误差均为 RGB8 编码值度量，包含起始帧；不含 alpha。
- temporalErrorMae8：计算当前输出误差与前一帧对应场景点的输出误差之差的绝对均值。对应关系来自构造运动，不使用被测 NVOF 来评价自己。排除出界与前后景身份发生变化的点。它是本样本的时间误差指标，不是经过主观校准的“闪烁评分”。
- 两个场景的运动均为整输入像素平移，尤其有利于普通空间缩放的时间稳定性；不代表自然片源、亚像素运动、压缩或所有遮挡。

## 实测结果

| 场景 / 路径 | RGB8 MAE ↓ | PSNR dB ↑ | 最大误差 ↓ | 时间误差 MAE8 ↓ |
|---|---:|---:|---:|---:|
| 多频棋盘 / 普通缩放 | 15.9795 | 19.0963 | 84 | 0.007202 |
| 多频棋盘 / DLSS SR | 13.8156 | 20.3638 | 140 | 8.36538 |
| 移动前景 / 普通缩放 | 1.92695 | 30.5818 | 64 | 0.006848 |
| 移动前景 / DLSS SR | 1.76538 | 30.8184 | 111 | 0.309143 |

SR 重复组像素逐帧精确重现。每组 SR Evaluate 24（空间组0）、NVOF23、reset1、NVOF failures0、D3D12 ERROR/CORRUPTION0。实际运行在本机 RTX，所有组 exit0。查看了非黑且有结构的移动前景 SR PNG。

- 场景0：`logs/optimization-goal-20260908/sr-recon-final-0-b240e19c658c43d2bc06b06c4f4442ea/`，17.030秒。
- 场景1：`logs/optimization-goal-20260908/sr-recon-final-1-42eb27fb292f4be0814b1e6d2c42de08/`，19.956秒。
- 每目录 `stdout.log` 包含实际 Create/Evaluate 与 `SR_RECON` 汇总；`result.json` 记录退出码与耗时。

SR stage 使用现有 GPU timestamp，跳过前4帧后各20样本：场景0首轮 mean3.007ms/P95 3.285ms，重复轮3.036/3.425ms；场景1首轮2.981/3.387ms，重复轮3.005/3.253ms。这只是 SR stage，排除 NVOF/解码/显示等；诊断有逐帧回读与等待，不能换算成正常播放器 FPS 或端到端延迟，20样本也不是长期性能结论。

## 决策与未完成事项

保持当前可选 SR，不强制替换用户设置，不把 4K 输出标签当作画质保证。不加默认深度模型或叠加另一个超分器。这个实验没有分离光流误差与 SR 自身历史处理的影响；下一步应检查已知运动指导的对照及自然素材，再决定具体改法，不能直接归罪于单个参数。

RTX Video SDK 在本地项目和定向 Downloads 文件名搜索中未找到，尚未接入或测得收益。未完成自然压缩素材、主观画质及 SR/NR 联合质量验收。Phase7 / Goal 仍进行中。

实际命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "$PWD" -Preset x64-release
out/build/x64-release/veyra_sr_reconstruction_tests.exe <唯一忽略日志目录> 0
out/build/x64-release/veyra_sr_reconstruction_tests.exe <另一个唯一忽略日志目录> 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7
```

构建 `logs/optimization-goal-20260908/build-cycle77-final.log` exit0。[独立只读复核](REVIEW_SR_RECONSTRUCTION_2026-09-08.md)与门禁已通过，仅覆盖本专项。
