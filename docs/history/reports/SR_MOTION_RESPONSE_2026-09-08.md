# SR 运动指导受控对照

Cycle78，基线 `8fc00d4`。沿用 [4K 重建实验](SR_RECONSTRUCTION_2026-09-08.md) 的两个场景和全部输入、颜色、深度、SR 参数、零 jitter、历史 reset，单独替换 SR 运动输入。**准确的构造运动改善了这两个样本的 SR 误差，但仍未消除与普通空间缩放的时间稳定性差距；全部清零运动不是正确修复。**

## 诊断边界

`EnhanceGraphDesc.srMotionProbe` 为默认空的诊断借用资源，仅测试赋值。验证 SR 已启用、资源属于相同 D3D12 device、working extent、RG16F、单 mip / slice / sample、可读；错误尺寸、格式和关闭 SR 三种负对照必须拒绝，再成功初始化正常组。正常产品入口不设置它。

资源只替代 SR 的 Evaluate motion，NR/FG 路径未改。测试在源帧提交前同步上传并恢复 NON_PIXEL_SHADER_RESOURCE，持有到 graph drain/shutdown；这些上传、等待和像素回读不进入正常播放/导出。构造运动为 current→previous、单位为输出像素：背景 +4x，前景 -8x；RG16F 对应 half 位模式 0x4400 / 0xc800，y=0。首帧、出界和前后景身份变化位置写0。零向量不保证模型历史被安全排除。

各模式：0普通缩放；1现有 SR+估算运动；2重复模式1；3准确构造运动；4零运动；5把有效运动方向反转；6重复模式3；7现有估算运动但关闭 `validateMotion`，保留原有 NVOF cost 规则，不是无条件信任所有向量。

每组24帧真实1920×1080→3840×2160，SR24（空间组0）、NVOF23、reset1；两类重复组逐帧完整像素 hash 精确一致。24个评分帧不运行NR/FG，但共享图初始化仍创建这些 feature，并执行一次不参与评分的FG预热。

## 指标

下面只统计 frame4–23，跳过前4帧；时间误差的首个对应仍来自 frame3。称为跳过启动帧的观察窗口，**不证明模型已经收敛或达到稳态**。完整24帧的原始指标仍打印为 `SR_RECON`。单位和已知运动对齐、遮挡排除方法与上一实验相同。

| 场景 / SR运动 | MAE8 ↓ | 时间误差 MAE8 ↓ |
|---|---:|---:|
| 棋盘 / 普通缩放 | 15.9796 | 0.007211 |
| 棋盘 / 现有估算 | 12.8635 | 7.77054 |
| 棋盘 / 准确构造 | 12.7557 | 4.39519 |
| 棋盘 / 零运动 | 36.0011 | 3.47346 |
| 棋盘 / 反向运动 | 39.8038 | 8.57094 |
| 棋盘 / 关闭过滤 | 12.8615 | 7.13306 |
| 移动前景 / 普通缩放 | 1.92688 | 0.006755 |
| 移动前景 / 现有估算 | 1.73715 | 0.227132 |
| 移动前景 / 准确构造 | 1.56788 | 0.144580 |
| 移动前景 / 零运动 | 6.06699 | 0.752772 |
| 移动前景 / 反向运动 | 3.04345 | 0.998255 |
| 移动前景 / 关闭过滤 | 1.71319 | 0.221763 |

准确运动的改善证明当前 SR 对运动有实际响应，也说明估算运动是影响因素之一。但构造场景具有先验真实对应关系，普通视频没有这些信息；不能将该诊断资源接入正常播放伪装成“准确运动已实现”。剩余时间误差也不能直接归结为某个未经单独验证的参数。

棋盘场景中零运动虽然减小了时间误差，却显著增加空间误差，说明只优化单一时间指标会选出错误方案。周期纹理、整数位移有利于空间缩放；结论不推广为自然视频、亚像素运动或主观画质排名。不据此默认新增深度模型、切换 SR 或声称现有运动过滤全面更好。

## 命令与记录

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "$PWD" -Preset x64-release
out/build/x64-release/veyra_sr_reconstruction_tests.exe <唯一忽略目录> 0
out/build/x64-release/veyra_sr_reconstruction_tests.exe <另一个唯一忽略目录> 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7
```

构建日志 `logs/optimization-goal-20260908/build-cycle78-legacy.log` exit0。各进程隐藏启动并采用275秒 watchdog。

初始7组/场景：`sr-motion-0-022d569e0cf84219ae6e3961b0731294`44.835秒、`sr-motion-1-673362e8dc004b22b0b9f479e3931bfa`49.665秒；增加资源负对照后场景1 `sr-motion-contract-0d6e2bc37bbb4b169b3860fe2312ecdc`49.659秒，均exit0/debug0且指标复现。路径前缀均为 `logs/optimization-goal-20260908/`。负对照的3条预期 graph contract error 日志不代表GPU执行失败。

最终8组/场景：`sr-motion-final-0-978ea39fa951411f9a3f9d82f2239c41`48.652秒、`sr-motion-final-1-f755274a89804bd19d4da245b3a712b2`55.509秒，均exit0。关闭过滤在这两个样本的评分略好于当前过滤；没有形成默认移除过滤的充分证据，需要自然素材和错误运动场景交叉检查。准确运动相比两者仍有改善空间。

门禁与独立复核待完成。导出完整性检查不变；Phase7/Goal仍进行中，下一项是检验过滤对自然素材的效果并按证据选择具体修复。
