# 2026-10-26 界面默认软件绘制（Claude）结果

分支 `claude/software-ui-default-20261026`，起点 main 68def10（标签 `checkpoint/pre-software-ui-default-20261026`），功能提交 41f0994。方案见 `docs/SOFTWARE_UI_DEFAULT_PLAN_2026-10-26.md`。没有 merge、push、Release，没有改运行库、模型、驱动或真实配置，Lecram 暂停。上一分支 `claude/vrr-event-query-20261025` 保留，未并入本分支。

## 依据（改动前的 A/B）

`reports/claude-ui-renderer-ab-20261026/ab.json`，测试脚本在 `tmp/claude-ui-renderer-ab-20261026/ab.py`。条件：RTX 5070、4K30 + VFG 2X、2400×1300 窗口、320 Hz 屏幕。按 GPU、软件、软件、GPU 顺序跑四轮，每轮一个私有配置；软件界面用产品自带的 `--overlay-compat-restart` 切换，视频链路不变。

| 指标 | GPU 界面 | 软件界面 |
| --- | --- | --- |
| 进程显存（图创建时） | 469 MiB | 105 MiB |
| 界面静止时 GPU（nvidia-smi） | 21.0 / 16.8% | 19.7 / 18.8% |
| 每 0.5 秒切一次标签页时 GPU | 33.5 / 27.5% | 16.7 / 14.3% |
| 切换标签页时进程 CPU（单核占比） | 32.3 / 38.5% | 37.7 / 46.3% |
| 视频 | 60 fps，0 跳帧 | 60 fps，0 跳帧 |

## 改了什么

- `include/veyra/ui/InterfaceRenderer.h`：无 Qt 依赖的启动选择函数。原有规则保持不变并且优先：
  - OBS 游戏采集兼容 → 软件。
  - 监控软件兼容为“自动”，且启动时检测到 RTSS/NVIDIA 浮窗/游戏加加/抖音直播伴侣，或刚接受过兼容重启 → 软件。
  - 设了 `VEYRA_UI_RHI` → 按它选 GPU 接口。
  - 其余情况按新偏好 `uiRenderer`，缺省为软件。
- `apps/veyra-qml/main.cpp`：启动时读 `uiRenderer`，改用上面的函数。每种原因记一行 `[app]` 日志，原有的 OBS/RTSS 兼容日志原样保留。选 GPU 时仍优先 D3D12；RTSS OSD 环境变量、晚到浮窗的重启提示仍只看实际是否为软件界面。
- 桥接：`setPreference` 接受 `uiRenderer`（software/gpu）；新增常量属性 `softwareUiActive`。
- 设置 → 兼容：
  - 新增“界面绘制：软件 / GPU”，需重启，沿用现有重启确认。只有重启真会改变渲染器时才弹确认，兼容规则会让界面继续保持软件时不弹。
  - 提示说明当前实际状态，以及 OBS 或监控软件兼容让 GPU 选择不生效的原因。
  - “监控软件兼容”的提示和重启判断改为只在选了 GPU 时生效。分组说明加上“界面绘制”。
- 翻译：新增 8 条，繁中/英/日齐全，旧的分组说明随之移除。`scripts/i18n/extract.py --check`：2671 条，0 缺译，0 占位符问题。

## 构建与测试

- 构建：`py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task claude-software-ui-default-20261026 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.0-swui1 veyra_qml_ui veyra_ui_contract_tests veyra_repair_contract_tests veyra_ui_i18n_tests veyra_qml_quick_tests`，62.2 秒通过。日志 `logs/claude-software-ui-default-20261026/build-20261026-005610`；EXE SHA256 `8e0d85d94e6e4fce1c279df0a2fdd2635bf197fc6112006557fe8ee89447c599`。构建后未再改产品源码。
- 测试（`reports/claude-software-ui-default-20261026/tests.json`）：
  - UI 合同：新增启动选择各组合（缺省、GPU、GPU + 监控软件自动/关闭、兼容重启、OBS、强制 RHI）及原有 384 布局，全部通过。
  - Repair 合同 272 项 0 失败；翻译 0 失败。
  - QtQuick 115 passed / 0 failed（离屏软件渲染，`-o` 输出文件）。
- 实机四次启动（`scripts/acceptance/ui-renderer-default.py`，同一个全新私有配置，2.1.0 NVIDIA 运行库基底硬链接副本，不放视频）。每次都通过设置页真实控件的 `picked` 信号切换，渲染器以程序日志为准：

| 次 | 条件 | 实际界面 | 设置操作 |
| --- | --- | --- | --- |
| 1 | 全新配置 | 软件（默认） | 选 GPU → 保存，弹出重启确认 |
| 2 | 偏好 GPU | D3D12 | 选软件 → 弹出重启确认（随后改回 GPU） |
| 3 | 偏好 GPU + `--overlay-compat-restart` | 软件（兼容） | 选软件 → 不需要重启，不弹确认 |
| 4 | 偏好软件 | 软件（默认） | — |

  - 四次都没有 QML 报错和引擎错误。结果 `reports/claude-software-ui-default-20261026/launches.json`，截图 `tests/claude-software-ui-default-20261026/shots/settings-compat-{1..4}.png`。
  - 第一轮脚本先发起异步截图、紧接着就点选，导致第 1 张截图混入了点选后的状态。改为等截图写完再点后重跑，四次仍全部通过。

## 未验证与已知边界

- 软件界面的代价没有消失：阴影、发光、模糊效果简化；窗口背景不透明；界面动画时多用约 6–7% 的单核 CPU（上述 A/B 条件）；动画在高刷屏上没有 GPU 模式顺滑（按 Qt 软件渲染机制推断，没有测量帧时间）。
- A/B 只在一张 RTX 5070、一种窗口尺寸、每种模式两轮的条件下做过。AMD/Intel、4K 窗口、150%/200% 缩放、弱 CPU 上的软件界面 CPU 开销没有测。
- 已经保存过偏好的老用户：只要没有 `uiRenderer` 这一项，升级后就会变成软件绘制。这是有意的默认变化，想要旧效果需在设置里选 GPU。
- 本分支不含上一个分支的 VRR 事件查询改动，两者合入 main 时需要另行整合。
- 没有打用户包，也没有覆盖 `E:/App`。
