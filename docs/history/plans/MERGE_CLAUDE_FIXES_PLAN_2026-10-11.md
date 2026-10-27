# Claude 修复本地合并与交互验收

用户要求“把目前分支claude做的一些修复合并到main”。按最近未合入的完整修复分支整合；历史 no-merge 被本次明确授权覆盖。没有新推送、包、发布或关机。

## 来源固定

| 分支 | tip | 最终范围 |
| --- | --- | --- |
| claude/vfg-20261010 | d3803ab | 跳过预算组时保留VFG输入历史、测试诊断、负载提示；倍率降档和连续CUDA实验已撤回 |
| claude/pipeline-20261010 | be834fd | Present统计、重复/失效组配对校准、同步说明；交换链预等实验已撤回 |
| claude/freeze-fix-20261010 | d0694d7 | NVIDIA浮窗/游戏加加兼容、引擎无进展诊断、有限等待退出 |
| claude/dedup-cadence-20261011 | 67574f2 | CPU画面内容去重与有界内容节奏；硬解来源保留原兜底 |
| claude/audio-reader-20261011 | 87ca1e5 | 文件音频独立读包、有界队列、读盘阻塞诊断 |
| claude/elgato-hdr-20261011 | 147e76c | Elgato格式/色调映射和P010混合色度范围 |
| codex/ui-smear-handoff-20261011 | 29e33df | 软件图标/虚线裁剪、保持局部更新与33ms统计 |

各分支详细原因、失败与边界分别见自身同日记录，不将待实卡确认的候选宣传为根治。

## 顺序及保护

1. main基线5ba186a；独立不可变archives/merge-claude-fixes-20261011/start.json与source-before.bundle保护54旧工作树/496脏文件，保留各来源tip。隔离worktrees/merge-claude-fixes-20261011，所有产物E:/项目/Veyra对应用途子目录。
2. 按表顺序merge --no-ff完整tip，防止挑选提交漏掉负优化revert。AGENTS/WORKLOG独立历史追加冲突用三方union保留记录，源码/翻译不用union。初次整合源码自动合并，无源文件冲突。
3. 复查共同接点：EngineController的去重/配对/看门狗、EnhanceGraph的VFG/输入范围、QmlPlayerBridge的状态/浮窗、软件UI的clip。已发现看门狗通知读取的hangPreviousStatus在watchdog成员前销毁，须在控制器析构体内join后解除回调，以避免退出竞态。
4. 重编所有变更及头文件依赖闭包，显式新Watchdog/DeckLink、MOC、catalog和YUV shader；复用已验不可变对象时核对SHA并如实标明。实际CMake配置检查新增源、测试栈选项及依赖，不用关闭功能的配置冒充最终GUI能力。
5. 合并后定向覆盖音频拖动/恢复/慢读、内容节奏/60→30、Elgato颜色/采集合同、VFG GPU、设置重建/回退/关闭项、生命周期、QML输入布局、真实GTA播放、软件UI裁剪与卡死诊断。GPU测试串行、不加竞争负载，每进程≤300秒。优先复用既有测试入口，只在共同接点确有缺口时补充。
6. 保持VTelemetryValue SHA、默认GPU优先级、既有SR实验撤回；核对DLL/SDK/模型及原测试包逐文件SHA，真实配置不动。记录所有失败并修复后再计通过。
7. 测试通过后冻结整合tip，main --no-ff合入，确认完整Git树相同；文档记录源码/构建/日志/现场边界。源分支/其它工作树不改、不删除；本地main最终干净，checkpoint/bundle/最终回执保全。
