# 2.1.3 默认档位与低延迟提示重构建
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户要求：超分默认中档；英伟达低延迟警告改红色，仅“开＋增强”显示；删除上一轮构建产物，重建2.1.3。

## 改动

- `kDefaultVideoSrQuality=2`，列表/节点、基础/第二次超分统一中档。从DLSS/FSR切入RTX版本没有既有档位时使用该默认（标准2、高码率17、流媒体21），已有档位和保存设置不迁移覆盖。
- NvidiaLowLatencyEditor共用组件按选择显示警告，Boost用Theme.err红字，Off/On无警告。非NVIDIA的硬件不可用说明保持普通说明色。
- 不改变低延迟驱动调用、模型或运行库；版本仍2.1.3。本轮不push/Release。

## 清理与路径

上一轮`codex-build-2.1.3-20261106`的包、ZIP、验收解压副本和测试二进制按受控清单逐文件SHA核对后删除；未列入清单的个人配置/日志和修改文件保留。共享构建缓存、依赖、旧正式版和源代码工作树不删。
清理脚本/实际清单在`reports/codex-rebuild-2.1.3-20261106/cleanup-old.ps1`与CLEANUP-PLAN/CLEANUP.json。不使用跨Shell删除或递归到连接目标。

复用`build/claude-screen-recording-20261022`，新task为`codex-rebuild-2.1.3-20261106`；test-packages/tests/logs/tmp/verify/reports全部按新task隔离，旧interactive配置保留在原位。

## 验证

构建主程序、AMD worker及相关QML/设置/预设/合同目标；更新原UI合同和低延迟交互用例，验证新默认与显式保存值的恢复。新包逐文件SHA/CRC和独立解压最小PATH启动/播放。实际命令、失败与通过、交付身份以WORKLOG和新task FINAL.json为准，本计划不作为通过证明。

## 本轮实际验证（2026-11-06）

产品源码7a2d15d，PE 2.1.3.0。9目标首构建127.6秒；发现预设写入格式对第二次超分默认值写死4，修正PresetStore/PresetLibrary的五处判定为统一常量后，最终增量构建11.8秒。没有改写已有明确保存的档位。

最终设置、效果链、预设库、NR模型、UI合同/i18n及翻译提取检查通过；完整软件QtQuick173/0/0，GPU低延迟提示6/0/0。实际程序新配置基础/第二次SR均为中档，三种RTX版本在没有原档位时均为中档，明确高档值保留；列表和节点逐项验证关/开不显示警告、开＋增强显示红字。截图已查看。保存4/19与低延迟“开”后重启恢复通过，无引擎/QML错误。

保留失败记录：首次旧默认判定导致设置/预设测试失败（一个测试进程退出生成转储），修正后所有相关检查通过；仅本测试的转储经路径证据和SHA核对搬至E盘日志。中间一次完整Quick有5个既有菜单几何断言失败；同源码/最终程序的菜单独立14/0和完整173/0复测通过，未确定其间歇原因，也不宣称本轮修复菜单。真实UI夹具先误用旧interfaceMode键、继而遗漏节点模式首次空图，已纠正夹具并重新用私有配置验证；不改变产品迁就夹具。

上一轮产物逐SHA清理9,422文件，逻辑8,000,253,348字节，不等于实际磁盘净释放。旧个人配置/日志、依赖、历史正式包保留。新包路径test-packages/codex-rebuild-2.1.3-20261106，独立启动器使用新的测试配置。ZIP逐文件核验、最小PATH解压播放的后续结果以本任务FINAL.json为准。NVIDIA/AMD两包本机RTX5070默认效果关的冷启动不代替AMD推理或实卡采集验收。未push/Release。

复现命令：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-rebuild-2.1.3-20261106 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.3 --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_qml_ui veyra_amd_nr_worker veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_video_ai_settings_tests veyra_effect_chain_tests veyra_preset_library_tests veyra_nr_model_settings_tests`。检查脚本reports/codex-rebuild-2.1.3-20261106/check.py、ui_runner.py/defaults-ui.qml/defaults-restart.qml；实际命令、环境、首败/复测和日志路径在ACCEPTANCE.json及logs同任务。封包命令为`python -X utf8 -B scripts/acceptance/package-2.1.3.py seal`及`cold`。

## 封包与清理实际完成

应用源码归档1177696、产品7a2d15d；两ZIP逐文件SHA/CRC与独立解压最小PATH播放通过（NVIDIA=207真实呈现帧, AMD=208真实呈现帧，默认效果关闭，本机RTX5070）。最终包、源代码与网络源码摘要见test-packages/codex-rebuild-2.1.3-20261106/DELIVERY.json。两包无个人配置/日志/测试程序/SDK。临时验证副本逐SHA清理5541文件，新旧交互配置、日志与最终包保留。系统自动生成的本次单测转储经路径与SHA确认已搬至E盘日志。Git仅验收文档在归档后继续更新；未push/Release。完整实际命令、首次失败、修正和日志在本任务reports/logs。
