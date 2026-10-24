# 2026-10-24 补帧调度降档修复与真采集对照

已在隔离分支`codex/fg-scheduling-repair-20261024`修复一个明确的周期性阻塞：显示器刷新/VRR查询原本在播放线程同步执行，采集帧已读取、准入期限已开始计时后，每约两秒调用一次NVIDIA显示状态接口。该组只读查询在本机耗时10.5–22.0ms，足以让约3ms的VFG也错过期限。改为单个在途后台查询，播放线程仅非阻塞取结果；窗口/显示器/图生命周期改变时丢弃旧代结果，异步失败保留缓存并限频重试。跟随显示器限帧和VRR策略在新结果到达后重新应用，不拿未知值或旧显示器状态当当前值。

未扩大准入期限、未隐藏降档提示、未堆积帧、未降低用户设置倍率；`FgRecoveryBudget.h`、`LivePairLatency.h`、`PresentationScheduler.h`、`LiveStatusHistory.h`、`EnhanceGraph.cpp`、`CaptureCardSource.cpp`内容相对97959cc保持不变（Git换行规范化比较）。此前诊断中的真实GPU超预算依然应降档；本修复不保证任何负载都不跳帧。

## 用户要求的2K60 NV12真采集

用户明确追加要求后，在无其它Veyra进程占用时，以私有配置顺序运行KUHAIMI G50。每例都核对驱动日志：`SetFormat`/`ConnectDirect`成功，实际`2560x1440 nominalFps=60.000`，上游子类型`0x3231564E`（NV12），无转换滤镜；回调5529600字节与1440p NV12相符。NR/SR关闭，NVIDIA VFG2X Medium，显示同步Automatic，输出限帧关闭、VRR开关关闭，音频禁用以隔离视频问题。完整原60与主动60->30分开对照。旧基线为nr1（d43a5384…60289），修复为fg1（32296020…a6a），运行库与QML/测试步骤相同。

各例先预热6秒，再测量40秒。下表使用该窗口内首末**同会话/同revision累计计数之差**，对应计数区间约38.6–39.2秒，绝非把限频日志行数冒充丢帧总数。

| 路径 | 版本 | 计数区间 | FG候选 | 准入前跳过 | 生成后过期 | 平均采集回调 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 原60→VFG2X | nr1旧基线 | 38.600s | 2316 | 10 | 2 | 60.00fps |
| 原60→VFG2X | fg1修复 | 38.600s | 2316 | 0 | 0 | 60.00fps |
| 主动60→30→VFG2X | nr1旧基线 | 39.166s | 1175 | 13 | 0 | 60.00fps |
| 主动60→30→VFG2X | fg1修复 | 39.100s | 1173 | 0 | 0 | 60.00fps |

四例计数区间的邮箱覆盖均为0。旧版被限频采样记录到的准入前CPU最大耗时分别13.188/16.729ms；修复版相应采样最大0.151/0.210ms，后台查询仍耗时最高14.602/14.366ms，但不再由播放线程等待。此采样最大值不冒充全部帧的真实最大值。两组修复中所有候选均完成评估并实际提交生成帧，不能用减少候选/关闭补帧解释通过。原帧数、主动半帧计数保持真实。测量结束后才抓一张处理后画面，实际2560x1440、非黑，已人工查看；截图不用于断言生成帧视觉质量、扫描率或屏幕端到端延迟。

这证明本机该配置下的周期性问题得到修复，不能扩展为长期零掉帧、其它卡/后端/重NR/HDR全部通过。日志里其它长控制/提交停顿、过高GPU负载仍可能触发提示。

## 其它验证、命令与失败记录

- `scripts/acceptance/build-recording-handoff.py --task codex-fg-scheduling-repair-20261024 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.0-fg1 veyra_qml_ui veyra_async_display_query_tests veyra_presentation_worker_tests veyra_realtime_preview_tests veyra_hdr_display_state_tests veyra_ui_contract_tests veyra_repair_contract_tests`：配置/构建通过，50.3秒，-j2。后仅重编读显示探针测试目标2.8秒，应用EXE未变化。
- 六项CPU合同全部通过：异步查询10项（阻塞驱动时1000次owner轮询不等待、不堆worker；关闭重开/旧代丢弃/异常重试）、调度/预算127项、实时预览57项、HDR显示状态、UI合同含384布局、Repair272项。Repair首启缺FFmpeg运行路径报0xC0000135，补入现有Qt/FFmpeg依赖PATH后返回0，保留初始失败，不归为产品修复。首次保护文件原始字节比较因CRLF工作树/LF Git blob不同而拒绝，改为Git换行规范化比较六文件均无内容改动。
- `veyra_async_display_query_tests.exe --probe 0`：只读显示状态探针，无D3D设备/队列/捕获初始化，八次VRR查询10.5–22.0ms，刷新率查询约0.01–0.16ms。
- `python -X utf8 -B -I scripts/acceptance/fg-display-query.py`：真实4K30文件，VFG2X/4X、跟随显示器320Hz/VRR、暂停恢复、关闭重开通过；后台查询15次，媒体时钟继续，VFG实际生成提交，未借此替代真采集。
- `python -X utf8 -B -I scripts/acceptance/fg-capture-query.py --case <baseline-transport|fixed-transport|baseline-half|fixed-half> [--baseline] [--half] --seconds 40`：上述四个真采集用例均通过。
- `python -X utf8 -B -I scripts/acceptance/analyze-fg-capture-query.py`：生成按测量窗口的累计计数对照。探针、用例与分析脚本在源码，媒体/截图/配置/日志不入Git。

证据根目录`E:/项目/Veyra/reports/fg-scheduling-repair-20261024`；原始实时快照`archives/fg-scheduling-repair-20261024/live-0949.log`；构建`logs/codex-fg-scheduling-repair-20261024/build-20261024-095142`；四例日志`logs/fg-scheduling-repair-20261024/capture-*`，私有配置和截图`tests/fg-scheduling-repair-20261024/capture-*`。每次测试独立profile/log/TEMP，共用一份任务app，非可变输入才共享硬链接，未改供体DLL/模型，不产生多套实体运行库。

## 本地交付

`Veyra-2.1.0-fg1-NVIDIA-update.zip`为需已有完整2.1.0 NVIDIA底包的小更新层。关闭播放器，把文件解压覆盖原包根目录；包含NR手动比例及此前旋转/录制改动。更新不含DLL/模型/SDK/用户配置/日志，身份以`LOCAL_UPDATE.json`为准。源提交、ZIP/EXE SHA与逐文件核验见同任务`update-package.json`。原用户E:/App包未由Agent覆盖，未合main、未push/Release；Lecram暂停。本任务保留当前测试app与必要验收证据用于续测，没有复制整工程备份。
