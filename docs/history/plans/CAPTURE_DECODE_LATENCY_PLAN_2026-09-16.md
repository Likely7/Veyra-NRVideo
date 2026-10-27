# 采集解码与延迟：最终架构实施方案（2026-09-16）

状态：**施工中**（2026-09-16 晚：N4/N3/N1 已完成并各自取证，N2 尝试后回退——实测负收益，见 WORKLOG；
压缩解码链路待开工；逐项进度见
WORKLOG 对应条目与本文标注）。包含两部分：①压缩格式
（MJPEG/H.264/HEVC）的解码架构；②原生格式（YUY2/NV12/RGB…）的 N1–N4 优化。目标是一条架构做到
"低延迟 + 顶级效果 + N/A/I 三家全覆盖"，不分期、不并行多套过渡形态。

> **唯一权威版本（2026-09-16 晚统一）**：隔离分支旧版 `1028ddb` 与 main 重建版 `8a681d0`
> 的内容已按并集并入本版（分支的实测依据/模块清单/施工顺序/验收表 + main 的 §8–§10 原生链路）。
> 此后只维护本文档；2026-09-16 已随分支合并进 main（merge `24e7de7`），main 旧版被本版取代。
>
> **相邻修复（不属本计划施工，但与本计划有交互）**：
> - RGB24 采集方向修复（分支 `015f8a4`、main `87ad4cd`）：直连 RGB DIB 先协商 top-down，
>   另加手动"画面上下翻转"开关。N1 要改写同一个 `copyCaptureSample`，方向契约是硬性约束（见 §8 N1）。
>   本机没有 RGB24 设备，实机确认只能由受影响用户做（看 `[capture] DIB top-down request` 日志）。
> - PS5 串流首帧中止修复（分支 `015f8a4`）：`2ffb5c7` 的 dangling else 回归，与延迟链路无关。

## 1. 实测依据（本机 RTX 5070 + AMD 核显 + USB3 采集卡）

| 事实 | 数字 | 来源 |
| --- | --- | --- |
| 我们软件段（1080p60 原生、无增强） | **2.640 ms**（=0.16 帧） | 实卡 `[capture-timing]` |
| 我们软件段（1080p60 MJPEG、无增强） | 4.121 ms（CPU 2.16ms/帧） | 实卡 |
| 4K（卡上限 18fps）MJPEG 每帧 CPU | 7.04 ms | 实卡 |
| 4K MJPEG 软解（多线程无效，熵解码串行） | 7.2 ms/帧（单线程 8.0） | ffmpeg benchmark |
| 4K MJPEG 用 NVDEC（帧留 GPU） | **2.97 ms/帧** | `-hwaccel cuda -hwaccel_output_format cuda` |
| 4K MJPEG 用 D3D11VA | 7.34 ms/帧（=软解回退） | 同左 |
| 增强代价（60fps 实卡） | NR+SR +10.5ms；再开补帧 +4.4ms | 实卡 |
| 卡侧 A/V 相对偏移 | 视频比音频晚 ≈46ms（YUY2 与 MJPEG 相同） | `[live-audio-sync] skewMs` |
| D3D12 解码能力探针 | N 卡：H264/HEVC/HEVC10/AV1/VP9 + **MJPEG_420**；A 卡核显：仅 H264/HEVC/AV1/VP9，**MJPEG 全不支持** | `veyra_decode_profile_probe.exe` |
| OBS 同卡配置 | `format: YUY2`、`buffering: disabled`、`hardware decode` 开关 | OBS 日志 |
| 4K 采集回调：YUY2 行拷贝（16.6MB） | 0.89 ms/帧 | 微基准（本机） |
| 4K 采集回调：BGR24→BGR0 逐像素 | **3.88 ms/帧** | 微基准 |
| 4K 采集回调：UYVY→YUY2 逐像素 | **2.22 ms/帧** | 微基准 |
| 4K 采集回调：RGB565→BGR0 逐像素 | **8.41 ms/帧** | 微基准 |
| 4K 对照：单次 16.6MB memcpy | 0.70 ms | 微基准 |

完整排查过程、A/B 方法与全部原始数字见 [采集延迟调查报告](../reports/CAPTURE_LATENCY_INVESTIGATION_2026-09-16.md)。

结论：**H.264/HEVC/AV1 可以靠系统硬解三家通吃；MJPEG 押不住硬解，必须我们自己解。**

## 2. 最终架构（压缩格式）

```
采集卡 → UVC 驱动样本
  ├─ 原生格式 YUY2/NV12/P010/RGB24/RGB32 → 现有原生路径（§8 N1–N4 处理）
  ├─ H.264 / HEVC / AV1（含 10bit） → FFmpeg D3D12VA 解码（复用我们已有的 D3D12 设备/队列）
  │        → 解出 AV_PIX_FMT_D3D12 纹理 → 图的硬件入口（已存在，文件播放就在用）
  └─ MJPEG → 自研"帧级并行软解"（MJPEG 每帧独立，可多帧并行）
           → NV12 帧 → 图现有的软件入口
                                   ↓
        统一：显式颜色合同（BT.601/709/2020、full/limited、PQ/HLG）
              → GPU 色度/转换 → NR/超分/补帧 → 呈现（vsync=0 + tearing）
```

**彻底移除**：系统解码器 → 强制 RGB32 → SampleGrabber → NullRenderer 这条兼容路径（2026-09-16 已完成：
压缩族默认走自有解码；RGB32 路径保留为回退/诊断，任一级失败时自动启用）。

### 为什么这条可行（不是画饼）

1. **图已经有硬件纹理入口**：`EnhanceGraph` 支持 `AV_PIX_FMT_D3D12` 输入（`hardwareInputFrames_`），文件播放的 D3D12VA 就是走这条路 —— 采集压缩流复用同一入口，**不需要新的零拷贝管线**。
2. **FFmpeg 已包着我们的 D3D12 设备**：`FFmpegVideoDecoder::openD3D12VA()` 用 `AVD3D12VADeviceContext` 包我们的 device/queue；采集侧复用同一套。
3. **构建里已有需要的解码封装**：`CONFIG_H264/HEVC/AV1/VP9_D3D12VA_HWACCEL=YES`；**没有** MJPEG 的封装 → 正好对应"两条实现"。
4. **能力探针已就绪**，可在任意机器上确认支持面（Intel 待跑）。

## 3. 模块级改动清单

| 模块 | 改什么 | 关键点 |
| --- | --- | --- |
| `src/source/CaptureCardSource.cpp/.h` | ①格式分类：原生 / 压缩两族；②压缩格式改为 `ConnectDirect(设备pin → 我们的 sink)`，不再 `RenderStream`+RGB32；③对**视频**输出 pin 调 `IAMBufferNegotiation::SuggestAllocatorProperties`（现在只有音频做）；④解析 H.264/HEVC 媒体类型里的 sequence header 作为 `extradata` | 压缩类型不再要求 `captureMediaLayout`；记录实际 `cBuffers/cbBuffer` |
| `src/source/NativeCaptureSink.cpp/.h` | 接受压缩媒体类型（不解析像素布局），把样本 payload + 时间戳交给上层队列 | 保持"借样+立即归还"，压缩样本必须**拷贝一份**（大小只有几十~几百 KB，远小于一帧未压缩数据） |
| 新增 `src/source/CaptureDecodeQueue.cpp/.h` | 有界队列 + 工作线程池：压缩样本 → 解码 → 输出帧 | MJPEG 池化并行（2–4 路）；H.264/HEVC/AV1 走 FFmpeg D3D12VA（单路或 2 路）；下游慢时**丢旧帧**（与现有 mailbox 语义一致） |
| 新增 `src/source/CaptureDecoder.cpp/.h` | 后端选择 + 回退：`D3D12VA(格式支持)→并行软解→单线程软解`；MJPEG 直接走并行软解 | 能力判定用 `ID3D12VideoDevice::CheckFeatureSupport`（复用探针逻辑，抽成 `decodeProfilesAvailable()`） |
| `src/media/FFmpegVideoDecoder.*` | 抽出可复用的"D3D12VA 会话"（设备/队列/池参数），供采集解码复用 | 不改文件播放行为；`extra_hw_frames` 按最坏延迟给 2–3 |
| `src/engine/EngineController.cpp` | 采集分支接入新入口；压缩路径的帧直接是 D3D12 纹理，原生路径不变；新增诊断字段（decodeBackend、每帧解码耗时、队列深度） | 现有 `[capture-timing]` 增加 `decodeMs`、`queueDepth`、`backend=` |
| `apps/veyra/ui/CapturePanel.cpp` | 格式列表按"延迟成本"排序并标注：原生（最低）/卡内编码（+1~3 帧）/需系统解码（更高）；显示"需 CPU 解码"提示 | 4K60 且选到压缩格式时给一次建议 |
| `apps/veyra/SettingsWindow.cpp` | 新增"设备缓冲：自动 / 最小 / 驱动默认"档位 | 默认=自动（按分辨率选择 cBuffers） |
| `tests/integration/CaptureSourceTests.cpp` | 新增：压缩格式直连成功、后端选择与回退、缓冲协商数值、并行软解吞吐 | 真机可跳过（无卡时按现有约定） |

## 4. 施工顺序（单一序列，不是分期方案）

1. **能力与契约**：抽出 `decodeProfilesAvailable()`（探针逻辑）+ 定义"解码输出帧"契约（D3D12 纹理 或 NV12 CPU 帧）。
2. **压缩样本直连**：压缩格式改 `ConnectDirect`，`NativeCaptureSink` 接受压缩类型并拷贝 payload；日志记录 subtype 与字节数。
3. **H.264/HEVC/AV1 后端**：复用 FFmpeg D3D12VA，解出的纹理走图现有硬件入口；先保证"能出画面、颜色正确"。
4. **MJPEG 并行软解**：线程池 + 有界队列 + 丢旧帧；输出 NV12 走现有软件入口。
5. **缓冲协商 + UI**：视频 pin 协商、缓冲档位、格式排序标注。
6. **原生格式拷贝合并**：sink 直接写进图中转缓冲（省一次整帧拷贝）。
7. **清理与验收**：默认不再走 RGB32 兼容路径；跑完整门禁 + 真机对比。

> 进度（2026-09-16 深夜收工）：①**已完成**（解码后端选择/回退与输出帧契约为 `CaptureCompressedDecoder`，
> 含硬件帧可导入校验）；②**已完成**（压缩族全部直连压缩 sink，payload 拷贝入有界队列，MJPEG 真机验证）；
> ③**已完成**（H.264/HEVC/AV1/VP9 走 D3D12VA，首帧不可导入自动回退软解；合成验证见报告 §6，真卡未验）；
> ④**不做**（并行软解池：单 worker 已 0 丢帧且吞吐远超需求，并行只提高吞吐不降低单帧延迟，理由见报告 §7）；
> ⑤（=N4）**已完成**；⑥（=N2）**尝试后回退（实测负收益，见 WORKLOG）**；⑦**部分完成**（默认不再走 RGB32，
> 保留为回退/诊断；delivery 门禁已纳入解码测试）。完整报告：`docs/CAPTURE_MPEG_CHAIN_REPAIR_2026-09-16.md`。

## 5. 验收指标（可量化，达不到不算完成）

**本机可测（RTX 5070 + 这张 USB3 卡）**

| 指标 | 目标 |
| --- | --- |
| 1080p60 原生，无增强 | callback→Present ≤ **3 ms**（现状 2.64，不许回退） |
| 1080p60 MJPEG | callback→Present ≤ **3.5 ms**（现状 4.12） |
| 4K（尽可能高帧率）MJPEG | 每帧 CPU ≤ **3 ms**（现状 7.0+） |
| 4K MJPEG 4K60 推算 | 每帧 CPU ≤ 3 ms 且 `readAgeMs` ≤ 1 帧、`callbackFps` ≥ 名义 98%（**不出现积压**） |
| H.264/HEVC | 解码在 GPU：每帧 CPU ≤1 ms，颜色与软解逐像素差 ≤2 code |
| 颜色一致性 | 同一画面：压缩路径输出与"软解参考"逐像素差 ≤ 阈值（RGB32 路径作对照） |
| 回退 | 任一级失败自动降级，不断流；日志写明后端 |
| 门禁 | 修复合同 181 项、delivery 短测、预设/UI 合同全过；采集/导出既有行为不回归 |

**必须真机（本机替代不了）**：见 §10。

## 6. 风险与对策

| 风险 | 对策 |
| --- | --- |
| AMD 要求 **reference-only allocations**（探针 flags 已给出） | 解码帧池按该约束创建；初始化失败时记明确日志并回退并行软解 |
| 卡的 MJPEG 可能是 **4:2:2**（N 卡只硬解 4:2:0） | 不押注硬解：MJPEG 一律走并行软解；若确认是 4:2:0 再启用 D3D12 MJPEG 加速 |
| UVC 的 H.264 媒体类型 sequence header 位置/形式不一 | 同时支持 `MPEG2VIDEOINFO`(SPS/PPS) 与采样内 in-band SPS/PPS；解析失败→回退软解并记录 |
| 并行软解和 NR/SR 抢 CPU | 线程数默认 = min(4, 核数-2)，可在诊断里看到占用；GPU 侧业务不受影响 |
| 驱动/码流兼容导致解码失败 | 三级回退（D3D12VA → 并行软解 → 单线程软解），任何一级失败都不断流 |
| 缓冲压到最小引发丢帧 | 档位制（自动/最小/驱动默认），默认自动；丢帧计入 `captureDropped` 并在 UI 显示 |
| 颜色回归 | 引入"软解参考帧"作为对照测试；颜色合同沿用现有显式解析，不新增隐式转换 |

## 7. 明确不做（边界）

- 不动导出链路（NVENC/MF、码率、CFR 逻辑）。
- 不动 NR/SR/补帧算法与其调度。
- 不动文件播放路径（D3D12VA 文件解码只做"抽出来复用"，不改变其行为）。
- 不引入新的第三方二进制依赖（复用已批准的 FFmpeg 构建能力；若将来需要 NVDEC 直连再单独审批）。
- 不宣称"整体延迟 X ms"：卡内部、DWM、显示器三段不在我们控制内，只能靠真机端到端测量给结论。

## 8. 原生链路（未压缩格式）优化：N1–N4 全部实施

原生格式不走解码器，但当前在**采集回调线程里**做两件重活，且有 5 种打包格式是**逐像素 CPU
转换**（基线见 §1 微基准）。

> **进度（2026-09-16 晚）**：N4 ✅（视频 pin 缓冲协商 + 三档设置 + requested/actual 日志；本机 UVC 卡驱动忽略建议、如实标注）/ N3 ✅（排序 + 延迟档位 + 一次性提示）/ N1 ✅（GPU 解包；4K 回调成本 RGB24 1.12ms、RGB565 0.83ms、UYVY 0.64ms，全部达标；`--capture-cpu-unpack` 可回退）/ N2 ✖ 尝试后回退（直接写入图上传缓冲，功能可用但 1080p/4K 均净亏 ~0.85ms，原因见 WORKLOG；代码已整体移除）。证据见 WORKLOG 2026-09-16 各节。

### N1：逐像素转换从 CPU 挪到 GPU

- **改动**：`copyCaptureSample` 对 UYVY/YVYU/BGR24/RGB555/RGB565 只做"按行原始字节拷贝"；
  `EnhanceGraph` 入口接受这些 packed 布局；着色器用**一个带 packing 常量的统一变体**，
  保留现有 `Yuy2ToLinear`/`RgbToLinear` 行为。
- **方向契约（硬性）**：这次改的 `copyCaptureSample` 同时承担 2026-09-16 的 RGB24 方向修复：
  RGB DIB 按协商后的 `biHeight` 符号决定翻转、手动"画面上下翻转"按样本生效（YUV 连色度行一起翻）。
  N1 只准把逐像素转换挪走，不准改动方向判定与传递；完成后
  `veyra_capture_color_tests.exe` 的方向断言（bottom-up 读最后一行 / top-down 直通 / 手动翻转对
  两种输入都反相 / NV12 色度行跟随）必须继续全过，`--capture-flip` 烟测必须继续 exit 0。
- **验收**：
  - 4K RGB565 采集回调每帧 **≤1.2 ms**（现 8.41）；4K BGR24 **≤1.5 ms**（现 3.88）；
    4K UYVY **≤1.2 ms**（现 2.22）；4K YUY2 不回退 ≤1.0 ms；
  - 颜色：各格式与旧 CPU 路径逐像素比对，平均误差 ≤1、最大 ≤2 code（合成色卡 + 真实信号各一次）；
  - 720p/1080p/4K 各连续 60 秒：`captureDropped=0`、无花屏；
  - 提供"切回 CPU 转换"的诊断开关。

### N2：两次拷贝合并成一次

- **改动**：`EnhanceGraph` 暴露 `acquireIngressBuffer(parity)` / `commitIngressFrame(parity,pitch,format,pts)`；
  `NativeCaptureSink` 直接写进图的上传缓冲；无图上下文时保留旧信箱路径。
- **验收**：
  - 4K YUY2 全链路每帧 CPU **≤1.0 ms**（现 1.186）；4K RGB32 **≤2.0 ms**（现约 2.4）；
  - 写入前等待该 parity 的 fence；debug 断言 + 日志证明从不写"正在被 GPU 读"的缓冲；
  - 连续 5 分钟 4K60 + 3 次分辨率切换 + 2 次暂停/恢复：无撕裂/花屏，`captureDropped` 不高于旧路径；
  - 失败自动回退旧路径并记日志。

### N3：格式排序与延迟标注

- **改动**：推荐顺序 `NV12/P010 < YUY2 < RGB24 < RGB32 < UYVY/YVYU/BGR24/RGB555/RGB565`，
  压缩格式永远排在原生之后；每项标注"延迟档位：低/中/高"与"是否需要 CPU 拆包"；选到高成本模式给一次性提示。
- **验收**：
  - 自动化断言：同一设备同时提供 YUY2 与 RGB565 时，推荐列表把 YUY2 排在前面；
  - **用户已记住的格式优先于推荐**（不覆盖用户选择）；
  - 4K60 + 压缩格式选择时出现提示（可关闭）。

### N4：视频 pin 缓冲协商 + 界面档位

- **改动**：两条直连路径在 `ConnectDirect` 之前对视频输出 pin 调
  `IAMBufferNegotiation::SuggestAllocatorProperties`（advisory）；日志记录 requested/actual
  `cBuffers/cbBuffer`；设置项"设备缓冲：自动 / 最小 / 驱动默认"（默认自动）。
- **验收**：
  - 日志出现 actual `cBuffers/cbBuffer`（现在完全没有）；
  - 支持协商的卡上 `cBuffers` 从驱动默认（常见 3）降到 **≤2**，并给出 `readAgeMs` 变化；
  - 1080p60 YUY2 的 `readAgeMs` 目标 **≤0.6 ms**（现约 1.0）；
  - 驱动不理会时写"协商未生效"，**不得计入收益**；
  - `captureDropped` 超旧路径 1.5 倍时自动回到"自动"档。

### 8.1 原生链路施工顺序

N4 → N3 → N1 → N2（N4/N3 便宜且立刻可测；N1 要动 shader 与入口格式；N2 风险最高放最后）。

### 8.2 原生链路总验收（硬性）

1. **延迟必须比现在低**：同一台机、同一张卡、同一路信号，改前/改后各跑 3 次 60 秒，
   下列指标**至少两项改善 ≥5% 且无一项回退 >3%**：`callback→Present P95`、每帧采集回调 CPU、
   `readAgeMs`、`captureDropped`。若某张卡四项全无变化（例如纯 YUY2 且驱动不响应协商），
   **如实报告"该卡无收益"**，不得把未生效项写进收益。
2. **逐像素格式必须显著改善**：RGB565/UYVY/BGR24 的采集回调每帧成本相对 §1 基线
   **至少下降 50%**（目标见 N1）。
3. **颜色与方向零回归**：N1 各格式与旧路径逐像素差 ≤2 code；P010(HDR) 路径不受影响；
   RGB DIB 方向契约（见 N1）断言全过。
4. **稳定性**：4K60 连续 30 分钟（含 3 次分辨率切换、2 次暂停/恢复）无花屏、无丢帧尖峰、
   RSS 增长 ≤50 MB、无句柄泄漏。
5. **门禁**：修复合同 181 项、`delivery.ps1` 短测、预设/UI 合同全过；采集/导出/图片路径无回归。
6. **可回退**：N1、N2 各自独立开关，出问题无需改版本即可切回旧路径。

### 8.3 原生链路真机要求

这张 ¥30 卡只提供 YUY2/MJPEG，**没有 UYVY/BGR24/RGB555/RGB565** → N1 的正收益本机无法取证。
两条路径：① 找一张能输出这些格式的卡（很多 USB3.0 卡低带宽模式就是 UYVY/RGB565）跑改前/改后；
② 拿不到卡时用合成样本喂 `copyCaptureSample` 与着色器做逐像素比对 + 微基准，并在报告里明确
标注"未在真卡验证"，不得写成已验收。

## 9. 三家显卡兼容（实测 + 待测）

| 能力 | NVIDIA | AMD | Intel |
| --- | --- | --- | --- |
| H264/HEVC/AV1 D3D12 硬解 | ✅ 实测 tier2 | ✅ 实测 tier2（ref-only alloc） | 待探针确认 |
| MJPEG D3D12 硬解 | ✅ 仅 4:2:0 | ❌ | 不押注 |
| MJPEG 自研并行软解 | ✅ | ✅ | ✅ |
| 原生格式 N1–N4 | ✅ | ✅ | ✅ |

## 10. 必须真机验证

1. Intel 显卡跑 `veyra_decode_profile_probe.exe`（只读），确认 D3D12 解码支持面；
2. 一张真 4K30/4K60 卡：MJPEG 采样格式（4:2:0/4:2:2）、改前/改后每帧成本、掉帧率；
3. 能输出 UYVY/BGR24/RGB565 的卡：N1 的改前/改后（否则只能合成样本验证）；
4. 相机端到端（240fps 拍主机画面 + 显示器）：改前/改后 + OBS/PotPlayer 同条件对比；
5. 能输出 RGB24/BGR24 的卡（相邻修复 `015f8a4`/`87ad4cd` 的实机验收，可与第 3 项同卡同轮做）：
   确认 `[capture] DIB top-down request … accepted=0/1` 与画面方向；若 accepted=0 仍倒置，
   勾选面板"画面上下翻转"兜底。
