# 小飞机（RTSS / MSI Afterburner OSD）适配（2026-10-03）

用户要求：微星小飞机的 RTSS 监控叠加在 Veyra 上时必定闪退或显示异常；改界面为 D3D11 / D3D12 不解决问题（界面一个数值、播放窗口一个数值，界面不需要数值）；同时保证 OBS 采集与 OBS 兼容模式采集正常。

分支 `claude/rtss-compat-20261003`（基于 Codex 的 2.0.1 候选 `15a0e39`），工作区 `E:/项目/Veyra/worktrees/rtss-compat-20261003`，构建 `build/rtss-compat-20261003`，测试证据 `tests/rtss-20261003`。本机 RTSS 7.3.7 + MSI Afterburner、OBS 32.1.2 便携副本（`tmp/obs-test/obs`，不碰用户 OBS 配置）、RTX 5070。

## 复现与根因

本机 Afterburner 没配 OSD 项，测试用 RTSS SDK 的共享内存接口写一行 OSD 文字（与 Afterburner 写 OSD 的方式相同，`rtss_osd.py`，退出时释放）。

| 条件 | 结果 |
|---|---|
| 2.0.1 候选（界面 D3D12）+ RTSS 注入，OSD 无内容 | 正常 |
| 同上，OSD 有内容，打开视频 | 视频开始约 0.4 秒后 GPU 设备被移除（`0x887A002B` ACCESS_DENIED），画面定住 |
| 界面 D3D11（`VEYRA_UI_RHI=d3d11`） | 不移除；但界面自己的交换链也画 OSD（首页显示界面的 320 FPS），即用户说的"两个数值"；现场还有 XeSS 补帧时崩溃 |
| 界面软件绘制（OBS 兼容模式那条路径） | 不移除；OSD 只在视频上；首页无 OSD |
| 界面 DirectComposition / OpenGL / Vulkan | 仍移除或 OSD 跑到界面上（NVIDIA 把 GL/Vulkan 走 DXGI 互操作，仍是第二条 D3D12 交换链） |
| RTSS 进程配置 `RestrictMultipleSwapChains=1`（临时文件，测后删除） | 不移除，但 OSD 锁在先出帧的界面交换链上，视频没有 OSD |
| `RTSSHooksProfileOverride=EnableOSD,0` | 不移除，Veyra 里不画 OSD |

反汇编 RTSSHooks64 的 Present 钩子：先 `GetDesc().OutputWindow`，窗口不可见才跳过；D3D12 下记住"当前交换链"，换一条交换链出帧就重建它的 OSD 渲染器。界面和视频两条 D3D12 交换链交替出帧 → 反复重建 → 设备移除。`RTSSHooksProfileOverride` 环境变量（`名,值[,名,值…]`）只接受 EnableOSD、AppDetectionLevel、PositionX/Y、ZoomRatio 等约 25 项，不能按交换链控制。

## 改动

- `gfx::rivaTunerRunning()`：`RTSSSharedMemoryV2` 存在且签名为 `RTSS` 即视为小飞机在运行。
- 启动时（`main.cpp`，Qt 创建前）：设置 → **监控软件兼容** 为"自动"（默认）且小飞机在运行 → 界面改软件绘制（与 OBS 兼容模式同一路径；视频、NR/SR/补帧仍是原生 D3D12）。RTSS 只看得到视频交换链，OSD 只画在视频上。`VEYRA_UI_RHI` 显式指定时不自动切换。
- 界面用 GPU（D3D12）绘制时，进程里设 `RTSSHooksProfileOverride=EnableOSD,0`：之后才启动的小飞机，或用户把兼容设为"关闭"时，RTSS 在 Veyra 里不画 OSD，不会再把设备拖垮。软件绘制时清掉这个变量（重启继承也会清）。
- 运行中检测到小飞机后来才注入（GPU 界面、兼容为自动）：弹"检测到小飞机（RTSS）"，可一键重启切到兼容绘制。
- OBS 游戏采集与小飞机同时注入：只提示一次（偏好 `obsRivaTunerHintShown`），说明在 RTSS 里给 veyra_qml_ui.exe 勾选"Use Microsoft Detours API hooking"，或 OBS 改用窗口采集。
- 设置页新行"监控软件兼容 自动/关闭"，提示随状态变化；崩溃/设备丢失后的 RTSS 建议文字改为指向这个设置。四种语言已翻译。
- 测试钩子 `VEYRA_TEST_IGNORE_RTSS=1`：启动时当作小飞机没运行（测试"后启动"路径）。

## 验证（本机 RTSS 运行、OSD 有内容）

| 项目 | 结果 |
|---|---|
| 自动：`overlay-stress.qml`（播放 → NR+SR → DLSS 补帧 → 全屏 → XeSS → FSR3 → 窗口 → 专业页 → 首页） | 全程正常、无设备移除；OSD 只在视频上，首页/专业页界面无 OSD |
| 关闭：同一流程，界面 D3D12 | 全程正常、无设备移除；Veyra 内无 OSD |
| 小飞机后启动（`VEYRA_TEST_IGNORE_RTSS=1`）：界面 D3D12 播放中被注入 | 不冻结，弹重启提示，视频继续 |
| 继承 `EnableOSD,0` 重启到软件绘制 | 变量被清，OSD 正常出现在视频上 |
| OBS 窗口采集（WGC）+ 小飞机 | 6/6 帧有画面，含视频上的 OSD |
| OBS 游戏采集，不受 RTSS 影响（`AppDetectionLevel,0`）：GPU 界面 / OBS 兼容模式 | 6/6、6/6 帧为视频画面 |
| OBS 游戏采集 + 小飞机（默认挂钩） | 0/6；**最小 D3D12 窗口程序同样 0/4**，是 RTSS 7.3.7 与 OBS 32.1.2 在 D3D12 上的通用冲突，改动前的 2.0.1 候选也 0/6 |
| 同上，RTSS 进程配置 `UseDetours=1`（临时文件，测后删除） | 最小程序 4/4；Veyra 6/6 帧为视频、无设备移除 |
| 单测 | i18n、qml-data、ui-contract 通过；Qt Quick 29/29 |
| CPU（播放 1080p30） | GPU 界面约 6–16%，软件界面约 6–13%；首页软件界面约 4–25%（首页动画由 CPU 重绘） |

## 限制

- 软件绘制界面：首页图标光晕、背景渐变等 MultiEffect 效果简化（与 OBS 兼容模式相同）。
- OBS 游戏采集与小飞机同开需要用户在 RTSS 里给 Veyra 开 Detours 挂钩（或用窗口采集）；Veyra 不改小飞机、OBS 的配置文件。
- 游戏加加（GamePP）等其他叠加层未在本机验证；可手动开"OBS 游戏采集兼容"走同样的软件绘制界面。
- 未在 AMD/Intel 显卡、其他 RTSS 版本上验证。
