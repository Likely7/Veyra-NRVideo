# UI 修复 v3 与真实桌面毛玻璃 — 2026-09-08

本轮从本地提交 `b6b251d` 修复用户逐项复现的问题，最终采用 Windows Desktop Acrylic。此前应用内彩色渐变不符合用户要求，已删除。控制区真实透出下方桌面或其他窗口的模糊色彩，视频区不透明，未打开媒体时纯黑。

当前仍在 Phase 7 / 全局 `needs_review`。本报告是限定范围的 UI 软件修复证据，不改变旧阶段记录；用户实卡、长稳和公开分发尚未通过。最新构建 `logs/ui-repair-v3/build16.log` exit0，EXE SHA256 `B1892C51E8AFC27D7223E271D48D93107C34CAA96BC4014DFF6F45FDE0D7BF74`。

## 实际改动

- 窗口：移除自定义边缘点击时的系统白框；播放器全屏铺满显示器，隐藏专业面板，底部控制条和鼠标约1.6秒无操作后隐藏，移动唤回；退出恢复原窗口。
- 日常模式：视频占满上方，仅保留底部播放、进度、来源、字幕、音量和模式入口；精细参数及导出保留在专业模式。
- 切换：240毫秒展开/收起，可中途反向；保留同一个视频宿主、来源和设置版本，延迟中间尺寸的 swapchain 重建。
- 专业面板：固定状态区与滚动参数使用独立父窗口裁剪；控件统一缓冲绘制，状态文字仅在变化时更新，仪表低频刷新。
- NR / SR：专业开关立即发起独立事务，不被无效数值草稿阻挡；开启单项可以同时恢复总增强，失败回滚；总增强事务进行时保留重叠参数草稿。
- SR尺寸：共用按比例、偶数尺寸的3840×2160边界规划，4:3得到2880×2160；图片保存和视频导出共用规则。修复“保存SR图后立刻关增强”会丢保存请求的时序。
- 材质：通过 DWM 系统材质合成桌面背景，前景使用显式 premultiplied BGRA DIB。文字、图标、滑杆、按钮与原生 EDIT 不再各自铺不透明黑底。原生输入、选择、滚动和 IME 消息继续交给系统编辑控件。
- 空画面不放标题、提示或彩色背景。视频像素不参与桌面模糊；全屏关闭桌面材质。诊断父面板也采用同一背景。独立采集设置对话框仍是普通深色。

Desktop Acrylic 使用 `DWMWA_SYSTEMBACKDROP_TYPE=DWMSBT_TRANSIENTWINDOW`，而非仅采样壁纸的 Mica；透明客户区使用正确 alpha。最低支持 Windows 11 build22621，系统透明/高对比度策略可能使其回退。软件不改变系统设置，也不读取桌面像素。[微软材质说明](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type)、[自定义窗口 alpha 绘制说明](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)。

## 修改文件

| 文件 | 用途 |
| --- | --- |
| `apps/veyra/ui/AppShell.cpp` | 窗口边框、全屏、布局、事务衔接及材质接入 |
| `apps/veyra/ui/GlassMaterial.h` | DWM Desktop Acrylic、alpha DIB 与卡片材质 |
| `apps/veyra/ui/Theme.h` | 透明控件、文字回退、编辑框输入和绘制 |
| `apps/veyra/ui/WorkspaceChrome.h` | 仪表及工作台文字绘制 |
| `apps/veyra/ui/WorkspaceTransition.h` | 可反向模式展开动画 |
| `apps/veyra/SettingsWindow.cpp/.h` | 滚动裁剪、开关和数值草稿、应用反馈 |
| `apps/veyra/TelemetryWindow.cpp` | 诊断父背景一致性 |
| `include/veyra/pipeline/ResolutionPlan.h` | 按比例 SR 输出尺寸 |
| `src/engine/EngineController.cpp` | 图像保存与设置请求的原子取用顺序 |
| `src/engine/VideoExportJob.cpp` | 导出使用共享尺寸规划 |
| `src/engine/VideoPresenter.cpp` | 动画期间缓冲尺寸及视口映射 |
| `apps/veyra/ui/UiRepairChecks.h`、`tests/unit/UiContractTests.cpp` | 原生事件、事务、布局和尺寸检查 |
| `scripts/acceptance/ui-repair-v3.ps1`、`scripts/acceptance/ui-dual-mode.ps1` | 每次300秒以内的验收入口 |
| `tests/integration/DesktopBackdropFixture.cpp` | 180秒自动退出的独立红蓝背窗视觉测试 |
| `docs/USER_GUIDE.md`、本报告、复核报告、`docs/WORKLOG.md`、`docs/DELIVERY_STATUS.md`、`loop/STATE.json`、`loop/EVIDENCE.md`、`loop/JOURNAL.md` | 操作、真实证据与恢复点 |

## 构建与测试

实际运行命令（每次调用上限300秒）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-repair-v3.ps1 -Root .
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-dual-mode.ps1 -Root . -Case all
```

专项最终结果：`logs/ui-repair-v3/3fb66a3472264eb4abc1fc61917839a8/result.json`，11项PASS，81.001秒。真实触发原生 NR/SR BM_CLICK、NaN草稿保留、主增强冲突保护、保存2880×2160图像、窗口裁剪/全屏，以及2880×2160视频导出并解码。使用本机 RTX5070 与固定 NVIDIA runtime；不是仅编译。

运行日志包括：`snippet CreateFeature id=18 result=0x1 (NVSDK_NGX_Result_Success)`、NR/FG Evaluate `result=0x1`，SR输出2880×2160，`image-save saved extent=2880x2160 revision=6`。拒绝路径明确为 `settings-test` 注入“test-only reject NR-disable transaction ... no driver failure”，不是实际驱动报错。

双模式最终回归：`logs/ui-dual-mode/13e2d933fe66477797a71b32612af303/result.json`，15子检查PASS，141.187秒；包括设置/状态、播放及暂停时各40次切换、增强总开关与失败回滚、实际音量增益/静音、冻结参数的后台导出并解码、取消作业及无孤儿进程退出。

| 最终build16场景 | 40次切换P95 | 最大值 |
| --- | --- | --- |
| 播放 | 8.191ms | 9.963ms |
| 暂停 | 8.633ms | 9.173ms |
| 同时导出 | 8.481ms | 8.506ms |

播放切换期间442次实际提交，epoch始终1、最大提交间隔18.1116ms，无Feature创建、历史reset或重开源。GDI12→12、handles721→721、PrivateBytes1729171456→1729171456。以上指软件提交与UI命令耗时，不冒充显示扫描率或完整动画帧率。

Repair v2全套：`logs/repair-v2/joint-f3c40457ea02481c87dfce0739f51c74/result.json`，18项PASS，子进程计时合计70.478秒；保留历史累计558.1472326秒，不把累计时间当单次上限。原生4K当前合同补测：`logs/ui-dual-mode/4k-719b7f3002fa42b88abbad8ba0262137/result.json`，23项PASS，13.751秒。二者仍使用同一个最终EXE，命令如下：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/repair-v2.ps1 -Root . -Case all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-4k-regression.ps1 -Root .
```

独立只读复核通过本轮限定修复，详见 [复核记录](REVIEW_UI_REPAIR_V3_2026-09-08.md)。

## 真实视觉证据

使用本项目独立测试窗口作为播放器背后的真实桌面内容。它每30秒在红蓝之间变化，180秒退出；Veyra 不读取它的图像。

- `logs/ui-repair-v3/desktop-red-final.jpg` 与 `desktop-blue-final.jpg`：同一空闲播放器，控制区随真实背窗红蓝变化、模糊棋盘边缘，视频区域保持黑色。
- `logs/ui-repair-v3/video-red-final.jpg`：真实合成视频正常呈现，双语字幕清晰，未染上面板材质。
- 上述三张来自 build14；最终 build16 保留相同材质主路径，随后只补主题失效回退、编辑重绘消息及测试观察时序。build14的40次切换PASS：P95 7.860ms / max10.280ms，GDI12→12、handles721→721。
- 原生数值框实际键盘输入0.75、Shift+End选择和光标已检查。真实中文IME候选组合过程尚未验证，不将Unicode输入当IME验收。

最终build16实际窗口还检查了日常底部布局、F11进入/退出、控制条及鼠标自动隐藏、专业展开、Ctrl+A和数值0.75输入/复制/粘贴、参数滚动和诊断背景；日志 `logs/ui-repair-v3/final-visual.app.log`。截图：

- `logs/ui-repair-v3/acrylic-daily-final.jpg`
- `logs/ui-repair-v3/acrylic-professional-final.jpg`
- `logs/ui-repair-v3/acrylic-fullscreen-hidden-final.jpg`
- `logs/ui-repair-v3/acrylic-scroll-final.jpg`
- `logs/ui-repair-v3/acrylic-diagnostics-final.jpg`

全屏DWM日志实际为 `apiAccepted=false fullscreen=true video=opaque`，退出后恢复 `apiAccepted=true fullscreen=false`。预览为200秒自动退出的合成媒体测试，不写入用户默认配置。

## 失败记录与修复依据

1. build1/2、build9曾因旧Veyra占用EXE导致链接失败；正常结束空闲预览后重新构建成功。早期混用不同父窗口的 DeferWindowPos 导致参数缺失，已拆分父窗口批次。
2. 首次v3图像保存回归失败：保存请求与设置切换之间存在竞态，已在同一锁中取出请求，先保存当前真实输出，再应用新设置。后续专项保存尺寸验证通过。
3. `logs/ui-dual-mode/3844e92179114c289d7a076a22dcf2c8/result.json` 整轮保留FAIL；前六个场景通过，后台导出因冷启动耗时未在原14秒窗口内完成。仅导出/取消/退出作业场景改为30秒，并使用30秒合成音视频；所有断言及单次300秒上限不变。此前补测export/cancel/exit均通过。
4. 应用内模拟玻璃被用户拒绝；它的切换P95 22.117ms也超过16ms门槛（`f342c983cfe24d83b282daa04719e9a9` FAIL）。该实现已删除，改为系统桌面合成，没有放宽性能阈值。
5. build12 GDI+ LONG参数重载歧义编译失败，修正显式Rect参数后build13成功。build14移除定时器重新显示空提示的残留路径。
6. 只读复核发现缓存HTHEME失效后raw-GDI文字破坏alpha；build15增加 WM_THEMECHANGED 失效处理和覆盖率mask回退，并补原生编辑的粘贴/撤销/滚动/IME重绘。
7. `logs/ui-repair-v3/bfc72bf143ad42a2aa4e6c185e038432/result.json` 保留FAIL。回滚重建耗时约1.4秒，旧测试从点击计时500ms，在回滚结束后仅约34ms就检查250ms定时器更新的复选框。build16改为回滚完成后有界等待500ms，不改变断言。最终日志实际捕获 `appliedNR=true desiredNR=true checkboxNR=false draftPreserved=true`，随后 `checkboxNR=true draftPreserved=true`，证明旧失败是观察时序，未覆盖草稿。
8. 本轮受保护合并gate：`logs/delivery/04388a82e2424d4a8f8832daa4db83db/result.json` 36.690秒后在旧23帧断言FAIL。当前CFR合同12源帧2X含尾帧占位为24帧；build8当前合同补测 `logs/ui-dual-mode/4k-ececc320b3744d409757c49585976c58/result.json` 23检查PASS/14.417秒（H264/HEVC原生4K、120fps、0.2秒、AAC、解码）。保护脚本及断言未改，补测不替代Phase gate通过。

## 边界与下一步

本轮未开启采集流，实卡视频/音频/断连/体感延迟仍由用户验收；真实多屏DPI、长稳、物理扫描率、完整SR4K+4X性能和IME候选未通过本轮证明。全局状态仍 `needs_review`，专有二进制公开分发仍 `distribution_blocked`。根目录/运行目录NR与addon SHA256均匹配指定身份，7个保护文件hash未变，用户删除的测试视频仍未恢复。

下一条唯一任务：用户从 `Veyra.cmd` 启动，验收这版真实毛玻璃与播放器交互，再按原合同做实卡体验验收。只做本地Git存档；本次交互修复不执行历史的一次性关机请求。
