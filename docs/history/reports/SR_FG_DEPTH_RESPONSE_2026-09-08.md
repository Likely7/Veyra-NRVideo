# SR / FG 深度输入受控实验

Cycle76，基线 `33d248e`。结论仅限当前共享图、固定运行时与构造输入：SR 最终 RGBA8 输出未随深度变化；FG 对非恒定深度有轻微响应，前景深度带来的误差改善不足以支持默认引入推理模型。没有新增模型或改变产品默认处理、导出完整性检查。

## 实现与方法

新增 `tests/integration/SrFgDepthResponseTests.cpp` / `veyra_srfg_depth_tests`；`EnhanceGraph.h` 增加诊断借用资源访问器，无正常路径调用。SR 使用原有 `nrZeroDepth_`，FG 使用 `depthTex_`。直接写原纹理内容，避免仅改 Evaluate 指针的缓存歧义。R32F 上传前后恢复 COMMON；测试同步等待和像素回读只在诊断程序。

SR-only：640×360 → 1280×720，12 个真实帧；FG-only：640×360，12 个真实帧与 11 个有效中间帧。两组 NR 均关闭，NVOF 11 次、reset 1 次。逐帧比较 RGB，排除 alpha；PNG 仅保存末帧，不作为整个序列的替代。检查生成帧 subframe=1、PTS 为真实半帧时刻（100ns 整数舍入容差 1）。

对照为默认深度、显式相同常量（SR .5 / FG .9）、0、1、水平梯度、棋盘格、重新创建的默认重复、源红通道 +20 阳性对照。FG 另加逐帧对齐移动前景的 .1 / 背景 .9 深度及其反向。后两者是构造的相对前后顺序，不是校准的引擎投影 Z，也不是深度模型输出。

棋盘背景每源帧左移 2px，矩形前景右移 6px；半帧参考由相同场景函数按整数中间位移生成，不是相邻帧混合。全图与前景轮廓附近区域计算 RGB8 平均绝对误差（MAE）。此样本没有自然纹理、压缩、多层几何或复杂相机运动。

## 实测

最终运行 `logs/optimization-goal-20260908/srfg-final-b3590fc9f2b1478ab4e205897cb2ef2f/`，exit 0，31.988 秒，18 组全部通过，D3D12 ERROR/CORRUPTION 0。各 FG 组实际生成 11 帧、disabled 0、gtComplete 1。实际 Create / Evaluate 与计数见该目录 `stdout.log`。

SR 所有深度变化最终 RGB 均无变化；源阳性对照改变 13,966,737 个 RGB 通道样本、最大差 117。这里只检查最终 8bit 输出，未检查 SR 内部浮点差异，不能推广为 SR 永远忽略深度。

| FG 深度 | 相对默认变化的 RGB 通道数 / 最大差 | 全图 MAE8 | 轮廓区域 MAE8 |
|---|---:|---:|---:|
| 默认 / 相同常量 / 0 / 1 / 重复默认 | 0 / 0 | 0.237438 | 1.71047 |
| 梯度 | 35231 / 5 | 0.237758 | 1.70995 |
| 棋盘格 | 27314 / 4 | 0.237427 | 1.70925 |
| 前景近、背景远 | 23980 / 3 | 0.237343 | 1.70943 |
| 反向前后景 | 10689 / 3 | 0.237453 | 1.71070 |

FG 源阳性对照改变 2,777,056 个生成 RGB 通道样本、最大差 34；源改变后的 GT 也随之改变，因此该组 MAE 不用于深度收益比较。全部深度组真实帧 RGB 不变。

正确前后景相对默认的全图误差只减少约 0.000095 个 RGB8 单位，轮廓区域约 0.00104。没有建立自然素材、统计显著性、感知提升或每帧推理成本收益。**保持默认无深度模型**；有响应不等于有值得付出成本的改善。

## 命令与证据边界

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "$PWD" -Preset x64-release
out/build/x64-release/veyra_srfg_depth_tests.exe <唯一的被忽略日志目录>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7
```

测试外层实际使用隐藏进程、缓存 Process.Handle、275 秒 watchdog、WaitForExit 后 Refresh 读取退出码。构建日志 `logs/optimization-goal-20260908/build-cycle76-final.log` exit 0。之前的 16 组初测与 18 组 GT 测试保留，最终新增 PTS 检查后完整复跑。

本专项门禁与独立复核已通过，见 [独立复核](REVIEW_SR_FG_DEPTH_RESPONSE_2026-09-08.md)。初次复核发现部分深度组缺帧仍可能通过的 P2，现已强制所有 FG 组逐帧恰好一个有效生成帧、总计 11、GT 样本完整，以及 FG 深度组真实帧不变；最终独立 18 组复跑数值一致。Phase 7 / Goal 仍进行中；不覆盖自然素材画质、实卡体验、长期稳定或分发许可。
