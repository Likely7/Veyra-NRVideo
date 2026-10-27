# 日志50：采集补帧误重置修复

2026-10-12。当前用户授权“尝试修复”，起点 main a167831，隔离分支 codex/fg-capture-recovery-20261012。main/既有树不改，不推送或制作发布包。

## 证据与边界

原日志 SHA256 cf2ea29a124385e62bf65858c89ff6b449303aebe4f17d01087988dfca706e55；诊断与最小生产代码复现在 E:/项目/Veyra/reports/log50-fg-diagnosis-20261012。Elgato4KPro 协商60Hz、回调约59.94，常见样本 PTS/时长20.833ms，周期性出现1ms短增量。SceneCadenceAnalyzer 将这个单帧增量作为下一帧的基线，下一正常间隔被误判为断点。733条场景边界中726条 cadenceBreak=true。这是已证实的软件缺陷；导致驱动时钟变化的外部条件未确定。

日志另有负补帧截止时间和 Present CPU 阻塞，以及切换后 CreateSwapChainForHwnd/ResizeBuffers 失败。当前 main 已包含108cbaa交换链复用、失败resize保留旧缓冲；先做真实对象/像素回归，不凭模块名字归咎某个叠加层，不重复引入未经验证的窗口替换。

## 实施

1. 存档64旧工作树/515未提交文件的状态和SHA、main增量Git bundle，建立独立工作树。原日志、所有已有包/运行库保持原样。
2. 将单个上一间隔基线改为固定小窗口的稳健基线，短异常不能污染下一帧；不改源PTS/样本时长、不删真正画面、不硬锁60Hz。保留 sourceStep 跳帧归一化、真实大间隔、重复帧、切镜、显式reset语义。先在旧代码上确认新回归失败，再修复。
3. 同一生产库做连续时序回放与实际GPU补帧验证；补充DLSS/VFG切换持有外部COM引用、resize失败后继续出画的真实回归。若发现新的根因，只加有证据的最小接点，不提高优先级、放宽补帧截止时间或增加排队掩盖问题。
4. 构建当前依赖闭包和应用，运行场景/采集时序/呈现回归以及用户GTA视频定向回归；原件校验，写REPORT/WORKLOG，封存已验源码。现场Elgato/5070Ti/120Hz仍需用户重测，不能承诺恢复固定120fps。

## 输出与验证

产物统一 E:/项目/Veyra/{archives,build,tests,logs,tmp,reports}/fg-capture-recovery-20261012。构建复用不可变缓存，明确记录实际重编对象，不能冒称clean全量。测试每进程≤300秒，构建≤900秒；GUI不增加Timer、不改33ms统计/250ms状态节奏。无 proprietary runtime/SDK/模型进入Git，不修改真实配置。失败回执保留，收益仅按本轮实际对照报告。
