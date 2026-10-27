# 采集卡直播窗口标题修复（第三方窗口捕获“识别不到 Veyra”）

## 结论

采集卡来源（`capture:` / `capture2:` 连接串）被当成文件名写进了主窗口标题，导致 Veyra 在采集卡运行时标题变成一个 776 字符的十六进制设备路径串。任务栏、Alt+Tab 和第三方直播工具的窗口列表都只能看到这串机器码（直播伴侣自己还把标题截断到 259 字符再使用），用户在主窗口列表里无法辨认 Veyra，从而“还没抓就先开采集卡就抓不到”，而“先抓到窗口再开采集卡就一直正常”。

本修复只改窗口标题与回归依据，不改采集、颜色、换链和呈现路径，也不改 `capture:`/`capture2:` 连接串格式。

## 实测证据（本轮，用户实机）

1. 采集卡运行中的 Veyra 主窗口标题（Windows 进程实测，`Get-Process ... MainWindowTitle`）：

```text
Veyra — capture2:005C005C003F005C...005C0067006C006F00620061006C:0:0:0040006400650076006900630065003A0063006D003A...0077:0
长度 776 字符（十六进制编码的视频设备路径 + 音频端点路径）
```

2. 同一进程在“没打开任何来源”时标题为 `Veyra — 本地实验版`（14 字符），播放文件时为 `Veyra — <文件名>`；只有采集卡来源会变成机器串。

3. 直播伴侣自身的窗口捕获回调日志（`%APPDATA%\webcast_mate\biz-logs\2026-09-15-21-42-14.log`）在添加 Veyra 来源时记录到：

```text
[window capture] compatibleGameCallback {"windowClass":"VeyraApp","windowExe":"Veyra.exe",
"windowTitle":"Veyra — capture2:005C005C003F...007B00360035006500380037003700330064002D00",
"captureType":"WindowRTVisual2D","errorCode":"0x00000000"}
```

该字段只有 259 字符，末尾停在半个十六进制字符上，说明工具侧对标题做了固定长度截断后再参与来源命名/识别；其包内前端代码（`resources/app/app.content/resource/js/52682.*.js`）用 `window.EnumWindows` 结果生成来源名 `${exe} ${title}`，并按 `hwnd`/`pid` 回绑。

4. 同一场会话的 `mediasdk_server` 日志显示：本轮“采集卡已在运行，再添加 game 来源”时，注入 hook 实际走通（`Load Shared Texture Success, size: 842 x 494, flip: 0`、`OnAutoSwitchMode from Window to Game`，之后 `GameSource` 每 10 秒 profile 连续 60 秒以上有数据）。因此“画面内容无法被捕获（全黑）”不是本轮的首要根因，标题不可辨认才是与现象一致的差异。

## 代码根因与修复

- 根因位置：`apps/veyra/ui/AppShell.cpp` 的 `openFile()` 无条件执行
  `SetWindowTextW(mainWindow, (L"Veyra — " + std::filesystem::path(file).filename().wstring()).c_str())`。
  对采集卡来源 `file` 是连接串，`filename()` 返回整串连接串；PS5 串流有单独标题，所以只有采集卡中招。
- 修复：新增纯函数 `apps/veyra/ui/SourceTitle.h::windowTitleForSource()`：
  - `capture:` / `capture2:` → `Veyra — 采集卡 · LIVE`（与软件内 `MediaTitle` 文案一致）
  - `remoteplay:` → `Veyra — PS5 Remote Play`
  - 其它 → `Veyra — <文件名>`；空串 → `Veyra — 本地实验版`
- `AppShell::openFile()` 改为调用该函数，其它行为（引擎打开、最近打开记录、字幕查找）不变。

## 验证（实际执行）

```powershell
cmd.exe /c out\build\veyra-build-x64-release.cmd        # exit 0，4/4：UiContractTests、AppShell、两次链接
out\build\audio-continuity-repair-20260915\veyra_ui_contract_tests.exe <临时目录>   # PASS，exit 0（新增标题合同断言）
out\build\audio-continuity-repair-20260915\veyra.exe --smoke-seconds 6 capture:9:0:-1:0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\gates\delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915
```

- 新构建运行时标题实测：`Veyra — 采集卡 · LIVE`（18 字符）。
- 对照组 1.3.0 便携版同一命令：`Veyra — capture:9:0:-1:0`（原始连接串直接进标题，确认修复前后差异）。
- 采集卡实机（用户当前 1.3.0 会话）修复前：776 字符（见上）。
- delivery 短测 23/23 PASS、59.633 秒，`logs/delivery/7283c292af9e471bbba9c4bc8b0316af/result.json`（`status=software_short_gate_passed`、`capture=awaiting_user_capture_test`）；本构建 EXE SHA256 `0F1DEB29D80E80264CC5EE1D6D210D9415DEBC68EBA47DD22A1C73500D5700A4`。
- 说明：smoke 退出码为 1，原因是伪造设备索引 `capture:9:...` 没有帧，smoke 判定条件要求 `frames>0`；本轮 smoke 只用于读取真实窗口标题，不作为采集/播放入证。

### 真实采集卡运行复测（同机同设备，修复后）

用本机实体采集卡（`\\?\usb#vid_345f&pid_2131&mi_00...` + `@device:cm:{33D9A762...}\wave:{73A0923C...}`，即用户会话同一连接串，重建为 `capture2:` 形式）跑修复后的候选 EXE：

```powershell
out\build\audio-continuity-repair-20260915\veyra.exe --smoke-seconds 12 <capture2:连接串>
# t+4s / t+9s 实测 MainWindowTitle = Veyra — 采集卡 · LIVE（18 字符）
# exit = 0
```

`logs/veyra-app.log` 对应片段：

```text
[capture] configured 1920x1080 nominalFps=60.000 upstreamSubtype=0x32595559 formatHr=0x0 ... audioDevice=0
[present] present-sink: window 1280x712 swapEffect=flip-discard buffers=3 vsync=0 tearing=1 captureCompatible=false
[capture] Run hr=0x0 actual=1920x1080 nominalFps=60.000
[app] smoke frames=685 generated=0 failed=false ... capture=true processedFps=60.00 callbackFps=60.01 captureDropped=0
```

即：修复后采集卡真实出帧 685 帧 / 约 11.5 秒、0 丢弃、无失败，且整个过程窗口标题为可读短名。

## 未验证与边界

- 未直接驱动直播伴侣/OBS 的 UI 复测“先开采集再添加来源”是否出画；本机没有可用的第三方应用 UI 自动化，工具侧结论来自其自身日志与包内前端代码，属 Veyra 不可控一侧。
- 不宣称第三方工具一定恢复正常：如果某个工具仍按标题长度或内容做额外限制，Veyra 无法覆盖；本修复消除的是 Veyra 自己制造的机器串标题。
- 未推送、未发布；改动仅在工作区（`apps/veyra/ui/SourceTitle.h` 新增，`AppShell.cpp`、`tests/unit/UiContractTests.cpp` 修改），候选 EXE 位于 `out/build/audio-continuity-repair-20260915/`。

## 下一步唯一任务

用该候选构建，在同一采集卡场景下分别用直播伴侣的“窗口捕获”和“游戏捕获”各添加一次 Veyra，记录：列表里标题是否为 `Veyra — 采集卡 · LIVE`、是否出画、出画延迟量级。若窗口捕获仍无画面，下一步做 WGC 内容对照（同源三方对照），必要时再查换链/组合路径，而不是先调画质参数。
