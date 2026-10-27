# 软件处理链路音画同步修复

> 0.0.4之后的4060反馈确认持续欠速仍有音频卡顿；本文两轮修复及短测不代表该问题已解决。继续修复以 [连续音频与实时视频调度方案](../plans/REALTIME_AV_SCHEDULING_REPAIR_PLAN_2026-09-11.md) 为准，当前仅已制定方案。

> 后续用户验收发现声音随 30/35ms 处理波动卡顿。第一版以下短测只证明时钟偏差有界，没有证明音频连续；当前继续修复见文末“音频连续性修订”，第一版不能视为用户验收通过。

## 本次范围

补偿 Veyra 内部解码、SR、NR、FG、GPU 等待和呈现调度增加的视频等待。采集卡共同的输入音视频延迟不额外计入；不估算 HDMI 到屏幕物理延迟，不修改导出。

## 已核对的问题

- 文件在首张有效视频 Present 前启动 WASAPI；seek、暂停恢复、设置重建也可能先放声音。
- 文件过载只在视频线程完成 GPU 等待后，才按领先 80ms/回到 20ms 暂停/恢复音频。GPU 或设置重建阻塞时这段代码无法工作。
- 采集自动补偿按 `视频 Present host − 视频 PTS − 音频 ingress 映射` 计算，能抵消共同输入延迟。但补偿被截在 250ms；重建及暂停没有主动失效旧视频映射。
- Present 返回是提交时刻，当前没有屏幕扫描或扬声器声学测量，不宣称绝对物理同步。

## 实施

1. 文件音频预解码及 WASAPI 预填充保留，但首帧呈现前停止设备时钟。seek/设置/暂停恢复复用视频等待状态。
2. 音频线程根据可呈现视频的 PTS 上限自行停表；即使视频线程卡在 NR/FG 或重建中，声音也不能持续跑远。GPU 就绪帧允许共同时钟前进到该帧，真正 Present 后再允许下一个帧间隔；不丢文件源帧，不按 GPU 平均耗时硬加固定延迟。
3. 采集自动音频等待首次有效视频及重置后的新呈现。自动补偿上限扩至 1500ms、PCM 总预算 2000ms，仍有硬上限和超限状态；手动偏移保持 ±250ms。真实输入/输出 PTS 决定补偿，不加采集卡硬件延迟。
4. 使用真实 WASAPI 验证首帧/停顿/seek/恢复、延迟变化、共同输入延迟抵消；Engine 测试覆盖实际 NR/SR/FG、设置、暂停和 seek。每个测试有不超过 300 秒外部超时。

## 状态

代码及本机短测完成，用户实际听看验收待执行。根目录 `Veyra.cmd` 启动本次构建的 `out/build/x64-release/veyra.exe`。

## 改动文件

- `src/engine/EngineController.cpp`：首帧/seek/恢复/设置的共同锚点、GPU 就绪到呈现的音频许可范围；取消视频线程的 80/20ms 事后追赶。
- `src/sink/WasapiAudioSink.cpp`、对应头文件：音频线程独立执行视频等待，保留 PCM 和设备时钟；恢复端点也保留该等待。
- `src/sink/CaptureAudioSession.cpp`、`src/source/CaptureCardSource.cpp` 和对应头文件：失效旧呈现锚点、扩大有界自动补偿范围。
- `include/veyra/engine/EngineController.h`：真实视频等待次数诊断。
- `tests/integration/{AudioTimelineTests,CaptureAudioTests,LivePresentationTests}.cpp`：真实设备时钟与实际产品图回归。

## 实际验证（2026-09-11）

以下路径均相对项目根目录；所有日志在 gitignore 范围。

| 检查 | 实际命令 / 关键参数 | 结果与证据 |
| --- | --- | --- |
| Release 构建 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <项目绝对路径> -Preset x64-release` | `logs/software-av-sync-20260911/build4.log` exit 0 |
| 文件音频 PCM / WASAPI | `scripts/run-short-test.ps1 -Exe out/build/x64-release/veyra_audio_timeline_tests.exe -Arguments logs/software-av-sync-20260911/audio-fixtures -TimeoutSeconds 60 -LogPrefix logs/software-av-sync-20260911/audio-timeline3` | 68 PASS，exit 0。首帧等待 350ms 时 PCM 时钟不走；视频覆盖到 100ms 后故意再阻塞 350ms，音频停在 120ms 且不再增长；含首帧、设置、seek、暂停恢复和设备失效 |
| 真实 NR/SR/FG 文件播放 | `scripts/run-short-test.ps1 -Exe out/build/x64-release/veyra_live_presentation_tests.exe -Arguments @(<test_av_1080p.mp4绝对路径>,<logs/software-av-sync-20260911/file-overload绝对路径>,'--file-overload') -TimeoutSeconds 180 -LogPrefix logs/software-av-sync-20260911/file-overload` | 10 PASS，exit 0。RTX 5070、1080 输入、4K 超分/原生 NR/FG4；100 源帧及 FG4→2 重建/播放中 seek/暂停 seek/恢复。Present 时最大观测音频领先 26.667ms。实际 Create Feature18 / DLSSG 与 Evaluate `0x1`、SEH 0；完整 `file-overload/engine.log` |
| 文件端点恢复 | 同上测试 EXE，参数 `--file-endpoint`，日志目录及前缀 `file-endpoint`，外部超时 60s | 7 PASS，exit 0。端点离线保持共同时钟，重连、暂停 seek 和恢复通过 |
| 采集软件延迟 | `scripts/run-short-test.ps1 -Exe out/build/x64-release/veyra_capture_audio_tests.exe -Arguments --software-delay -TimeoutSeconds 60 -LogPrefix logs/software-av-sync-20260911/capture-audio2` | 16 PASS，exit 0。合成 DirectShow 风格 PTS + 真实 WASAPI。自动 80/160/400/900ms；共同输入偏移 900ms 时软件额外 400ms 仅补 400.485ms，额外 900ms 补 900.082ms。自动稳态平均软件偏差约 18–23ms；重建等待、手动/关闭、断流、重开及 2000ms 总队列硬上限通过 |
| 采集端点恢复 | 同上 EXE，参数 `--endpoint-loss`，前缀 `capture-endpoint`，外部超时 30s | exit 0，2 次端点打开，平均软件偏差 16.335ms，无溢出 |
| 完整交付短测 | 通过 `run-short-test.ps1` 以 300s 外部超时执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <项目绝对路径>` | 23 PASS，48.892s；`logs/delivery/ab1cf0c67a174cc3845c0eb1336c1bae/result.json`。含实际 NR/NVOF、原生 4K、播放、图像、NVENC H264/HEVC 带音轨/帧数/时间戳及取消。导出完整性 gate 未修改 |

`file-overload` 使用最终端点回退小修前的构建（build3）；其后仅修正无有效音频时钟时仍保留视频等待上限。最终 build4 已通过 68 项音频、文件端点恢复、采集合成及完整 delivery，不把先前测试冒充同一 EXE。

最终 EXE SHA256：`CD4E5EDA37CEACE83C09D327E123A160A0AE814E84729C5D18BD9D4FA68548EE`。原版 NR SHA256 仍为 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`；未改任何运行时文件。

## 失败与修正

- `audio-timeline` / `audio-timeline2` exit 1：新增检查最初要求 `stalled < 120ms`，真实 100ms 视频许可在此 WASAPI 22ms 端点上停止于恰好 120ms；第二次观测 350ms 后仍为 120ms。并发现等待标记早于实际 Stop 发布，已改为执行暂停后发布。验收明确为覆盖上限后 30ms 内且之后时钟不再增长（最终 120→120，通过），不冒充零误差。
- 首次无参数调用 `run-short-test.ps1` 被现有空 `ArgumentList` 包装器拒绝；改用测试标识参数 `--software-delay`，未改检查脚本或产品逻辑。
- `build2.log` exit 6：测试仍运行时重链接该测试 EXE，Windows 返回 LNK1104 文件占用。等待退出后顺序执行 build3/build4 均 exit 0；非源代码编译错误。

## 边界与下一步

- 未执行物理采集卡音画录制对照、扬声器到屏幕测量、长时间漂移或 XeSS 内部呈现延迟验证。当前证据是本机 WASAPI 消费时钟和 Present 提交时间，不能保证所有声卡/显示器的固定物理偏移都为零。
- GPU 吞吐持续不足时，文件音频会跟随画面停顿；此修复消除持续跑远，不把过载伪装成实时播放。实时采集自动补偿最多 1500ms，超过边界仍明确标记受限；2000ms 为音频容量上限，不是固定新增等待。
- 未提交或上传 SDK、DLL、模型；没有 push、发布或改写 0.0.3 Release。
- 下一步：用户使用本机 `Veyra.cmd`，分别以文件播放和真实采集测试 NR/超分/FG 开关与切换后的音画同步，按实际反馈继续校准。

## 音频连续性修订（2026-09-11，代码及针对性回归完成）

- 用户日志 `logs/audio-jitter-20260911/user-session-before.log` 是 4K30 文件，含 XeSS 与 DLSS。XeSS 每真实源帧 33.3ms，而上一版给音频的许可范围除以 2，只有 16.7ms；SDK 内部生成帧不会回调延长这个范围。修正为源帧间隔，对比模式也不按隐藏插帧划分。
- 旧文件音频一达到许可 PTS 就 Stop，不给线程唤醒、声卡事件或 GPU 完成抖动留容差。新策略保持首帧/seek/设置/暂停的显式等待；普通播放使用 20ms 死区，领先持续超过死区 100ms 或达到 80ms 才重缓冲，追上许可范围再恢复。这个容差是瞬时软件音画偏差预算，不是给采集卡硬件延迟补偿，也不是新增固定音频延迟。
- 新增真实 WASAPI、真实 PCM 的 30fps + 5ms 抖动测试，覆盖真实帧和显式 2×/4× 子帧。检查额外音频停止次数为 0、时钟持续以一倍速走、无缺音填零；不再仅以 Present 时偏差小作为通过条件。先执行旧版作为失败对照，证据 `jitter-before`。
- 采集现有连续重采样路径不使用文件的逐帧 gate；增加同类抖动检查，若通过则不做无关改造。旧正常低延迟及长阻塞测试随新容差要求保留同步有界检查。

### 第二轮实际证据

新增生产文件 `include/veyra/sink/AudioVideoContinuity.h`，由 `WasapiAudioSink.cpp` 的音频 owner 调用；`EngineController.cpp` 修正 XeSS 与对比模式的覆盖间隔。没有新增视频帧队列、固定播放延迟或改动采集重采样算法。`AudioTimelineTests.cpp`、`CaptureAudioTests.cpp`、`LivePresentationTests.cpp` 增加连续性和过载后恢复检查。

构建仍使用前述 `scripts/build.ps1`，最终 `logs/audio-jitter-20260911/build3.log` exit0。下面测试均经 `scripts/run-short-test.ps1` 外部超时包装，输出分别保存在所列前缀的 `.stdout.log/.stderr.log`；每次远低于300s：

| 测试 | EXE / Arguments / 超时 | 结果 |
| --- | --- | --- |
| 新旧连续性对照 | `veyra_audio_timeline_tests.exe`，`@('logs/audio-jitter-20260911/fixtures','--jitter')`，60s | `jitter-before` exit1：2×/4× 各有一次额外停表，3秒内音频分别只前进2964.25/2959.25ms。`jitter-after` exit0、15 PASS：1×/2×/4× 全为0额外停表、0underrun；约3秒内媒体进度分别2992.31/2999.12/2990.12ms，保持一倍速，未用静音填缺口 |
| 完整音频回归 | `veyra_audio_timeline_tests.exe`，`@('logs/audio-jitter-20260911/full-audio-fixtures')`，60s | `audio-full` exit0，68 PASS；含首帧、seek、暂停恢复、端点断开、长阻塞及 PCM 时间线。长阻塞时100ms视频许可的音频停在200ms并保持不动，符合80ms硬领先阈值加声卡事件响应的范围 |
| 原生1080 NR＋XeSS | `veyra_live_presentation_tests.exe`，`@(<30fps.mp4绝对路径>,<xess-continuity2日志目录绝对路径>,'--file-continuity')`，60s | 6 PASS、exit0，100张真实帧前进3.33333秒/耗时3.33751秒；额外音频等待0，采样末偏差1.167ms |
| 原生4K NR＋XeSS | 同上，输入`logs/audio-jitter-20260911/4k30fps.mp4`、目录/前缀`xess-native4k`，60s | 6 PASS、exit0；日志确认 source/base/NR/flow均3840×2160。100张真实帧前进3.33333秒/耗时3.33008秒，额外音频等待0，末次偏差0.896ms。NR P95约24.24ms、GPU就绪P95约29.20ms；实际Feature18 Create `0x1`、SEH0，XeSS Init/PresentStatus `0`且实际产生SDK生成帧 |
| 采集30/35ms交替 | `veyra_capture_audio_tests.exe`，`--jitter`，30s | `capture-jitter` exit0，5秒合成采集PTS＋真实WASAPI。稳定后额外reset/underrun/missing均0，P95软件偏差24.997ms。验证通过，未改采集生产算法 |
| 原生4K SR/NR/DLSS4过载及恢复 | `veyra_live_presentation_tests.exe`，`@(<test_av_1080p.mp4绝对路径>,<file-overload2日志目录绝对路径>,'--file-overload')`，180s | `file-overload2` exit0、12 PASS；持续过载最大观测领先96.667ms；FG重建、播放/暂停seek及恢复通过。恢复可实时处理的配置后偏差重新小于35ms，没有保留过载固定偏移 |

30fps测试输入由本机ffmpeg `testsrc2`＋`sine`生成8秒H264/AAC（1920×1080、3840×2160），证据`fixture/fixture4k`；没有读取新的用户私人素材或把测试媒体放进Git。第一份实际XeSS测试`xess-continuity` exit1必须保留：当时测试退出后GPU仍90–93%/约250W，Magpie正在运行；NR完成P95达81.9ms。用户回复“已停用，可以测试”后，开测前GPU5%/37.5W，再执行上述原生1080及4K，两项通过。这项外部争用不改变已经确认的Veyra逐帧停表及XeSS覆盖错误。

`file-overload`第一次exit1：只有“重建后4帧仍要求偏差<35ms”失败，因为它仍处于故意制造的持续过载。根据事先定义的连续性容差将该项改为有界120ms，并增加关闭高负荷配置后的独立<35ms恢复检查；不能把重建当成性能恢复，也不能将新的96.667ms过载偏差宣传成零偏差。

最终EXE SHA256：`93F9C4D7D596D833B7E3E347FBC225D77626810F42638E9B57BE88FBED949AC1`。根目录`Veyra.cmd`启动此构建。正常小抖动优先保持声音连续；超过吞吐能力的持续过载仍有有界等待，不能保证同时不丢文件帧、一倍速、音画同步且声音绝不断续。未执行实卡声学/屏幕录制对照或长期听感验收，未push或发布。

本轮最终交付短测：`run-short-test.ps1` 以300s外部上限执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root <项目绝对路径>`，`logs/delivery/c2d5b549af744590b66188aff1dd7cbc/result.json` 为23 PASS、45.421s、EXE哈希与上述一致。包括实际NR/NVOF/原生4K/播放器以及保持现状的图像与NVENC音视频导出检查。`git diff --check`通过；新增头文件和文档为自有文本，无SDK、DLL、模型或测试媒体进入源码变更。
