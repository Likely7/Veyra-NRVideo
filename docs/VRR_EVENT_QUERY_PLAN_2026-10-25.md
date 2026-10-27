# 2026-10-25 可变刷新/刷新率改为事件触发查询（Claude）

用户审查补帧链路后决定：VRR 状态不再定时查询，改为事件触发，并在“可变刷新”旁加“重新检测”按钮；引擎和界面都去掉 2 秒轮询。从 main 68def10 开 `claude/vrr-event-query-20261025`，起点标签 `checkpoint/pre-vrr-event-query-20261025`，开工状态 `archives/claude-vrr-event-query-20261025/start.json`。单 Agent、全 E 盘、复用 `build/claude-screen-recording-20261022`（-j2、≤900 秒）、单测试 ≤300 秒；不改运行库/模型/驱动/真实配置，不 merge/push/Release，Lecram 暂停。

## 现状

- 2.0.8–2.1.0：播放线程每 2 秒同步调用 NVAPI 查 VRR（本机 10.5–22 ms），与 VRR 开关无关。038beac 已改到后台执行，但仍每 2 秒查一次。
- 界面不播放时，`QmlPlayerBridge::updateRunStatus()` 每 2 秒在界面线程同步调用 `gfx::queryVrr`。
- 每次新建呈现器或处理图重建都把 VRR/刷新率清零：先按“未知”配置一次，结果回来后再重配一次。

## 方案

1. 新增进程内共享的 `engine::DisplayStateService`（由 `EngineController` 持有，呈现器和界面共用）：按显示器缓存刷新率与 VRR 状态；后台一次只跑一个查询，其余排队；读取从不等待，只有开播首次配置可等待 ≤100 ms。不加任何定时器。
2. 查询时机：
   - 软件启动后界面第一次刷新状态（空闲，主窗口所在屏）。
   - 新建呈现链（开播）：先用该屏缓存立即配置，同时后台核对一次；没有缓存时等 ≤100 ms。处理图重建（沿用同一交换链）保留当前状态，不查询。
   - 窗口换到另一块屏：用该屏缓存，没有才查。
   - 显示配置变化（`WM_DISPLAYCHANGE`：分辨率、刷新率、HDR、插拔屏幕）、打开“可变刷新”开关、点“重新检测”：全部状态作废，正在用的屏各查一次。
   - 可变刷新呈现已开启但驱动尚未确认生效：开启后、或画面尺寸变化（进出全屏）停止 3 秒后补查一次，只为把状态更新为“生效中”。
   - 查询失败（NVAPI 返回错误、查询期间窗口换屏）：2 秒后最多自动重试 2 次，之后等下一次事件或按钮。
3. 界面：去掉空闲时的 2 秒同步查询，改读共享缓存。“可变刷新”旁加图标按钮（刷新图标，提示“重新检测”，检测中转圈），专业页“显示”与节点页输出框各一处。开关仍在屏幕不支持时变灰，按钮始终可用；手动重新检测完成后弹出结果提示。
4. 日志：每次实际查询记一行原因（ui、session-start、monitor-change、display-change、manual、vrr-confirm、retry）和耗时，不再每 2 秒一行。

## 验证

- CPU 单测（`veyra_async_display_query_tests`）：缓存命中不查询、作废后每屏只查一次、同屏在途不重复、排队、失败限次重试、有界等待、换屏不套用旧屏状态。
- 构建 `veyra_qml_ui` 与相关测试目标；运行异步查询、实时预览、UI 合同、Repair 合同、翻译测试，以及 `scripts/i18n/extract.py --check`。
- 私有配置实际运行（确认没有其他 Veyra 进程）：播放本地视频 ≥30 秒，日志里的查询次数不随时间增长；点“重新检测”、切换开关各只查一次。本机屏幕驱动报告不支持 VRR，真实 VRR 是否生效仍需用户在支持 VRR 的设备上验证。

产物：`E:/项目/Veyra/{tests,logs,tmp,reports,archives}/claude-vrr-event-query-20261025`；构建复用 `build/claude-screen-recording-20261022`。
