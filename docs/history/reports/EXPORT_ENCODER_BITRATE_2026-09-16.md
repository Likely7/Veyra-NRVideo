# 2026-09-16 导出编码器多厂商化 + 可调码率（隔离分支）

用户指令："能不能换编码？让全部都支持导出？还有加个可以调整码率的功能"。
分支 `codex/framegen-fsr-dolby-20260916`；2026-09-16 已随该分支合并进 main（merge `24e7de7`）；未推送、未发布。

## 1. 之前的现状（事实）

- 导出硬性要求 NVIDIA：`VideoExportJob` 在非 N 卡上直接报"当前视频导出使用 NVIDIA
  NVENC；尚未实现 AMD AMF 或 Intel 编码后端"并放弃，AMD/Intel 用户**一个文件都导不出**。
- 我们的 FFmpeg 是**只解不编**的定制构建
  （`C:\veyra-deps\ffmpeg-ps5-dav1d-installed`：`CONFIG_LIBX264_ENCODER=no`、
  `CONFIG_H264_AMF_ENCODER=no`、`CONFIG_H264_QSV_ENCODER=no`、`CONFIG_H264_MF_ENCODER=no`），
  所以"用 FFmpeg 的 amf/qsv/mf/x264 编码器"必须重建 FFmpeg，代价与许可证面都更大。
- 本机（RTX 5070）`ffmpeg -encoders` 显示 `h264_mf`/`hevc_mf` 可用、`hevc_mf` 实测失败
  （NVIDIA 不提供 HEVC 的 MF 编码器），`h264_mf` 实测 exit 0 —— MF 是**可验证**的通用路径。

## 2. 本轮实现

### 2.1 编码器抽象（`include/veyra/sink/VideoEncoder.h`）

`VideoEncoder` 接口 + `openVideoEncoder()` 工厂：**NVIDIA 适配器走 NVENC（D3D12 零拷贝），
其余适配器走 Media Foundation 硬件 MFT**；NVENC 会话被拒（旧/混杂 `nvEncodeAPI64.dll`、
该卡不支持的编码格式）时自动回落到 MF，而不是让用户丢掉整个导出。

### 2.2 Media Foundation 编码器（`src/sink/MfVideoEncoder.cpp`）

移植 FFmpeg `mfenc.c`（LGPL-2.1+）的 MFT 流程，见 `THIRD_PARTY_NOTICES.md`。要点：

- `MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE|SORTANDFILTER)`；
- **按显卡厂商优选**：本机枚举同时出现 `AMDh264Encoder`（驱动带着）与 `NVIDIA H.264 Encoder MFT`，
  取第一个会在 A 卡驱动上得到 `MF_E_HW_MFT_FAILED_START_STREAMING (0xC00D6D76)`。现在按
  vendor 关键字排序并对每个候选做完整类型协商，取第一个成功的；
- NV12 输入来自 Veyra 自己的 D3D12 图：`RgbToNv12` 着色器 → READBACK 缓冲 → 紧凑打包
  （MFT 只接受系统内存 NV12）；
- 码率通过 `ICodecAPI`：`MeanBitRate`/`MaxBitRate`/`BufferSize`(0.5s CPB) +
  `PeakConstrainedVBR`；`LowLatencyMode`、`GOPSize`、`QualityVsSpeed` 同步设置；
- 输出样本：异步 MFT 自己分配 `out.pSample`（这条曾导致 300 帧全部被丢弃，已修）；
- 首个样本与 GOP 边界用 `CODECAPI_AVEncVideoForceKeyFrame` 强制关键帧。

### 2.3 码率（NVENC + MF）

- `EnhancementSettings.exportBitrateMbps`（0 = 编码器恒定质量档，上限 300）；
  该字段**不参与** `sameVideoConfiguration`，改码率不会重建预览管线；
- NVENC：`NV_ENC_PARAMS_RC_VBR`，`averageBitRate=maxBitRate=target`，
  `vbvBufferSize = target/2`（半秒）；0 时沿用 `CONSTQP 20/22/22`；
- MF：上面的 `PeakConstrainedVBR`；
- 预设格式 v14 → **v15**（行尾追加码率字段；解析顺序必须与写出顺序一致，本轮踩过一次）；
- UI：设置面板"导出"页新增"导出码率"下拉（自动/6/10/16/24/40/60/100/150/200 Mbps）；
- CLI：`--bitrate-mbps N`。

### 2.4 非 N 卡特性门控

导出图的 `enableNr/enableSr/enableNvofStandalone/enableFg` 现在与预览同规则：
DLSS NR / DLSS SR / DLSS FG / NVOF 仅 NVIDIA；AMD FSR 超分（`--video-sr 5`）是
唯一厂商中立的超分路径，非 N 卡请求 DLSS 系功能时**降级并在提示/日志中写明**，不再整任务失败。

## 3. 本机证据（RTX 5070）

| 场景 | 命令要点 | 结果 |
| --- | --- | --- |
| NVENC 码率 | `--bitrate-mbps 6` / `40` | 实际 6.11 / 32.8 Mbps（简单内容低于目标属正常 VBR） |
| MF 硬件编码 | `VEYRA_TEST_FORCE_MF_ENCODER=1 --bitrate-mbps 10` | 300 帧写满、`export-verify decoded=300 passed=true`、实际 10.16 Mbps |
| MF 30 Mbps | 同上 `--bitrate-mbps 30` | 26.1 Mbps、验证通过 |
| NVENC 全拒 → 回落 | `VEYRA_TEST_NVENC_FIRST_OPEN_FAILS=2` | 自动选 MF，16 Mbps 实际 15.2 Mbps，exit 0 |
| MF + 补帧 | `--fg-xess --fg-multiplier 2` | XeSS 替换为 DLSS 2X，600 帧 @60fps、验证通过 |
| HDR + MF | `pq-tagged-51.mp4 --hevc` | 明确拒绝（8bit MFT 不支持 Main10），无 partial/final |
| HDR + NVENC | 同素材 | 30 帧 HEVC Main10、验证通过（无回归） |

自动检查：修复合同 169 项 0 失败；预设往返（新增码率字段）66 组 legacy 迁移 + 全字段往返 PASS；
delivery 短测 PASS `logs/delivery/35f4f46808fb4236856701704e5a7f52/result.json`。

## 4. 边界（如实）

- **MF 路径只支持 8bit 4:2:0**：HDR（HEVC Main10）导出仍然只有 NVENC；非 N 卡导出 HDR 会
  明确报错而不是偷偷输出 SDR。非 N 卡的 HEVC 取决于该驱动是否提供 HEVC MFT（Intel/AMD 通常提供）。
- **性能**：MF 输入走"GPU 渲染 → readback → CPU 打包"，4K60 输出会有可观的 PCIe 与内存拷贝
  开销（每帧约 12 MB）。后续可优化为 D3D12 共享纹理 + D3D11 互操作，本轮未做。
- **未在 AMD/Intel 实卡上验证**：本机只有 N 卡，MF 路径是用 NVIDIA 的 MFT 跑通的；
  AMD/Intel 走同一条 API，但驱动差异（支持的 profile/码率控制/GOP 设置）需要用户实机复测。
- **AMF/QSV 原生后端未接入**：MF 走的就是这两家驱动里的编码器，原生 SDK 只提供更细的控制，
  不是"能不能导出"的必要条件。
- 软件编码（x264/x265）未接入：需要重建带编码器的 FFmpeg 或引入 openh264/x264 依赖，
  且 4K 软件编码速度不现实；当前策略是"任何现代显卡的驱动 MFT"。

## 5. 测试包（已合并 main / 未推送 / 未发布）

`C:\veyra-test-packages\final-encoders-r6\Veyra-1.3.1beta-win64-portable.zip`，
469,804,907 字节，SHA256
`B2D6BFE2FD62772E87DD3E17B60BC9DCA0B18CBFAFA952D7CAC786A1F6250E5A`；包内 `Veyra.exe`
SHA256 `DE306CB03A81A8AD3904FF0CB331EB124EAF567B3640A5479C85ED55DDE414DB`（与构建树一致）。
包内 EXE 实测：NVENC 20 Mbps 导出 300 帧验证通过；强制 MF 12 Mbps 导出 300 帧验证通过。

## 6. 给 AMD / Intel 测试者的最小复现步骤

1. 打开任意本地视频 → 专业模式 → 导出页 → 编码器不用选（自动），码率选 20 Mbps →
   选择一个输出路径导出；
2. 日志（`logs\export-worker-<pid>.log`）应出现
   `[export] encoder=MediaFoundation-MFT ...` 与 `[mf-encoder] selected MFT <驱动编码器名>`；
3. 若失败，把该日志与 `[mf-encoder] candidate[...]` 行发回：那里会列出该机器上所有候选
   MFT 名称与协商结果，足以定位是驱动没有编码器还是类型协商被拒。
