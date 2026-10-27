# PS5 补帧、过载耗时与声音补偿修复

基线 `c68b4b0`，分支 `codex/ps5-fg-overload-audio`。用户报告 DLSS 2X 无有效补帧、过载后耗时消失、声音补偿持续上涨。本次只修本机版本，不发布。

## 证据与口径

- 原始日志副本：`logs/ps5-fg-overload-audio-before.log`（含本机信息，不入 Git）。revision 7：485 个补帧候选全部在 Evaluate 前被拒绝；revision 8：1694 个候选中只执行 3 次且全部预热。不是仅 UI 显示错误，也不是 DLL 初始化失败。
- 19:09:20 至 19:17:23 的串流回调计数、扣除 receiveAge 后，平均约 59.9314 fps。旧 RemotePlayClock 直接用 frameIndex / 60；约八分钟产生约 0.55 秒时间轴偏差，与用户约 575 ms 音频等待现象量级一致。日志不足以测量 PS5 扫描输出时间，不能把 59.9314 宣称为协议原生帧率。
- 声音补偿公式：`当前呈现 host - 视频 PTS - 音频到达 host/PTS 映射`，裁剪到自动模式上限。它是音视频对齐等待，不是 NR/SR/FG GPU 耗时。PCM 队列是实际尚未送入声音设备的样本量，不能与补偿直接相加。
- 解码后 latest mailbox 旧实现把覆盖标成 Discontinuity，导致每次过载丢帧重开统计窗口；GPU 计时异步返回时又被 epoch 检查拒绝。需要保留 GPU 历史 reset，但不清空同配置下的耗时统计。
- PS5 的 arrival 是压缩 AU 到达，不能直接当成解码后画面可用的时间。在补帧调度锚点使用 decodedHost，完整输入到呈现的测量仍从原 AU arrival 起算，不隐藏解码耗时。
- 实机第三轮表明：仅修正首次锚点仍不足，PS5 硬解交付耗时约 2–15 ms 摆动，会耗尽固定旧锚点的余量。最终 PS5 专用准入/呈现使用每对新解码输入的 decodedHost + 一个输入间隔预算；在增强前确定，增强耗时或 GPU 完成不能延长它。A/B 内仍按 PTS 间距显示，源 PTS 不改写为完成时间；网络/解码抖动仍可能体现在输出节奏，不能保证锁定 120 fps。文件与物理采集的现有时间线不变。

## 实施

1. RemotePlayClock 采用最近一秒到达时间的低抖动相位估计，最大 0.5% 渐进修正。保持单调 PTS、原帧序号和 nominal duration；真实大间断继续显式 reset。不是硬编码 59.94，不改变文件原始 PTS。
2. 音频 ingress 映射改为最近两秒的最小值，移除永不老化的启动最小值，避免设备钟差被算成永久增加的补偿。保留原 PCM 连续性、重采样和有限队列策略，不靠丢声音压低显示值。
3. decoded mailbox 使用 Drop，并传递先前真实的 Open/Discontinuity 等事件。Engine 的 Drop / preview skip 只重置处理历史；明确 seek/配置/尺寸/设备/时间轴中断仍创建新统计窗口。FrameFlowWindow 只接受本统计窗口注册的 epoch 范围及同配置 GPU 结果。
4. 最近一秒有实际 FG 测量则显示该均值；配置已开启但当前没有执行时显示“等待补帧”，不再写“未开启”。XeSS 内部计时仍标不可测。
5. 新增每秒补帧准入原因、每两秒音频对齐和周期串流时钟日志，供后续用户长时验收。

## 验证与未完成边界

- 构建命令：`cmd /c out/remoteplay/build-extra-delay.cmd`。最初链接因运行中的 veyra.exe 被锁失败；将旧 EXE 重命名为同目录忽略文件 `veyra-before-clock-fix.exe`，保持旧进程运行，再链接成功。不是关闭用户的软件。
- `cmd /c out/remoteplay/build-clock-tests.cmd`：离线核心 68/68；新增模拟十分钟 30/60 profile、正负 1400 ppm 漂移、2 ms 抖动及真实准入类检查。初版用 5 ms 抖动却要求 98% 准入，因预算确实可能不足而失败；改为具有明确余量的 2 ms 固定输入，未放宽产品准入阈值。
- `veyra_repair_contract_tests.exe`：最终 103 checks / 0 failures；覆盖连续 78 次历史切换的异步计时保留、新窗口拒绝旧计时、十分钟音频 ingress 漂移老化、PS5 解码抖动不占预算而增强超时仍拒绝。
- `veyra_presentation_worker_tests.exe`：通过现有调度预算和恢复检查。
- `veyra_capture_audio_tests.exe`：真实 WASAPI 基础、模式切换、长延迟、间断恢复和有界 PCM 测试通过，`logs/ps5-clock-audio.log`。
- `veyra_capture_audio_tests.exe --drift-slow`：120 秒设备钟差，P95 音画偏差 4.254 ms、missing=0、resets=1（启动）、overflows=0，队列峰值 89.646 ms，`logs/ps5-clock-audio-drift.log`。
- `veyra_live_presentation_tests.exe --last-paired-ps5 <日志目录>` 是明确 opt-in 的真实串流测试：复用本机已加密的最后主机、硬解、4K 视频 SR、NR、DLSS 2X、真实声音设备静音输出，30–38 秒注入 35 ms CPU 工作模拟过载。不会写配对或执行开关机。
- 实机第一轮失败，暴露 decoded mailbox 的 Discontinuity 与压缩 AU 锚点问题；第二轮过载耗时 164/164 采样保留，但补帧恢复/长时测试未通过。第三轮跑完 120 秒，音频等待早期 64.761 / 末期 57.389 ms，过载计时 163/163，仍因末期有效补帧只有 4 fps 失败，因此继续修正解码输入对的固定预算。后续结果见下方追加；不得把前三轮算通过。

## 后续实测结果

最终第四轮 120 秒实机测试通过：`logs/ps5-clock-live4-result.log`、`logs/ps5-clock-live4/engine.log`。

- RTX 5070、PS5 H.264 硬解、视频 SR 到 3840×2160、实时内部 NR、DLSS 2X；NR Feature18 Create=0x1 / SEH=0，DLSSG Create 3840×2160=0x1 / SEH=0，预热 Evaluate=0x1。
- 115 秒时有效生成累计 5251 帧，最终一秒有效生成 53 fps。呈现提交达到过 120 fps，稳态仍有约 84–118 fps 波动，不能宣传固定 120 fps，也未测显示器 scanout。
- 注入 CPU 过载后，163/163 次采样保留 NR 实测耗时；恢复后持续生成有效补帧。不是取消过载保护或给假计时。
- 音频等待早期 64.587 ms，末期 57.892 ms；启动与强制过载恢复合计 4 次 reset，恢复后未继续增长。两分钟实机加离线十分钟仿真，不等于小时级游戏验收。
- `veyra_remoteplay_boundary_tests.exe` 通过，新增 Drop / Discontinuity 分类断言；边界测试不冒充实机证据。
- 最终产品构建：`cmd /c out/remoteplay/build-clock-product.cmd`，`logs/ps5-clock-product-final.log`；最终合同构建及运行：`cmd /c out/remoteplay/build-clock-contract.cmd`、`veyra_repair_contract_tests.exe`，`logs/ps5-clock-contract-final.log`。`git diff --check` 通过。

交付 EXE：`out/remoteplay/product-repair/veyra.exe`，桌面原 PS5 测试版快捷方式仍指向该位置。当前旧进程须退出后重新打开才会运行新代码。本轮未改 NVIDIA/FFmpeg DLL，未提交 SDK、二进制、配对资料或日志，没有 push / Release。下一步唯一验收：用户重开测试版，按实际游戏配置长时游玩，核对补帧、过载计时和声音连续性；若仍异常，带新增 `live-fg-admission` / `remoteplay-clock` / `live-audio-sync` 日志继续定位。
