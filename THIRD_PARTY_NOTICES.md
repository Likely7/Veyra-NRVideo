# Third-party dependencies

NVIDIA SDKs and runtimes are excluded from source control. The publisher-authorized experimental Release package contains only selected runtime DLLs and applicable notices, as documented in `docs/RUNTIME_COMPONENTS_0.0.2.md`. This is not vendor endorsement or a general redistribution grant. ReShade/RenoDX add-ons are not loaded or distributed by the product.

## AVerMedia capture HDR-to-SDR control

The AVerMedia hardware HDR-to-SDR property request in
`src/source/CaptureCardSource.cpp` is adapted from `source/device-vendor.cpp`
(`SetTonemapperAvermedia`) in [obsproject/libdshowcapture](https://github.com/obsproject/libdshowcapture/tree/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22),
fixed commit `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`, Copyright (C) 2023
Lain Bailey, LGPL-2.1-or-later (see `licenses/capture/LIBDSHOWCAPTURE_LGPL.txt`). Veyra restricts
the protocol to the selected AVerMedia filter, logs Set/Get results, and applies
the wide/narrow format policy at each connection before SetFormat. Unsupported
properties remain an explicit warning. This does not prove the GC573 15 FPS
cause or provide unsupported Elgato 4K X HID control.

## Elgato MK.2 capture control

MIT adaptations in `src/source/ElgatoHdrControl.cpp` and its header:

- https://github.com/elgatosf/capture-device-support at
  `fe9630974d47f51bf54826e72fb8b654e620aa93`, `SampleCode/DriverInterface.cpp`
  and `Library/HDMIInfoFramesAPI.h`: private property GUID, IDs 720/721/722,
  two-part HDR packet and hardware HDR-to-SDR control protocol. Copyright
  (c) 2022 Corsair Memory, Inc.
- https://github.com/nitlink-dev/nitlink at
  `4f8a723406f63c6e92832488a6022fcf25233871`,
  `src/capture/elgato_hdr_control.cpp` and `elgato_device_identity.h`:
  exact MK.2 aliases, packet read size checks and hardware/native HDR pairing.
  Copyright (c) 2026 NitLink Contributors.

Veyra changes: use the already selected DirectShow filter instead of opening
another device; scope to MK.2/P010; validate packet length/type/checksum;
retain manual overrides; log HRESULT/readback and restore a readable initial
tonemap value on close. No vendor shader, SDK header or binary is vendored.
No global color/chroma formula change. Full license:
`licenses/capture/ELGATO_NITLINK_MIT.txt`.

## NVENC API declarations

Source: https://github.com/FFmpeg/nv-codec-headers ; encoder ABI baseline `e844e5b26f46bb77479f063029595293aa8f812d` (tag `n13.0.19.0`, SDK 13.0 declarations), stored only under ignored `third_party_local/nvidia/nv-codec-headers-13.0`. Stage with `git clone --depth 1 --branch n13.0.19.0 https://github.com/FFmpeg/nv-codec-headers.git third_party_local/nvidia/nv-codec-headers-13.0`. CMake accepts `VEYRA_NVENC_HEADERS_ROOT`; compilation checks major/minor 13.0. The previous 13.1 checkout `eddcea9e27f6b772057c9b3f87de2cc1737faffc` remains unmodified for other local tools. Veyra now queries the driver's maximum API before creating the 13.0 function table/session and uses the same ABI for every structure, without pretending that a version-number-only downgrade changes structure layouts. No header or runtime is copied into source control.

The nvEncodeAPI.h header itself has NVIDIA's permissive MIT-style notice (Copyright 2010–2024 NVIDIA Corporation in the pinned 13.0 header). Its full notice is retained unmodified in that header. This permits using the declarations without retrieving the full developer-portal sample package; it does not grant rights to distribute NVIDIA driver/runtime binaries. Veyra uses the system NVENC library, never copies it into a package. Implementation is independently authored against these declarations; no competitor/sample implementation copied.

FFmpeg: dynamically linked 9.0.1#1 vcpkg build. The portable package carries five FFmpeg DLLs, the complete copyright/license notices and SPDX provenance. Corresponding upstream source and the vcpkg patch/build recipe are listed in `docs/BUILD.md`. No FFmpeg command-line executable or test-media toolchain is shipped.

The PS5 H.264 repair build additionally applies `scripts/ffmpeg/ps5-h264-slices.patch` to FFmpeg's LGPL `libavcodec/h264dec.h`, increasing the bounded slice capacity from 32 to 256. This is a Veyra modification, not an upstream release claim. Its corresponding source, patch, configuration and DLL identity record must accompany any future binary release; the previously published 0.0.5 package is unchanged by this local repair.

## Intel XeSS / XeLL

Official XeSS SDK 3.0.2. Veyra loads `libxess_fg.dll` and `libxell.dll` for experimental preview frame generation. Unmodified binaries may be redistributed under the Intel Simplified Software License; the complete license and `third-party-programs.txt` accompany the package. User DLL replacement is allowed by Veyra without fixed identity locks; compatibility is not guaranteed.

### XeSS multi-frame unlock (ported, process-memory only)

Source: https://github.com/Coldwood1026/OptiScaler , commit `70676c5f037c8c26f1ec355b250a72303cd268da`, files `OptiScaler/proxies/XeFGUnlock.h` and `XeFGPacing.h` (GPL-3.0). The five byte patches that raise the provider's generated-frame ceiling on non-Intel GPUs are ported into `include/veyra/gfx/XessMfgUnlock.h` / `src/gfx/XessMfgUnlock.cpp`; the structural design (module identity checks, transactional install, rollback after the contexts exit) follows `SAOG0721/Magpie` (`experimental` branch, GPL-3.0, `XeSSFGCompatibility.h` / `XeSSFGPatchTransaction.h`). Veyra adds size + SHA-256 + PE identity checks and refuses unaudited provider builds.

The provider DLL on disk is never modified, re-signed or renamed; only the mapped image of the process is patched, and every patched byte is restored when the XeFG/XeLL contexts are destroyed. Because Veyra itself is GPLv3, the ported GPL-3.0 code is compatible; the upstream authorship above is attributed here. Locked provider identity: `libxess_fg.dll` 1.3.1.78, 22,957,432 bytes, SHA-256 `EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27`, PE TimeDateStamp `0x69CB0F4D`, SizeOfImage `0x015ED000`.

2026-09-30 changes: `src/gfx/XessMfgUnlock.cpp` now stores the actual byte
string before forming the mismatch diagnostic, avoiding iterators into two
different temporaries; `src/gfx/XessPresenter.cpp` logs the unlock result detail.
The five patch locations and transaction remain unchanged. They were compared
with the renamed upstream [OptiScalerDp4aUnlock](https://github.com/Coldwood1026/OptiScalerDp4aUnlock),
commit `9eea95bba9fda7121f214d2eba358423be598d7e` (GPL-3.0).
The QML selector exposes experimental 3X/4X only for an audited provider and
uses the running SDK capacity when available. This is not official Intel
support for non-Intel multi-frame generation or proof for every AMD adapter.

XeSS pacing adaptation (2026-09-19): `include/veyra/gfx/XessPacing.h` and `src/gfx/XessPacing.cpp` also adapt the above pinned OptiScaler `XeFGPacing.h` NoteFrame/PaceFrame/WaitUntil logic: a bounded 15-period median, generated-frame deadlines, and the provider-owned tail limiter condition. Veyra retains its audited call-site hooks, adds synchronized statistics and complete hooked-present-return gap measurements, and does not port upstream timestamp hooks. Provider scheduling remains preferred; wall-clock pacing is used only when its scheduler is unavailable. No on-disk runtime changes.

XeSS read-only timing diagnostics (2026-09-21): the timestamp callback ABI and
pinned provider locations (0x3430 / 0x224B30) were cross-checked against
SAOG0721/Magpie commit `3841698348bfb246623d4acf791984c8b68a577b`,
`src/Magpie.Core/XeSSFGPacing.h` (GPL-3.0). Veyra's trace-only hook records
the native deadline without modifying it. The upstream deadline rewrite is
not ported. The audited DLL remains unchanged on disk.

Read-only fence attribution additionally checks the same pinned provider's
scheduler at 0x21EE30: context+0x328 is the ID3D12Fence queried at 0x21EECF,
and timestamp lookup+0x18 holds the target tested at 0x21EEEB. Veyra samples
GetCompletedValue around its synchronous scheduler call only when tracing;
no fence value, event, deadline or queue submission is modified.

### Magpie NR temporal residual route (2026-09-20)

Veyra's optional `src/pipeline/NrTemporalPass.cpp` and
`shaders/NrTemporal.hlsl` adapt the motion-guided residual-history design from
SAOG0721/Magpie, commit
`3841698348bfb246623d4acf791984c8b68a577b`,
`src/Magpie.Core/DLSSNRTemporalShader.h` (GPL-3.0). The Veyra version keeps
the existing D3D12 graph, uses its source motion extent and signed HDR working
space, and adds its own resource/reset plumbing; it is not a binary or runtime
copy. The 2026-09-21 adaptation distinguishes explicit protection masks from
valid zero residual observations, excludes protected/feathered history, and
rejects invalid source time intervals. A Veyra groupshared tile reuses the
upstream-derived neighborhood observations without changing their tap order.
Product D3D12 regression coverage is in
`tests/integration/NrTemporalGpuTests.cpp`. The switch is default-off until affected RTX hardware and motion-scene
quality tests reject ghosting and flicker regressions.

## DLSS NR default runtime: community Lecram 310.8.3 (2026-09-29)

The default NR slot (`runtime/experimental/nvngx_dlssnr.dll`) holds a community-modified build of
NVIDIA's `nvngx_dlssnr.dll`, file version 310.8.3.0, SHA-256
`F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC`, 165840496 bytes, obtained by the user
from https://github.com/RankFTW/rhi-repo/releases/tag/dlssnr-310.8.Lecram (archive SHA-256
`ddb64e5545ba3f217c32555d2eb8fdfe76b9510d2256f13e8a5ecb1a9650ca54`). Compared with NVIDIA 310.8.0.0 it differs
in the `.data` section that holds the GPU kernels (and 25 bytes of `.text`); the model weights are unchanged.
Its Authenticode status is HashMismatch: NVIDIA's certificate over modified content, so it is not an
NVIDIA-signed original and is not endorsed by NVIDIA. Veyra loads it unmodified by absolute path; it is
never committed to source control. On an RTX 5070 its NR output was byte-identical to 310.8.0.0 on matched
frames and about 2% cheaper. NVIDIA's rights in the runtime and the model are unaffected by Veyra's license.

## DLSS NR RTX30 runtime: community SF-v2 310.8.2 (2026-09-29)

The RTX30 NR slot (`runtime/experimental/nr-ampere/nvngx_dlssnr.dll`) holds ShortFuse's community-modified
build of NVIDIA's `nvngx_dlssnr.dll`, version string 310.8.SF.0 (numeric 310.8.2.0), SHA-256
`6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927`, 165830144 bytes, unsigned, obtained by the user
from https://github.com/RankFTW/rhi-repo/releases/tag/dlssnr-310.8.SF-v2 (archive SHA-256
`1da35941894994eb087e017577829e492454e9bae3a6a9397027069ceb74955c`). It changes the kernel and model-weight
sections to provide an FP16 route for RTX 20/30. On an RTX 5070 its NR output was byte-identical to NVIDIA's 310.8.0.0 on matched
frames; the RTX 20/30 route has not been measured locally. Veyra
loads it unmodified by absolute path and does not rewrite the reported GPU architecture for this build. It is
never committed to source control and is not endorsed by NVIDIA.

## AMD FidelityFX Optical Flow

FidelityFX SDK 1.1.4, upstream commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK . The optical-flow and DX12 backend libraries are statically linked. Copyright (C) 2024 Advanced Micro Devices, Inc.; MIT license, reproduced in the package's `licenses/AMD_FIDELITYFX_LICENSE.txt`. This is optical flow, not AMD NR or AMD super resolution.

## Microsoft Visual C++ Runtime

The package includes unmodified x64 `vcruntime140.dll`, `vcruntime140_1.dll`, and `msvcp140.dll` from the Visual Studio 2022 C++ Redistributables directory for FFmpeg and XeSS. These are Microsoft Distributable Code under the [Visual Studio software terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/); Windows system and GPU driver DLLs are not copied. Veyra itself uses the static MSVC runtime.

## Lucide UI icons

Source: https://github.com/lucide-icons/lucide/tree/a537cb6eb323b885f4c60baf3cec1a995982d167

24 native icon mappings use 23 Lucide SVGs, retained under `assets/icons/lucide/`. Lucide is ISC-licensed (Copyright 2026 Lucide Icons and Contributors); its Feather-derived subset is MIT-licensed (Copyright 2013-present Cole Bemis). The complete upstream notices are preserved in `assets/icons/lucide/LICENSE` and must accompany any distribution containing these icons.

`assets/icons/lucide/manifest.json` records the pinned revision and per-file SHA256. `scripts/generate-lucide-icons.py` converts the SVG geometry to GDI+ paths in `apps/veyra/ui/LucideIcons.h`, using development-only fonttools 4.64.0. Veyra requires no fonttools, network request, icon font or external icon runtime.

The Qt/QML interface (2026-09-26) draws the Lucide geometry that the approved prototype carries in `prototypes/ui-redesign-2026-09-25/icons.js`. `tools/qt_probe/make-icons.py` converts those SVG elements to path data in `qml/Veyra/IconData.js`, rendered by `qml/Veyra/VIcon.qml`; the same Lucide ISC / Feather MIT notices in `assets/icons/lucide/LICENSE` apply. `back10`, `fwd10`, `playfill` and `pausefill` in that file are the prototype's own inline SVGs, not Lucide.

## RTX Video SDK 1.1 local VSR adapter

User-provided official SDK from https://developer.nvidia.com/rtx-video-sdk/getting-started, kept under ignored third_party_local/nvidia/RTX_Video_SDK_1.1.0. Its NVIDIA RTX SDK license remains applicable. VideoSrBackend is independently authored against the documented VSR parameter ABI; no SDK sample implementation or proprietary header is copied into the repository. Local nvngx_vsr.dll remains excluded from Git. Under the user-authorized 2026-09-09 Release Runtime Pack policy, its pinned signed release copy may be included only as a Release asset alongside the applicable SDK license and manifest; it is never committed to source control.

## NVIDIA FRUC

FRUC was evaluated during development and has been removed from the product. No FRUC runtime, worker, SDK headers, or binaries are built or packaged.

## PS5 Remote Play / chiaki-ng (0.0.5)

The optional `VEYRA_ENABLE_REMOTEPLAY` build compiles chiaki-ng at
`0e16950165f06e5c3291537c2eeba6e852be7120` from https://github.com/streetpea/chiaki-ng,
with the two reviewed patches in `scripts/remoteplay/patches/`. Chiaki code and
these derived patches retain AGPL-3.0-only with the upstream OpenSSL exception;
see `licenses/remoteplay/CHIAKI_AGPL3_OPENSSL.txt`. The metadata patch was adapted
from the user-supplied Code 01 change description and checked against this pin.
No Chiaki binary or third-party SDK is committed. The 0.0.5 distribution provides its corresponding complete source,
these patches, dependency pins, build instructions and upstream notices; the
existing Veyra GPL file alone is not the combined program's license record.

Gamepad input uses SDL 3.4.14 (https://github.com/libsdl-org/SDL/tree/release-3.4.14),
statically built using vcpkg. SDL provides DualSense and other controller device
support; Veyra maps its public gamepad API to Chiaki semantic input. SDL also routes DualSense haptic audio; media playback remains in the shared Veyra engine. Retained notices: `licenses/remoteplay/SDL3_NOTICES.txt`.
The 0.0.5 Release contains the combined executable. Its application source is the release tag; the RemotePlay-source asset supplies pinned upstream sources and build recipes.
# Remote Play controller additions (2026-09-12)

Veyra calls the pinned Chiaki orientation tracker API (AGPL-3.0-only with OpenSSL exception). DualSense SDL report offsets and 3 kHz stereo to four-channel haptic routing were checked against chiaki-ng gui/src/controllermanager.cpp and streamsession.cpp. Existing Chiaki and SDL license notices apply. No third-party binary was added.

### PSN browser authorization adapter (2026-09-12)

`src/remoteplay/PsnAuth.cpp` adapts the OAuth request contract and public client identifiers from chiaki-ng, commit `0e16950165f06e5c3291537c2eeba6e852be7120`, `gui/src/psnaccountid.cpp`, `gui/include/psnaccountid.h`, and `gui/src/psntoken.cpp`. Upstream credits the Account ID script to grill2010. License: AGPL-3.0-only with the upstream OpenSSL exception. The Windows HTTP, protected storage and UI integration are Veyra code. This is an unofficial client, not Sony endorsement. json-c remains an existing Chiaki dependency; its license/notice must remain in binary distributions.

Static dependency notices and installed SPDX records: licenses/remoteplay/{json-c,libevent,miniupnpc,openssl,opus,sdl3}/. Upstream curl, nanopb, Jerasure and gf-complete notices are in the same directory. Their corresponding sources and port recipes accompany the RemotePlay-source release asset.

## GPU DIS optical flow (experimental)

Source: https://github.com/gggz114514-oss/XeSS-GPU-Motion , commit
`cb7523b5104fc914dc501767c3139b43c2067af7` (public R4.2 snapshot).
The DIS provider and its shader closure are included under `third_party/gpu-dis/`.
Owned additions use Apache-2.0; OpenCV-derived portions retain their BSD/Apache
terms and Intel/Willow Garage/other upstream attribution. See that directory's
LICENSE, NOTICE, PROVENANCE.md, VEYRA_INTEGRATION.md, and licenses/.
Veyra adds GPU input/consumer adapters and descriptor staging; no Intel SDK,
worker dependencies or proprietary runtime is included in this source subset.
This is a motion-estimation option, not Intel XeSS frame generation itself.

## Acknowledgments and Project Sources

Thank you to these projects and their contributors. Integrated code, dependencies, and development references are identified separately below. Pinned versions, modifications, and licenses are recorded in the component sections above.

### Integrated Code and Dependencies

| Project | Contribution to Veyra |
| --- | --- |
| [chiaki-ng](https://github.com/streetpea/chiaki-ng) | PS5 Remote Play protocol, pairing and sessions; controller, haptics and PSN authorization integration and adaptations. Also credit to grill2010 for the upstream Account ID approach. |
| [XeSS-GPU-Motion](https://github.com/gggz114514-oss/XeSS-GPU-Motion) / [OpenCV](https://github.com/opencv/opencv) | Ported GPU DIS provider and shaders, retaining attribution and licenses for the OpenCV DIS-derived portions. |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg) / [nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) | Media demuxing, decoding, muxing and NVENC API declarations. Releases provide the actual FFmpeg patches and corresponding source. |
| [SDL](https://github.com/libsdl-org/SDL) | PC controller input, DualSense support and haptic audio output. |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | Optical flow and its D3D12 backend. |
| [Lucide](https://github.com/lucide-icons/lucide) | UI icons, retaining Lucide and Feather-derived license notices. |
| [Intel XeSS / XeLL](https://github.com/intel/xess) | Experimental XeSS frame generation and related runtime interfaces. |
| [NVIDIA DLSS](https://github.com/NVIDIA/DLSS), [RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk), [Optical Flow SDK](https://developer.nvidia.com/opticalflow-sdk), [Video Codec SDK](https://developer.nvidia.com/video-codec-sdk) | Upscaling, optical flow, encoding and enhancement interfaces. Experimental NR components have separate identities and boundaries in [Runtime Components](docs/RUNTIME_COMPONENTS_1.1.0.md); this is not vendor certification. |
| [vcpkg](https://github.com/microsoft/vcpkg) | Dependency builds, version records and license provenance. |

Remote Play also depends on **OpenSSL, Opus, json-c, libevent, miniupnpc, curl, nanopb, Jerasure and gf-complete**. Their attribution and license notices are retained in [licenses/remoteplay](licenses/remoteplay), with corresponding source supplied in Releases.

### Architecture, Implementation Ideas and Comparisons

| Project | Reference areas |
| --- | --- |
| [Magpie Experimental](https://github.com/SAOG0721/Magpie/tree/experimental) | NR / SR / FG combinations, configurable processing order, motion guidance, residual composition, resource synchronization and presentation pacing; an important practical comparison during development. |
| [OBS Studio](https://github.com/obsproject/obs-studio) | DirectShow capture, pixel formats, color metadata, buffering and window-capture behavior. |
| [DLSS5-NeuralScreen](https://github.com/perseval-BLR/DLSS5-NeuralScreen/tree/8098ccf261bedc16e4b5fe7887c51a07eb41720e) | RTX30 NR compatibility research and architecture-query behavior reference. Veyra's scoped adapter is independently implemented; the user-provided modified runtime has a separate identity and is not covered by the application's MIT license. See [local integration record](docs/RTX30_NR_AND_SAFE_DEFAULTS_2026-09-14.md). |
| [NVEnc](https://github.com/rigaya/NVEnc) / [RTXVideoProcessor](https://github.com/DrC0ns0le/RTXVideoProcessor) | Video upscaling, GPU frame resources, codecs and scheduling. FRUC was also researched; it has since been removed from Veyra. |
| [mpv](https://github.com/mpv-player/mpv) | RTX upscaling integration and video-processing approaches in a media player. |
| [video2dlssnr](https://github.com/DaniilSokolyuk/video2dlssnr) / [dlss5-nr-player](https://github.com/Zonnery/dlss5-nr-player) | Research comparisons for NR pipelines, stage order and data transfers; their code was not copied. |
| [dlss5-video-player](https://github.com/2600th/dlss5-video-player) / [dlss5-visual-enhancer](https://github.com/Merserk/dlss5-visual-enhancer) / [dlss5-infinity-studio](https://github.com/SamG-Coder/dlss5-infinity-studio) | Playback, offline enhancement, export and caching workflow references. |
| [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) / [DLSS5-Reshade-AIO](https://github.com/kibblerz/DLSS5-Reshade-AIO) / [Assassin’s Creed Odyssey DLAA](https://github.com/SAOG0721/Assassins-Creed-Odyssey-DLAA) | Research into motion/depth inputs, color transfer, NR protection regions and temporal contracts. Veyra does not load or distribute ReShade / RenoDX add-ons. |
| [Video2X](https://github.com/k4yt3x/video2x) / [RIFE](https://github.com/hzwer/ECCV2022-RIFE) | Research into video upscaling and interpolation approaches; these algorithms are not integrated. |

<details>
<summary>Deferred AMD NR research references</summary>

[DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD), [dlss5-on-amd-9070xt-porting](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting), [dlss5-image-enhancer-zluda](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda) and its [ZLUDA fork](https://github.com/RedDukeDev/ZLUDA), [dlss5-neural-amd](https://github.com/zmodelerlover/dlss5-neural-amd), and [DLSS5-AMD-Video](https://github.com/eikkapine/DLSS5-AMD-Video). These informed feasibility and performance research. AMD NR is not available in the current release.

</details>

## AMD FidelityFX SDK 2.3.0 (experimental FSR frame generation and FSR upscaling)

The player's AMD FSR backends (independent frame generation and upscaling in
the enhancement graph) are written against the public FidelityFX
API documented and shipped in the AMD FidelityFX SDK 2.3.0 (MIT licensed). The
SDK itself is **not** vendored into this source repository: it stays in the
gitignored `third_party_local/amd/FidelityFX-SDK-2.3.0` tree, and the signed
provider/loader DLLs are used from the local
`runtime_local/amd/fidelityfx/` folder like the other experimental runtimes.
Integration code (`src/gfx/FsrFgPresenter.cpp`, `src/gfx/FsrSrBackend.cpp`) is an
independent implementation of the documented API sequence and was written
against these references:

- Fixed SDK source: [GPUOpen-LibrariesAndSDKs/FidelityFX-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/60f4ea81909200d8542eca14dccb2628b763a9a3),
  commit `60f4ea81909200d8542eca14dccb2628b763a9a3` (SDK 2.3.0).
- `Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h` and
  `Kits/FidelityFX/framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp`
- `Kits/FidelityFX/docs/techniques/frame-interpolation-ml.md`
- `Kits/FidelityFX/docs/techniques/frame-interpolation-swap-chain.md`
- `Kits/FidelityFX/docs/techniques/frame-interpolation-api.md`
- `Kits/FidelityFX/upscalers/include/ffx_upscale.h` and the FSR3 upscaler
  shader contract (`ffx_fsr3upscaler_reproject.h` for the motion convention)
- `Samples/Upscalers/FidelityFX_FSR/dx12/fsrapirendermodule.cpp`

Historical negative finding: a FidelityFX
frame-generation swapchain context keeps the real DXGI swapchain alive after
`ffxDestroyContext` (measured locally). The 2026-09-30 repair removes this proxy
path from the player. `include/veyra/gfx/FsrFgPresenter.h` and
`src/gfx/FsrFgPresenter.cpp` use the documented
`FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY` flag and the
Configure → PrepareV2 → Generate sequence, with caller-owned command lists and
output textures. `EnhanceGraph`, `VideoPresenter` and `PresentSink` share the
existing bounded batch, GPU fences and native presentation path; no dummy
display or second hidden video window is created. The adapter selects a
provider version explicitly and verifies the version returned by the created
context. FSR 3.1 and FSR 4 ML are separate requests, with no implicit version
fallback. Modules retain one process reference per absolute path, while each
effect context is destroyed after its GPU consumers finish.

Version selection and API ordering were also compared with OptiScaler commit
`45a2001303ddff632e279f77aef85ceede5832cb`,
`FSRFG_Dx12.cpp` (GPL-3.0); no source code from that file was
copied. AMD algorithm source, SDK files and runtime binaries are not added to
the Veyra source repository. Changes and actual hardware evidence are in
[FSR / XeSS repair execution](docs/FG_FSR_XESS_EXECUTION_2026-09-30.md).

The FidelityFX upscale dispatch also does
not complete under D3D12 GPU-based validation (the standalone probe stalls
before its dispatch), so that validation mode is skipped for the FSR SR
configuration with an explicit log line. See
[AMD FSR frame generation integration record](docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md).

## DLSS-G 310.9.1 provider patches: DLSSG-Transfusion (2026-09-30)

SilyNoMeta/DLSSG-Transfusion, tag `v1.4.5.3-rtx20-30-40`, commit
`b56bd2deed114507ad2c88f986d90ed50ffb4639` (MIT; fork of
TonyJoaca/DLSSG-Transfusion, MIT, Copyright (c) 2026 Michael Robles), ported
into `include/veyra/ngx/DlssgTransfusion.h`, `src/ngx/DlssgTransfusion.cpp` and
`src/ngx/transfusion/` (which keeps the upstream MIT text in
`src/ngx/transfusion/LICENSE.txt`).

Ported pieces, with upstream file names:

- `patcher.cpp`: `SafeScanDlssgArchSites` / `PatchDlssgArchGates` (Blackwell
  `cmp r32, 0x1b0` gates lowered to the architecture actually present),
  `SafeFindUniqueImmediate` / `PatchDlssgMinimumArchitecture` (the unique Ada
  immediate `0x190` inside `NVSDK_NGX_GetGPUArchitecture` and the three
  `*_GetFeatureRequirements` exports), and `kNgxPatch` — the provider
  count/index validator branch (`84 d2 0f 84 03 01 00 00 be 05 00 00 00`) whose
  jz is removed, gate discovery by `PatchUniqueExecutablePattern`.
- `midpoint_fix.cpp`: `PatchProvider`. With Blackwell kernels enabled it
  rebuilds the `Kernel_EstimateIntermMvecsScatter` and
  `Kernel_BlendCandidatesFused` fatbins from their sm_120 PTX (uncompressed,
  relabelled for the target SM, so the driver JITs them instead of loading
  NVIDIA's SASS), redirects the descriptor slots that point at them, and
  retargets every remaining kernel container in place
  (`RetargetContainers`, same-length `.target` rewrite inside the LZ4 literal
  run, other images parked at arch 122). With Blackwell kernels disabled it
  applies the Ada temporal midpoint fix instead (`BuildTemporalFatbin`: the
  temporal parameter is injected and the 104 compiled-in `0f3F000000` midpoint
  multiplies become the kernel's own temporal parameter).
- `cu_module_hook.h`: interception of `NvAPI_D3D12_CreateCuModule` (id
  `0xAD1A677D`) so image kernels can be replaced before the driver compiles them.
- `network_optimizer.h`: the DL1/DL2 launch substitution (kernel ids, grids,
  blocks and fusions of the dlssg_for_sm86 0.3.5 backend, whose output is
  bit-identical to NVIDIA's; NVIDIA's weights and parameters are passed
  unchanged), plus `image_kernels.h` (Blend `st.global.v2.u32` store merge and
  the exact `Kernel_OutputPull` / `Kernel_OutputPushFine` replacements, matched
  by an FNV-1a fingerprint of NVIDIA's 310.9.1 sm_120 PTX with the `.target`
  line excluded) and `quality_fix.h` + `quality_explained_warp.h` (the
  valid-warp anti-tearing/anti-ghosting protection: Transfusion's V4 policy or,
  by default in Veyra, dlssg_for_sm86's `explained-warp` tuning).

Veyra adaptations: the module is compiled into `veyra_ngx` and applies its edits
through a recorded-write helper, so every patched byte is restored on release
with read-back verification (`memProtectionUncertain` is raised when
restoration cannot be proved); NvAPI is reached by redirecting only the
provider's own `GetProcAddress` import slot, so no Detours, trampoline or
global hook is installed; the optimized kernels are separate runtime files
under `runtime/experimental/dlssg-kernels/` (gitignored authoring copy in
`third_party_local/nvidia/dlssg-transfusion-kernels-1.4.5.3/`, extracted
read-only from the upstream release DLL), never linked in, and a missing file
set only disables the optimization; Turing, Vulkan, Streamline, the UI-assist
capture and the scatter/input-motion experiments are **not** ported.

### Optimized kernel files (`runtime/experimental/dlssg-kernels`)

29 PTX files extracted, read-only, from the upstream release binary
`DLSSG-Transfusion.dll` (release `v1.4.5.3-rtx20-30-40`, SHA256
`3C0621BE577E56D03945ACCA48C2DFC5935A8A5759C432044E4E561B16DBBAD8`): 27 RCDATA
resources 4000–4062 (the DL1/DL2 sm_86 network kernels) and the two embedded
image kernels. Upstream's `docs/PUBLIC-SOURCE.md` and `docs/OPTIMIZED-KERNELS.md`
state their provenance: they come from the `dlssg_for_sm86` 0.3.5 backend,
which publishes no source and no license for them, and they derive from
NVIDIA's DLSS-G network kernels; SilyNoMeta publishes them only as compiled
resources and explicitly disclaims the right to license them. **No license is
granted for these files by either author.** The 2026-09-30 user decision
accepts that risk and ships them the same way as the other experimental runtime
binaries: as separate runtime files, listed by name, size and SHA-256 in the
package manifest and in `release-runtime-manifest.json`, never committed to
source control. Each file is verified by FNV-1a fingerprint and per-launch
shape checks before use; the provider fingerprint guard (the
`Kernel_OutputPull` 310.9.1 match) is what enables the substitution at all, so
a different provider build leaves the kernels unused. Removing the folder
disables the optimization without affecting playback.

Machine evidence (RTX 5070, 2026-09-30): the patched provider applies and rolls
back cleanly, the harness reports `exact=true` and `networkReady=true`, the FG
truth harness (translation, direction, scene cut) passes with the patches
installed, and the product smoke path reports 2400+ generated frames with
`failed=false`. RTX 40 and RTX 30 hardware behaviour is unverified; the valid
warp and the optimized kernels only change output when both are active
together, which is recorded as an open question in
`docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md`.

## RTX 40 series DLSS multi-frame unlock (ported)

Two upstream projects are ported into `include/veyra/ngx/AdaMfgUnlock.h` /
`src/ngx/AdaMfgUnlock.cpp`; both are MIT-licensed and are attributed separately.

### ImDreamt/MFGAdaUnlock-RenoDx (initial port)

https://github.com/ImDreamt/MFGAdaUnlock-RenoDx , commit
`a8aa0d9289471facc1e32013d17fd9c1195bc2cd` (MIT). The initial port brought:

- the two architecture compares (`0x1b0` → `0x190`);
- the kernel PTX midpoint correction (inject the temporal parameter, replace the
  104 compiled-in `0.5` multiplies, truncate the fatbin so the driver JITs the
  corrected code instead of loading the precompiled cubin);
- the `dlfg_kernel` descriptor redirect, all process-memory only.

### dashdogy/RTX40MFG-Unlock v1.3.3 (completion port, 2026-09-16)

https://github.com/dashdogy/RTX40MFG-Unlock , commit
`33b41835dc39c5d8ab1ef93efb2449be31139c09` (v1.3.3, MIT; source tree studied in
the gitignored `third_party_local/community/RTX40MFG-Unlock`). Ported pieces:

- the provider count/index validator patch from `source/native/ngx_mfg_gate.h`
  (pattern `84 d2 0f 84 03 01 00 00 be 05 00 00 00`, near-jz head `0f 84` →
  `eb 04`, whose entry condition gates the accepted generated-frame ceiling on
  the runtime-detected architecture). This is the documented mechanism behind
  upstream's v1.3.3 fix for "MFG getting stuck at 2x"; every count, index and
  profile-limit check after the branch is retained;
- the three-stage SHA-256 identity chain from `source/native/midpoint_fix.cpp`
  legacy profile: source fatbin (98408 B) `5A8E0284…`, decompressed PTX
  (99362 B) `46C05996…`, rebuilt fatbin (127200 B) `19FB3CD5…`, plus the
  sm_120/sm_89 entry field checks (28024/28017/90490 and 30384/30379/99362).
  Veyra reproduced the rebuilt-fatbin digest byte-for-byte from its own
  `runtime_local/nvidia/nvngx_dlssg.dll` 310.7 before porting; the digest is now
  a publication gate, so a build that does not reproduce the upstream-verified
  byte stream is refused and fully rolled back.

Veyra adaptations and additions: hard architecture gating (never applied on
Blackwell, so RTX 50 keeps its native path); the count/index site must match
uniquely, sit at an even address and branch to the audited `cmp r8d, 1` target
before any write; all edits happen in the mapped image only (disk file is never
touched, re-signed or renamed); read-back verification and full rollback when
the session ends. The rollback path re-opens page protection before restoring
bytes — negative testing during this port caught that the original Veyra
rollback path wrote into read-only pages and would have crashed the process on
a refused kernel fix. The upstream Streamline-side hardware flip-metering /
pacing workaround and per-title `nvidia_mfg_policy` ceilings are **not
applicable**: Veyra calls NGX directly and does not load Streamline, ReShade or
any RenoDX add-on.

Evidence on this machine (RTX 5070, structure and patch mechanics only):
`logs/fsr/dlssg-unlock-scan.log`, `logs/fsr/dlssg-unlock-applytest.log`;
delivery gate `logs/delivery/d0a266a1e549402ca26c2c8ec8d27022/result.json`.
Ada behaviour still has to be verified on RTX 40 hardware.

### dashdogy/RTX40MFG-Unlock v1.3.3 — RTX 30 (sm_86) Ampere path (2026-09-16)

Same upstream (MIT, commit `33b41835dc39c5d8ab1ef93efb2449be31139c09`),
`source/native/ampere_gpu.cpp` (`FindUniqueSm89Ptx`, `BuildAmpereSm86Fatbin`),
`ampere_cuda_program.cpp` (driver preflight intent) and `ampere_policy.h`
(sm_86 adapter contract), ported into
`include/veyra/ngx/AmpereMfgUnlock.h` / `src/ngx/AmpereMfgUnlock.cpp`.

2026-09-18 correction: the current port rebuilds only the 25 registered DLFG
programs and the hash-identified auxiliary font, matching upstream's scope.
It preserves the 38 network and other five auxiliary fatbins. The previous
all-69 rewrite and global Blackwell architecture spoof are superseded.
Real NVAPI architecture is preserved for provider hardware selection; scoped
capability policies and two ceiling comparisons are adapted separately.
Both registration pointer and supplied length are published. The temporal
program retains the upstream midpoint correction and unsupported PTX checks.

Additional source from the same MIT commit: `ampere_font_program.inl`
(`PrepareFont`, `FontPageProtectionMatches`, `RestoreFontPage`, `WriteFont`)
and `ampere_cuda_program.cpp` (adapter LUID matching and function preflight).
Veyra adapts these into `src/ngx/AmpereMfgUnlock.cpp`: rebuild font PTX from
the mapped provider, preserve its address, save/restore page bytes and actual
protection, accept privatized WRITECOPY pages only with working-set proof.
No upstream native font payload is embedded. The 26 rebuilt programs must
pass `cuModuleLoadData`, `cuModuleGetFunction` and available `cuFuncLoad` on
the D3D12 adapter's CUDA device, with the caller's context restored.
`src/pipeline/EnhanceGraph.cpp` passes that LUID and defaults to no spoof.
All modifications are in process memory; the provider DLL on disk is untouched.

Current evidence: `out/logs/3060-r4-real-arch-lifecycle-final.stdout.log`
passes 2X through 6X and repeated patch restoration on RTX5070, with 26/26
preflight and actual NGX Evaluate. RTX3060 execution remains unverified;
CUDA preflight on RTX5070 is not proof of target hardware support.

Historical evidence for the superseded all-69 implementation (RTX5070 only):
`veyra_dlssg_ampere_probe` — scan (runs=8, 200 slots, 25+38+6 fatbins, 44 lea,
2 gates, temporal unique), apply (applied=1, preflight=69/69, readBack=1,
restored=1), delivery gate
`logs/delivery/548a606d92484286806f272d4744d2a7/result.json`.

## NGX compatibility session port (2026-09-17)

Source: dashdogy/RTX40MFG-Unlock, fixed commit
`33b41835dc39c5d8ab1ef93efb2449be31139c09`, MIT, copyright 2026 Michael Robles.
Full permission notice: `src/ngx/compat/RTX40MFG_LICENSE.txt`.

- `source/native/ampere_backend.cpp` discovery and scoped startup/Create logic:
  adapted into `src/ngx/compat/NgxDiscovery.h`, `src/ngx/FgCompatibilitySession.cpp`
  and `include/veyra/ngx/FgCompatibilitySession.h`. Veyra substitutes its direct
  NGX calls and owned D3D12 resources for upstream wrapper/swapchain hooks;
  adds an exclusive session lease, absolute-path/module ownership, adapter
  and texture identity, and restoration before releasing module references.
  Follow-up on 2026-09-18: extend the verified Ampere metadata scope to the
  direct NGX Init call in `src/pipeline/EnhanceGraph.cpp`, matching upstream's
  startup coverage before capability results can be cached. Restore on both
  successful and failed Init; reject capabilities before successful Init.
- `source/native/ampere_patterns.h`: metadata/Create patterns only, extracted
  into `src/ngx/compat/NgxPatterns.h`; Streamline patterns omitted.
- `source/native/protected_pointer.h`: retained implementation in
  `src/ngx/compat/protected_pointer.h`, attribution comment added.
- `src/ngx/compat/RestoreMemory.h`: Veyra helper using the above page-query
  and restoration routines; accepts Windows image-page copy-on-write protection,
  verifies writes/cache flush/protection and retains a process failure latch.
  Ada/Ampere program publications now retain rollback records before each
  checked write. Unproven rollback blocks later NGX sessions.
- Upstream MinHook HDE64 decoder: `src/ngx/compat/hde/{hde64.c,hde64.h,table64.h,pstdint.h}`.
  BSD copyright/terms retained in `src/ngx/compat/HDE_LICENSE.txt` and source
  headers. Compiled as C++; local `c` initialized to zero for MSVC /WX.

The session is used only for the requested Ada/Ampere compatibility path.
Native RTX50 does not install these patches. Forced-path RTX5070 diagnostics
are not evidence of RTX30/40 hardware compatibility.
`src/engine/FgCompatibilityProbe.cpp` is Veyra's own bounded child-process
Create/Evaluate preflight, with inherited mapping, process job, exact adapter
selection and cancellation; it is not upstream wrapper code or a quality gate.


## Media Foundation encoder（系统硬件编码路径，移植自 FFmpeg mfenc.c）

Source: FFmpeg n9.0.1 `libavcodec/mfenc.c` 与 `libavcodec/mf_utils.c`
（LGPL-2.1-or-later；本地源码 `C:\veyra-deps\ffmpeg-ps5-slices-source`）。
`src/sink/MfVideoEncoder.cpp` 移植了其中被证明可用的部分：`MFTEnumEx` 硬件枚举与激活、
`MF_TRANSFORM_ASYNC_UNLOCK` 异步解锁、输入/输出媒体类型协商、`ICodecAPI`
（码率/低延迟/GOP/B 帧/质量）设置、事件驱动的 `ProcessInput`/`ProcessOutput` 循环、
`MFT_MESSAGE_COMMAND_DRAIN` 收尾，以及 `MF_MT_MPEG_SEQUENCE_HEADER`（SPS/PPS）
作为封装 extradata。

Veyra 的差异（非上游代码）：输入不是 AVFrame，而是 Veyra 自己 D3D12 图渲染出的 NV12
经 readback 打包；枚举结果按当前显卡厂商优选并对每个候选做完整契约协商（一台机器上可能
同时装着多家驱动的编码 MFT）；首个样本与 GOP 边界强制关键帧；码流直接交给 Veyra 的
MP4 封装路径。MFT 本身属于 Windows 与显卡驱动，不随包分发。FFmpeg 采用
LGPL-2.1-or-later，与 Veyra 的 GPLv3 兼容；上游作者归属见上。

## RTX Video HDR (local integration, 2026-09-18)

`src/ngx/TrueHdrBackend.cpp` independently implements the NVIDIA RTX Video SDK
1.1.0 D3D12 parameter contract. Parameter defaults and behavior were compared with
rigaya/NVEnc, MIT, commit `4cb3101f451e3f932334c994323e743808502995`:
https://github.com/rigaya/NVEnc/tree/4cb3101f451e3f932334c994323e743808502995 .
No NVEnc CUDA implementation or NVIDIA SDK source is vendored here.

Local runtime: `nvngx_truehdr.dll`, 1.1.0.0, 3955752 bytes, NVIDIA Authenticode
Valid, SHA256 `9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B`.
This runtime retains the NVIDIA RTX Video SDK license; it is not relicensed under
Veyra's GPL. SDK/license and runtime remain external. This change does not publish
or grant redistribution of a new runtime pack.

## NVAPI Reflex ABI reference (2026-09-18)

Source: https://github.com/NVIDIA/nvapi/tree/87dca625e83fd89a983e19b904e5f3a580da90d2
(`nvapi.h`, `nvapi_interface.h`, `nvapi_lite_common.h`), MIT.
`include/veyra/gfx/ReflexSession.h` contains minimal renamed ABI declarations for
SetSleepMode v1 and SetLatencyMarker v1, and QueryInterface IDs for initialization,
unloading, sleep and markers. Adaptations: independent RAII device lifetime,
absolute system-driver loading, return-code logging, successful call counts and
explicit disable. No complete SDK header or NVIDIA runtime is committed or bundled.
The driver is supplied by Windows/NVIDIA. Frame markers delimit Veyra enhancement
and presentation, not game input or physical display latency. Generated-frame
Reflex integration is not enabled; DLSS retains its selected multiplier.

Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Win32CaptureSample (screen capture)

Source: https://github.com/robmikh/Win32CaptureSample
Fixed commit: `49fefe79fd9b11025f0b5eb91783a98888516070`.
License: MIT, Copyright (c) 2019 Robert Mikhayelyan.
Full license: `licenses/WIN32_CAPTURE_SAMPLE_MIT.txt`.
`src/source/ScreenCaptureSource.cpp` adapts `SimpleCapture.cpp/.h` free-threaded
WGC session/frame-pool creation and resize lifecycle. Veyra replaces the
upstream swapchain with bounded leased D3D11/D3D12 shared textures and fences,
latest-frame polling, crop, explicit scRGB color, timestamps and shared graph
ingress. DXGI duplication is a Veyra addition. Screen capture is video-only.

## dav1d (1.3.0 AV1 playback)

FFmpeg dynamically links dav1d 1.5.4 from the pinned local vcpkg build. The portable package includes its complete aggregated copyright/license text in `licenses/DAV1D-COPYRIGHT.txt` and provenance in `licenses/DAV1D-SPDX.json`. The FFmpeg corresponding-source ZIP includes dav1d source and its vcpkg port. Upstream: https://code.videolan.org/videolan/dav1d . License set recorded by the build: Apache-2.0, BSD-2-Clause, ISC and MIT; retain all notices supplied with the source.

## Moonlight / GameStream streaming core (2026-10-01)

The optional `VEYRA_ENABLE_MOONLIGHT` build compiles **moonlight-common-c** (GPL-3.0,
https://github.com/moonlight-stream/moonlight-common-c) at commit
`f900dd4767759c7b9d0e93bcea666b55c69ea62f` (2026-09-26) together with its two pinned submodules:
ENet (MIT, https://github.com/cgutman/enet, `aca87840b57f045a1f7f9299e4b1b9b8e2a5e2f1`) and
nanors (MIT, https://github.com/sleepybishop/nanors, `b1e3c22ca0cdc0bb83e3cd6ed1a2fc77869ed99a`).
The build refuses any other checkout (`scripts/moonlight/verify-moonlight-stage.py`, pins in
`scripts/moonlight/dependency-lock.json`). No Moonlight or Sunshine binary is committed or bundled.

The host HTTP(S) client, the pairing handshake and the serverinfo / applist / launch handling in
`src/moonlight/` and `include/veyra/moonlight/` are a port of **moonlight-qt** (GPL-3.0,
https://github.com/moonlight-stream/moonlight-qt, commit `8369d1a0e11b999d4d1598f62ca5f6dea49602fb`):
`app/backend/nvhttp.cpp`, `nvpairingmanager.cpp`, `identitymanager.cpp`, `nvcomputer.cpp` and the default
bitrate table of `app/streaming/session.cpp`, with the Qt types replaced by standard C++, OpenSSL and Winsock.
Nothing from the moonlight-qt tree is compiled. The OpenSSL (Apache-2.0) and Opus (BSD-3-Clause) libraries come
from the same vcpkg tree as the PS5 build. Veyra is GPL-3.0, which is compatible with all of the above.
License texts: `licenses/moonlight/ENET_MIT.txt`, `licenses/moonlight/NANORS_MIT.txt`; the GPL-3.0 text is Veyra's own `LICENSE`.

The streamed machine runs **Sunshine** (GPL-3.0, https://github.com/LizardByte/Sunshine), which users install
themselves; Veyra neither ships nor downloads it.
