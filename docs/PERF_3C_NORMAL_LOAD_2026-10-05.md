# 2026-10-05：正常负载匹配对照

用户要求“测一波看看”，本轮先验证同一压力测试构建卸去额外负载的表现，并交错复测优化前基线。不是新一轮压力测试；不会启动竞争进程、关闭用户其他应用或修改全局驱动配置。

## 固定条件与存档

任务目录沿用perf-nr-20261004。before为5782948 / checkpoint/perf-nr-3c-normal-load-before-20261004；标签日期为原任务日期，测试发生在2026-10-05（Asia/Taipei）。

- A：封存优化前产品8cdc612，EXE b7081f0e6045bc791105bc80de364247e169e3522891ef50cf9f62bbbfc10eb4，tests/perf-nr-20261004/app-A。
- B：确实用于先前十二轮压力测试的EXE 97dbccbfa029e4604e308c1df4198742c8e505c1510b891715f3aaf4b9afeb34，tests/perf-nr-20261004/app-B2d。build/B2d已重新链接完整COMPUTE候选，不能用其当前文件冒充同一压力构建；本轮显式验证并复用封存artifact。
- RTX5070 / 616.56；M1 1080p30，SHA03b2a0dc…；Lecram NR310.8.3.0，F95原字节。两层NR内1080p、DLSS SR到4K、DLSS2X、时域关闭、1280×800、普通GPU进程/呈现优先级。
- 每次独立profile与日志，50秒正常播放，取10–48秒稳态。A/B、B/A、A/B三轮，共六组；串行，单产品进程280秒上限，不与构建或其他本任务GPU测试重叠。
- replay驱动固定EXE/runtime/source哈希并记录所有EXE/DLL/QML的artifact manifest。复用只更新已有测试Loader的perf-run.json，产品载荷保持；harness调用sourceCommit不是已封存EXE的编译源码，不将两者混同。

## 指标与结论

原三轮六组和一对确认，共八组，已结束且没有遗留测试进程。退出/效果配置/固定EXE、source/runtime哈希通过，所有EXE/DLL/QML artifact manifest在测试结束后再次逐文件核对一致。连续性异常仍待定位，不能以进程PASS代表丝滑验收。

| 轮次 | A的NR | B的NR | A增强合计 | B增强合计 | A/B全日志最大累计预览跳帧 |
|---|---:|---:|---:|---:|---:|
| 1 | 7.480 | 7.267 | 21.448 | 21.219 | 0 / 0 |
| 2 | 7.305 | 7.237 | 21.340 | 21.271 | 0 / 0 |
| 3（异常保留） | 8.846 | 8.813 | 23.8155 | 21.545 | 89 / 13 |
| 4（另存确认） | 7.287 | 7.245 | 21.204 | 21.169 | 0 / 0 |
| 全四轮中位 | 7.3925 | 7.256 | 21.394 | 21.245 | 不作平均掩盖异常 |

单位ms（跳帧列除外）。原三轮中位A/B NR7.480/7.267、增强21.448/21.271，单对确认A/B NR7.287/7.245、增强21.204/21.169，原结果未被确认组替换。全四轮含异常的NR变化-1.85%、增强变化-0.70%，小于本轮波动，不宣称持续播放提速；该设置没有测到优化后普遍更慢的NR成本。八组稳态窗口均1280×800、active=false，说明本轮在非前台运行，但不能推广为所有后台掉帧已修复。稳态GPU频率主要2812–2827MHz，未锁频。

NR字段为最后一层滚动P95的稳态中位；增强合计为每帧增强区间合并去重后滚动mean的稳态中位，A3有一行mean=-1表示该次统计暂不可用，原CSV与receipt保留，不当成负GPU耗时。软件Present间隔另从成功提交日志计算，四轮P99统计量中位A17.25765/B17.2475ms，不将Qt Timer当FPS或软件事件当物理扫描。所有轮次保留，不择优；GPU频率/使用率记录供核对。

同一B EXE97dbccb…先前2X普通队列有三NR竞争：NR三轮中位11.367ms、增强27.653ms；卸去自有压力后上述四轮中位7.256/21.245ms。高读数与竞争条件密切相关，没有出现之前担心的普遍NR回退；环境仍有正常波动，不能将不同时间的压力/正常差值当成某项代码优化收益，也不能排除其他未测设置的回归。

命令：py -3.11 -B scripts/perf/nr-normal-load-compare.py normal-load-20261005-v1。日志与数据：E:/项目/Veyra/logs/perf-nr-20261004/normal-load-20261005-v1-*，summary内artifacts.json、completed.json、comparison.json、per-run.csv。测试/临时路径为同任务tests/tmp各label。通过后先向用户汇报这一波结果，不把本轮诊断当成3c完整COMPUTE性能或全方案完成。

## 连续性异常与有界确认

原第3轮A软件提交max2843.8827ms，稳态日志previewSkipped最大87、全日志最终89；engine-stall frame1328 total3008.731ms、controlAndSchedule2946.789ms、graphSubmit1.141ms，来源跳跃触发历史reset。每秒UI状态采样日志间隔约3.8s，独立16ms UI Timer的最大间隔实际2729ms，两项不混同。B第3轮max139.9184ms、稳态previewSkipped累计12–13、过期生成12–14，其最大engine-stall261.255ms（controlAndSchedule191.625ms、graphSubmit68.756ms）。两轮均不能称“完全丝滑”或用PASS退出掩盖这些事件。根因尚未证明，不能直接归因外部负载；原meter和log保留。

新增confirm参数只补一对B/A第4轮，单轮50秒，不替换原六组，不把单对确认当成三轮稳定性证据。命令：nr-normal-load-compare.py normal-load-20261005-v2-confirm confirm；路径同任务label-summary。确认前源码存档561ea60 / checkpoint/perf-nr-3c-normal-confirm-before-20261004。

确认B/A预览跳帧均0，最大软件提交间隔71.7471/69.3008ms，生成过期分别1/2。秒级断档本对未复现，不等于根因解决。其余正常轮也存在约69–73ms提交长间隔，六个非异常轮在源frame303/304附近约99–105ms engine周期（graphSubmit约68–72ms），同素材位置A/B均出现，作为既有调度/图提交调查线索保留，不称新增NR回归或已经定位。

汇总证据：normal-load-20261005-v1-summary/all-eight-review.json与all-eight.csv，包含八组完整路径、原三轮和单对确认分组、未剔除的异常、UI前台状态/计时和GPU快照。此次不改产品；测量收尾按checkpoint/perf-nr-3c-normal-load-docs-20261004及checkpoint/perf-nr-3c-normal-confirm-docs-20261004存档，收据同任务archives对应*-docs/checkpoint.json。下一步连续性异常列入R0，3c完整COMPUTE性能仍待，不把本诊断当全方案完成。
