# Veyra 0.0.5 Runtime Components

发布包组件清单 / Shipped components. NVIDIA/Intel DLLs are separate Release assets, never source Git content. NR and DLSS FG are community-experimental integrations, not vendor endorsement or certification.

## Locations and Replacement

NVIDIA components: `runtime/experimental/`. XeSS/XeLL: `runtime_local/intel/experimental/`. Exit Veyra before replacing a DLL and preserve its filename. No runtime hash, signature, or manifest lock is enforced. Manifests describe the original package only; users do not need to edit them after replacement. ABI/API and hardware compatibility still determine whether a feature works.

允许自行替换DLL，但替换不代表兼容。出现初始化错误时还原原件或关闭对应增强。清单是发布记录，不是加载锁。退出软件并删除解压目录即可卸载；需要保留设置时先备份`runtime_local`中的设置文件。移除某个增强DLL会使对应功能不可用。

## NVIDIA and Intel

The six original publisher-provided DLLs below have valid Authenticode signatures. The additional community NR variant does NOT: its recorded status is `HashMismatch`. Names and SHA256 describe the approved files used to build this release. The JSON manifests also record file size, signer, source category, and removability.

| File | Fixed file version | Source / use |
| --- | --- | --- |
| nvngx_dlss.dll | 310.7.0.0 | Official DLSS SDK 310.7.0 release runtime; DLSS SR |
| nvngx_dlssg.dll | 310.7.0.0 | DLSS SDK release runtime; experimental Veyra DLSS FG integration |
| nvngx_dlssnr.dll | 310.8.0.0 | User-provided pinned experimental NR runtime; 165840496 bytes |
| nvngx_vsr.dll | 1.6.0.0 | RTX Video SDK 1.1.0; RTX Video SR |
| libxess_fg.dll | 1.3.1.78 | Official Intel XeSS SDK 3.0.2; preview FG |
| libxell.dll | 1.3.2.10 | Official Intel XeSS SDK 3.0.2; XeLL timing |

```text
nvngx_dlss.dll    BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E
nvngx_dlssg.dll   135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F
nvngx_dlssnr.dll  E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
nvngx_vsr.dll     C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED
libxess_fg.dll    EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27
libxell.dll       D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7
```

NVIDIA license files: `licenses/NVIDIA_RTX_SDK_LICENSE.txt`, `NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf`, `NVIDIA_OPTICAL_FLOW_SDK_LICENSE.pdf`. Intel terms: `licenses/INTEL_XESS_LICENSE.txt`, `INTEL_THIRD_PARTY_PROGRAMS.txt`. Experimental NR distribution follows the publisher's explicitly approved Release-only policy; the included official SDK terms are not a claim that the experimental runtime has a general redistribution grant.

## Community NR Variant

The user-selected community NR file is also included, unmodified, in `runtime/experimental/nr-community/nvngx_dlssnr.dll`. Source category: user-provided community-modified RTX40/RTX50 runtime. File version: 310.8.0.0. Size: 165840496 bytes. SHA256: `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`. Authenticode: **HashMismatch**; retained signer metadata does not make this a valid NVIDIA signature.

社区版为用户指定的实验文件，本次明确授权随Release分发；与原版独立存放，默认仍选原版。不能把NVIDIA SDK许可证解释为对社区修改文件的通用授权。未修改、重签名或隐藏该文件。要移除社区版，先切回原版、退出Veyra，再删除`nr-community`目录；主包manifest仅为发布记录，无需修改。RTX40实机兼容性未验证，不将本机RTX5070结果扩大为全系列支持。

Both variants are recorded with distinct paths in `runtime/experimental/release-runtime-manifest.json`. Delete only the community subdirectory to remove that variant. Neither DLL, the SDKs, nor models are part of source Git history.

## Other Dependencies

- FFmpeg 9.0.1#1: `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll`, `swscale-10.dll`; LGPL configuration. Full notices and source provenance are in `licenses/FFMPEG-COPYRIGHT.txt`, `FFMPEG-SPDX.json`, and [BUILD.md](BUILD.md).
- Microsoft `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`: application-local x64 Visual C++ redistributables from the Visual Studio Redistributables directory; needed by FFmpeg and XeSS. Windows11 supplies UCRT.
- AMD FidelityFX optical flow: linked into Veyra, no AMD runtime DLL or NR model required. MIT terms in `licenses/AMD_FIDELITYFX_LICENSE.txt`.
- Lucide icon paths: built into Veyra, with ISC/MIT terms in `licenses/LUCIDE-LICENSE.txt`.
- Own compiled shader files: `shaders/*.dxil`. No local SDK headers or samples are required at runtime.

GPU drivers supply `nvofapi64.dll` and `nvEncodeAPI64.dll`; they are not redistributed. ReShade/RenoDX add-ons, FRUC, SDK archives, development libraries, depth weights, and AMD NR networks are not part of this package.


## PS5 Remote Play in 0.0.5

Chiaki core, SDL3, Opus, OpenSSL, json-c, libevent, miniupnpc, curl, nanopb, Jerasure and gf-complete are linked statically; no extra controller SDK installation is required. The pinned Chiaki revision is 0e16950165f06e5c3291537c2eeba6e852be7120. The combined executable must comply with AGPLv3 and upstream OpenSSL exception, in addition to component notices. See licenses/remoteplay/ in the package, THIRD_PARTY_NOTICES.md, and the release's RemotePlay-source archive for source, build recipes, provenance and dependency notices. Pairing credentials and personal configuration are never packaged.
