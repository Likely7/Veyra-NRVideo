# 采集链路整体排查接手

用户要求读取Claude已完成工作并完成后续。原会话8ec1c944-fff6-4d57-af33-3ea3ca27c6ab最后交接与源码b2eb77d已核对；从干净claude/capture-review-20261026新开codex/capture-review-handoff-20261026，checkpoint/pre-capture-review-handoff-20261026，原分支保留。产物统一E:/项目/Veyra/{用途}/codex-capture-review-handoff-20261026。

- 保留Claude的AA列表顺序、VRR/HDR事件查询及信号卡片刷新UI，复核实际启用的效果与计数，不能把源PTS/GPU提交/捕获估计当屏幕端到端延迟。
- 已完成135.6秒采集，其中60秒稳态；遵守用户“不要测很多轮，一轮就够”，不重跑实卡，不停止/争抢用户进程。离线用已有截图固定同一帧，完成real-frame真实NR/AA顺序对照，补HDR/合成边缘定向GPU契约。原节点截图不算AA前置证据，但可作为无效果输入；实际HDR截图转SDR后须明确不等价原HDR处理。
- 复核遗留问题：HDR截图路径提示与实际JXR不符、全屏恢复尺寸抖动、区域/格式日志。只修确认的最小接点；尤其不能通过改变Windows音量/AGC掩盖采集增益。声音+30dB依据原端点探针与软件映射核对。
- 审查事件查询是否仍在渲染路径等待；已有缓存不应因过期而等待慢驱动，冷启动遵守既有100ms上限。失败/超时/换屏使用已有保守颜色合同。
- 新改动做相应构建/测试，单Agent，-j2/<=900秒，单测试<=300秒。保留失败和原证据，写报告/WORKLOG/FINAL.json。主线68def10、软件UI默认独立分支保持；继承本任务不合main、不push、不Release、不封包，Lecram暂停。
