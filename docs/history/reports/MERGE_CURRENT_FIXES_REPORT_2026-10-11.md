# 当前 Codex / Claude 修复整合验收（2026-10-11）

用户明确要求将两边当前修复合入本地 main。本轮从 `049902b` 在 `codex/merge-current-fixes-20261011` 整合，实测产品tip `4904b73a12308a9f98ba0b3bffd20aa470b41fec`；main实际合并提交和最终核对见本报告收尾及 `E:/项目/Veyra/reports/merge-current-fixes-20261011/final.json`。没有推送、打包、发布、换版本或关机。

## 整合内容

- Codex `4557ab6`（包含接手Claude的 `e8300d9`）：DLSS SR K/M/L模型选择、旧预设/列表与节点预设/session持久化；按实际内容节奏、同步、上限、播放速度和对比状态判断有效目标，在状态上方显示选项；保留真正欠速告警；设置重建与seek同时发生时，确认跨循环保留到首帧成功处理。
- Claude `1d4c33f`（包含 `58dc123`）：VFG高+中/高+低/中+低可选档、每对原帧一次GPU镜头切换检测、关闭逐子帧SDK自动检测。默认中档不变，没有新CPU像素回读。此前六Claude分支及UI裁剪修复已在main，不重复应用原脏补丁。
- 解决AGENTS历史追加、翻译目录、bridge VFG估算三处冲突：VFG混合成本提示采用实际内容帧率，保留正常限速判定；翻译以 `(zh, ctx)` 为键保留两边所有原词条。VfgBackend/新shader与Claude tip逐字节相同；DLSS后端、事务确认、状态策略及VTelemetryValue与Codex tip相同。
- 两条旧未合入分支仅剩RTSS性能研究文档和明确已结束的圆刚未完成诊断工具，按历史用户决定保留分支，不恢复旧实验。已撤回的SR/交换链负优化不恢复。

## 本轮验证

`python -X utf8 -B -I E:/项目/Veyra/tmp/merge-current-fixes-20261011/build.py`：42项源文件/头文件依赖闭包、13个EXE、翻译和新VFG shader，67.9秒通过。复用已验对象并逐SHA核对，**不是clean全量构建**；显式DeckLink ABI与带zlib/PS5 slices256的FFmpeg保留。GUI SHA256 `05721e1676ef29841ab204a24f46858c7ed4ab17313a0f169f4f8a9ce2eaaddd`。私有构建沿用上一测试的开发显示标识，不代表新发行包。

`.../tmp/merge-current-fixes-20261011/configure.py`：当前完整CMake配置及私有软件UI probe编译链接通过，核对Watchdog、VFG shader、PS5/Moonlight/Xbox/DeckLink、回放测试栈及未恢复QuickPrivate。生产EXE不含probe驱动。

`.../tests/merge-current-fixes-20261011/harness/regress.py`：16项全部通过，包含效果/参数合同、预设库/旧预设、状态策略、VFG设置、HDR颜色、VFG异步/同步、实际视频设置重建/回滚、关闭效果器编辑的列表/节点路径，以及数字输入/菜单/组件。VFG两路径各2425检查、设置683检查。单测试最长99.8秒，未超过300秒。

`.../harness/ui-smoke.py`：真实生产界面12种状态、6个简中/繁中/英/日布局检查通过，耗时60.3秒。包括正常限帧15、同步DLSS4X、对比30、慢放15、组合上限12，及三种VFG混合档实际切换；中+低在上限15时正常，高+低/高+中在1080p30测试视频上均提交120fps。这里是软件提交帧率，显示器实际320Hz，没有假称60Hz实机限速或物理上屏帧率。

`.../harness/sr-ui-smoke.py selection restart`：29.8/5.3秒通过真实K/M/L入口、六种VFG质量入口，列表M+中低与节点L+高低组合预设的保存/应用、独立session和重启恢复。先前夹具只创建SR节点却查找FG节点，17.3秒时报错退出3；补建FG节点、使用新私有profile后通过。首轮失败JSON/日志/profile保留，没有修改产品迁就测试。翻译合并准备脚本也曾遗漏ctx键，断言阻止写入目录；补全ctx后保留原Close/Off、Play/Playback区分。这两项均非产品故障。

UI没有新增生产Timer或整窗重绘，33ms统计/250ms状态节奏及GPU默认优先级保持。本轮没有重跑性能A/B，不把Claude历史优化数据充当合并后的新收益。

## 证据与边界

所有新产物位于 `E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/merge-current-fixes-20261011`。`seal.py validate`核对833份源码/配置、595份构建源输入、327份不可变构建输入、13份EXE、56份实际部署QML/JS、1626个原包文件，以及59原工作树/504脏文件；最终main更新后其余58旧树仍须完全相同。每进程测试≤300秒，生产Main.qml已恢复，所有测试用私有data-dir/TEMP/CUDA缓存，真实用户配置与运行库原件不变。源码Git不含新增SDK、DLL或模型。

AMD/其它NVIDIA型号、真实采集卡、HDR实际导出及长期显存没有在本次重验；VFG混合档的主观观感仍由用户测试，NVIDIA长期显存问题没有被本次合并宣称根治。原正式/测试包没有更新。

## 合并收尾

实测产品 `4904b73a12308a9f98ba0b3bffd20aa470b41fec` 通过后，仅追加报告/WORKLOG得到整合记录tip `adcf509e8a752d8a60645c894262cd5dd43f33ad`。本地main执行 `git merge --no-ff codex/merge-current-fixes-20261011` 得到功能merge `d2e1396cbfafe7137829ec5f4264c86e7ce6b519`，无冲突；`git diff --exit-code codex/merge-current-fixes-20261011 HEAD`核对完整Git树相同。随后只补AGENTS/CURRENT_STATUS/WORKLOG/本报告四份文档，没有修改实测产品。

`seal.py final`复查833份源码/配置、13份EXE、所有通过/失败回执SHA、原包1626文件，验证来源tip均为main祖先、main clean、其它58旧工作树/504脏文件保持。开工/实测/合并标签保留，最终main提交与增量源码bundle及SHA写入 `E:/项目/Veyra/reports/merge-current-fixes-20261011/final.json`。原分支保留；无推送、打包、发布或关机。

首次收尾原始SHA检查在旧main的ColorDetailPass.h停止。对833份输入排查确认9个旧文件在main为LF、新工作树为CRLF；仅CRLF→LF逐字节比较全部相同，完整Git内容差异仍只有四份收尾文档。没有重写旧文件或降低产品内容核对，最终回执逐项列出这9份两种checkout SHA，保留该次失败记录 `reports/merge-current-fixes-20261011/final-raw-hash-attempt.json`。
