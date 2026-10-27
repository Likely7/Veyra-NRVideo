# #48 网络播放稳定性与服务器视频缓冲：实施方案与记录

日期：2026-10-17　分支：`claude/network-buffer-48-20261017`（基线 main `26c0d61`）　作者：Claude（子会话）

授权与范围见 `AGENTS.md` 顶部 2026-10-17 “#48 网络稳定与服务器缓冲授权”。用户原话：“http点播你随便找个Twitch的试试吧，我也没试过这玩意，然后服务器视频也可能是他的服务器速率太慢了，跑那种超大MKV根本跑不动，我自己手里也没有服务器不能测试，所以你就看着弄吧……开工吧”。#48 报错原文：`The source video contains corrupt data; processing stopped`（`源视频包含损坏数据，已停止处理`）。

存档：`checkpoint/pre-claude-network-buffer-48-20261017`（main 26c0d61）；`E:/项目/Veyra/archives/claude-network-buffer-48-20261017/{source-before.bundle,start.json}`（全引用 bundle 已 verify，75,282,603 字节，记录 88 个工作树 / 108 个脏文件）。产物目录统一 `E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/claude-network-buffer-48-20261017`。

---

## 1. 现状核对（main 26c0d61 实际代码）

| 事实 | 位置 |
| --- | --- |
| 损坏包 / 损坏帧 / 读包失败一律停止 | `src/source/MediaFileSource.cpp` `read()`：`AV_PKT_FLAG_CORRUPT` → “源视频包含损坏数据，已停止处理”；解码帧 `AV_FRAME_FLAG_CORRUPT`/`decode_error_flags` → “源视频包含损坏画面”；`readVideoPacket` 失败 → “源视频读取失败” |
| 网络期限 | `include/veyra/media/NetworkInput.h`：`rw_timeout`/`timeout`=5 s，`InputInterrupt::arm()` 每次操作 5 s；打开、探测、读包共用 |
| 视频、音频各开一个 `avformat` 连接 | 视频 `FFmpegDemuxer::open`（同步读，丢弃非视频包）；音频 `AudioPipeline::open`（自带读包线程与 2.5 秒包队列） |
| 内嵌字幕扫描 | `SubtitleLoader` 整流读取；**网络地址不扫描**（`QmlPlayerBridge::openPath` 对网络传空路径），UNC/映射盘按本地文件整文件扫描 |
| 直播暂停恢复 | `QmlPlayerBridge::togglePlayPause`：`liveKind=="url"` 且时长未知时恢复 = 重新打开地址 |
| 引擎读帧 | `EngineController::run`：首帧预读 `read()` 只接受 Frame/Error；主循环 `Waiting` 路径对所有源通用（`advanceLive(); waitLive(); continue;`） |
| 音频主时钟 | 文件播放以 WASAPI 设备时钟为主；断粮补静音且静音计入媒体时间 |

计划文档 §1.2 “字幕加载器对 http/https/rtsp 也会再开第三个连接”与实际不符：网络地址已不扫描字幕（见上表）。C5 的实质是 NAS 路径与网络点播的内嵌字幕获取方式。

## 2. 与计划的差异及理由（实现细节按代码实际调整）

计划 C4 写成两套机制：点播/NAS 用“内存字节缓存 + 每个读者一个自定义 AVIO”，直播用“单解复用线程 + 包队列”。核对代码后改为**一套**：

- **网络与 NAS 输入统一走“共享解复用会话”（`media::DemuxSession`）**：一个 `avformat` 上下文、一个解复用线程，按流把包放进有界队列；视频（`FFmpegDemuxer`）和音频（`AudioPipeline`）都从该会话取包。点播与直播不再区分机制，只是点播额外支持 seek。
- 理由：
  1. 字节缓存只能挂在“顶层 URL 的字节流”上。Twitch 回放等 HLS 点播的分片由 HLS 解复用器内部开连接，字节缓存无法让两个 `avformat` 共用分片下载（带宽仍翻倍）；RTSP 同理。包级共享对 HTTP 文件、HLS、RTSP、HTTP-TS 全部有效，保证“每会话一个连接”。
  2. 包队列天然给出“缓冲秒数”“下载速度”“码率”“暂停缓冲”的判据（对应 mpv 的 demuxer cache + cache-pause），比字节窗口更直接。
  3. 重连时队列里已有的包仍可播放，足够长的缓冲可完全掩盖短断流；点播重连后按“最后入队包的 DTS”续读并去重，解码器无需重置。
  4. 本地 SSD 文件完全不进会话，保持 2.0.9 的直读路径逐字节不变。
- 未实现 / 推迟：字节级回看窗口（小幅回退 seek 仍重新定位底层连接，与 2.0.9 相同）；记入“已知限制”。
- 会话使用范围：`http/https/rtsp` 地址，UNC 路径（`\\server\share\…`，不含 `\\?\C:\` 本地扩展路径）与 `GetDriveType==DRIVE_REMOTE` 的映射盘。测试开关 `VEYRA_TEST_FORCE_DEMUX_SESSION=1` 可让本地文件也走会话，用于与直读路径逐帧对照。
- 测试用慢速 IO：`VEYRA_TEST_SLOW_IO=<MB/s>[,<ms 延迟>]` 在会话的自定义文件 AVIO 上限速并加读延迟，模拟 NAS；不新建系统共享。

实施中另有这些与计划文字不同的地方（均已按下面的“设计”一节更新）：

- **重连退避**：1 s，之后每 2 s 一次（计划为 1/2/4/8 s 封顶 8 s），总预算 60 s；HTTP 内层 `reconnect_delay_max=2`。服务端无论何时恢复，下一次重试最多 2 秒后到来，给“等关键帧 + 起播缓冲”留出 5 秒内恢复的余量（首轮实测封顶 4 s 时直播 8 秒断流的恢复为 4.47 s，余量只有 0.5 s，所以改成 2 s）。
- **恢复目标上限**：慢服务器的“暂停缓冲”攒够 `clamp(60×(1−速度/码率), 起播目标, 上限)` 秒再放行，上限 20 s（“大”档 45 s），不是计划里的“约一分钟”。慢服务器填满 60 秒窗口所需的时间与播放它一样长，用户要等太久；改为较短的多次放行，每次停顿都带原因提示。
- **损坏画面**：不用 `recoverAtKeyframe()`（`skip_frame=NONKEY` 与帧线程并用时输出异常）；“等下一个关键帧”改为清空解码器并丢包到干净关键帧（最长 5 秒后放行）；解码器报错用漏桶计数，2 秒内超过 12 次才算真正失败。
- **被截断的包按断线处理**：包带 `AV_PKT_FLAG_CORRUPT` 且 `pb->error<0` 说明连接在包中间断了。2.0.9 的 HTTP 点播遇到服务器中途断开，MP4 解复用器正是交出这种截断包，被 `MediaFileSource` 报成“源视频包含损坏数据，已停止处理”——这是 #48 的另一个来源。现在丢弃该包并走重连。
- **HLS 主播放列表**：打开后只保留画质最高的一个变体，其余变体的流设 `AVDISCARD_ALL`，HLS 解复用器随即不再下载它们（FFmpeg 在读头时仍会取每个变体的前 1–2 个分片）。按代码推断，2.0.9 的视频解复用器只丢弃非视频流、音频管线另开一个上下文，所有画质的视频流都会持续下载；未在 2.0.9 构建上实测。
- **字幕旁路未做**：2.0.9 本来就不对网络地址扫描字幕，保持不变（网络地址不开任何字幕连接）；只做 NAS 路径的“延后 + 限速 + 让出”，见 §3.5。
- 引擎在 `open()` 里经 `PlayerOptions::from()` 往返会丢掉 `PlayerOptions::deinterlace`（2.0.9 已有的缺陷，设置里的“自动去隔行”开关因此不起作用）；与本任务无关，只在 `network` 字段旁保留了同样的往返，缺陷本身未改，列为后续。

## 3. 设计

### 3.1 新增 / 修改的代码

| 文件 | 变更 |
| --- | --- |
| `include/veyra/media/NetworkInput.h` | 拆分连接/读超时；`NetworkSettings`（档位、起播预缓冲、自动重连、损坏画面策略）；`NetworkStats`；`remoteMediaPath()`（UNC/映射盘）；HTTP `reconnect*` 选项 |
| `include/veyra/media/DemuxSession.h`、`src/media/DemuxSession.cpp`（新） | 共享解复用会话：线程、包队列、seek、重连、码率与下载速度测算、缓冲状态机、慢速 IO 测试文件 AVIO |
| `src/media/FFmpegDemuxer.cpp`、`include/…/FFmpegDemuxer.h` | 会话模式：流参数读自会话上下文，`readVideoPacket` 取队列包，新增非阻塞结果 |
| `src/source/MediaFileSource.cpp`、`include/…/MediaFileSource.h` | 网络容错（C1）、会话接入、`Waiting`、重连不连续标记、统计出口 |
| `include/veyra/source/IFrameSource.h` | `SourceOpenDesc` 增加 `NetworkSettings`、`nonBlocking`、`tolerateCorruption` |
| `src/sink/WasapiAudioSink.cpp`、`include/…/WasapiAudioSink.h` | `AudioPipeline::open(session)`：从会话音频队列取包；网络缓冲暂停时挂起端点（时钟冻结） |
| `src/engine/EngineController.cpp`、`include/…/EngineController.h` | 首帧预读等待缓冲；`Waiting` 状态文案；`PlayerSnapshot::network`；`PlayerOptions::network` |
| `src/engine/Subtitles.cpp` 等 | C5：NAS 扫描延迟与限速 |
| `src/ui/QmlPlayerBridge.cpp`、`include/…/QmlPlayerBridge.h`、`qml/Veyra/SettingsPage.qml`、`qml/Veyra/ProPage.qml`、`i18n/catalog.json` | C6：设置项、信号面板“网络”行、四语翻译 |
| `tests/network/slow_server.py`、`tests/integration/NetworkBufferTests.cpp`（新） | 本地测试服务端与回归 |

### 3.2 解复用会话

- 解复用线程循环：`av_read_frame` → 视频包进视频队列，所选音轨的包进音频队列，其它流（含字幕）设 `AVDISCARD_ALL`。每次读包前设读期限（点播 20 s、直播 10 s），打开用连接期限 15 s、探测 30 s；用户停止或切源（`cancelled`）、会话销毁、seek 请求都会立即打断阻塞读。
- 队列上限：软目标 S（点播：小 8 s / 中·自动 30 s / 大 120 s；直播抖动缓冲 小 1.5 s / 中 3 s / 大 6 s，自动 3 s；实现见 `NetworkBufferPlan`），按“最短一路相对播放位置的前瞻”判断；同时有字节硬上限（点播 64 / 256 / 1024 MB，直播 32 / 64 / 128 MB）。自动档在服务器偏慢时窗口可放大到 120 s / 1024 MB。任一路前瞻 < S 就继续读，另一路最多超出到硬上限，避免交错偏移饿死另一路。
- 播放位置由引擎每轮写入（音频时钟或最后呈现 PTS）。**前瞻（lead）= 队列里等待的包的时长之和 + 消费方已取走但尚未播到的部分（上限 5 s）**，不再用“最后入队包结束时间 − 播放位置”：10 分钟 1% 丢包测试中，HTTP-TS 流中途重新开始（时间戳回跳 600 s），旧算法的前瞻一直“满”，队列已空却不再读，永久卡死；换成与时间戳无关的时长累加后消除。另有保险：视频消费方发现队列空，生产者无视前瞻继续读（仍受字节上限约束）。
- 码率估计：容器 `bit_rate` 或队列字节 / PTS 跨度（≥3 s 样本后取实测）；下载速度 = 解复用线程“读包中”的字节 / 读包耗时（EMA），另报 2 秒窗口平均。
- 状态机：`Prebuffering`（起播或 seek 后，前瞻 ≥ 起播目标或 EOF 才放行）→ `Playing`（前瞻 < 低水位（0.25–0.5 s，随档位）且未 EOF → `Buffering`）→ `Buffering`（前瞻 ≥ 恢复目标或 EOF → `Playing`）；`Reconnecting` 叠加在读线程上，消费方继续用队列里的包，队列见底才进入 `Buffering`。档位“关闭”不停顿（`holds=false`）。
- 自适应恢复目标（C7）：持续（≥5 s 读包时间）下载速度 < 码率 × 0.95 时置 `bandwidthLow`（带滞回），恢复目标 = clamp(60 × (1 − 速度/码率), 起播目标, 20 s（“大”档 45 s）)；界面提示“服务器速度 x MB/s 低于视频码率 y MB/s，建议加大缓冲或下载后播放”。绝不降速播放、不报错黑屏。
- 暂停缓冲：会话告知 `MediaFileSource::read()` 返回 `Waiting`（引擎主循环已按活源处理，画面停在当前帧）；音频线程因会话 hold 而暂停端点，设备时钟冻结，恢复后继续，不丢 PCM、不拿静音冒充同步。

### 3.3 重连（C2）

- 触发：读包返回非 EOF 错误、读期限超时、连续 10 秒只有损坏数据。关闭用户设置“断线自动重连”时直接报错（带原因）。
- 退避 1 s，之后每 2 s 一次，总预算 60 s（测试开关 `VEYRA_TEST_RECONNECT_BUDGET_MS` 可改）；用户停止立即中断；每次尝试用连接期限 15 s。读包返回的截断包（`AV_PKT_FLAG_CORRUPT` 且 `pb->error<0`）与读期限超时同样触发重连。
- 点播（可 seek 且时长已知）：重新打开后 `av_seek_frame(最后入队视频 DTS, BACKWARD)`，每路丢弃 DTS ≤ 该路最后入队 DTS 的重复包，队列已有的包照常播放，解码器不重置。HLS 点播同理（按分片重新定位）。
- 直播（不可 seek）：重新打开，丢弃首个视频关键帧之前的包，时间戳平移使新连接首个关键帧紧接上一连接结束时间（连续时间线，音视频同一平移量），队列首包带“不连续”标记，解码器 `flushBuffers()`，帧带 `Discontinuity` 使图历史重置；重连后的音频包若解码报 `AVERROR_INVALIDDATA` 视为坏包跳过（连续 8 次才算失败）。
- **直播流中途重新开始 / 时间戳突变**（编码器重启、损坏的时间戳；只对直播）：视频关键帧的时间戳相对上一个入队包回跳超过 1 s 或前跳超过 3 s，就按重连同样的方式重新锚定（同一平移量让时间线连续，不连续标记，解码器与图历史重置，3 s 内到达的旧时间线音频包丢弃）；带突变的非关键视频包按损坏时间戳丢弃，等关键帧。只有音频的突变视为声音的缺口，原样通过。点播只记日志。没有这一条，引擎在流重启后画面冻结到新时间戳追上时钟为止（实测 28 s）。
- HTTP 同时打开 FFmpeg `reconnect`、`reconnect_streamed`、`reconnect_on_network_error`、`reconnect_delay_max`（内层快速重试），HLS 设 `seg_max_retry`。

### 3.4 损坏容错（C1）

- `SourceOpenDesc::tolerateCorruption`：引擎对网络源与 `mpegts` 容器默认置真；导出、测试工具、`MediaFileSource` 默认值保持严格。
- 损坏包照常送解码器（FFmpeg 自带隐藏/修补）；解码失败的单个包记数后继续；带 `AV_FRAME_FLAG_CORRUPT`/`decode_error_flags` 的帧按“损坏画面”策略：`保持画面`（默认，显示解码器修补结果）或 `等待关键帧`（清空解码器并丢包到干净关键帧，最长 5 秒后放行）。解码器报错用漏桶计数，2 秒内超过 12 次才算真正失败。
- 计数与限频日志（≥1 秒一条）；界面提示“检测到网络丢包，已自动跳过 n 处”（每会话至少隔 20 秒一次）；连续 10 秒无输出 → 强制重连（两次之间至少隔 30 秒）。

### 3.5 字幕（C5）

- 网络地址：不开任何字幕连接（与 2.0.9 相同：网络地址不扫描内嵌字幕，也没有字幕旁路；已知限制）。
- NAS / 映射盘（`engine::subtitleServerFile`）：`SubtitleLoader::setReadGate` 由桥接按引擎的网络统计开关——会话 `Playing`、前瞻 ≥ 3 s 且服务器不比码流慢才放行（前瞻 < 1 s 即关，播放读完整个文件时也放行）。扫描在放行前不打开文件；放行后经 64 KiB 缓冲的限速 AVIO 读（≤ 40 MB/s，`VEYRA_TEST_SLOW_IO` 同样生效），门一关读取立即停住。Matroska 等容器为找字幕包要读完整个文件，所以这是整文件流量；服务器比码流慢时整个扫描不会开始。外部字幕文件（小文件）与本地文件走原来的路径。

### 3.6 设置与状态（C6）

- 设置→播放：`网络缓冲`（自动/关闭/小/中/大，默认自动）、`起播预缓冲`（跟随缓冲档/不等待/3/5/10/20/30 秒）、`断线自动重连`（默认开）、`损坏画面处理`（保持画面/等待关键帧）。均为保存的偏好（`networkBuffer` / `networkPrebuffer` / `networkAutoReconnect` / `networkCorrupt`），下次打开网络视频生效。
- 信号面板（ProPage 的“信号”卡）新增“网络”行：`缓冲 18.2 s · 下载 12.4 MB/s · 重连 0`（有丢包时追加 `丢包 n`，窄卡片换行）；暂停缓冲或重连时极简页在画面下方显示药丸（含原因），状态灯显示“缓冲中”而不是“输出未达标”，状态栏沿用引擎的网络提示。重连成功与丢包汇总各弹一次提示。四语翻译进 `i18n/catalog.json`（共 38 条新增）。UI 变化检测把网络统计计入（位置冻结时仍会刷新）。

## 4. 测试计划与验收（每类只测一次，不做压力测试）

测试服务端 `tests/network/slow_server.py`（Python 标准库）：Range、限速（低于/高于码率两档）、定时卡顿 8–12 s、中途断开、按比例破坏 TS 包、连接数统计。素材：本机已有高码率大文件（≥60 Mbps 4K，不改原件）。

| 指标 | 方法 |
| --- | --- |
| 1% TS 丢包播放 10 分钟不停止 | 服务端按 1% 破坏 TS 包；“10 分钟”用环回加速读取（按内容时间 10 分钟计）并如实说明 |
| 8 秒断流后 5 秒内恢复 | 服务端卡顿/断开 8 s，量测恢复出画时间 |
| 点播每会话连接数 = 1 | 服务端连接计数 |
| 缓存不超上限 | 会话字节统计 |
| 音画偏差 < 40 ms | 引擎遥测 / 音频时钟与视频 PTS 对照 |
| 带宽低于码率 | 限速低于码率档：进入暂停缓冲，提示含原因，无报错 |
| RTSP 单会话 | mediamtx 连接计数 |
| 本地文件回放与 2.0.9 一致 | 同一素材：直读路径输出与 2.0.9 构建逐帧 PTS/像素 SHA 对照；强制会话路径与直读路径对照 |
| 真实 HTTP 点播（用户指定） | Twitch 回放 HLS / 剪辑 MP4 在 Veyra 内试播；令牌脱敏；拿不到则改用 Mux/Apple HLS 测试流与 W3C Sintel，如实记录 |

真实 NAS 与用户的直播源本机无法验证，列为现场待验。

## 5. 进度记录

（随实施逐项更新；详细命令、日志路径见 `docs/WORKLOG.md` 与 `reports/claude-network-buffer-48-20261017`。）

- [x] 存档、AGENTS.md 授权、方案文档（本文件）
- [x] 共享解复用会话（C2/C3/C4/C7 核心）`c6d933c`；引擎接入、测试服务端与 `veyra_network_buffer_tests` `c4da72d`；直播重连修正 `2ad5559`
- [x] 设置、信号面板与缓冲界面（C6/C7）`0d07c9d`；服务器文件字幕扫描等待播放并限速（C5）`3e513c1`
- [x] 与时间戳无关的流控、直播时间线重启、重连间隔上限 `d46d47a`
- [x] 收尾（子会话额度用尽后由协调会话接手提交）：HLS 主列表只打开最佳码率变体（Twitch 回放需要；与 mpv `hls-bitrate=max` 同法）、直播重连只做短探测并以 0.5 s 起播缓冲、带宽判定需 3 s 读包证据、码率历史按读包时间老化、`selectAudioStream()` 与解复用线程的锁、重连退避（前 15 s 每秒一次、之后每 2 s）、连接期限改为整个打开过程 40 s（死连接仍由 15 s 读期限切断）。
- [x] 隔行网络电视（1080i IPTV）的损坏画面：此前隔行分支遇到损坏帧仍直接停止（“隔行视频画面损坏或尺寸改变”），现与逐行同一策略（显示修补结果 / 等待关键帧，尺寸变化仍停止；去隔行输出不再重复计数）。新增测试素材 `av-1080i25-60s.ts`（x264 隔行 TFF）与用例 `ts-flip-1pct-interlaced`；测试程序增加 `deinterlace=1` 选项。

### 验收结果（2026-10-18，本机 RTX 5070，单次运行）

完整矩阵 `tests/claude-network-buffer-48-20261017/results/matrix-claude-verify`（收尾改动编译后的同一套程序）：19/19 通过。

| 用例 | 结果 | 秒 | 用例 | 结果 | 秒 |
|---|---|---|---|---|---|
| selftest | 通过 | 0.1 | live-outage-8s | 通过 | 46.1 |
| local-ab（本地直读与会话逐帧/像素哈希一致） | 通过 | 2.8 | stop-latency | 通过 | 37.0 |
| local-engine（本地文件不走会话） | 通过 | 12.2 | ts-flip-1pct-10min-decode | 通过 | 11.2 |
| vod-fast | 通过 | 22.3 | ts-drop-1pct-10min-decode | 通过 | 11.2 |
| vod-slow（服务器慢于码率：暂停缓冲并说明原因） | 通过 | 45.4 | ts-loss-1pct-realtime | 通过 | 200.4 |
| vod-big-4k-mkv（约 70 Mbps 4K MKV，慢服务器） | 通过 | 100.4 | live-restart | 通过 | 65.3 |
| vod-stall | 通过 | 50.4 | hls（单码率 / 多码率） | 通过 | 1.0 |
| vod-outage-8s | 通过 | 27.9 | rtsp（mediamtx 只见 1 个读者会话） | 通过 | 25.1 |
| vod-reconnect-off | 通过 | 10.1 | nas-slow-io（慢速 IO 模拟） | 通过 | 40.2 |
| subtitle-scan-nas | 通过 | 18.2 | | | |

隔行损坏补测 `results/claude-interlaced`：`ts-flip-1pct-interlaced` 通过（去隔行输出 2974 场、1363 张损坏画面被容忍、无错误无重连），`ts-flip-1pct-10min-decode` 与 `local-ab` 复测通过。

真实点播（用户指定“随便找个 Twitch 试试”）`tests/claude-network-buffer-48-20261017/twitch`：公开 Twitch 回放（HLS）在引擎里播放 69 s，首帧 5.3 s、1 个连接、0 卡顿 0 重连、服务器 6.4 MB/s / 码率 0.74 MB/s、音画偏差 p95 1 ms；Twitch 剪辑（直链 MP4）30 s 播完，首帧 2.3 s、1 个连接、0 卡顿。带签名令牌的地址只在测试进程内存里，没有写进提交、文档或日志。

未验证：用户自己的直播 / 点播地址（#48 原始源）、真实 NAS / SMB 服务器（用户无服务器，用慢速 IO 与限速服务端模拟）、AMD 实卡、长时间运行。
