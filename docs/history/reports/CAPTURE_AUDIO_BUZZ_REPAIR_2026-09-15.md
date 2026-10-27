# 采集卡随音乐出现滋滋声：实录定位与修复

## 范围与现状

用户明确授权连接当前 USB3 Video / USB3 Digital Audio 采集卡录音，PS5 保持音乐开启。串流未报告相同噪声。本轮优先处理采集音频，暂停 HDR→SDR 后续工作；不操作游戏、不发布。分支 `codex/user-issues-repair-20260915`，前置提交 `f7bc4eb`。

## 根因证据

1. DirectShow 实际协商 48000 Hz / 16-bit / 双声道，10 ms（480 frame）回调。分别记录原始 PCM、连续 SWR 输出、队列 pull 和送给 WASAPI 的 PCM；最后用 Windows **指定进程** loopback 回录当前测试 Veyra，未记录其他程序/麦克风。
2. 无效果录音：转换后与 pull 对齐 463 frame 后逐样本一致。增强录音中，pull 与送入 WASAPI 的 PCM 在启动 240 frame 音量渐入后逐样本一致。输入没有过幅削波；不能因该证据宣称用户没有听到噪声。
3. 原控制器只按音频 PTS 与视频目标偏差调整 `swr_set_compensation`，负补偿不断缩短音频。目标低于实时播放所需的调度缓冲时，原有 20 ms 安全余量被耗尽，每个 10 ms 周期只有约 478～479 frame 可提交。驱动 `IAudioClock` 可在微小耗尽时停住，旧 underrun 计数仍为 0；连续 PCM 文件看不出 Windows 实际插入的间隙。
4. 修复前独立回录 `logs/audio-buzz-loopback-before-20260915.loopback.f32`：5～20 秒共 96 处前一 frame 幅值超过 0.002、随后双声道突然同时为零的断口；129 个双声道零 frame。断口集中在同一 480-frame 周期边界。启动 1 秒后的 1902 次写入全部观察到 padding=0。

这说明当前实机的一条确切软件缺陷是**同步控制耗尽播放缓冲造成周期性间隙**，不是把音乐振幅限制一下可以解决，也不能靠 swresample 单独离线测试排除。不同录音的音乐内容并不完全相同，不将峰值/RMS 差异当作音质改善量。

## 修改

- `include/veyra/sink/CaptureAudioDsp.h`：`captureSafeCorrectionPpm` 将时钟请求与实际音频储备约束结合；储备包含未转换输入、软件 PCM 和端点实际排队，沿用已有启动安全窗（本机 20 ms）。储备不足时限制负补偿并平滑补足；有富余仍允许追赶。安全限制可及时覆盖向下速度斜率，不重建 FIR、不丢 PCM、不加逐块 AGC。
- `src/sink/CaptureAudioSession.cpp`：接入储备约束，记录 `capture-audio-reserve` 的真实队列值、最低储备、限制状态和 ppm。不把这个调度储备冒充额外视频处理耗时。
- `tests/integration/AudioWaveformTests.cpp`：增加持续不可能的提前目标、低储备恢复、富余时追赶和正向斜率测试；原实际 FFmpeg 波形连续性测试保留。
- `include/veyra/diagnostics/CapturePcmRecording.h`、`AudioPcmSource.h`、`WasapiAudioSink.cpp`：显式环境变量 `VEYRA_DIAGNOSTIC_CAPTURE_PCM_PREFIX`（绝对路径）才开启采集源分段诊断；内存上限 20 秒，关闭后落盘，默认不录音。输出 tap 在成功 ReleaseBuffer 后使用自有 staging 内存，绝不访问已释放的 WASAPI 指针。记录保存失败单独警告，不冒充已保存。

## 已执行验证

- `cmd.exe /c out\build\veyra-build-x64-release.cmd`：录音版、修复版构建成功。日志 `logs/audio-buzz-record-build-b-20260915.log`、`logs/audio-buzz-fix-build-20260915.log`。末次仅诊断保存检查的构建见后续记录。
- `scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_audio_waveform_tests.exe -TimeoutSeconds 60`：`logs/audio-buzz-waveform-20260915.stdout.log`，51 checks / 0 failures。
- 同 wrapper 运行 `veyra_capture_audio_tests.exe`，60 秒上限：`logs/audio-buzz-sync-regression-20260915.stdout.log`，exit 0。实际静音 WASAPI，含同步模式切换、80/160/400/900 ms 目标、同源时间偏移、视频重建、真实输入停顿恢复和容量上限；不冒充物理 VRR 验收。
- 实机增强前后：Veyra 参数 `capture:0:0:0 --nr --no-sr --fg --smoke-seconds 32`；单独进程 loopback 28 秒，上层在进程超时后只终止自有测试进程。实际 NR/NVOF/FG 日志保留。
- 修复后成功样本 `logs/audio-buzz-loopback-after-b-20260915.*`：5～20 秒的上述断口 **0**、双声道零 frame **0**；启动 1 秒后 1899 次写入 padding=0 次数 **0**，最低 padding 168 frame，中位 486 frame。gain 渐入后 pull/render PCM 仍逐样本一致。日志中实际储备约 20 ms，没有通过停止声音或关闭增强通过测试。

## 失败与边界

- 第一轮修复后测试 `audio-buzz-loopback-after-20260915` 被用户另一份 Veyra 占用设备，DirectShow Run 返回 `0x800705AA`，录音为空；**不计通过**。用户关闭后独立重跑 after-b 成功，未强制关闭用户程序。
- 初始工具误用旧测试名/FFmpeg DirectShow 替代名、cmd 正斜杠路径，均已纠正；失败日志保留。
- 本机已证实和消除周期性间隙，仍需用户听感确认是否覆盖全部杂音。Windows 进程 loopback 不是耳机/扬声器的声学测量，也不代表其他型号采集卡已全部验收。
- 旧 underrun 计数无法可靠衡量极短的驱动停钟间隙，本轮不伪造缺失 frame 计数。用独立回录与实际 padding 做证据，新增储备日志供定位。
- 指定进程回录使用 Windows 官方公开 API，依据：https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/ 。本地工具 `out/audio-process-loopback.cpp` 只用于诊断，不进入应用或便携包。

## 最终构建与交付

最终构建 `logs/audio-buzz-final-build-20260915.log` exit 0。执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915`，46.48 秒 exit 0：`logs/delivery/3c3099c2022d4272a439d63b3062363b/result.json`。覆盖实际 NR/NVOF、原生 4K、播放/控制/图片、H.264/HEVC 4K 2X 导出及音轨；该 gate 的通用采集标签不替代本轮独立实卡录音证据。

可重复录音分析：`python scripts/diagnostics/capture-pcm-audit.py logs/audio-buzz-loopback-before-20260915`，after-b 同命令替换前缀。JSON 结果分别为 `logs/audio-buzz-before-audit-20260915.json`、`logs/audio-buzz-after-audit-20260915.json`。

候选 `out/build/audio-continuity-repair-20260915/veyra.exe`，SHA256 `912FECDF30FE385BC33F1A6272EF8DF8E695B43F1D76F650AC2B521422CEBF93`。启动入口 `out/start-user-issues-candidate.cmd`，不修改其他桌面入口。运行组件、用户配置、版本号不变；未发布。

持续漂移：`scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_capture_audio_tests.exe -Arguments @('--drift-slow') -TimeoutSeconds 150`，`logs/audio-buzz-drift-slow-20260915.stdout.log` exit 0。120 秒合成输入时钟慢 0.1%，实际静音 WASAPI；时差绝对值 P95=4.279 ms、missing=0、仅启动 reset=1、队列高水位89.646 ms，稳态 underrun=0。该数值是软件时钟差，不是声学端到端延迟。

## 后续唯一任务

交用户听感验收。本轮不继续扩展颜色或导出功能。
