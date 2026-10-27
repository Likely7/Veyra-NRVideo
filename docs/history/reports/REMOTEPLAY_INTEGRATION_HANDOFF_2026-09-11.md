# Veyra PS5 Remote Play 集成交接

> 施工已继续并形成可执行本机测试版。**当前结果、Git节点、证据与剩余实机项以 [执行记录最终节](REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md#最终交付状态等待用户-ps5-实机连接) 和 [PS5验收步骤](REMOTEPLAY_PS5_ACCEPTANCE_2026-09-11.md) 为准。** 下文尤其第18节保留修复前事实，不是当前缺陷清单。

> 二次审计后用户授权当前 Agent 继续施工，节点状态改由 [修复执行记录](REMOTEPLAY_REPAIR_EXECUTION_2026-09-11.md) 维护。本文第18节保留修复前证据，不应据旧缺陷表断言新代码仍未修。

日期：2026-09-11  
当前分支：`agent/remoteplay-integration`  
代码基线：`0f78cc29f34365589bcd4757e7017236e3ac9cb1`  
开工存档：`checkpoint/remoteplay-preintegration-2026-09-11`  
Chiaki 固定提交：`0e16950165f06e5c3291537c2eeba6e852be7120`

**2026-09-11 二次代码审计修订：本工作树不能作为可用串流功能交付。** 除原交接列出的缺陷，已真实复现 H.264 配置头导致首帧失败，并确认元数据回调被移植过程删去、音频重启丢弃、尺寸和构建复现问题。先读第 18 节的审计与证据，再按第 17 节执行；旧交接的音频 PTS 修法已经纠正。以下“已通过”仅在各自测试范围内成立。

本文是 Remote Play 后续施工的唯一交接入口。`docs/remoteplay/WORKLOG_CODE01.md` 和用户提供包中的报告是历史阶段记录。`NATIVE_GATE.md` 已更新本机状态；“Windows native 未执行”已经过期，但 native 初始化通过不代表视频回调运行或主程序已接通。

## 1. 最终目标

把 PS5 Remote Play 作为 Veyra 的第四种输入源接入现有产品：

```text
PS5 / Chiaki transport
  -> compressed H.264/H.265 access units + Opus PCM + controller channel
  -> RemotePlaySource
  -> existing EnhanceGraph
  -> existing realtime scheduler
  -> existing VideoPresenter

RemotePlay PCM
  -> dedicated audio owner thread
  -> existing AudioRenderer / WASAPI
```

必须复用现有 `FrameSource -> EnhanceGraph -> FrameSink`，不能复制 `EngineController::run()`、增强图、Presenter、NR/SR/FG 或 reset 逻辑。Remote Play 是实时预览源，不进入视频导出路径。

完成状态的含义是：用户能在 Win32 界面配对或选择已保存的 PS5，连接串流，看到视频，听到连续音频，使用手柄，正常停止/重连，并能在同一条 Veyra 增强链上启用 NR/SR/FG。只有 probe 初始化不算完成。

## 2. 当前结论

用户提供的 `C:\Users\123\Desktop\Veyra_RemotePlay_Code_01` 不是可直接交付的 PS5 功能，而是一套质量尚可的协议桥原型。可以继续用，但必须先修数据正确性，再接主程序。

已经成立的事实：

- 固定的 chiaki-ng 源码能在本机 Windows x64/MSVC 下真实编译并链接。
- `ChiakiBackend` 能调用真实 `chiaki_lib_init()`，不是 stub。
- 回调边界、有界压缩队列、PCM 队列、代次隔离和基础输入校验已有实现；IDR 请求只接到队列，尚未从 source 转发给 backend，不能称恢复链已经跑通。
- `RemotePlaySource.cpp` 已经能与 Veyra 的 FFmpeg、source 和 sink 库编译链接。
- 当前代码没有进入 `EngineController` 或 `AppShell`，所以用户现在仍看不到 Remote Play 入口。
- 没有 PS5 实机连接、PS5 码流解码、Remote Play 音频播放、手柄、增强、OBS 或延迟验证证据。二次审计新增了本地合成 H.264 经真实 FFmpeg 解码的故障复现，不冒充 PS5 实测。

## 3. 已验证证据

### 3.1 离线核心

当前可执行文件：

```text
out/remoteplay/core-windows/veyra_remoteplay_core_tests.exe
```

本次交接前重新运行结果：

```text
TOTAL 67 PASS 67 FAIL 0
```

覆盖：输入校验、Base64 Account ID、序号展开、相对时钟、Annex-B 分类、有界视频/PCM 队列、IDR 恢复、EAGAIN 状态机、输入失焦归零和 SessionInbox 生命周期。它不包含完整 FFmpeg 解码、WASAPI、PS5 或 GPU。

### 3.2 Windows 原生 Chiaki 核心

真实构建目录：

```text
out/remoteplay/native-fresh5
```

实际环境：

```text
Generator: Ninja
Compiler: MSVC 19.44 / Visual Studio 2022 Build Tools
Build type: Release
Chiaki stage: C:/Users/123/Desktop/Veyra DLSS Video Player/out/remoteplay/chiaki-msvc-stage
Clean verification checkout: C:/veyra-deps/chiaki-source
Static dependency prefix: C:/veyra-deps/remoteplay-installed/x64-windows-static
```

构建完成 248/248 个步骤。`Testing/Temporary/LastTest.log` 记录：

```text
rp.native_core_initialization PASS
REAL_CHIAKI_CORE_INITIALIZED upstream_video_callback=1
PS5_CONNECTION_NOT_TESTED VIDEO_DECODE_NOT_TESTED WINDOWS_PLAYER_NOT_TESTED
```

本次交接前直接重跑 `veyra_remoteplay_native_probe.exe`，退出码为 0，输出相同。普通 PowerShell 当前没有把 `ctest.exe` 加入 `PATH`；需要使用 VS Developer PowerShell 或 CMake 自带工具的绝对路径。这不是产品失败。

注意：`upstream_video_callback=1` 是 `native_main.cpp:15` 的固定文字，**不是回调计数**。该 probe 只调用 `initializeChiaki()`、profile preset 和手柄 idle 初始化；没有 start session、注册/触发视频回调、pairing 或输入发送。下一位 Agent 应把文案改成不可能被误解为回调实测成功的标识。

### 3.3 Veyra source 回归

`out/build/x64-release/veyra_source_tests.exe` 本次重跑结果：23 checks，0 failures。它验证现有文件源，没有验证 Remote Play 真码流。

此前主项目已编译出 `veyra_sources.lib` 和 `veyra_engine.lib`。旧的 `veyra.exe` 链接尝试只因正在运行的程序占用目标文件而报 `LNK1104`；下一位 Agent 应使用全新的构建目录，不要关闭用户正在使用的程序。

## 4. 工作树与外部依赖

当前未提交代码改动：

```text
M  CMakeLists.txt
M  include/veyra/pipeline/FramePacket.h
?? docs/remoteplay/
?? include/veyra/remoteplay/
?? include/veyra/source/RemotePlaySource.h
?? scripts/remoteplay/
?? services/remoteplay-probe/
?? src/remoteplay/
?? src/source/RemotePlaySource.cpp
?? tests/remoteplay/
```

本交接还会修改 `docs/WORKLOG.md` 并新增本文。不要 reset、checkout 或重新运行用户包的 `apply.py`；代码已经合入工作树，再跑可能覆盖已经完成的本机修复。

本机依赖位置：

```text
干净 chiaki-ng checkout:
  C:\veyra-deps\chiaki-source

只含已审查 MSVC 兼容补丁的 stage:
  C:\Users\123\Desktop\Veyra DLSS Video Player\out\remoteplay\chiaki-msvc-stage

静态依赖 prefix:
  C:\veyra-deps\remoteplay-installed\x64-windows-static
```

干净 checkout 当前 HEAD 已核对为固定提交，tracked status 为空。不要把上述依赖目录、生成物、凭据、PS5 抓包或日志提交到 Git。

## 5. 当前代码职责

| 模块 | 位置 | 当前职责 | 当前状态 |
|---|---|---|---|
| 类型与校验 | `include/veyra/remoteplay/Types.h`, `src/remoteplay/Validation.cpp` | 主机、Account ID、PIN、profile、PCM/视频数据结构 | 离线通过 |
| Annex-B | `src/remoteplay/AnnexB.cpp` | H.264/H.265 config/picture/IDR 分类 | 合成输入通过；真码流未测 |
| 有界 ingress | `VideoIngress.cpp`, `SessionInbox.cpp` | 代次隔离、积压恢复、IDR 请求、PCM 队列 | 离线通过 |
| Chiaki 协议桥 | `ChiakiBackend.cpp`, `ChiakiRegistration.cpp` | 会话、回调、Opus、配对、控制输入 | 原生初始化通过；PS5 未测 |
| Veyra source 适配 | `RemotePlaySource.cpp` | AU -> FFmpeg AVFrame，PCM -> AudioPcmSource | 可编译；存在下列正确性缺陷 |
| 主引擎 | `EngineController.cpp` | 现有文件/采集/图片增强与呈现 | Remote Play 未接 |
| UI | `apps/veyra/ui/AppShell.cpp` | 现有本地媒体、采集、专业设置 | Remote Play 未接 |
| 凭据存储 | 无 | DPAPI profile | 未实现 |
| 发现/唤醒 | 无 | LAN discovery / wakeup | 未实现 |
| 手柄 | 只有 `InputGate` | 状态门控 | 设备枚举和轮询未实现 |

## 6. 必须先修的正确性问题

这些问题必须在主程序接线之前修。否则实机联调会把基础数据错误伪装成网络、性能或 PS5 问题。下列 `P0.x` 是原交接步骤编号；本次审计的缺陷优先级和复现状态统一见第 18 节，不能把这些编号当作“已修复”。

### 新增首要阻断：H.264 配置头提交失败（审计 R01，P1）

`read()` 先单独 `submitPacket(configBefore)`，当前 FFmpeg H.264 decoder 对仅 SPS/PPS 的包实际返回 `AVERROR_INVALIDDATA`（`-1094995529`，stderr `no frame!`），source 在首个 AU 提交前就返回 Error。审计使用本地生成的合法 1280×720 H.264：两条 ingress 投递成功，`read_status=2 frame=0`；同样的 SPS/PPS 与首个 AU 合并后 `decoded=1`。

需要为 config 与 AU 建立正确的解码输入契约，例如把配置前缀与首个 AU 组成一个合法包，或在经过 H.264/H.265 真解码验证的初始化流程中设置 extradata。不能单纯吞掉所有 INVALIDDATA 当作成功。恢复/更换配置时同样适用。首先添加能够复现当前首帧失败的 source 测试，再修实现。

### P0.1 PCM 尾部被永久丢弃

位置：`src/source/RemotePlaySource.cpp` 的 `pullAudio()`，约 320 行。

当前每次从 `SessionInbox` 取出整个 `PcmBlock`，只复制调用者还需要的帧。如果输出缓冲比块小，未复制的块尾直接丢失。必须给 `RemotePlaySource` 增加当前 PCM block 和帧偏移 cursor，例如：

```cpp
std::optional<remoteplay::PcmBlock> audioBlock_;
std::size_t audioOffsetFrames_ = 0;
```

一个 block 必须被完整消费后才能弹下一个。discontinuity 到来时不要在一次 `pull()` 中跨越两个时间段拼接；返回当前段，让 AudioRenderer 下一次拉取并重新锚定或显式 reset。

### P0.2 音频 PTS 和视频 PTS 零点不一致

位置：`pullAudio()` 约 329 行。

视频 `RemotePlayClock::video()` 输出相对 common origin 的 100ns PTS；音频当前把 `commonOrigin100ns` 绝对值再次加回，得到 steady-clock 绝对时间。两条时间线不能比较。

**撤回旧交接的逐块 `audioPts(firstSample, rate, arrival100ns)` 建议。** 这个函数的第三参数是固定的 `audioStart`；每块都传当前 arrival，会同时加上到达时间增长与累计样本增长，重复计时。实际离线演示中，两块相隔 10ms、样本数增加 480，计算结果却增加 20ms。

每个连续音频段固定一个起始锚点：首块到达的共同相对时间对应首样本序号，后续只加 `(block.firstSample - firstBlockSample + cursor) / rate`。arrival 仅用于首锚、网络断点和诊断，不逐块追逐到达抖动。由于到达时间不是远端音视频采样时间，仍需实机校准/估计固定相对偏移；“共用 steady-clock origin”本身不证明物理同步。首块采样序号可能非零，恢复时要同时保存段起始序号。

### P0.3 decoder 重建会覆盖 `decoderFrame_`

位置：`openDecoder()` 约 135-174 行。

编码或尺寸变化时先释放 `codecContext_`，随后直接把新 `av_frame_alloc()` 赋给旧 `decoderFrame_`。旧 frame 需要先 `av_frame_free(&decoderFrame_)`。失败路径也要保持对象可再次连接。

### P0.4 首帧状态永远没有进入 Streaming

位置：`read()` 约 300-311 行。

第一张 AVFrame 成功交付后必须调用：

```cpp
inbox_->decodedFrameReady(sample.generation);
```

只在对应 generation 的第一张真实解码帧上调用。`waitingForFirstFrame_` 应成为这个一次性门控，或者删除并用明确状态替代。不能在收到压缩包时提前标记 Streaming。

### P0.5 IDR 请求没有送回 PS5

`VideoIngress` 会在丢包、积压或配置变化时设置 IDR 请求，但 `RemotePlaySource` 从不消费 `inbox_->takeIdrRequest(now)`。

会话 owner 线程必须周期性检查并调用 `backend_.requestIdr()`。记录固定操作名和错误码，不记录未过滤的 Chiaki 原始文本。调用必须留在创建 `ChiakiBackend` 的同一线程。

### P0.6 解码输出 PTS 错配

位置：`submitPacket()` / `drainDecoder()`，约 206-259 行。

当前把 `sourceIndex` 写入 `AVPacket::pts`，但 `drainDecoder()` 无条件使用“当前输入包”的 `sourcePacket`。有 B 帧、decoder delay、EAGAIN drain 或单包多帧时，输出可能对应更早的包。

应维护严格有界的输入 timestamp/packet 元数据表，以 `AVFrame::best_effort_timestamp` / `frame->pts` 按真实解码契约查回对应输入 packet，并设置一致的 `pkt_timebase`。没有匹配时显式失败或按已定义的估计策略处理、标记 LocalEstimated，不能静默套用当前包或把重建值称为真实源 PTS。多帧输出的时间必须有可验证来源；同 timestamp 不能直接复制成几帧相同 PTS。flush、config change、stop、reconnect 时清空 timestamp 历史。

建议不要只用 `std::map<int64_t, FramePacket>`：同 timestamp 可能有多帧。用带上限的 deque entry，包含 timestamp、基础 packet 和已发出子帧数；上限与 decoder reorder 深度绑定，异常超限触发 reset/IDR。

### P0.7 Opus 采样率契约过宽

位置：`src/remoteplay/ChiakiBackend.cpp` 的 `opusSettings()`，约 94 行。

现有 WASAPI 路径固定 48 kHz，backend 却接受 8 kHz 到 192 kHz。当前最小安全实现只接受 1/2 声道和 48000 Hz；其他 rate 明确失败。后续若确有非 48 kHz 设备证据，再增加 libswresample，不要假定 AudioRenderer 会自动重采样。

### P0.8 stop 和失败状态需要完整覆盖

当前正确顺序已经改为：

```text
inbox.invalidate
  -> backend.stop / chiaki_session_stop + join + fini
  -> inbox.finishStop
```

这只是正常成功路径的顺序，**当前失败路径不正确**：`close()` 丢弃 `backend_.stop()` 返回值后无条件 finishStop 并清空 token；wrong-owner/join 失败时旧 session 可能仍存在。必须确认 stop/join/fini 的实际结果，失败留在明确故障状态，不得宣称 Idle 或开启新会话。新增音频、手柄和 pairing worker 后，先停止输入和音频 owner，再销毁 source；callback join 前不得释放 inbox、decoder 或 credential storage。成功 close 与 connect 失败后还应清零 `request_.credentials`，不能仅依赖最终析构。

## 7. CMake 和构建问题

根 `CMakeLists.txt` 当前无条件把 `RemotePlaySource.cpp` 和 remoteplay core 编进 `veyra_sources`，但 `ChiakiBackend.cpp` 只在三个未正式声明的变量都成立时加入：

```cmake
VEYRA_ENABLE_REMOTEPLAY
VEYRA_REMOTEPLAY_INCLUDE_DIR
VEYRA_REMOTEPLAY_LIBRARY
```

这不是可维护的产品配置。静态库暂时可能因没人引用 `RemotePlaySource` 而隐藏悬空符号；一旦 UI 真正引用就会在默认配置链接失败。

必须显式声明：

```cmake
option(VEYRA_ENABLE_REMOTEPLAY "Enable PS5 Remote Play through pinned chiaki-ng" OFF)
set(VEYRA_REMOTEPLAY_CHIAKI_SOURCE_DIR "" CACHE PATH "...")
set(VEYRA_REMOTEPLAY_CHIAKI_VERIFY_DIR "" CACHE PATH "...")
```

启用时提取/完善 `services/remoteplay-probe/NativeChiaki.cmake` 的真实 `add_subdirectory()` 接法，直接链接 `chiaki-lib`；不要把 probe 的完整 target 定义原样复制到主工程。现有 provenance 检查还需修复：clean verify checkout 的 HEAD 不能证明另一个 stage 的内容，限定改动文件名与 reverse-apply 检查也不能证明文件中没有额外改动。必须核对 stage HEAD、全部差异内容、index/worktree 和递归子模块。不要发明只有 include/lib 路径、无法追溯 commit 的旁路。关闭时应排除 backend、`RemotePlaySource`、Remote Play UI 和相应测试，或者提供完整的编译期 disabled 实现。

`scripts/build.ps1` 还没有传 Remote Play 参数。应检测上述三个本机目录，在明确启用时追加 CMake 参数。不要把机器路径写进 `CMakePresets.json`。本机 Ninja/MSVC 曾受代码页 936 的 `/showIncludes` 影响并生成缺失的 `rules.ninja`；构建批处理先执行 `chcp 65001 >nul`，并使用 Windows本地 TEMP。

## 8. 主程序接线方案

### 8.1 请求模型

`EngineController::open(HWND, path, options)` 现在靠字符串前缀识别采集卡，不适合塞入密钥。增加独立入口：

```cpp
void openRemotePlay(
    HWND video,
    source::RemotePlayConnectDesc request,
    PlayerOptions options);
```

`PairingCredentials` 是 move-only，现有 `post(std::function<void()>)` 要求闭包可复制。不要为了方便把凭据类型改成可复制。可把请求放入 `std::shared_ptr<RemotePlayConnectDesc>`，闭包复制 shared_ptr，worker 取得后立刻移动到只属于本次会话的对象，并在关闭时清零。更彻底的方案是让任务队列支持 move-only callable，但不要在本次顺手重写整个 dispatcher。

### 8.2 统一 run 路径

不要新增一份 `runRemotePlay()` 复制约千行播放循环。将 source 选择收敛为内部 `OpenRequest`/variant：

```text
Local file/image request
Capture request
RemotePlay request
```

完成 source 创建和 open/connect 后，仍走同一个 extent、ResolutionPlan、EnhanceGraph、LiveGpuScheduler、FrameFlowWindow 和 VideoPresenter。Remote Play 需要与 physical capture 一样走实时 admission/latest-deadline 策略，但 source identity 必须保持 `SourceKind::RemotePlay`，不能把它伪装成 CaptureCard。

`gd.rgbInput`、`gd.yuy2Input` 必须根据实际解码 AVFrame 格式设置。源码已确认现有 `EnhanceGraph.cpp:818` 支持 YUV420P/YUVJ420P/NV12 的上传路径，不要另加一套重复颜色转换。图在 `:663` 会通过 `resolveFrameColor` 读取明确 VUI，能缓解 source 未解析颜色的问题；但 source 当前的 720p BT.601 fallback 错误，VUI 缺省时会透传到图。需在 source 输出统一正确的 metadata、assumed 标记，并验证未知格式/不支持 HDR 的拒绝行为。

新增独立解码 owner 是实时集成的必要环节：Chiaki 网络回调只投递，CPU owner 按压缩参考顺序持续解码，再通过现有 `LatestMailbox<owned AVFrame>` 或同等有界候选把解码结果交给唯一 GPU owner。保持同一个引擎/graph/presenter，不等于把协议、解码和慢 NR 全串在一个线程。当前 source 的 FFmpeg 工作就在 `read()` 内；若直接由每次增强后调用一次 read，4 个 AU/100ms 队列会在 GPU 欠速时反复溢出并等待 IDR，不能靠扩大队列解决。

### 8.3 实时调度和 reset

Remote Play 适用现有实时预览规则：持续欠速时按真实 PTS 均匀跳过过期增强机会，音频保持一倍速，不能堆积无界延迟。网络丢包、IDR 恢复、codec config、分辨率变化、重连和 decoder flush 必须原子 reset SR/NR/FG/NVOF 历史。

至少增加这些 reset cause 的日志：

```text
RemotePlayOpen
RemotePlayPacketGap
RemotePlayDecoderReset
RemotePlayReconnect
RemotePlayResolutionChange
```

如果现有 enum 不允许扩展，映射到已有准确语义并在日志 detail 中标出 remote source；不要写成 Seek。

## 9. Remote Play 音频

Remote Play PCM 已由 Chiaki 的 Opus decoder callback 产出，不能走文件 `AudioPipeline::open(path)`，也不应该套采集卡 DirectShow 音频类。

增加 Remote Play 专用音频 owner：

```text
wait until first real PCM block is available
  -> AudioRenderer::start()
  -> AudioRenderer::startAnchored(RemotePlayAudioSource)
  -> event-driven AudioRenderer::pumpOnce(...)
```

具体实现可作为 `RemotePlayAudioSession`，持有 `RemotePlayAudioSource`、`AudioRenderer`、stop token 和线程。WASAPI 对象只在音频 owner 线程创建、pump、reset 和销毁。引擎线程只发布音量、暂停和诊断状态。

音频以共同相对 PTS 为媒体时钟。视频处理变慢时跳过过期视频机会，不能停 PCM、丢 PCM 或频繁重锚来追逐 5-10 ms 抖动。真正的网络 discontinuity、队列 overflow 或 reconnect 才允许一次受控 reset/re-anchor。把 underrun、buffered ms、音频 PTS、视频 PTS、A/V delta 和 reset 次数放进现有实时状态。

生命周期顺序：停止 Remote Play 音频线程并 shutdown endpoint，然后停止/加入 Chiaki session，最后销毁 source。不能让音频线程在 source 析构后继续调用 `pullAudio()`。

## 10. 配对、凭据和 UI

### 10.1 DPAPI profile

实现独立的 `RemotePlayProfileStore`：

- profile 元数据可保存 nickname、host、MAC、最后使用的 video profile。
- Account ID、registration key、session key 使用 Windows DPAPI `CryptProtectData`，绑定当前用户。
- 磁盘文件写到 `runtime::localDataDirectory()` 下的 Remote Play 子目录。
- 使用原子临时文件 + rename，设置合理 ACL；解析长度严格有界。
- 解密后的 buffer 和临时 `PairingCredentials` 用后立即清零。
- 禁止写入 INI、命令行、环境变量、日志、崩溃诊断或 UI 文本。

### 10.2 配对流程

`pairLocalPs5()` 是同步所有权操作，最长可到 30 秒，必须在独立 setup worker 执行。UI 提供取消，退出窗口时 request stop 并等待 worker 清理。不能在 HWND 线程调用。

首版页面至少包含：

- 手动 IP/主机名。
- PSN Account ID，支持当前校验器接受的十进制或 Base64。
- PS5 显示的 8 位配对码，保留前导零。
- 配对、取消。
- 已保存主机列表：连接、删除、重新配对。
- 720p/1080p、30/60、H.264/H.265 和 bitrate 选择。
- 连接状态：正在连接、等待首帧、串流中、需要登录 PIN、失败码。

Remote Play 应作为专业模式左侧 rail 的独立入口，不要塞进“采集卡”弹窗。日常模式可以在已有空页面增加“PS5 串流”命令，但精细连接参数放专业模式。

### 10.3 发现和唤醒

先完成手动 IP 的真实连接，再接 discovery/wakeup。复用固定 chiaki-ng commit 的 discovery 和 wakeup API，不抄 chiaki-ng GUI/Qt 页面。发现结果是易变网络状态，不能直接覆盖持久 profile。唤醒失败不应删除配对凭据。

## 11. 手柄

`InputGate` 只做 generation、焦点和 stale timeout 门控，不负责发现设备。需要新增设备服务，把 Windows 手柄状态映射到 `remoteplay::ControllerState`，在 session owner 线程调用 `backend_.submitController()`。

最低行为：

- 有焦点时按固定频率采样并去重发送。
- 窗口失焦、设备拔出、停止、重连时立即发送一次中立状态。
- 输入线程不能等待 GPU 或网络 callback。
- 连接代次变化时旧输入不能进入新 session。
- 先覆盖常用按键、双摇杆、L2/R2、方向键、Options/Create、PS、触摸板点击。

可以选 SDL 或 Windows 原生 GameInput/XInput，但只能引入一套明确依赖。若使用 SDL，记录版本、许可证、构建和 Release 文件；不要把 chiaki-ng GUI 整体编进 Veyra。

## 12. 推荐施工顺序

每一步通过后再进入下一步，单次测试不超过 300 秒。

1. **源层正确性**：首先修 H.264 config/AU 首帧失败；然后按第 18 节修 PCM cursor/共同音频锚点、元数据与解码 PTS、尺寸/颜色、IDR/恢复、首帧状态、48 kHz/音频段重启和清理失败。
2. **源层测试**：给上述每个缺陷写独立回归；增加真实 FFmpeg config + AU 解码 fixture，验证输出帧数、PTS、flush 和尺寸变化。
3. **生产 CMake**：显式 option，固定上游验证，主项目链接真实 `chiaki-lib`，更新 `scripts/build.ps1`，Remote Play OFF/ON 两种全新构建都通过。
4. **引擎接线**：新增安全的 remote open request，复用同一 run loop、增强图、实时调度、reset 和诊断。
5. **音频**：专用 owner thread + AudioRenderer，共同 PTS，停止/重连完整生命周期。
6. **DPAPI 和配对 UI**：后台配对、取消、保存主机、连接/删除/重配。
7. **发现、唤醒和手柄**：手动 IP 基线通过后再加；后台 owner 须能接收 login PIN、IDR、控制输入且不能等待 GPU。当前 source 还没有公开这些控制入口。
8. **离线总回归**：Remote Play tests、source tests、engine tests、UI smoke、`scripts/gates/delivery.ps1`。
9. **PS5 实机验收**：视频、音频、手柄、断线恢复、暂停/停止、NR/SR/FG、OBS、窗口调整和延迟。
10. **许可证和提交**：补 notices、exact source/build recipe，扫描 Git 不含凭据/依赖/二进制；经用户明确授权后才能 push 或 Release。

不要先写完整 UI 再回头修 PTS，也不要先接硬件解码。首个实机基线使用当前软件解码，先证明协议、时序和生命周期正确；基线稳定后再评估 D3D12VA 零拷贝是否值得做。

## 13. 构建命令

### 13.1 离线 core

在已配置 MSVC 的 x64 Developer PowerShell：

```powershell
powershell.exe -NoProfile -File scripts/remoteplay/test-core.ps1 `
  -Root . `
  -BuildDirectory out/remoteplay/core-handoff `
  -Configuration Release
```

### 13.2 原生 Chiaki probe

若现有 stage 已在，不要删除或覆盖，脚本会验证补丁：

```powershell
powershell.exe -NoProfile -File scripts/remoteplay/build-native.ps1 `
  -Root . `
  -ChiakiCheckout C:/veyra-deps/chiaki-source `
  -StageDirectory "C:/Users/123/Desktop/Veyra DLSS Video Player/out/remoteplay/chiaki-msvc-stage" `
  -BuildDirectory out/remoteplay/native-handoff `
  -PrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static `
  -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe `
  -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe
```

前版漏掉后两项，在相同机器全新配置目录真实复现 `Could not find protoc`（exit 1）；不能依赖 `native-fresh5` 的旧 cache。以上路径来自成功目录的 CMakeCache，换机需要定位实际工具。二次审计仅复现旧命令的失败，没有重新执行这份修订命令的完整 248 步构建。

### 13.3 主项目

用户可能仍在运行旧 `out/build/x64-release/veyra.exe`。使用新目录：

```powershell
powershell.exe -NoProfile -File scripts/build.ps1 `
  -Root "C:/Users/123/Desktop/Veyra DLSS Video Player" `
  -Preset x64-release `
  -BuildDirectory "C:/Users/123/Desktop/Veyra DLSS Video Player/out/build/remoteplay-main-fresh"
```

完成生产 CMake 接线后，命令必须在 Remote Play ON 和 OFF 两种模式分别执行。ON 模式的具体参数应由更新后的 `scripts/build.ps1` 生成，不要手写不可审计的 `.lib` 旁路。

## 14. 验收矩阵

| 项目 | 当前 | 完成标准 |
|---|---|---|
| Offline core | PASS 67/67 | 新回归加入后仍全绿 |
| Native Chiaki init | PASS | 固定 commit、真实库、干净 source 复现 |
| Production OFF build | 未形成稳定配置 | 全新目录构建和原有 gate 通过 |
| Production ON build | 未完成 | `veyra.exe` 链接真实 chiaki-lib |
| FFmpeg 合成真 AU 解码 | 审计失败：split H.264 首帧、重排 PTS | 修复后 H.264/H.265 fixture 帧数和 PTS 正确；PS5 输入单独验收 |
| Pairing | 未执行 | 后台配对、取消、DPAPI 保存成功 |
| PS5 video | 未执行 | 连续画面、真实分辨率/帧率、断线可恢复 |
| PS5 audio | 未执行 | 48 kHz 连续、无 PCM 尾丢失、A/V 维持同步 |
| Controller | 未执行 | 常用输入、失焦/拔插归零、无 stuck input |
| EnhanceGraph | 未执行 | NR/SR/FG 复用同图且 reset 正确 |
| OBS/window/fullscreen | 未执行 | Windows Graphics Capture 可见，调整窗口稳定 |
| Latency | 未执行 | 分别测回调后排队、解码、增强、呈现及软件总延迟；PS5渲染/编码/网络与屏幕扫描无证据时明确未测，不能把回调时刻冒充源端时间 |
| License/package | 未完成 | AGPL 对应源码、notices、依赖清单和包扫描完整 |

## 15. 许可证和安全边界

chiaki-ng 固定提交的许可证是 `AGPL-3.0-only`，附 OpenSSL exception。Veyra 当前根许可证是 GPLv3。把 Chiaki 链接进发布的 Veyra 组合程序时，不能只保留当前 GPL 文件并声称已经满足要求；发布物需要履行适用于组合程序的 AGPL 条款，提供用于构建该二进制的完整对应源码、Veyra 修改、Chiaki 固定源码/补丁、构建脚本和许可证/例外文本。发布前应把准确归因加入 `THIRD_PARTY_NOTICES.md`，并由项目维护者确认整个发行方式。

允许参考或调用 chiaki-ng 的协议库，不等于可以机械复制它的 Qt GUI。若复制任何上游实现，保留版权、来源路径和修改记录。优先调用 upstream `chiaki-lib` API，并独立实现 Veyra UI/engine adapter。

严禁：

- 提交 `C:\veyra-deps`、stage、静态依赖、构建目录或用户提供包中的缓存。
- 提交 Account ID、registration key、session key、配对码、PS5 MAC、抓包或含密钥日志。
- 直接转发 Chiaki 原始日志或 hexdump；当前 backend 故意只计 warning/error。
- 把 NVIDIA SDK、runtime、模型或其他本机二进制混入 Remote Play 源码提交。
- 未经用户明确授权 push、创建 Release 或上传任何二进制。

## 16. 对抗式检查清单

下一位 Agent 每次声称完成前都要反问并给证据：

- 初始化通过是否被误说成 PS5 已连接？
- 收到压缩包是否被误说成解码帧已显示？
- `SourceReadStatus::Frame` 是否真的带有效 AVFrame、真实 PTS 和正确 pixel format？
- 处理欠速时是否在丢过期视频机会，而不是堆积延迟、停音频或丢 PCM？
- 断线/重连时旧 callback、音频和手柄是否可能进入新 generation？
- UI 是否在等待 30 秒同步 pairing？
- 运行目录是否因 DLL/依赖搜索顺序加载了未审计文件？
- Git diff 是否包含凭据、上游 checkout、SDK、DLL、LIB、PDB、日志或抓帧？
- AGPL 对应源码是否与发布二进制完全一致？

任何一项没有证据就保持 `NOT_RUN` 或 `INCOMPLETE`，不得用“应该可用”代替。

## 17. 下一步唯一任务

只做第 6、18 节的源层正确性修复和对应测试。先写出当前会失败的真实 H.264 config/AU 测试，再修首帧；不要先接 UI。完成门槛：

1. 配置与真实 AU 可解码，H.264/H.265 及更新配置均有独立证据。
2. PCM 任意分块拉取不丢样本；固定锚点加样本偏移，连续段、溢出和重启不混接。
3. 解码实际尺寸/颜色与 source/graph 契约一致；重建无 frame 泄漏。
4. 第一张真实解码帧进入 Streaming；IDR 确实转发，解码可恢复错误不直接终止整个播放器。
5. decoder delay/重排不会配错 PTS，真实帧号完整展开；没有源时间则标记本机估计。
6. 48 kHz 契约和同格式音频重启正确；停止失败不得冒充已停止，凭据及时清零。
7. core、新增 source tests、既有 23 项文件 source 回归通过；故障复现日志不能作为成功测试计数。

完成后把命令、退出码、日志路径和仍未执行的硬件项追加到 `docs/WORKLOG.md`，再进入生产 CMake 接线。

## 18. 2026-09-11 二次代码审计（用户要求交给其他 Agent 修）

### 18.1 审查范围、代码来源与结论

这轮审查从用户要求移植 Remote Play 的存档 `0f78cc29` 起覆盖全部工作树增量：根 CMake、`FramePacket.h`、全部 Remote Play header/cpp、`RemotePlaySource`、独立 probe、67 项 core tests、构建脚本和 MSVC patch，并核对用户 Code 01 原包、固定 Chiaki 上游与现有 EnhanceGraph/AudioRenderer 接口。是本 Agent 的重新审查，没有第二位 Reviewer，也不能根据截图确认平台路由到过哪个模型。

按 UTF-8 文本统一换行后比较，原包的 core headers、Validation/Timeline/Ingress/Registration 和 CoreTests 没有实质改写；移植中实质改写了 `ChiakiBackend.cpp`、native 构建/probe，新增 `RemotePlaySource`、根 CMake 接线、SourceKind 和 MSVC patch。逐字节初比把 CRLF 差异也计入 changed，因此以 `origin-comparison-normalized.json` 为准。原包 `prepare_chiaki.py`、`chiaki_metadata_patch.json` 和 `PatchToolsTests.py` 没有进入当前工作树，文档仍引用它们是错误。

**结论：基础模块可以保留；不能把这一批代码直接接 UI 后交付。** 问题集中在真实数据适配层、原包元数据契约被移除以及文档/测试覆盖误读。下面区分实际失败、静态确认、接线后才触发的风险；不把尚未实现的 UI/手柄说成“写坏了”。

### 18.2 缺陷清单与修复要求

| 编号 | 优先级 | 位置（审计时行号） | 确认的问题与触发 | 下一位 Agent 的动作 |
| --- | --- | --- | --- | --- |
| R01 | P1 | `RemotePlaySource.cpp:296` | 配置头独立 send 导致合法 H.264 首帧 Error；真实 FFmpeg 已复现 | 明确 config/AU 输入契约，首帧/配置更新均写真解码回归；见第 6 节 |
| R02 | P1 | `RemotePlaySource.cpp:320–346` | PCM block 尾部永久丢弃；480 帧分两次拉取只得到 100 帧 | 保存 block/cursor，任何合法拉取粒度总样本一致 |
| R03 | P1 | `RemotePlaySource.cpp:329`、`Timeline.cpp:44` | source 输出绝对 PTS；旧交接的“当前 arrival＋累计样本”方案又会双重计时 | 固定音频段锚点＋样本偏移；处理首样本非零、断点，不混拼时间段 |
| R04 | P1 | `RemotePlaySource.cpp:206–227` | 解码输出一律套当前输入 packet；B 帧样本 10 张输出全部错配 PTS；多帧注释与实际相同 PTS 也不一致 | 有界元数据匹配和真实 send/receive adapter；不能拿独立 FakeCodec 测试代替 source 测试 |
| R05 | P1 | `ChiakiBackend.cpp:77–91,152` | 原包 metadata callback 被替换为普通回调，wireFrameIndex 永远缺失，width/height 用请求值冒充实际值 | 恢复经过审查的元数据通道，或实现并证明等价的受限方案；不要整包 apply 覆盖修订 |
| R06 | P1 | `RemotePlaySource.cpp:282,286,219` | decoderReady 后不再调用 openDecoder；reset 只 flush。实际尺寸变化未更新 SourceInfo/Resize/graph 契约；观测 info.width=1920 而 AVFrame.width=1280 | 首帧按实际帧建立图，配置/尺寸改变发原子重建；先拒绝不匹配，防 YUV 上传使用错误 buffer/stride |
| R07 | P1 | `RemotePlaySource.cpp:275–302` | 没有消费 takeIdrRequest；可恢复 decode error 虽 requireKeyframe 却返回终止性 Error | owner 周期转发 IDR，区分恢复中的 Waiting 与致命错误；真实错误码、节流、取消/恢复超时需可观测 |
| R08 | P1 | `ChiakiBackend.cpp:94–97`、`VideoIngress.cpp:111–112` | 同格式 Opus settings 重来会把 sampleIndex 归零，AudioIngress 的 nextSample 仍为旧值；标记 discontinuity 也被判乱序拒绝，静音直到计数追上 | 使用明确音频段/代次重启或连续计数契约；不能仅凭任意乱序块带 discontinuity 就接受旧数据 |
| R09 | P1 | `RemotePlaySource.cpp:353–358,89–95` | stop 返回值丢弃，未确认 join 就标 Idle 并丢 token；后续重连失败，可能隔离泄漏旧 session | 成功和失败清理分别建状态机；wrong-owner/join/启动失败注入测试，失败不重新连接 |
| R10 | P1（接线门槛） | 根 `CMakeLists.txt:412–441` | 默认无条件编入 source，却条件性定义其 backend；没人引用时静态库能藏住未解析符号；ON 也没有正式选项/依赖闭环 | 生产 OFF/ON 全新构建，ON 可执行文件真实引用/链接 source，完整依赖 target 和构建参数 |
| R11 | P2 | `RemotePlaySource.cpp:168` | decoder 真正重建时覆写旧 AVFrame 指针，未 free；当前 read 的重建缺陷还把它部分掩盖 | RAII/先释放；在修 R06 后加重复重建/分配失败回归 |
| R12 | P2 | `RemotePlaySource.cpp:262–311` | 成功交付真实帧仍不调用 decodedFrameReady；观测 Frame 返回、状态仍 WaitingFirstFrame | 两处交付路径统一更新状态，generation 一致，失败/恢复不能复活旧帧 |
| R13 | P2 | `RemotePlaySource.cpp:110,219` | 720p fallback 错设 BT.601；source 不解析 VUI。明确 VUI 会被现有图再次解析缓解，缺省时仍错 | HD SDR fallback BT.709；复用颜色解析并保留 assumed；明确 VUI/缺省 VUI 各测，勿重复色彩转换 |
| R14 | P2 | `RemotePlaySource.cpp:292–294`、`VideoIngress.cpp:59` | queue 的 16 位展开结果未向 source 传递；source 直接用原始 16 位数，65535→0 后 PTS unknown | 将扩展序号传到 source 并统一使用；当前普通 callback 无 wire index，此项是恢复 R05 后必然暴露的潜伏问题，非已证明当前 PS5 每18分钟卡死 |
| R15 | P2 | `ChiakiBackend.cpp:94–108`、`RemotePlayAudioSource::pull` | Opus 接受多个 rate，而 AudioRenderer/尾 PTS 写死 48000；有效24k等会以48k解释 | 当前范围显式要求48k且传出失败；或加入真实测试的重采样，不静默按48k播放 |
| R16 | P2 | `RemotePlaySource.cpp:72–77,348–364` | 手工复制凭据到 request_，成功 close 和部分连接失败不清除，直到下次 connect/最终析构 | 限制凭据所有者和存活时间，及时清零临时/会话副本；当前未发现真实凭据被提交 |
| R17 | P2 | `NativeChiaki.cmake:10–32`、`build-native.ps1:43–48` | stage 验证只看文件名/补丁可反向应用，不核对完整内容；直接 CMake 未核对 stage HEAD、index/子模块闭环 | 收紧源码构建可追溯性，不是恢复 NVIDIA DLL 加载哈希锁；本次当前 stage 差异全文与审查 patch 一致，没有证据表明已混入异常改动 |
| R18 | P2 | 第13节旧命令、`NATIVE_GATE.md`、`SOURCES_CODE01.md`、`dependency-lock.json` | 旧命令缺 protoc（已复现配置失败），文档引用不存在 metadata 脚本；probe 固定输出容易误读为回调验证 | 本轮已修交接/文档入口；后续改 native probe 文案、构建依赖发现和相应测试/机器可读状态 |

R02/R03/R04/R06/R07/R11/R12/R14/R16 主要位于移植时新增的 source；R05 是对原包 backend 的实质退化。R08/R15 的基础行为已存在原包，移植者仍需负责在接 AudioRenderer 前修正，不能仅以“另一个 AI 写的”放行。

R06 必须在接 graph 前解决：现有 YUV 分支会按 graph 的 `srcW_/srcH_` 分配/采样，而 source 可能返回另一尺寸的 AVFrame。这里只复现尺寸契约错配，未运行 GPU 越界测试、未声称已发生崩溃。

R05 的普通回调只带数据、frames_lost 和 reference_recovered，不带真实帧号与实际 profile。当前 fallbackSourceIndex 只按被消费 AU 自增，不是远端帧号；FramePacket.sequence 也只是已解码输出编号。不能把本机名义 cadence PTS 当远端原生 PTS 或端到端延迟依据。`TimestampProvenance::LocalEstimated` 在时钟类型中存在，但 source 未把这个来源信息传到产品诊断，后续必须保留。

### 18.3 未完成的架构工作，不能与已修缺陷混为一谈

- 生产引擎和 UI 仍没有 Remote Play 入口；本轮没有改现有文件/采集的播放循环。未发现这批移植修改了已经完成的欠速音频策略。
- 必须把压缩流连续解码与 GPU 慢任务解耦，复用已有参考帧 mailbox/有限候选。`PacketPump` 和 `LatestMailbox` 都已有单测，但实际 source 没用它们，`ready_` 也没有显式容量/时间预算。不能据此声称正常 FFmpeg 会无限产帧；应建立可测试的上界和取消点。
- Remote Play 音频 owner、DPAPI、异步配对/取消、发现/唤醒、手柄轮询，以及公开的 login PIN/控制消息入口都未接。`SessionInbox` 不替代 owner 线程，`InputGate` 不替代设备服务。
- 保留完整 source 错误原因、帧 provenance、队列/时钟统计；超时、源断流、GPU 欠速需分别报告。回调后计时不能命名为 PS5→屏幕总延迟。
- MSVC VLA patch 的 Windows 分配/释放、错误分支已静态审阅；没有发现必须直接撤销的证据。其修改包含大小检查和错误处理，不能仅以“编译兼容”宣称所有网络行为等价；RUDP/洞穿/配对/OOM 分支均没有实机执行。当前任务先做 LAN，不能把编译了这些文件写成公网串流可用。

### 18.4 本轮实际命令、结果与可复现证据

证据目录：`logs/remoteplay-audit-20260911/`。临时观察程序和合成样本：`out/remoteplay/audit-20260911/`（全部 gitignored）。程序只把 RemotePlaySource 的访问控制在隔离 shadow header 中改为 public 以注入 inbox；**实际编译的是当前未改动的 `src/source/RemotePlaySource.cpp`**，链接本机真实 FFmpeg DLL、Veyra base 和既有真实 Chiaki/core 静态库。没有伪造 backend 初始化/连接成功，没有网络连接、WASAPI 或 GPU 操作。

```powershell
# 创建诊断编译参数和两个本地合成 H.264 样本；每个 ffmpeg 进程有60秒上限
python out/remoteplay/audit-20260911/prepare.py
# MSVC x64 /MT；build.cmd 内载入 vcvars64、codepage65001
cmd.exe /d /c out\remoteplay\audit-20260911\build.cmd
# 给本次诊断进程定位现有FFmpeg DLL，无文件复制/替换
$env:PATH='C:\veyra-deps\installed\x64-windows\bin;'+$env:PATH
& scripts/run-short-test.ps1 `
  -Exe out/remoteplay/audit-20260911/audit.exe `
  -Arguments @('out/remoteplay/audit-20260911/single','out/remoteplay/audit-20260911/reorder.h264') `
  -TimeoutSeconds 30 -LogPrefix logs/remoteplay-audit-20260911/observations2
```

观察程序 exit 0 仅代表收集完结果，**不是产品 PASS**。`build.log/build2.log` 记录两次诊断编译；`observations.stdout.log` 是初次输出，`observations2.stdout.log` 补充颜色解析和真实帧状态对照；两次均保留。代表性结果：

```text
PCM supplied=480 pulled=100 first_pts_ms=1.36365e+08 expected_relative_ms=30
HANDOFF_AUDIO_RECIPE first=300000 next=500000 expected_step=100000
AUDIO_SAME_FORMAT_RESTART accepted=0 rejected=1
WIRE_WRAP before_known=1 after_known=0
avcodec_send_packet failed code=-1094995529
SPLIT_H264 config_accepted=1 au_accepted=1 read_status=2 frame=0 session_state=2
COMBINED_H264 ok=1 decoded=1 ... resolved_matrix=2 info_width=1920 decoded_width=1280
REAL_FRAME_STATE read_status=0 frame=1 session_state=2 streaming_enum=3
REORDER input=12 output_before_eof=10 wrong_pts=10
```

这里 `REORDER` 故意没有发送 EOF drain，所以没有输出的两帧不是额外“丢尾”结论；错误是已经输出的 10 帧都套了另一个 packet 的 PTS。颜色枚举的 AV 与 Veyra 数字不同，必须按类型解释；`resolved_matrix=2` 在 Veyra 表示 BT.709，证明图能读取明确 VUI，不能只看 raw 数值不同判颜色错误。

既有测试重跑（均通过 `scripts/run-short-test.ps1`、每进程30秒上限）：

| Exe | LogPrefix | 实际结果 | 范围 |
| --- | --- | --- | --- |
| `out/remoteplay/core-windows/veyra_remoteplay_core_tests.exe` | `logs/remoteplay-audit-20260911/core` | exit0，67/67 | 基础模块，未包含真实 source adapter |
| `out/remoteplay/native-fresh5/veyra_remoteplay_native_probe.exe` | `logs/remoteplay-audit-20260911/native` | exit0，初始化成功 | 不执行配对/视频callback/session |
| `out/build/x64-release/veyra_source_tests.exe` | `logs/remoteplay-audit-20260911/source` | exit0，23 checks/0 failures | 既有文件source，未覆盖Remote Play |

构建复现：执行隔离 `out/remoteplay/audit-20260911/configure.cmd`（vcvars64＋UTF-8＋Windows TEMP，从第13节旧参数组创建全新 native-doc-repro 目录），CMake exit1，`Could not find protoc`，日志 `configure-from-handoff.log`。这证明旧交接命令不完整，不推翻此前成功目录真实构建/初始化的记录。修订命令的 ProtocPath/PkgConfigPath 取自成功 cache，尚待下一位执行完整复现。

源码证据：`origin-comparison-normalized.json` 保存与用户原包的文本差异列表；`reviewed-files-sha256.json` 保存审查范围内的文件 hash；`stage-current.patch` 与项目 MSVC patch 的文本一致性比较为 True，stage submodule status 版本匹配。不要把这些日志、二进制或本机依赖提交到 Git。

### 18.5 本轮更新和未做项

本轮仅更正本交接、文档入口/原生指南/来源记录与 WORKLOG，并在忽略目录建立审计工具。没有修产品、修改 SDK/runtime、关闭用户程序、连接 PS5、commit、push 或 Release。没有新增完整 Veyra 构建/delivery/GPU测试；下一位必须以修复后新代码重新构建和验证，不能拿本次故障观察程序 exit0 或旧67项结果放行。

API核对参考：[FFmpeg send/receive 官方契约](https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html)、[固定版本 Chiaki decoder](https://github.com/streetpea/chiaki-ng/blob/0e16950165f06e5c3291537c2eeba6e852be7120/lib/src/ffmpegdecoder.c)、[固定版本 video receiver](https://github.com/streetpea/chiaki-ng/blob/0e16950165f06e5c3291537c2eeba6e852be7120/lib/src/videoreceiver.c)。源码定位使用同一提交的本地 clean checkout；上游播放器的简化解码处理不构成照抄许可之外的正确性证明。
