# 采集卡音频高延迟排查与修复

用户反馈：同一采集卡在 OBS 正常，Veyra 音频延迟明显。未收到反馈者日志、增强设置或实际延迟测量，不能断言单一根因已经在其设备复现。开工存档 `checkpoint/capture-audio-latency-20260913`。保留工作区先前的缓存清理记录。

## 已确认的实现缺口

`CaptureCardSource` 在音频输出 pin 上直接 ConnectDirect，未调用 IAMBufferNegotiation。`NativeCaptureSink` 音频 allocator 仅要求一个 PCM sample 的最小字节数，未提供有效的实时块长要求。驱动可能按其默认大块交付，软件收到数据前就已经发生批量等待。

OBS 的 libdshowcapture 固定提交 `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`，`source/device.cpp` 的 SetAudioBuffering 在连接前调用 SuggestAllocatorProperties，普通设备默认请求10ms；有设备兼容例外。这是公开 API 调用方式参考，本次不复制其实现。

- [OBS 实现](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device.cpp)
- [Microsoft 缓冲协商](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-iambuffernegotiation-suggestallocatorproperties)：须在连接前调用，建议值不保证驱动遵守。
- [Microsoft Live Sources](https://learn.microsoft.com/en-us/windows/win32/directshow/live-sources)：音频与视频来源可能有不同缓冲延迟；相同采集设备不保证两者缓冲相同。

WASAPI 当前请求10ms共享事件缓冲，实际大小由设备决定；1500ms自动同步、2000ms PCM是上限，不是每次固定插入的延迟。自动补偿基于视频呈现时间/PTS和音频到达映射，不能用它消除驱动在交付PCM前的等待。继续保留正常播放的连续重采样，不因未取得的日志猜测而整体重写同步。

## 本次范围

1. 连接音频前按协商格式请求帧对齐的10ms PCM块，驱动不支持/拒绝时明确日志并继续兼容连接。
2. 音频接收器给出一致的10ms allocator要求，记录实际allocator容量；视频allocator不变。
3. 记录实际收到的PCM块时长、回调间隔和数量，与补偿、PCM队列、输出端队列同时输出，区分采集交付慢与软件堆积。
4. 使用产品接收器/COM协商测试、真实WASAPI合成PCM回归和完整构建验证。单次测试低于300秒，不抢占用户设备、不宣称实卡声学验证。

## 验证记录

待追加。本次不发布新Release，不改已发布1.0.0资产，不修改SDK或运行时。

## 本轮实际结果

- 先只修改产品接收器的10ms要求断言，旧实现测试32 PASS / 1 FAIL，contract-before.log，失败点正是旧cbBuffer=4而非1920。它证明合同缺口，不能冒充实卡500ms复现。
- 完整增量构建100步通过：cmd.exe /c out\gpu-dis-build-all.cmd，build-after.log。最初使用out/xxx.cmd的cmd路径被当作命令out拒绝，改为Windows反斜杠后正常；未改变编译器或运行时。
- 使用scripts/run-short-test.ps1包装产品测试，contract-after 38 PASS；覆盖上游接口缺失、请求成功、驱动拒绝后的兼容连接、48k16bit/32bit及11025采样率的帧对齐请求，实际COM内存allocator及PCM输入。mock返回0x80004002/0x80004005/0均如实日志，非物理驱动验证。
- veyra_capture_audio_tests.exe --software-delay，60s外部上限，16 PASS：80/160/400/900ms软件延迟与900ms共同输入偏移，后者没有重复计入补偿；手动/关闭、重锚、断流和队列边界通过。mode off/手动负值及刻意断流中有underrun计数，不宣称所有模式无缺音。
- --jitter，30s外部上限，5秒真实WASAPI合成PCM：additionalResets=0、additionalUnderruns=0、missing=0、p95SkewMs=29.4802，实际inputBlocks=500、inputBlockMs=10、inputIntervalMs=9.8056。视频延迟30/35ms交替；这不是扬声器到屏幕测量。
- --endpoint-loss，30s外部上限，PASS；重连2次，平均软件偏差9.61721ms，无overflow/underrun。
- scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair：23/23 PASS，53.123279秒，logs/delivery/c7ecf48ee7ae44a99cc3eb6f971d61fc/result.json。真实NR Init/Create Feature18 result=0x1、handle非空、seh=0，含NVOF/4K/呈现和导出检查，不是实卡音频验收。
- 当前EXE SHA256 B847BFAA3440548C8494DB5DE90280BAB76609DB5B5DECCFF3ABB0870C6DB89A；patched avcodec SHA256仍0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。

证据统一位于logs/capture-audio-latency-20260913/。修改NativeCaptureSink(.h/.cpp)、CaptureCardSource.cpp、CaptureAudioSession(.h/.cpp)、CaptureColorContractTests.cpp、CaptureAudioTests.cpp及本记录/WORKLOG；不修改实际音频补偿、重采样、呈现/FG调度。没有SDK/runtime修改或新增。仅本地修复，无push或Release资产更新。

## 待反馈者验收

仍缺反馈者日志、使用设置、音频来源以及同源OBS/本程序的声学测量。请求10ms不等于端到端10ms，也不保证每个驱动遵守；实际allocator容量、inputBlockMs/inputIntervalMs与compensationMs/pcmMs/endpointMs/skewMs应一起判断。如小块已生效仍延迟大，下一步按日志区分软件补偿、PCM堆积、输出设备固定延迟及采集音视频PTS差异，不能继续盲目减小同步上限或停声音。反馈者所下载的公开1.0.0尚未包含此修复。

## 反馈者日志复核（2026-09-13）

随后用户提供原版veyra-app.log，SHA256 edf0252bb10494c9e5b4d9d74b653392e2f9ed88294f0f011144004ce2386409，共39227行。仅只读分析，原始日志不进入Git。摘要保存logs/capture-audio-latency-20260913/user-log-analysis.json。

采集1920x1080@60 YUY2、独立音频设备索引4、RTX5090，包含多次增强参数与顺序调整。音频输出48000Hz/1056frames即22ms。12:59:19.892Z至14:07:28.499Z共2040条同步样本：compensationMs全部为0；1733个有效skewMs样本为-536.786至-495.728ms，中位-519.634ms，负值表示声音落后；307个-999为时钟无效占位，不能计作999ms延迟。endpointMs最大22ms。初始至末尾resets从1增至8540，约2次/秒；不能把无ERROR级日志当音频正常。

例：第91行PCM队列0、输出19.875ms，声音仍落后502.336ms；第255行PCM队列500ms；第274行PCM467.875ms/输出22ms/声音落后501.051ms。视频映射减音频到达映射的未截断补偿，中位约-503.672ms，说明不是自动额外延迟把声音故意推迟半秒。大块交付/音频入口时序约半秒落后的解释与缺失缓冲协商高度一致，反复重锚是额外异常症状。

日志无inputBlockMs、inputIntervalMs或capture-audio-buffer字段，属于本次修复前路径；没有每次IMediaSample长度/时间或实际allocator记录，故不能直接证明该驱动确实以500ms块交付，也不能排除音频设备选择或驱动PTS差异。新10ms协商修复针对首要疑点；最终仍需反馈者新版复测。如果小块交付已确认而负约500ms偏差持续，继续查时间戳/设备与重锚原因，不宣称一次修复已经覆盖所有异常。
