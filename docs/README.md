# Veyra 开发文档

日常只从这些入口开始：

当前独立分支已完成续接收尾、未提交：[垂直同步等待上限3 ms](VSYNC_JITTER_WAIT_2026-11-02.md)。

最新并入本地main（未发布）：[11-01合并记录](MAIN_INTEGRATION_2026-11-01.md)、[补帧返工与VFG自动调控](FG_NO_LATENCY_GROWTH_2026-11-01.md)。

最近并入本地 main（未发布）：[补帧接近刷新率的间歇掉帧](FG_60HZ_PACING_2026-10-31.md)、[英伟达低延迟三档](NVIDIA_LOW_LATENCY_2026-10-28.md)、[RX 7000 NR](AMD_RDNA3_NR_PLAN_2026-10-29.md)、[屏幕捕获与采集音频](CAPTURE_FPS_AUDIO_REPORT_2026-10-27.md)。

1. [当前项目状态](CURRENT_STATUS.md)：正式版、活动工作、暂停项、现场问题及验收边界。
2. [开发与验证](DEVELOPMENT.md)：唯一施工树、构建、回归、运行供体和路径规则。
3. [当前工作日志](WORKLOG.md)：正在进行的实际结果与续接信息。
4. [项目整理执行记录](PROJECT_MAINTENANCE_EXECUTION_2026-10-27.md)：本轮清理与结构整理。
5. [分支与工作树归档](BRANCH_ARCHIVE.md)：保留项、已退休分支tip与恢复方法。
6. [2.1.1用户更新说明](RELEASE_NOTES_2.1.1.md) 与 [对应源码/构建](BUILD_2.1.1.md)。
7. [界面代码与职责边界](ARCHITECTURE.md)：桥接模块与QML组件从哪里找。
8. [当前双重超分施工](DUAL_SR_PLAN_2026-10-28.md)：基础SR→降低AI结果→第二次SR；覆盖同日早期单层方案与4K限制。随后新增[超采样选项](SR_SUPERSAMPLE_2026-10-28.md)：单次RTX视频超分到更高尺寸后精确缩回。

历史方案、排查和发行过程集中在[历史索引](history/INDEX.md)。旧路径对照[PATH_MAP.json](history/PATH_MAP.json)；这些历史文件中的“当前/未合并/未发布”按原日期解释。
