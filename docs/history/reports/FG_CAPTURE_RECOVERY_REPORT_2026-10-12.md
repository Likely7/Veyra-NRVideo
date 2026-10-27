# 日志50补帧：短时间戳误重置修复

2026-10-12。分支 codex/fg-capture-recovery-20261012，基线 main a167831b。已完成可复现缺陷修复、构建和定向回归；没有合并main、推送或制作发布包。实卡性能仍待原用户复测。

## 改了什么

SceneCadenceAnalyzer 原来将上一帧间隔直接当作下一帧基线。Elgato4KPro 日志里的 1ms 正增量恰好不属于 `<1ms` 的重复帧，紧接着的16.683/20.833ms正常增量就超过 `3×1ms+5ms`，导致误重置NR/光流/补帧历史。现改为最近5个非重复、按sourceStep归一化的正间隔中位数；只保存5个数字，没有增加5帧缓存或等待。窗口随显式reset清空。源PTS、帧号、样本时长、VRR、真正大断点及切镜检测保留。

生产修改只有 include/veyra/core/SceneCadenceAnalyzer.h 和 src/core/SceneCadenceAnalyzer.cpp。CMake仅登记新增永久GPU回归；新增/扩充测试在 tests/integration/CaptureCadenceFgTests.cpp、tests/unit/SceneCadenceTests.cpp。UI、EngineController、呈现策略、补帧预算/截止时间、队列容量、GPU默认优先级、驱动/运行库/模型原件均未修改。保留33ms统计/250ms状态节奏。

日志里的切换失败另有交换链占用问题。main已经有108cbaa的持久交换链复用及失败resize保留旧缓冲；本轮验证它们覆盖DLSS↔VFG相同格式切换，不重复修改。测试保留真实外部COM引用，并强制ResizeBuffers返回0x887A0001：继续用旧缓冲出画，外部引用释放后恢复800×450。

## 本轮实测

| 检查 | 结果 |
| --- | --- |
| 单测先复现再修 | 修复前30检查/5失败；修复后30/0，包括周期1ms、连续短异常、sourceStep、48-120Hz模拟VRR、200/400ms真断点、切镜/闪光/重复帧/reset |
| 同一GPU回归负对照 | 修复前main的实际库，224帧中误重置2次、生成221帧；预期exit1，D3D12错误0 |
| 修复后GPU回放 | RTX5070；P010输入、640×360，DLSS/VFG × 20.833/16.683ms四组；每组224帧、223有效生成、误重置0，异常前后四对均可生成；原PTS和中间PTS顺序保持，200ms真断点正确reset，D3D12错误0 |
| 交换链/尺寸恢复 | 上述四组跨后端保留同一链；真实持有backbuffer使resize失败一次，释放后成功；原有16轮SDR/HDR设置/外部引用像素回归及关闭/失效窗口/重新打开通过 |
| 共享时间/预算契约 | timing及259条contracts通过；未修改准入阈值 |
| 用户GTA原4K视频 | 8轮NR/FG/SR热切换、seek/慢放事务确认及故障回滚/停止全部通过，单项最长43.9秒；非现场采集FPS对照 |
| 生产QML应用 | GPU与软件UI两条实际GTA播放、私有profile、自动正常退出均通过；没有QML错误；这是启动回归，不是新UI性能A/B |

构建命令：`python -X utf8 -B -I E:/项目/Veyra/tmp/fg-capture-recovery-20261012/build.py`。复用不可变已验缓存，重编22个依赖闭包对象并重新链接10个EXE，耗时55.4秒；不是从零全量重编。完整CMake配置验证新目标和既有PS5/Moonlight/Xbox/DeckLink支持。既有未改代码/SDK的编译警告保留；新节奏实现没有编译警告。

主要回归命令：`tests/fg-capture-recovery-20261012/harness/regress.py scene timing contracts capture-cadence`、`presenter lifecycle video rollback`、`capture-cadence-before`，都在同一个任务私有运行目录；最后一项为预期失败负对照。`tmp/fg-capture-recovery-20261012/gui-smoke.py`验证两种生产UI。每测试进程≤300秒，构建≤900秒。

## 证据、保护与失败记录

产物根目录 E:/项目/Veyra；本任务archives/build/tests/logs/tmp/reports均同名。构建回执 logs/fg-capture-recovery-20261012/build-20261012-152556/receipt.json；GPU回放 fg-recovery/tests-20261012-152709；GTA/交换链回归152749；负对照153016；GUI gui-20261012-153428。validation.json核对598源码/配置、329构建输入、10EXE、QML与原包1626文件，原用户日志SHA保持。

严格旧树保护检查如实失败：开工64旧工作树中63树仍完全一致，claude/xess-flow-tune-20261012在本轮期间有并行未提交编辑；该树由另一任务使用，本轮未写入、未回滚或合入它。全部原分支tip与main保持。没有修改旧start/guard来制造通过；差异见worktree-audit.json。

工具夹具出现过失败：独立单测编译器PATH/Windows环境键解析、复制构建脚本时反斜杠转义、旧Ninja图没有settings_transaction目标、负对照的目录断言误包含其自身输出目录，以及GUI日志实际在app/logs而非data-dir。均只修E盘夹具后重新验证；原测试单测5失败和旧GPU2误重置是预期缺陷证据，未冒称通过。另一次归档旧GUI日志因目录名写错未复制，初次console仍在153322，未将该次夹具计为通过。严格旧树差异保留，不恢复并行任务。没有通过改产品迎合夹具。

## 还不能声称什么

上述224帧是同步GPU回归，不能算实时吞吐或物理屏幕帧率；不把221→223说成普遍性能百分比。本机没有反馈者的Elgato4KPro/5070Ti/120Hz组合，尚未验证它的实际提交FPS、VSync阻塞及长时间行为。驱动给出的48fps时间尺度与60fps回调不一致仍保留原始时间戳，本轮没有强行重定时。真正GPU欠速、截止时间耗尽或外部模块持续持有资源仍可出现预算告警。下一步只需该用户用此构建、原设置重测并提供新日志，确认误重置消失后再判断剩余准入损失。

收尾仅删除本任务tmp下7个已解析核对的临时子目录，792文件/70,852,248逻辑字节；不把逻辑字节冒充精确物理释放量。任务脚本、实际构建、私有可用运行目录、profile和必要证据保留，详见cleanup.json。

可用本地测试程序：E:/项目/Veyra/tests/fg-capture-recovery-20261012/app/veyra_qml_ui.exe。无新发布包、用户配置未改，Git提交/存档和存储整理见reports/fg-capture-recovery-20261012/final.json。
