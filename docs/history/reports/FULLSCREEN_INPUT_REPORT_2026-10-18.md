# 全屏输入修复验收

修复在 `codex/fullscreen-input-20261018`；基于 AMD0.44 已完成分支 `f6c4fd9`。产品代码为 `33ec229`、`2065efb`，只改 `qml/Veyra/Main.qml` 与 `apps/veyra-qml/main.cpp`。没有合入 main、推送或发布。

## 原因和处理

专业模式的全屏按钮在播放条内。按钮先切换全屏，窗口级画面 TapHandler 随后处理同一次松开；此时画面矩形已经扩大，误认为用户点击了画面，于是暂停或恢复。基线实测同一时刻出现 `ui-fullscreen enabled=true` 和 `ui-transport pause`。

现在在按下时同时记录“是否在画面内”和“是否在浮层上”，用这个来源判定点击归属。全屏按钮不再触发画面单击；真正的画面单击仍可暂停/恢复。回归另发现双击退出全屏时，第二次画面单击会被缩小后的矩形拒绝，无法抵消第一次暂停；也移除了这处松开后重判，最终播放和暂停两种状态双击退出均保持原状态。

极简全屏原先只请求 Qt.BlankCursor。视频是 Qt 之外的原生 HWND，其窗口类有箭头指针，缺少与 Qt 隐藏策略同步的原生处理。现在 QML 与原生共享一个隐藏条件，现有 afterAnimating/snapshotChanged 跟随在状态变化时同步 SetCursor，并处理视频及其父窗口的 WM_SETCURSOR。只作用于当前前台视频区域，尊重浮层裁剪，不改其他应用的指针，不使用 ShowCursor 全局计数。打开调节面板或对话框时保留指针。

参考：[Microsoft SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor)、[WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor)。这些接口说明支持原生指针处理方式；实际成功以本机日志为准。

## 实测

Windows、RTX5070、Qt6.8.3，使用 `E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4` 和每轮独立配置。通过 computer-use 的原生鼠标/键盘操作验证，未使用旧 SendInput 脚本。

| 检查 | 结果 |
| --- | --- |
| 专业模式播放中点击全屏按钮 | 持续播放，无误暂停 |
| 专业模式暂停时点击全屏按钮 | 保持暂停，无误恢复 |
| 播放/暂停两种状态双击退出全屏 | 两次画面点击抵消，最终状态保持 |
| 画面单击暂停/恢复 | 保留 |
| 极简全屏静止隐藏、活动恢复 | 通过，包含暂停时 |
| Home 调节面板、Ctrl+L 锁定/解锁 | 通过 |
| OBS 兼容软件界面极简全屏隐藏/恢复、Home、F11退出 | 通过 |

最终 GPU 日志：`logs/fullscreen-input-20261018/gpu-pro-153358/player.log`。15:34:09 和15:34:32进入全屏，按钮操作附近均没有传输切换；双击退出两次分别出现 pause→resume、resume→pause，保持操作前状态。15:34:10.866 和15:34:34.214记录 `hidden=true overVideo=true applied=true cursorKnown=true cursorShowing=false`，实际活动后对应 `cursorShowing=true`。

最终软件界面日志：`logs/fullscreen-input-20261018/software-min-153508/player.log`。15:35:42.119记录系统指针已隐藏，15:35:52.311活动恢复，15:35:56.977打开面板恢复；15:36:05正常退出全屏。时间为台北本地时间，原始日志为UTC。操作时间、原始截图、机器校验汇总在 `E:/项目/Veyra/reports/fullscreen-input-20261018/native-actions.json` 和 `final.json`；不以截图工具可能显示的指针标记替代系统 cursorShowing 读数。

## 构建和回归

- `python E:/项目/Veyra/tmp/fullscreen-input-20261018/build.py`：5目标从零构建258.6秒通过；补齐双击后增量构建17.6秒通过。实际输入/二进制SHA见 `reports/fullscreen-input-20261018/build.json` 指向的回执。
- `stage.py`、`checks.py`：最终82项 Qt Quick、384组布局、18项真实D3D12预览几何及2449条翻译检查通过。最终回执 `logs/fullscreen-input-20261018/checks-153313/receipt.json`。
- `verify.py`：源/构建/运行目录QML一致，5个可执行文件一致，2014个共享依赖文件与原测试目录SHA一致；传输切换及系统指针状态断言通过，QML Timer数量不变，main仍为26c0d61。
- 未提高33ms/250ms刷新频率，保留1.6秒隐藏定时器；无新增Timer、update/requestUpdate、全量重绘或视频处理修改。软件界面静止窗口期已有主窗口0次/10秒呈现记录，未用它冒充NR/补帧性能测量。

首轮Quick失败是本任务staging误跳过全部qml目录、漏部署Qt模块；修正为只跳过应用Veyra模块并原样链接Qt模块后82项通过，失败日志保留在checks-152840。首次原生回归发现并保留双击退出的失败证据，补齐后用最终2065efb重新实测及自动检查。中途有点击落在已变化的播放条位置，日志未出现对应操作，未计为通过，重新观察后点击实际按钮。

## 交付与边界

本地可用构建：`E:/项目/Veyra/tests/fullscreen-input-20261018/app/veyra_qml_ui.exe`；这是独立QA目录，没有制作正式ZIP。所有测试实例已正常退出。构建、日志、私有配置、截图、临时文件均在E盘任务目录；没有写入桌面旧脏树或Claude目录，没有改驱动、专有运行时或真实用户配置。

未做AMD实卡、Moonlight/PC游戏穿透会话、多显示器混合DPI及NR/FG负载性能基准。第一轮工具发送Escape未观察到退出，不将Esc列为已验；F11和真实画面双击退出已独立验证。没有把上述未覆盖项计入本次两项修复通过证明。
