# Veyra 2.0.4 优化数据报告 / Performance report

本报告汇总已执行的正常负载匹配对照。RTX5070、驱动616.56，NR性能样本采用Lecram 310.8.3.0；三轮中位数或文中明确的统计区间。数据来自逐节点封存A/B/B-off和最终33组普通播放，无额外竞争负载。它们不是2.0.4对所有显卡、素材或物理屏幕延迟的承诺。

| 实测场景 | 优化前 | 优化后 | 变化与适用范围 |
|---|---:|---:|---|
| NR开启的暖重建（核心复用） | 1569.510 ms | 379.349 ms | 创建耗时减少75.83%；排除首次冷创建，同设备/运行库准入 |
| 最近单NR配置再次开启（真实UI） | 363.8505 ms | 3.6020 ms | createMs减少99.01%；仅严格命中的单NR缓存，含Presenter创建，不含显示端停顿 |
| 暂停单NR、只改残差参数 | 7.46140 ms | 0.76570 ms | 单次处理减少89.74%；CPU处理含GPU完成等待，300次编辑NR求值由300次降至2次，模型变化仍完整重算 |
| 暂停时本进程GPU最大引擎利用率 | 2.237185% | 0.010999% | 相对减少99.51%；三轮、每轮24个稳定样本，绝非整卡GPU利用率保证 |
| 双NR1080上限＋SR4K＋DLSS2X | 19.481 ms | 18.795 ms | 最终候选增强区间减少3.52%；同候选DIRECT关闭对照为19.299 ms，队列分工贡献2.61% |

最后一组成功软件Present P99为17.236→17.233 ms，几乎不变。单层原生NR为6.129→6.159 ms，也没有稳定提速。模型推理未改；主要收益来自避免重复初始化、严格安全的缓存、暂停闲置及限定队列分工。首次冷创建仍约1.99–2.50秒；保留核心/实例可能保留显存，不能称为零成本。

暂停缓存只在已验证配置准入；SR、抗闪烁、双层NR等不满足时完整处理。近期实例缓存未扩到SR/NR+SR，因为对应输出曾不一致。预热保留为可选、最终默认关闭，1942→100 ms等预热实验不作为默认首启收益。可选Auto NR/先粗后细通过降低内部尺寸改变画面，不能并入同画质默认提速数据。

后台Create候选曾将Present P99从32.044 ms恶化到74.189 ms，超33.333 ms事件3→55，已经拒绝；NVOF预取整次导出慢1.41–5.25%，也已撤回。原生多层低分辨率整体收益约0.62–1.09%却有画面差异，未带入产品。完整8K导出曾改善3.91%，未达到10%目标，不能只取处理区间宣称整次达标。

采集现场日志源PTS仍约60Hz，原NR＋4K SR＋3X组合实际消费约55–57fps；本次修复节奏判断与掉帧历史恢复，不承诺此负载达到60fps。普通矩阵33/33、sourceSkipped=0，不能证明所有后台掉帧根因已消除；约110ms提交长尾及历史2.8秒断档仍有未解边界。未测物理屏幕/端到端延迟。

证据：[核心复用](PERF_2A_CORE_REUSE_2026-10-04.md)、[单NR缓存](PERF_2C_RECENT_CACHE_2026-10-04.md)、[暂停复用](PERF_5C_PAUSED_NR_2026-10-04.md)、[最终普通播放与回归](PERF_R0_ACCEPTANCE_2026-10-05.md)、[完整执行账本](PERF_EXECUTION_NR_2026-10-04.md)。逐组日志根目录为E:/项目/Veyra/logs/perf-nr-20261004/，报告不把历史节点EXE冒充最终2.0.4新跑的计时。

## English

Matched ordinary-load measurements used an RTX5070, driver616.56 and Lecram310.8.3.0. Three-run medians: warm NR creation 1569.510→379.349ms (75.83% lower); actual-UI reactivation of a strictly matching single-NR cache 363.8505→3.6020ms (99.01% lower); paused single-NR residual editing 7.46140→0.76570ms (89.74% lower); this process's paused GPU-engine utilization 2.237185→0.010999% (99.51% lower). These measure different operations and cannot be combined into a playback-FPS or display-latency claim.

The final dual-NR1080＋SR4K＋DLSS2X candidate reduced the enhancement interval from19.481 to18.795ms (3.52%). Successful software-Present P99 stayed17.236→17.233ms. Native single-NR timing remained about6.1ms. Cold startup did not improve; prewarming defaults off. Cache retention can retain VRAM and has strict eligibility/invalidation rules. Optional reduced-resolution modes alter the image and are reported separately.

Rejected background creation, NVOF prefetch and low-resolution whole-chain candidates were removed. Capture-card, Xbox/AMD hardware, other RTX models, physical display latency and background long-session behavior are not established by these local results. See the linked original reports for exact configurations, failed candidates and measurement limits.
