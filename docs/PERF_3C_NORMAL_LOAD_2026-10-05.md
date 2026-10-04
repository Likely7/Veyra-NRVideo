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

原六组测量完成，NR每轮A=7.480/7.305/8.846ms，B=7.267/7.237/8.813ms；尚在核对连续性异常。NR字段为最后一层滚动P95的稳态中位；增强合计为每帧增强区间合并去重后滚动mean的稳态中位。软件Present间隔另从成功提交日志计算，不将Qt Timer当FPS或软件事件当物理扫描。三轮均保留，不择优；GPU频率/使用率记录供核对。

命令：py -3.11 -B scripts/perf/nr-normal-load-compare.py normal-load-20261005-v1。日志与数据：E:/项目/Veyra/logs/perf-nr-20261004/normal-load-20261005-v1-*，summary内artifacts.json、completed.json、comparison.json、per-run.csv。测试/临时路径为同任务tests/tmp各label。通过后先向用户汇报这一波结果，不把本轮诊断当成3c完整COMPUTE性能或全方案完成。

## 连续性异常与有界确认

原第3轮A软件提交max2843.8827ms，稳态日志previewSkipped最大87、全日志最终89；engine-stall frame1328 total3008.731ms、controlAndSchedule2946.789ms、graphSubmit1.141ms，来源跳跃触发历史reset，UI Timer也出现约3.8s断档。B第3轮max139.9184ms、稳态previewSkipped累计12–13、过期生成12–14。两轮均不能称“完全丝滑”或用PASS退出掩盖这些事件。其根因尚未证明，不能直接归因外部负载；同轮GPU频率约2812–2827MHz、GPU使用率较前轮增加，原meter和log保留。

新增confirm参数只补一对B/A第4轮，单轮50秒，不替换原六组，不把单对确认当成三轮稳定性证据。命令：nr-normal-load-compare.py normal-load-20261005-v2-confirm confirm；路径同任务label-summary。确认前源码存档561ea60 / checkpoint/perf-nr-3c-normal-confirm-before-20261004。
