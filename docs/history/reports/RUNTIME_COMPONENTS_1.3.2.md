# Veyra 1.3.2 Runtime Components

发布包组件清单 / Shipped components. NVIDIA and Intel DLLs are separate Release assets and never source Git content. NR and DLSS FG are community-experimental integrations, not vendor endorsement or certification.

## Locations and replacement

NVIDIA files are in `runtime/experimental/`; XeSS/XeLL files are in `runtime_local/intel/experimental/`. Exit Veyra before replacing a DLL and preserve its filename. The manifests record the publisher package only and do not lock user replacement; ABI, API, driver, and hardware compatibility still apply.

允许用户自行替换 DLL，但不保证兼容。删除某个运行组件只会禁用对应增强功能；不需要修改 manifest。完整解压包不包含个人预设、凭据、SDK、模型、PDB、LIB、测试媒体或日志。

## Enhancement runtime records

| Path | Version | SHA-256 | Signature / scope |
| --- | --- | --- | --- |
| `runtime/experimental/nvngx_dlss.dll` | 310.7.0.0 | `BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E` | Valid; DLSS SR |
| `runtime/experimental/nvngx_dlssg.dll` | 310.7.0.0 | `135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F` | Valid; experimental DLSS FG |
| `runtime/experimental/nvngx_dlssnr.dll` | 310.8.0.0 | `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E` | Valid; pinned experimental NR |
| `runtime/experimental/nvngx_vsr.dll` | 1.6.0.0 | `C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED` | Valid; RTX Video SR |
| `runtime_local/intel/experimental/libxess_fg.dll` | 1.3.1.78 | `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27` | Valid; XeSS preview FG |
| `runtime_local/intel/experimental/libxell.dll` | 1.3.2.10 | `D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7` | Valid; XeLL timing |
| `runtime/experimental/nr-community/nvngx_dlssnr.dll` | 310.8.0.0 | `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014` | **HashMismatch**; user-provided RTX40/50 community experimental NR |
| `runtime/experimental/nr-ampere/nvngx_dlssnr.dll` | 310.8.0.0 | `DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F` | **HashMismatch**; user-provided RTX30 experimental NR |

The two community NR binaries are copied unchanged and are explicitly marked `HashMismatch`; their included NVIDIA notices do not establish a general redistribution grant or NVIDIA support. The RTX30 file is an experimental choice, not a claim of verified RTX30 compatibility. Fresh installations leave NR, SR, and internal FG off.

The package includes FFmpeg 9.0.1#1 DLLs, application-local x64 Visual C++ redistributables, Intel license files, AMD FidelityFX optical-flow notices, Lucide notices, Remote Play notices, and compiled shader files. GPU drivers supply `nvofapi64.dll` and `nvEncodeAPI64.dll`; they are not redistributed. ReShade/RenoDX add-ons, FRUC, development SDKs, models, and AMD NR networks are not included.

The shipped FFmpeg includes the Veyra PS5 H.264 32-to-256 slice-capacity patch. `licenses/FFMPEG-VEYRA-BUILD.json` records the binary identity; the separate FFmpeg source asset contains the exact patched source, patch, build configuration, port provenance, and notices.

AV1 playback adds app-local dav1d 1.5.4 (SHA256 `38E09F960822A081FC46FC296FB3EF5F841D1A6C15A39F20684C2F9D88A9FC52`). FFmpeg is rebuilt with `--enable-libdav1d` while retaining the PS5 slice patch; avcodec SHA256 `22D1321B8D31161E7CB0920EC08C43B51884436A066E62C70A0C564ACD4C813E`. The portable package carries dav1d notices and provenance; the FFmpeg source asset includes dav1d source and its vcpkg port. All eight enhancement runtimes keep the previously approved identities.
# Veyra 1.3.1beta 运行组件清单（测试包）

本清单说明测试包里 `runtime/` 与 `runtime_local/` 下的运行组件来源与身份，
与 `release-runtime-manifest.json` 一一对应。1.3.1beta 相对 1.3.0 **新增 AMD FidelityFX 组件**
（MIT 许可，AMD 签名），用于 FSR 帧生成与 FSR 超分；NVIDIA / Intel 组件身份不变。

## 新增（1.3.1beta）

| 文件 | 目录 | 版本 | 类别 |
| --- | --- | --- | --- |
| `amd_fidelityfx_loader_dx12.dll` | `runtime_local/amd/fidelityfx` | 2.3.0.2740 | AMD FidelityFX SDK 2.3.0 loader |
| `amd_fidelityfx_framegeneration_dx12.dll` | `runtime_local/amd/fidelityfx` | 4.0.1.2740 | 帧生成提供方（本机枚举到 3.1.6/3.1.7 能力） |
| `amd_fidelityfx_upscaler_dx12.dll` | `runtime_local/amd/fidelityfx` | 4.1.1.2740 | 超分提供方（本机枚举到 3.1.5/2.3.4） |

三者的 SHA-256、签名状态（AMD 有效签名）与大小见 manifest；许可证文本在
`licenses/AMD-FIDELITYFX-LICENSE.txt`（MIT）。**注意**：这些是 AMD 官方签名产物，
不是 NVIDIA 运行库，不受 NVIDIA EULA 的 Release Runtime Pack 限制。
