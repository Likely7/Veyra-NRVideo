# VFG 真实 GTA6 各档位排查（2026-10-07）

结论：复现用户反馈。Medium 在本机这段素材中只有 2X 达标；GPU 平均耗时低于源帧预算，并不保证插帧能按输出间隔及时提交。存在两个独立问题：生成与送显共用 CPU owner 导致已就绪帧错过截止时间；降档后性能球只反映实际减少的工作，不能表示请求倍率的余量。High 高倍率另有真实计算成本不足及恢复重试尖峰。**本轮只排查，尚未修改产品实现或宣称优化完成。**

## 身份、条件和口径

- 源码起点 `af5bfc3a66afce3047868229a3070a91c0fc9c22`，独立分支 `codex/vfg-diagnosis-20261007`；不改桌面、main 或其他工作树。
- 使用正式 2.0.5 NVIDIA 包原 EXE，SHA256 `acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936`。私有副本仅加 QML 自动设置与观察 loader，无 C++/shader/DLL 修改、无重编译。VFG SDK 1.3.0 / nvidia-vfx 0.2.0.0，随包五 DLL 按原 manifest 核对 SHA256。
- RTX5070 12GB / 驱动616.56；GPU Normal，日志 `requested=2 actual=2`；独立呈现队列 Priority0。窗口1280×800，Auto同步、限帧关、严格首帧准入关。每秒维持自有 PID 窗口前台；原生视频子窗口取得焦点时，主 QML `active=false` 不能用于判定后台。
- 原片 `E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4`，本轮 ffprobe：3840×2160、H.264、30/1fps、BT.709、1608.066667秒。14115273308字节，mtimeNs1789034773254693400，与既有完整散列收据一致；复用既有 SHA256 `93db61292353f19aaf6bfa2fbd3a094d1140300785ab2d93b0c06aa7881f8b3f`，**本轮没有重新散列14GB全片**。
- NR 原版 runtime3，SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`；单层内部1080p、风格0、强度1、零运动、调控/抗闪关。SR、HDR、调色关，音频静音但保留正常媒体时钟。**VFG 仍在最终3840×2160上运行**，1080p只限定NR内部推理尺寸。
- 21组质量×倍率，固定从同一原片起始位置播放，独立进程串行；约5秒预热后观察20秒，每组20–21次1Hz样本，单进程约30–40秒。没有压力/竞争GPU负载或驱动、显示设置修改。
- 下表 FPS 是观察窗口内每秒**应用呈现提交读数的中位数**，不是物理显示FPS、ETW PresentMode或屏幕延迟。耗时是每秒已完成工作平均值的中位数；CPU P95是日志滚动窗口P95的中位数，不能称合并全段P95。脚本 `passed` 只表示采样/后端/退出契约通过，**不表示该倍率性能达标**。

## 全档位结果

| 倍率 | 30fps源目标 | Low实际提交fps | Medium实际提交fps | High实际提交fps |
| --- | ---: | ---: | ---: | ---: |
| 2X | 60 | 60 | 60 | 37 |
| 3X | 90 | 90 | 82 | 30 |
| 4X | 120 | 112 | 84 | 30 |
| 5X | 150 | 123 | 74 | 33 |
| 6X | 180 | 130.5 | 57 | 33 |
| 7X | 210 | 131.5 | 34.5 | 32.5 |
| 8X | 240 | 128.5 | 30 | 32 |

Medium8与High3–8触发调度降档；High高倍率不是实际连续完成请求倍率。Low4与Medium3起已低于目标，不能因为平均FPS比2X高就写成稳定达标。这里没有电影全长或长稳验收。

| 用例 | 增强总耗时ms | FG阶段ms | GPU占用% | CPU提交滚动P95ms |
| --- | ---: | ---: | ---: | ---: |
| Low3 + NR | 10.48 | 3.77 | 37.7 | 12.55 |
| Medium2 + NR | 11.51 | 4.49 | 39.8 | 13.52 |
| Medium4 + NR | 19.04 | 12.27 | 60.9 | 21.86 |
| High2 + NR | 25.02 | 18.12 | 77.7 | 29.12 |
| High5 + NR（降档混合） | 18.74 | 19.66 | 67.8 | 28.77 |

FG阶段包含CUDA/D3D12交接及对应工作，不能称纯模型内核时间。降档/seed组与完整组混在时间窗口里，各阶段有效样本数不同、各列分别取中位数，不能将这些中位数直接相加。源帧预算为33.33ms；High5的增强总耗时与GPU占用均低于70%绿球阈值，但实际33fps远低于150fps目标。

## 已确认的原因

### 1. GPU队列已经分开，CPU送显仍会被整组生成占住

`VideoPresenter::open` 已为应用补帧创建独立queue/fence与3个command slots；不能把“再开一条GPU队列”当成尚未完成的修复。`LiveGpuScheduler`明确要求所有回调在graph owner执行。`EngineController`同一线程调用整次 `graph->process`，然后才重新推进呈现；`EnhanceGraph`逐张调用VFG直到一组生成工作都提交完，中途没有推进旧组送显。

Medium4没有GPU准入拒绝、没有reduced、没有源帧预览跳过、没有command-slot CPU fence等待：整次会话 `fgEvaluated=2299`、`fgReadyValid=2295`、`fgInvalid=0`、`fgReduced=0`；但 `generatedPresented=1355`、`generatedExpiredAfterEval=928`。约40.4%的已判有效生成帧在送显前过期。原帧仍大致30fps，媒体时间大致一倍速。

稳态18.334秒日志差分：550张源帧被处理、1541次呈现提交、659张生成帧过期、previewSkipped=0、slotWaits=0。解码滚动P95约0.468ms、Present CPU约0.340ms，CPU整组提交滚动P95约21.856ms。4X应每8.333ms提交一帧，实测提交间隔P95约21.752ms。

逐帧 `pacing-sample` 的单调host时间与 `frame-batch` 日志交叉定位，在观察段找到569个这样的长间隔：上一组下一张图已就绪，却被下一组CPU处理覆盖超过该送显间隔的80%。具体例子：下一张属于batch177，早在batch178处理开始前14.264ms已观测就绪；batch178处理约18.047ms，两次呈现之间空了19.119ms，PTS从5833.333直接到5850ms。中间5841.667ms的候选没有及时呈现。

CPU处理起点来自单调host原始记录，结束点借助UTC毫秒日志映射，近似区间存在约毫秒级量化误差；不能用它宣称微秒精度。但19ms阻塞与8.33ms目标间隔的差异，以及独立的 `graphSubmitP95Ms` 与过期计数，结论一致。**已确认阻塞发生在整组process/提交期间，尚未按单个SDK调用测量，不能断言具体是哪一个NvVFX/CUDA API在内部等待。** 产品虽传 `NvVFX_Run(effect,1)`，仍不能把该标志当成CPU必定立即返回的实测证明。

### 2. 性能球表示实际开销，不能判断请求倍率能否按时送显

QML三个球分别使用 `chainTotalMs/stageBudgetMs`、系统最忙GPU引擎占用、再次使用 `chainTotalMs/stageBudgetMs`。第三个“预算”球并没有采用实际输出达成率；CPU整组提交间隔与过期生成帧也没有进入这三个颜色条件。

Medium4：19.04/33.33=57.1%，GPU60.9%，三个球可全绿，但提交只有84/120fps。High5：18.74/33.33=56.2%，GPU67.8%，同样全绿，实际仅33/150fps；原因包括大量reduced/seed/skip，测量的是已经少做工作的结果。不能从这组绿球反推完整High5只需18.74ms。

现有运行状态有“输出未达标/调度降档”提示，并不是完全不报告失败；问题是球的语义容易被理解成倍率容量评估。

### 3. High高倍率计算不足之外，恢复探测会制造额外尖峰

High2的单张FG阶段约18.12ms，Medium2约4.49ms；完整High5必须生成4张中间帧，不能由单张或降档组成本代替。

High5会话 `fgCandidate=3056`、`fgSkippedBeforeEval=2577`、`fgReduced=352`、`fgReadyValid=428`，生成帧实际呈现81、过期347。日志完整成本预测在7.578–79.798ms之间跳动。`FgRecoveryBudget::predicted` 的完整FG样本超过2秒未更新后，用 `value_or(0)` 合并基础成本；reduced组又不允许更新完整FG成本，因此会重新以近似基础成本放行昂贵完整组。例：source711预测7.883ms准入，source712预测76.326ms再次拒绝。High8 FG阶段的某一秒平均值曾到123.31ms。

这解释了“低负载一会儿、突然长停顿、再降档”的恢复摆动，是可改的调度问题；但不能因此宣称4K High5/8在5070上拥有完整实时计算余量。单纯放宽过期阈值会保留更旧的图并增加延迟，没有消除计算或CPU阻塞。

## 对照实验

- Medium4 + NR，Auto→手动允许撕裂：两组都84fps，19.04→18.92ms；此次现象不是由等待垂直同步造成。
- Medium4，关闭NR：84→113fps，总耗时19.04→12.35ms，仍低于120目标。说明减少基础处理可缓解，但不能消除VFG整组提交期间的节奏问题。
- High2，关闭NR：37→58fps，FG阶段约18.10ms，仍有未达标样本。不能将NR开销减少冒充VFG自身已优化。
- Medium4 + NR，关闭逐帧详细日志：仍84fps，总耗时19.04→19.02ms；详细日志开销不是本次丢帧的主导原因。

## 优化顺序与验收条件

1. 先用可关闭的分段CPU计时定位SetImage/SetU32/NvVFX_Run、CUDA交接各调用的阻塞。与官方样例对照验证：同一pair的输入绑定、未变化倍率不必每张子帧重复设置；现实现每张子帧重设FrameMultiplier，而文档规定该设置重置FrameIndex/Timestep。**尚无A/B证据证明这些重复设置造成缓存失效，不能直接声称删掉就提速。**
2. 若SDK提交仍会长时间占用owner，将耗时提交与按PTS送显解耦，或采用安全的分段协作推进。已有独立呈现GPU队列保留。保持容量上限、跨线程所有权、producer/consumer fence、失败signal、批次租约及seek/reset/退出顺序；不能靠扩大队列、丢源帧或拉长音频时间线伪造达标。
3. 修正完整档位成本过期后的恢复探测：未知成本应显式标未知，保留可解释的历史/单子帧信息并受控重试，避免每轮重新把完整FG算成0。不能简单永远保存一次高成本而使降档永不恢复。
4. 性能显示同时给出请求倍率、实际输出达成率和降档/过期原因；第三球可恢复输出达成率语义。当前负载预算可继续显示，但不能代替达标判断。
5. 用相同原片、EXE/运行时身份与私有配置A/B。首要目标是Medium3/4减少已就绪帧过期、改善提交间隔与实际FPS；High2也需要复核。High完整倍率必须分别通过真实预算，不预先承诺High4/8稳定。

官方资料：[VFG 1.3.0](https://docs.nvidia.com/maxine/vfx/1.3.0/Filters/VideoFrameGeneration.html)、[固定官方样例](https://github.com/NVIDIA-Maxine/VFX-SDK-Samples/blob/52011f89c1741d06b40ea312af1f20be8be9ec62/apps/VideoFrameGenerationEffectApp/VideoFrameGenerationEffectApp.cpp)。每次Run只产生一张中间帧，倍率M需M−1次，VFG只支持batch size1；不把其他VFX滤镜的batch优化套用到VFG。本轮没有下载、替换或修改运行库。

## 复查与产物

源码仅新增本报告、方案及测量/分析脚本，最后追加WORKLOG。命令：`python -B scripts/acceptance/vfg-diagnosis.py prepare`；`case medium2-nr --quality 1 --multiplier 2`；`case medium4-nr --quality 1 --multiplier 4`；`case medium4-nr-tearing --quality 1 --multiplier 4 --tearing`；`matrix`；对照 `case medium4-no-nr --quality 1 --multiplier 4 --no-nr`、`case high2-no-nr --quality 2 --multiplier 2 --no-nr`、`case medium4-nr-quiet --quality 1 --multiplier 4 --no-verbose`；`python -B scripts/acceptance/vfg-diagnosis-report.py`；`audit`、Python AST、`git diff --check`。

日志/JSON：`E:/项目/Veyra/logs/vfg-diagnosis-20261007/<case>/`；汇总 `analysis.json`；私有app/profile：`tests/vfg-diagnosis-20261007/`；子进程TEMP/TMP/CUDA缓存：`tmp/vfg-diagnosis-20261007/`；原始身份与其他工作树脏改动基线：`archives/vfg-diagnosis-20261007/start.json`。保留一份私有app及必要配置、日志供后续优化复测，没有生成额外交付包或删除其他任务文件。

脚本早期失败如实保留：prepare首次编码名`utf8-sig`无效，修为`utf-8-sig`后执行成功，失败时尚未创建基线/app；分析首次假定每种admission日志都有admittedPairs而KeyError，修为按实际字段读取；随后修正同一行解释文本重复字段覆盖真实字段的问题，统一保留首次出现的实际值，重算分析JSON。均未发生产品异常，也未用失败测量当通过证据。21组主矩阵与4组对照的最终核验、原包/其他工作树保全结果在最终收据。

没有merge/push/Release/关机。未测试其他显卡/驱动、全屏、真实采集/串流、全片长稳或物理显示延迟；本轮结论限定上述4K文件播放条件。自查不是独立Reviewer验收。
