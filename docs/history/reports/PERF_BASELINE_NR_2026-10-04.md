# 优化前基线 A（2026-10-04，持续补齐）

## 固定身份与方法

产品源码 `8cdc612120cbf23ba116a33c3cb0a53e2043f718`，含尚未公开的RTSS重启、字幕、自定义倍速、UI热路径和专业布局/全屏恢复修复。全新构建时文档提交 `37bc0c090918566f7ebebc9f5edb24ba60c5f5a3`；未改产品C++/QML。RTX5070，实际驱动 **616.56**，与用户另一台机器的616.92不混同。窗口1280×800、GPU界面；NR时域抗闪关闭，低延迟关闭，各轮设置一致。

- EXE SHA256：`b7081f0e6045bc791105bc80de364247e169e3522891ef50cf9f62bbbfc10eb4`。
- NR默认Lecram310.8.3.0、HashMismatch社区原字节：`f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc`。
- M1 GTA6 1920×1080/30、60秒源：`03b2a0dc7f682a2c70ae65809d4db69c725a95f49e60116b6e9da2e2ae4610c8`。
- 构建：`nr-build.py A build-A-v1 veyra_qml_ui veyra_nr_video_quality_probe veyra_export_probe`，476步exit0。
- 测量：`nr-series.py A`，五组各三轮真实产品正常退出/PASS，50秒稳态请求，统计位置10–48秒的滚动日志观测。每组报告“每轮滚动观测中位数”的三轮中位数及范围，**不是全部帧的总体P95**。软件提交/GPU timestamp/CPU Timer都不是物理显示刷新或端到端延迟。

## M1 三轮基线

单位ms；括号为三轮最小–最大。这里记录现有设置差异，**没有本轮优化收益**。

| 设置 | enhancementProcessingMs | 日志gpuNrP95Ms | 日志gpuSrP95Ms | 日志gpuResidualP95Ms |
|---|---:|---:|---:|---:|
| S1 单NR，1920×1080 | 7.7145（7.6845–7.723） | 7.0645（7.036–7.086） | 0 | 0.057 |
| S2-720 单NR，1280×720 | 4.6975（4.683–4.707） | 4.246（4.1805–4.269） | 0 | 0.057 |
| S3 NR实时1080+DLSS SR 4K | 11.3565（11.347–11.377） | 7.156（7.135–7.281） | 3.634（3.629–3.652） | 0.536 |
| S4 两NR实时1080+DLSS SR 4K+DLSS FG2X | 21.192（21.173–21.204） | 7.202（7.188–7.212） | 3.692（3.667–3.783） | 0.895 |
| S5 现有三层480/720/原生组合 | 14.372（13.59–14.377） | 7.233（6.626–7.257） | 0 | 0.262 |

现有gpuNrP95字段不能直接当多层NR总耗时；后续按独立层计时与整链ready时间判断。全部previewSkipped滚动计数中位0；S4 expiredGenerated中位2，不能声称补帧没有过期。S5第二轮约5.4%较快，保留全部原始数据和频率记录，后续小于该量级的S5收益不能凭单次测试确认。不同设置GPU自动频率不同（约2370–2880MHz），不强行锁频或修改用户驱动。

原UI没有精确50%档位，720p为输入维度66.7%、像素44.4%。各层不同尺寸已经实现，S5不计作新增功能。

## 原始证据与封存

`E:/项目/Veyra/logs/perf-nr-20261004/baseline-M1-summary.json` 保存所有15轮路径和SHA；各目录含player.log、console.log、meters.json、timing.csv、result.json和driver.log，全部通过退出/效果生效检查。

完整A目录：`E:/项目/Veyra/test-packages/perf-nr-20261004/Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable`，1542载荷，独立manifest核对SHA；新EXE/QML独立、依赖仅硬链接不可变原字节。所有QML与起点8cdc逐文件文本一致，没有测试Loader。该目录与带测试Loader的`tests/perf-nr-20261004/app-A`分开。

当前M1基线已完成；4K、黑边、节奏、8K导出、切换/暂停基线会在对应节点产品改动前补齐。真实主机、其他GPU、物理显示事件与外部Magpie参照尚未测；不得将合成素材或当前M1结论冒充这些验收。
