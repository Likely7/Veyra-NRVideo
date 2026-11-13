# 实时送帧时序与采集延迟续接

用户要求沿 Claude 线索继续排查并完成。基线 `1cd2ff7`，分支 `codex/live-deadline-latency-20261112`；使用现有施工树与构建，任务产物统一 `E:/项目/Veyra/{reports,logs,tests,tmp}/codex-live-deadline-20261112`。不合 main、不推送发布、不关机，不修改真实设置、运行组件或驱动。

已完成本地实现与验证；最终范围、实测、失败和限制见 [续接结果](LIVE_DEADLINE_LATENCY_2026-11-12.md)。PresentMon 权限缺口、重复控制台输出干扰和未采用的第二版捷径按该结果报告，不按初稿估算冒充通过。

## 证据与待验证项

Claude 的未完成报告在 `reports/claude-low-latency-audit-20261112/FINDINGS.md`。旧 120 fps 数据显示约 1 ms 定时唤醒误差，DLSS 原帧期限与下一帧 CPU 处理相撞。其 run1 中无效果回调到 Present 返回中位 1.56/1.64 ms，窗口/全屏 DXGI 推算显示等待约 4.7/1.9 ms，compositionMode=-1，不能据等待值直接断言显示模式。4X 段不完整，初始化曾约 11.9 秒停顿后恢复；保留原始日志和转储。

无补帧的 fixed readiness anchor 不是真实期限；旧 pacing-sample lateMs 随播放时间增长，不能拿它判定卡顿。ready=0 是尚未观察，不能参与 GPU-ready 时长统计。

## 施工与判据

1. 区分已就绪帧期限与 GPU/显示容量轮询，仅前者使用最多 1 ms 的可取消自旋尾段。采集事件与围栏事件仍可立即唤醒，停止/暂停/seek 可取消；设置仍由原外层循环应用。队列仍至多两批。
2. 在图处理子步骤之间，同一 owner 线程只运行到期的旧帧呈现。必须使用独立呈现 ring，禁止图重入、计时查询收集、重建或设置修改。XeSS、文件、导出与图片不进入该回调路径；reset 不让出。
3. 单独验证无补帧物理采集的 GPU 围栏交接：沿用 presenter 的队列 Wait 与资源租约，尝试省掉 CPU fence 完成再提交的往返。与旧行为 A/B，只有真实收益且无退化才保留。
4. 修正 ready-driven 诊断语义，使用 PresentMon 核实窗口/全屏模式，不按 1% low 或 DXGI 估计冒充光学延迟。
5. 构建、scheduler/timing/preview 合同、GPU 实际播放和窗口/全屏切换。实卡 T911 4K60、NR、DLSS/VFG，2X/4X及去重；补充 XeSS/垂直同步/VRR 设置与停止、重建、暂停恢复。单测试≤300秒、构建并发2。

## 对抗审查

精度收益不得用延后整组期限换取；不能让缺帧/慢 GPU 进入持续自旋。回调不允许共用正在录制的 command ring，不得释放仍在使用的租约。预提交后 Present 返回早不等于屏幕更早，需同时看 GPU 队列与显示事件。实卡内容若不稳定，不能沿用旧数字声称本轮通过。

平台依据：[GPU-side Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-wait)、[DXGI flip model](https://learn.microsoft.com/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)。最终实测结果与失败记录在后续执行节及本任务 FINAL.json。
