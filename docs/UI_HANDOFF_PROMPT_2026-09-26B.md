# 交接提示词（2026-09-26 第二轮，复制下面横线内全部内容给新 Agent）

---

Veyra DLSS Video Player 的前端迁移 + 后端升级，接手继续做。下面是完整的现状、环境、坑和红线，不要跳过任何一节。

## 一、你在哪

- 仓库：`C:\Users\123\Desktop\Veyra DLSS Video Player`
- 分支：`codex/ui-qml-migration-20260925`
- 当前 HEAD：`536e7fe`（标签 `checkpoint/ui-mig-wip-handoff-2`），工作区干净
- 阶段标签：`checkpoint/ui-mig-g0.7` / `-g1.6` / `-g2.5` / `-g3.1`
- **开工前必须完整读**：`AGENTS.md`（红线，最高优先级）、`docs/UI_FULL_GOAL_PLAN_2026-09-26.md`（本次施工计划，§0 是续接指针表）、`docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md`、`docs/UI_MIGRATION_EXECUTION_2026-09-25.md`、`docs/WORKLOG.md` 尾部
- 设计稿是唯一权威：`prototypes/ui-redesign-2026-09-25/`，17 屏清单在 `board.js` 的 `FRAMES`

## 二、当前进度（大约 20/50 步）

| 阶段 | 状态 |
|---|---|
| G0 工具与基线 | 完成 |
| G1 基础（缓动/背景/字体/图标/组件动效） | 完成 |
| G2 外壳（dock/页面切换/视频窗同步/拖放快捷键/全屏控制窗） | 完成 |
| G3.1 首页 | 完成 |
| **G3.2 极简页** | **做到一半，未提交验收**（见第四节） |
| G3.3 专业页 / G3.4 节点页 | 未开始 |
| B1–B5 后端升级 | **完全未开始** |
| G4 其余屏 / W 收尾 | 未开始 |

## 三、第一优先级：修「界面完全不显示，只有一黑条」

用户实机运行后报告：**UI 完全不显示，只有一条黑色横条**（他给了截图，那条黑条就是 `CineBar` 药丸）。

这没定位、没修。**这是你接手后的第一件事，在它修好之前不要推进任何功能步。**

已知的架构事实，按嫌疑排序：

1. **视频是独立原生 D3D12 子窗口，画在整个 QML 场景之上**（airspace）。`apps/veyra-qml/main.cpp` 把它创建成 `WS_CHILD`，几何由 QML 里名为 `videoHost` 的 item 驱动。如果它的区域/矩形算错，一块不透明黑面会盖住整个 QML 层——而 `CineBar` 恰恰是独立顶层窗口（`FullscreenBar`），会「活」在黑面之上，**和用户截图完全对得上**。
2. `syncVideoCovers()` 里的 `holes.isEmpty()` 分支会调 `SetWindowRgn(g_video, nullptr, TRUE)`，即**取消挖区、把视频窗恢复成整块矩形**。我这一轮为极简页新加的 cineIn 内缩逻辑（`insetBand`）也在同一个函数里，是**最新的改动，也是最新引入的嫌疑**。先在这两处打断点/加日志。
3. 根 `Window` 是 `color: "transparent"` + `Qt.FramelessWindowHint`。如果 QML 场景这一层没合成上，剩下的就只有原生视频窗 + 独立顶层药丸。
4. 视频窗只在 `setPreOpenHook` 里 `ShowWindow(SW_SHOWNA)`——也就是说**没打开片源时它不该可见**。如果它在无片源时就已经显示并且是黑的，那第 2 条的路径就是重点。

复现方式：按**正常用户路径**启动（**不要**加 `--reduced-motion`、`--page` 等测试开关），打开一个本地视频文件，看界面还在不在。

## 四、G3.2 做了什么、卡在哪

已做（`536e7fe`，未验收）：

- `qml/Veyra/CineBar.qml`：从极简页抽出的播放药丸组件，极简页与全屏共用（`cc`/音轨/预设菜单、传输键、进度轨、peek 时间条）。
- `qml/Veyra/Main.qml`：`barBelow` 从 92 改成 46（设计稿 `BAR_BELOW = 46`），药丸靠独立顶层窗压在画面下边缘。
- `qml/Veyra/MinimalPage.qml`：cineIn 载体（`objectName: "videoInset"`，只带 `frac` 值不绘制）。
- `apps/veyra-qml/main.cpp`：cineIn → 视频窗**区域**内缩（不动尺寸，避免重建 swapchain）、优雅退出、关闭时刷日志、`--data-dir` / `--exit-after` 测试开关。

卡住/未验证：

- cineIn 动效**没有一条日志证据**证明 `cineIn inset band=...` 那行真的输出过。
- 悬停/动效类目视验证全部是「未执行」（原因见第五节）。
- `--exit-after` 我这边跑起来「看起来不退出」（PowerShell 对 `Start-Process` 对象的 `HasExited` 不可靠），优雅退出刷盘路径**未证实**。

## 五、环境与工具链（照抄，别重新摸索）

**构建（QML 端）**

```bash
powershell -NoProfile -File scripts/build-qt-probe.ps1 -Targets veyra_qml_ui
```

Qt 6.8.3 在 `E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64`（在源码 Git 外）。产物在 `E:\项目\Veyra\build\qt-probe-20260926`。

**构建（引擎端，B 阶段用）**：`scripts/build-ui-migration.ps1` → `scripts/stage-ui-migration.ps1`

**同步到运行目录**（每次改完 QML 必须跑，否则看到的是旧界面）

```bash
powershell -NoProfile -File "E:/项目/Veyra/tmp/ui-qml-migration-20260925/sync.ps1"
```

把 exe 和 `qml/Veyra/*` 复制到 `E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app`。

**截图**

```bash
powershell -NoProfile -File "E:/项目/Veyra/tmp/ui-qml-migration-20260925/shoot.ps1" -Step <步骤> -Frames "f-min,f-min169"
```

证据落在 `E:\项目\Veyra\logs\ui-qml-migration-20260925\goal\<步骤>\`。另有 `tools/qt_probe/capture-window.ps1`（PrintWindow `PW_RENDERFULLCONTENT=2`）、`tools/qt_probe/hover-shot.ps1`（真实指针悬停）、`tools/qt_probe/compose-compare.ps1`（左设计右实现对并）。

**三个会让你白干半天的坑**

1. **`PrintWindow` 拍不到原生视频窗**——它在离屏 DC 里永远是黑的。**这是采集限制，不是界面坏了**。但你绝不能因此忽略第三节那个报告：那张全黑图和你用户看到的「一黑条」长得一样，必须靠实机/区域矩形日志区分。
2. **日志是 64 KB `_IOFBF` 缓冲**（`src/base/Log.cpp`），只在 Warn/Error、缓冲满 64 KB、或距上次写 ≥250 ms 时刷盘。**进程被 `Stop-Process -Force` 杀掉时尾部日志全丢。** 我实测过：同一轮运行 stdout 重定向有 4 行、日志文件只有 3 行，丢的正是关键那行 `video host geometry 1280x536 at 0,0`。日志路径用环境变量 **`VEYRA_LOG_FILE`** 覆盖（注意不是 `VEYRA_LOG`）。**建议直接用 stdout 重定向拿证据，不要在这上面继续投入。**
3. **显示器经常在休眠**（实测 `dwm-vblank.ps1` → `refreshes/s=1 composed/s=1`）。休眠期间 Qt Quick 不投递 hover、不出帧，**所有动效和悬停验证都做不了，只能写「未执行」**。这是上一轮进度慢的主要原因之一。如果新环境能保证屏幕亮着，效率会完全不同。

**门槛脚本（B 阶段每步必跑，判据见计划 §1）**：`scripts/hash-ui-migration.ps1`（17 项哈希）、`scripts/perf-ui-migration.ps1`（提交 P95 不劣化 >10%）、`scripts/gates/delivery.ps1`（PASS）、`scripts/run-unit-ui-migration.ps1`（基线 pass=44 fail=33 skip=3）。单个测试 ≤ 300 秒。

## 六、红线（违反即停工）

- **不 push、不合并 main、不发布、不做 GitHub Release。** 这些要用户在当前对话另行明确授权。
- **不画假控件**：引擎没有的能力，界面写「尚未接入」或置灰，不能画一个点了没反应的按钮。
- **一次只改一个变量**，改完必须看图再判断。**未跑的写「未执行」**，不许写「应该可以」。
- 每完成一步：构建 → 运行 → 截图 → **看图** → 与设计稿并排比对 → 提交 → 打标签 `checkpoint/ui-mig-<阶段>.<步>` → 更新计划 §0 表 + 执行记录 + `docs/WORKLOG.md`。
- **所有 Agent 产生的构建/测试/日志/临时文件写 `E:\项目\Veyra\`**，不许散落桌面、Downloads、C 盘根目录或系统 Temp；只为子进程设 `TEMP`/`TMP`，不改全局环境。
- `nvngx_dlssnr.dll` 身份不符立即停工；不得联网找「更新偷跑版」、不得修改/重签名/提交 Git。`renodx-dlss5-1.addon64` 不得加载/注入/链接/分发。
- NVIDIA / Intel 运行库**只允许进程内修改**，不改磁盘文件、不重签名、不伪装身份。
- PSN 凭据只 DPAPI 加密存用户数据目录，不进 Git。
- 正常播放/采集/导出路径禁止 GPU→CPU 回读。
- 移植第三方代码必须逐项标注来源仓库、固定提交、许可证、改动文件，写进 `THIRD_PARTY_NOTICES.md`。
- 提交信息结尾加 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。

## 七、上一轮为什么慢（避免重蹈）

1. **我花了很多轮在修「证据链」而不是产品**：日志刷盘、`--exit-after`、截图脚本。这三样都只是测试便利。**stdout 重定向已经能拿到证据，别再往这里投时间。**
2. **我一直在原生视频窗口的边缘打补丁**（HREGION 挖洞、`SetWindowRgn`、把药丸做成独立顶层窗），没有正面解决「两块窗口争一层」的架构矛盾。第三节那个 bug 很可能就是这条链的账。**如果你判断需要重构窗口分层，就正面做，不要继续叠补丁。**
3. **同时追三条证据线（截图 + 日志 + 并排比对）**。每步只留**一条能自动判定的证据**。

## 八、验证纪律

任何一步声称「完成」时必须同时给出：构建命令与产物、实际运行过的测试命令与原始输出、截图路径且你**看过图**、未执行项明确写「未执行」。只写接口桩、只编译未运行、只显示理论帧率、用 manifest 代替 provider、把重复帧冒充补帧、把软件驻留减少冒充屏幕延迟降低——**都不算完成**。不得把本机 RTX 5070 的验收扩展成其他型号或实卡结论。

---
