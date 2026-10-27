# 底栏与选择器 UI v4：独立只读复核

复核者：现有独立子 Agent `/root/review_ui_v3`（Planck）。主 Agent 是唯一写入者；本文件由主 Agent 整理其真实返回结论，不冒充 Reviewer 亲自执行过测试。

范围：`AppShell.cpp`、`Theme.h`、`PopupSelector.h`、`TransportLayout.h`、Lucide SVG/生成器/许可证，以及本轮测试证据。阶段仍为 Phase 7 / `needs_review`。

首轮指出的缺陷均已修正：

- 总增强关闭后，底栏 SR 不再显示待恢复的缓存值；显示实际关闭状态，点击可恢复总增强。
- `CB_SHOWDROPDOWN(FALSE)` 能关闭自定义弹层；父窗口在 `CBN_DROPDOWN` 回调中同步取消时，也会在建窗前终止。
- Tab / Shift+Tab 取消后分别移到下一 / 上一控件；外部窗口获得焦点时不抢回焦点。
- 字幕选项名称包含“已开启 / 已关闭”，辅助技术能读取当前状态。
- SR 提示从不准确的“最长边4K”改为“按比例提升，最高3840×2160”。

实际窗口另发现同一个 HWND 在一次 DeferWindowPos 中重复收到 HIDE/SHOW 会保留隐藏标志。布局现在先收集每个 HWND 的最终意图，再各提交一次。Reviewer 核对该修正，720px 日常及最窄专业布局未发现重叠。

Reviewer 最终原文结论：“当前源码限定审查通过，未发现未关闭的 P0/P1/P2。”其核对的 EXE 为 `AAEF44C6A860EA1975B5A91A41641716A089E06C361F7D7D7542B0217795E5F1`，对应14个弹层场景通过记录 `logs/ui-controls-v4/d24c191e9d9e400fa59e3760da470a97/result.json`。测试覆盖正反 Tab、长列表、Esc、程序取消、同步通知取消/销毁、异步刷新及 combo/owner 销毁。

之后唯一产品改动是窄窗口 SR 按钮宽度由48改为60 DIP，避免文本省略。最终 EXE `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514` 重新通过布局/14场景弹层、40次模式切换和原生 NR/SR 控件专项，并实际检查720px窗口。此12 DIP调整及最终截图由主 Agent复核；不冒称此前Reviewer已看过此后版本。

Lucide 的24个映射来自23份原始SVG，另有完整LICENSE；Reviewer逐项核对manifest共24个文件哈希，ISC / Feather MIT通知完整。本轮未取得或宣称Phase7整体通过。
