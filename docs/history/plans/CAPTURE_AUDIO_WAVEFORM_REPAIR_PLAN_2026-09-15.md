# 采集音频沙沙声：重新审查与波形闭环方案

日期：2026-09-15。状态：**已实施已知缺陷修复，最终专项回归通过；真实火堆/口哨听感未验收**。

> 下文§1–5保留首次审查时状态。当前实施与验证以文末§6为准，不将已知缺陷修复泛化为持续沙沙声已根治。

本轮用户报告火堆、吹口哨呼唤马匹时仍有沙沙声，OBS 对照正常，要求重新审查前序 Luna 改动并给出方案。不能将旧“开始修复”扩展为这次未请求的盲改。当前只新增离线诊断脚本、方案和工作记录；不替换 EXE、不发布、不改运行时。

## 1. 结论及证据等级

1. 前序将“合成满幅阶跃会过冲”直接认定为用户沙沙声根因，证据不成立。尖锐的音高不等于接近数字满幅；满幅阶跃可触发削波，不证明真实口哨/火堆触发了同一机制。
2. cubic 已撤回，当前默认 Kaiser 路径仍被用户否定。不能继续低通、按块自动压音量，也不能把 `clipped=0` 当保真验收。
3. 当前代码有三个可确认的缺陷/风险：峰值保护增益跳变和释放错误；补偿归零时销毁有延迟历史的重采样器；把软件空拉等同设备断音并触发淡入。前两个有离线数值反例，第三个有代码与 WASAPI 合同依据。**这些不是对全部持续沙沙声的已证实解释。**
4. 固定补偿的实际 libswresample 离线测试未发现明显高频杂散失真。当前重点应是动态时钟控制、块拼接/端点提交及采集协商与 OBS 的差异，不是换一种滤波器赌听感。
5. 缺失的决定性证据是：同一次问题声音在原始输入、转换后、最终提交和系统回录四个位置的波形。拿到首次出现额外失真的阶段，再修改该阶段。

## 2. 当前构建和真实使用日志

审查构建：`out/build/audio-artifact-repair-20260914/veyra.exe`。

SHA256：`A98B657FA5F66F5C2A3CD26ADBFE0051DDB7C8EA7DCFC7F5D8907909D57EA7FA`。本轮未重编译/替换。已有未提交改动全部保留，包括 MOV 负 AAC PTS 修复与采集设备枚举改动。

`logs/veyra-app.log` 最新完整采集会话从第26036行开始，2026-09-14 16:09:11Z 至16:24:13Z，即本地9月15日00:09–00:24：

- 配置48 kHz / S16 / stereo，有效位16，音量1；明确记录 `default-kaiser-or-identity` 和 `native-rate path restored`。可证明使用了当前修复路径，但旧日志没记录完整 EXE hash，不冒充逐字节匹配证明。
- 448条 `live-audio-sync`，全程累计 `peakProtected=0 clipped=0 underruns=0`，最终转换后峰值0.95073；`resets=1`。
- `native-rate path restored` 共5次，最后在16:10:18.894Z。它们未计入 `resets`，所以旧 `resets=1` 不能证明重采样器没被重建。
- 后段持续非零 `correctionPpm`，例如末条-1243.0，软件补偿时长0ms不等于关闭变速。最终块间隔10.019ms、块时长10ms。
- 日志没有用户标记的口哨/火堆精确时刻，也没有输出PCM。因此不能把该会话某条状态强行绑定到某一次听感。

这段日志对候选原因有反证价值：新增峰值保护和空拉淡入未在这段会话触发，不能拿这两个问题解释这段会话全程持续噪声；5次早期重建也不能解释十多分钟后仍出现的问题。

## 3. 前序代码审查

### 3.1 块级峰值保护不是“保持波形与频谱”

位置：`src/sink/CaptureAudioSession.cpp:204–215`。

- 攻击时整块增益瞬变，块边界没有连续包络。
- 释放分支用 `min(1, gain+0.05)`，而非受当前安全目标约束。同样峰值1.05的连续块，增益在0.947619与0.997619间来回切换；第二块保护后峰值仍1.0475，随后真实硬裁剪。
- 下一块峰值低于0.995时完全不乘保留的 `peakGuardGain`，立即回到1；所谓释放状态没有实际应用。
- 注释“never hard-clips”与紧接的 `std::clamp(sample,-1,1)` 矛盾。局部常数缩放不等于整个时变信号频谱不变。
- 指标 `inputPeak` 位于 `swr_convert` 后、保护前，不是采集原始输入峰值，UI与诊断命名要纠正。

### 3.2 补偿归零不能直接换 context

位置：`src/sink/CaptureAudioSession.cpp:102–110, 332–338`。

`delta==0` 且曾进入补偿时，`clearCorrection()` 不 drain、不衔接，直接释放旧 `SwrContext`，丢掉滤波历史和待输出内容。它还将滤波误差和增益状态清零，混淆“暂时零校正”与“会话断点”。

本轮直接调用应用目录 FFmpeg 9.0.1 的 DLL，用-1250ppm、0.5幅值连续4kHz正弦，1秒处将补偿置零，对比“保留context，仅取消补偿”和“销毁重建”：

- 旧 context 仍有16输入帧延迟；最终 drain 后，重建分支少17输出帧。
- 边界后的第一个样本：保留为-0.496447，重建为+0.325928。
- 边界后48帧最大差0.871988。

这是实际库的离线波形反例，不是完整播放器或实卡听测。相位偏差和丢样比0.35ms时长本身更重要。

FFmpeg 的 `swr_set_compensation(s,0,0)` 在**未创建 resampler**时会强制初始化重采样，这个前序发现成立。但正确边界是“首次启动选择好策略，活跃后保留历史”，不是每次补偿归零切回直通。[FFmpeg API说明](https://ffmpeg.org/doxygen/trunk/group__lswr.html)。本机源代码另核对 `C:/veyra-deps/ffmpeg-ps5-slices-source/libswresample/swresample.c:909` 与 `resample.c` 的补偿实现；实际执行证据以应用DLL为准。

### 3.3 空拉不等于设备已经断音

位置：`src/sink/WasapiAudioSink.cpp:401–460`；`include/veyra/sink/AudioGain.h:25`。

当前 `got==0` 就累计欠载并把下次PCM套5ms淡入；没有确认设备队列是否耗尽。`padding` 是尚待播放的有效帧，不是无声帧，见[Microsoft合同](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding)。

例如空拉时还有480帧/10ms，下一次回调在耗尽前到达，完全可以连续播放，却会在新块开头把增益变为1/240（约-47.6dB）再升回去。它能人为制造音量缺口。只检查 `padding==0` 也不够：应结合实际设备时钟、最后提交终点与实际写入时刻，分辨无缝接续和真实空洞；记录不足时注明未知。

此外，把可写容量 `avail` 累加为“缺失帧”会重复计算未填的空位，不能把此计数直接解释为声学丢失时长。

### 3.4 动态同步仍是待验证候选，不能直接判死刑

位置：`src/sink/CaptureAudioSession.cpp:252–258, 321–339`；`ArrivalClockMapping.h`。

当前每250ms由音画相位差更新变速，误差平滑为0.75/0.25，最大±5000ppm，每次变化最多250ppm，没有稳定死区；音频输入映射取2秒窗口最小值，视频延迟则取最新呈现值。它可能将呈现抖动、固定安全缓冲偏差混入真实设备频差。非零补偿本身并不是错误，独立采集/播放时钟本来就可能需要校正。

本輪固定-1250ppm、1/4/8/16kHz、0.5幅值S16测试：原生同速转换与S16/32768逐样本误差0；固定补偿拟合实际输出频率后，残差分别约-100.53/-100.96/-105.82/-112.71dBFS，幅值约0.5。未包含动态控制器、WASAPI、系统APO或真实输入。**因此没有证据说默认 Kaiser 本身必然造成可闻沙沙声。**

初次分析只用理论频率拟合，出现较高残差；复核发现是FFmpeg整数步长导致微小频差，已改为拟合实际频率，不能将频偏误报为噪声。

上游OBS的[FFmpeg重采样封装](https://github.com/obsproject/obs-studio/blob/master/libobs/media-io/audio-resampler-ffmpeg.c)也调用libswresample。仅作接口/行为参考，未复制代码；没有审计用户安装的OBS版本，也不将OBS全部监听策略直接搬入Veyra。

### 3.5 旧测试为什么漏检

- `--transient` 给44100Hz满幅矩形脉冲，仅检查计数；没有视频锚点，默认auto模式不启动播放端点。
- `--transient-comp` 是满幅平台，结束前断供150ms，最终补偿状态可能已经重置；不检查输出频谱、块边界或累计丢帧。
- 两者及物理回归设置增益0。静音本身可以用于安全自动测试，但必须在静音前截取输出验证，现有测试没有这样做。
- `--jitter` 循环重复同一小块正弦，非连续相位；只检查时间偏差/计数，不检查波形，不能验收高频保真。
- 已通过的格式、5.1、恢复、同步检查仍有价值，但覆盖范围必须如实收窄。

## 4. 实施顺序（待用户要求开始修复）

### P0：先固定失败与可观察证据

1. 固定当前代码/EXE身份，保留原包；仅本地新建候选构建目录，不覆盖用户正在测试的文件。
2. 在共享生产链上增加默认关闭的有界诊断tap：A原始回调PCM（格式/帧数/PTS），B重采样后，C音量/声道映射后、WASAPI提交前；同步记录每次补偿值、context epoch、延迟样本、pull数量、padding、设备时钟和写入帧序号。音频线程只写预分配内存环，后台限时落盘，满了明确标记diagnostic drop，不阻塞播放。仅记录用户选择的采集来源，不后台录麦克风或保存其他程序声音。
3. 同一次捕获的连续PCM在自动夹具中重放，分别跑当前生产链、固定补偿链和短时同速基线。不能依赖两次游戏随机火焰具有相同波形；原始录音及输出保持gitignore。
4. 必要时由用户显式启动回录，比较D系统输出。记录OBS比较的是监听还是录制、输出设备、采样率、声道/滤镜/增益，避免拿OBS录制文件与Veyra不同输出设备混比。一次只监听一路，避免双路叠加，但不预设用户犯了这个错误。

分流标准：A已异常→检查协商/回调/动态格式，和OBS同输入交叉验证；A干净B异常→转换/补偿；B干净C异常→增益/降混/缓冲；C干净D异常→端点提交、系统格式转换/APO/时钟。任何阶段不能凭峰值一个数代替波形。

### P1：去除已证实的新增失真，单变量验证

1. 撤销块级peak guard；保持默认高质量重采样，不换cubic、不加降噪/低通。有限浮点样本保留内部余量，异常NaN/Inf防护保留；若真实输出证明存在过载，单独设计最终音量/降混后的可验证输出余量策略，不通过无标注AGC掩盖问题。
2. 一个连续音频epoch保持同一重采样历史。自动模式在预填阶段准备连续重采样路径，避免播放中首次切入或归零切出；已激活的context归零只更新校正，不重建、不drain中断、不偷偷丢PCM。真正设备重连/PTS断点才明确reset和重锚；手动模式切换仍保留显式语义。
3. 空拉且端点仍有可连续播放数据时，不改后续音量，不记为已发生声学缺口。将 `emptyPull`、实际/可确认的设备gap、软件填零帧分开。真实中断的恢复才做有限淡入，不能为“去爆音”制造每块音量洞。
4. 保留独立的设备音频枚举、validBits解析、20ms预填、视频图重建不重启健康音频、MOV合法负PTS修复，不整文件回退。

### P2：按证据修动态同步，而不是永久关闭同步

1. 分离“固定软件延迟目标”和“采集—输出硬件频差”。固定延迟由预填与稳定锚点处理；频差用较长窗的累计实际音频帧、有效PTS和输出时钟趋势估计，不能用单次GPU/Present毛刺直接调音高。
2. 仅在P0对照证实后调整控制器：延迟目标去毛刺/稳定死区、频差估计平滑、变速比例连续且有界，记录每次实际请求。窗口/死区/上限必须通过波形和±1000ppm测试确定，不能拍脑袋把5000改成100就交付。
3. 不把“关闭自动同步”作为最终修复：短时同速基线只做因果对照，长时保持独立时钟校正、队列上限、真实PTS；不得靠丢PCM、插伪静音、无限缓冲或视频欠速拖慢声音通过。

### P3：新的交付门槛

| 场景 | 必须验证 |
| --- | --- |
| 连续相位1/4/8/16kHz，多个低于满幅的电平 | 同速转换逐样本正确；扣除真实必要变速后，不出现额外明显杂散/频响下陷；不能靠低通过关 |
| 补偿正→零→负、零附近往返、长时间固定补偿 | 有界且连续的历史、帧数/延迟闭合；无context切换丢样，无块边界尖峰 |
| 5/10/20ms块及不同边界相位 | 相同PCM换分块不改变结果（允许正确重采样舍入），无块级增益调制 |
| 空pull但padding未耗尽；真实延迟回调与断流 | 前者无淡入/静音/声学gap；后者准确计数、受控恢复，不伪报缺口时长 |
| 真实口哨、火堆与宽带噪声/多音/瞬态 | A/B/C/D确定首次失真位置；同一素材、同输出设备、匹配音量与OBS听测一致性验证 |
| ±1000ppm各120秒，视频30/35ms抖动与大延迟阶跃 | 同步误差和队列有界，无普通抖动触发reset；现有同步门槛不放宽，单次不超过300秒 |
| 立体声/5.1、packed24/24-in32、44.1→48、输出44.1/48/96（设备可用时） | 格式及声道位置正确，降混后也不过载；不可用设备标未执行 |
| 文件音轨含合法负PTS、PS5共享renderer、暂停/重连 | 不回归首帧死锁、已知恢复或其他入口声音；RTX增强测试单独报告 |

具体保真基准：对固定比率正弦排除启动/尾部后拟合实际频率，残差相对同库连续参考不劣化超过3dB，1–16kHz稳态幅度相对参考偏差≤0.1dB；同速无补偿的整数到浮点转换与直接归一化一致。动态变速须按实际采样轨迹比较，不能用固定正弦频率误判正常频偏。补偿归零丢弃待输出帧必须为0；无真实断流时新增淡入必须为0。以上是待执行门槛，不是这次已验收结果。

## 5. 本轮实际命令和产物

环境Python3.11、numpy2.4.3。执行（退出码0）：

```powershell
python scripts/diagnostics/audio-waveform-audit.py --dll-dir out/build/audio-artifact-repair-20260914 > logs/audio-waveform-audit-20260915/offline-dsp.json
```

脚本调用实际应用目录的 `avutil-61.dll`、`swresample-7.dll`，hash匹配所选patched FFmpeg prefix；只做内存内合成数据测试，不打开扬声器或采集设备。峰值保护/淡入案例显式标为代码算术模型，不冒充生产库集成。

FFmpeg DLL SHA256分别为 `C46EE9C04E8DADA7BEB4AEEE12F93A5D147D758E990F204141AA163D427B9688`、`A6A6876291DFA42E5609261DF2212038A2C7536EFCA0372E3899E12F8073B339`。

尚未执行：本轮产品构建、生产链tap/loopback、用户火堆/口哨录音比对、实卡听测、新同步控制器、RTX Create/Evaluate。未改任何DLL、SDK、版本号、发布资产。没有外部Reviewer验收。

下一条唯一任务：P0——在当前失败生产链增加受控波形证据与重放回归，先找到额外失真首次产生的位置，再逐项实施P1/P2并按P3验收。

## 6. 用户要求先修已知缺陷后的实施（2026-09-15）

本轮按“先把当前的问题修复”实施已确认的三处缺陷，不根据未验证假设调整动态同步控制器。未自动记录用户音频，完整原始/转换/提交三处tap及真实游戏素材同源对照尚未实施；通过共享生产DSP的离线波形对照、实际WASAPI受控空拉、现有产品集成回归先闭合P1。

### 实际修改

- 新增共享 `CaptureAudioDsp.h/.cpp`。`CaptureRateCorrection` 在自动模式新epoch预填前准备重采样；整个连续epoch内正/零/负补偿只更新同一context。归零不销毁、不清空滤波历史，不重置控制器误差。仅真正输入断点/端点恢复/显式同步重设仍走原有重置路径。
- `CaptureAudioSession` 移除块级peak guard及转换阶段的有限值硬裁剪。`inspectCapturePcm` 只清理NaN/Inf，记录转换后峰值与超过unity的样本；最终增益/声道处理仍在renderer。有限浮点超满幅不再错误等同已发生削波。保留旧状态字段兼容调用，`peakProtectedSamples/clippedSamples` 在此转换路径不再增加，不以它们为保真验收。
- `LiveAudioGapTracker` 依据padding为零且设备已消费位置超过已提交终点确认可观察缺口；按新增的时钟帧差计数，不重复累加可写容量。一段持续缺口只计一次事件；会话恢复判断同时看新增缺口帧，以保持30ms持续断流恢复。空pull仍有设备排队PCM时不触发淡入。
- 新增 `emptyPulls` 与 `recoveryFades`，同步日志明确列出 `convertedPeak`、`overRange`；UI将“输入峰值/保护”改为“转换峰值/超满幅”。设备时钟采样仍非物理声学测量，不能据零计数承诺驱动层零glitch。
- 保留设备音频枚举、validBits/packed24解析、20ms预填、视频图reset隔离、MOV负AAC起点修复；未调整现有同步上限/测试容差、未改DLL或SDK。

### 已执行验证和构建

新增 `tests/integration/AudioWaveformTests.cpp`，链接与产品相同的DSP实现及应用FFmpeg。覆盖44.1/48kHz、2/6声道、1/4/8/16kHz、正零负补偿、480/127帧分块；全部与保留历史参考的输出数量一致，逐样本最大误差0。反向故障控制重新引入滤波历史reset时，参考96001帧，错误分支95935帧，测试能检出。有限超unity、NaN/Inf、正确应用后续音量以及缺口状态检查合计43项通过。

另一个真实WASAPI用例在暂停端点中预填PCM，然后制造空pull：实际 `emptyPulls=1 gaps=0 fades=0`，再提交PCM仍无误淡入。端点暂停且音量0，不播放扬声器，也不将此测试当作声学验收。

首轮新构建因新增测试缺少 `<string>` 编译失败（C2039/to_string）；补齐后构建成功。随后沿用现有PS5依赖配置重新启用RemotePlay，完整326/326构建成功，避免沿用前序音频候选 `VEYRA_ENABLE_REMOTEPLAY=OFF`；最终新增实际端点用例增量2/2成功。原候选目录未覆盖。

构建目录：`out/build/audio-continuity-repair-20260915`。完整命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root 'C:\Users\123\Desktop\Veyra DLSS Video Player' -Preset x64-release -BuildDirectory 'C:\Users\123\Desktop\Veyra DLSS Video Player\out\build\audio-continuity-repair-20260915' -FfmpegRoot C:\veyra-deps\ffmpeg-ps5-dav1d-installed -RemotePlay -ChiakiCheckout C:\veyra-deps\chiaki-source -ChiakiStage 'C:\Users\123\Desktop\Veyra DLSS Video Player\out\remoteplay\chiaki-msvc-stage' -RemotePlayPrefixPath C:\veyra-deps\remoteplay-installed\x64-windows-static -ProtocPath C:\veyra-deps\remoteplay-installed\x64-windows\tools\protobuf\protoc.exe -PkgConfigPath C:\veyra-deps\vcpkg\downloads\tools\msys2\3e71d1f8e22ab23f\mingw64\bin\pkg-config.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/test-audio-continuity.ps1 -BuildDirectory out/build/audio-continuity-repair-20260915 -DriftOnly
```

首套11个独立进程均exit0：离线波形、实际排队空拉、采集基线同步/模式切换/恢复、立体声抖动、5.1抖动、44.1k和补偿瞬态浮点余量、端点故障注入恢复、多声道31项、完整文件音频时间线、文件抖动。每个用例独立限时，最长120秒；漂移用例各限150秒，不并发争用端点。瞬态仍只验证转换余量，不冒充实卡听感。

构建和回归日志：`logs/audio-continuity-repair-20260915/`，首套汇总 `results.json`，波形证据 `waveform.stdout.log`，实际端点证据 `queued-empty-pull.stdout.log`。本地原版NR hash复核仍为批准的E16BCF…FC8E；运行组件未修改。漂移与最终交付状态待下文收口。

### 对抗复核发现并修正的第二个断流边界

首套测试虽然全部返回0，但复核输入断供日志发现emptyPulls增加、设备时钟没有越过最后提交帧，未触发恢复；旧基线测试在恢复时同时切换了同步模式，显式模式reset掩盖了这个漏检。本轮未将这组通过直接作为最终验收。

新增 `--endpoint-gap`：真实WASAPI预填240帧、音量0，播放端点连续80ms无输入、8次pump，再恢复PCM。只依赖时钟帧差的第一版真实失败：`emptyPulls=8 gaps=0 fades=0`，证据 `true-gap-before.stdout.log`。

修正为双证据策略：设备时钟能量出缺口时仍按真实帧差计数；对时钟停在队列尾部的设备，连续两次确认端点为空且间隔至少一个实际 `GetDevicePeriod` 后才判持续断流。排队PCM仍在、短暂空观察或显式暂停不触发；不把墙钟等待伪造成已测设备缺失帧，单列 `clockStalledGaps`，日志 `clockMeasuredMissingFrames=unknown`，UI明确这类断流时长未量出。持续空pull仍可推进30ms恢复观察，避免缺口只记一次事件后失去恢复资格。

修后真实用例 `emptyPulls=8 gaps=1 fades=1`，且排队空拉仍 `emptyPulls=1 gaps=0 fades=0`。新增4项状态断言后离线波形47/47通过。基线测试改为断流前后保持相同auto模式，并在恢复输入之前断言已完成重锚；最终日志明确 `PASS persistent dry input recovers without a sync setting change`。

最终行为构建为 `build-verified.log`，增量34/34成功；所有回归在 `logs/audio-continuity-repair-20260915/final/` 重新执行，不用前一版结果代替。初次 `cmd /c` 使用正斜杠路径被cmd拒绝，改为反斜杠后正常，未改变构建参数。

### 最终收口

- 最终12组短回归全部exit0；生产DSP47/47、实际WASAPI有队列空pull与真实断供用例均通过。
- 最终 `-DriftOnly` 两组各120秒的合成输入时钟+真实WASAPI回归均exit0：1.001倍输入P95软件偏差4.24594ms，0.999倍4.30137ms；missing=0、resets=1（仅启动）、队列高水位均89.6458ms。同期同步日志无欠载或误恢复淡入。证据 `final/drift-results.json` 和两份stdout日志。不冒充物理采集卡晶振或端到端声学测量。
- 最终完整GUI空界面2秒启动检查exit0，日志 `final/gui-smoke.log`；没有播放媒体、没有执行NR/NVOF，不将零帧视为播放通过。
- 当前EXE：`out/build/audio-continuity-repair-20260915/veyra.exe`，11,442,688字节，SHA256 `F26C655DDF7D9CF07FBA35AEA323059B9F65708139AB580015CF788B23D1D552`。原测试EXE仍保留，hash未变。
- `git diff --check`通过（现有CRLF提示）；源码Git无新增DLL/SDK/模型。旧审查Python生成的单个PYC缓存已清理，仅移除本次工具产物，没有删除用户源文件。
- 未做RTX Create/Evaluate、全GPU delivery gate、真实火堆/口哨听测或分段PCM录音；没有push/Release，未替换任何运行组件。

下一条唯一任务：用这个明确路径的新构建复测同一火堆/口哨场景。若仍有噪声，直接做原始PCM→转换后→最终输出前的同源波形定位；已通过的自动检查不代替用户听感，也不作为再次盲改滤波器的理由。
