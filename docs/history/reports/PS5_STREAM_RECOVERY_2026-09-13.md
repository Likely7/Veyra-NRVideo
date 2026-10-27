# PS5 断流恢复施工记录

## 任务与基线

用户游玩中画面停住，接收和解码均为 0，要求修复。基线 `b5cb1b5`，施工分支 `codex/ps5-stream-recovery`。不发布、不更换运行 DLL、不修改配对文件。

## 已确认事实

现场日志副本 `logs/ps5-stall-20260913-before.log`（本地忽略，不提交）：2026-09-12 19:42:05 UTC（本地 9 月 13 日 03:42:05）之后完整视频输入与解码计数都停在 7053，队列为 0，decodeBusy 为 -1。此前 NR 约 6.23 ms、SR 约 2.12 ms、FG 约 2.65 ms，未见 GPU 超时。旧逻辑请求关键帧三次未恢复，30 秒后报错；手动重连后恢复。

这是解码前没有新的完整视频输入，不足以判断是网络收包、Chiaki 组帧还是适配器拒绝。部分音频回调继续到达，不能称为整条连接完全断开。现有解码错误计数为零也不等于底层无错误。

## 修复设计及代码

- `StreamRecovery.h`：有画面后断流 1 秒、3 秒请求关键帧，6 秒仍未恢复则重建串流会话。初次连接未出画面仍保留 30 秒等待；登录 PIN 保留 120 秒等待。最多自动重连 3 次，等待间隔 1、2、4 秒；偶发恢复一帧不会刷新次数预算。
- `RemotePlaySessionSource`：同一个 owner 重用 source，先停止监测、PCM feeder、WASAPI，再 stop/join/fini Chiaki 和解码器，旧会话退出后才创建新会话。主动断开可取消退避。凭据只保留在本次连接请求的内存中，销毁时清零，不重新配对或写盘。
- 重连后的应用帧序号连续，源 epoch 分开，首帧携带 Discontinuity；即使首帧被 latest mailbox 覆盖，重置标记也传递至下一帧。继续共享 EngineController/EnhanceGraph/Presenter；不另建播放循环。
- `ChiakiBackend` 新增固定字段日志：完整回调数、拒绝数、底层包统计窗口、上游警告/错误分类及终止码。底层包窗口可能被上游重置，不能当累计 UDP 或相邻窗口差速率。原始上游字符串/密钥不输出。
- 已知主机关闭、明确终止或认证拒绝不自动重连。可重试的上游终止码限 CTRL_UNKNOWN、CTRL_CONNECT_FAILED、STREAM_CONNECTION_UNKNOWN、SESSION_REQUEST_RP_IN_USE，无终止码的无画面超时也可重试。RP_IN_USE 是实测旧会话退出后主机尚未释放时出现的状态，只能消耗原有 3 次预算；初次连接从未收到画面时不自动重试，不强制抢占其他会话。
- UI 面板与状态卡显示“恢复中”，最终失败显示串流原因，不再通用显示为采集卡断开。

## 验证状态

构建与离线：`cmd /c out/remoteplay/build-extra-delay.cmd` 构建主程序、实际播放测试和现有产品测试成功。MSVC 有既存 FFmpeg 头文件 C4244 警告。`cmd /c out/remoteplay/build-clock-tests.cmd` 最终核心 73/73（`logs/ps5-stream-core-result.log`）。`cmd /c out/remoteplay/build-boundary-clock.cmd` 后执行 `veyra_remoteplay_boundary_tests.exe` 通过，包含重连序号连续及首帧重置标记跨 mailbox 覆盖；`veyra_repair_contract_tests.exe` 103 checks / 0 failures。

新增实机测试选项：`out/remoteplay/product-repair/veyra_live_presentation_tests.exe --last-paired-ps5 <日志目录> --reconnect`，显式环境变量在第 600 个解码帧停止测试自己持有的传输，不发送 PS5 关机命令。验证断流前实际播放、恢复后 NR/SR/DLSS 补帧和音频持续推进。`--cancel-reconnect` 在第 120 个解码帧注入，验证重连退避阶段主动停止。此注入是可控断流，不能冒充已复现最初网络故障。

### 实机结果与失败记录

均在 RTX 5070、PS5 H.264 1080p60 硬解、80 Mbps 请求、视频 SR 到 4K、实时 NR、DLSS 2X 下运行；每轮不超过 50 秒加停止等待，单次低于 300 秒。测试使用静音 WASAPI 实际输出，不代表主观听感或手柄已验收。

1. `logs/ps5-stream-reconnect-result.log` / `logs/ps5-stream-reconnect/engine.log`：自动重连一次、FG 与音频恢复。但初版在第 120 解码帧、增强初始化尚未完成时就断流，且最后一秒 FG 从 60 降到 5 导致单点性能断言失败。这轮不算整体通过。后续改为第 600 帧，并检查断流前已呈现和生成、恢复后至少 300 新帧/100 生成帧及至少 250 次健康采样，不修改产品性能准入逻辑。
2. `logs/ps5-stream-reconnect2-result.log` / `logs/ps5-stream-reconnect2/engine.log`：初始播放成功，重连被 PS5 返回 quitReason=4 / error=2004（RP_IN_USE），旧策略直接失败。由此增加有上限的占用状态重试，保留失败日志。
3. `logs/ps5-stream-reconnect3-result.log` / `logs/ps5-stream-reconnect3/engine.log`：PASS。断流前已有画面和补帧；第一次重连主机占用，第二次恢复；730 次恢复后健康采样，退出 idle=1。45 秒采样呈现提交 110 fps、生成累计 1000，音频补偿 103.945 ms。NR Feature18 Create=0x1、DLSSG Create 3840×2160=0x1，SEH=0；关闭时 DLSSG evaluates=1276，Release=0x1。不是固定 120 fps 或屏幕 scanout 测量。

4. `logs/ps5-stream-cancel-result.log` / `logs/ps5-stream-cancel/engine.log`：`--cancel-reconnect` PASS。恢复阶段主动 Stop，manualCancel=1、idle=1，额外观察 2 秒未再次启动会话；该测试只验证取消，不声称恢复输出成功。

`git diff --check` 通过。只提交源码、测试和文档，无 SDK/DLL/配对/日志进入 Git；没有推送或发布。产品 EXE 为 `out/remoteplay/product-repair/veyra.exe`，原桌面 PS5 测试版快捷方式路径不变。下一步由用户重新打开测试版长时游玩；若再次自然断流，保留新 `remoteplay-transport` 与 `remoteplay-recovery` 日志继续定位最初原因。

## 边界

不保证断流期间声音画面连续，也不掩盖新会话的时间轴断点。若底层 decode/驱动调用或 join 本身永久阻塞，本次 owner 上的恢复不能安全强杀线程；不得 detach 后释放回调资源。最初自然断流根因仍待新日志区分。未验证小时级网络波动、休眠/恢复和断线时的真实手柄触觉状态。
