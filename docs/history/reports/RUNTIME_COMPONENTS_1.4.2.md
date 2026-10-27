# Veyra 1.4.2 Runtime Components

发布包组件清单 / Shipped components. NVIDIA and Intel DLLs are separate Release assets and never source Git content. NR and DLSS FG are community-experimental integrations, not vendor endorsement or certification.

1.4.2 正式包沿用 1.4.2beta 已审计的全部组件身份，包括以下官方 TrueHDR 原件，不修改磁盘 DLL。本轮设备帧率与 UI 修复没有新增运行组件。

TrueHDR 来源：RTX Video SDK 1.1.0，文件 nvngx_truehdr.dll，3955752 bytes，版本 1.1.0.0，签名 Valid（NVIDIA），SHA256 `9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B`。放在 runtime/experimental/，许可证为 licenses/NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf，单独登记 manifest。用户于 2026-09-19 授权随 1.4.2 GitHub Release 分发该原件。

NVIDIA FSR4 INT8 研究 provider 已撤回，不在包内；官方 AMD upscaler 保留原文件与原有 FSR3 能力。
RTX30/40 补帧兼容修复在进程内完成，RTX50 保持原生路径；FSR 补帧界面入口继续隐藏。
对应移植代码的许可证见包内 RTX40MFG_LICENSE.txt 和 HDE_LICENSE.txt。

## Locations and replacement

NVIDIA files are in `runtime/experimental/`; XeSS/XeLL files are in `runtime_local/intel/experimental/`.
Exit Veyra before replacing a DLL and preserve its filename. The manifests record the publisher package
only and do not lock user replacement; ABI, API, driver, and hardware compatibility still apply.

允许用户自行替换 DLL，但不保证兼容。删除某个运行组件只会禁用对应增强功能；不需要修改 manifest。
完整解压包不包含个人预设、凭据、SDK、模型、PDB、LIB、测试媒体或日志。

## Enhancement runtime records

| Path | Version | SHA-256 | Signature / scope |
| --- | --- | --- | --- |
| `runtime/experimental/nvngx_dlss.dll` | 310.7.0.0 | `BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E` | Valid; DLSS SR |
| `runtime/experimental/nvngx_dlssg.dll` | 310.7.0.0 | `135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F` | Valid; experimental DLSS FG |
| `runtime/experimental/nvngx_dlssnr.dll` | 310.8.0.0 | `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E` | Valid; pinned experimental NR |
| `runtime/experimental/nvngx_truehdr.dll` | 1.1.0.0 | `9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B` | Valid (NVIDIA); RTX Video HDR |
| `runtime/experimental/nvngx_vsr.dll` | 1.6.0.0 | `C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED` | Valid; RTX Video SR |
| `runtime_local/intel/experimental/libxess_fg.dll` | 1.3.1.78 | `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27` | Valid; XeSS preview FG |
| `runtime_local/intel/experimental/libxell.dll` | 1.3.2.10 | `D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7` | Valid; XeLL timing |
| `runtime/experimental/nr-community/nvngx_dlssnr.dll` | 310.8.0.0 | `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014` | **HashMismatch**; user-provided RTX40/50 community experimental NR |
| `runtime/experimental/nr-ampere/nvngx_dlssnr.dll` | 310.8.0.0 | `DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F` | **HashMismatch**; user-provided RTX30 experimental NR |
| `runtime_local/amd/fidelityfx/amd_fidelityfx_loader_dx12.dll` | 2.3.0.2740 | 见 manifest | Valid (AMD); MIT；FSR 提供方装载器 |
| `runtime_local/amd/fidelityfx/amd_fidelityfx_framegeneration_dx12.dll` | 4.0.1.2740 | 见 manifest | Valid (AMD)；**1.4.0 起界面不再提供 FSR 补帧入口**，后端保留 |
| `runtime_local/amd/fidelityfx/amd_fidelityfx_upscaler_dx12.dll` | 4.1.1.2740 | 见 manifest | Valid (AMD)；FSR 超分（3.1.x 能力） |

The two community NR binaries are copied unchanged and are explicitly marked `HashMismatch`; their
included NVIDIA notices do not establish a general redistribution grant or NVIDIA support. The RTX30 file
is an experimental choice. Affected users confirmed the 1.4.1 repairs work on their
RTX30/40 systems; this is not validation of every model or driver. Fresh installations leave NR, SR,
and internal FG off.

The package includes FFmpeg 9.0.1#1 DLLs, application-local x64 Visual C++ redistributables, Intel license
files, AMD FidelityFX notices, Lucide notices, Remote Play notices, and compiled shader files. GPU drivers
supply `nvofapi64.dll` and `nvEncodeAPI64.dll`; they are not redistributed. ReShade/RenoDX add-ons, FRUC,
development SDKs, models, and AMD NR networks are not included.

The shipped FFmpeg includes the Veyra PS5 H.264 32-to-256 slice-capacity patch.
`licenses/FFMPEG-VEYRA-BUILD.json` records the binary identity; the separate FFmpeg source asset contains
the exact patched source, patch, build configuration, port provenance, and notices.

AV1 playback adds app-local dav1d 1.5.4 (SHA256 `38E09F960822A081FC46FC296FB3EF5F841D1A6C15A39F20684C2F9D88A9FC52`).
