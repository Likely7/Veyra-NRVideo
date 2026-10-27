# 2026-09-12 修复版：明天验收

代码与本机自动回归已完成，未推送、未发布。PS5 基础连接和手柄此前已由用户验证；本轮新增硬解与停帧恢复仍需真实 PS5 验收。原始偶发冻结未重现，不能宣称已经证明其根因或彻底消除。

## 打开哪个版本

使用桌面 `Veyra PS5 测试版.lnk`，指向本项目 `out/remoteplay/product-repair/veyra.exe`。快捷方式已去掉 smoke 和禁用增强的测试参数。

EXE SHA256：`7AA2452E05E6B423DB1C7A4D3D66B9C9D2BFE41262C337B8D70010EBE301E61A`。

## 建议测试顺序

1. 打开此前复现问题的 4K30 视频，原生 NR＋2X。查看“实际呈现”和“请求目标”，确认不再长期锁在 30。硬件真的处理不过来时不能保证 60，但声音应连续、一倍速；降低负载后补帧应恢复。
2. 连接 PS5，先使用“自动（优先硬解）”，确认状态面板实际显示 D3D12VA。对比 CPU 软件解码；更换解码选项后需要断开再连接。
3. 对比软硬解颜色，测试常用 NR、SR、FG 设置、暂停、全屏、专业/日常切换，以及《羊蹄山之魂》的手柄操作。无需重新注册已保存的主机。
4. 如果再停帧，观察接收、解码、呈现的停滞时间，给恢复逻辑几秒钟。保留本项目 `logs/veyra-app.log`，记录大概时间、参数，以及声音和手柄是否正常。不要发送配对凭据或配置存档。

## 数据怎么看

- 实际呈现：真正提交呈现的帧率，区分于增强完成但已过期的帧；不是显示器物理扫描测量。
- 软件平均延迟与 P95：同组样本的均值和 95% 分位，数值不同正常。阶段耗时存在重叠，不能直接相加。
- PS5 软件延迟从完整视频样本到达应用开始，不包括此前的主机编码、网络传输和屏幕扫描。网络 RTT、完整端到端延迟与 FEC 成功率没有可靠数据时明确显示未测量。

## 已完成及证据

- 文件增强预处理使用有界双批次，修复生成帧总在期限后到达的问题；欠速先减少补帧工作，再按真实时间线跳过预览机会，不停音频、不丢 PCM，导出仍完整处理。
- 独立 PS5 接收/解码进度监控、停滞后有界 IDR 恢复；真实可选 D3D12VA 硬解、自动回退和强制硬解失败提示。
- 状态面板重排，显示真实呈现、各处理阶段和 PS5 数据；不再拿空的采集卡计数当 PS5 输入。
- 最终统一 gate：`scripts/gates/delivery.ps1 -Root $PWD -BuildDirectory out/remoteplay/product-repair -PlayerExe out/remoteplay/product-repair/veyra.exe`，45.29 秒通过。结果：`logs/delivery/82d949d48503403495e10de477ce70d8/result.json`，覆盖实际 NR、NVOF、4K、播放、图片、NVENC 视频及音轨导出。
- 45 秒 4K30 原生 NR＋2X：1284 次原帧呈现、1247 次生成帧呈现、5 次生成帧过期；末段约 59 次/秒，媒体时间约一倍速。`logs/ps5-final-native4k-long-runtime.log`。不是任何素材或显卡都保证 60fps。
- 欠速及原设置恢复、XeSS 音频连续性、400ms 源间断恢复、240 帧 EOF 完整排空、单帧 EOF、3X 回归均通过。证据：`logs/ps5-final-file-overload2.stdout.log`、`logs/ps5-dynamic-recovery.stdout.log`、`logs/ps5-final-xess-continuity.stdout.log`、`logs/ps5-final-source-gap.stdout.log`、`logs/ps5-final-single-frame-runtime.log`、`logs/ps5-final-fg3-runtime.log`。
- Native CTest 69/69、文件源 23 项、呈现/调度/修复/UI/Remote Play 边界测试通过。H.264/H.265 真实软硬解相同片段 YUV 最大差值 0；PTS、颜色元数据、自动回退和强制硬解错误策略通过。`logs/ps5-final-native-ctest2.log`、`logs/ps5-final-media-source.stdout.log`、`logs/ps5-final-source-hardware.stdout.log`。
- UI 实际绘制截图及 20 次模式切换、20 次全屏通过，`logs/ps5-final-ui-visible.log` 与同名目录。早期隐藏窗口 PrintWindow 黑图不作为验证证据。

上述硬解验证使用本地码流及生产增强图，本轮没有重新连接 PS5 或采集卡，也没有测量物理音画偏差。完整命令、失败记录和实现说明见 [执行记录](PS5_TELEMETRY_SCHEDULER_EXECUTION_2026-09-12.md)。

Git 节点：`959776f` 独立进度；`8aa7200` 文件调度；`7843d21` 硬解/统计/恢复预算；`3405717` 硬解失败策略与实际 UI 验证。源码提交不含 SDK、DLL、模型或本机测试媒体。
