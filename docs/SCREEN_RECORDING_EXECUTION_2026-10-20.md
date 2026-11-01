# 实时录制功能执行记录 — 2026-10-20

## 一、范围

用户要求：录制**处理后的画面**、设置页可设**位置与码率**、专业模式**截图按钮旁**加录制按钮
（**动画 + 3 秒倒计时**），并给出**新的测试版本**实测。

方案与取舍见 [SCREEN_RECORDING_PLAN_2026-10-20.md](SCREEN_RECORDING_PLAN_2026-10-20.md)。

## 二、分支与存档

- 分支 `codex/screen-recording-20261020`，起点 `nrvideo/main` `4ae285b`；
- 工作树 `E:\项目\Veyra\worktrees\screen-recording-20261020`；
- 存档标签 `checkpoint/pre-screen-recording-20261020`；
- 提交：`2c80233`（功能）、`99bfc43`（首轮编译错误修正）、`65fe7ef`（界面布局修正）；
- 未合并 main、未推送、未发布、未修改 GitHub 上任何 Release 或标签。

## 三、构建

构建沿用 2.1.0 正式包的同一套固定依赖与 CMake 选项
（`logs/pr64-avermedia-20261019/build-20261019-094942/receipt.json` 的 `dependencies`），
脚本 `E:\项目\Veyra\tmp\screen-recording-20261020\build.py`，构建目录
`E:\项目\Veyra\build\screen-recording-20261020`，并行 -j2。

**首轮全量构建（548 步）在 431/548 处失败**，三次编译错误与修正：

| 错误 | 原因 | 修正 |
| --- | --- | --- |
| `NvencD3D12Encoder.cpp(36): C2065 “p” 未声明` | 排空超时写成了 `p.drainTimeoutMs`，但 `drain()` 在 `Impl` 内部，成员直接访问 | 改为 `drainTimeoutMs` |
| `EngineController.cpp(1565): C2065 “RecordingOptions” 未声明` | 引擎用了 `RecordingOptions/RecordingSnapshot`，而 `LiveRecorder.h` 里是 `RecordingSettings/RecordingStatus` | 统一为 `RecordingSettings/RecordingStatus`（含 `QmlPlayerBridge.cpp`） |
| `EngineController.cpp(1206): C2059 语法错误` | 会话退出钩子 `OnExit{...}` 多加了一个语句后少一个右花括号 | 补 `}}}` |

修正后增量重建 **44.8 秒**通过，5 个目标全部产出：

| 目标 | SHA256 |
| --- | --- |
| `veyra_qml_ui.exe` | `016366feec4c22b4c20971404e9934f8dd9fdce867a4ed49d77fdd9049355bb3` |
| `veyra_qml_quick_tests.exe` | `c2abd79e275f81d488deb161a3baec4d39a211873a53568ab343412444d3aaa1` |
| `veyra_ui_i18n_tests.exe` | `ea0cf211488cd179b6562c7eb2062d67a57bc37a62398dccabc236e6900ead69` |
| `veyra_ui_contract_tests.exe` | `3cf42aca6bcd419a6879c4c5505a48f906168ad57e889282f6d3d99c878c0897` |
| `veyra_qml_data_tests.exe` | `bb9b6693f34d3a4c66578ec61f08040fc93dab41ce4df2820e70d0f1b864b40d` |

## 四、回归

`E:\项目\Veyra\tmp\screen-recording-20261020\run_tests.py`，日志在
`E:\项目\Veyra\logs\screen-recording-20261020\`：

| 组 | 结果 |
| --- | --- |
| `veyra_qml_quick_tests`（会真实实例化改过的 ProPage） | 105 passed / 0 failed |
| `veyra_qml_data_tests` | PASS |
| `veyra_ui_i18n_tests` | 0 failures（2637 条，四语无缺） |
| `veyra_ui_contract_tests` | PASS |

`scripts/i18n/extract.py --check`：2637 条，缺译 0，占位符问题 0。

**冷启动抓到两个真实 QML 错误**（`logs/screen-recording-20261020/cold-console.log`），
已修正并重建：

1. 录制控件在头部 `RowLayout` 里用了 `anchors` → 改为 `Layout.alignment` / `Layout.preferredWidth`；
2. `easing.type: Theme.spring` 把线性缓动当成枚举 → 改为 `easing: Theme.spring`。

修正后重新打包并冷启动，日志中不再有这两条错误。

## 五、测试包

供体是已交付的 2.1.0-test NVIDIA 包（运行组件字节原样不动），只替换程序本体、QML 与文档，
manifest 按包内实际文件重新审计（1734 个文件）。

- 包目录 `E:\项目\Veyra\test-packages\screen-recording-20261020\Veyra-2.1.0-rec-NVIDIA-win64-portable`
- 压缩包 `Veyra-2.1.0-rec-NVIDIA-win64-portable.zip`
- 冷启动：从压缩包解压到 `E:\项目\Veyra\verify\screen-recording-20261020\NVIDIA`，逐文件核对 manifest
  哈希后启动，退出码 0

## 六、功能验证（真实录制）

`scripts/acceptance/screen-recording-control.py` + `scripts/acceptance/screen-recording-ui.qml`：
把本任务打包好的测试版复制到独立目录，在**正式 Main.qml**里注入控制 QML，用**生产 bridge**
打开一段合成测试片（`testsrc2` 1280x720 30fps，20 秒），`veyra.startRecording()` 开始录制，
录约 5 秒后 `veyra.stopRecording()` 停止，随后检查引擎写出的文件。

结果（`logs/screen-recording-20261020/recording-console.log` 与 `recording-files.json`）：

| 项 | 实测 |
| --- | --- |
| 界面通知 | 录制已结束：…/recordings/Veyra-2026-10-20-201734.mp4 |
| 编码器帧数 | 160 帧，丢帧 0 |
| 录制时长 | 5.216 秒 |
| 产出文件 | 9,966,597 字节 |
| ffprobe | h264 / 1280x720 / 160 帧 / 时长 5.249 秒 |

即：**处理后画面确实被录成了可播放的 MP4**，录制-停止-落盘这条链路在生产代码上跑通。

## 七、未验证与边界

- 录制链路已在本机跑通（见第六节），但**画质观感、长时间录制稳定性、补帧帧是否进入文件、
  真实采集卡与串流场景**仍由用户实测确认。
- 本机走 NVIDIA NVENC 路径；AMD/Intel 走系统 Media Foundation 编码器，本轮无实卡验证。
- 录制**不含音频**；暂停时不继续写新帧；改增强设置会结束录制（增强图重建会换掉纹理）。
- XeSS 自行持有交换链，其补帧生成帧不在增强图里，录制只取真实帧（与导出限制一致）。
- 未合并 main、未推送、未发布；`docs/CURRENT_STATUS.md` 与对外 README 本轮未改。
