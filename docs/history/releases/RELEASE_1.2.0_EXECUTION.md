# 1.2.0 发布记录

用户于 2026-09-14 授权发布 Veyra 1.2.0 至 `Likely7/Veyra-NRVideo`。本次以 1.1.1 的远端 `main` 为祖先，整合 HDR、5.1 PCM 与 SDR 预览开关。发布前建立本地版本提交和 tag；GitHub 资产上传后必须逐项核对字节数与 SHA-256 digest。

## 范围

- HDR10 / PQ、HLG 与 BT.2020 NCL 文件及 P010/P016 采集输入；设备未报告时可手动选择 PQ/HLG。
- 在 HDR 保留链路中组合 NR、DLSS SR / RTX Video SR、DLSS / XeSS 预览补帧；HEVC Main10 / PQ 导出和 FP16 scRGB JPEG XR 截图。
- 文件和采集 PCM 最多 5.1 的声道位置、时钟、音量与明确下混。
- 采集面板“转为 SDR 显示”：默认关闭、无需重连实时切换、不影响真实输入颜色元数据或 HDR 视频导出。

这不是对所有 HDR 显示器、采集卡、5.1 端点、PS5 HDR、Dolby/DTS 码流直通或 Atmos 的全量承诺。PS5 串流保持上游提供的立体声；压缩 Dolby/DTS 与 Atmos 对象音频不在本次范围。HDR NR/RTX Video SR 是 SDR 代理与 HDR 基底的保留合成，不宣称原生 HDR 模型推理。

## 构建与验证

- `cmd /c out\\release-1.1.0-build.cmd`：通过，EXE ProductVersion / FileVersion 均为 1.2.0，SHA-256 `E49D90E1217E0DE1B59C9C889DE318DC46337DF51FCF838038D7F5AF896EE457`。
- `scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair`：23/23 通过，48.85 秒；证据 `logs/delivery/f51f2a1bc24b4a69b0d38d2f9c19406d/result.json`。真实 RTX 5070 NR CreateFeature id 18 返回 `0x1`、SEH 0、60 次 Evaluate，NVOF 非零运动；4K、图片、H.264 / HEVC 导出与取消回归通过。
- `veyra_hdr_enhancement_tests.exe` 模式 1、3、4、5、6：每种 20 次真实 NR；DLSS / RTX Video SR / XeSS 与两种处理顺序均通过。生成帧分别为 18、18、16、18，约 999 nit 高光保持；`logs/release-1.2.0/hdr-mode-*.txt`。
- `veyra_hdr_color_tests.exe`：30 个采集颜色 GPU 用例与 16 个 PQ/HLG、planar/P010、range、HDR/SDR 输出组合通过；`logs/release-1.2.0/hdr-color.txt`。
- 预设版本 v12 的旧 v1–v11 迁移、SDR 开关 roundtrip、关闭增强时仍生效、UI 输出策略合同通过。六声道 28 检查和捕获抖动 5.1 测试通过；`logs/release-1.2.0/multichannel.txt`、`capture-audio-jitter.txt`、`audio-timeline.txt`。
- 最终便携包解压到独立目录、隔离 PATH、临时移走 manifest 后测试 7/7 通过：默认空载 / 基础、原版与 RTX40 社区 NR + DLSS、Video SR、首次默认全关、RTX30 实验 NR。证据 `logs/release-1.2.0-upload/portable-smoke/result.json`。这证明包内实际 DLL 路径与基础启动，不代替 RTX30、HDR 屏或采集卡实测。

## 资产与审计

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `Veyra-1.2.0-win64-portable.zip` | 421736651 | `0A0D65DA75BE45F79587C1CBABE33062AFB32D2007F59E9270DFDDDA0973687E` |
| `Veyra-1.2.0-RemotePlay-source.zip` | 143745490 | `9E6494F2EC510C4E793B341B64AA463513E40A0F5EB0C1ABF87D0734F3C0C273` |
| `Veyra-1.2.0-FFmpeg-source.zip` | 23253198 | `BC2F5ECF1CAE1D5398A239985CA9895EC58F0DCE414D1C9766A1840B224CF402` |

每个 ZIP 都附有同名 `.sha256`。`logs/release-1.2.0-upload/archive-audit.json` 已逐文件重算三个 ZIP、核对包 manifest、禁止的源码/SDK/运行库开发文件、八运行组件及两个 `HashMismatch` 社区 NR 文件。便携包共 101 文件（100 个 manifest records）；Remote Play 对应源码 17714 文件；FFmpeg 对应源码 10451 文件。无凭据、个人配置、日志、测试媒体、PDB、LIB、SDK 或模型进入资产或源码 Git。

运行时身份沿用 1.1.1 的八个批准记录，详情见 `RUNTIME_COMPONENTS_1.2.0.md`。六个原件签名 Valid；RTX40/50 与 RTX30 社区 NR 均为 `HashMismatch`，分别保留 SHA-256、版本、大小与实验标记。没有修改、重签名或替换它们。patched `avcodec-63.dll` SHA-256 仍为 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`；FFmpeg 对应源码包包含实际 patched tree、补丁、构建记录及许可证。

真实 HDR 显示效果、HDR 采集卡、5.1 扬声器定位、PS5 新会话与 RTX30/40 实卡仍未在本轮发布前复测。不得据此宣称所有硬件已验收。
