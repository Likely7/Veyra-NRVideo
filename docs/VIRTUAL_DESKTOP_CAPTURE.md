# 屏幕捕获：Windows 虚拟桌面（多桌面）支持

## 目标

Windows 的多个虚拟桌面（Win+Ctrl+D 新建，Win+Ctrl+←/→ 切换，任务视图 Win+Tab）之间：

- 程序（游戏、播放器）放在 **桌面 2**，Veyra 放在 **桌面 1** 显示增强后的画面，不用切过去就能选中并捕获那个窗口。
- 捕获开始后，无论用户切到哪个桌面、或把源窗口移到别的桌面，捕获会话都保持，不中断、不重开。

多显示器此前已支持（“显示器”类型逐个列出每块屏幕），本次改动不涉及。

## 原因

Shell 会对不在当前桌面上的窗口做 DWM cloak（`DWMWA_CLOAKED == DWM_CLOAKED_SHELL`）。
原来的窗口枚举把所有 cloaked 窗口都过滤掉，所以其他桌面上的窗口不出现在列表里。
Windows Graphics Capture（WGC）对 cloaked 窗口仍然合成并出帧，因此只需要让这些窗口进入列表即可。

## 实现

| 位置 | 改动 |
|---|---|
| `include/veyra/source/VirtualDesktop.h`、`src/source/VirtualDesktop.cpp` | 新模块。用公开的 `IVirtualDesktopManager`（`IsWindowOnCurrentVirtualDesktop`、`GetWindowDesktopId`）判断窗口是否在其他桌面；从 Explorer 的 `HKCU\...\Explorer\VirtualDesktops` 只读取得任务视图顺序（桌面编号）和用户自定义的桌面名。注册表布局不认识时编号/名称为未知，功能仍可用。 |
| `ScreenCaptureSource::targets()` | cloaked 窗口中，仅当 **shell cloak + 管理器确认不在当前桌面 + 桌面 GUID 在任务视图列表中** 才列出；其他 cloaked 窗口（挂起的商店应用框架、开始菜单等 shell 界面）仍排除。当前桌面窗口保持 Z 序排在前面（默认选中项不变），其他桌面按任务视图顺序排在后面。 |
| `ScreenCaptureTarget` | 新增 `otherDesktop`、`desktopNumber`、`desktopName`。 |
| `ScreenCaptureSource::open()/read()` | 引擎线程上不调用 COM，只用 `DwmGetWindowAttribute` 每秒检查一次源窗口是否在隐藏桌面，日志记录切换；源窗口在隐藏桌面且 2 秒无新帧时，状态提示“部分程序（如 Chrome、Edge）在所在桌面不显示时会暂停绘制”，有新帧或回到当前桌面后清除。会话本身不重建。 |
| `src/ui/bridge/Capture.cpp`、`qml/Veyra/ScreenSourceDialog.qml` | 目标列表显示 `[桌面 2] 标题`（重命名过的桌面显示 `[桌面 2 · 名称]`）；选中其他桌面窗口时给出提示。 |
| 游戏模式 | 游戏模式会把前台交给源窗口，而激活其他桌面上的窗口会让 Windows 切换桌面、离开 Veyra 画面，所以源窗口在其他桌面时拒绝开启游戏模式并提示移动窗口或关闭游戏模式（开始前实时检查）。 |
| `apps/veyra/ui/ScreenCapturePanel.cpp` | 旧 Win32 面板同样显示桌面标记。 |
| `i18n/catalog.json` | 新增 6 条文案的繁中/英/日翻译。 |

## 已知边界

- **程序自己停止绘制**：WGC 能捕获隐藏桌面上的窗口，但有些程序在不可见时会暂停渲染。典型的是 Chromium 系浏览器（Chrome、Edge 及基于它的应用）的窗口遮挡检测会把其他桌面上的窗口当作被遮挡；这时画面会停在最后一帧。可以把浏览器放回当前桌面，或用其启动参数关闭遮挡计算（`--disable-features=CalculateNativeWinOcclusion`）。多数游戏和播放器不受影响。
- **最小化窗口**依旧无法捕获（与之前一致，返回“窗口已最小化，等待恢复”）。
- **显示器捕获**只能捕获显示器当前显示的内容，即当前桌面；Windows 没有按虚拟桌面单独输出整块桌面画面的公开接口。要捕获某个隐藏桌面上的内容，请使用窗口捕获。
- **游戏模式**需要源窗口与 Veyra 在同一桌面（见上）。

## 验证

- 在 Linux 上用 MinGW-w64（g++ 13，`-Wall -Wextra -Wpedantic`）编译并链接了 `VirtualDesktop.cpp`，无警告。
- 其余改动依赖 MSVC、Qt、C++/WinRT、FFmpeg，**尚未在 Windows 上构建或运行**；需要在 Windows 上完成构建和以下测试。
- `veyra_screen_capture_tests`（`tests/integration/ScreenCaptureTests.cpp`）新增虚拟桌面用例：系统中有至少 2 个虚拟桌面时，测试图案窗口把自己移到另一个桌面（Windows 只允许进程移动自己的窗口），检查
  1. 窗口被 shell cloak；
  2. `targets()` 仍列出它，且带桌面编号；
  3. WGC 打开后读到真实的红/绿/蓝条纹像素；
  4. 隐藏期间 1 秒内持续出帧（>20 帧）；
  5. 移回原桌面后不再 cloaked。

  只有一个桌面时打印 `SCREEN virtual desktop cases skipped` 并跳过。运行前先按 Win+Ctrl+D 新建一个桌面，再切回原桌面运行测试。
- 手动验证建议：桌面 2 打开一个持续变化的窗口（游戏或本地播放器），桌面 1 打开 Veyra → 屏幕捕获 → 窗口，确认列表中出现 `[桌面 2] …`，开始捕获后画面持续更新；再切到桌面 2、移动窗口到桌面 3，确认捕获不中断。
