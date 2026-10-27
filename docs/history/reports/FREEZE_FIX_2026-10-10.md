# 采集中卡死（现场 logs(15)）修复记录（2026-10-10）

## 授权与范围

- 用户：“这有个用户反馈，经常玩着玩着卡死，排查修复这个问题”；随后提供线索“NVIDIA app 里那个信息浮窗开了之后 2.0 以后的就要卡死”“软件一开采集 10s 不到就卡死”，并要求“估计和之前小飞机适配差不多的问题”“也许需要是个偶发现象，你先根据目前的证据做一些适配吧”“还有游戏加加这个东西，也一起适配”。
- 沿用同一会话的规则：先存档、开分支，结果倒退就退回。
- 只改监控/叠加软件兼容（界面绘制方式的选择）、引擎卡死诊断和退出流程。不改采集、NR/SR/FG、呈现策略、GPU 优先级、运行库、驱动和用户配置。不 merge/push/Release。

## 存档与分支

- 检查点标签 `checkpoint/pre-claude-freeze-20261010` 指向 main `5ba186a`。
- `archives/claude-freeze-20261010/`：`source-before.bundle`（已 verify）和 `start.json`（48 个工作树、495 个未提交文件的 SHA；现场日志包 SHA256 `10366440…e304`）。
- 分支 `claude/freeze-fix-20261010`，工作树 `worktrees/freeze-fix-20261010`。
- 构建：`py -3.11 -I tmp/claude-freeze-20261010/build.py freeze`（复用 vfg-next 的 CMake 图，只重编改动文件及其包含闭包；新文件 `src/engine/EngineWatchdog.cpp` 用 EngineController 的编译参数并入 veyra_engine）。测试构建给 `veyra_qml_ui` 额外生成公共符号 PDB，供 `symbolize.py` 还原日志里的地址。

## 现场日志结论（logs(15)，RTX 5060 Ti，驱动 617.42）

- 2026-10-10 11:47–13:14（UTC）共 12 次卡死。每次引擎线程（GPU owner）在一次正常的帧之后不再写任何日志，没有错误、没有 10 秒栅栏超时；采集回调和音频线程照常运行。
- 12 次中有 10 次，界面线程在同一时刻也停了（`[qml-frames]` 和窗口日志一起消失），只剩采集/音频线程：这是两个“出画面”的线程互相卡住。另外两次界面又活了 10 秒到 1 分钟。
- 用户点关闭后进程不退出（11:48:53 关闭，进程一直活到 11:54:25 被结束），因为退出时要等引擎线程。
- 12 次全部发生在 HDR 输出（HDR10 RGB10 PQ）且开着 NVIDIA NGX 功能时：11 次是 4K30 P010 + DLSS 补帧 2X，1 次是 1080p60 + RTX Video HDR。同一份日志里 SDR 输出或不开 NGX 功能约 35 分钟没有卡死。
- GPU 优先级不是原因：前 8 次是“实时”，用户改成“高”之后又卡了 4 次。会话早期把 NVIDIA+HAGS 下改用“高”作为候选的方案因此放弃，默认值不动。
- 每次启动都有 `[overlay-hooks] NVIDIA overlay / instant replay is injected`（nvspcap64.dll）。但本机浮窗关闭时它也一样被驱动加载，所以模块在不在不能说明浮窗开没开；浮窗开启时 NVIDIA App 才运行 `NVIDIA Overlay.exe`（本机实测：开启前没有，开启后 5 个进程）。
- 结合用户线索（开浮窗后 2.0 以后的版本采集几秒内卡死；1.4.4 的界面没有 D3D 交换链），以及小飞机（RTSS）在两个 D3D12 交换链之间切换导致设备丢失的先例：浮窗同时挂接界面和视频两个交换链、两个线程同时出画面时互相卡住，是目前最符合证据的解释。

## 本机复现（RTX 5070，驱动 616.56，浮窗开启，D3D12 界面）

| 组 | 内容 | 结果 |
| --- | --- | --- |
| a1 | 2K60 NV12 无特效 25 秒 | 不卡，60 fps |
| a2 | DLSS 补帧 2X 35 秒 | 不卡，120 fps |
| a3 | 全屏 + DLSS 补帧 2X 35 秒 | 不卡 |
| a4 | HDR 输出（RTX Video HDR）+ DLSS 补帧 2X 45 秒 | 不卡 |

本机短时间没有复现，和“偶发”及显卡/驱动差异一致。以下改动按现有证据做适配，现场效果需要反馈用户确认。

## 改动

1. **监控软件兼容扩展到 NVIDIA 信息浮窗和游戏加加**（设置 → 监控软件兼容，默认“自动”）：
   - 启动时检测到 NVIDIA 信息浮窗（`NVIDIA Overlay.exe`，GeForce Experience 为 `NVIDIA Share.exe`）或游戏加加（`GamePP*.exe`），和小飞机一样让界面用软件绘制，进程里只剩视频一个 D3D12 交换链；视频的 D3D12 处理和呈现不变。
   - 运行中才出现（界面已经用显卡绘制）时弹窗建议重启进兼容绘制，文案按小飞机 / NVIDIA 信息浮窗 / 游戏加加区分；游戏加加注入的模块（GPP64.dll、GameTracker64.dll、shade64.dll）出现也会触发。每次运行只问一次。
   - “关闭”时界面始终用显卡绘制，设置说明里写明开着浮窗或游戏加加可能卡死。
   - 日志：`[app] NVIDIA overlay running=… GamePP running=… interface=software|GPU`。
2. **引擎卡死监视**（`EngineWatchdog`）：播放循环超过 8 秒没有进展时
   - 记录 `[engine-hang]`：最后完成的阶段、线程 CPU 时间、浮窗/小飞机/游戏加加状态，以及卡住线程的调用栈（模块 + 偏移 + 最近的导出函数名，例如 `ntdll.dll NtDelayExecution`）；
   - 在日志目录写一份小转储 `veyra-hang-*.dmp`（只含线程栈、模块和句柄，约 1.2 MB，每个进程最多一份）；
   - 弹出提示“画面引擎无响应，已记录诊断信息；如画面未恢复，请关闭软件后重新打开”；10 秒后再采一次栈，区分“完全阻塞”和“空转”；恢复时记录卡了多久。
3. **退出不再卡死**：关闭软件时最多等引擎线程 20 秒；监视已判定卡死时只等 3 秒，然后直接结束进程（之前会一直挂着，只能在任务管理器里结束）。
- 测试开关：`VEYRA_TEST_ENGINE_HANG_MS`（播放 120 帧后让引擎阻塞指定毫秒，-1 为永久）、`VEYRA_TEST_IGNORE_NVIDIA_OVERLAY`、`VEYRA_TEST_IGNORE_GAMEPP`。
- 新增文案已加入 i18n/catalog.json（繁中/英/日）；`scripts/i18n/extract.py --check` 与 main 相同（既有 29 条缺译，占位符问题 0）。

## 测试（脚本 `tests/claude-freeze-20261010/harness/ab.py`，日志 `logs/claude-freeze-20261010/freeze/`）

| 组 | 内容 | 结果 |
| --- | --- | --- |
| t1 | 浮窗开启 + 自动 + DLSS 补帧 2X | 启动即软件绘制界面（`nvidiaOverlay=true … interface=software`），120 fps，原帧回调→送显 13.9/14.6 ms（D3D12 界面时 13.4/13.8 ms） |
| t2 | 引擎阻塞 12 秒 | 第 8 秒记录调用栈（NtDelayExecution ← SleepEx ← 播放循环）、写转储 1.25 MB、弹出提示；12062 ms 后记录恢复 |
| t3b | 引擎永久阻塞后关闭 | 第 8 秒记录；第 18 秒复查“栈不变、CPU 0 ms”（阻塞而非空转）；关闭时等 3013 ms 后结束进程，退出码 0 |
| t4 | 模拟游戏加加（改名为 GamePP.exe 的系统 ping）启动前已运行 | 启动即软件绘制界面，采集 60 fps |
| t5 | 模拟游戏加加在软件运行中途启动 | 约 1 秒后记录并弹出重启建议 |

回归：`py -3.11 -I tests/claude-freeze-20261010/harness/regress.py freeze …` 13 组全部通过（chain、contracts、hdr、presenter、lifecycle、video、rollback、inactive、inactive-node、sr、slider、menu、components；Qt Quick 13/8/22 项），回执 `logs/claude-freeze-20261010/freeze/tests-20261011-030000/receipt.json`，donor 测试包逐文件前后一致。

## 未验证与边界

- 本机没有复现卡死，修复对现场的效果需要反馈用户开着浮窗实际使用确认。如果仍卡，新的 `[engine-hang]` 日志和转储会直接给出卡住的调用。
- 本机没有安装游戏加加，只用进程名模拟；它的辅助进程名、在关闭游戏内显示时是否还注入，都未核实。GeForce Experience 的 `NVIDIA Share.exe` 行为未在本机验证。
- 正式构建的 EXE 不带 PDB，`[engine-hang]` 里我们自己模块的帧只有偏移；系统和驱动模块（ntdll、dxgi、d3d12、nvwgf2umx、nvspcap64 等）有导出名，能说明卡在哪类调用。建议发布时保留同版本 PDB 或 MAP。
- 软件绘制界面与小飞机兼容模式相同：界面用 CPU 绘制，部分阴影与模糊简化；默认开着 NVIDIA 浮窗的用户都会走这条路。想保留显卡绘制可把“监控软件兼容”改为“关闭”。
- 引擎卡死时如果用户正好确认了“重启”，进程会被直接结束而不会自动重新打开。
