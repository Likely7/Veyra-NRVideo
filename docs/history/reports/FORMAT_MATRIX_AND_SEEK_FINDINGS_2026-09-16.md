# 2026-09-16 格式矩阵测试 + MKV 跳转排查（只测只查，未改产品代码）

用户指令："各种视频格式的导入导出都测试一下确保没问题，还有 1.3.0 修复的播放器流畅度
似乎只在 mp4 生效，MKV 跳转要卡半分钟……这轮只做导入导出测试和问题排查，不要开始
自顾自修复"。

本轮**没有修改任何产品代码**。新增的只有测试素材与驱动脚本：
`out/format-matrix/`（素材 + `run-seek.ps1`）与本文档。构建产物未变
（`out/build/audio-continuity-repair-20260915`，EXE SHA256 `DE306CB0…`）。

## 1. 导入（播放）矩阵：20/20 通过

每项 `--smoke-seconds 5`，判定 `exit=0 && frames>0 && failed=false`：

| 素材 | 容器/编码/音频 | 结果 |
| --- | --- | --- |
| a1 | MP4 / H.264 / AAC | 279 帧，lateness P95 1.06ms |
| a2 | MKV / H.264 / AAC | 279 帧 |
| a3 | MP4 / HEVC / AAC | 281 帧 |
| a4 | MKV / HEVC / AAC | 281 帧 |
| a5 | MOV / H.264 / AAC | 280 帧 |
| a6 | MOV / H.264 / **PCM** | 278 帧 |
| a7 | AVI / MPEG-4 / MP3 | 279 帧（日志有一条 `send_packet failed -22` 但继续播放） |
| a8 | WebM / VP9 / Opus | 280 帧 |
| a9 | TS / H.264 / AAC | 280 帧 |
| a10 | FLV / H.264 / AAC | 278 帧 |
| a11 | MKV / HEVC **10bit** / AAC | 281 帧 |
| a12 | MOV / **ProRes** / PCM | 281 帧 |
| a13 | TS / **MPEG-2** / MP2 | 280 帧 |
| a14 | MKV / 抽帧后非标准帧率 | 42 帧（8.57fps × 5s，符合预期） |
| a15 | MP4 / H.264 + **旋转元数据 90°** | 281 帧（但方向被忽略，见 §4） |
| a16 | MKV / H.264 + **内嵌 SRT/ASS 字幕** | 281 帧（字幕轨被忽略，见 §5） |
| a17/a21 | MKV 多音轨（AAC 2ch + AC3 5.1） | 281 帧（只用第 1 条音轨，见 §4） |
| a18 | MKV / **AV1**(dav1d) / AAC | 283 帧 |
| a19 | MKV / **HEVC HDR PQ** / AAC 5.1 | 30 帧（1s 素材） |
| a20 | MP4 / H.264 / **AAC 5.1** | 281 帧 |

## 2. 导出矩阵：19/22 通过，3 类被拒

每项 `--export-out … --max-frames 12 --no-nr --no-sr --no-fg`（HDR 加 `--hevc`），
判定 `exit=0` 且输出经 ffprobe 检查：

| 结果 | 素材 |
| --- | --- |
| ✅ 正常导出 H.264/H.265 | a1/a2/a3/a4/a5/a8/a9/a10/a11/a12/a13/a15/a16/a18/a19(HDR Main10)/a20(5.1)/a21/HLG 1080p/4K60 H.264/HEVC 1080p |
| ❌ **AVI（a7）** | preflight `CFR rejected … no timestamp-consistent rate candidate`。该 AVI 前几帧时间戳为 0 / 50 / 66.7 / 83.3 / 100 ms（间隔 min 16.666、max 50ms、3 种），不是严格 CFR |
| ❌ **非标准恒定帧率（a14，8.57fps）** | 同样死在 preflight：CFR 候选只有 24/25/30/48/50/60/100/120 与 24000/1001 等 NTSC 档，**不在表里的帧率一律拒绝**（手机 15/10fps、录屏 8.57fps、动画 12fps 都会中招） |
| ❌ 真 VFR（时间戳不均匀） | 由 a14/a7 已覆盖同类路径；中间跳变仍会按 `export-timeline` 拒绝（尾帧容差只覆盖最后 3 帧，见 2026-09-16 导出修复文档） |

**兼容性注意（不是失败，但值得知道）**：PCM（a6/a12）与 MPEG 层 2（a13）音频被**原样复制**
进 MP4（`pcm_s16le` / `mp3` 标签）。我们自己能解，但第三方播放器对 PCM-in-MP4 支持很差；
我们的导出校验只逐帧验证**视频**，不验证音频可播放性。

## 3. 4K HEVC 播放失败（本轮最重的发现，与 MKV 无关）

测试 `out/format-matrix/seek-4k-hevc-2s.{mp4,mkv}`（NVENC 编码的 4K HEVC）与
**我们自己导出的 `out/hdr-audio-fixtures/export-4k-nr-vsr-fg.mp4`**：

```
[ERROR] [media] decoder: send_packet failed code=-22 text=Invalid argument
[WARN ] [source-file] D3D12VA first-frame fallback to software reason=send-packet-error
[ERROR] [gpu-timestamp] query heap hr=0x887A0005        (DXGI_ERROR_DEVICE_REMOVED)
[ERROR] [gfx-util] upload buffer alloc failed size=4 hr=0x887A0005
→ smoke frames=0 failed=true（整场会话挂掉）
```

边界与归因（全部本机实测，驱动 32.0.16.1656 / RTX 5070）：

- 用 **系统 ffmpeg 走同一条 D3D12VA 路径**解码同一文件：`hardware accelerator failed to
  decode picture` + `Could not create the texture` —— **同样失败**，所以不是 Veyra 的解码
  集成写错，而是这条硬解路径在这台机器上处理该码流就失败。
- **软件解码同一文件正常**（`ffmpeg -i … -f null -` exit 0；Veyra 导出路径用软解 12 帧
  导出+逐帧验证通过）。
- 逐项缩小：4K HEVC 无论 8bit/10bit、有无 B 帧都失败；**1440p / 1080p HEVC 正常**；
  4K **H.264** 正常。
- 关键对照：**x265 生成的 4K HEVC（Main/L5.0/含 B 帧）能硬解**，NVENC 生成的 4K HEVC
  （同为 Main/L5.0）不能 → 差异在 NVENC 的码流细节，不在分辨率/档次本身。
- 用旧包 `E:\App\Veyra-1.3.1beta-win64-portable\Veyra.exe` 复测同一文件：**同样失败**
  → 不是本轮改动引入的回归。

影响：**我们用 NVENC 导出的 4K HEVC 文件，在这台机器上自己都放不了**；粉丝之间互传 4K HEVC
导出件也可能踩到。需要别的机器（其它驱动/其它 40/30 系）复现确认是驱动面还是普遍面。

## 4. 其它实锤问题

- **旋转元数据被忽略**：`a15-rotated90.mp4`（像素 1920×1080 + `rotate=90`）播放正常但方向
  不变；`--smoke-save` 抓出来的帧仍是 1920×1080。手机竖拍视频会横着显示，导出后也是横的
  （导出文件不带 rotation 元数据）。
- **多音轨只用第一条**：`a21-multi-track.mkv`（AAC 2ch + AC3 5.1 + SRT + ASS）播放日志
  `[audio] audio stream idx=1 codec=aac`；导出输出也是 `aac,2ch`。**没有音轨选择 UI**，
  5.1 AC3 轨被完全忽略（对 PS5/家庭影院用户是硬伤）。
- 内嵌字幕轨（SRT/ASS）不读取，也不影响播放（见 §5）。

## 5. MKV 跳转（用户反馈"卡半分钟"）排查

驱动脚本：`out/format-matrix/run-seek.ps1`（PostMessage 发 VK_RIGHT，每次 +10s；进度条本身
不能用外部消息驱动，因为 250ms 的 UI 定时器会把它写回播放位置）。采集的数据来自现成的
`[seek-latency]` 日志（`drainAndDemuxMs` = 排空+解封装耗时；`firstPresentMs` = 到目标帧
真正上屏；`decodedToTarget` = 从关键帧解码到目标跳过的帧数）。

| 素材 | GOP | 容器 | 特效 | firstPresentMs | decodedToTarget |
| --- | --- | --- | --- | --- | --- |
| seek-hevc-2s | 2s | **MKV** | 关 | 17–62 ms | 8–61 |
| seek-hevc-2s | 2s | **MP4** | 关 | 14–64 ms | 8–59 |
| seek-hevc-2s | 2s | MKV，**砍掉尾部索引** | 关 | 22–57 ms | 8–60 |
| seek-hevc-2s | 2s | MKV | **NR+SR 开** | 25–56 ms | 8–59 |
| seek-hevc-20s | **20s** | MKV | 关 | **89–300 ms** | 113–423 |

结论：**在这台机器上 MKV 跳转不比 MP4 慢**（同内容同编码，20 – 60 毫秒级）；
关键帧间隔拉到 20 秒也只是 0.1–0.3 秒；没有索引、开 NR+SR 都不改变结论。
代码侧也确认：跳转后"解码到目标"的帧在 `EngineController` 第 803 行
`if(pts+0.1<discardBefore)continue;` **在进增强图之前就被丢弃**，所以只花解码时间，
不花 NR/SR/补帧的时间。

那 30 秒从哪来？按可能性排序，需要用用户日志确认（现在下结论就是猜）：

1. **他的文件走的是软件解码**（他的日志里 `[source-file] opened … hw=true/false` 一眼可见）。
   软解 1080p60 HEVC ≈ 60–150fps，跳过 2 秒 GOP 就要 1–2 秒；GOP 30 秒就是 15–30 秒 ✓ 最像。
2. **GOP 极长**（部分录像/串流文件 30–120 秒一个关键帧），配合软解就是几十秒。
3. **存储慢**（外置盘/网络盘/机械盘）：从关键帧到目标要读几十 MB 随机数据。
4. **显卡被他自己的 NR/SR/补帧占满**，或**硬解在该码流上失败后走了 §3 那条"设备被移除"
   的路径**（那种情况不是慢，而是卡住后整场挂掉）。

要定位，只需要用户提供两行日志：`[source-file] opened … hw=?` 和
`[seek-latency] request=? firstPresentMs=? decodedToTarget=?`（以及
`[seek-latency] … drainAndDemuxMs=?`）。若 `drainAndDemuxMs` 大 → 解封装/存储面；
若 `decodedToTarget` 大而 `firstPresentMs` ≈ `decodedToTarget / 解码速度` → 解码面；
若卡住后 `failed=true` → §3 的设备移除面。

## 6. 字幕（现状，代码为准）

- **只支持外挂 SRT**：`loadSrt` 解析 `HH:MM:SS,mmm` 的 SubRip；UTF-8(BOM)/UTF-16LE；
  上限 8MB / 5 万条。载入对话框过滤器写死 `*.srt`。
- **同名自动加载：有**。`openFile()` 打开视频时自动尝试同目录同名的 `.srt`
  （`path.replace_extension(L".srt")`）。这解决"文件对不上"，但**不对时间轴**。
- **MKV 内嵌字幕：不支持**。内嵌 SRT/ASS 轨（a16/a21）能正常播放视频，但字幕轨完全不读，
  界面里也没有字幕轨选择。PGS/DVB 图形字幕的解码器**根本没编进我们的 FFmpeg**。
- **时间轴对齐/延时调整：没有**。既没有手动 ±毫秒偏移，也没有 ffsubsync/alass 那种
  按音频指纹自动对齐（"单独字幕文件自动对齐视频"目前只能靠文件名配对，不能修时间偏移）。
- 渲染：只有一个文本叠加层 + 字号 22/28/34 三档切换；无字体/颜色/描边/描边阴影/位置/
  背景条/双语/样式（ASS 特效完全不支持）。
- 导出：字幕**不烧录、也不封装**（导出面板写明），截图也不含字幕。
- 性能小坑：`subtitleAt()` 每帧线性扫描全部 cue（5 万条时每帧 5 万次比较），长字幕文件
  会造成无谓 CPU 开销。

## 7. 建议加的字幕功能（按性价比排序，等用户拍板）

1. **字幕延时微调（±10s，±50ms 步进）+ 快捷键**（`Z`/`X` 之类）：最便宜、最能救
   "字幕对不上"的刚需，覆盖 80% 的抱怨。
2. **ASS/SSA 支持（libass 渲染）+ 基础样式**：动漫/影视字幕大量是 ASS；现在连读都读不了。
   要引入 libass（ISC 许可，可静态链接）。
3. **MKV 内嵌字幕轨读取 + 字幕轨选择菜单**：SRT 直接读文本轨；ASS 交给 libass；
   PGS/DVB 需要图形字幕解码 + 叠加（工作量大，且要 FFmpeg 编进对应解码器）。
4. **多字幕同时显示（双语）**：外语片/学英语场景刚需，两条轨各占一行。
5. **字幕自动匹配 + 下载**：打开视频时按文件名哈希/title 到 OpenSubtitles 搜索并下载
   （要联网与隐私说明）。
6. **时间轴自动对齐（音频指纹，如 ffsubsync/alass 思路）**：真正"把外挂字幕对上视频"，
   但要跑离线对齐（几秒到几十秒），建议做"手动触发"。
7. **样式自定义**：字体/字号/颜色/描边/阴影/底部距离/背景条；跟随系统字体回退（中日韩）。
8. **音频轨选择**（顺带）：多音轨 MKV/TS 现在只用第一条，5.1 用户会骂；和字幕轨选择
   可以共用一套"轨道"菜单。
9. 小优化：cue 用二分查找/索引（顺手降低长字幕文件的每帧开销）。

## 8. 本轮未做

- 未改任何产品代码、未修 §3/§4/§5 的任何问题（用户明确要求只测只查）。
- 未在其它显卡/驱动上复现 4K HEVC 硬解失败；未取得反馈用户的 `seek-latency` 日志。
- 未测试 AMD/Intel 机器（本机只有 N 卡）。
