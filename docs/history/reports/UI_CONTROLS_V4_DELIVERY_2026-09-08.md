# 底栏常用操作与毛玻璃选择器交付

基线本地提交：`97ccd19`。当前仍为 **Phase 7 / needs_review**；本次完成用户要求的UI范围，整体产品门禁、实卡验收与分发边界不因这次UI更新改变。

最终程序：`out/build/x64-release/veyra.exe`，SHA256：

`7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`

## 最终行为

- 日常底栏直接显示打开、采集卡、最近文件、总增强、SR、停止/播放、音量、字幕、全屏、专业模式、最小化和关闭。文件名与时间放上行。删除齿轮菜单入口；窄窗口收起部分文字而保留所有操作。720px实际检查无重叠，SR文字完整。
- 图标采用Lucide，24个用途映射到23份SVG，统一线宽与圆头矢量绘制。固定上游版本 `a537cb6eb323b885f4c60baf3cec1a995982d167`，原文件与完整ISC / Feather MIT许可证保留。程序运行时无需字体、下载或额外图标库。
- 字幕与全部应用内下拉框共用深色Desktop Acrylic弹层：圆角、细边、橙色当前值与勾选，方向键/首尾键、回车、Esc、正反Tab、滚动、外部关闭与焦点恢复。保留原生LISTBOX可访问名称与COMBOBOX通知契约。异步刷新或同步通知取消/销毁不会提交失效选项。
- 底栏SR与专业面板共享设置处理；总增强关闭后SR显示关闭，点击SR可以恢复总增强，非法数值草稿不阻挡开关也不会被提交。
- 保留真实桌面毛玻璃和不透明视频。未打开媒体时视频区纯黑；专业展开动画、视频全屏和原有播放/导出处理保持原合同。Windows文件对话框、独立采集设置窗口的外框仍采用系统/普通深色界面，其下拉选择已共用新弹层。

## 修改文件

- `apps/veyra/ui/AppShell.cpp`：底栏、专业来源栏、共享SR路由、字幕弹层、唯一HWND布局批次。
- `apps/veyra/ui/Theme.h`、`PopupSelector.h`、`TransportLayout.h`、`LucideIcons.h`：控件绘制、弹层行为、响应布局和矢量路径。
- `assets/icons/lucide/*`、`scripts/generate-lucide-icons.py`、`THIRD_PARTY_NOTICES.md`：固定源SVG、许可证、文件身份与可重建生成器。生成器开发依赖fonttools4.64.0；没有把该依赖带入播放器。
- `tests/unit/UiContractTests.cpp`、`tests/integration/PopupSelectorTests.cpp`、`apps/veyra/ui/UiRepairChecks.h`、`CMakeLists.txt`、`scripts/acceptance/ui-controls-v4.ps1`：窄布局、14场景原生弹层交互、实际底栏按钮与SR恢复检查。
- 本报告、独立复核记录、使用指南、WORKLOG、DELIVERY_STATUS与loop记录：实际结果及延续状态。

## 实际命令与结果

每次测试调用均小于300秒；累计测试时间不是单轮上限。

| 命令 | 结果与证据 |
| --- | --- |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release` | 最终构建成功，`logs/ui-controls-v4/build-final.log`。 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-controls-v4.ps1 -Root .` | 最终EXE，1.312秒PASS：固定图标/许可身份、各宽度布局、14个原生弹层交互场景。`logs/ui-controls-v4/b03d18f21e404b7a9106c133ea05395f/result.json`。 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-dual-mode.ps1 -Root . -Case switch` | 最终EXE，15.008秒PASS，40次模式切换。`logs/ui-dual-mode/e0a09d4eef9541edb9607853133bae26/result.json`。 |
| `veyra.exe --smoke-seconds 25 --nr --no-sr --no-fg --smoke-repair-ui logs/ui-repair-v3/cabd46022aa244d2a521bea6807773fd/aspect-4x3.png` | 最终EXE，25.858秒、exit0：实际原生NR/SR点击、NaN草稿、总增强、底栏所有控件可见与互不重叠、滚动和全屏。实际命令中媒体绝对路径已加引号，见`logs/ui-controls-v4/native-sr-final.json`及`.app.log`。 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-repair-v3.ps1 -Root .` | 最后12DIP排版微调前的EXE `AAEF44C6…795E5F1`，80.91秒、11命令PASS，包括图片/视频SR、失败回滚与2880×2160导出解码。`logs/ui-repair-v3/cabd46022aa244d2a521bea6807773fd/result.json`。 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-dual-mode.ps1 -Root . -Case all` | 同一排版微调前EXE，141.503秒、15命令PASS，包括切换/暂停、总开关与回滚、音量、冻结设置导出、取消和退出。`logs/ui-dual-mode/abe2b4a697e344a3aa6a8825081a196c/result.json`。 |

不是只编译：本机NVIDIA实验运行时已实际执行。最终`native-sr-final.app.log`记录 `CreateFeature id=18 result=0x1 (NVSDK_NGX_Result_Success) handle=non-null seh=0`；SR `Evaluate #1 result=0x1`；结束 `failed=false nrEvaluated=5`。底栏SR恢复与专业开关同步、NaN草稿保留断言为true。视频和导出证据分别在上述80.91秒专项及141.503秒全套日志；静态图片不冒充视频/NVOF或采集卡实测。

## 实际窗口检查

使用Computer Use操作真实程序，未用网页模型代替原生窗口。检查底栏入口、720px缩窄、专业面板、字幕及NR档位弹层、方向键高亮、回车确认和Esc取消。

- `logs/ui-controls-v4/daily-final.jpg`：最终EXE，宽窗口底栏。
- `logs/ui-controls-v4/daily-720-final.jpg`：最终EXE，720px窗口，SR完整可读。
- `logs/ui-controls-v4/subtitle-popup.jpg`、`professional-selector.jpg`：本轮早期同样式构建的弹层截图；后续改动为取消/焦点行为及SR宽度，不能把这两张标成最终EXE抓图。
- `logs/ui-controls-v4/visual-delivery.app.log`：最终实际窗口，字幕弹层打开与取消，180秒预览正常自动退出；预览不保存用户偏好。

## 失败、复核与剩余边界

保留早期失败日志：一次非Unicode测试目标的IDC_ARROW类型不匹配已改为明确宽字符资源；预览占用EXE造成的链接失败经正常关闭/限时预览结束后重建解决；首次实际布局缺少新增按钮，原因是同批重复HIDE/SHOW；弹层Tab测试最初因STARTUPINFO隐藏了测试窗口失败，显式显示测试fixture后原断言通过；720px的SR省略已增加12DIP宽度修复。未改断言自我放行。

[独立只读复核](REVIEW_UI_CONTROLS_V4_2026-09-08.md)核对源码、许可与真实14场景结果，限定范围未发现未关闭P0/P1/P2。最终12DIP排版调整另经主Agent测试与实际窗口复核。

七个保护文件与既有基线全部相同，见`logs/ui-controls-v4/protected-after.json`；两个指定二进制与staged NR哈希相符。没有改保护门禁、SDK、runtime或用户已删除的媒体。没有上传、发布或打包。本轮没有开启采集流；实卡、长稳、多屏物理DPI/IME候选及分发仍沿用既有未验收边界，旧delivery gate失败没有被本次UI测试消除。

下一条唯一任务：用户从`Veyra.cmd`验收新的直接操作底栏和统一选择器，并按原合同完成实卡体验验收。
