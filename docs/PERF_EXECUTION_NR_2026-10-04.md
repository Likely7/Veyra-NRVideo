# NR / 增强管线优化执行记录（2026-10-04）

## 当前授权与基线

用户要求按 `PERF_PLAN_NR_2026-10-03.md` 完成优化，先对齐项目文档、存档，再记录当前版本的优化前实测；每个节点单独存档，负优化回退并记录，有效优化保留实际数据。目标模式已开启。只在 `E:/项目/Veyra/worktrees/perf-nr-20261004`、`codex/perf-nr-20261004` 施工；起点 `8cdc612120cbf23ba116a33c3cb0a53e2043f718`。本轮不合并、推送、发布或关机，不派子 Agent。

原方案从 Claude 的 `claude/rtss-compat-20261003` 工作树读取，原件及 SHA256 存入本轮 archive；不修改 Claude 工作树。方案当时的 2.0.1 基线、RTSS 未合并和启动优化冲突已过时：这些已整合并发布到 2.0.2/2.0.3。当前起点另含本地 RTSS 重启循环、倍速/字幕/UI热路径/专业布局/全屏恢复修复，优化不得丢失这些修复。公开版本仍是 2.0.3，严重后台掉帧根因未确认，HDR/Dolby PR #13/#14 暂缓，不能将它们计入本轮完成。

## 执行与保留规则

- 2026-10-05用户续推并明确禁止压力测试：剩余节点只做正常负载匹配对照和必要功能/画质回归；不另起竞争程序、不人为满载或制造显存压力。原3a/3b压力要求被覆盖，历史数据保留；自动档在正常不同素材/配置下验收，压力边界不作已验承诺。

- 所有新增 build/tests/logs/tmp/archives/test-packages 均放 `E:/项目/Veyra/` 对应 `perf-nr-20261004` 子目录；固定素材放 `tests/perf-matrix/media`，只创建本轮拥有的文件。
- 文档对齐已提交 `37bc0c090918566f7ebebc9f5edb24ba60c5f5a3`，标签 `checkpoint/perf-nr-initial-docs-20261004`，完整 bundle 已验证。基线产品源码为起点；测试工具改动独立记录。
- 每个实验/节点使用 before/candidate/accepted 或 rejected 标签、独立增量 bundle、patch、构建身份、设置、环境、原始日志、结果 JSON 和结论文档。负优化用明确的 revert 提交；不重写历史，不删失败证据。未达到收益或正确性门槛的候选不进保留实现。
- A 为当前源码的全新生产构建和独立 staging；B 为节点候选；B-off 在同一候选关闭该节点。固定驱动/运行库/素材/尺寸/设置，三次运行报告中位数与范围。GPU任务串行，构建与计时测试不重叠，其他用户应用不强行关闭；记录负载干扰并排除受干扰样本。
- 软件 Present/提交时间、GPU timestamp、CPU Timer、物理显示事件分别报告。没有 ETW/高速相机证据时不宣称实屏帧率或端到端延迟。
- 单测试进程最多300秒、单构建最多900秒；E2 的10分钟观察拆成有清楚边界的测试运行，报告持续运行与累计运行区别，不将两次运行冒充一次10分钟长稳。
- 需要画质确认的节点先生成可审查的同源数值与图片，用户确认前不标画质验收通过。用户尚未提供的主机30/40帧实卡信号不能用合成素材冒充；合成节奏测试与真实来源分开报告。
- 运行库/SDK/权重原字节与既有身份保持，不进源码Git；AMD/其他RTX型号未验边界保持。技术范围仅本方案必要 graph/NGX/device/engine/presenter/export调度/性能设置/QML/diagnostic/shader/CMake及定向测试接点，不重写解码/音频/串流协议/旧Win32。

## 节点账本

“pending”不是完成；每行必须有实测或可复现反例，再决定保留、回退、既有实现满足、或记录外部验收缺口。

| 节点 | 内容 | 状态 | 证据 / 下一步 |
|---|---|---|---|
| D0 | 项目文档对齐与存档 | accepted | 37bc0c0 / initial-docs完整bundle verify，桌面/main保持 |
| A0 | 当前版本基线及矩阵驱动 | in-progress | A完整包封存；M1五组各三轮15/15通过，PERF_BASELINE_NR；其余素材/切换基线在各节点前补齐 |
| E1 | Feature18动态输入尺寸 | completed-conditional | 两运行库8组合API/区域/恢复正常，但缩小后与独立小实例不同；PERF_E1_DYNAMIC_SIZE，3b优先实际尺寸实例 |
| E2 | 并行Evaluate/CreateFeature | completed-conditional | 单Core/adapter，三次240s累计12min及150次创建安全通过；间隔110–116ms，不能据此宣称2b无缝 |
| E3 | 真实来源精确重复比例 | completed-screen-awaiting-card | 真实WGC三阶段各60秒通过，静止99.932%/移动内容0.540%/窗口位置99.937%；PERF_E3_DUPLICATE_SOURCE；实卡待来源确认 |
| 1a | 精确重复帧整链复用 | rejected-unchanged-output | 自然同像素300帧×五组×三轮，A-A噪声0；NR/SR复用299帧不同；PERF_1A_DUPLICATE_REUSE；无生产改动 |
| 5a | 黑边检测与有效区域处理 | pending | 防暗场误裁，先测准确性与重建收益 |
| 3a | 进程GPU调度优先级 | retained-pending-R0 | 18组交错实际三档；60Hz实时P99改善6.83%，High无稳定收益，普通默认；两绘制模式40次Set/Get及重启通过；PERF_3A_GPU_PRIORITY |
| 2a | 跨重建保留NGX核心 | retained-pending-R0 | v2三轮A/B/B-off完整输出0差异；暖创建减少75.83/71.23/64.58/72.04%；真实UI及SDK拒绝路径通过；导出待R0 |
| 2c | 最近配置实例缓存 | retained-single-NR-pending-R0 | 单NR actual create363.851→3.602ms及像素/压力通过；SR与SRNR每轮21/50输出不一致，拒绝扩大；PERF_2C_RECENT_CACHE / PERF_2C_SR_CACHE_REJECTION |
| 2d | 空闲预热 | retained-pending-R0 | Qt27组A/B/off首次成功Present1942→100 / 2053→107 / 1733→92ms；54完整输出0差异、5生命周期通过；PERF_2D_PREWARM |
| 2b | 后台建图与帧边界切换 | rejected-continuity | 6轮新鲜Evaluate输出实际Present、300创建安全，但P99 32.044→74.189ms、>33.333ms次数3→55，未满足连续性；PERF_2B_BACKGROUND_CREATE |
| 3b | NR自动内部尺寸 | pending | 取决于E1、真实预算/画质证据 |
| 1b | 内容帧率预算 | rejected-prerequisite | 1a画面不成立，完整求值仍按transport次数；实测成本构造预算反例，PERF_1B_CONTENT_BUDGET；无生产改动 |
| 1c | 重复节奏预测 | rejected | 三种节奏单像素瞬态反例均漏第49帧且后续未检测；PERF_1C_CADENCE_REJECTION；无产品代码需回退 |
| 3c | 队列分工/呈现优先级 | retained-limited-pending-R0 | 普通负载18组，增强21.386→19.477ms（2X）/23.330→21.542ms（3X）；准入仅已测5070双NR+SR4K+DLSS2/3文件图；9组往返1278全图/PTS一致/debug0、真实Qt FSR/XeSS/VFG/调色/时域回退通过；源跳过0但长帧仍在，HIGH拒绝默认；PERF_3C_GRAPH_NORMAL_2026-10-05 |
| 5b | 文件/导出跨帧并行 | retained-export-rejected-prefetch | 无FG NVENC两帧/事件默认保留，75文件8100图一致/15边界/18默认-off/5worker；NVOF准备器24普通导出输出一致但整次慢1.41–5.25%、多232–867MiB，独立trace实际依赖区间重叠约1ms，拒绝并回退；不扩文件/采集/串流，PERF_5B_CROSS_FRAME / PERF_5B_NVOF_PREFETCH |
| 4a | 多NR整链低分辨率调度 | rejected-benefit-to-risk | 20组800图控制/debug通过但多层最大像素差81/255；12组普通播放整体仅降1.09%/0.62%，呈现P99无改善，拒绝替换产品路径并回退；完整PNG/HTML/候选源码保留，PERF_4A_LOW_CHAIN_2026-10-05 |
| 4b | 先粗后细组合 | pending | 核对现有逐层尺寸；不新增强制预设 |
| 5c | 暂停不重跑/残差重合成 | retained-pending-R0 | 残差7.461→0.766ms约89.74%；真实Qt自身暂停GPU三轮2.237→0.011%（off2.237%）；48完整呈现图像一致、四FG暂停/恢复/seek通过；PERF_5C_PAUSED_NR |
| UI | 性能设置及状态 | pending | 只暴露保留实现，统一持久化/能力/翻译 |
| R0 | 全产品回归与最终候选 | pending | 合同/Xbox/Qt/字幕/导出/兼容及源码存档；正常负载A3控制/调度约2.95s断档、B3跳帧，A/B素材frame303/304约68–72ms图提交；根因待查、不宣称丝滑 |

## 当前可续接状态

已完成项目文档对齐、独立分支和开工存档。`py -3.11 -B scripts/perf/nr-build.py A build-A-v1 veyra_qml_ui veyra_nr_video_quality_probe veyra_export_probe` 全新构建476步、exit0；日志 `E:/项目/Veyra/logs/perf-nr-20261004/build-A-v1.log`。运行库沿用已验收完整NVIDIA包原字节，EXE/QML和各profile独立。

基线驱动 `nr-series.py A` 已完成M1/S1、S2-720、S3、S4、S5-existing各三轮、稳态50秒；每个产品进程和驱动有270/280/299秒上限。1280×800窗口、真实GPU timestamp及CPU提交统计，不将16ms UI Timer当渲染FPS。现有UI没有精确50%档位，S2明确为1280×720（1080p输入维度66.7%、像素44.4%）。多层先粗后细已有逐层尺寸参数，S5核对现有行为，不计为新收益。

2a核心复用、2c单NR缓存、2d空闲预热、5c暂停复用已保留，3a提供用户可选进程优先级且普通默认，均待R0最终产品回归。1a/1c、依赖它们的1b、2b后台创建、SR缓存扩展及4a低尺寸整链候选均拒绝，证据保留。3c已保留限定准入的自动COMPUTE，18组普通负载约8%增强区间收益、9组迁移完整画面及真实三provider切换回归通过；其他组合DIRECT，HIGH不默认。5b保留无FG NVENC两帧/事件，另一个NVOF准备器完整导出负收益，回退且不扩入文件播放。5a/3b/4b/UI/R0仍待，不能将局部耗时或暂停GPU改善冒充整个方案收益。
