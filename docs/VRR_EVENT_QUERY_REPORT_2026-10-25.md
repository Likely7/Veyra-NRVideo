# 2026-10-25 可变刷新/刷新率改为事件触发查询（Claude）结果

分支 `claude/vrr-event-query-20261025`，起点 main 68def10（标签 `checkpoint/pre-vrr-event-query-20261025`），功能提交 5a80948。方案见 `docs/VRR_EVENT_QUERY_PLAN_2026-10-25.md`。没有 merge、push、Release，没有改运行库、模型、驱动或真实配置，Lecram 暂停。

## 改了什么

- 新增 `engine::DisplayStateService`（`include/veyra/engine/DisplayStateService.h`、`src/engine/DisplayStateService.cpp`），由 `EngineController` 持有，各会话的呈现器和界面共用。按显示器缓存刷新率与 VRR 状态；后台一次只跑一个查询，其余排队；`request()` 在该屏状态仍是当前代时什么都不做，所以每帧、每次状态刷新调用都只是一把锁加一次查表。
- 呈现器（`VideoPresenter`）去掉 038beac 的每 2 秒后台重查：
  - 新建呈现链时先用缓存立即配置，同时后台强制核对一次；没有缓存才最多等 100 ms。
  - 同一交换链的处理图重建保留状态，不再清零后重配两次。
  - 换屏时不套用旧屏的状态；新屏有缓存就直接用。
  - 可变刷新呈现开启后，或画面尺寸变化（进出全屏、改窗口大小）停止 3 秒后，补查一次。
- 界面（`QmlPlayerBridge`）：
  - 去掉不播放时每 2 秒在界面线程同步调用 `gfx::queryVrr`，改读共享缓存。
  - 原生事件过滤器收到 `WM_DISPLAYCHANGE` 时作废缓存，打开开关时也作废一次。
  - 新增 `recheckDisplay()` 与 `vrrChecking`；重新检测完成后弹出结果提示，5 秒没有答复则提示未完成。
- QML：专业页“显示”和节点页输出框，在“可变刷新”开关旁加刷新图标按钮，检测中显示转圈。现在只有开关在屏幕不支持时变灰，按钮始终可用。
- 翻译：新增 3 条（重新检测、已重新检测：%1、重新检测没有完成，请稍后再试），繁中/英/日齐全。`scripts/i18n/extract.py --check`：2667 条，0 缺译，0 占位符问题。
- 失败处理：NVAPI 返回错误、查询期间窗口换屏或抛出异常时，同一代内间隔 2 秒最多自动重试 2 次，之后等下一次事件或按钮。

## 构建与测试

- 构建：`py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task claude-vrr-event-query-20261025 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.0-vrr1 veyra_qml_ui veyra_async_display_query_tests veyra_realtime_preview_tests veyra_presentation_worker_tests veyra_ui_contract_tests veyra_repair_contract_tests veyra_ui_i18n_tests veyra_qml_quick_tests`，190 秒通过，-j2。日志 `logs/claude-vrr-event-query-20261025/build-20261026-001118`；`veyra_qml_ui.exe` SHA256 `cc35f381121023d166d955e673970771c6271a7747a1848dd334401fcdf2e062`。构建后没有再改产品源码，测试的程序就是 5a80948 的内容。
- 定向测试（`tmp/claude-vrr-event-query-20261025/run_tests.py`，结果 `reports/claude-vrr-event-query-20261025/tests.json`）：
  - 异步查询：33 项通过（原有 10 项 + 新增 23 项：缓存命中不查、作废后每屏只查一次、在途不重复、排队、强制核对只跑一次、限次重试、换屏结果丢弃后重试、异常不卡住、有界等待、析构只等在途查询）。
  - 实时预览、呈现线程、UI 合同（含 384 布局）、Repair 合同 272 项、翻译：均为 0 失败。
  - QtQuick：115 passed，0 failed。第一次运行时日志为空，不算通过；按以前脚本的参数（离屏、软件渲染、`-o` 输出文件）重跑后得到上述结果。
- 实机运行（`scripts/acceptance/vrr-event-query.py`）：私有配置，2.1.0 NVIDIA 运行库基底硬链接副本，替换本次程序与 QML/i18n，真实 1080p30 文件，VFG 2X，没有其他 Veyra 进程，不接采集设备。脚本在引擎日志里按步骤打标记，统计每步之间实际开始的查询：

| 步骤 | 真实屏幕（native，等级 3，开关后走 VRR 呈现） | 模拟不支持 VRR（novrr，等级 1） |
| --- | --- | --- |
| 启动 | ui ×1 | ui ×1 |
| 空闲 4 秒 | 0 | 0 |
| 开播 | session-start ×1（有缓存，未等待） | session-start ×1 |
| 播放 15 秒（VFG 2X，60 fps 提交） | 0 | 0 |
| 点重新检测 | 1，19 ms 后弹出结果提示 | 1 |
| 打开可变刷新 | 1 + 约 3 秒后 vrr-confirm ×1 | 1 |
| 改窗口大小（模拟进出全屏） | 约 3 秒后 vrr-confirm ×1 | 0 |
| 停播 5 秒、切到节点页 | 0 | 0 |

  - 真实屏幕整轮约 70 秒共 6 次查询，模拟不支持 VRR 共 4 次，每次都对应一个事件。对照：Codex 那版（fg1）的同类运行日志在 28.5 秒内查了 15 次，中位间隔 2.0 秒（`reports/claude-vrr-event-query-20261025/query-rate.json`）。
  - 两轮都没有 QML 报错和查询失败，节点页正常显示。专业页“显示”标签截图：`tests/claude-vrr-event-query-20261025/shots/display-tab-{native,novrr}.png`。
  - 结果：`reports/claude-vrr-event-query-20261025/native-{native,novrr}.json`。
  - 脚本自身的两处问题已修正：一是动作执行后才打标记，导致打开开关那次查询被记进上一步；二是 250 ms 采样抓不到短暂的“检测中”状态。

## 未验证与已知边界

- 本机屏幕现在报告支持 VRR（G-SYNC Compatible，等级 3），与 2.0.8 时“不支持”的记录不同。但窗口模式下驱动始终没有报告“生效中”（requested/active=false），所以“生效中”这一档没有在真实设备上见到。真实 VRR 的画面与延迟收益、进出真全屏后的状态仍需用户在自己的 VRR 设备上验证；改窗口大小只模拟了尺寸变化这一触发条件。
- 在控制面板里改了 G-SYNC 设置、屏幕又没有重设显示模式时，Veyra 要等点“重新检测”、重新开播或下一次显示变化才会知道。按钮提示即为此设计。
- 原有行为未改：VRR 从“已请求”变为“生效中”时，呈现状态文字（“显示同步：自动 + 可变刷新 · 已请求，等待驱动确认”）不会自动更新，因为它只在重新配置呈现时生成；“可变刷新”行提示和状态栏会随新等级更新。
- `AsyncDisplayQuery.h` 已不再被呈现器使用，保留其测试；以后可另行删除。
- 实机运行用的私有程序目录 `tests/claude-vrr-event-query-20261025/app` 是对 2.1.0 基底的硬链接，只额外占用替换的 EXE/QML/i18n。没有打用户包，也没有覆盖 `E:/App`。
