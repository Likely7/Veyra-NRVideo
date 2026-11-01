# 2026-10-26 界面默认软件绘制（Claude）

用户依据 GPU/软件界面 A/B 决定：默认界面改为软件绘制，设置里保留“GPU 绘制”（需重启），自动兼容逻辑不变。从 main 68def10 开 `claude/software-ui-default-20261026`，起点标签 `checkpoint/pre-software-ui-default-20261026`，开工状态 `archives/claude-software-ui-default-20261026/start.json`。单 Agent、全 E 盘、复用 `build/claude-screen-recording-20261022`（-j2、≤900 秒）、单测试 ≤300 秒；不改运行库/模型/驱动/真实配置，不 merge/push/Release，Lecram 暂停。上一分支 `claude/vrr-event-query-20261025` 保留，不并入。

## 依据

`reports/claude-ui-renderer-ab-20261026/ab.json`：RTX 5070、4K30 + VFG 2X、2400×1300 窗口、320 Hz，GPU/软件/软件/GPU 四轮。
- GPU 界面进程显存 469 MiB，软件界面 105 MiB。
- 界面静止时 GPU 占用相同（约 19%）；连续切换标签页时，GPU 界面约 30%，软件界面约 15%。
- 软件界面在切换时多用约 6.6% 的单核 CPU。四轮视频都是 60 fps、0 跳帧。
- 现场问题（RTSS 设备移除、NVIDIA 浮窗/游戏加加死锁、抖音直播伴侣卡死、OBS 抓到界面背景、GPU 卡死时界面与视频共用 D3D12 设备一起停）都来自 GPU 界面的交换链或设备。

## 方案

1. 新偏好 `uiRenderer`：`software`（缺省、无效值）或 `gpu`，由 `main.cpp` 在 Qt 启动前读取。启动选择提取为无 Qt 依赖的 `include/veyra/ui/InterfaceRenderer.h`：
   - OBS 游戏采集兼容：软件（不变）。
   - 监控软件兼容（自动 + 检测到 RTSS/NVIDIA 浮窗/游戏加加/抖音直播伴侣，或已接受兼容重启，且未设 `VEYRA_UI_RHI`）：软件（判定不变）。
   - 设了 `VEYRA_UI_RHI`：按它选 GPU 接口（不变）。
   - 其余：按 `uiRenderer`，缺省软件。
   - 选 GPU 时照旧优先 D3D12；RTSS OSD 环境变量、晚到浮窗的重启提示等仍只看“实际是否软件界面”。
2. 设置 → 兼容：新增“界面绘制：软件 / GPU”（需重启，沿用现有重启确认）。提示说明当前实际渲染器，以及 OBS/监控软件兼容覆盖 GPU 选择的原因。“监控软件兼容”的提示和重启判断改为只在选了 GPU 时才有意义。分组说明加上“界面绘制”。
3. 桥接：`setPreference` 接受 `uiRenderer`；新增常量属性 `softwareUiActive`。新文案补繁中/英/日翻译。
4. 不改视频呈现、软件界面的绘制细节、OBS 兼容效果、33ms/250ms 刷新与 GPU 优先级。

## 验证

- UI 合同测试新增启动选择各组合：缺省、GPU、GPU + 检测到监控软件（自动/关闭）、GPU + 兼容重启、OBS、强制 RHI。
- 构建 `veyra_qml_ui` 与相关测试，运行 UI 合同、Repair 合同、翻译、QtQuick，以及 `scripts/i18n/extract.py --check`。
- 私有配置实机启动（确认没有其他 Veyra 进程），看日志里的实际渲染器：
  - 全新配置 → 软件；
  - 在设置里选 GPU 后重启 → D3D12；
  - GPU + `--overlay-compat-restart` → 软件（兼容）；
  - 再选回软件后重启 → 软件。
  - 另截取设置页“兼容”分组两种状态的截图。

产物：`E:/项目/Veyra/{tests,logs,tmp,reports,archives}/claude-software-ui-default-20261026`。
