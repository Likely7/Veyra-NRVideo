# MPEG/压缩采集链路修复报告（2026-09-16）

状态：**MJPEG 已真机交付并复测**；H.264/HEVC/AV1/VP9 的 D3D12VA 后端已实现并通过合成验证
（本机采集卡没有这两种格式）；端到端光子延迟、静止画面颜色验收、真 H.264/HEVC 采集卡验收未执行。

关联：`docs/CAPTURE_DECODE_LATENCY_PLAN_2026-09-16.md`（架构与验收）、`docs/WORKLOG.md`（逐提交记录）。

## 1. 问题

用户与粉丝反馈：**用 MPEG 类格式（MJPEG/H.264）的采集卡，在我们软件里的延迟明显高于 OBS/PotPlayer**。
原生格式（YUY2/NV12/RGB）用户没有这个反馈。本机 ¥30 UVC 卡提供 MJPEG 1080p60 与 4K18 两档，
可复现：MJPEG 走过的是一条"兼容路径"。

## 2. 原因（代码与日志确认，不是猜测）

旧路径 = `IGraphBuilder::RenderStream` 自动建链：

```
设备 pin → [系统 MJPEG/H.264 解码器] → [颜色转换] → SampleGrabber(要 RGB32) → NullRenderer
```

具体代价：

1. **系统解码器在场**：解码发生在我们的 `SampleCB` 之前，耗时不可见、不可控、无法优化；
2. **每帧 RGB32 展开**：1080p 8.3MB、4K 33MB，颜色转换 + 拷贝都计入链路；
3. **回调线程做整帧拷贝**：`copyCaptureSample` 在 DirectShow 回调里 memcpy 整帧；
4. **图侧再上传 RGB32**：上传带宽 2.7 倍于 NV12（1080p 8.3 vs 3.1MB）。

日志证据：`[capture] explicit RGB32 compatibility path subtype=0x47504A4D`。

## 3. 新架构

```
采集卡 → UVC 驱动样本 → ConnectDirect(设备 pin → NativeCaptureSink 压缩模式)
   ├─ MJPEG              → 自有软解 worker（FFmpeg MJPEG + swscale）→ full-range NV12 → 图
   ├─ H.264/HEVC/AV1/VP9 → D3D12VA（共享 Veyra 设备/队列）→ D3D12 纹理直入图的硬件入口
   └─ 任一级失败          → RGB32 兼容路径（仅作回退/诊断，不再是默认）
```

关键契约：

- 回调线程只拷压缩 payload（1080p MJPEG 单帧数百 KB），有界队列上限 3、满则丢旧；
- 独立解码 worker，NV12 帧来自 **4 帧池**（交付/待交付/写入/备用），worker 只写"从未交付过或已被
  `read()` 释放"的帧——调用方持有的帧不会被覆盖；
- MJPEG 强制软解（驱动对 4:2:0/4:2:2 不一致，硬件路径只有一家支持 4:2:0）；
- 首帧硬件不可导入（非 NV12/P010）时自动切软解；解码器打不开时自动回退 RGB32 兼容路径；
- `MPEG2VIDEOINFO` 的 sequence header 支持四种形态（Annex-B / avcC / hvcC / 4 字节长度前缀），
  统一转 Annex-B 给 FFmpeg；无 extradata 时靠 in-band 参数集。

提交序列：`2a4f44f`（压缩 sink + codec 识别）→ `f0636f9`（MJPEG 直连 + 自解码）→
`b10e5c2`（解码 worker）→ `705e907`（D3D12VA/软解后端 + extradata + 低延迟标志 + 测试）→
`35e5593`（帧池契约 + probe 越界修复）。

## 4. 真机实测（MJPEG，本机 ¥30 UVC 卡，无增强）

**口径警告**：旧路径的系统解码发生在回调之前，`callback→Present`/`readAgeMs` 两个窗口**都不含解码**；
新路径窗口从压缩样本到达回调开始、**包含解码本身**。因此这两项新旧数字**不可直接比较**，
只能看"同口径项"与稳定性。

| 指标（2 分钟真机） | RGB32 兼容路径（基线） | 新链路 | 可比性 |
| --- | --- | --- | --- |
| 1080p60 帧数 / 丢帧 | 7163 / 0 | 7175–7184 / 0 | 可比（持平） |
| 1080p60 图侧 processCpuP95 | 1.987 ms | 0.364–0.375 ms | **可比，−81%** |
| 1080p60 readAgeMs | 0.5–1.5 ms | 5.0–6.8 ms（mean 6.0） | 不可比（新含解码） |
| 1080p60 callback→Present P95 | 3.909 ms | 7.7–8.9 ms | 不可比（新含解码） |
| 4K18 帧数 / 丢帧 | 2147 / 0 | 2150 / 0 | 可比（持平） |
| 4K18 图侧 processCpuP95 | 6.218 ms | 0.912–0.936 ms | **可比，−85%** |
| 4K18 readAgeMs | ≈2.9 ms | 19.8–22.9 ms | 不可比（新含解码） |
| 4K18 callback→Present P95 | 10.496 ms | 23.6–25.7 ms | 不可比（新含解码） |

解码器内部计数：`decoded≥6600 errors=0 queueDrops=0`（1080p60）、`decoded≥1800 errors=0 queueDrops=0`（4K18）。

其余可比较的客观项：

- 回调拷贝量：1080p 8.3MB → 数百 KB；4K 33MB → 数百 KB；
- 图侧上传量：1080p 8.3MB → 3.1MB；4K 33MB → 12.4MB；
- processCpu 下降的归因候选（未做单因子实验）：上传字节数减少 + 少了系统颜色转换层。

**4K18 readAgeMs 的注记（如实）**：worker 版（无帧池约束）单次采样 19.8 ms；4 帧池版 5 次采样
20.2 / 22.9 / 22.5（spare=1）与 23.6 / 25.2（spare=3）。spare 数没有单调影响，无法判定是帧池代价
还是运行波动；4 帧池换来的是"调用方持有的帧永不被 worker 覆盖"的硬保证，按安全优先保留。

**未执行**：端到端光子延迟（相机法）、与 OBS/PotPlayer 的同源对比。

## 5. 颜色验证（工具已就绪，严格判定待静止画面）

`veyra_capture_color_probe.exe <采集路径> <输出目录> [--legacy-rgb]` 现在两条路径都能跑：

- 新路径：`CAPTURE_COLOR success=1 presentMaxError8=0 format=23`（NV12）；
- 旧路径：`format=121`（BGR0）。

**本轮修掉的真实 bug**：probe 里 `AV_CEIL_RSHIFT(info.height, …)` 的 `info.height` 是 `uint32_t`，
无符号取负→移位得到 **2147484188 行**，在任何双平面格式（NV12）上越界崩溃。旧 RGB32 路径是单平面，
所以一直没有暴露。修复后两种路径都能输出 `source.raw` / `gpu.png` / `present.png` / `result.json`。

A/B 结果（1080p60，同管线输出 `gpu.png` 逐通道比较）：

| 对比 | max | mean | >2 code 占比 |
| --- | --- | --- | --- |
| 新路径 vs 新路径（两次采集） | 201 | 4.63 | 36.8% |
| 旧路径 vs 旧路径（两次采集） | 202 | 4.96 | 38.3% |
| 新路径 vs 旧路径 | 203–213 | 5.42–5.43 | 40.9–42.2% |

**解读**：当前采集信号是动态画面，两次采集之间内容本身就在变（同路径重复差异已 ≥ 跨路径差异）。
没有发现系统性色偏，但**逐像素 ≤2 code 的严格验收无法在动态内容上执行**。

用户验收步骤（约 1 分钟）：把采集源切到一个完全静止的画面（暂停的视频/纯色/静止菜单），依次执行：

```
veyra_capture_color_probe.exe "capture:0:<MJPEG格式号>:-1:0" out/color-ab/comp
veyra_capture_color_probe.exe "capture:0:<同一个格式号>:-1:0" out/color-ab/legacy --legacy-rgb
```

然后比较两个目录的 `gpu.png`（同路径重复两次作为噪声基线，跨路径差异应与之同量级）。

## 6. H.264/HEVC 合成验证（真卡验证不可得）

本机卡没有 H.264/HEVC 格式，用新测试 `veyra_capture_compressed_tests.exe [文件]` 驱动**同一个**采集
解码器，输入是本地 H.264/HEVC 文件的 Annex-B 数据（`h264_mp4toannexb` / `hevc_mp4toannexb`），
逐包像实时流一样喂入：

| 输入 | 硬解（D3D12VA） | 软解 | 帧数一致 | D3D12 帧可导入 |
| --- | --- | --- | --- | --- |
| H.264 1080p（`loop/local/fixed_clips/test_h264_1080p.mp4`） | 900 帧 / 0 失败 | 900 帧 / 0 失败 | ✅ | ✅ |
| HEVC（`out/format-matrix/a3-hevc-aac.mp4`） | 252 帧 / 0 失败 | 252 帧 / 0 失败 | ✅ | ✅ |

该测试已加入 `scripts/gates/delivery.ps1`（无本地素材时显式 SKIP，不伪造通过）。

**未验证**：真采集卡的 H.264/HEVC（含 in-band 参数集形态）、10bit/HDR 压缩采集、AV1、VP9。
10bit 压缩输入会被降位到 8bit NV12 并写警告日志——本轮不实现 P010/HDR 采集合同。

## 7. 明确不做（附理由）

| 项 | 结论 | 理由 |
| --- | --- | --- |
| MJPEG 并行软解池 | 不做 | 单 worker 已 0 丢帧：1080p60 图侧 0.37ms、4K18 0.93ms，解码能力远超帧率需求；并行只提高吞吐，不降低单帧延迟，还会引入乱序与 CPU 争用 |
| `CODECAPI_AVLowLatencyMode` | 作废 | 它设置的是系统解码器（AVI Decompressor 等）；新链路已完全不使用系统解码器 |
| 10bit/HDR 压缩采集 | 不做（如实记录） | 需要 P010 采集合同 + HDR 色彩管线；本机无 10bit 压缩源可验证，做了也无法取证 |

## 8. 已知风险与边界

- `callback→Present` / `readAgeMs` 两个窗口新旧口径不同，任何引用都必须带口径说明，否则会误导；
- H.264/HEVC 的 D3D12VA 路径没有真卡证据，只有合成数据；驱动差异（UVC 的 sequence header 形态）可能
  导致回退——回退链条（D3D12VA→软解→RGB32）已实现并记录日志；
- 4K18 readAge 与 worker 版存在 0.4–3ms 无法定性的差异（采样数不对等）；
- 帧池固定 4 帧：1080p 12.4MB、4K 49.7MB 系统内存。

## 9. 复现命令

```
构建：out\build\veyra-build-x64-release.cmd
测试：veyra_capture_color_tests.exe / veyra_repair_contract_tests.exe /
      veyra_capture_compressed_tests.exe [H.264/HEVC 文件]
门禁：scripts\gates\delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915
MJPEG 真机 2 分钟：veyra.exe "capture:0:24:-1:0" --smoke-seconds 120 --no-nr --no-sr --no-fg   (1080p60)
                   veyra.exe "capture:0:46:-1:0" --smoke-seconds 120 --no-nr --no-sr --no-fg   (4K18)
颜色 A/B：veyra_capture_color_probe.exe "capture:0:24:-1:0" out/color-ab/comp
          veyra_capture_color_probe.exe "capture:0:24:-1:0" out/color-ab/legacy --legacy-rgb
```
