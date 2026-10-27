# 原生控件闪白修复

2026-09-10。Phase 7 仍为 `in_progress`；这是限定 UI 修复，不代表整体性能或实卡验收完成。

## 原因与修改

原有控件子类只接管 `WM_PAINT`。Windows 原生控件还会在文字、选中状态、焦点、编辑和滑条位置变化期间同步绘制，随后自定义绘制才覆盖它；`WM_PRINTCLIENT` 也会绕过旧的自定义绘制。这是原生外观可能短暂露出的具体入口，未宣称穷尽用户所有闪烁场景。

- `apps/veyra/ui/Theme.h`：更新原生控件状态期间暂时关闭原生重绘，然后统一使自定义缓冲绘制失效重画。保留原生文字、选择、通知、键盘输入和焦点模型；隐藏控件不会因恢复重绘而显示，外层批量关闭重绘的状态保留。
- 同文件：标签、按钮、下拉框、滑条及毛玻璃编辑框的 `WM_PRINTCLIENT` 使用相同自定义画法。编辑框内部用于取得字形的原生打印仍保留。
- `apps/veyra/ui/PopupSelector.h`：列表选择、滚动和焦点更新采用相同保护；列表打印也走自定义缓冲绘制。
- `tests/integration/ControlPaintTests.cpp`、`CMakeLists.txt`：新增真实 Win32 控件回归，检查绘制通知、打印像素、输入行为和可见性。
- `scripts/acceptance/ui-settings-page-scope.py`：NR 分辨率切换由固定等待 1 秒改为现有最多 4 秒的日志轮询，保留实际尺寸断言。

未修改视频纹理、GPU 处理路线、采集颜色、Windows 毛玻璃材质、SDK 或运行时。

## 实际执行

命令均从项目根目录运行。包装器为本机现有 `logs/baseline-repair-20260909/run.py`，单次外部 timeout 290 秒；测试串行。

| 命令（前缀 `python logs/baseline-repair-20260909/run.py`） | 结果 / 日志（均在该目录） |
| --- | --- |
| `build veyra veyra_control_paint_tests` | exit0，`build-090204.log` |
| `build veyra_popup_selector_tests` | exit0，`build-090604.log` |
| `build veyra_control_paint_tests` | 加入输入检查后 exit0，`build-090652.log` |
| `ui-paint-native-input out/build/x64-release/veyra_control_paint_tests.exe` | 21 PASS，`ui-paint-native-input.log` |
| `ui-paint-popup out/build/x64-release/veyra_popup_selector_tests.exe` | 14 个菜单场景 PASS，`ui-paint-popup.log` |
| `ui-paint-settings python scripts/acceptance/ui-settings-page-scope.py` | 参数即时应用、无滚轮误调、独立字段、离散按钮、还原默认 PASS，`ui-paint-settings.log` |
| `ui-paint-resize python scripts/acceptance/ui-window-resize.py` | 24 次移动、24 次缩放、4 次菜单打开，18.266 秒 exit0，`ui-paint-resize.log` |
| `ui-paint-fps python scripts/acceptance/ui-processing-fps.py` | 日常/专业/窄窗口帧率显示和暂停归零 PASS，`ui-paint-fps.log` |

实际 RTX 设置页日志：`logs/settings-page-scope-10174b8d1a724aed839a22c39bc6b856.log`。Feature18 和 DLSSG Create 均为 `0x1 (NVSDK_NGX_Result_Success)`，最终 `smoke frames=600 generated=379 failed=false nrEvaluated=609 nvofExecuted=599`。此负载反复重建增强，不能以它的 lateness 测正常稳定配置性能。

首轮设置页测试确实失败：`logs/settings-page-scope-83059d05536748ef80592896060a7e3f.log` 在固定 1 秒等待结束时仍释放旧增强资源。改成限时等待真实尺寸日志后通过；不是删除断言。其余早期 15 项绘制检查见 `ui-paint-native.log`。

构建 EXE SHA256：`E97B716B99116BEC942262FFEF1612299CBB2F4B0BDA7C308A5BFF318B3B5157`。

## 验收边界

没有实卡测试、长时间桌面录屏或中文 IME 候选输入验收，没有新增独立 Reviewer 结论。未重新执行完整 delivery，本轮证据不替代既有门禁。未修改控制面、SDK/runtime，未 push 或上传 Release。

下一条任务：重启 `Veyra.cmd`，复核原先闪白的具体操作。已验证的同步原生背景绘制入口被阻止、打印输出没有大片白底，不等于证明任何桌面环境下都绝无闪烁。既有 FRUC reset 像素与临界负载节奏问题仍开放。
