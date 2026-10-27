# 补帧受限与180fps显示矛盾排查

用户报告补帧受限但右侧显示180fps。本轮优先排查该反馈，保留未提交的HDR转SDR修复，不发布。

## 确认的事实

原始日志 `logs/veyra-app.log`，2026-09-15T10:56:57Z至10:57:04Z（本地18:56至18:57），session=3 / revision=556，实际DLSS采集链路：源帧处理约60fps，有效生成120fps；同期Present提交141、148、160、163、165、170fps。关闭时累计有效生成2780，生成帧提交2420，生成后过期360，约12.95%。这不是“完全没补帧”，也不能把180当实际屏幕帧率。

UI `AppShell.cpp` 右侧原先读取 outputCompletedFps，即GPU完成原帧与有效生成帧合计，包含稍后过期的帧。DashboardHistory却读取presentSubmitFps（XeSS读取SDK报告提交），连续8次250ms采样低于目标95%显示受限、连续8次恢复后消除。不同口径加上状态滞后，能产生用户所见的矛盾。

`EngineController.cpp` 的实时呈现路径在完成整批生成后，对每个生成帧检查 `timeline.expired(pts, now, 100000)`，即晚于时间线10ms则丢弃。60fps的3X每5.56ms有一个输出机会；稳定的平均产出不保证各张帧及时就绪。日志已证明过期丢弃，但没有逐帧GPU就绪、调度唤醒和各子帧过期时差，无法进一步断言是显卡算力、整批等待还是调度竞争独占责任。

本机此前同时运行过另一轮GPU测试，可能影响其中一段性能；不能未经对照将整个用户问题归因于并行测试。也不能用放宽10ms丢弃阈值、堆积队列或只改状态颜色冒充修复。

## 本轮改动

- 右侧改成“显示提交”，读取与状态判定相同的presentSubmitFps；XeSS继续明确SDK提交，非物理扫描实测。
- 详细面板明确“显示提交（非屏幕实测）”，增加全部来源的原帧/生成帧提交速率；GPU产出明确标注含过期。
- 不修改补帧算法、倍率、过期阈值、音频或真实计数。

## 下一步

180fps口径错误已改。采集3X过期问题仍需同一设置下独立复现，增加限频记录的子帧就绪/截止/过期时差，再决定调整逐子帧就绪调度或实际预算。不能称为已根治补帧受限。

## 本次验证

`cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0，日志 `logs/fg-rate-display-build-20260915.log`。`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915` exit0，结果 `logs/delivery/6bade0b84d8449d89198cffa37453a2d/result.json`。这次未同时运行用户采集，NR/NVOF/4K、播放控制、图片、视频导出与取消回归通过；不代表采集3X已验收。`git diff --check` exit0（仅CRLF提示）。

标准候选 `out/build/audio-continuity-repair-20260915/veyra.exe` 当前SHA256 `02BBC51FF46A8DEBCCA9961BEC48500E2AA77FE62B238174172D4E238ACA6401`，入口 `out/start-user-issues-candidate.cmd`。包含保留的HDR映射改动；未发布、未上传运行组件。

## 后续实卡定位与逐帧修复

用户明确要求继续修复。先只增加限频fg-deadline记录，每批各lease栅栏首次CPU观测时间、子帧截止时间、决定时间和整批就绪状态；每秒至多一条，不做像素回读，也不称为精确GPU执行终点或屏幕时刻。

同一物理采集 `capture:0:0:0 --nr --video-sr 2 --fg-multiplier 3 --smoke-seconds 32`，对应1080p60采集、4K输出、1080p NR、SR质量2、3X DLSS。每次用 `scripts/run-short-test.ps1` 限制55秒，单独执行，无并行GPU任务。

修前日志 `logs/fg-deadline-baseline-capture-20260915.log`，exit0但复现掉帧：有效生成3480、实际生成帧提交3060、过期420；末段170fps。batch442第一张生成帧在截止后8.546ms已经观测到就绪，实际决定却在10.588ms，等待2.043ms后超过既有10ms容差而丢弃。

根因：`EnhanceGraph::resolveGeneration` / 播放CompletionWatch以整批全部fence为屏障。MFG各子帧已有独立lease和readyFence，但实时呈现被最后一张生成帧阻挡，导致先完成的帧跨过自己的截止线。这是可复现的软件调度缺陷，不等于显卡完全没有性能限制。

修复：新增共享 `EnhanceGraph::resolveFrame`，仅在对应lease栅栏完成后读取其4字节SDK有效性状态，Pending转Valid/Disabled只执行一次；实时采集/串流呈现逐张检查、依PTS顺序输出。原完整resolveGeneration复用该函数，文件/导出仍等整批完成并精确统计。保留2批容量、资源lease和consumer fence、原时间线与10ms过期阈值；不延后源锚点、不增加播放缓冲、不复制/混合帧冒充生成。

修后日志 `logs/fg-progressive-capture-20260915.log`，同样32秒exit0：有效生成3478、生成帧提交3478、过期0，稳定段180fps。batch1226第一张生成帧readyObservedLate=7.851ms、decisionLate=7.854ms、batchReady=false，直接证明不再等第二张完成才处理第一张。这里的180是显示提交而非显示器扫描实测；不承诺任意效果、显卡或4X都能满帧。

构建 `logs/fg-deadline-baseline-build-20260915.log` 与 `logs/fg-progressive-build-20260915.log` 均exit0。初次用WindowsPowerShell -File跨进程传数组时--nr被误当脚本参数，未启动应用；改为当前PowerShell用 `& scripts/run-short-test.ps1 -Arguments @(...)` 后正常测试，不将参数错误当产品失败。

`veyra_presentation_worker_tests.exe`、`veyra_live_timing_tests.exe` exit0，日志 `logs/fg-progressive-worker-tests-20260915.log`、`logs/fg-progressive-timing-tests-20260915.log`。最终delivery exit0，结果 `logs/delivery/90feb52b7ed84cf19d92d64596d79f10/result.json`；实际NR/NVOF、4K、GUI播放/暂停seek、图片、H.264/HEVC导出与取消回归通过。采集3X长测已通过，但实机用户验收仍需保留。

120秒实卡日志 `logs/fg-progressive-capture-long-20260915.log`：源帧7062、有效生成14050、生成帧提交14050、生成后过期0，末段显示提交180fps；启动及预算恢复期间另有68个候选未执行，不计作有效生成。NR CreateFeature18返回0x1 Success、NVOF CreateInstanceD3D12状态0。上述两个单元测试是既有调度/时序回归，本轮新增逐帧行为由实卡日志验证，未宣称新增独立单元覆盖。

没有增加缓冲或修改延迟目标，不等于已经证明实测延迟下降：原帧回调至Present返回P95在修前32秒为44.606ms、修后32秒为50.328ms、120秒为50.837ms，包含启动与更多实际呈现工作。本轮通过证据是消除已复现的整批等待过期，不承诺屏幕延迟降低、完美帧间距或所有设备满帧。GPU队列中的后续工作仍可能影响物理输出；软件提交时间不能替代扫描实测。
