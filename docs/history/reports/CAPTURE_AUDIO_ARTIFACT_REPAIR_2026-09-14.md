# 采集音频爆音与沙沙声修复记录（2026-09-14）

> 2026-09-15 重新审查：用户明确否定当前候选的听感。下文阶跃过冲、cubic与块级峰值保护的历史结论不能作为用户沙沙声根因或修复成功的证明。新审查已复现补偿归零重建导致丢样，并发现峰值保护释放错误、空拉误淡入；但最新约15分钟会话保护/削波/欠载计数均为0，原始持续噪声仍须生产链波形定位。以 `CAPTURE_AUDIO_WAVEFORM_REPAIR_PLAN_2026-09-15.md` 为当前方案；本轮未修改产品或交付新EXE。

## 结论

本次问题不是单一的“音量太大”。采集音频链路同时存在三个内部风险：采集卡媒体类型按枚举顺序选择，可能先选到 44.1 kHz；WASAPI 端点实际约 22 ms，而采集启动只预填约 10 ms；实时欠载时旧逻辑会直接停止并重置 renderer。另有部分设备可能使用有效位数小于容器位数的 PCM，旧路径没有保留 `validBits`。

本地 USB3 采集卡实际枚举顺序是 44.1 kHz、32 kHz、22.05 kHz、11.025 kHz、8 kHz，48 kHz 在后面。修复后选择 48 kHz/16-bit，而不是依赖枚举顺序。对于 `WAVEFORMATEXTENSIBLE`，有效 PCM 位在容器中可能是左对齐的；Microsoft 的结构说明见[官方文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible)。

## 修改内容

- `AudioFormat.h` 增加采集格式排序：优先原生 48 kHz，再按临近采样率、声道布局、标准整数 PCM、完整有效位数排序。
- `CaptureCardSource` 保留并记录声道 mask、采样率、容器位数、有效位数、浮点标记，连接成功后输出最终选择的媒体类型。
- `CaptureAudioSession` 保留 `validBits`，对 16/32-bit PCM 清除容器低位填充，对 packed 24-bit 按左对齐规则扩展到 S32；转换后检查非有限值、削波和异常填充并纳入诊断状态。
- 采集 WASAPI 请求约 20 ms 端点缓冲，并将启动/重锚预填提高到最多 20 ms。该数值是抗调度抖动的安全窗口，不是端到端延迟承诺。
- 实时路径的短暂欠载不再写入伪造的媒体静音帧，也不立即 `stopAndReset`；端点保持静音、媒体时间线不被合成帧推进。连续约 30 ms 且输入也持续缺失时，先提交有限淡出，再清空 PCM、重置重采样/漂移校正并重新锚定。
- 状态面板区分欠载次数、缺口时长和实际插入的静音时长；欠载日志按首个/周期/持续故障限频。
- 集成测试改用非零低幅 PCM，新增 5.1、24-in-32 有效位数和真实采集卡音频状态检查；物理测试将应用增益设为 0，避免改变用户扬声器听感。

## 实际验证

构建目录：`out/build/audio-artifact-repair-20260914`。

构建命令：

```powershell
& '.\scripts\build.ps1' -Root (Get-Location).Path -Preset x64-release `
  -BuildDirectory (Join-Path (Get-Location).Path 'out\build\audio-artifact-repair-20260914') `
  -FfmpegRoot 'C:\veyra-deps\ffmpeg-ps5-dav1d-installed'
```

结果：完整增量构建 29/29、后续物理测试变更增量 2/2，均 exit 0。初次构建曾因 `CaptureAudioSession.h` 直接引入 `ks.h` 与工程 `GUID_NULL` 宏发生 include 顺序冲突而失败，改为前置声明 `WavePcmFormat` 后恢复；只有已有 FFmpeg/工程编译警告，没有新的编译错误。

针对性结果：

- `veyra_capture_audio_tests.exe`：exit 0；完整同步、模式切换、图 reset、持续欠载恢复和停止清理通过。持续输入故障在约 31 ms 后执行淡出/重锚，恢复后收到真实 PCM。
- `veyra_capture_audio_tests.exe --jitter`：exit 0；500 个 10 ms 输入块，追加 reset/underrun 为 0，p95 skew 26.29 ms，peak 0.0687，非有限/削波均为 0。
- `veyra_capture_audio_tests.exe --jitter --5.1`：exit 0；500 个 10 ms 5.1 输入块，追加 reset/underrun 为 0，p95 skew 23.74 ms，peak 0.0644，非有限/削波均为 0。
- `veyra_multichannel_tests.exe logs/audio-artifact-repair-20260914-prefill20`：exit 0，`MULTICHANNEL checks=31 failures=0`；24-in-32 转换峰值约 0.25，异常填充和非有限值为 0。
- `veyra_audio_timeline_tests.exe logs/audio-artifact-repair-20260914-prefill20 --jitter`：exit 0；1x/2x/4x 抖动倍率均为 0 underrun，文件音频连续播放通过。
- `veyra_capture_tests.exe capture:0:0:0:0`：exit 0；本机 USB3 Video + USB3 Digital Audio 静音物理回归通过。最终选择 48 kHz/16-bit/16 valid bits，2 秒收到 31 个音频块，输入 peak 0.01043，欠载 1 次/576 帧，插入静音 0，重锚 2 次。该结果证明本机真实链路运行，不证明反馈者采集卡或所有驱动均无欠载。

中间回归失败也已修正：第一次实时欠载方案把合成静音帧推进了媒体时间线，导致两个旧时序阶段失败；改为实时 `ReleaseBuffer(0)` 后完整采集音频测试恢复 exit 0。第一次物理音频断言因 source-only 测试没有视频呈现锚点而误判 renderer 未运行，实际已有 32 个音频块；测试改用显式音频时钟模式后复跑通过。

## 边界与未完成事项

- 物理回归使用的是本机 USB3 采集卡，不是反馈用户的具体型号；尚未取得反馈者新版日志和未静音听测，不能宣称所有采集卡的爆音/沙沙声已根治。
- 当前短暂实时欠载会产生真实的静音缺口，但不再把缺口伪造成媒体帧或用硬停止制造 click；若上游持续不发数据，产品只能淡出并重锚，不能凭空恢复丢失的音频。
- 本次没有修改 NVIDIA/NGX/DLSS 运行时，也没有执行 RTX runtime 的 Create/Evaluate；这是音频链路修复，不能用本机 GPU 结果替代音频验收。
- 未发布、未 push、未替换正式便携包或运行时文件。

## 二次排查：实测仍有声音后的复核

### 已确认的旧版内部触发点

重新逐行对齐 `logs/veyra-app.log` 后，确认旧版有一条与采集卡原始噪声无关的 click/爆音触发链：

```text
14:11:49.858  UI selector committed=true changed=true
14:11:49.859  video graph shutdown/rebuild
14:11:49.859  audio-fade submitted frames=240 realPcm=0
14:11:49.859  capture-audio-reset mode=0
```

也就是说，用户修改视频设置时，旧 `EngineController` 会调用采集源的 `videoReset()`；旧实现把仍然健康的音频 renderer 一起淡出、停止、清空 PCM 并重新锚定。同期长时间日志的 `underruns=0`，因此这条路径不能解释持续沙沙声，但足以解释设置切换瞬间的爆音。此前把这条旧日志误判成“自动时钟偏差重锚”是不准确的，已在本节纠正。

### 二次实现

- 视频图因设置重建时调用 `videoReset(false)`，保留没有断流的音频时钟、PCM 队列和 renderer；暂停/真正的音频断流仍可使用完整 reset。
- 用户主动修改音频同步模式或偏移时，记录 `capture-audio-sync ... reanchor=explicit-audio-setting`，执行有边界的淡出/重锚，使音频设置不会因为隔离视频 reset 而失效。
- 实时端点从短暂空拉恢复时，首段真实 PCM 增加约 5 ms 淡入，避免从任意非零采样硬切回来。
- 保留 5.1/24-in-32/非有限值和削波诊断；没有把测试容差变化当成音质修复。

### 二次验证结果

当前构建仍为 `out/build/audio-artifact-repair-20260914`。最终增量构建 29/29，测试夹具变更增量 2/2，均 exit 0。最新回归结果：

- `veyra_capture_audio_tests.exe`：exit 0；视频图 reset 保持音频时钟、显式音频同步设置、持续输入故障后的真实 PCM 恢复均通过。
- `veyra_capture_audio_tests.exe --jitter`：exit 0；500 个 10 ms 块，追加 reset/underrun 均为 0，p95 skew 20.4108 ms，非有限值/削波为 0。
- `veyra_capture_audio_tests.exe --jitter --5.1`：exit 0；追加 reset/underrun 均为 0，p95 skew 20.8171 ms，非有限值/削波为 0。
- `veyra_multichannel_tests.exe logs/audio-artifact-repair-20260914-second-pass`：exit 0，31 项检查、0 失败。
- `veyra_audio_timeline_tests.exe logs/audio-artifact-repair-20260914-second-pass --jitter`：exit 0；1x/2x/4x 文件音频均未发生 underrun，视频抖动没有停止连续音频。
- 本机真实 USB3 采集卡 `veyra_capture_tests.exe capture:0:0:0:0`：exit 0；最终选择 48 kHz/16-bit/16 valid bits，2 秒收到 31 个音频块，增益为 0，输入 peak 0.05127，记录 2 次/1632 帧欠载、0 帧软件插入静音、2 次重锚。它证明本机 DirectShow→音频会话→WASAPI 路径可运行，但不证明反馈者设备的听感已经正常；这次短测仍有调度边界风险。
- 同一台本机采集卡绕过 Veyra 的 DirectShow 原始录音约 10 秒：左右声道 peak 约 -41.27/-44.29 dBFS、RMS 约 -55.95/-60.99 dBFS，无满幅削波，但存在可测的低电平源噪声。该结果只说明本机输入源有噪声底，不能外推到反馈者的采集卡，也不能替代 Veyra 输出端的 loopback 对照。

### 当前结论与剩余闭环

“视频设置切换导致音频被硬重启”的内部 bug 已定位并隔离；但用户实测仍听到沙沙声，当前证据还不足以宣称沙沙声已经修复。因为尚未拿到反馈者在新版程序中的具体时间点、`capture-audio-format`/`live-audio-sync`/`capture-audio-underrun` 日志和输出录音，仍需区分：采集卡本底噪声、驱动/USB 调度欠载、Veyra 输出波形处理，还是扬声器/线路增益问题。未执行 RTX/NVIDIA runtime Create/Evaluate；未发布、未 push、未替换正式包。

## 三次排查：OBS 对照与尖锐瞬态复现

用户补充“同一采集卡在 OBS 没有沙沙声，Veyra 只在尖锐声音上出现”。这改变了判断：本底噪声不再是主假设，必须对比 Veyra 独有的波形处理。新增了阶跃/尖脉冲回归，不再只用低幅正弦覆盖音频链路。

### 复现到的真实内部机制

`CaptureAudioSession` 将采集 PCM 统一送入 48 kHz `SwrContext`。libswresample 默认 Kaiser 窗化 sinc 在瞬态边缘会产生过冲，旧代码随后把大于 1.0 的 float 硬裁剪到 [-1, 1]：

- 44.1 kHz 输入重采样到 48 kHz：修复前阶跃测试出现 30 个裁剪样本，峰值 1.05091。
- 即使输入已经是 48 kHz，只要音画自动同步调用 `swr_set_compensation`，同一个 context 也会进入有效变速重采样；修复前 48 kHz 补偿瞬态测试出现 32 个裁剪样本，峰值 1.13351。

这与“OBS 正常、Veyra 在尖锐声音上出现沙沙/砂砾声”高度吻合：稳态正弦和低电平声音不触发过冲，尖锐边沿会触发硬裁剪。当前证据是软件内部合成波形的可重复复现，不是凭用户描述猜测。

### 三次施工

实时采集的所有 `SwrContext` 统一设置 `SWR_FILTER_TYPE_CUBIC`，不只对非 48 kHz 输入设置；即使当前输入是 48 kHz，后续漂移补偿也仍然使用瞬态安全的滤波器。失败时不静默退回未知滤波器，日志增加 `capture-audio-resampler inputRate/outputRate/filter/compensationSafe`。

新增 `veyra_capture_audio_tests.exe --transient` 和 `--transient-comp`：分别覆盖 44.1→48 kHz 瞬态，以及 48 kHz + 自动漂移补偿瞬态。

### 最终复核

- 构建命令仍为 `scripts/build.ps1 -Preset x64-release -BuildDirectory out/build/audio-artifact-repair-20260914 -FfmpegRoot C:\veyra-deps\ffmpeg-ps5-dav1d-installed`；最终 29/29 exit 0。
- `--transient`：exit 0，峰值 0.999969，clipped=0，nonFinite=0。
- `--transient-comp`：exit 0，48 kHz 自动补偿期间峰值 0.999969，clipped=0，nonFinite=0。
- 完整 `veyra_capture_audio_tests.exe`：exit 0；图 reset 保持音频时钟、显式同步设置、持续欠载重锚和停止清理通过。
- `--jitter`：exit 0，追加 reset/underrun=0，p95 skew 20.6725 ms，clipped=0。
- `--jitter --5.1`：exit 0，追加 reset/underrun=0，p95 skew 20.7548 ms，clipped=0。
- `veyra_multichannel_tests.exe logs/audio-artifact-repair-20260914-final`：exit 0，31 checks/0 failures；`veyra_audio_timeline_tests.exe ... --jitter`：exit 0，1x/2x/4x 连续音频通过。
- 本机 USB3 实卡：exit 0，实际选择 48 kHz/16-bit/16 valid bits，收到 32 个音频块，增益为 0，peak 0.05359，欠载 1 次/576 帧，软件插入静音 0，重锚 2 次。它验证了当前本机链路，没有验证反馈者设备的最终听感。
- 独立 WASAPI 4 秒 harness：mix 48 kHz/2ch/32-bit，0 underrun，时钟漂移 0.01 ms，Stop+Reset padding=0，exit 0。

### 结论

“漂移补偿启动默认 Kaiser 过冲，再被 Veyra 硬裁剪”的瞬态失真已经在代码中复现并修复；这比此前仅看到欠载计数为 0 的证据强得多。但仍需反馈者用当前构建实测确认，因为用户设备可能还有独立的 USB 欠载、输入位宽或线路问题。当前程序路径和剩余验收边界不变；未执行 RTX/NVIDIA runtime Create/Evaluate，未发布、未 push、未替换正式包。

## 2026-09-15 用户复测否定 cubic：撤回低通方向并改为峰值保护

用户实际听测确认上一轮 cubic 方案使声音发闷，且沙沙声更明显。这个反馈否定了“换成 cubic 就是修复”的结论：上一轮 `clipped=0` 只证明 cubic 压住了过冲计数，不证明音频保真，也不能作为交付依据。上一节保留为历史排查证据，但 cubic 不再是当前实现。

本轮已从 `src/sink/CaptureAudioSession.cpp` 移除强制 `SWR_FILTER_TYPE_CUBIC` 及对应 `libavutil/opt.h` 依赖，恢复 libswresample 默认 Kaiser 窗化 sinc；当输入和输出同为 48 kHz 时保持原本的 identity 路径，只有实际发生采样率转换或时钟补偿时才进入重采样。针对 Kaiser/补偿在尖锐满幅边缘产生的 1.0 以上样本，改为转换块级共享增益保护：以 0.995 为上限，对整块 PCM 做线性缩放并带释放，不对样本逐点硬裁剪，不引入低通；`CaptureAudioState.peakProtectedSamples` 与限频日志记录保护是否实际触发，`clippedSamples` 只代表保护之后仍发生的硬裁剪。

实际构建命令：

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "C:\Users\123\Desktop\Veyra DLSS Video Player" -Preset x64-release -BuildDirectory "out/build/audio-artifact-repair-20260914" -FfmpegRoot "C:\veyra-deps\ffmpeg-ps5-dav1d-installed"`

最终代码构建 exit 0；随后测试目标增量构建 exit 0。串行回归结果：

- `veyra_capture_audio_tests.exe --transient`：exit 0；44.1→48 kHz 尖锐阶跃 `peak=1.05091`，`peakProtected=9582`，`clipped=0`，`nonFinite=0`。
- `veyra_capture_audio_tests.exe --transient-comp`：exit 0；48 kHz 自动补偿尖锐阶跃，实际日志 `peak=1.04561`、保护增益约 `0.951596`、`peakProtected=9570`，`clipped=0`。
- `veyra_capture_audio_tests.exe --jitter`：exit 0；p95 skew `20.4106 ms`，`clipped=0`。
- `veyra_capture_audio_tests.exe --jitter --5.1`：exit 0；p95 skew `20.3669 ms`，`clipped=0`。
- `veyra_multichannel_tests.exe logs/audio-artifact-repair-20260914-corrected`：exit 0，`checks=31 failures=0`。
- `veyra_capture_tests.exe capture:0:0:0:0`：exit 0；本机真实 USB3 采集卡实际选择 48 kHz/16-bit/16 valid bits，音频块 31，欠载 1 次/576 帧，测试增益为 0。它验证真实 DirectShow→音频会话→WASAPI 链路，不等于扬声器听感验收。

当前版本仍需用户用未静音的真实尖锐声做 A/B 听测；本轮没有执行反馈者设备，也没有执行 RTX runtime Create/Evaluate。只有在用户确认高频未变闷、尖锐声沙沙声消失后，才能把这条候选修复标记为听感通过。

## 2026-09-15 进一步修正：无补偿时真正恢复 identity，并修复重建生命周期

继续审查后发现，旧的 `clearCorrection()` 即使传入 `swr_set_compensation(swr,0,0)`，也会让 48 kHz 同速 `SwrContext` 进入强制 resample 标志；因此“48 kHz identity”在此前代码里并不完全成立。当前改为记录 `compensationActive`：无补偿时不调用该 API；补偿结束、欠载重锚、同步重置或大幅偏差重锚时，创建新的默认 context，并通过同一个 RAII owner 接管，恢复真正的 native-rate identity。此前一次重建测试发现旧智能指针仍持有已释放 context，已修正后才收口。

最终构建命令同上，完整 29/29、exit 0。最终串行证据：`--transient` exit 0（peak 1.05091、peakProtected 9582、clipped 0）；`--transient-comp` exit 0（peak 1.13518、peakProtected 9570、clipped 0，重锚日志明确 `native-rate path restored`）；`--jitter` exit 0（p95 20.7114 ms）；`--jitter --5.1` exit 0（p95 20.4901 ms）；`veyra_multichannel_tests.exe` `checks=31 failures=0`；`veyra_audio_timeline_tests.exe --jitter` 的 1x/2x/4x 均通过；本机 `capture:0:0:0:0` exit 0，实际 48 kHz/16-bit/16 valid bits，31 音频块，欠载 1 次/576 帧，增益0，peak0.13455。

这次才是当前候选构建，路径仍为 `out/build/audio-artifact-repair-20260914/veyra.exe`，文件大小 3,375,616 bytes，SHA-256 `A98B657FA5F66F5C2A3CD26ADBFE0051DDB7C8EA7DCFC7F5D8907909D57EA7FA`。自动化和本机链路均通过，但反馈者的未静音听感仍未完成，不能把“沙沙声已消失”写成事实。
