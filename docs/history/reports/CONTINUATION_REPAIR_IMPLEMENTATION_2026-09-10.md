# 继续修复实施记录

## 2026-09-11 最新收尾状态（覆盖下方历史待办）

用户决定暂缓AMD NR/性能继续优化/跨屏UI扩展/发布，实卡与音画同步由用户验收。本次已彻底移除FRUC、修正旧预设v4-v7迁移并写v8、修复打包脚本和过期测试，清掉运行目录旧FRUC组件；补齐reset原因及8192条有界事件轨迹，接入现有诊断预览。Release构建、77项合同、30组预设、28项实际RTX引擎回放、DLSS/XeSS正常与失败UI切换，以及23项delivery通过。完整文件/命令/失败/哈希见 [本次收尾记录](FRUC_REMOVAL_AND_DIAGNOSTICS_2026-09-11.md)。最新EXE SHA `E078E0B9348841E8A109E1160E704862F175B43F7C5E07C1ADB1D50F73432804`；未实卡、未发布。下一步为用户实测，不再沿下方历史“下一条AMD/重建计时”自动扩展。

状态：目标模式执行中。范围按 [继续修复方案](../plans/CONTINUATION_REPAIR_PLAN_2026-09-10.md)，未完成全计划。起始代码 `be00e27`，保留上一轮方案文档改动。不启用旧Loop。

## 2026-09-11 续接实证：设置重置/重建生命周期计时

为避免把整段 `graph.process` 或 GPU `Evaluate` 返回误报成“重置完成”，`FrameFlowMetrics` 现在保存最近一次设置事务的 session、请求 revision、实际 epoch/source identity、是否重建、结果（完成/回滚/取消/失败）以及五个独立 CPU 观测区间：排空、销毁、创建、首帧预热提交、首个 GPU-ready 输出。每个区间在边界推进时切换起点；首个有效输出必须通过对应 batch 的真实 fence，旧 revision/epoch 的完成事件不能结束新事务。状态面板显示这些值，仍将 GPU timestamp 与 CPU 观测分开。

设置失败记录为 `RolledBack`，不会显示“已恢复有效输出”；停止发生在重建首帧前记录 `Cancelled`，不伪造 FirstValid。暂停缓存预览也通过同一完成条件。排空失败直接停止并记录 `Failed`。当前记录容量随现有活动流窗口保留，后续仍需把高频 capture drop/scene-cut 等历史边界接入统一 reset cause 和有界逐帧轨迹，不能把这一项冒充所有 reset 已完成。

验证：完整 release 构建 `logs/continuation-repair-20260910/reset-lifecycle-build-final.log` exit 0；`continuation-reset-final-rollback` 约 8.8 秒 32 项 PASS，覆盖真实 live scheduler 的 NR+2X 重建、匹配 GPU-ready、paused cached preview、失败回滚、停止取消及旧计数隔离。合成文件回放，不是实体采集卡；没有宣称扫描输出或物理显卡拔插已验证。测试 EXE SHA `474726E22BBAB4D8629C5232594EF6E05FC904A526D344543E94980A41AC5554`，worker SHA `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。

随后把普通 open/history boundary 也接入相同记录，修正 `historyResets/settingsResets/captureDropResets` 从覆盖赋值为累计。`continuation-reset-cause-live-final` 62.6 秒通过 source-gap 与 EOF 尾批验收：3600/3600 real ready/presented、0 cancelled；它是合成回放，未当作实体采集结论。第一次 20/30 秒窗口失败是素材在本机需要约一分钟读完，已保留失败日志并将测试等待改为90秒，仍受单次290秒 watchdog限制。

## 2026-09-11 续接实证（优先于下面历史进度）

### 最新续接：可控音频重锚淡出

基线 `78675f2` 干净。可控采集设置/PTS突变、大幅A/V重锚和文件播放中seek在Reset前提交240帧（48k、5ms）立体声淡出，优先使用旧PCM，不足时由最后已提交样本收尾；旧队列已经空或之前补静音时不复活陈旧样本。读取实际WASAPI padding确认尾段消耗后重置，再沿用既有淡入。不是只改音量变量，也没有改用户音量或源PTS。

正常pump未增加等待。淡出仅在重锚边界、音频owner上执行，80ms超时/停止取消/端点错误均退出；暂停、不在播放或已失效端点直接Reset。淡出期间clock为未知，不能把人工收尾样本计为有效媒体进度。日志分清提交、端点消耗、超时和取消；端点padding清空不是扬声器发声测量。物理设备突然消失、已发生的欠载断音无法事后淡出。

构建 `scripts/build.ps1 -Root <root> -Preset x64-release`，`logs/continuation-repair-20260910/audio-fade-final-build.log` exit0。短测通过 `scripts/acceptance/scheduler-short-test.ps1 -Name <名称> -Exe out/build/x64-release/<test>.exe -TestArgs <result.args>`：

- `continuation-audio-fade-final-timeline` 55项2.652秒PASS：PCM两声道单调到零、真实WASAPI尾段消耗/重置、暂停不Start、取消立即返回、播放中seek到600ms及前序设备恢复/媒体映射。EXE `354FDD6C4D94F5B95D625819E3839B0E2E2004B78F575A6A31C443EE06485CD6`。
- `continuation-audio-fade-final-player` 7项2.348秒PASS，真实引擎端点恢复/共同时间线/暂停seek，EXE `9B6B24751D2A40521409D03D35BDBFC81F3D0C4E588FDE36250C57923166727B`。素材同前序 `test_av_1080p.mp4`、SHA `7952AD2904C8FED78402BC299EA2A04D1D869663EBCE17BBAC0C297F84FA2A91`。
- `continuation-audio-fade-final-capture` 11.105秒PASS，合成采集PCM+真实WASAPI：自动80/160、手动/关闭/负偏移、断流/重开、500ms突发队列。偏差约10.6至21.3ms；淡出实际边界等待约16至40ms（包括旧端点排队），不是正常每帧增加此延迟。EXE `3924491A3819367B1859D4DF30458C19FC31171C02C20D72D5CD3D071B220AFD`。无物理采集、无外部听觉验证。

最终联合 `scripts/gates/delivery.ps1 -Root <root>` 23项PASS，`logs/delivery/6304c4518f864f34a4bb91392cd16ab1/result.json`；当前应用SHA `A86DB75E321677C2B9EDC0380D3BC4DCC13FAD3DE5BA05D59DA5C1C30C6BB97A`，worker仍 `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。实际RTX NR/NVOF/NVENC等原合同执行，未加重导出检查，未修改SDK/runtime或发布。所有单测少于300秒。

下一条唯一任务：补全原方案reset/重建耗时。当前`FrameFlowMetrics`有阶段计时但没有reset排空/销毁/创建/预热到首有效输出的测量；`DiagnosticHistory`仅64条去重错误，不能当8192逐帧性能轨迹。需先按实际时间边界建立当前事务证据，区分轻量reset和资源重建、请求与实际applied、回滚与恢复，不能把图process整段叫NR reset耗时。AMD provider/权重/预览环境、系统高DPI与实卡仍未完成，整体目标active。

### 最新续接：文件音频端点恢复与共同时间线

基线 `bba7283` 工作树干净，上一轮UI修复/AMD loader定因属于有效进展。本轮完成文件WASAPI端点错误后的自动恢复，不改SR -> NR -> FG、导出检查或实体采集设置。

- 原因：`AudioPipeline::runOnAudioThread`在pump失败直接退出；引擎在音频clock变NaN时切换到墙钟，设备断开后视频继续走。现在文件路径由音频线程创建/释放/重建端点和COM，500ms重试；旧诊断调用可保留外部初始化。端点释放与anchor更新受短生命周期互斥保护，引擎clock读取不会访问已释放的COM；正常pump仍由单音频owner执行。
- 设备失效、clock失败、暂停/恢复失败都进入恢复；保留HRESULT及恢复次数。视频在音频失效期间保持最后有效媒体时间，不用墙钟冒充仍在播音；只有已确认音频尾部耗尽才允许无音频尾段继续。恢复清理旧PCM/重采样，seek到最后有效播放PTS后预填，不能从已经预解码约一秒的队首直接跳过去。用户在断开期间seek会更新恢复目标。seek失败返回NaN并保留恢复状态，不能记录成功。
- 暂停时重建只预填/锚定，不调用IAudioClient::Start，恢复后仍暂停；音量/静音保留。状态面板新增设备恢复状态、次数和最近错误，停止清除恢复中标记。测试注入只在音频owner上释放自己的端点，不操作系统默认设备、不关闭其它软件。
- 最终 `continuation-file-endpoint-final2-timeline` 47项、2.628秒PASS：既有44.1/48k PCM、sample精度seek、真实WASAPI、半速映射、断粮及恢复，加播放中设备丢失/重开、暂停丢失、断开中seek到1234.25ms、暂停重建不播放、恢复后resume、重试期间stop、并发clock读取。恢复前111.979ms，重新锚定127.438ms，差来自故障命令被音频线程接收前已播放部分；未跳到预解码队首。97/98次并发读取有实证。首轮 `continuation-file-endpoint-recovery` 只因测试要求读取次数>100失败（Windows线程sleep调度），修正为实际多次跨恢复读取/最终clock失效检查，失败日志保留；功能门槛未放宽。
- 最终 `continuation-file-endpoint-final2-player` 7项、2.297秒PASS，真实EngineController文件路径：端点断开300ms期间最多两帧边界变化，时间线不前进；恢复后继续50帧、软件A/V偏差<30ms、暂停seek/resume/关闭均通过。测试EXE `3075E01896ED72899C675FE40A89C69BADDA01F97F378B57BBF707E232FB0204`；音频测试EXE `5060691745A1DC6F15891179EA28DC501855AFCD42541C5C1B872829B9A5FAC2`。
- `continuation-file-endpoint-overload` 5项、7.067秒PASS：实际原生4K NR+VSR4+FG4过载仍保持音频并在视频追上后恢复、暂停seek/关闭通过；最大观测lead123.333ms是过载捕获瞬间，不是稳态同步达标值。最终异常边界补丁前执行，EXE `BB6E1BBC49FA77F7D76BB38B7F71F294A7C7A050360E06C444ACB4014243F3FF`。`continuation-file-endpoint-capture` 11.118秒PASS，合成PCM/真实WASAPI自动80/160、手动/关闭/负值钳制/断流/重开，普通偏差13.2至18.3ms、突发预算500ms，非实体采集验收。

实际构建 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，最终 `logs/continuation-repair-20260910/audio-file-recovery-final-build.log` exit0。全部针对性测试经 `scripts/acceptance/scheduler-short-test.ps1 -Name <上述名称> -Exe out/build/x64-release/<test>.exe -TestArgs <result.args>`，stdout/stderr/result在 `logs/scheduler-repair-20260910/`。文件输入 `loop/local/fixed_clips/test_av_1080p.mp4` SHA `7952AD2904C8FED78402BC299EA2A04D1D869663EBCE17BBAC0C297F84FA2A91`；这只是历史目录内素材，不启用旧Loop。

最终 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <root>` 23项42.420秒PASS，`logs/delivery/5c80d299087b4f2482706bf2a24df9c9/result.json`。当前应用SHA `E2EA9CB93CB290E6B13683647835B3E7F54EA21810FC358D69DB803AA9168866`，worker不变 `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。实际RTX NR/NVOF、原生4K正确性、播放器、图片、NVENC H264/HEVC及取消已执行；例如overload日志CreateFeature18 `0x1`、handle非空，DLSSG Create `0x1`。不是AMD或实卡验证。未添加SDK/runtime/日志到Git，未push/发布。

剩余边界：未在真实拔插/系统默认设备切换上测试，当前自动恢复针对已打开端点报告失效；仍可用的旧设备不会因默认设备变化主动切换。声卡突然消失时无法为已丢失设备播放淡出；可控设置/PTS突变前的短淡出仍需补齐。下一条唯一任务：处理可控音频重锚的短淡出与恢复边界，并对原方案逐项核对计时/重建覆盖。AMD完整网络/provider仍未实现，缺Developer Mode预览加载、权重与目标硬件实证；真实系统150%/200%DPI、实卡验收未完成。整体目标active。

### 最新续接：AMD预览运行时失败已定因

UI批次已本地存档 `ee46c5f`。继续AMD诊断：原隔离探针源码/EXE在 `logs/amd-nr-research-20260910/capability.*`，不是continuation日志目录。新增ignored `debug-capability.cpp` 只以 `DEBUG_ONLY_THIS_PROCESS` 启动该探针，读取其调试字符串，最长20秒；不附加其它进程、不装驱动、不改系统设置。编译命令 `cmd.exe /c logs\amd-nr-research-20260910\build-debug-capability.cmd` exit0。

`scripts/acceptance/scheduler-short-test.ps1 -Name continuation-amd-debug-loader -Exe logs/amd-nr-research-20260910/debug-capability.exe` 在0.088秒结束，exit1；EXE SHA `ABB4CACA9D79A4AB5BC507D3984B89CD65DBD0B265907B7B0972F44406E37802`。真实调试输出为 **`Preview releases of D3D12Core require Developer Mode.`**，随后 `D3D12GetInterface failed retrieving CLSID_D3D12CoreModule`。这解释之前0x887E0003发生在factory创建阶段；不是设备SM6.10/linalg能力查询结果，更不是网络测试通过。

本机没有开启Windows Developer Mode。本轮已读取固定上游README：权重不在仓库，完整网络需SM6.10/FP8，既有实证仅RX9070XT/1920x1080。没有替换主程序Agility、提取权重或加载ReShade；当前AMD provider仍未实现。开启开发者模式只能解除这一加载条件，不能证明硬件、PSO、权重和网络闭环全部可用。

下一条唯一独立任务转到文件音频设备恢复：`AudioPipeline::runOnAudioThread`遇到`pumpOnce`失败直接break，暂停/恢复失败也仅log；需要共同PTS恢复与明确状态，不能直接在音频线程shutdown/restart共享renderer，因为引擎线程同时读取它的clock和状态。先梳理renderer所有权与文件媒体时钟，再做窄修和自有端点故障测试。AMD依赖、真实系统高DPI和实卡保持未完成，整体目标active。

### 最新续接：最小窗口、缩放与选择器回归

基线 `db98af7`，工作树干净。本轮只修改 UI 布局、DPI 与针对性测试，没有改变 SR -> NR -> FG、GPU 调度、音频或导出检查。

- 最小 540 逻辑像素高度时，原状态区只有 116 高，固定标题/主值/页脚后没有可显示的明细。现在预留至少 166 高，仍给设置正文 90 高；状态明细以完整 25 像素行滚动，避免半行裁切后永远不可见。删除已经失效的“Present调用与锁等待”名称，当前计量为 Present 调用。
- 首次实际最小窗口测试 `ui-layout-1789065061472173300` 失败：设置正文 90 高，滚轮也跳 90，FG 选择器从下缘被裁直接跳到上缘被裁。步长改为 36，用户可完整滚出选择器；控件滚轮仍不修改参数。
- 真实 `WM_DPICHANGED` 不再把普通字体从初始 14 改成 15；使用统一 `layoutDpi` 转换字体和逻辑坐标，并取消旧模式动画与弹窗。窄窗标题也限制在增强按钮前，避免绘制矩形重叠。
- 仅 `--smoke-seconds` 测试进程接受 `WM_APP+90` 的 0/96/144/192 缩放注入，普通启动不接受。所有相关窗口与自绘 popup 使用相同缩放；测试没有假造 `WM_DPICHANGED`，没有改变 Windows 缩放。真实当前显示器 DPI=96；150%/200% 是应用缩放/字体测试，**不等于真实跨显示器 DPI 验收**。
- 新 `python scripts/acceptance/ui-layout-dpi.py` 有 120 秒外部 watchdog，只控制自己启动的 PID。最终 `logs/continuation-repair-20260910/ui-layout-1789065295999940200/result.json` PASS：三档字体 14/21/28，720x540 最小布局、状态首尾截图、选择器可达/三项/Esc关闭、滚轮不改滑条、专业展开至少两个中间矩形、连续 resize、最大化、原生全屏覆盖与自动隐藏/退出恢复、空视频中心纯黑。最终截图限制在屏幕范围，旧候选超出屏幕底部的捕获白边不是 UI 白底通过证据。
- `continuation-ui-dpi-paint` 21项、0.140秒 PASS，自绘控件模型变更不触发原生立即绘制，WM_PRINTCLIENT无整块原生白底；`continuation-ui-dpi-popup` 14种交互、1.021秒 PASS，键盘/滚动/取消/焦点/异步刷新/销毁；`continuation-ui-dpi-contract` 0.070秒 PASS。均使用 `scripts/acceptance/scheduler-short-test.ps1 -Name <上述名称> -Exe out/build/x64-release/<对应测试>.exe`，结果在 `logs/scheduler-repair-20260910/`。
- 实际 RTX 后端 UI 回归 `python scripts/acceptance/ui-fg-backends.py`，`ui-fg-1789065217190095800` PASS，FRUC -> XeSS -> DLSS -> 关闭均实际 applied。app.log 的 DLSSG Create 为 `0x1`、handle非空，warm-up Evaluate `0x1`，后续20次Evaluate/19有效生成与呈现。它不是 NR/AMD 或实体采集测试，也不是扫描时刻测量。

构建命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，最终日志 `logs/continuation-repair-20260910/ui-dpi-build2.log` exit0。应用 SHA256 `27910DEDB2855334C45A0B92F4694542AB8631FB9AA17FED819E65585CC35C55`，FRUC worker仍 `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。本轮没有重跑与 UI 改动无关的联合 delivery，下面的23项记录仅代表当时版本。源码/runtime隔离不变，本地存档，不push/发布。

当前全计划仍未完成：R0的FG选择与R1状态布局已具应用侧证据，但AMD独立NR/provider仍缺；R2单GPU所有者和FRUC正确性已有前序证据，原生4K重载不代表4X实时；R3合成音频/漂移已通过，文件设备自动恢复与突变淡出还有缺口；R4真实150%/200%系统DPI、实卡验收尚未覆盖。下一条唯一任务：继续 AMD NR 的预览运行时/完整网络依赖诊断与 provider 接入，硬件不足不得用可选按钮或光流替代功能。

### 最新续接：单GPU所有者调度、断流与末帧

基线`e25585e`工作树干净；上一目标轮是有效进展（音频与逐帧GPU计时两个已验证本地提交）。本轮按原计划推进S3，没有开启旧Loop或发布。

- `LiveGpuScheduler`取代旧`PresentationWorker`线程。增强提交、完成检查、deadline和Present在现有引擎线程推进；任务返回Pending/Complete/Failed，未来播放时间只作为截止时间返回，不在任务内等待。最多两批包含当前租约。跨线程调用拒绝、失败/取消逐项finalize、先释放旧租约再接下一批。删除旧worker实现与旧线程测试，保留测试target名称以兼容现有命令。
- 移除EngineController的`gpuMutex/liveStatsMutex`。UI仍只提交设置/控制请求，采集回调仍只更新容量1 mailbox；`CaptureCardSource::tryRead`立即检查输入，原`read`保留30ms等待供其它调用者。主循环遇到Waiting或两批占满时继续推进完成、呈现和GPU时间戳，使用局部高精度timer等待，未加入正常像素回读/逐pass CPU fence等待。
- GPU-ready时长改用完成观测时刻，不能把前一批的呈现排队时间计入GPU-ready。EOF先完成剩余呈现再报Ended；失败不能被Ended覆盖，批处理中发现暂停/停止就取消剩余工作。状态统计仍区分完成与Present，不声称scanout。
- `continuation-single-owner-state`15项0.074秒PASS：同一owner、Pending立即返回、提交可推进、两批上限、lease释放、取消一次、错误传播和外部线程拒绝。最终`continuation-single-owner-cancel-live`30项8.723秒PASS，30/60、NR/2X/4X、设置/音频隔离、暂停/恢复/停止；source30fps对应Color30样本。最终live EXE SHA `6B0D1EDF5D3FF1750E1E06088D3A372149CE7C98B22489E01E49D7ADE4AA1BE1`。
- 输入空档与EOF：在仅测试回放模式下注入400ms Waiting，未读取新输入时12帧全部完成/呈现；恢复后处理完62帧。最终`continuation-single-owner-cancel-eof`6项3.325秒PASS，所有epoch累计真实提交/呈现均62。初版`continuation-single-owner-gap`用长素材等20秒没有到EOF，不能当产品EOF失败；`continuation-single-owner-short-eof`错误用当前epoch计数与整段总数比较（末尾epoch1帧、前epoch61帧），修正为累计closed窗口，失败日志保留。
- 短素材由`ffmpeg -hide_banner -loglevel error -i loop/local/fixed_clips/test_av_1080p.mp4 -t 1 -c copy logs/continuation-repair-20260910/owner-eof-1s.mp4`生成，SHA `9CF8AEE99AB10E1B476616A9677A329687E4E7D4AE235A18BF119AB5544EBE25`。流复制尾部含62帧，测试按实际输出，不假设恰60。原素材SHA `7952AD2904C8FED78402BC299EA2A04D1D869663EBCE17BBAC0C297F84FA2A91`。
- FRUC回放`continuation-single-owner-fruc`24项6.644秒PASS，known-pan15 SHA `EB05452E25325D9882DDD9E975051A010459FDD2432305F61E3820D85689D05B`；worker固定`EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。UI `python scripts/acceptance/ui-fg-backends.py`正常与`--reject-xess`分别`ui-fg-1789064098272439200`/`ui-fg-1789064215917318600`通过，FRUC->XeSS->DLSS->关闭及XeSS失败保留FRUC。它们早于最终退出边界补丁，非全DPI验收。
- **性能对照无显著提升**：在`out/owner-baseline`单独checkout`e25585e`并构建，源码干净；仅在ignored输出目录补同版FFmpeg5个DLL及与当前相同FRUC worker。`continuation-owner-ab-old-runtime`7.708秒与`continuation-owner-ab-new`7.646秒，同素材、原生4K NR+VSR4+FRUC4+admission，均150源帧、33源fps、148真实呈现、0有效生成呈现、skip450/evaluate0，帧年龄P95 59.659/59.645ms。旧EXE `AAF6EF3DA6D331B40AB3B44DEDA589BA8B9F301D6BB53099A487B21C6C041510`，新对照EXE `2F425BE7F77B704397A58526B0C797E8C70F3E5DED4F0769AA0556297B9E0F97`。本轮解决所有权/等待推进，不声称NR内核提速或4X实时。基线第一次启动缺FFmpeg返回0xC0000135，记录`continuation-owner-ab-old`，未把启动失败拿来比较性能。
- 干净构建发现旧CMake先读FFmpeg变量、后find_package导致scene_analyzer首次缺头文件；改用`veyra_media`传递依赖。无实验NR配置还缺自有parity shader target，现将两个通用着色器target移到功能开关外。基线首构建失败`build-owner-baseline.log`，第二次配置成功`build-owner-baseline-configured.log`。最终全新VS生成目录`out/scene-no-nr-first-config`首次configure与scene_analyzer Release构建通过，日志`configure-scene-no-nr-final.log`/`build-scene-no-nr-final.log`；也验证带实验runtime的全新目录。先前无NR失败保留在`configure-scene-fresh.log`，不能说那次configure成功。

命令统一`scripts/build.ps1 -Root <root> -Preset x64-release`，最新`build-single-owner-cancel.log`exit0；短测`scripts/acceptance/scheduler-short-test.ps1 -Name <名称> -Exe <test> -TestArgs <result.args>`，result/stdout/stderr在`logs/scheduler-repair-20260910/`。最终`scripts/gates/delivery.ps1 -Root <root>`23项42.380秒PASS，`logs/delivery/3462b28549ed4982a99799adc5a3071e/result.json`，EXE SHA `1B0DF186574B552E349EBB9502AF89A001430B89407B3D15635862E668279CE6`。前置退出边界补丁前gate `7ce879edc9094d70b9febd31cd2cba1b`23项42.279秒也通过，不能与最终EXE混用。实际RTX NR/NVOF和NVENC已运行；NR CreateFeature18返回0x1、handle非空，例证`owner-fruc/engine.log`。没有改变导出检查、SDK/runtime没有入Git。

以上单次<300秒，未打开实体采集卡、未执行AMD NR。单所有者主循环和对应短测已落地；下一任务为专业页全DPI/最小窗口/展开滚动验收与修复，再推进AMD NR依赖/provider。GPU driver/NGX/FRUC调用仍可能阻塞当前线程，此状态机不是可抢占GPU内核，也不是所有成本消除。整体目标仍active。

### 最新续接：逐帧GPU时间戳交付

音频修复已本地存档`1821120`，随后修复阶段时间戳漏样，不改NR/SR/FG处理顺序。

- 根因一：`GpuTimer::collect`一次收回多个完成查询，但只保留`last_`，上层仅上报最新结果；现增加按需启用的64条完成队列，graph与presenter逐条交给`FrameFlowWindow::gpuFrame`，按epoch/revision过滤，不能重复消费latest。旧harness只读取latest时不启用队列。队列超限丢最旧并记录overflow，不增加GPU等待或像素回读。
- 根因二：查询槽按source id取模，多张同源补帧争抢一个槽，其余空闲槽未被使用；现从建议槽开始寻找四个槽中的空位，全部未完成才跳过并记缺失数。日志按1/2/4等次数输出，避免持续过载刷屏。
- 新`veyra_gpu_timing_tests`使用实际RTX5070 D3D12 queue/fence，测试自有hold fence阻止五次同源提交提前完成：四份真实时间戳全部收回，第五份明确skipped1，未完成不返回、take只消费一次。70份不消费测试保留最后64份、overflow6，close清状态。`continuation-gpu-timing-queue`6项PASS0.519秒，测试EXE `959AA7B3FEA1426D86EF01E968ED9E4E0747B40E0B12032C17CFD0E948975301`；等待/人工hold只在此隔离测试。
- `continuation-gpu-timing-contract`71项PASS0.077秒，验证多帧同timestamp可计数、旧revision/epoch拒绝。`continuation-gpu-timing-final-live`30项PASS8.812秒，真实controller回放60->30->60、NR/2X/4X、暂停和设置；观测源30fps、最近一秒Color样本30，engine日志无query skipped或completion overflow。输入`loop/local/fixed_clips/test_av_1080p.mp4` SHA `7952AD2904C8FED78402BC299EA2A04D1D869663EBCE17BBAC0C297F84FA2A91`，live测试EXE `87AD62DE0F88D4E6BE6389F23B52021FB40D1DBA57D4CC7B3BC91A39BECBC3D8`，worker未变。
- 构建`build-gpu-timing-queue.log`/`build-gpu-timing-final.log`exit0，使用`scripts/build.ps1 -Root <root> -Preset x64-release`。测试仍经`scripts/acceptance/scheduler-short-test.ps1`，结果在`logs/scheduler-repair-20260910/<名称>.result.json`，最终live args为`@('loop/local/fixed_clips/test_av_1080p.mp4','logs/continuation-repair-20260910/timing-final-live','--half-rate')`。
- 最终`delivery.ps1 -Root <root>`23项PASS42.146秒，`logs/delivery/88753f608465497dba0c2660bf7755dd/result.json`；应用SHA `F6B5F0E96B81B223FE856E888221CFF51ABBE706C9898A6609A49B7EF90413A2`。实际RTX NR/NVOF/NVENC已执行，gate不证明实卡或原生4K实时60。`git diff --check`通过，新增测试源码不含SDK/运行时。

本项修复收回后丢样与同源查询冲突，不等于全GPU所有者调度完成；样本仍在主循环收集时进入统计窗口，回调/提交阻塞带来的观测滞后、EOF最后未采集样本和完整S3待处理。无实卡/AMD NR验收，无push/发布。下一条唯一任务：把完成观测、deadline与present推进收敛到单GPU所有者状态机，保持最多两批与历史/租约规则，不能靠移除现有反压制造吞吐假象。

### 最新续接：采集时钟漂移与真实PCM播放位置

基线本地提交`ed5aed3`。本轮仅合成PCM/PTS与静音WASAPI测试，没有打开实体采集卡、修改系统默认音频设备或发布。

- 增加有界`AudioFrameTimeline`，分别记录重采样输出样本对应的源媒体起止PTS，以及写入WASAPI的位置。播放时钟从实际IAudioClock位置查找媒体时间，不能继续把每个48k输出样本当作固定长度源媒体样本。未写入区域、设备断粮后及reset后没有虚构的有效媒体时钟；恢复时越过设备实际消耗的静音区间。
- 自动模式每250ms低通观测音画误差，`swr_set_compensation`最多正负5000ppm、每次变化最多250ppm；突变大于60ms保留重锚。模式/端点重置清除补偿。PCM持续转换保留swr前后delay并建立分段媒体映射，软件总队列预算仍500ms。
- 候选回归确实失败：`continuation-audio-drift-prefill-normal`关闭/零补偿误差约53.8ms。事件等待在PCM转换后，等待期间到达的PCM不能参与此次填充；采集路径又把所有剩余设备空位补成静音，插入额外延迟。修正为先等事件再收集PCM、避免启动后立即重复填充、采集仅提交已有真实PCM，文件路径保留原欠载策略。初始只改预填和等待并未解决，相关失败日志完整保留。
- 测试端普通`std::this_thread::sleep_until`的10ms回调受Windows定时粒度影响；改为独立高精度waitable timer，不改系统定时器或验收阈值。设备时钟QPC采样年龄诊断约0.001ms以内，排除读取旧时钟假设，临时逐次诊断已移除。
- `continuation-audio-drift-final-normal`11.112秒PASS：自动80/160、手动100、关闭、负值钳制、350ms断流与重开；最终关闭/零补偿误差23.36/23.41ms，自动/手动误差18.10至22.67ms。突发high-water500ms、overflow11，普通模式0overflow。关闭同步不代表消除声卡/系统固有延迟。
- `continuation-audio-drift-final-loss`3.573秒PASS：释放测试自有端点后恢复，retries2、平均软件偏差12.22ms、0overflow。`continuation-audio-drift-final-timeline`1.082秒PASS：44.1/48k文件各36000输出样本、seek/pause、真实WASAPI半速媒体映射、断粮clock失效与恢复新PCM。`continuation-audio-drift-final-contract`70项PASS、0.069秒。
- 最终同一EXE `continuation-audio-drift-final-fast`119.757秒、`continuation-audio-drift-final-slow`119.998秒均PASS：源时钟正负1000ppm，P95偏差分别4.242/4.302ms，两项missing0、仅首次reset1、overflow0、high-water89.65ms。中间候选`continuation-audio-gap-slow`120.002秒P95为4.297ms（EXE `3D334D7A9BC81726676B25BA8ED58A46608018B2886259238C9A4ECF741C5DE2`），仅保留追溯，以最终同EXE结果为准。
- 最新构建`build-audio-drift-final.log`exit0。采集测试EXE `7D9FEF157E2EE871544A6B312CDC4CBBD9EF9A340322B4B6B44FC7C7DF8DA1AB`，worker仍`EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。所有短测由`scripts/acceptance/scheduler-short-test.ps1 -Name <以上名称> -Exe out/build/x64-release/<test>.exe -TestArgs <参数>`执行；普通无参数，设备恢复`--endpoint-loss`，漂移`--drift-slow`/`--drift-fast`。完整result/stdout/stderr在`logs/scheduler-repair-20260910/`，构建在`logs/continuation-repair-20260910/`。

最终联合命令`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <root>`23项PASS、42.484秒，`logs/delivery/c41ad55982e6448190d1bc4ed239a415/result.json`；应用EXE SHA `FDA98B226562C55B44A8139E1A4D7F3F35A7458109AC8ED4F72FD6ACE755D6BE`。实际执行RTX NR/NVOF、原生4K正确性、播放器、图片、NVENC H264/HEVC及取消；未改导出检查。NR runtime固定SHA再次核对一致、签名Valid。`git diff --check`通过，SDK/runtime/日志没有加入源码。

本轮音频软件回归通过，整体目标仍active。下一条任务为逐帧GPU时间戳交付：已确认`GpuTimer::collect`可能一次收回多帧而上层`last()`只消费最新一帧，需逐帧交付完成样本并保持revision/epoch隔离；随后继续单GPU所有者调度。AMD NR依赖/provider、UI完整DPI、实体采集验收仍未完成。旧Loop不启用、无push/发布。音频只证明本机合成输入的软件估算同步，不是实卡听觉/屏幕扫描验收；手动/关闭模式未启用漂移补偿、突变短淡出和文件设备自动恢复仍有限制。

### 最新续接：采集音频总预算与输出设备恢复

基线`8b36b4c`，工作树原先干净。本轮仍未打开实体采集设备，不修改默认系统音频设备、不发布。

- 原始PCM队列和转换后PCM原先分别允许500ms，总量可能接近1秒。现在`CaptureAudioSession`以统一500ms预算覆盖原始、正在转换与转换后队列；回调入队/拉取/转换提交都更新水位，超过预算丢弃过期输入并让音频线程重锚。正在转换期间出现新discontinuity/overflow时不提交旧转换块。软件水位有明确high-water计数，WASAPI设备padding单列，不重复当额外补偿相加。
- WASAPI初始化、buffer、Start、事件等待/重置等失败记录实际HRESULT；事件超时也检查padding，让失效端点有机会被发现。采集音频失败后释放自己的端点与旧PCM，每500ms尝试重新打开默认输出，期间保留视频会话、界面显示重连状态，恢复后重新等视频锚点。新增AudioRenderer RAII析构和启动/reset时5ms增益淡入。尚未实现旧PCM短淡出、物理设备热插拔验收或文件播放设备自动恢复。
- 实际测试：`continuation-audio-recovery-final-normal`11.12秒PASS，自动80/160ms、手动100ms、关闭、负补偿钳制、350ms断流、停止/重启和600个10ms PCM块突发输入。突发high-water恰500ms、overflow11；普通阶段0overflow，平均软件偏差/预期偏差误差2.65至12.54ms。
- `continuation-audio-recovery-final-loss`3.59秒PASS：仅通过test环境开关释放测试会话自有WASAPI端点，出现重连状态、初始化次数2、恢复后平均软件偏差13.12ms、队列78ms、0overflow。前序同类测试5.02ms是另一次样本，不是最终结果。该测试不冒充物理声卡失效或所有HRESULT路径验证。
- `continuation-audio-renderer-timeline`0.523秒PASS：文件PCM44.1/48k完整750ms/36000输出样本、采样级seek、首缓冲/暂停/无效时钟仍通过。
- **漂移回归失败，未修复**：新增`--drift-slow`持续120秒，输入源时钟相对host慢1000ppm。`continuation-audio-drift-baseline`120.007秒exit1，P95软件音画偏差31.7295ms（要求<=30），24次未运行/无有效clock采样，resets4（首帧1+漂移触发3），软件最高水位103ms。现有策略仍约35/72/111秒间歇重锚，不能称长期平滑同步完成。没有为了通过而放宽断言。后续需要重采样补偿与重采样样本的媒体时间映射一起实现，不能只改swr样本数而保留1:1 WASAPI时间公式。
- 构建命令`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，`build-audio-budget.log`、`build-audio-endpoint.log`、`build-audio-recovery-final.log`、`build-audio-drift-test.log`均exit0。测试统一`scripts/acceptance/scheduler-short-test.ps1 -Name <名称> -Exe out/build/x64-release/veyra_capture_audio_tests.exe -TestArgs @('--endpoint-loss'或'--drift-slow')`，普通无args。结果/stdout位于`logs/scheduler-repair-20260910/`。最终采集测试EXE SHA `24C7B03ACA8C159079E73A3B444A1E18E6F5115E5056963642EDA59701459958`。
- 最终联合`scripts/gates/delivery.ps1 -Root <root>`23项PASS42.43秒，`logs/delivery/0325b097b61a42b0becf6dc71a900175/result.json`；应用SHA `1136543C02604020F877BAA8BE0F67442671DFC7805FBB28310CD57B2D3E8FC6`，worker保持`EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。实际RTX NR/NVOF与NVENC按gate执行；此gate不覆盖漂移失败，不能覆盖为整体通过。

下一条唯一任务：实现有界采集时钟漂移校正及正确的重采样后播放PTS，再跑120秒正负偏差。完整GPU所有者/计时覆盖、AMD NR依赖/provider、UI全DPI与实卡验收仍未完成。所有测试单次<300秒；运行时/SDK未加入Git，未push。

### 最新续接：设置隔离、生成帧关系和FRUC admission

前序源码/文档本地存档 `6316376`，无push/发布。本段新增内容优先于下方同日旧快照，目标仍active。

- 设置：`EnhancementSettings::sameVideoConfiguration`将revision仅用于GPU配置隔离；纯音频请求保留revision，主循环与大图路径直接更新音频/配置状态，不排空视频、不重新处理缓存画面。重复通知无操作。视频事务排空后重读最新desired，减少过时重建；失败回滚保留随后独立提交的音频修改。设置页按完整desired比较，避免同revision音频变动不刷新。没有宣称完整单GPU所有者或所有设置均无等待。
- 生成帧：`FrameLineageTracker`记录相邻已接受源帧identity/PTS/真实到达时刻，仅同epoch/revision且A的PTS匹配时建立关系；reset、缺少对应A、缓存预览不给推算值。`FrameFlowWindow`与状态区加入A/B到达间隔、生成帧Present返回距A和B的时间，分别有一秒均值/P95/样本数；不混入真实帧总延迟。文件模式标软件取帧，实卡才使用callback锚点；不是光子延迟或免费lookahead。verbose日志`frame-lineage`保留A/B源id与端点。
- FRUC admission首次新回归失败 `continuation-fruc-admission-pixels4`：跳过i=1后，i=2复用同source parity，CUDA数组覆盖SDK仍引用的前帧，之后亚帧GT错位（4X前两张误差约2.5/2.0，重复基准约0.85/1.36）。`FrucWorker`改为按成功SDK调用轮转独立`inputSlot`，源共享纹理仍按source parity读取。候选`continuation-fruc-call-parity2/3/4`均通过六epoch、每epoch两次skip/reseed/像素恢复，3.06/3.93/4.54秒；不销毁worker、不增加CPU fence等待或像素回读。
- 实时采集FRUC重新启用与DLSS相同的整对提交前admission；XeSS仍由SDK呈现内部控制。正常`continuation-fruc-admission-live`24项PASS6.52秒，known-pan15素材SHA `EB05452E25325D9882DDD9E975051A010459FDD2432305F61E3820D85689D05B`，含有效补帧/生成帧关系/音频设置/暂停/2X4X/关闭。
- 同源过载A/B：`continuation-fruc-admission-baseline`16.74秒与`continuation-fruc-admission-overload`7.70秒，均150源帧观察点、原生4K NR+VSR4+FRUC4，测试EXE `8FB7B4566A4E7C86336375952A12DE904E31C396FA0C5D07A2E097C5F5C5797B`，worker `EC8150FF88C7C84FACDF92B920DED2D6CEC5F682990C375A92550AFD5E818695`。基线Evaluate450/skip0/源处理12fps；筛选Evaluate0/skip450/源处理33fps。有效生成呈现两边0，基线expired也0，不能说450张有效生成帧都过期；只证明这组无收益计算被省掉。软件源帧年龄P95 90.68→60.13ms，非实卡、无GPU占用百分比证明、非FRUC内核加速。此前原生4K FRUC本体耗时边界仍存在。
- 验证：`continuation-lineage-contract`66项PASS0.082秒；`continuation-lineage-half`29项PASS8.84秒；`continuation-lineage-fruc`24项PASS6.57秒；设置隔离前置CPU58/half28/FRUC23也通过，后续用上述新版本覆盖。UI正常 `ui-fg-1789059745509958500` 与故障回滚 `ui-fg-1789059468450574400`均PASS，自有窗口状态截图已查看，没有完整DPI/动画验收。所有名称对应`logs/scheduler-repair-20260910/`下result/stdout/stderr，controller日志路径见各result.args。
- 实际构建命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，最新`logs/continuation-repair-20260910/build-fruc-call-parity.log` exit0。短测统一`scripts/acceptance/scheduler-short-test.ps1 -Name <以上名称> -Exe out/build/x64-release/<test>.exe -TestArgs <result.args>`；FRUC skip为`@('2|3|4','logs/video-sdk-trial-20260909/pan.nv12','--skip-pixels')`；overload为live test的`--fruc-overload[-baseline]`。
- 最终联合命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <root>`，`logs/delivery/f024e12327b5429593e762943abb2eca/result.json` 23项PASS；EXE `D3E8736F9D0ADD088B0CEA5FE8C979B99820745467CA7743654A93687EF24823`。日志`delivery-fruc-admission.log`。此前lineage版本delivery `2e071ef905724217bf652d135552607a`23项42.49秒PASS为前序版本。实际RTX runtime已执行，NR固定SHA/签名Valid未变；没有打开实体采集卡。
- AMD隔离探针新证据：显式绝对路径LoadLibraryEx成功、导出coreVersion721，ID3D12SDKConfiguration1获取成功，但CreateDeviceFactory721仍`0x887E0003`；`continuation-amd-factory-capability` exit1，不是能力失败结论，更不是成功执行网络。初次链接缺CLSID定义失败，包含initguid后构建成功。微软官方getting-started列举该码可由SDK版本/SDKLayers不匹配触发；尚未拿到debug输出定根因。Windows未改开发者模式，未换主程序Agility。缺权重/RDNA4/preview编译链和provider问题仍在。

下一条任务：音频缓慢漂移、全局PCM容量与端点恢复；然后继续完整GPU所有者调度/逐帧GPU计时覆盖及AMD依赖。现有GPU图和呈现仍双线程互斥；没有以这批通过声称全目标完成。所有测试单次<300秒，源码/runtime隔离，未push/发布。

整合检查补充：`build-fruc-ui-integrated.log`构建通过；`continuation-fruc-batch-final2/3`最新CUDA候选2X/3X通过（2.84/3.33秒），`continuation-fruc-batch-live`真实controller19项通过6.20秒，正常UI `ui-fg-1789058417812768300`通过。最新联合delivery `ad0a4e4680d748d0b97de6fe0e665c53` 23项PASS、42.35秒，EXE `24216560E25D6C84D0BF3656E3AC70BFCD8917F2BED52AE82AB9B4BF2B168778`，worker `DE38EFBE302FB3664EEC167A7FC3105154B877A4BBDFC8980DCC7C168B0C1678`。UI长错误行换行已编译，完整DPI尚未验收。

AMD新增依赖证据：官方NuGet `Microsoft.Direct3D.D3D12/1.721.3-preview`，包SHA `0131BCE1E4BACE3FC08C03018C29A09EDE2570B263721C6449B0EC75762AB22D`；D3D12Core签名Valid/Microsoft，LICENSE.txt和LICENSE-CODE.txt原样保留。仅置`third_party_local/microsoft`与ignored隔离探针，未进主程序/Release/Git。`continuation-amd-linalg-capability`实际返回0x887E0003（SDK加载失败），没有成功创建设备或查询linalg；探针进程exit0只是完成错误记录，**不是能力PASS**。未改系统开发者模式。另核实上游`NativeActualNetwork70::RecordUnsubmitted`是曾触发device hung的诊断路径，接入必须保留分段提交；当前计划中单列表假设不可直接落地。

- R1：GPU完成检查现在在呈现deadline等待及生产者两批反压期间非阻塞执行。观察者与呈现任务共享同一个FrameOutputs，不重复解析SDK状态；去掉旧processingRate与会话平均FPS赋值。Drop仍只重置历史账本、保留同revision的完成速率。尚非完整单GPU所有者状态机。
- 阶段统计最近一秒均值/P95/样本数，GPU timestamp去重，250ms刷新，有界8192条与溢出计数；P95改为nearest-rank。Submit使用processDone-processStart，不包含后续统计开销。53项合同通过 `continuation-stage-p95-watch`；worker反压期间完成观察测试通过 `continuation-worker-progress`。
- 实际RTX共享controller回放 `continuation-watch-half` 23项通过，8.51秒，含30/60处理、NR/2X/4X、暂停/设置/退出；测试EXE `EBD6CF1E4C101B36D0DFA042B4994A5EC580EE29F26346A58A7613F632524C04`。这是文件模拟采集，未打开实体采集卡。
- 文件过载保持音频主时钟/追帧恢复已通过 `continuation-file-overload-fixed`（5项、7.06秒，最大观察领先113.33ms）。修复暂停seek卡在目标前关键帧，以及seekPreviewPending在EOS不清理的问题。文件音频750ms完整PCM/采样级seek/真实WASAPI测试通过，详见下文；不承诺GPU不足时仍无停顿。
- 采集音频增加初始化/运行错误状态；不支持PCM时保留视频并显示原因。空Chunk.discontinuity原未初始化，已修正；停止/重启清空旧队列、时钟和视频锚点，持续100ms以上无输入且欠载时重锚。首版任何欠载即重置导致手动/关闭模式频繁重置，失败 `continuation-capture-audio-recovery` 保留，已修正。
- 音频恢复回归 `continuation-capture-audio-recovery2` 通过，11.05秒：自动80/160ms、手动100ms、关闭、负偏移钳制、350ms断流、停止/重启；平均软件偏差/预期偏差误差约2.7至12.8ms，未溢出。测试EXE `6FA43505594A63CED540E259DE5D65162A8A8877DC70330D718F4E4CAFA0C11B`。合成PTS+真实静音WASAPI，不是实卡/物理扫描延迟验收；微量漂移重采样和平滑仍待做。
- 原生PCM pin合同 `continuation-native-pcm` 已通过；预设schema7音频设置roundtrip `continuation-presets-audio7` 已通过。
- **FRUC重置像素错误已有通过候选并进入当前源码**：原DirectX11Resource交给FRUC内部互操作的路径撤换为应用拥有的CUDA arrays。D3D12共享纹理经D3D11自有纹理桥接，CUDA graphics map/unmap明确所有权转换，GPU fence向父进程传递完成。直接注册D3D12共享纹理失败CUDA400，记录 `continuation-fruc-cuda-map`；使用D3D11桥后正确。未加入cuCtxSynchronize、CPU像素回读或显式逐帧CPU fence等待；CUDA SDK map/unmap本身可能阻塞，不能称完全异步。
- 新FRUC常规reset只通知下个真实帧bSkipWarp更新历史，不销毁SDK/上下文/资源、不重启worker、不为reset调用ring.drainQueue；失败才回退重启自己拥有的worker。2X/3X/4X六次重置、正向/反向PTS、颜色反转与实际亚帧GT均通过 `continuation-fruc-default2/3/4`，2.70/3.14/3.64秒。初版正式候选worker SHA `5533EBA3ED6E6E925CB53412761375A66C635B9AF17158CA309CC0DA9FA39906`。
- 之后同一CUDA context/整批map-unmap候选4X像素通过 `continuation-fruc-batch-map4`，worker SHA `ECD33BDB87C4C3F38D9992DDBD63A494074E38795F9E703C0EE1B3384E95D6A1`，编译日志 `build-fruc-batch-map.log`。最新2X/3X和联合回归待补；源码未存档提交。
- FRUC实际controller已知平移回放 `continuation-fruc-cuda-known-live` 19项通过、7.11秒。另一个pan15素材的NR后FRUC报告repeated、没有有效补帧，`continuation-fruc-cuda-live`失败保留，不能篡改成有效输出；正确known-pan15素材具有独立SHA，不混淆两份素材。
- **FRUC性能仍有限**：新增同一图120帧、最多两批、每30帧reset、无像素回读的吞吐用例。初版CUDA4X原生4K实际完成120真实+348生成，FG区间中位70.58ms、图提交CPU中位73.49ms、合成图填充+整图源吞吐11.18fps，`continuation-fruc-native4k-throughput`；1080p最终批量map版本FG中位26.05ms、CPU26.61ms、源吞吐33.88fps，`continuation-fruc-batch-throughput`。计时包含互操作/排队，不是FRUC纯网络内核时长。不能把正确性通过说成原生4K实时性能已修好，也没有精确HEAD旧worker同源A/B性能结论。
- 本轮此前联合delivery `aba35e073bfb41edb6507d5f2caae220` 23项PASS、46.80秒（audio之后、最新完成观察/FRUC之前），只证明当时版本。最新联合gate待运行，不能沿用旧结果。

下一步：FRUC候选生命周期/2X3X/集成回归、状态区长文案与DPI、单所有者调度，以及AMD NR依赖/provider。本机仍缺AMD网络权重、SM6.10预览编译环境与目标RDNA4硬件，未实现AMD NR；不可用说明或光流不算实现。未push/发布。所有测试单次少于300秒。

## 已推进

- R0部分：补帧页标题、后端、倍率、光流和内容节奏按明确行位置排列，移除后追加到标题位置的ID208。NR独立实现切换仍待接入。
- XeSS初始化失败的设置事务回滚旧后端，不再把普通呈现fallback当XeSS切换成功；启动时的基础播放fallback保留，新增持久后端警告和实际活动状态。选择器上方显示applied后端与等待有效补帧/运行。
- R1首版：同一FrameFlowWindow记录源帧完成、有效生成完成、总产出、Present提交和XeSS SDK送呈现速率；按batch去重、容量8192、停流衰减。保留`s.fps`兼容字段为源处理，底栏改用总产出或明确标注的XeSS SDK提交。
- 软件总延迟使用真实源帧进入本进程至Present返回端点，最近一秒均值/P95；文件为取帧开始至Present返回驻留，非扫描/光子延迟。生成帧年龄与完整逐阶段聚合仍待补齐。
- 实时状态改为独立可滚动区域，主产出/总延迟固定，其余处理/生成/呈现/输入、各阶段、尺寸和音频状态集中显示。布局和像素审查仍在进行。

## 实际验证

运行时：`runtime_local/nvidia/nvngx_dlssnr.dll` SHA256仍为`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，签名Valid。未替换DLL。

`python scripts/acceptance/ui-fg-backends.py`：旧EXE实测失败，标题 `[994,294,1266,324]` 与后端 `[994,290,1266,318]` 重叠，证据 `logs/continuation-repair-20260910/ui-fg-1789052091042365500/result.json`。初版脚本在专业动画前取状态误报隐藏，已改为等待专业模式。

Release构建通过：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <root> -Preset x64-release`，日志 `logs/continuation-repair-20260910/build-r0*.log`、`build-r1.log`。已有FFmpeg转换警告与probe未使用变量警告，未新增编译错误。

R0正常UI切换通过：`ui-fg-1789052625652703800`，FRUC -> XeSS -> DLSS -> off，两种窗口尺寸、真实自绘popup点击提交、applied版本一致，9.73秒。对应EXE SHA `74ECED96D403B9CEB63E00836B981E81210A02B16087BF8B85B25641E69BCA44`。XeSS为实际SDK初始化与呈现，不是AMD实卡或扫描验收。

`python scripts/acceptance/ui-fg-backends.py --reject-xess`通过：`ui-fg-1789052653905109600`；故障注入在加载前返回失败，FRUC保留、applied日志不出现XeSS，再切DLSS/关闭成功。没有破坏/重命名本机DLL。测试观察应用控件revision，退出后复核日志；之前只等缓冲日志曾超时，但退出日志确认DLSS已应用，已修测试判定。

`scripts/acceptance/scheduler-short-test.ps1 -Name continuation-r1-contract -Exe out/build/x64-release/veyra_repair_contract_tests.exe`通过49项：60真实+40有效生成=100产出、90呈现、重复完成不计、XeSS单列、延迟样本衰减、1000fps支持；0.10秒，日志在`logs/scheduler-repair-20260910/continuation-r1-contract.*`。

`scripts/acceptance/scheduler-short-test.ps1 -Name continuation-r1-half -Exe out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('loop/local/fixed_clips/test_av_1080p.mp4','logs/continuation-repair-20260910/live-half','--half-rate')`通过23项，10.98秒。GPU真实完成30/60切换、NR/2X/4X、暂停/重配/退出、有界租约与epoch。文件模拟采集，未占实体采集卡。

R1首版UI切换通过：`ui-fg-1789053218053203700/result.json`，EXE SHA `C4571EF78ED0F8883E61A2CD7B6B38D49C7719E4EB09B80502492F461817E6BC`。该次截屏被其它前台窗口遮住，不能作为状态区视觉通过证据；后续改用本应用窗口独立绘制截取，不操作其它软件。

## 当前未完成

### 续接进展（2026-09-10，仍在施工）

- R1补正：Drop只重置epoch账本，同revision的完成速率窗口保留，避免重载时永远“采样中”；新revision仍独立。51项CPU合同通过，`continuation-rates-epoch`，0.072秒。小窗口保留参数区域至少约200逻辑像素，状态明细滚动；`ui-fg-1789055596503007200`正常切换通过，含1040x540请求尺寸；不是完整100/150/200DPI验收。之前独立窗口截图发现状态区glassText绕过父DC clip，已跳过半露行，`ui-fg-1789053467482583100`复测通过。截图不能证明flip视频像素。
- 文件音频确认并修正：原`nextPtsMs_`在push前被赋为输入音频块结束PTS，错误偏移一个块；按输入PTS减swr延迟标记输出开头。处理decoder EAGAIN/EOF和resampler尾样本，seek按输出样本裁剪、清空swr历史，seekRequested改atomic、ring水位加锁。WASAPI先填真实PCM再Start，未启动/reset/clock失败返回NaN；requestSeek返回实际重锚PTS，不返回已被首缓冲消耗后的队头。初版paused-seek测试因此失败，修复后通过。
- `veyra_audio_timeline_tests`两种PCM输入（44.1/48kHz）均完整输出36000个48k样本/750ms，PTS误差约1.7e-12ms；采样级seek、设备暂停、seek重锚、无效clock测试通过。实际WASAPI输出设静音，`continuation-audio-timeline-fixed`，0.544秒，测试EXE `3658B889A2E2E19F6B8F2242BDA226C356CA67CA666664321195D4ACE2DD5481`。
- 采集音频施工：新增共享`AudioPcmSource`接口，`CaptureAudioSession`复用AudioRenderer；原NativeCaptureSink扩展明确PCM类型的终端pin。DirectShow音频不再RenderStream自动直通，协商PCM后写有界队列，图显式SystemClock，WASAPI线程转换/播放。视频Present PTS/host映射估算自动0..250ms补偿；声音队列/偏差/重锚/溢出显示。新设置自动/手动/关闭，手动-250..250ms，负值实际钳制到0；预设schema7兼容旧版，roundtrip通过`continuation-presets-audio7`。当前策略仍需审查突变、过载、端点错误和真实设备；不能称实体采集音画已经修好。
- `veyra_capture_audio_tests`合成连续PCM与80ms视频延迟，真实WASAPI：平均绝对软件偏差12.795ms，89样本，补偿86.622ms、PCM队列58ms、0溢出/0欠载，`continuation-capture-audio80`，1.698秒。仅验证合成PTS，不打开实体采集卡，不测scanout。
- 文件过载新增共同缓冲：音频领先视频>80ms暂停音频主时钟，视频保留所有源帧推进，追到20ms内恢复；刚写入，尚未构建/验证，不当通过。
- FRUC新候选：worker双向D3D11自有纹理桥接（输入、输出均经D3D11复制）。首次缺NTHANDLE导致Register=3；补标志后API成功但reset像素仍旧输出（第二对偏移-12而应-4）。`continuation-fruc-owned{,2}`失败，全部候选已撤回，Worker与HEAD无逻辑diff。尚未修复FRUC reset，不引入cuCtxSynchronize产品等待。
- AMD固定仓库成功克隆到ignored `third_party_local/amd/nr-d3d12`，sparse checkout `src/shaders/scripts`，detached `628620a74f3fcd3b5081620bd86308f57fc210a3`。README确认完整71block、SM6.10 FP8 linalg、Agility721，权重不在仓库（作者约16GB含参考dump），本机尚无权重/预览编译器/目标RX9070XT。本轮还未接provider，不将AMD光流或选择器算作AMD NR完成。未修改或抽取本机NR二进制。

最新已构建UI EXE（音频设置后、文件过载补丁前）`BAE93B5DE106E8940815FDA017CFA9E93C599902EE8531817B469B72E8219792`，`build-audio-settings.log`。第一次采集音频构建因缺mmreg.h失败，修复后通过。所有失败日志保留，单次测试<300秒。未Git提交、未push、未发布。接着验证文件过载/采集突变与Native音频pin，再推进GPU完成观测/调度和AMD依赖。

R0 NR backend/provider、R1完整计时与状态布局验收、A1 AMD完整网络、R2 FRUC同步/完整GPU状态机、R3音画同步及R4集成回归均未完成。新指标仍由原presentation worker观测GPU-ready，尚未解决该观测时机受调度等待影响的问题。文档和测试通过不是全目标完成。

下一步：完成R1窗口自身截图与小尺寸布局审查、补充统计回归，再按方案推进AMD provider和FRUC/音频。代码在工作树，未push、打包或发布。

