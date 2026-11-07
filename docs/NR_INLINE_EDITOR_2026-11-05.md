# NR 分组内网状／列表修订
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户撤销独立网状调节器布局，要求直接放在“增强变化量”和“画面调控”的手动区域，补齐九项调控。本次继续 `codex/nr-web-editor-20261105`，起点684d906，存档 `checkpoint/nr-web-before-inline-20261105`；不合main／push／发布。

- 取消独立面板、全局开关和整套置灰副本；版本、模型、分辨率及其他控件回到原位。
- 两组各有独立“调节方式：网状／列表”，共享原参数，切换不复制、不清零。
- 增强变化量四轴，总变化强度始终是滑条；手动调控九轴，色相、色度、中性、原色、亮度、暗部、高光、局部压缩、时域稳定全部纳入。
- 网与列表共用同一份各组参数规格，避免六轴与九条列表再次分叉。自动调控只保留原自动力度；手动方式和值保留。
- 新偏好nrResidualView／nrCorrectionView取list或web；旧nrWebEditor仅用于未选新偏好时迁移既有视图选择，不再有全局接管行为。
- 删除NrParameterEditor／NrWebControls两层旧包装，恢复一份NrLayerEditor，保留经过验证的NrWebPad手势。移除只为旧自动六轴显示添加的桥接字段；不改增强算法、参数存储或导出协议。

发现用户仍运行旧测试实例PID23760，因此新程序、测试配置及日志另放 `tests/logs/tmp/codex-nr-inline-editor-20261105`，报告 `reports/nr-inline-editor-20261105`，不改旧程序／用户配置、不停止旧实例。

验收包含两组独立切换、九项逐值写入与切回、自动／手动保留、残差总量、九轴标签与输入、迟到手势阻断、两模式与AMD约束、完整QtQuick／翻译／保存恢复、实际主程序与截图。构建并发2，每测试≤300秒。

## 实现与验收结果

`NrLayerEditor.qml`恢复单一完整编辑器，residualSpecs／correctionSpecs分别供列表和网共用；九项清单顺序与原手动列表一致。旧两个包装文件及qmldir入口删除；自动模式下不显示手动调节方式或网，不再构造旧只读六轴map。隐藏列表和网的迟到输入均阻断，切换方式只影响显示。

构建命令：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-nr-inline-editor-20261105 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.2-nr-inline-test --reconfigure --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_qml_ui veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_nr_model_settings_tests`，73.9秒通过。最终提交后增量确认源码身份。

- 软件绘制完整QtQuick：**170/0/0**，38.98秒；分组编辑器11/0/0、GPU绘制Pad13/0/0。包含九轴全部标签的240／280／330／500逻辑像素边界与互不重叠、逐项选择写入、全九列表切回、两组独立性、模式共享、旧偏好迁移、AMD与缺组件预设等。
- UI合同、i18n、NR模型/尺寸/保存测试均通过；catalog2739条，繁中／英语／日语缺译与占位问题0。
- 真实程序23项通过：两偏好有效性与非法值拒绝、无独立块／灰副本、每个九轴参数逐一读回实际bridge及列表值、自动/手动值与视图保留、总倍率及外部尺寸参数、两张实际截图；重启后两组偏好及原参数恢复。两进程均0退出，无engine或QML错误。
- 已查看实际完整面板截图 `tests/codex-nr-inline-editor-20261105/nr-web-ui.png`。单组PNG保留透明背景，整体截图体现实际深色背景；不重绘或伪造界面。

本轮只做控件及布局回归，真实程序使用无增强的私有文件输入，避免与旧用户实例争用NR负载；不宣称新的推理性能、AMD实卡或物理延迟验证。正常播放/采集/导出算法和格式未改。旧测试实例及手动配置均未写入或停止。

手动入口 `tests/codex-nr-inline-editor-20261105/启动NR分组调节测试.cmd`，使用旧测试设置的独立快照（如旧设置存在），并保存新的两组调节方式。只迁移UI/链路配置白名单，不复制凭据；启动自动恢复和预热关闭，输入由用户主动选择。完整回执见 `reports/nr-inline-editor-20261105/FINAL.json`。未合main、未push／发布。
