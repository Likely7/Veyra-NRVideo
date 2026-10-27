# 全屏点击与鼠标隐藏修复

用户要求同时修复专业模式点击全屏误暂停/恢复、极简全屏指针不隐藏。隔离分支 codex/fullscreen-input-20261018，基于已完成 AMD0.44 分支 f6c4fd9；不改 main 或 Claude 正在施工的分支，不发布。

## 已复现

- 独立私有配置 GTA4K 播放，专业模式点击全屏按钮，同一时刻出现 ui-fullscreen enabled=true 与 ui-transport pause。按下在播放条，松开时全屏使画面几何扩大，窗口级 TapHandler 误将其视为画面点击。
- fullControls=false、Qt.BlankCursor=10 后实际屏幕仍可见箭头。Qt 画面层与原生视频 HWND 的指针策略没有同步。
- 基线证据 E:/项目/Veyra/logs/fullscreen-input-20261018/baseline；仅基线 staging 添加观察探针，生产 QML 不添加观察 Timer。

## 最小修改

1. 画面 TapHandler 按下时记录是否在画面内，按钮改变布局不再改变该次点击的归属。保留画面单击暂停、双击全屏及串流捕获的既有行为。
2. 统一 QML/原生视频的隐藏条件，用现有 afterAnimating/snapshotChanged 跟随及 WM_SETCURSOR 同步。只影响当前前台视频区域，尊重浮层裁剪，退出全屏/显示控件恢复；不使用全局 ShowCursor 计数。
3. UI33ms/250ms、1.6秒自动隐藏不变，不新增播放 Timer/requestUpdate/全量重绘，不改媒体/效果/调度。

## 验证

限并发独立构建、Qt Quick/布局/预览几何回归；Windows 原生真实鼠标检查专业模式播放与暂停两种状态进出全屏、极简全屏空闲隐藏/活动恢复、菜单/锁定及退出。记录操作后的真实 OS cursorShowing 与传输日志，不能只用 QML 请求值充当通过。正常/软件 UI 在可行范围分别验证。所有产物 E:/项目/Veyra 对应 fullscreen-input-20261018 目录；单测试≤300秒、构建≤900秒。未覆盖硬件/输入须明示。
