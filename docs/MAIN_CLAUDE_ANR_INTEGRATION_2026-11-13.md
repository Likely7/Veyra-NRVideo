# Claude／Codex／ANR 本地主线整合（2026-11-13）

状态：组合产品2c278af的相关验收已通过，本轮已合入本地main。当前用户明确要求整理两边修复并合main，不包含推送、发布或关机。计划见[整合计划](MAIN_CLAUDE_ANR_INTEGRATION_PLAN_2026-11-13.md)。

## 来源与范围

| 来源 | 固定身份 | 处理 |
| --- | --- | --- |
| 原本地main | 8332c8a8382242096a2699d3ba4dd52ad47bbd74 | 检查点checkpoint/pre-main-claude-anr-20261113 |
| Claude后续修复 | 活动树6e3747a上的20个未提交文件 | 原字节归档，独立提交d4be12d25e825bc3ef0b4fe6ae931fdac8a0763f，原脏树不动 |
| Codex／ANR链 | bc4cbfa86c343e9906b3830fcc2927b6039a7d04 | 保留原提交历史合入候选 |
| 组合产品候选 | 2c278af83d09823141eb5e7d297552e5ff03cbfe | 独立构建及回归 |

Codex链不只ANR：还包括此前未合main的全效果遮罩、独立NR节点、美颜层删除、PSNTools Account ID入口、去重选项精简、移除Boost、原生采集交接、日志和送帧、自动识别/60→30。分支祖先清单已核对；只有既有Lecram两线、AVerMedia诊断存档和独立原生音频原型不在组合历史，继续按原状态保留，不从旧分支恢复被撤销的实验。

Claude改动原样保留：

- 到期帧先送出、图开始前和VFG提交后的不等待检查点。
- 1% low按Present调用开始计时，提交FPS仍计真实完成；不当成面板刷新率。
- 图像比较在新画面已确定时提前停止，参考拷贝放到发布后；重复和小幅变化保持精确判断。
- Reflex睡眠异常保护和日志退出窗口修正。
- 0–1ms的自动期限余量，排除只是线程晚看到的样本。
- 能在一个源间隔内完成的重档补帧允许相位容纳FG成本；自动去重原有上限保持。
- 持续拒绝1秒后真实帧就绪即送，探测生成帧过期，连续8对接纳才恢复FG相位。

仅AGENTS、WORKLOG有追加冲突，保留双方。C++与i18n自动合并后自审：EngineController保留ANR独立错误回退与Claude调度；EnhanceGraph保留ANR初始化、像素运动/资源边界及Claude检查点。43个非共同修改的侧分支Git blob精确一致，见SOURCE-AUDIT.json。没有独立Reviewer，本轮单Agent。

## 独立施工与验证

源码工作树 `E:/项目/Veyra/worktrees/main-claude-anr-20261113`；build/tests/logs/reports/tmp分别使用同任务 `codex-main-claude-anr-20261113`。不覆盖Claude共享构建，也不替换旧ANR测试程序。运行供体使用前轮已核验ANR私有stage，固定原始组件字节；QML为私有副本，替换EXE用新文件+原子替换，避免改硬链接供体。

17目标独立构建332.4秒通过，标签2.1.5-main-integration-test。实际命令：

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-main-claude-anr-20261113 --root E:/项目/Veyra/worktrees/main-claude-anr-20261113 --label 2.1.5-main-integration-test --jobs 2 veyra_qml_ui veyra_logger_tests veyra_live_timing_tests veyra_presentation_worker_tests veyra_realtime_preview_tests veyra_repair_contract_tests veyra_anr_settings_tests veyra_preset_library_tests veyra_vfg_settings_tests veyra_qml_quick_tests veyra_live_presentation_tests veyra_repair_fg_tests veyra_hdr_enhancement_tests veyra_vfg_export_probe veyra_fg_lifecycle_tests veyra_effect_chain_tests veyra_ui_contract_tests
py -3.11 -X utf8 -B -I scripts/i18n/extract.py --check
```

构建回执logs同任务/build-20261113-051520/receipt.json，源码为已提交且干净的2c278af。后续只整理文档时不把文档提交冒充另一次产品重构建。

已完成：日志12、时序238、调度179、实时预览57、合同308、预设629、ANR设置48、VFG设置683、效果链466及UI合同通过。QtQuick199/0/0；首轮进程exit0但默认stdout空，未据此认定测试通过，改显式txt/junitxml文件后取得完整199项结果。翻译2797条，三语缺失0/占位错误0。

ANR估算光流2X＋精确光流2–6X共176生成子帧全部像素/时间戳真值通过；HDR纵向11帧、与原生DLSS共存11帧、双ANR销毁同伴后11帧通过，各组D3D12 debug errors=0。有意持有帧租约后覆盖被拒的ERROR为负例断言，不算未知运行失败。HDR6X＋NR/SR再次20次NR、20次SR、90个子帧，约998.9nit亮部及JXR对比通过；6→2→5→3→4→6的resize/reset/重建通过。产品导出HDR5X为270源→1350输出，SDR6X为60→360，4K NR+SR+ANR2X为60→120；后续独立解码和GUI子进程结果以收尾节为准。

## 保留的问题与验收边界

Claude已发现的VFG高档窗口合成问题没有被本次合并“修好”：特定1440p/4K、30帧内容下，内部提交60而系统显示事件只有30–34，全屏正常；GPU优先级5与CUDA长任务阻塞DWM是机制推断，不是已经抓到的唯一根因。本轮不擅自改长期GPU优先级；原始证据仍在reports/claude-game30-live-20261113/FINDINGS.md。

VFG长期拒绝时真实帧延迟明显降低，但每2–4秒预算探测仍可能拖慢几帧，1% low可下降；单线程检查点送帧限制、VFG参数切换重建数秒、去重引擎CPU额外开销等未在本轮另行改造。保留事实，不恢复已撤销的预读/等待/预算实验。

本次是本地main整合，公开Release仍为原2.1.5字节。按2026-11-12固定提交与B15调研，当时未取得ANR核心源码/明确分发许可，运行组件留本机私有目录，源码Git不含DLL/SDK/模型。原生ANR SR/RR无可用组件；节点图导出仍是所有后端共同的未接入功能，列表导出另验。本机RTX5070的通过不代表AMD/Intel/其他型号或所有游戏画质，也没有屏幕光学延迟证明。

原Claude20文件的原始归档在archives同任务，开工与收尾指纹在CLAUDE-SNAPSHOT.json/INTEGRITY.json；原ANR分支与程序保留。可复现驱动和全部组合回执在reports同任务，最终main提交/测试入口/组件身份见FINAL.json。

## 组合验收收尾

- 7组生产回放覆盖ANR稳定/变帧率/60→30/每17帧丢失、VFG半速/变帧率、DLSS变帧率；无已观察新画面误丢/重复，PTS单调且不借未来时间。ANR稳定30→60约59.95提交FPS，P99间隔16.95ms；ANR60→30约59.95、P99=17.20ms；VFG60→30约59.95、P99=17.19ms。变帧率/静止段不套固定60fps门槛。
- 完整60秒诊断片3600个输入决策逐条覆盖，0个已观察新画面误丢/重复；日志无隐藏丢行，时间戳有效。原始不均匀素材/静止恢复仍有长间隔，不宣称所有场景零卡顿。
- 实际GUI5X、暂停改6X、跨DLSS切换、节点5X、预设和重启通过。非法320×180尺寸正确关闭ANR并继续60提交FPS，界面回退1X。GUI独立导出子进程输出300帧/236生成/0失败。
- 四个输出独立解码全部计数正确、PTS严格递增，与源PTS插值最大误差≤0.667微秒；画面非黑、4K尺寸正确，HDR为Main10/BT.2020/PQ。没有用名义FPS替代真实PTS。
- T911 4K60 NV12＋1080p NR：DLSS4X＋半速段末120提交FPS/1%low113.18，累计captureDropped1；VFG高3X＋半速因预算不足保持约30提交FPS/1%low26.62、captureDropped14，日志确认LiveFgRejectionHold进入保护，不能说高3X正常达到90fps；ANR自动2X段末60提交FPS/1%low58.76、captureDropped0。ANR自动组1061个采样、531个不同picture、maxDiff102，确有动态输入；无人工逐帧画质真值或屏幕光学测量，不能宣称所有游戏画质/延迟通过。
- 97既有运行二进制与19ANR原件逐SHA不变，104个产品QML与组合源码一致，测试探针已恢复。QtQuick额外组件来自本机已安装Qt，只作本地回归。Claude20原始文件、branch/head/status/diff指纹完全保持；旧ANR工作树和旧交付程序未改。
- 独立启动器实际启动/退出0，私有配置默认效果关闭、不自动打开输入。EXE SHA-256 c343e4286a429357f6530ad65847d0fec927e367b16ad460cd4ebca6e8cb1c09。测试入口E:/项目/Veyra/tests/codex-main-claude-anr-20261113/启动main整合测试.cmd。当前本地测试目录保留回归工具和证据，不是新公开发行包。

本轮没有新的产品实现修改，组合代码与2c278af保持一致；整理提交仅更新文档/当前入口。原main检查点、Claude原样快照和ANR提交均可追溯。实际main合并提交与最终工作树状态以reports同任务FINAL.json为准。
