# 2026-10-24 NR内部手动百分比

已实现NR内部处理分辨率的“手动”选项。选择后显示25%–100%、1%步进的处理比例滑条；列表/节点各层共用同一编辑器与参数合同。仅改变视频预览内部尺寸，最终输出不变。补帧调度只诊断，报告见docs/FG_SCHEDULING_LOG_DIAGNOSIS_2026-10-24.md。

## 行为

- 宽、高同时按百分比缩放，按既有规则向下对齐偶数，极小输入保持至少1像素。50%是约四分之一像素数，不是“像素总数减少一半”。比例相对于该NR层实际输入：SR之后按SR底图，NR先行按源尺寸。
- 选择手动才显示滑条；拖动时显示待选值，松开才提交，避免拖动每40ms重建GPU特征。键盘至少1%步进、双击/Enter输入、范围限制与100%重置均有效，绑定保留。切回固定档再选手动会记住当前编辑器中的最后比例；当前选中的手动比例能保存至预设/会话并重启恢复。
- AMD手动比例基于原有最高1080p预算内的输入，界面明示；没有扩大模型预算。NVIDIA图片/视频导出仍按完整尺寸，AMD视频导出保留既有内部预算；不把实时档冒充原生导出。图片预览沿用既有完整处理策略。
- 旧0..6档位ID不变。手动百分比作为既有NrSizePolicy的具名扩展：125..200编码25..100，Manual=200。校验/转换集中在ResolutionPlan.h，不增加共享POD字段或改变导出进程协议布局；QML使用桥接的manualSize/sizePercent，不计算存档编码。旧版不识别新手动档，应由本更新读取其预设；旧格式预设仍能加载。
- 不改任何NGX/模型/运行组件字节、采集/补帧预算/呈现/状态判定代码或真实用户配置。继承当前分支已完成的录制、AMD超分关闭、旋转改动；Lecram继续暂停，无main合并/push/Release。

## 修改与构建

主要文件：ResolutionPlan.h、GraphDescription.h、EffectChain.cpp的NR尺寸规划；QmlPlayerBridge.cpp的每层参数与校验；NrLayerEditor.qml共用编辑器、VSlider.qml可选步进；catalog.json四语言；NrModelSettingsTests.cpp、tst_components.qml与本轮原生测试脚本。新步进参数默认0，其他滑条维持连续行为。

复用 `E:\项目\Veyra\build\claude-screen-recording-20261022`，无新工作树/重复构建目录。命令：

```
python -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-nr-manual-scale-20261024 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.0-nr1 veyra_qml_ui veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_repair_contract_tests veyra_nr_model_settings_tests veyra_effect_chain_tests veyra_preset_library_tests veyra_repair_preset_tests
python -X utf8 -B -I scripts/i18n/extract.py --check
python -X utf8 -B -I scripts/acceptance/nr-manual-scale.py
python -X utf8 -B -I scripts/acceptance/nr-manual-scale.py --four-k
python -X utf8 -B -I scripts/acceptance/nr-manual-scale.py --four-k --pixels
```

首轮同源增量构建180秒通过；随后显式重新配置、构建36.8秒通过，内部标签2.1.0-nr1，限-j2。build-recording-handoff.py增加--reconfigure，是因为复用已存在的build.ninja时原脚本不会应用新的显示版本。未修改CMakeLists.txt。

## 实测结果

所有证据 `E:\项目\Veyra\reports\nr-manual-scale-20261024`；日志 `E:\项目\Veyra\logs\nr-manual-scale-20261024`，构建日志在 `logs\codex-nr-manual-scale-20261024`。

- NR模型设置、EffectChain、PresetLibrary、旧PresetStore、Repair272、UI契约含384布局、i18n2664条均通过。新增76个整数百分比的尺寸/输出/边界合同；同时覆盖纵向画面、1×1输入、NR在SR之前、各层独立比例、AMD预算、原生导出与List/Node/未连接草稿的实际磁盘序列化恢复。
- QtQuick **110通过、0失败**，新增真实鼠标拖动/松开只提交一次、整数键盘、越界输入、手动与固定切换和重置；原有滑条/模式/旋转测试保留。
- RTX5070真实SF310.9.16、性能模型B，输入1080p30和原生4K30，分别跑25/37/50/75/100及节点75%，NGX Feature18 Create均成功、有非零GPU NR计时与连续播放；每次尺寸重建无错误，输出保持输入大小。两次独立重启恢复List50%/Node75%成功。完整日志、Create返回值与尺寸见 `native-nr-verification.json`、`real-player.json`、`real-player-4k.json`。
- 另从真实4K37%处理中截图读取GPU输出：PNG为3840×2160，RGB标准差约82/97/86，非空白/全黑；`pixel-validation.json`记录路径与SHA。该截图是诊断读回，不新增正常播放读回，也不把尺寸/非黑检查冒充与上游模型的画质等价。

| 比例 | 1080p输入的NR尺寸 | 4K输入的NR尺寸 | 输出 |
| --- | --- | --- | --- |
| 25% | 480×270 | 960×540 | 原1080p/4K |
| 37% | 710×398 | 1420×798 | 原1080p/4K |
| 50% | 960×540 | 1920×1080 | 原1080p/4K |
| 75% | 1440×810 | 2880×1620 | 原1080p/4K |
| 100% | 1920×1080 | 3840×2160 | 原1080p/4K |

NVIDIA/AMD专有运行身份不变；用户现有E:/App包177个DLL/模型/着色器只读SHA审计零差异。没有RX9000 NR实卡、其他NVIDIA型号、HDR、真实采集/主机串流、跨屏、长期稳定性或光学延迟验收；不能由本机结果宣称所有硬件无bug。AMD范围只通过尺寸/预算合同测试。

## 失败与修正（保留原始记录）

- 初次QtQuick跑到旧staging测试脚本，旧期望7项与新8项冲突；不是产品回退。将实际源码测试目录同步并逐文件校验后110通过，原失败 `contracts-084912` 保留。
- 第一版原生夹具在冷启动NR初始化后过早要求媒体进度，虽Create成功且已出帧仍判失败；改为有界等待真实进度。随后首次进节点模式没有NR节点，按既有产品流程显式添加/连接；第三次复用失败profile导致同名预设被正常拒绝，改每轮新建私有profile。以上均只改夹具，没有降低模型运行/错误检查门槛；成功记录gpu-091045/restore-091133及gpu-4k-091514/restore-4k-091602。失败日志仍在该任务目录。
- 重新配置时发现上一轮依赖迁移遗漏：Chiaki阶段工作树的8个子模块.git元数据以跨C盘相对core.worktree定位。普通C兼容入口与增量构建不暴露问题，Git递归校验才发现。先备份8个原config，再仅将core.worktree写为其原有C盘源码工作树的绝对路径；没有移动/改写源码、子模块提交或已审核patch。原 `verify-chiaki-stage.py` 全子模块/完整patch验证通过，随后完整重新配置通过。证据 `dependency-git-path-plan/repair.json`、`chiaki-stage-verification.log`。迁移时149125文件哈希是当时证据，之后这8份Git元数据是有意修正，不应依旧清单撤回。

## 本地交付

交付为小更新层，复用2.1.0已有运行库，仅含Veyra EXE、当前QML/i18n和说明/逐文件清单，不包含任何新DLL/模型/SDK/用户配置。包含此前已交付的旋转与录制修复。用户E:/App原包只读未覆盖；包路径、精确SHA、源码提交及最终验证以 `reports\nr-manual-scale-20261024\update-package.json` 为准。正式Release与旧完整rot1供体保留。本轮私有测试app/生成媒体按收尾清单清理，实际结果单独记录，不从计划假定已删除。
