# 2026-09-17 诊断：HEVC 文件打不开 + 导出中途中止

本文件只记录**本次排查的证据与结论**。产品代码本轮未改动；下面第 5 节的修复方案需要用户先拍板。

输入：

- 视频：`C:\Users\123\Desktop\IMAX.Laser.Pre.Show.New.2160P.DDP5.1.Atmos-ZhiLuan.mkv`
- 应用日志：`E:\App\Veyra-1.3.2beta4-win64-portable\logs\veyra-app.log`
- 导出失败日志：`C:\Users\123\Desktop\导出失败\`（8 份 export-worker + 7 份 veyra-app）

## 0. 结论

1. **打不开的原因不是这个视频，而是本机 D3D12VA 的 HEVC 解码整体不可用。**
   本机 = RTX 5070 + 驱动 32.0.16.1656（616.56）。任何 HEVC 文件（720p / 2024p / 2160p，含本机
   NVENC 自己编出来的文件）走 `D3D12VA` 都会在第一个 picture 上失败，并且**把 Veyra 的共享
   D3D12 设备打成 removed（`hr=0x887A0005`）**。软解回退是在同一台已死的设备上继续的，图初始化
   随即失败，会话直接退出——用户看到的就是"打不开"。
2. **导出失败与 HEVC 无关，是源文件时间戳的问题。** 源文件第 10467 个源帧的时间戳比恒定帧率
   网格整整晚一帧（+33.333 ms，不是量化抖动），撞上 `CfrTimeline` 的硬校验；中段跳变按设计一律
   拒绝（只有最后 3 帧有豁免），导出主动中止并保留 `.partial`。
3. 附带发现（另一台机器的导出路径）：RTX 5060 那台机器的 `nvEncOpenEncodeSessionEx` 三档
   apiVersion 全被拒（`status=15`），退回 Media Foundation MFT；9/16 的旧版没有这条 fallback，
   所以那次是当场失败而不是跑 8 分半。

## 1. 现场证据（用户日志原文）

`E:\App\Veyra-1.3.2beta4-win64-portable\logs\veyra-app.log:3072-3088`（2026-09-17T08:02:15Z =
16:02 本地时间，RTX 5070 / 32.0.16.1656）：

```
[media] demuxer: opened codecId=172 streams=2 videoStream=0 durationUs=72334000 timeBase=1/1000
[media] decoder: d3d12va decoder opened codec=hevc 3840x2024 lowLatency=false (shared Veyra device)
[source-file] opened 3840x2024 dur=72.334s avgFps=60.000 container=matroska,webm codec=hevc pixelFmt=yuv420p hw=true ...
[ERROR] [media] decoder: send_packet failed code=-22 text=Invalid argument
[WARN ] [source-file] D3D12VA first-frame fallback to software reason=send-packet-error path=redacted
[media] decoder: software decoder opened codec=hevc 3840x2024 pixFmt=0 threads=4 activeThreadType=1 ...
[media] decoder: frame#1 stamp=0 tb=1/1000 -> ptsUs=0 format=0      <- 软解首帧正常
[ERROR] [gpu-timestamp] query heap hr=0x887A0005                    <- 设备已被移除
[ERROR] [gfx-util] upload buffer alloc failed size=4 hr=0x887A0005  <- 空描述符堆都建不出来
```

即：设备在 `device created / timestamp heap created` 时是好的（同一会话 08:02:14 两条 S_OK），
中间只有一次 D3D12VA HEVC 解码尝试，此后所有 D3D12 资源创建都返回"设备已移除"。

## 2. 对照实验（本机复现）

视频文件本身先排除：软件解码全片 12 秒、零告警；D3D11VA/DXVA2 硬解 62 帧 0 错误。

| 实验 | 命令要点 | 结果 |
|---|---|---|
| IMAX 文件 软解 | `ffmpeg -i IMAX.mkv -f null -` | 12s 完成，无 warning |
| IMAX 文件 D3D11VA | `ffmpeg -hwaccel d3d11va ... -frames:v 60` | exit=0，62 帧 0 错误 |
| IMAX 文件 DXVA2 | `ffmpeg -hwaccel dxva2 ... -frames:v 60` | exit=0 |
| IMAX 文件 D3D12VA | `ffmpeg -hwaccel d3d12va -frames:v 5` | **exit=-22**，`hardware accelerator failed to decode picture` + 之后每帧 `Could not create the texture` |
| 本机 NVENC 合成 3840x2160 HEVC | `d3d12va` | **exit=-22**（同样的两条错误） |
| 本机 NVENC 合成 3840x2024 HEVC | `d3d12va` | **exit=-22** |
| 本机 NVENC 合成 1280x720 HEVC | `d3d12va` | **exit=-22** |
| 1.mp4（1080x1920 H.264） | `d3d12va` | exit=0 |
| **Veyra.exe 本体**（1280x720 HEVC + `--smoke-seconds 6`） | 见下 | frames=0 failed=true，exit=1 |

Veyra 本体复现（换一个完全不同的 HEVC 文件，走产品自己的代码路径）：

```
[media] decoder: d3d12va decoder opened codec=hevc 1280x720 lowLatency=false (shared Veyra device)
[ERROR] [media] decoder: send_packet failed code=-22 text=Invalid argument
[WARN ] [source-file] D3D12VA first-frame fallback to software reason=send-packet-error
[ERROR] [gpu-timestamp] query heap hr=0x887A0005
[ERROR] [gfx-util] upload buffer alloc failed size=4 hr=0x887A0005
[app] smoke frames=0 generated=0 failed=true ...      <- 一帧都没出来
```

这条命令追加到了用户便携包的 `logs\veyra-app.log`（一次性 6 秒，未改设置：`--smoke-seconds>0`
时不会读/写偏好与"最近打开"）。

结论：**打不开与这个 IMAX 文件无关**；本机 5070 + 616.56 上 HEVC+D3D12VA 本身就坏，
H.264+D3D12VA 正常，HEVC+D3D11VA 正常。

## 3. 代码路径（为什么"回退成功"还会整个会话退出）

- `EngineController.cpp:200`：本地文件固定 `od.preferHardwareDecode=true` + 共享设备/队列（无 UI 开关）。
- `FFmpegVideoDecoder::openD3D12VA()`：把 Veyra 自己的 `ID3D12Device` 包成 `AVHWDeviceType_D3D12VA`。
- `MediaFileSource::read()`：首个 packet `send_packet` 失败 → `fallbackToSoftware()`（重开 demuxer +
  软解），**但整套图/呈现器仍然用那个已经被移除的 D3D12 设备**，于是 `EnhanceGraph::initialize()`
  在 `gpu-timestamp` / `gfx-util` 处失败，会话按错误路径退出。
- 导出路径相反：`VideoExportJob.cpp:67` 固定 `od.preferHardwareDecode=false`（软解），所以导出
  不会踩这个雷——用户那台 5060 机器导出能一路跑到第 10467 帧正是这个原因。

## 4. 导出失败：证据与机制

两轮导出（01:29:06 与 01:40:29 启动，各跑约 8 分 45 秒）在**同一帧**中止：

```
[ERROR] [export-timeline] CFR rejected source=10467 pts=348.93333333333334 expected=348.9
        deviationMs=33.333 estimatedFrames=20451 outputIndex=20933
```

源文件（导出输入）：1920x1080 H.264 / 29.999 fps / AAC 44.1k 立体声 / mp4，
`stream->duration=61353000 @1/90000` = 681.7 s，2 条流。

机制（`include/veyra/engine/CfrTimeline.h`）：`accepts()` 在"每帧正好整数个 tick"的源
（90000/30 = 3000 tick，`ticksPerFrame` 为整数）上要求 `|pts - (origin + i/30)| <= 1e-9`，
即**必须严丝合缝**；`tailAccepts()` 只放过"容器时长×帧率"最后 3 帧内的 ≤1.5 帧偏差。
第 10467 帧落在 20451 帧的中段 → 走硬失败分支，停止写盘并保留 partial。

偏差是**整整一帧**（33.333 ms），不是量化抖动：前 120 帧抽样完全符合 30/1（否则 preflight 会
选 30000/1001），说明第 10467 帧前后源时间戳里少了一个网格槽位。

两种可能来源，日志把"解码侧丢帧"压得很低：

- 文件自身丢帧/时间戳跳变（VFR 记录、录制丢帧）——最可能；
- 解码侧丢帧——三重排除：`receiveFrame()` 失败必打 `[ERROR] media ... receive_frame failed`；
  损坏 packet 与损坏帧在 `MediaFileSource.cpp:251` / `:273` 是**硬失败并打日志**（源码里写明
  "refusing to skip source data"，不存在静默丢帧路径）；两份 worker 日志除上述 CFR 两条之外，
  整个 8 分 45 秒里没有任何一行解码/损坏相关错误。

**待确认**：需要导出源文件（或在该机器上产出 packet 时间戳清单）才能给出最终判定。

另外记录一条**不同**的失败（9/16 那 5 份 worker，全部 3 秒内结束、干净退出）：

```
[nvenc] CreateInstance status=0
[nvenc] OpenD3D12Session status=15                    <- NV_ENC_ERR_INVALID_VERSION
[ERROR] ... 无 fallback，导出当场失败（旧版 1.3.0）
9/17 版：retry apiVersion=12.0 status=15 / 11.0 status=15 → 回退 MediaFoundation-MFT，导出继续
```

与 `NvencD3D12Encoder.cpp:49-54` 的注释一致（"observed on a user RTX 5060"）。当前版本靠 MF 兜底
可以继续导出，但要注意 MF 路径在 `bitrateMbps=0` 时走的是 `UnconstrainedVBR + Quality=75`
（`MfVideoEncoder.cpp:310-313`），只在媒体类型上声明 20 Mbps（`:332`），**编码器目标码率完全
不受用户设置控制**——属于本次范围外但应记录的降级。

## 5. 建议（待用户决定，本轮未改代码）

**A. 立刻可做的验证（不用改代码）**

1. 拿这个 IMAX 文件去那台 RTX 5060 机器（驱动 581.57）放一次：
   - 能放 → 616.56 驱动的 D3D12VA HEVC 回归；反过来也能确认 581.57 可用。
   - 不能放 → 环境/硬件之外的共同因素，需要进一步定位。
2. 若要留在 616.56：告诉 NVIDIA（或先回滚到上一版驱动）——本机能给出可复现的最小用例
   （`ffmpeg -hwaccel d3d12va -i any_hevc.mkv -f null -`）。

**B. 产品侧修复（我的建议顺序）**

1. **失败即弃设备（必须做）**：`send_packet` 在 D3D12VA 首次失败后调用
   `ID3D12Device::GetDeviceRemovedReason()`，若为 `DXGI_ERROR_DEVICE_REMOVED` 就重建设备上下文
   与图，再用软解继续；不能让一个已死的设备拖着整个会话退出。这也是"用户看到打不开"的直接修复。
2. **HEVC 的硬解能力探测（推荐）**：进程内用**独立的一次性设备**跑一次极小的 HEVC 硬解探测
   （1 帧、32x32 即可），失败就对该驱动会话禁用 D3D12VA HEVC，直接软解——避免每次打开 HEVC 都
   先把设备打坏。探测必须与主设备隔离，否则等于重复今天的故障。
3. **（可选，成本高）HEVC 走 D3D11VA 硬解**：本机 D3D11VA 正常，但要新写一条解码路径并把
   D3D11 纹理共享回 D3D12 图，工作量明显大于 1+2；建议先做 1+2 再评估。
4. 本地文件目前没有解码方式开关（只有 RemotePlay 面板有），加一个"强制软解"开关可以作为兜底，
   但它治不了根——用户不知道什么时候该开。

**C. 导出侧（等拿到源文件再定）**

三种取舍，按"诚实优先"排序：

1. **保时间戳导出**：输出 PTS 用 `round(pts*rate)` 而不是帧序号，缺口真实保留（mp4 允许某一片
   时长加倍），A/V 完全对齐，不假装 CLF；`outputIndex` 与 FG 的 2 倍网格要一起改。
2. **补齐成严格 CFR**：缺口处重复上一帧（用户看到一次 1 帧卡顿），保持现有"输出严格 CFR"的承诺。
3. **现状不变**：只在 UI/日志里说清楚"第 N 帧偏移 X 毫秒"，让用户自己选别的工具——我不推荐，
   用户拿到的是一个跑了一半的过程。

无论选哪种，都需要先把"尾部 3 帧豁免"扩展成"中段 ±1 帧可重同步"的策略文档，并在
`CfrTimeline` 上补单测（整数 tick、非整数 tick、跳变、回归尾部）。

## 6. 未验证 / 边界（不许当作已通过）

- 未在 581.57 或其它驱动上验证 HEVC D3D12VA（本机只有 616.56）。
- 未取得导出源文件，因此"文件自身丢帧"只有概率判断，没有 packet 级证据。
- 未测试导出走 MF MFT 的实际画质（码率 0 → MFT 默认值）。
- 未在 RTX 30/40、AMD、Intel 上验证任何一条结论。
- 本机没有 Windows TDR（4101）事件，`0x887A0005` 是驱动内部把设备标记为移除，不是 GPU 挂起复位；
  这一点与"D3D11VA 同时正常"一致，但不能据此推断驱动以外的原因。

## 7. 本次用到的证据文件

- `logs/diag-20260917-hevc-d3d12va/ffmpeg-d3d12va-imax.txt`（本机 FFmpeg 8.1.1 D3D12VA 全量 verbose 日志）
- `logs/diag-20260917-hevc-d3d12va/ffmpeg-d3d11va-imax.txt`（同一文件 D3D11VA 对照，62 帧 0 错误）
- 用户日志：`E:\App\Veyra-1.3.2beta4-win64-portable\logs\veyra-app.log`（3072-3088 行）
- 导出日志：`C:\Users\123\Desktop\导出失败\export-worker-32444.log` / `-27300.log`

复现命令（本机，含一次会刷新设备状态的 D3D12VA 尝试）：

```powershell
# 软件解码（基线）
ffmpeg -hide_banner -i IMAX.mkv -f null -
# D3D12VA（复现故障）
ffmpeg -hide_banner -loglevel verbose -hwaccel d3d12va -i IMAX.mkv -frames:v 5 -f null -
# D3D11VA（对照）
ffmpeg -hide_banner -loglevel verbose -hwaccel d3d11va -i IMAX.mkv -frames:v 60 -f null -
# Veyra 本体（任意 HEVC 文件都会复现）
Veyra.exe <any_hevc.mkv> --smoke-seconds 6
```

## 8. 修复实施：HEVC 改走 D3D11VA（2026-09-17 当天完成）

用户决定：**HEVC 一律接 D3D11VA，不再用 D3D12VA 碰运气。**

### 8.1 实现

| 文件 | 改动 |
|---|---|
| `include/veyra/media/FFmpegVideoDecoder.h` / `src/media/FFmpegVideoDecoder.cpp` | 新增 `openD3D11VA()`（同适配器私有 D3D11 设备 + 共享纹理环 + D3D11→D3D12 fence）、`HardwareSurfaceView`、`receiveFrame()` 里逐帧发布共享面；`hardwareFrameImportable()` 支持 D3D11 面 |
| `include/veyra/pipeline/FramePacket.h` | 新增 `HardwareSurfaceInput`（纹理 / 子资源 / 等待 fence / 纹理尺寸），随帧包传递 |
| `include/veyra/pipeline/EnhanceGraph.h` / `src/pipeline/EnhanceGraph.cpp` | `process()` 增加硬件面参数；ingress 支持 `AV_PIX_FMT_D3D11`，与 D3D12VA 共用同一段 SRV/等待逻辑 |
| `include/veyra/source/IFrameSource.h` / `src/source/MediaFileSource.cpp` | `SourceOpenDesc.d3d12AdapterLuid`、`SourceInfo.videoDecodePath`；**HEVC → D3D11VA**，其余编码仍走 D3D12VA，失败再软解 |
| `include/veyra/gfx/D3D12DeviceContext.h` / `src/gfx/D3D12DeviceContext.cpp` | `AdapterInfo.luid`（D3D11 设备需要同适配器） |
| `src/engine/EngineController.cpp` / `src/engine/VideoExportJob.cpp` 等 | 传 adapter LUID 与 `&packet.hardwareSurface` |
| `tests/integration/HardwareImportImageTests.cpp` | 支持两种硬件路径，打印真实解码路径与共享面信息 |
| `CMakeLists.txt` | `veyra_media` 链接 `d3d11` |

### 8.2 为什么是"拷贝进自己的共享纹理"而不是直接共享解码面

实测（本机 RTX 5070 / 616.56）：

1. 给 FFmpeg 的解码池加 `SHARED_NTHANDLE` → `[AVHWFramesContext] Could not create the texture (80070057)`，
   即驱动拒绝在 **DXVA 解码输出纹理**上开共享。
2. 自己新建一张 NV12 纹理，只加 `SHARED_NTHANDLE` → 同样 `0x80070057`。
3. 换成 `D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE`（NT 句柄共享的标准组合）
   → 创建成功，D3D12 `OpenSharedHandle` 成功。

所以最终结构：FFmpeg 用标准（非共享）解码池 → 每帧把解码切片 **GPU 拷贝**进我们自己的共享纹理环（8 槽，
保留解码对齐尺寸）→ D3D11 fence 加信号 → 图在 D3D12 队列上 `Wait` 一次再采样。全程零回读、零 CPU 等待。
图入口本来就按**绝对 texel 索引**差分采样，所以对齐留白（HEVC 128 对齐）天然不会被读进来。

槽位复用安全性：应用每 `read()` 一帧才提交一次 D3D12 工作，而 6 个命令槽会强制"第 N 帧提交前，第 N-6 帧
的 GPU 工作已完成"；8 槽 > 6+1，因此一个槽在 8 帧后被重写时，它上一次的读取已经完成。同一槽内的两次拷贝
之间还有 D3D11 侧 fence 等待兜底。

### 8.3 验证（`veyra_hw_import_image_tests`，硬解 vs 软解逐像素全图对比）

| 输入 | 解码路径 | 解码面 | 结果 |
|---|---|---|---|
| 1280x720 HEVC（本机 NVENC） | `d3d11va` | 1280x768（对齐） | `FULL_IMAGE_PASS=1 worst=0` |
| 3840x2160 HEVC（本机 NVENC） | `d3d11va` | 3840x2176（对齐） | `FULL_IMAGE_PASS=1 worst=0` |
| **IMAX 3840x2024 HEVC（用户文件）** | `d3d11va` | 3840x2048（对齐） | `FULL_IMAGE_PASS=1 worst=0` |
| 1080x1920 H.264（回归） | `d3d12va`（未变） | 1080x1920 | `FULL_IMAGE_PASS=1 worst=0` |

`worst=0` = 4 帧全图平均绝对误差为 0，说明共享/拷贝/裁剪链路与软解完全一致，没有颜色或几何失真；
测试内附带的 `hardwareImportFixtures`（1/3 切片纹理阵列）同样通过。

### 8.4 未验证（不许当作已通过）

- **没有跑 Veyra 本体 smoke**：构建目录里的 `veyra.exe` 被另一个正在运行的实例占用。
  全目标编译（`cmake --build <dir> -- -k 0`）结果：**105/105 个其他目标全部编译+链接通过**，唯一失败的是
  `veyra.exe` 的链接（`LNK1104: 无法打开文件"veyra.exe"`，纯文件锁）。因此"打开 IMAX 文件、播放 30 秒"
  这一条留给用户顺手验收。
- 10-bit HEVC（P010）未测；AMD/Intel 适配器未测；低延迟（采集）路径未测。
- 8 槽复用的推理依赖"应用 6 命令槽 + 每次 read 提交一帧"这一当前架构，改环大小或改调度需重新评估。

## 9. 导出修复：整槽缺口补帧（同日晚，用户拍板"把导出修复一下"）

### 9.1 策略

- **只有"正好缺整数个网格槽位"才补**：`CfrTimeline::missingSlots()` 要求该帧时间戳落在网格上
  （残差 ≤ 半个容器 tick），且相对当前索引前移 1..2 槽。落在网格外（真 VFR 抖动）或跳幅超过 2 槽的源，
  **照旧硬失败**，输出不落地。
- 缺口用**上一帧真实画面重复填充**（`multiplier` 个输出帧，与正常一格的输出格数一致），**不伪造插值**：
  DLSS-G 无法用"同一帧"做插值，硬插会编造运动。补帧计入 `hold`，并单独统计 `gapFillEvents/gapFilledSlots`。
- 补完立刻 `CfrTimeline::resync()` 重锚相位，后续帧继续严格校验；预检的 120 帧抽样
  （`CfrTimeline::select()`）也认同样的整槽缺口，否则文件会在进循环前就被拒。
- 结束语会写清楚：`（源文件缺帧 N 处/M 帧，已按恒定帧率复制上一帧补齐）`；日志另有一行
  `[export-timeline] gap filled … (previous frame repeated; no interpolation invented)`。

### 9.2 验证

素材：`testsrc2` 30fps / 6.000s / 180 帧 CFR，用 `select` 精确抠掉样点（保留时间轴）：
`exp_hole1.mp4`（缺第 60 帧，179 帧，时间戳 1.9333→2.0333）与 `exp_hole3.mp4`（缺 60/61/62，177 帧）。

| 用例 | 命令 | 结果 |
|---|---|---|
| 单元策略 | `veyra_repair_contract_tests` | **197 checks / 0 failures**（新增 6 项：缺口识别、网格外抖动拒绝、>2 槽拒绝、resync 后重对齐） |
| 缺 1 帧导出 | `veyra_export_probe exp_hole1.mp4 out_hole1.mp4` | **exit 0**；`gap filled source=60 … missingSlots=1`；`source=179 hold=1 output=180 gapFillEvents=1`；`export-verify decoded=180 expected=180 passed=true` |
| 缺 3 帧导出 | `veyra_export_probe exp_hole3.mp4 out_hole3.mp4` | **exit 1**，`CFR rejected source=preflight`，**不生成输出文件**（边界未被放宽） |
| 无缺口回归 | `veyra_export_probe exp_src.mp4 out_clean.mp4` | exit 0，`gapFillEvents=0 hold=0 output=180`，verify 通过 |
| **应用本体无界面导出** | `Veyra.exe exp_hole1.mp4 --export-out … --max-frames 80` | **exitCode=0**，输出存在，`[app] export result=true`，verify `decoded=81 expected=81` |
| 补的是不是"上一帧" | 输出第 60 帧 vs 源各帧 PSNR | 源第 59 帧（洞前最后一张）**43.64 dB**，源第 60 帧（洞后第一张）20.47 dB，源第 57 帧 22.20 dB → 确为重复上一帧，不是插值/编造 |

### 9.3 改动文件

`include/veyra/engine/CfrTimeline.h`（`missingSlots()` / `resync()` / `select()` 缺口容忍）、
`src/engine/VideoExportJob.cpp`（补帧 + 重锚 + 计数 + 收尾文案）、
`tests/unit/RepairContractTests.cpp`（策略用例）、
`tools/export_probe/main.cpp` + `CMakeLists.txt`（无界面导出验证台）。

### 9.4 边界（如实记录）

- 只补 1..2 槽；3 槽以上或网格外偏差仍然拒绝——**没有把 VFR 源悄悄当 CFR 导出**。
- 缺口处是"冻结上一帧"，缺口后第一帧的 DLSSG 插值仍取自（洞前, 洞后）两帧；那一处运动速度会比真实
  时间轴略快，属于源素材本身丢帧的固有代价，不额外伪造。
- 补帧数计入 `hold`（导出摘要里的"重复帧"），未新增 `ExportCounts` 字段以免动 UI 契约。
