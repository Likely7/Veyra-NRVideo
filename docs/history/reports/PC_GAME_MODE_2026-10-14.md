# PC 游戏模式（穿透操作）实施记录 2026-10-14（Claude）

方案见 [PC_GAME_PASSTHROUGH_PLAN_2026-10-14.md](../plans/PC_GAME_PASSTHROUGH_PLAN_2026-10-14.md)。

用户拍板：“在捕获内加一个开关，热键就用你这个吧，不要倒计时，并且如果是捕获显示器，直接去掉黄色的指示框，
全屏 Veyra 后相当于也能直接操作整个电脑一样，然后把你没注意到的内容处理好”。

分支 `claude/fg-gpu-home-20261014`。证据在 `logs/claude-fg-gpu-home-20261014/`。

## 用法

1. 屏幕捕获对话框新增“游戏”分区和“游戏模式”开关（屏幕选项 `passthrough`），选窗口或显示器都可以。
2. 点“开始捕获”后不倒计时：Veyra 移到被捕获的显示器（窗口捕获时为游戏所在的显示器）并全屏，
   捕获一开始出画就进入穿透：
   - 键鼠和手柄直接操作下面的程序；
   - 显示器捕获时，屏幕上任何程序都能直接操作，就像直接用电脑。
3. 调整画面：按 Home，打开调整面板（列表模式为检查器，节点模式为画布），Veyra 收回操作。
   关闭面板的方式：再按 Home、按 Esc，或点击面板旁边的画面，操作自动还给程序。
4. 退出：Ctrl+Alt+Home 退出全屏和穿透，Veyra 回到前台。捕获继续；重新全屏会再次进入穿透。
5. 捕获结束或失败（例如游戏窗口关闭）时自动退出全屏，提示原因。

## 实现

- **`include/veyra/ui/GamePassthrough.h`**（header-only，进程内，GUI 线程）：
  - **热键**：`RegisterHotKey`，`MOD_NOREPEAT`。
    - Ctrl+Alt+Home 注册不上就拒绝进入穿透。
    - Home 只在用户“处于画面里”时占用：窗口捕获时游戏在前台，显示器捕获时前台程序在这块屏幕上。
      在别的程序里 Home 仍归那个程序。
  - **置顶**：`SetWindowPos(HWND_TOPMOST)`。
  - **交出前台**：窗口捕获交给被捕获的窗口；显示器捕获交给这块屏幕上 z 序第一个普通窗口，没有就交给桌面。
    失败时用 `AttachThreadInput` 兜底，结果写日志。
  - **跟随游戏**（`SetWinEventHook`）：窗口捕获时，同屏的其它程序到前台，画面就退到它后面（仍在游戏之上）；
    游戏回来，画面重新置顶。开始菜单、Alt+Tab、任务栏、桌面不触发这个动作。
  - **看门狗**：GUI 线程 3 秒没有心跳，看门狗线程用 `DWMWA_CLOAK` 隐藏画面（不需要 GUI 线程配合）；
    恢复后取消隐藏，退出游戏模式并提示。
  - **新显示窗口**：捕获期间新显示的自身窗口在显示前就排除出捕获（`WM_SHOWWINDOW` /
    `SWP_SHOWWINDOW`），穿透期间也变为可穿透。
- **主窗口的穿透**：用 Qt 自己的标志，在 `Main.qml` 里把 `Qt.WindowTransparentForInput |
  Qt.WindowDoesNotAcceptFocus` 绑定到 `veyra.passthroughActive`，并另外补上 `WS_EX_NOACTIVATE`
  （Qt 不为这个窗口设置它）。原因：软件界面（RTSS、NVIDIA 浮窗、游戏加加的兼容模式，玩家常见）的
  主窗口不是分层窗口；从外部加上的分层位会被 Qt 去掉（实测 `game-softui2`），点击就穿不过去了。
  交给 Qt 管理后，两种渲染器都稳定（`game-softui4`、`game-gpuui4`）。
- **桥接**：
  - `gameMode`、`passthroughActive` 属性；`setGamePanelOpen`、`gameModeScreenIndex` 方法，
    后者按 HMONITOR 匹配 `QNativeInterface::QWindowsScreen`。
  - `gameHotkey`、`gameModeStopped` 信号。
  - 状态机在 16 ms 轮询里运行：游戏模式捕获已出画 + 全屏 → 穿透；面板打开 → 面板；否则为普通窗口。
  - 先发布状态（Qt 先改窗口标志），再移交前台。
  - 只有 `startScreenCapture` 以游戏模式打开的那个 URI 对应的会话才算游戏模式。
- **游戏模式开始时**：
  - 捕获不带指针（真指针在最上层，避免第二个延迟指针）；
  - 裁剪归零（画面与屏幕一一对应，点到哪里就是哪里）；
  - 窗口捕获也去掉黄框；
  - 检测到独占全屏游戏时提示改成无边框；游戏窗口没有铺满屏幕时提示点击位置可能对不上；
  - 穿透期间隐藏控制条和播放列表窗口。

## 显示器捕获修正（对所有显示器捕获生效，不只是游戏模式）

- **黄色边框**：Windows 11 申请 borderless 访问后设置 `IsBorderRequired(false)`。实测 `access=4`（Allowed）、
  `required=false`。没有该属性的系统保留边框。
- **控制条被捕获进画面**：
  - 原因：捕获打开约 0.1 秒后控制条窗口才显示，旧代码只排除打开时可见的窗口，所以它的 affinity 为 `0x0`。
  - 修正：打开时连同隐藏的 Qt 窗口一起排除；之后显示的窗口在 GUI 线程上、显示前排除。
  - 引擎线程不再持锁调用任何窗口函数，避免 GUI 与引擎互锁。
  - 实测控制条 affinity 为 `0x11`（`mon-aff2`）。
- **URI 兼容**：borderless 放在 URI 的 cursor 字段 bit 1。旧 URI（0/1）解析结果不变，测试覆盖。

## 实测（RTX 5070，2560×1440 HDR 单屏）

| 场景 | 结果 | 证据 |
|---|---|---|
| 显示器捕获游戏模式 | 穿透 `0x8080028`，前台交给屏幕上的程序；Home 面板 `0x80008`，前台 Veyra；再按 Home 回到穿透；Ctrl+Alt+Home 退出，样式还原为 `0x80000` | `game-monitor`、`game-gpuui4` |
| 软件界面 | 穿透 `0x8080028`（3 秒后仍在）、面板 `0x8`、退出 `0x0` | `game-softui4` |
| 窗口捕获游戏模式 | 测试窗口获得前台（窗口自己记录到 focus-in），屏幕索引 0，边框关闭；没铺满屏幕的提示出现 | `game-window2` |
| 关闭被捕获窗口 | 自动退出全屏，Veyra 回到前台并提示“采集窗口已关闭” | `game-window3` |
| 面板中按 Esc / 点画面 | 关闭面板回到穿透，不退出全屏 | `game-escclick` |
| GUI 卡住 5 秒 | 约 3.1 秒后画面被隐藏（cloaked=1）；恢复后取消隐藏、退出全屏、提示 | `game-hang2` |
| 对话框 | 截图核对新分区、指针和裁剪行的灰显与说明 | `dialog-game` |

热键测试用投递的 `WM_HOTKEY`（不按真实键盘）；`RegisterHotKey` 的成功与失败写在日志里。

回归（`tests-183824`）：

- repair 259/0，UI 合同通过，realtime preview 0 失败；
- 节奏测试 6X 回放垂直同步、屏幕 XeSS、屏幕 DLSS 4X、生命周期全部通过；
- `veyra_screen_capture_tests` 新增的 borderless URI 两项通过，排除与恢复检查通过。
  其中 5 项 RGB 颜色检查失败：本机桌面是 HDR，测试的颜色阈值按 SDR 设定（红条测到 255,110,115）。
  用原版捕获代码单独构建的同一测试也是这 5 项失败（`veyra_screen_capture_tests_baseline.txt`），
  与本次改动无关。

## 没做 / 待验证

- 真实游戏（Raw Input 射击、鼠标指针类、手柄）和反作弊游戏未测。
- 独占全屏游戏本身无法覆盖，只给提示。
- 窗口化游戏的“贴合窗口”模式、缩放模式（指针映射）、手柄组合键都没有做（方案中的 P2/P3）。
- 热键不能在设置里改（设置页“固定”列表里已列出 Home / Ctrl+Alt+Home）。
