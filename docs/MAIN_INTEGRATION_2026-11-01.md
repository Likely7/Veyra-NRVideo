# Claude最新补帧修复合入本地main
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户2026-11-01授权“刚刚Claude那边做了一些修复，你给合并到Main”。本轮范围是`claude/fg-no-latency-growth-20261101`在main `13016a8`上的37个未提交文件；其余此前整合内容main已有，不再次搬运，Lecram暂停线不动。没有push或Release授权。

## 合并结果

先保全Claude工作区的完整37文件、binary diff及SHA，再检查、构建和回归；与保全快照逐字节核对后，原样提交为`6e79a5e`。切到main以`--ff-only`快进，无代码冲突，没有替换、删减Claude产品实现。后续本轮提交仅更新状态、AGENTS和此合并记录。

合并前tag `checkpoint/pre-main-claude-fg-20261101` → `13016a8`；修复tag `checkpoint/claude-fg-no-latency-growth-20261101` → `6e79a5e`。源分支保留作回溯。本轮原始归档`E:/项目/Veyra/archives/codex-main-integration-20261101`；回执、测试脚本`reports/codex-main-integration-20261101`，最终head在FINAL.json。

包含：撤回上一版自动队列/长等待/宽预算，平滑实时呈现锚点，VFG探测退避及下一档模型，列表与节点的VFG自动调控开关（默认开、可保存），内容选项改名为“自动剔除重复帧”。具体模型选择、显存代价和原有实测见[Claude实现记录](FG_NO_LATENCY_GROWTH_2026-11-01.md)。

## 本轮检查

- 构建15个相关产品/测试目标，增量0.7秒退出0，并发2，日志`logs/codex-main-integration-20261101/build-20261101-053508/compile.log`。receipt中的旧head/dirty=true反映当时Claude尚未提交；before.json记录37文件SHA，原样提交保证被测实现与`6e79a5e`一致。
- 8组合同/设置：live timing、presentation scheduler、realtime preview、repair300项、i18n、effect chain、NR model settings、preset library，全部退出0，无FAIL。覆盖新旧设置/预设兼容；没有把默认值字段加入误认成旧配置已损坏。
- QtQuick：123通过、0失败、1既有跳过。
- GPU短测：4K回放模拟采集+VFG运行中开关切换、DLSS6X、XeSS2X，3组全部退出0、真实提供方生成帧、干净退出，无D3D12 ERROR/CORRUPTION。每组13.5/15.5/6.5秒，当前屏320Hz；没有改变用户显示模式或使用采集卡。
- 真实应用界面：两次启动、私有profile；列表默认值/置灰/切换、节点实际开关双向修改及低档/超过2X置灰、重启保存、中英文新名称，全部通过，无QML错误。补上Claude仅加载节点页但未点开关的检查。原Main.qml注入后恢复。
- 程序EXE、worker、shader与构建核对；QML/翻译与源码一致；原正式供体manifest、97项默认运行组件和原用户配置指纹核对见FINAL。

命令入口：

```
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-main-integration-20261101 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.1-test --jobs 2 veyra_qml_ui veyra_live_timing_tests veyra_presentation_worker_tests veyra_realtime_preview_tests veyra_repair_contract_tests veyra_ui_i18n_tests veyra_effect_chain_tests veyra_nr_model_settings_tests veyra_preset_library_tests veyra_qml_quick_tests veyra_presentation_pacing_tests veyra_live_presentation_tests veyra_amd_nr_graph_tests veyra_rdna3_nr_contract_tests veyra_lmxxf_nr_tests
python -X utf8 -B E:/项目/Veyra/reports/codex-main-integration-20261101/verify.py stage
python -X utf8 -B E:/项目/Veyra/reports/codex-main-integration-20261101/run.py cpu-merge
python -X utf8 -B E:/项目/Veyra/reports/codex-main-integration-20261101/verify.py qml
python -X utf8 -B E:/项目/Veyra/reports/codex-main-integration-20261101/run.py gpu-merge
python -X utf8 -B E:/项目/Veyra/reports/codex-main-integration-20261101/ui.py
```

GPU与offscreen软件QML曾并行，日志中的otherVeyra为本轮QtQuick测试程序，不是用户播放器。仅作功能回归，不作性能A/B。沿用脚本pacing_stats的captureToPresent字段错误地将相对媒体PTS与host相减，得出无意义的大数；本轮不采信这些字段，不据其宣称延迟变化。XeSS内部生成不进入普通generatedPerSecond，使用测试断言/SDK提供方数据确认。

## 边界与产物

未重跑Claude的60Hz模型速度A/B；其5060Ti数据是RTX5070工作线程加延迟模拟，不是实卡；未验真实采集卡、5060Ti、RX7000推理、VRR或模型切换画质。既有source-gap EOF快照不稳定、两个FSR探针缺本地SDK的记录没有被本轮当作通过，也未重复全量构建来掩盖它们。本轮针对目标均通过，没有新增检查失败。

运行目录仍为`E:/项目/Veyra/tests/claude-sr-stage-20261028/app`，EXE为本次被测构建；不重包、不覆盖2.1.1正式资产。仅清本轮可再生TEMP，保留原Claude工作、依赖、日志和必要证据。原配置、默认运行组件、驱动及其他工作树保持。正式版本仍2.1.1；本地main整合不代表已公开发布。
